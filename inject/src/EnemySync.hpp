#pragma once
// ============================================================================
// EnemySync — shared enemy HP and deaths, enemy sync step 1 (VUH-1502).
//
// Every instance runs its own enemy AI. Enemies are identified by the
// VUH-1499 key: room instance (epoch) + battle program + spawn sequence
// index + objentry id; actor addresses are never used across machines.
//
// Host: announces each room instance (RoomTransition, epoch++), sends the
//       manifest as enemies spawn, absolute HP ~10 Hz, and each death once.
// Client: matches its own spawns to the host's manifest, holds matched HP
//       at the host's value, applies host deaths through the native lethal
//       path, and drops its own hits on enemies (claims are VUH-1501).
// Both: once per second in confirmed, controllable gameplay, publish a hash
//       of actual local live actors (including unmatched copies) and verified
//       applied SAVE bytes. Native actor observations accompany each hash log.
// Presence comes from a checked canonical native active-list census. An
// unavailable census suspends enemy writes, sends, hashes and despawn inference;
// time without a complete observation does not count toward despawn grace.
// Hash rows use a fresh post-application census, independently of spawn presence.
// Host room announcements wait for safe native gameplay and an enqueued full
// progress snapshot; actors remain tracked while the room packet is retried.
// Packets travel through the WorldBridge (kh2coop/WorldBridge.hpp) as the
// codec encodes them; the runtime forwards them to and from the relay.
// ============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "NativeHitTrace.hpp"
#include "kh2coop/PuppetProvenance.hpp"
#include "kh2coop/WorldContext.hpp"
#include "kh2coop/DownedState.hpp" // Types + WorldContext only; safe beside HitChannel
#include "EnemyMirrorPose.hpp" // VUH-1515: Pose only (no Protocol.hpp beside HitChannel)
#include "EnemyPopulation.hpp" // VUH-1788: pure planner/cull decision (Types.hpp only)
#include <array>

