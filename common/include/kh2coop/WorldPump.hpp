#pragma once
// ============================================================================
// WorldPump — the runtime's half of the WorldBridge: moves encoded world
// packets from the DLL to the relay, and from the relay to the DLL.
//
// DLL -> relay: only world packet types pass (anything else the DLL emits is
// dropped and counted); EnemyHp, EnemyMotion, StateHash and activation challenges/responses
// go unreliable, everything else reliable.
// Host-only enforcement stays in the relay, so a client DLL that emits host
// messages is harmless.
// ============================================================================

#include "kh2coop/RcRefusalPacketTrace.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/PopulationCutJson.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/WorldBridge.hpp"

#include <cstdint>
#include <cstdio>
#include <deque>
#include <stdexcept>
#include <vector>

namespace kh2coop {

struct WorldPumpStats {
    std::uint64_t toNet {0};
    std::uint64_t toDll {0};
    std::uint64_t rejected {0};     // non-world or malformed packets from the DLL
    std::uint64_t dllRingFull {0};  // DLL ring backpressure observations
    std::uint64_t deferred {0};     // retained until the game bridge can accept them
    std::uint64_t inboxOverflow {0}; // queue overflow or an undeliverable record
    std::uint64_t ephemeralDropped {0}; // no durable backlog for activation leases
    std::uint64_t retiredOutgoing {0}; // unavailable or retired DLL producer
};

using WorldPumpObserver = void(*)(void*, const std::vector<std::uint8_t>&, const ProducerWorldContext&, bool) noexcept;

// Drains the DLL's outgoing ring into the network client (call every tick).
inline void pumpDllToNet(WorldBridge& bridge, NetworkClient& net, WorldPumpStats& stats,
                         WorldPumpObserver observer = nullptr, void* observerContext = nullptr,
                         bool recoveryOnly = false) {
    std::uint8_t operatorMask = 0;
    std::uint64_t operatorHost = 0;
    ProducerWorldContext operatorContext;
    if (bridge.ReceiveResyncCommand(operatorMask, operatorContext, operatorHost)) {
        if (!net.ready() || !operatorContext.generation ||
            operatorContext.generation != bridge.SessionGeneration() ||
            !operatorContext.deliverySerial || operatorContext.deliverySerial != bridge.DeliverySerial() ||
            operatorContext.deliverySerial != net.deliverySerial() ||
            !operatorHost || operatorHost != bridge.ConnectionId(0) || bridge.LocalSlot() != 0 ||
            bridge.GetPuppetAuthorityMode() != PuppetAuthorityMode::Network) {
            net.recordResyncCallerRejection(ResyncRequestOrigin::OperatorMailbox, operatorMask);
            ++stats.retiredOutgoing;
        } else if (net.requestWorldResync(operatorMask, nullptr, ResyncRequestOrigin::OperatorMailbox)) {
            ++stats.toNet;
        } else {
            ++stats.rejected;
        }
    }
    std::vector<std::uint8_t> packet;
    ProducerWorldContext context;
    // VUH-1504 hop log (diagnostic only, bounded): the DLL's ReviveRequest into NetworkClient.
    static unsigned reviveHopLogs = 0, remoteHitHopLogs = 0, targetAuthorityHopLogs = 0;
    const auto reviveHop = [](const std::vector<std::uint8_t>& p, const char* what) {
        if (!p.empty() && p.front() == static_cast<std::uint8_t>(PacketType::ReviveRequest) && reviveHopLogs < 32) {
            ++reviveHopLogs;
            std::printf("[revive-hop] runtime dll->net %s bytes=%zu\n", what, p.size());
            std::fflush(stdout);
        } else if (!p.empty() && p.front() == static_cast<std::uint8_t>(PacketType::RemoteHit) && remoteHitHopLogs < 32) {
            // VUH-1515 hop log (diagnostic only, bounded): the host DLL's RemoteHit into NetworkClient.
            ++remoteHitHopLogs;
            std::printf("[remote-hit] runtime dll->net %s bytes=%zu\n", what, p.size());
            std::fflush(stdout);
        } else if (!p.empty() && p.front() == static_cast<std::uint8_t>(PacketType::TargetAuthority) && targetAuthorityHopLogs < 32) {
            // VUH-1515 hop log (diagnostic only, bounded): the host DLL's TargetAuthority into NetworkClient.
            ++targetAuthorityHopLogs;
            std::printf("[target-authority] runtime dll->net %s bytes=%zu\n", what, p.size());
            std::fflush(stdout);
        }
    };
    while (bridge.ReceiveFromDll(packet, context)) {
        auto rcTrace=rcdiag::Packet(packet);rcdiag::ProducerScope(rcTrace,context);
        rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,"received","dll-ring-pop",rcTrace);
        const char* rcWhy="unknown";
        if (rcdiag::Reject(!net.ready(),"net-not-ready",rcWhy) ||
            rcdiag::Reject(!context.generation,"generation-zero",rcWhy) ||
            rcdiag::Reject(context.generation!=bridge.SessionGeneration(),"bridge-generation",rcWhy) ||
            rcdiag::Reject(!context.deliverySerial,"delivery-zero",rcWhy) ||
            rcdiag::Reject(context.deliverySerial!=bridge.DeliverySerial(),"bridge-delivery",rcWhy) ||
            rcdiag::Reject(context.deliverySerial!=net.deliverySerial(),"net-delivery",rcWhy)) {
            rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,"retired",rcWhy,rcTrace);
            reviveHop(packet, !net.ready() ? "retired:net-not-ready" : "retired:context-mismatch");
            ++stats.retiredOutgoing;
            continue;
        }
        try {
            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            const auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
            if (packet.size() != size + 3) {
                rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,"refused","framing",rcTrace);
                ++stats.rejected;
                continue;
            }
            if (recoveryOnly && type != PacketType::ResyncAck && type != PacketType::ResyncResult &&
                type != PacketType::NativeResyncSnapshot && type != PacketType::TransitionAck &&
                type != PacketType::SeatState) {
                rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,"retired","recovery-quarantined",rcTrace);
                ++stats.retiredOutgoing;
                continue;
            }
            bool submitted = false;
            if (type == PacketType::LocalResyncCommand) {
                // Operator commands have their own mailbox. Accepting them
                // here would invite a second producer on the native SPSC ring.
                ++stats.rejected;
                continue;
            } else if (type == PacketType::NativeResyncSnapshot) {
                ResyncBegin begin;
                ResyncSnapshot snapshot;
                decodeNativeResyncSnapshot(packet, begin, snapshot);
                submitted = net.sendResyncCapture(begin, snapshot, context);
            } else if (type == PacketType::ResyncAck) {
                ByteReader reader(payload, size);
                ResyncAck ack;
                read(reader, ack);
                submitted = reader.atEnd() && net.sendResyncAck(ack, context);
            } else if (type == PacketType::ResyncResult) {
                ByteReader reader(payload, size);
                ResyncResult result;
                read(reader, result);
                submitted = reader.atEnd() && net.sendResyncFailure(result, context);
            } else if (!isScopedWorldPacket(type)) {
                ++stats.rejected;
                continue;
            } else {
                if (isEphemeralWorldPacket(type)) validateActivationPacket(packet);
                const bool periodic = type == PacketType::EnemyHp || type == PacketType::EnemyMotion ||
                                      type == PacketType::FieldObjects || type == PacketType::MissionState ||
                                      type == PacketType::StateHash ||
                                      isEphemeralWorldPacket(type);
                rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,"submit-enter","sendNativeWorld",rcTrace);
                submitted = net.sendNativeWorld(packet, context, !periodic);
                rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,submitted?"submitted":"submit-false","sendNativeWorld-returned",rcTrace);
                if(submitted && type==PacketType::PopulationCut && net.worldBinding()) {
                    ByteReader cutReader(payload,size);PopulationCut cut;read(cutReader,cut);
                    const auto receipt=PopulationCutJson(cut,net.worldBinding()->sessionId);
                    std::printf("[enemy-pop] occurrence-cut-json %s\n",receipt.c_str());
                }
                reviveHop(packet, submitted ? "submitted" : "rejected:sendNativeWorld");
            }
            if (observer) observer(observerContext, packet, context, submitted);
            if (submitted) ++stats.toNet;
            else {
                ++stats.rejected;
                if (net.pendingResync() && type != PacketType::LocalResyncCommand)
                    net.failWorldResync(ResyncResultReason::Overflow, "native bridge submission failed");
            }
        } catch (const std::exception&) {
            rcdiag::Stdout(rcdiag::Stage::RuntimeOutgoing,"refused","decode-or-submit-exception",rcTrace);
            ++stats.rejected;
            if (net.pendingResync())
                net.failWorldResync(ResyncResultReason::InvalidSnapshot, "malformed native bridge record");
        }
    }
}

