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

#include "kh2coop/Codec.hpp"
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

// Drains the DLL's outgoing ring into the network client (call every tick).
inline void pumpDllToNet(WorldBridge& bridge, NetworkClient& net, WorldPumpStats& stats) {
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
        if (!net.ready() || !context.generation || context.generation != bridge.SessionGeneration() ||
            !context.deliverySerial || context.deliverySerial != bridge.DeliverySerial() ||
            context.deliverySerial != net.deliverySerial()) {
            reviveHop(packet, !net.ready() ? "retired:net-not-ready" : "retired:context-mismatch");
            ++stats.retiredOutgoing;
            continue;
        }
        try {
            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            const auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
            if (packet.size() != size + 3) {
                ++stats.rejected;
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
                                      type == PacketType::StateHash ||
                                      isEphemeralWorldPacket(type);
                submitted = net.sendNativeWorld(packet, context, !periodic);
                reviveHop(packet, submitted ? "submitted" : "rejected:sendNativeWorld");
            }
            if (submitted) ++stats.toNet;
            else {
                ++stats.rejected;
                if (net.pendingResync() && type != PacketType::LocalResyncCommand)
                    net.failWorldResync(ResyncResultReason::Overflow, "native bridge submission failed");
            }
        } catch (const std::exception&) {
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
            ++stats.inboxOverflow;
            return false;
        }
        Flush(bridge, stats);
        if (pending_.empty() && bridge.IsOpen() && bridge.SendToDll(packet)) {
            ++stats.toDll;
            return true;
        }
        if (packet.size() > kMaxBytes - bytes_) {
            ++stats.inboxOverflow;
            return false;
        }
        pending_.push_back(packet);
        bytes_ += packet.size();
        ++stats.deferred;
        return true;
    }

    void Flush(WorldBridge& bridge, WorldPumpStats& stats) {
        if (!bridge.IsOpen()) return;
        while (!pending_.empty()) {
            if (!bridge.SendToDll(pending_.front())) {
                ++stats.dllRingFull;
                return;
            }
            bytes_ -= pending_.front().size();
            pending_.pop_front();
            ++stats.toDll;
        }
    }

    void Clear() {
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