namespace kh2coop {
namespace inject {
namespace enemysync {

using LogFn = void (*)(const char* fmt, ...);
// ApplyStatDelta(actor, delta, idx, react): the native HP path (0x3D2EB0).
using StatDeltaFn = int(__fastcall*)(void* actor, int delta, int idx, int reactFlag);
using TakeDamageFn = void(__fastcall*)(void* actor, int delta, int idx, std::uint8_t reactFlag);

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta, TakeDamageFn takeDamage);
// Install-time opt-in, checked flush result; no authority or protocol role.
void SetHashDiagnosticSink(std::function<bool(const std::string&)> sink);

// Submitted synchronously by the verified local-player HP-hit hook. Native
// addresses are inspected now and never retained or put on the wire.
struct LocalPlayerEnemyHit {
    uintptr_t victim = 0, attacker = 0;
    std::uint32_t attackId = 0;
    std::int32_t damage = 0;
    float attackerPosition[3] {};
};
bool RecordLocalPlayerEnemyHit(const LocalPlayerEnemyHit& hit) noexcept;

// Game thread, at the head actor's update entry, before its native update.
// Dependency scheduling means this need not be the first actor callback.
void OnFrameStart(std::uint32_t frame);

// Compatibility entry point for EntityHook; callback coverage is not presence.
void NoteActor(uintptr_t actor);

// True on a client with enemy sync running: its own hits on enemies must
// not change their HP (the host owns it).
bool DropLocalEnemyDamage(uintptr_t victim);

// VUH-1515 step 2 (KH2COOP_ENEMY_MIRROR=1). Owner thread only. True when
// `actor` is a bound, living, allowlisted host enemy on this client whose
// stream is fresh; `out` is the host pose/motion at the render cursor.
bool MirrorRequested() noexcept;
// Gate for one actor this frame (EnemyMirror.inl PreUpdate). Fills out.netId
// for Bound and Drive, the full pose for Drive only.
// Freshness uses EnemySync's own frame clock, the one that stamps arrivals (not
// EntityHook's counter, which Panacea's loader also advances).
enemymirror::Gate MirrorPose(uintptr_t actor, enemymirror::Pose& out) noexcept;
bool MirrorTrace() noexcept;  // KH2COOP_ENEMY_MIRROR_TRACE=1: fixture position trace lines
bool MirrorLatencyTrace() noexcept; // bounded denser trace for latency A/B only
double MirrorCursor() noexcept;  // the stream's displayed host-frame cursor; < 0 when none
// VUH-1788 (KH2COOP_ENEMY_POPULATION=1 with KH2COOP_ENEMY_MIRROR=1; client only).
bool PopulationRequested() noexcept;
// Owner thread: `actor` (matched by actor + objentry + status) is a copy this
// client force-spawned whose host enemy is now dead or unknown, the epoch moved,
// or a native duplicate claims it: the removal predicate forces true.
bool PopulationForceRemove(uintptr_t actor) noexcept;
// Owner thread: `actor` is a live forced copy whose host enemy lives (held from creation).
bool PopulationForcedHold(uintptr_t actor) noexcept;
// Owner thread: the cull hook committed a forced removal of `actor` (its +0x48 dispose follows): forget it.
void PopulationForget(uintptr_t actor) noexcept;
// Planner generation (Rebase/Clear count); the cull hook drops its history when it changes.
std::uint32_t PopulationGeneration() noexcept;

// Copied diagnostic context on the registered game thread. Partial observations
// retain their masks; this never advances authority or performs a census.
nativehittrace::Context CaptureNativeHitContext() noexcept;
// Damage ownership snapshot on the registered game thread, independent of
// trace opt-in. A false result leaves authority unavailable and roster zero.
bool CaptureDamageContext(nativehittrace::Context& context,
                          std::uint64_t (&roster)[3]) noexcept;
// Read-only avatar authority on the registered game thread. An unavailable or
// changing network binding never grants standalone authority.
PuppetAuthority CapturePuppetAuthority() noexcept;
// Bounded release eligibility from the existing complete native census. This
// proves current list membership/lifecycle, not creation incarnation.
bool CurrentPuppetActor(uintptr_t actor, std::uint32_t transition,
                        std::uint32_t load) noexcept;

// Link quality the runtime publishes (app-level round trip, loss in
// per-mille). False until a runtime has connected.
bool NetStats(std::uint32_t& rttMs, std::uint32_t& lossPermille);

// Read the runtime's current session role even between gameplay frames,
// so disconnecting while a menu/load is active releases native exits.
bool HasClientAuthority();
// Loading-thread admission for a cached, generation-tagged party plan.
bool PartyLoadAdmitted(std::uint32_t generation) noexcept;

// Read-only on the registered game thread: the nonzero header generation whose
// ordered reset has been consumed, or zero while retired/unavailable. Callers
// retain and recheck this value before applying a queued native world action.
std::uint32_t WorldSessionGeneration() noexcept;
// Capture once at the native observation. Retained work must send that context;
// a newer header never grants permission to relabel an old packet.
bool CaptureWorldContext(ProducerWorldContext& context);
bool WorldContextCurrent(const ProducerWorldContext& context) noexcept;
bool SendCapturedWorld(const std::vector<std::uint8_t>& packet, const ProducerWorldContext& context);

// Scoped native controller hook callbacks. A point is captured only after an
// accepted client challenge; a client copy never falls back to its local point.
// Role values: 0 off, 1 host, 2 client. Called on the native game thread.
std::uint8_t ActivationRole();
// VUH-1515 spawn picks (SpawnPickHook): the shared salt, this visit's host epoch and the location,
// on the game thread during an area load. False with a short reason keeps the native draw.
struct SpawnPickInputs {
    std::uint64_t salt = 0;
    std::uint32_t epoch = 0;
    std::uint16_t world = 0, room = 0, map = 0, btl = 0, evt = 0;
    std::uint8_t role = 0;
};
bool SpawnPickContext(SpawnPickInputs& out, const char*& reason);
// The spawn-pick hook reports each random spawn op's path (shared or native) for the current load.
void NoteSpawnPickOp(bool shared, std::uint64_t salt);
// Review F1: whether the spawn-pick detour is live (spawnpick::Install's result); the host stamps the bit only then.
void SetSpawnPickLive(bool live);
void CaptureHostActivation(const float* position4);
bool CopyHostActivation(float* position4, uintptr_t controller, std::uint64_t updateSequence);

void Shutdown();

// VUH-1504 downed/revive native integration (docs/DOWNED_REVIVE.md). Game thread.
// The checked scope the downed owner publishes with: session generation and
// delivery (hostSourceSerial 0), the admitted room epoch and full tuple, the
// three roster connection IDs and the local slot. False = unavailable ({}).
struct DownedScope {
    ProducerWorldContext context {};
    std::uint32_t epoch = 0;
    std::uint16_t worldId = 0, roomId = 0, door = 0, mapProgram = 0, battleProgram = 0, eventProgram = 0;
    std::array<std::uint64_t, 3> connections {};
    std::uint8_t localSlot = 0xFF;
};
bool CaptureDownedScope(DownedScope& out) noexcept;
// The owner's publication this frame (the same value given to
// AvatarBridge::SetLocalDownedState); ReviveOwnerGate::Consume judges against it.
void NoteLocalDownedState(const LocalDownedState& state) noexcept;
// After envelope admission and ReviveOwnerGate::Consume (episode reserved),
// enemysync calls this exactly once; it must run the fresh native checks
// (downed::TryRevive). Returns the native result code (1 = revived).
using ReviveApplyFn = int (*)(std::uint64_t episode, std::uint8_t requesterSlot, std::uint64_t seq);
void SetReviveApply(ReviveApplyFn apply) noexcept;
struct ReviveStats {
    std::uint64_t seen = 0, admissionRefused = 0, gateRefused = 0, consumed = 0, applied = 0;
};
ReviveStats GetReviveStats() noexcept;
// Requester side: builds and sends one ReviveRequest for a teammate's streamed
// downed episode through the captured-context owner ring. seqOut = its sequence.
bool SendReviveRequest(std::uint8_t targetSlot, std::uint64_t targetEpisode, std::uint64_t& seqOut);

// VUH-1515 RemoteHit (KH2COOP_ENEMY_TARGET_REMOTE). Game thread.
// Host: the announced netId of a host enemy actor in the current epoch, 0 = unknown.
std::uint16_t HostEnemyNetId(uintptr_t actor, std::uint32_t objectId) noexcept;
// Host: sends one RemoteHit for the owner of a hit clone through the captured-context ring.
bool SendRemoteHit(std::uint8_t targetSlot, std::uint16_t netId, std::uint32_t objectId, std::uint32_t attackId,
                   std::int32_t damage, std::uint64_t& seqOut);
// Owner: after envelope/source/epoch/sequence admission enemysync calls this exactly once per
// RemoteHit. Returns 1 when applied to the local player, 0 when refused by the native checks.
using RemoteHitApplyFn = int (*)(std::int32_t damage, std::uint32_t attackId, std::uint32_t objectId, std::uint64_t seq);
void SetRemoteHitApply(RemoteHitApplyFn apply) noexcept;
struct RemoteHitStats {
    std::uint64_t seen = 0, admissionRefused = 0, applied = 0, nativeRefused = 0, sent = 0, sendFailed = 0;
};
RemoteHitStats GetRemoteHitStats() noexcept;
// VUH-1515 TargetAuthority (review B2/B3). Host: one reliable advertisement of which slots' clones
// it currently targets and covers (slotMask bit = owner slot), the families and the damage mode
// (0 forward: RemoteHit + owner cancel; 1 mirror: target choice only). Game thread.
bool SendTargetAuthority(std::uint8_t slotMask, std::uint32_t familyMask, std::uint8_t mode, std::uint64_t& seqOut);
// Owner: the latest admitted advertisement, only while it is from the current host connection,
// for our admitted host epoch and room, and at most maxAgeMs old. held = false otherwise.
struct TargetAuthorityView {
    bool held = false;
    std::uint8_t slotMask = 0, mode = 0, localSlot = 0xFF;
    std::uint32_t familyMask = 0;
    std::uint64_t seq = 0, rxMs = 0;  // rxMs: DLL admission time (GetTickCount64)
};
TargetAuthorityView CurrentTargetAuthority(std::uint64_t maxAgeMs) noexcept;

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
