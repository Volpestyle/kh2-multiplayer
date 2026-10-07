#pragma once
// ============================================================================
// Warp — send this instance to a room on command (VUH-1486). Requests arrive
// through WarpChannel (common/include/kh2coop/WarpChannel.hpp).
// ============================================================================

#include <cstdint>

namespace kh2coop {
struct RoomTransition;
struct ProducerWorldContext;
struct WorldScope;
struct ResyncBegin;
struct ResyncTarget;
namespace inject {
namespace warp {

using LogFn = void (*)(const char* fmt, ...);

// Verifies the game's transition-request function and creates the channel.
// Returns false (logged) if the function bytes don't match this build.
bool Install(uintptr_t exeBase, LogFn log);

// Call on the game thread at the start of each frame's entity update, with
// the entity list head (Sora's actor). Publishes liveness and hands a
// pending request to the game.
void OnFrameStart(std::uint32_t frame, uintptr_t listHead);

// Game-thread transition authority and lifecycle. Every accepted host epoch,
// including an initial join to the same location, performs a real reload.
// A queued target is bound to the consumed WorldBridge session generation;
// retirement disarms it before progress writes or native transition issuance.
void SetClientAuthority(bool enabled);
bool QueueHostTransition(const RoomTransition& target);
// Diagnostic causal identity only: admission/scheduling stay with the caller.
// Missing or conflicting evidence is logged unavailable, never invented.
bool QueueHostTransition(const RoomTransition& target, const ProducerWorldContext& context,
                         const WorldScope* scope, const ResyncBegin* begin,
                         const ResyncTarget* resyncTarget = nullptr);
bool HostTransitionArrived(std::uint32_t epoch);
// Game-thread diagnostic check over an already checked native location. Reads
// only owned lifecycle state; caller must separately check native safe state.
bool MatchesArrivedHostTransition(const RoomTransition& location) noexcept;
bool TransitionPending();
// VUH-1787 sender flag: a verified own room load is pending (request issued or
// not in field). Unlike TransitionPending, unverified hooks do not count.
bool LoadPending();
// Called on the game thread whenever a room transition is requested.
void SetTransitionObserver(void (*observer)());
std::uint32_t TransitionSerial();
std::uint32_t LoadSerial();
RoomTransition ReadLocation();
// VUH-1515 spawn picks: while the client executes the load the host issued (between its issue and
// CompleteLoad) and the native location equals that target, the host's epoch for it; else 0.
std::uint32_t HostIssuedLoadEpoch(const RoomTransition& loading);
// The spawn-pick shared bit and salt tag the host stamped on the transition the client is following.
void HostTargetSpawnPick(std::uint8_t& shared, std::uint32_t& saltTag);

void Shutdown();

} // namespace warp
} // namespace inject
} // namespace kh2coop
