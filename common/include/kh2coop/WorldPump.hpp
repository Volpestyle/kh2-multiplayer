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
#include <vector>

namespace kh2coop {

struct WorldPumpStats {
    std::uint64_t toNet {0};
    std::uint64_t toDll {0};
    std::uint64_t rejected {0};     // non-world or malformed packets from the DLL
    std::uint64_t dllRingFull {0};  // relay packets the DLL ring had no room for
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

} // namespace kh2coop
