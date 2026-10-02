#pragma once
#include "kh2coop/Protocol.hpp"
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

    // Last room this peer reported arriving in (TransitionAck)
    std::uint32_t ackEpoch{0};
    std::uint16_t ackWorldId{0};
    std::uint16_t ackRoomId{0};
    bool ackArrived{false};

    // Desync detection: latest state hash and how many consecutive
    // comparisons with the host disagreed (and on what).
    bool hasHash{false};
    StateHash lastHash{};
    std::uint32_t mismatchStreak{0};
    std::uint8_t reportedFields{0};

    // Heartbeat tracking
    std::uint64_t lastHeartbeatMs{0};
    std::uint32_t roundTripMs{0};
};

} // namespace kh2coop
