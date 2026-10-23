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
namespace spawncontroller {
enum class EmissionPhase : std::uint8_t;
struct EmissionPermit;
struct EmissionBirth;
}
namespace enemysync {

using LogFn = void (*)(const char* fmt, ...);
// ApplyStatDelta(actor, delta, idx, react): the native HP path (0x3D2EB0).
using StatDeltaFn = int(__fastcall*)(void* actor, int delta, int idx, int reactFlag);
using TakeDamageFn = void(__fastcall*)(void* actor, int delta, int idx, std::uint8_t reactFlag);

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta, TakeDamageFn takeDamage, bool populationDeathHelperVerified);
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

// VUH-2341: warp calls this on the game thread right after the host's own
// native request for the World Map, while NOW still names the room it leaves.
// The host's event hold covers travel, so friends hold in that room.
void OnHostTravelRequest();

// Compatibility entry point for EntityHook; callback coverage is not presence.
void NoteActor(uintptr_t actor);

// True on a client with enemy sync running: its own hits on enemies must
// not change their HP (the host owns it).
bool DropLocalEnemyDamage(uintptr_t victim);

// VUH-1515 step 2 (KH2COOP_ENEMY_MIRROR=1). Owner thread only. True when
// `actor` is a bound, living, mirrored host enemy on this client whose
// stream is fresh; `out` is the host pose/motion at the render cursor.
bool MirrorRequested() noexcept;
// VUH-2421: the host's friendly fire as the runtime advertised it (on without a session).
bool FriendlyFire() noexcept;
// Gate for one actor this frame (EnemyMirror.inl PreUpdate). Fills out.netId
// for Bound and Drive, the full pose for Drive only.
// Freshness uses EnemySync's own frame clock, the one that stamps arrivals (not
// EntityHook's counter, which Panacea's loader also advances).
bool RecordMirrorAuthorityCurrent(uintptr_t actor) noexcept;
enemymirror::Gate MirrorPose(uintptr_t actor, enemymirror::Pose& out) noexcept;
// Host-owned field and Thorn decisions stay suppressed through binding/stream holds.
bool SuppressClientDecision(uintptr_t actor) noexcept;
bool FieldActorCurrent(uintptr_t actor) noexcept;
bool ConsumeFieldLeave(uintptr_t actor) noexcept;

bool MirrorTrace() noexcept;  // KH2COOP_ENEMY_MIRROR_TRACE=1: fixture position trace lines
bool MirrorLatencyTrace() noexcept; // bounded denser trace for latency A/B only
double MirrorCursor() noexcept;  // the stream's displayed host-frame cursor; < 0 when none
// VUH-1788 (KH2COOP_ENEMY_POPULATION=1 with KH2COOP_ENEMY_MIRROR=1; client only).
bool PopulationRequested() noexcept;
// Owner thread: `actor` (matched by actor + objentry + status) is a copy this
// client force-spawned whose host enemy is now dead or unknown, the epoch moved,
// or a native duplicate claims it: the removal predicate forces true.
bool PopulationStaleRequested(uintptr_t actor) noexcept;
bool PopulationForceRemove(uintptr_t actor) noexcept;
// A confirmed host-left copy whose fade settled; allows native effect cleanup,
// while preserving the removal predicate's resource/child terms.
bool PopulationLeftCleanupRequested(uintptr_t actor) noexcept;
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
bool TwoPlayerSessionLive() noexcept; // same loading-safe lease and existing roster

