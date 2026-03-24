#pragma once
#include "kh2coop/Types.hpp"

#include <cstdint>
#include <string>

struct _ENetPeer; // forward-declare without pulling in enet headers

namespace kh2coop {

// ---------------------------------------------------------------------------
// Connection lifecycle
// ---------------------------------------------------------------------------
enum class PeerStatus : std::uint8_t {
    PendingVersion, // connected, waiting for version handshake
    Verified,       // version OK, slot assigned, playing
    Disconnected,   // cleanly left or timed out
};

// ---------------------------------------------------------------------------
// Per-peer state tracked by the session host.
// ---------------------------------------------------------------------------
struct PeerState {
    _ENetPeer* enetPeer{nullptr};

    // Identity
    std::string peerId;
    SlotType assignedSlot{SlotType::Player};
    PeerStatus status{PeerStatus::PendingVersion};

    // Version gate — set on first message from peer
    std::string gameBuild;
    std::string modHash;

    // Latest input received from this peer
    InputFrame lastInput{};

    // Heartbeat tracking
    std::uint64_t lastHeartbeatMs{0};
    std::uint32_t roundTripMs{0};
};

} // namespace kh2coop
