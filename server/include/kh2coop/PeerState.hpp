#pragma once
#include "kh2coop/Protocol.hpp"
#include "kh2coop/Types.hpp"
#include "kh2coop/Transport.hpp"

#include <array>
#include <cstdint>
#include <string>


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
    TransportPeer* transportPeer{nullptr};

    // Identity
    std::string peerId;
    SlotType assignedSlot{SlotType::Player};
    PeerStatus status{PeerStatus::PendingVersion};
    std::uint64_t connectionId{0};
    std::uint64_t deliverySerial{1};
    bool worldQuarantined{false};
    // VUH-2640 Leg A: quarantined as an admitted boss sit-out. Material stays fenced, but SeatState (presence and
    // profile) crosses both ways: the arrival load into a prologue arena waits for the seats' profile agreement.
    bool sitOutPresence{false};
    std::uint64_t hostSourceFloor{0};
    std::uint32_t lastHitClaimSeq{0}; // accepted claims, connection-wide (not room-wide)
    std::uint64_t lastScriptReactionCompletionSeq{};
    std::uint32_t lastStageEndSeq{0}; // VUH-2555: accepted stage-end reports, connection-wide

    std::uint64_t lastReviveSeq{0}, revivedEpisode{0};
    std::uint32_t lastSeatSeq{0}; // VUH-2464 SeatState: strictly increasing per verified connection
    std::uint64_t lastRemoteHitSeq{0}; // VUH-1515 host-minted RemoteHit, connection-wide
    std::uint64_t lastTargetAuthoritySeq{0}; // VUH-1515 host-minted TargetAuthority, connection-wide
    std::uint64_t lastScriptCallSeq{0}; // VUH-2252 host-minted ScriptCall, connection-wide
    AvatarState reviveAvatar{};
    std::uint64_t reviveAvatarMs{0};
    std::uint32_t reviveAvatarSeq{0};

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
    // fresh client observations disagreed with the host (and on what).
    bool hasHash{false};
    StateHash lastHash{};
    std::uint64_t hashReceiptSeq{0}, hashReceiptMs{0}; // relay receipts, NOT native frame IDs
    std::uint32_t mismatchStreak{0};
    bool mismatchPopulationKnown{false};
    bool mismatchPopulationEqual{false};
    bool mismatchPopulationChanged{false};
    std::uint8_t reportedFields{0};
    std::uint8_t mismatchFields{0};
    std::uint64_t comparedHashReceiptSeq{0};
    std::uint32_t comparedHashEpoch{0};
    // VUH-2270: this peer's last enemy hashes and native living counts
    // (UINT32_MAX = incomplete census) in recentEnemiesEpoch, newest first,
    // and (for clients) hashes received since the host's latest one.
    std::array<std::uint32_t, 4> recentEnemiesHashes{};
    std::array<std::uint32_t, 4> recentLivingCounts{};
    std::uint8_t recentEnemiesCount{0};
    std::uint32_t recentEnemiesEpoch{0};
    std::uint32_t hashesSinceHost{0};
    // Diagnostic-only references into the relay-hash stream; never wire fields.
    std::uint64_t diagnosticHashReceive{0}, diagnosticPreviousCompare{0};
    WorldScope diagnosticHashScope{};
    bool diagnosticHashScopeAvailable{false};

    // Heartbeat tracking
    std::uint64_t lastHeartbeatMs{0};
    std::uint32_t roundTripMs{0};
};

} // namespace kh2coop