// Read-only on the registered game thread: the nonzero header generation whose
// ordered reset has been consumed, or zero while retired/unavailable. Callers
// retain and recheck this value before applying a queued native world action.
std::uint32_t WorldSessionGeneration() noexcept;
// VUH-2679 r3: playerkit::RebuildSafeField now, for the clone-room rebuild's native issue (warp::IssueHostTransition).
bool CloneRebuildIssueSafe(const char*& why);
// The relay session's identity: the hash of its session id that the runtime publishes before every
// generation advance (WorldBridge::SpawnPickSalt, VUH-1515). Unchanged across a same-session resync,
// changed by a replacement session, 0 once the runtime retires the session ("network-retired").
// False while the bridge is closed (unknown, not ended). Not a liveness test.
bool SessionIdentity(std::uint64_t& out) noexcept;
// VUH-2632: how many missions this host's script ended with mission_complete (table-4 trap 3), counted
// by the mission mirror's host detour (0 while it is parked or its hooks are not installed).
std::uint32_t HostMissionCompletes() noexcept;
// VUH-2656: on a client, the host's latest mission end with mission_complete: an admitted MissionState
// (the host's connection and delivery, relay-scoped, this client's current room from the host's last
// RoomTransition) whose end trap turned 3 for its mission. A resend is not another. Bound at admission to
// what held then (Quill r4): the downed owner's current downing, the session and the room generation.
struct HostMissionEnd {
    std::uint32_t serial = 0;           // 0: none yet; one per admitted end
    char file[24] {};
    std::uint16_t worldId = 0, roomId = 0;
    std::uint64_t downing = 0;          // the downing held at admission (0: none)
    std::uint64_t session = 0;          // the session identity at admission (0: none)
    std::uint32_t generation = 0, transition = 0, load = 0;
    std::uint64_t hostConnection = 0, hostDelivery = 0; // the admitted producer: the host's connection and delivery
    bool window = false;                // a transition or load was pending at admission
};
// The latest end, and whether its producer is still current: the same session identity (known, nonzero),
// session generation and host connection and delivery as at admission (Quill r4 R3).
bool LatestHostMissionEnd(HostMissionEnd& out, bool& producerCurrent) noexcept;
// The downed owner's current downing (0 when not downed), read at each admission. Set once by EntityHook.
using DowningProbeFn = std::uint64_t (*)();
void SetDowningProbe(DowningProbeFn probe) noexcept;
// On a client: the host's mission running in this room now (admitted, current, active, not ended).
bool CurrentHostMission(char (&file)[24]) noexcept;
// VUH-2660: on a client, this seat's own native mission now (its mission pointer set and readable, not
// ended): its file. The client runs the same mission script as the host.
bool LocalNativeMission(char (&file)[24]) noexcept;
// VUH-2640: this seat's native mission now, in any role (mission pointer set and readable, not ended).
bool NativeMissionFile(char (&file)[24]) noexcept;
// VUH-2640 Leg A: this friend's latest boss sit-out arrival: the admitted presence-only Bootstrap into a
// reviewed boss arena, minted when that Bootstrap's own load completes there, whatever the transaction's
// outcome (docs/FENCED_FRIEND_BOSS_ARENA.md). Evaluated on read, so DownedSpike sees it on its first frame
// after the load. DownedSpike begins the outage from it, by downedreturn::SitOutArrivalQualified. serial 0:
// none. Game thread only.
struct SitOutArrival {
    std::uint32_t serial = 0;
    std::uint64_t request = 0;
    std::uint16_t arena = 0;
    std::uint32_t generation = 0, transition = 0, load = 0;  // at the arrival
    std::uint32_t recoveryLoad = 0;                          // the recovery Bootstrap serial then: the outage's anchor
};
bool LatestSitOutArrival(SitOutArrival& out) noexcept;
// VUH-2640 Leg A veil: this client's own copy of the arena's boss, proven on a fresh, complete native census of this
// frame in the arena's exact room instance: the census's one living enemy, and it is a tracked spawn with the
// tracker's own identity (downedreturn::LoneBossCopy); bound: that spawn matched a host enemy. Game thread.
struct SitOutBossCopy {
    uintptr_t actor = 0;
    bool bound = false;
};
bool FindSitOutBossCopy(std::uint16_t arena, SitOutBossCopy& out) noexcept;
// The tracker's binding of exactly this actor (its latest spawn there matched a host enemy); reads no actor memory.
bool SitOutCopyBound(uintptr_t actor) noexcept;
// VUH-2640: positive session-end evidence: this seat's runtime (still the bridge's writer) stopped
// networking for good. An unavailable or expired writer lease is not evidence either way.
bool NetworkTerminal() noexcept;
// VUH-2640 H2: the puppet authority and the host runtime's returning connections, read together: the
// returning IDs bookend the authority capture (any change in between: not valid).
struct SeatAuthority {
    bool valid = false;
    PuppetAuthority authority {};
    std::array<std::uint64_t, 3> returning {};
};
SeatAuthority CaptureSeatAuthority() noexcept;
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
bool SpawnEmissionPolicy(std::uint32_t key, std::uint16_t header, std::uint16_t record, const float* trigger, spawncontroller::EmissionPhase phase, spawncontroller::EmissionPermit& permit, const spawncontroller::EmissionBirth* birth);
bool CopySpawnEmissionPoint(std::uint32_t key, std::uint16_t header, float* output);
bool CopyHostActivation(float* position4, uintptr_t controller, std::uint64_t updateSequence);
// VUH-2466: the Struggle orb hooks (StruggleOrbsHook.hpp), on the game thread. The host books each orb
// and queues its birth and collection as SpawnEmission rows; the client shows the host's orbs.
bool OrbHostLive();
bool OrbClientFollow();
void OrbHostBirth(std::uintptr_t manager, std::uintptr_t record, std::uint8_t type, const float* point, const float* velocity);
// collector: an OrbCollector value (Protocol.hpp stays out of this header).
void OrbHostCollect(std::uintptr_t manager, std::uintptr_t record, std::uint8_t collector, std::uint16_t netId);
// Every frame: the host observes its orb manager; the client also shows the host's orbs.
void OrbTick();
std::uint16_t HostCollectorNetId(uintptr_t actor) noexcept;  // a host enemy collector's netId, or 0
uintptr_t ClientActorForNetId(std::uint16_t netId) noexcept; // the client's living copy of a host enemy, or 0

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
// An admitted script role may open a native entity-camera task. This read-only
// proof is independent of gameplay/event gates, so those gates can query it.
using ScriptCameraCurrentFn = bool (*)(uintptr_t context, std::int32_t state) noexcept;
void SetScriptCameraCurrent(ScriptCameraCurrentFn check) noexcept;
bool OwnedScriptCameraCurrent(uintptr_t context, std::int32_t state) noexcept;
bool ScriptCameraScopeCurrent(const DownedScope& scope) noexcept;
// ScriptMirror qualifies cached native RCs against its current occurrence.
// No provider or an unrelated native target preserves the native command.
using ScriptReactionCurrentFn = std::uint16_t (*)(std::uint16_t raw) noexcept;
void SetScriptReactionCurrent(ScriptReactionCurrentFn check) noexcept;
std::uint16_t ActionableNativeReaction(std::uint16_t raw) noexcept;