// For ClientCallbacks::onWorldPacket: hands a relay packet to the DLL.
inline void forwardToDll(WorldBridge& bridge, const std::vector<std::uint8_t>& packet,
                         WorldPumpStats& stats) {
    if (bridge.IsOpen() && bridge.SendToDll(packet)) ++stats.toDll;
    else ++stats.dllRingFull;
}

// Network callbacks can run before KH2 attaches, including the relay's one-time
// late-join snapshot. Keep that ordered snapshot until the bridge is open, and
// retry a full DLL ring rather than silently losing a reliable transition.
// All methods run on the runtime's network/game-pump thread.
class WorldInbox {
public:
    static constexpr std::size_t kMaxBytes = 4u * WORLD_RING_BYTES;

    bool Receive(WorldBridge& bridge, const std::vector<std::uint8_t>& packet,
                 WorldPumpStats& stats) {
        const auto rcTrace=rcdiag::Packet(packet);
        rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"received","inbox-receive",rcTrace);
        // A lease arriving behind a reliable backlog is expendable. Never let
        // it bypass that backlog or take space in its durable pending queue.
        std::vector<std::uint8_t> inner;
        if (!packet.empty() && packet[0] == static_cast<std::uint8_t>(PacketType::WorldEnvelope)) {
            try {
                const std::uint8_t* payload = nullptr;
                std::size_t size = 0;
                decodePacketHeader(packet.data(), packet.size(), payload, size);
                if (packet.size() != size + 3) throw std::runtime_error("extra envelope bytes");
                ByteReader reader(payload, size);
                WorldEnvelope envelope;
                read(reader, envelope);
                if (!reader.atEnd()) throw std::runtime_error("extra envelope payload");
                inner = std::move(envelope.packet);
            } catch (const std::exception&) {
                rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"refused","envelope-decode",rcTrace);
                ++stats.rejected;
                return false;
            }
        }
        const auto& effective = inner.empty() ? packet : inner;
        if (!effective.empty() && isEphemeralWorldPacket(static_cast<PacketType>(effective[0]))) {
            try {
                validateActivationPacket(effective);
            } catch (const std::exception&) {
                ++stats.rejected;
                return true;
            }
            Flush(bridge, stats);
            if (pending_.empty() && bridge.IsOpen() && bridge.SendToDll(packet)) {
                ++stats.toDll;
            } else {
                ++stats.ephemeralDropped;
            }
            return true; // intentional drop is not reliable-world corruption
        }
        if (packet.empty() || packet.size() > WORLD_RING_BYTES / 2u - 4u) {
            rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"refused","packet-size",rcTrace);
            ++stats.inboxOverflow;
            return false;
        }
        Flush(bridge, stats);
        if (pending_.empty() && bridge.IsOpen() && bridge.SendToDll(packet)) {
            rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"ring-enqueued","immediate-push",rcTrace);
            ++stats.toDll;
            return true;
        }
        if (packet.size() > kMaxBytes - bytes_) {
            rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"refused","pending-overflow",rcTrace);
            ++stats.inboxOverflow;
            return false;
        }
        pending_.push_back(packet);
        bytes_ += packet.size();
        ++stats.deferred;
        rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"deferred","retained-not-delivered",rcTrace);
        return true;
    }

    void Flush(WorldBridge& bridge, WorldPumpStats& stats) {
        if (!bridge.IsOpen()) return;
        while (!pending_.empty()) {
            const auto rcTrace=rcdiag::Packet(pending_.front());
            if (!bridge.SendToDll(pending_.front())) {
                ++stats.dllRingFull;
                return;
            }
            rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"ring-enqueued","flush-push",rcTrace);
            bytes_ -= pending_.front().size();
            pending_.pop_front();
            ++stats.toDll;
        }
    }

    void Clear() {
        for(const auto& packet:pending_)
            rcdiag::Stdout(rcdiag::Stage::RuntimeInbox,"retired","inbox-clear",rcdiag::Packet(packet));
        pending_.clear();
        bytes_ = 0;
    }

    // Attach binds the queued current session to this mapping's generation.
    // Clear() at each session boundary guarantees the retained packets belong
    // to that session. Replace only local markers; preserve its first full SAVE.
    bool BindSessionGeneration(std::uint32_t generation, WorldPumpStats& stats,
                               std::uint64_t deliverySerial = 0) {
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (!it->empty() && (*it)[0] == static_cast<std::uint8_t>(PacketType::SessionState)) {
                bytes_ -= it->size();
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        auto reset = encodeWorldSessionReset(generation, deliverySerial);
        if (reset.size() > kMaxBytes - bytes_) {
            ++stats.inboxOverflow;
            return false;
        }
        bytes_ += reset.size();
        pending_.push_front(std::move(reset));
        return true;
    }

    [[nodiscard]] std::size_t PendingCount() const { return pending_.size(); }

private:
    std::deque<std::vector<std::uint8_t>> pending_;
    std::size_t bytes_ = 0;
};

} // namespace kh2coop
