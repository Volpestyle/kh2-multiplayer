#pragma once
// ============================================================================
// WorldPump — the runtime's half of the WorldBridge: moves encoded world
// packets from the DLL to the relay, and from the relay to the DLL.
//
// DLL -> relay: only world packet types pass (anything else the DLL emits is
// dropped and counted); EnemyHp and StateHash go unreliable, everything
// else reliable.
// Host-only enforcement stays in the relay, so a client DLL that emits host
// messages is harmless.
// ============================================================================

#include "kh2coop/Codec.hpp"
#include "kh2coop/NetworkClient.hpp"
#include "kh2coop/WorldBridge.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace kh2coop {

struct WorldPumpStats {
    std::uint64_t toNet {0};
    std::uint64_t toDll {0};
    std::uint64_t rejected {0};     // non-world or malformed packets from the DLL
    std::uint64_t dllRingFull {0};  // DLL ring backpressure observations
    std::uint64_t deferred {0};     // retained until the game bridge can accept them
    std::uint64_t inboxOverflow {0}; // queue overflow or an undeliverable record
};

// Drains the DLL's outgoing ring into the network client (call every tick).
inline void pumpDllToNet(WorldBridge& bridge, NetworkClient& net, WorldPumpStats& stats) {
    std::vector<std::uint8_t> packet;
    while (bridge.ReceiveFromDll(packet)) {
        try {
            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            const auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
            if (!isWorldPacket(type)) {
                ++stats.rejected;
                continue;
            }
            const bool periodic =
                type == PacketType::EnemyHp || type == PacketType::StateHash;
            net.sendRawPacket(packet, !periodic);
            ++stats.toNet;
        } catch (const std::exception&) {
            ++stats.rejected;
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

    [[nodiscard]] std::size_t PendingCount() const { return pending_.size(); }

private:
    std::deque<std::vector<std::uint8_t>> pending_;
    std::size_t bytes_ = 0;
};

} // namespace kh2coop