// The owner's publication this frame (the same value given to
// AvatarBridge::SetLocalDownedState); ReviveOwnerGate::Consume judges against it.
void NoteLocalDownedState(const LocalDownedState& state) noexcept;
// VUH-2495, host, game thread: the native stat funnel (0x3D2EB0) left `actor` at `hp`. An announced
// enemy brought to 0 is remembered as killed, so the census publishes its death even when the actor
// has already left the list (a fight-end event closes the census gate on the killing frame).
void NoteNativeEnemyHp(uintptr_t actor, int hp) noexcept;
// After envelope admission and ReviveOwnerGate::Consume (episode reserved),
// enemysync calls this exactly once; it must run the fresh native checks
// (downed::TryRevive). Returns the native result code (1 = revived).
// kind (v32, VUH-2632): kh2coop::ReviveKind as its byte; 0 a partner revive, 1 a return at an encounter
// defeat, 2 at a save point. The relay checked it.
using ReviveApplyFn = int (*)(std::uint64_t episode, std::uint8_t requesterSlot, std::uint64_t seq, std::uint8_t kind);
void SetReviveApply(ReviveApplyFn apply) noexcept;
struct ReviveStats {
    std::uint64_t seen = 0, admissionRefused = 0, gateRefused = 0, consumed = 0, applied = 0;
};
ReviveStats GetReviveStats() noexcept;
// Requester side: builds and sends one ReviveRequest for a teammate's streamed
// downed episode through the captured-context owner ring. seqOut = its sequence.
bool SendReviveRequest(std::uint8_t targetSlot, std::uint64_t targetEpisode, std::uint64_t& seqOut,
                       std::uint8_t kind = 0);
// VUH-2632 (D17): the session's native difficulty byte, game thread: the host's own save (0..3), or on a
// client the host's value from its last RoomTransition; 0xFF while unknown (Beginner rules apply).
std::uint8_t SessionNativeDifficulty() noexcept;

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

// VUH-2252 ScriptCall (ScriptMirror.inl). A script call a host mirrored enemy's script made on
// that enemy: bdscript trap table/index and its tagged values after the actor (Protocol.hpp
// ScriptCallEntry; that header stays out of EntityHook's unit). Game thread.
struct ScriptReactionCompletionRecord {
    std::uint64_t occurrence{}, requesterConnectionId{};
    std::uint32_t resource{}, objectId{}, epoch{};
    std::uint16_t netId{}, command{};
    std::uint8_t ownerSlot{0xFF};
};
using ScriptReactionCompletionFn = bool (*)(const ScriptReactionCompletionRecord&);
void SetScriptReactionCompletion(ScriptReactionCompletionFn apply) noexcept;
bool SendScriptReactionCompletion(const ScriptReactionCompletionRecord& completion);
struct ScriptCallRecord {
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0;
    std::uint8_t table = 0;
    std::uint16_t index = 0;
    std::uint32_t effect = 0;  // host effect key (effect calls); 0 for actor calls
    std::uint64_t args[4] {};
};
// Host: one reliable ScriptCall for the current room (1..32 records, in call order).
bool SendScriptCalls(const ScriptCallRecord* records, std::size_t count, std::uint32_t hostFrame, std::uint64_t& seqOut);
// Client: after source/epoch/room/sequence admission enemysync calls this once per record, in order.
// Returns 1 when applied to the mirrored enemy, 0 when refused.
using ScriptCallApplyFn = int (*)(const ScriptCallRecord& record);
void SetScriptCallApply(ScriptCallApplyFn apply) noexcept;
struct ScriptCallStats {
    std::uint64_t sent = 0, sendFailed = 0, seen = 0, admissionRefused = 0, records = 0, applied = 0, refused = 0;
};
ScriptCallStats GetScriptCallStats() noexcept;

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
