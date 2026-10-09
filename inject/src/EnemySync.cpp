#include "EnemyRecordBinding.hpp"
// ============================================================================
// EnemySync — see EnemySync.hpp.
//
// Room instances follow Warp's native request/load-completion generations.
// Cached actors are forgotten at requests and loads, including same-room
// reloads; NOW changes and frame stalls alone never prove arrival.
//
// Spawn order: an enemy's spawn index is its order of first appearance in
// the instance (entity-list order within a frame). VUH-1499 measured this
// identical across instances in 12/13 rooms; continuous spawners diverge
// and are out of scope here.
// ============================================================================

#include "EnemySync.hpp"
#include "OrdinaryEnemyBinding.hpp"
#include "SpawnRowIdentity.hpp"
#include "SpawnPick.hpp"
#include "EnemyMirrorState.hpp"
#include "NativeSpawnController.hpp"
#include "NativePopulationAuthority.hpp"
#include "NativeResourceTrace.hpp"
#include "NativeLifecycleTrace.hpp"
#include "ProgressSync.hpp"
#include "EventHoldNative.hpp"
#include "Warp.hpp"
#include "PartyNativePolicy.hpp"

#include "kh2coop/AppliedStateHash.hpp"
#include "kh2coop/CausalDiagnostics.hpp"
#include "kh2coop/ActivationLease.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/PopulationCutJson.hpp"
#include "kh2coop/Revive.hpp"
#include "kh2coop/NativeRecordContent.hpp"
#include "kh2coop/SurvivingPackPreparation.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/WorldBridge.hpp"
#include "kh2coop/RuntimeWriterLease.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace kh2coop {
namespace inject {
namespace enemysync {
namespace {

enum class Role { Off, Host, Client };
Role CurrentRole();

constexpr std::uint32_t HP_INTERVAL_FRAMES = 6;  // ~10 Hz
constexpr std::uint64_t HASH_INTERVAL_MS = 1000;
constexpr uintptr_t ACTOR_STATUS = 0x5C0;
constexpr float SPAWN_POINT_TOLERANCE = 8.0f;  // units; spawn points matched to 0.0 in VUH-1499
// A host enemy that leaves the list alive and isn't replaced at its spawn
// point within this time is reported dead, so clients don't keep a copy.
constexpr std::uint64_t DESPAWN_GRACE_MS = 3000;

bool SamePoint(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= SPAWN_POINT_TOLERANCE * SPAWN_POINT_TOLERANCE;
}

LogFn g_log = nullptr;
CausalSink g_hashDiagnosticSink;
CausalStream g_hashDiagnosticStream;
uintptr_t g_exeBase = 0;
StatDeltaFn g_applyStatDelta = nullptr;
bool g_populationDeathHelperVerified=false;
TakeDamageFn g_takeDamage = nullptr;
Role g_role = Role::Off;
WorldBridge g_bridge;
RuntimeWriterLease g_writerLease;
std::optional<std::vector<std::uint8_t>> g_writerRecoveryPacket; // retains a raced first new-binding packet
bool g_writerRetired = false; // game-thread retirement/diagnostic only
bool RuntimeWriterLive(std::uint32_t expectedGeneration = 0) noexcept {
    if (!g_bridge.IsOpen()) return false;
    const auto writer = g_bridge.RuntimeWriter();
    if (!writer.valid || (expectedGeneration && writer.generation != expectedGeneration) || !g_writerLease.Available(writer.generation, writer.pid, writer.heartbeat, GetTickCount())) return false;
    const auto after = g_bridge.RuntimeWriter();
    return after.valid && after.generation == writer.generation && after.pid == writer.pid;
}
// VUH-1504 revive owner state; game lifetime (never reset with sessions or rooms).
ReviveApplyFn g_reviveApply = nullptr;         // set once by EntityHook
ReviveOwnerGate g_reviveGate;
LocalDownedState g_localDowned {};             // this frame's owner publication
std::string g_reviveSession;                   // latched per session generation
std::uint32_t g_reviveSessionGeneration = 0;
std::uint64_t g_reviveRequestSeq = 0;          // requester: DLL lifetime; never recycled
RemoteHitApplyFn g_remoteHitApply = nullptr;   // VUH-1515: set once by EntityHook when requested
std::uint64_t g_remoteHitSeq = 0;              // host: DLL lifetime; never recycled
std::uint64_t g_remoteHitLastSeq = 0;          // owner: last admitted seq for g_remoteHitHost
std::uint64_t g_remoteHitHost = 0;             // owner: host connection that seq belongs to
RemoteHitStats g_remoteHitStats {};
std::uint64_t g_authoritySeq = 0;              // host: DLL lifetime; never recycled
struct HeldAuthority { std::uint64_t rxMs = 0, hostConnection = 0, seq = 0; std::uint32_t epoch = 0, familyMask = 0;
                       std::uint16_t worldId = 0, roomId = 0; std::uint8_t slotMask = 0, mode = 0; };
HeldAuthority g_authority {};                  // owner: latest admitted advertisement
std::uint64_t g_authorityFloor = 0, g_authorityFloorHost = 0;
unsigned g_authorityLogs = 0;
unsigned g_remoteHitLogs = 0;
ReviveStats g_reviveStats {};
std::uint32_t g_reviveLogs = 0, g_reviveLogGeneration = 0;  // peer-driven lines: 16 per generation
bool ReviveLogAllowed() {
    const auto generation = g_bridge.SessionGeneration();
    if (generation != g_reviveLogGeneration) { g_reviveLogGeneration = generation; g_reviveLogs = 0; }
    return g_log && g_reviveLogs++ < 16;
}

// One enemy as this machine saw it spawn in the current room instance.
struct Spawn {
    std::optional<EnemyRecordKey> observedRecordKey;
    std::uint16_t spawnIndex = 0;
    std::uint32_t objectId = 0;
    std::uint32_t objectType = 0;
    uintptr_t actor = 0;
    uintptr_t objentry = 0, status = 0; // metadata at this binding's creation
    uintptr_t controller = 0, record = 0; // native spawn controller (+0x9E8) / record (+0x9F0) at creation
    bool identityRead = false;            // both were readable at creation (else the old reuse rule decides)
    std::array<float,4> populationBirthPoint {};
    bool populationPointCaptured = false;
    Vec3 spawnPos {};         // where it first appeared (identical across instances)
    bool present = false;     // in the latest complete native census
    std::int32_t lastHp = 1;  // HP when last seen (<= 0: dead, the slot may be reused)
    std::int32_t lastMaxHp = 0;
    bool announced = false;   // host: sent in a manifest this epoch
    bool deathSent = false;   // host: EnemyDeath sent (or superseded by a refill)
    std::uint64_t goneSinceMs = 0;  // host: left the list alive at this time
    bool killed = false;      // client: native HP confirmed dead
    bool deathAttempted = false; // at most one native lethal call per local spawn
    int netId = -1;           // client: matched host enemy
    ordinarybinding::State ordinaryBinding {};
    int ordinaryLogReason = -1, ordinaryLogCandidate = -1;
};

struct Instance {
    std::uint16_t world = 0xFFFF, room = 0xFFFF, btl = 0;
    std::uint16_t door = 0, map = 0, evt = 0;
    bool live = false;        // tracking spawns (not mid-transition)
    unsigned ordinaryBindingLogs = 0; // bounded adoption/hold diagnostics per instance
    std::vector<Spawn> spawns;
    std::unordered_map<uintptr_t, std::size_t> byActor;  // every address seen -> latest spawn there
};

Instance g_inst;
spawnpick::LoadOutcome g_spawnPickOutcome {};  // the latest load's random spawn ops (game thread)
bool g_spawnPickLive = false;                   // review F1: the spawn-pick detour is installed (EntityHook)
std::optional<RoomTransition> g_lastRoomPacket; // review F2: the last accepted RoomTransition packet (client)
std::uint32_t g_spawnPickUnknownEpoch = 0;      // review F2: a resync target whose trailer is unknown
std::uint32_t g_seenTransition = 0;
std::uint32_t g_seenLoad = 0;
std::uint32_t g_epoch = 0;          // host: current epoch
// Source ordering belongs to this DLL lifetime, not a room, manifest or
// transport generation. Failed enqueue attempts consume their sample number.
std::uint64_t g_hpSourceSequence = 0;
// VUH-1515 step 2 (KH2COOP_ENEMY_MIRROR=1 on host and client). Host: own EnemyMotion
// producer sequence. Client: decoded stream state, sampled at the frame-start frame.
bool g_mirrorRequested = false;
bool g_recordBindingRequested = false;
bool g_recordAuthorityRequested = false;
struct RecordAuthorityProof {
    ordinarybinding::Identity roots;
    std::array<std::uint8_t,64> bytes{};
    std::uint32_t frame{},generation{},epoch{},load{},transition{};
    NativeRecordLocation location{};
    std::uint64_t hostConnection{};
    bool terminal{};
};
std::map<uintptr_t,RecordAuthorityProof> g_recordAuthority;
// Logging-only metadata; never read by an authority or native-pointer path.
std::map<uintptr_t,RecordAuthorityProof> g_recordLastAuthority;
recordbinding::Lease g_recordLease;
std::map<uintptr_t, EnemyRecordKey> g_recordKeys;
ProducerWorldContext g_recordContext;
unsigned g_recordLogs = 0;
std::uint32_t g_recordMatchLogFrame=0;
bool g_recordWasAdmitted=false;
std::uint32_t g_recordLastHoldEpoch=0, g_recordLastHoldLoad=0;
const char* g_recordLastHoldReason=nullptr;
void WithdrawRecordBindings(const char* reason);

unsigned g_latencyTraceBudget = 0; // opt-in bounded diagnostics, never a gameplay gate
std::uint64_t g_motionSourceSequence = 0;
std::uint64_t g_motionPublished = 0, g_motionSendFailures = 0;
enemymirror::Stream g_mirror;
// VUH-1788: client population follows the host (KH2COOP_ENEMY_POPULATION=1).
bool g_populationRequested = false;
// Force-spawn has its own sub-flag (default off): the cull hold and forced-copy cleanup run with
// KH2COOP_ENEMY_POPULATION=1 alone; creation needs KH2COOP_ENEMY_POPULATION_SPAWN=1 as well.
bool g_populationSpawnRequested = false;
enemypop::Planner g_population;
using EnemyFactoryFn = void*(__fastcall*)(std::uint32_t objectId, const float* point4, float yaw);
EnemyFactoryFn g_enemyFactory = nullptr;  // 0x3DF930, byte-checked at install
constexpr uintptr_t RVA_ENEMY_FACTORY = 0x3DF930;
constexpr std::uint8_t kEnemyFactoryBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x40,
                                               0x0F, 0x29, 0x74, 0x24, 0x30};
std::uint64_t g_popSpawns = 0, g_popSpawnFailures = 0, g_popForcedGone = 0, g_popYields = 0, g_popBudgetWaits = 0;
std::uint16_t g_popBudgetWaitNetId = 0;
std::uint32_t g_popBudgetGeneration = 0;
constexpr uintptr_t RVA_ADMISSION_LIMIT = 0x2A0F7DC, RVA_ADMISSION_USED = 0x2A0F830;  // 0x3A1F00 operands
std::uint32_t g_mirrorFrame = 0;
// Fixture controls (review B1). KH2COOP_ENEMY_MIRROR_TRACE=1 logs host/client
// positions on host frames % kTraceEvery. KH2COOP_ENEMY_MIRROR_CONTROL=<file>:
// the host polls the file every 30 frames; first word "mute" pauses publishing
// (the stale-release test without stopping the runtime); any other word is a
// fixture phase name, logged once per change with the host frame.
bool g_mirrorTrace = false;
char g_mirrorControlPath[260] {};
char g_mirrorPhase[24] {};
bool g_mirrorMuted = false;
std::uint64_t g_motionMutedFrames = 0, g_motionTruncated = 0, g_motionOutOfBounds = 0;
std::size_t g_motionRoundRobin = 0;
std::uint64_t g_mirrorRefusedFar = 0;
bool g_mirrorIgnoredLogged = false, g_mirrorFirstPacketLogged = false, g_mirrorSilentLogged = false;
bool g_hpSourceExhaustionLogged = false;
// Independent of HostRoom so replacement manifests and room changes cannot
// admit older HP. Only an actual bridge generation change retires this floor.
std::uint64_t g_hostHpSequence = 0;
bool g_manifestSent = false;        // host: first manifest of the epoch went out
bool g_hostBeginPending = false;
RoomTransition g_pendingHostRoom {};
ProducerWorldContext g_pendingHostRoomContext {};
// First bounded event-publication slice; client pause/control is separate.
// Owner-thread state survives native room/key changes, but never a session reset.
bool g_eventHoldProducerEnabled = false;
struct HostEventPublication {
    bool activePublished = false;
    bool failed = false;
    std::uint64_t retryDeadlineMs = 0;
    std::optional<EventHold> pending;
    ProducerWorldContext context {};
    ProducerWorldContext admittedRoom {};
};
HostEventPublication g_hostEventPublication;
std::uint64_t g_lastHashMs = 0;
bool g_censusInterrupted = false;
std::uint64_t g_lastCensusErrorMs = 0;
std::uint64_t g_traceLastDropped = 0, g_traceLastFaults = 0;
std::uint64_t g_traceDrained = 0, g_traceLastSummaryMs = 0;
std::uint64_t g_lifecycleDrained = 0, g_lifecycleLastSummaryMs = 0;
std::uint32_t g_hitTraceFrame = 0;

// Client view of the host.
struct HostEnemy {
    std::optional<EnemyRecordKey> recordKey;
    std::uint16_t spawnIndex = 0;
    std::uint32_t objectId = 0;
    Vec3 spawnPos {};
    std::uint16_t battleProgram = 0;
    std::int32_t hp = -1, maxHp = -1;
    std::uint64_t hpSourceSequence = 0;
    bool hpKnown = false;
    bool dead = false;
};
struct HostRoom {
    std::uint32_t epoch = 0;
    std::uint16_t world = 0xFFFF, room = 0xFFFF, btl = 0;
    bool arrived = false;
    bool ackSent = false;
    bool manifestComplete = false; // only fully framed replace or checked bootstrap
    std::map<std::uint16_t, HostEnemy> enemies;  // by netId
};
HostRoom g_host;
eventhold::Scope g_eventControlScope {};
RoomTransition g_eventControlRoom {};

ActivationLease g_activationLease;
std::array<std::uint64_t, 2> g_activationIncarnation {};
std::uint32_t g_activationGeneration = 0;
std::uint32_t g_activationOrderedGeneration = 0;
std::uint32_t g_lastOrderedGeneration = 0;
std::uint64_t g_orderedDeliverySerial = 0;
std::uint64_t g_worldSourceSerial = 0, g_worldSendFailures = 0;
bool g_worldSourceExhaustionLogged = false;
std::uint64_t g_resyncAppliedCut = 0;
struct RequesterDelivery { std::uint64_t connection = 0, minimum = 0; };
std::array<RequesterDelivery, 3> g_requesterDeliveries {};
bool RequesterCurrent(std::uint8_t slot, std::uint64_t connection, std::uint64_t delivery) {
    if (slot < 1 || slot > 2 || !connection || !delivery || g_bridge.ConnectionId(slot) != connection) return false;
    const auto published = g_bridge.PeerDeliverySerial(slot);
    if (!published || delivery != published) return false; // future is not confirmed authority either
    const auto& floor = g_requesterDeliveries[slot];
    return (floor.connection != connection || delivery >= floor.minimum) &&
           g_bridge.ConnectionId(slot) == connection && g_bridge.PeerDeliverySerial(slot) == published;
}
std::uint64_t g_activationSourceSeq = 0;
std::uint64_t g_activationLogMs = 0;
struct ActivationChallenge {
    ActivationRequest request;
    std::uint64_t captureFloor = 0;
    std::uint64_t receivedMs = 0;
    std::uint32_t transition = 0, load = 0, generation = 0;
    std::optional<HostActivationPoint> response;
    ProducerWorldContext responseContext {};
    std::uint64_t requesterConnection = 0, requesterDelivery = 0;
};
std::array<std::optional<ActivationChallenge>, 2> g_activationChallenges {};

constexpr std::size_t HIT_PENDING_CAP = 128;
constexpr unsigned HIT_NATIVE_PER_FRAME = 8;
constexpr std::uint64_t HIT_DEADLINE_MS = 500;
struct PendingHitClaim {
    HitClaim claim {};
    std::uint32_t generation = 0, transition = 0, load = 0;
    std::uint64_t deliverySerial = 0;
    std::uint64_t requesterDeliverySerial = 0;
    std::uint64_t receivedMs = 0;
};
std::array<PendingHitClaim, HIT_PENDING_CAP> g_pendingHits {};
std::size_t g_hitHead = 0, g_hitCount = 0;
struct ClaimSequence {
    std::uint64_t connection = 0;
    std::uint32_t consumed = 0;
};
std::array<ClaimSequence, 3> g_claimSequences {};
std::uint64_t g_localClaimConnection = 0;
std::uint32_t g_localClaimSequence = 0;
std::uint32_t g_claimDiagnosticBudget = 512;
std::uint64_t g_claimDiagnosticsSuppressed = 0;
bool g_hostClaimProcessing = false;

std::optional<ResyncPlan> g_resyncPlan;
std::uint64_t g_resyncDeadline = 0, g_resyncFailureFloor = 0;
std::uint32_t g_resyncPriorOrderedGeneration = 0;
bool g_resyncHostCaptured = false;
std::vector<std::uint8_t> g_resyncOutput;
ProducerWorldContext g_resyncOutputContext {};
std::uint32_t g_resyncLogBudget = 128;
std::uint64_t g_resyncLogsSuppressed = 0;
bool ResyncLogAllowed() {
    if (!g_log) return false;
    if (g_resyncLogBudget) { --g_resyncLogBudget; return true; }
    ++g_resyncLogsSuppressed;
    if ((g_resyncLogsSuppressed & (g_resyncLogsSuppressed - 1)) == 0)
        g_log("[resync] evidence suppressed=%llu", static_cast<unsigned long long>(g_resyncLogsSuppressed));
    return false;
}
struct NativeResyncTarget {
    ResyncBegin begin;
    ResyncSnapshot snapshot;
    ResyncTarget target;
    ProducerWorldContext context;
    std::uint32_t loadBefore = 0, observedLoad = 0;
    std::uint64_t firstFrame = 0;
    bool firstObserved = false, finished = false;
};
std::optional<NativeResyncTarget> g_nativeResync;
// Optional preparation only. It never calls a native emitter or grants creation.
// Retain the reducer/tombstone through transport cleanup for this DLL lifetime.
bool g_survivingPackEnabled = false;
SurvivingPackPreparation g_survivingPack;
// Historical input is a separate, opt-in policy for the original scheduled
// update. It does not grant the parked owned-emitter gateway any authority.
enum class ActivationRecoveryPhase { Historical, LiveHold, Verified, Failed };
struct ActivationRecovery {
    ActivationRecoveryPhase phase = ActivationRecoveryPhase::Historical;
    uintptr_t controller = 0, header = 0, records = 0, tableEntry = 0;
    std::array<std::uint8_t, 16> tableBytes {};
    std::array<std::uint8_t, 44> headerBytes {};
    std::array<std::array<std::uint8_t, 64>, 5> recordBytes {};
    KnownControllerMutationTicket mutation;
    std::uint32_t load = 0, transition = 0, historicalTicks = 0, liveTicks = 0;
    std::uint64_t lastSequence = 0, historicalBaseline = 0, liveBaseline = 0;
    bool reconciled = false, occupancyHold = false;
};
std::optional<ActivationRecovery> g_activationRecovery;
std::uint64_t g_activationReceiptSequence = 0, g_activationReceiptGaps = 0;
constexpr std::uint32_t kActivationRecoveryTicks = 512, kActivationLiveHoldTicks = 120;
bool PackPreparationActive() {
    const auto& intent = g_survivingPack.Intent();
    return g_survivingPackEnabled && intent && g_nativeResync && !g_nativeResync->finished &&
        intent->begin.key == g_nativeResync->begin.key &&
        intent->begin.sha256 == g_nativeResync->begin.sha256 &&
        intent->begin.phase == g_nativeResync->begin.phase;
}
// A result/ACK retires transport bookkeeping, not remaining enemy authority.
// Only an explicit new world scope or a qualified checkpoint opens this fence.
enum class ResyncWriteFence { None, Waiting, Exact, ObserveOnly, Failed };
ResyncWriteFence g_resyncWriteFence = ResyncWriteFence::None;
struct ResyncRecordAuthority {
    ResyncSnapshot snapshot;
    ProducerWorldContext context;
};
std::optional<ResyncRecordAuthority> g_resyncRecordAuthority;
void ReceiveResyncPlan(const ResyncPlan& plan);
void ReceiveResyncSnapshot(const std::vector<std::uint8_t>& packet);
void TickNativeResync(std::uint32_t frame, bool observed = false);

void ClearPendingHits() { g_hitHead = g_hitCount = 0; }

void LogClaim(const char* action, const HitClaim& claim, const char* reason) {
    if (!g_log) return;
    if (!g_claimDiagnosticBudget) {
        ++g_claimDiagnosticsSuppressed;
        if ((g_claimDiagnosticsSuppressed & (g_claimDiagnosticsSuppressed - 1)) == 0)
            g_log("[hitclaim] diagnostics suppressed=%llu; application logs continue",
                  static_cast<unsigned long long>(g_claimDiagnosticsSuppressed));
        return;
    }
    --g_claimDiagnosticBudget;
    g_log("[hitclaim] %s epoch=%u seq=%u slot=%u connection=%llu netId=%u objectId=%u damage=%d currentEpoch=%u reason=%s",
                     action, claim.epoch, claim.seq, static_cast<unsigned>(claim.attackerSlot),
                     static_cast<unsigned long long>(claim.requesterConnectionId), claim.netId,
                     claim.objectId, claim.damage, g_epoch, reason);
    if (!g_claimDiagnosticBudget) g_log("[hitclaim] rejection/receipt log budget exhausted; application logs continue");
}

void ResetPopulationRepair();
void ClearActivation() {
    g_activationLease.Clear();
    g_activationChallenges = {};
}

// Client claim authority is independent of optional historical replay. A
// release belongs to one admitted world/native scope and one manifest revision.
struct ClientClaimScope {
    std::uint32_t generation = 0, ordered = 0, transition = 0, load = 0;
    std::uint64_t delivery = 0;
    std::array<std::uint64_t, 3> roster {};
    RoomTransition room {};
    std::uint8_t slot = 0;
};
struct ClientClaimHold {
    ClientClaimScope scope;
    bool bound = false, held = true, poisoned = false;
    std::uint64_t id = 0, sequence = 0, dropped = 0, submitted = 0, receiptGaps = 0;
    std::uint64_t releasedRevision = 0;
};
ClientClaimHold g_clientClaimHold;
std::uint64_t g_clientManifestRevision = 0;
void LogClientClaim(const char* action, const char* reason, std::size_t rows = 0, const HitClaim* claim = nullptr);
void InvalidateClientClaims(const char* reason) {
    auto& hold = g_clientClaimHold;
    if (hold.bound || !hold.held) {
        hold.held = true; hold.bound = false;
        LogClientClaim("rearm", reason);
    }
}
void AdvanceClientManifestRevision() {
    if (g_clientManifestRevision == UINT64_MAX) g_clientClaimHold.poisoned = true;
    else ++g_clientManifestRevision;
}
bool EnsureClientClaimScope();
bool ReleaseClientClaims(std::uint32_t frame);

void RetireWorldSession() {
    ResetPopulationRepair();
    g_recordLease.Interrupt(); g_recordKeys.clear(); g_recordAuthority.clear(); g_recordContext={};
    eventholdnative::RetireOwner();
    g_eventControlScope = {}; g_eventControlRoom = {};
    g_hostEventPublication = {};
    InvalidateClientClaims("world-session-retired");
    g_activationRecovery.reset();
    if (g_survivingPack.Intent()) g_survivingPack.Cancel();
    g_nativeResync.reset();
    g_resyncRecordAuthority.reset();
    if (g_resyncWriteFence == ResyncWriteFence::Exact) g_resyncWriteFence = ResyncWriteFence::Waiting;
    g_resyncOutput.clear(); g_resyncOutputContext = {};
    g_host = {};
    g_lastRoomPacket.reset();      // shared-bit review N1: a retired session's trailer never reaches a resync
    g_spawnPickUnknownEpoch = 0;
    g_mirror.Reset(0);  // VUH-1515 S4: a restarted host reuses epochs, sequences and frames
    g_population.Rebase(0);  // VUH-1788 C1: planning restarts; forced entries stay until removed
    ClearActivation();
    ClearPendingHits();
    progresssync::Reset();
    g_hostBeginPending = false;
    g_pendingHostRoom = {};
    g_pendingHostRoomContext = {};
    g_lastHashMs = 0;
    warp::SetClientAuthority(false);
    for (Spawn& spawn : g_inst.spawns) {
        spawn.netId = -1;
        spawn.killed = false;
        spawn.deathAttempted = false;
    }
}

void CheckActivationGeneration() {
    const auto generation = g_bridge.SessionGeneration();
    if (generation != g_activationGeneration) {
        InvalidateClientClaims("generation-header-changed");
        LogClientClaim("boundary", "generation-header-changed");
        if (!g_resyncPlan) {
            g_resyncWriteFence = ResyncWriteFence::None;
            g_resyncRecordAuthority.reset();
        }
        g_activationOrderedGeneration = 0;
        g_orderedDeliverySerial = 0;
        // The ordered marker may still be outside a full ring. Retire native
        // authority immediately, even if no frame observed an Off role.
        RetireWorldSession();
        g_hostHpSequence = 0;
        g_resyncAppliedCut = 0;
        g_requesterDeliveries = {};
        g_claimSequences = {}; // an actual session boundary, never a room transition
        g_activationGeneration = generation;
    }
}

bool ActivationContext(Role role, RoomTransition& location);
void AdmitRemoteHit(const WorldScope& scope, const std::uint8_t* payload, std::size_t size); // VUH-1515
void AdmitTargetAuthority(const WorldScope& scope, const std::uint8_t* payload, std::size_t size); // VUH-1515
void RequestActivation();
void FlushActivationResponses();

std::uint32_t g_logBudget = 400;
#define SYNC_LOG(...) do { if (g_log && g_logBudget) { --g_logBudget; g_log(__VA_ARGS__); } } while (0)

template <typename T>
T Read(uintptr_t address) {
    T value {};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

// Keep SEH in a POD-only leaf: callers own all C++ containers/unwinding.
bool CopyNative(uintptr_t address, void* destination, std::size_t size) {
    if (address <= 0x10000 || address >= 0x7FFFFFFFFFFFULL ||
        size > 0x7FFFFFFFFFFFULL - address) return false;
    __try {
        std::memcpy(destination, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T>
bool ReadNative(uintptr_t address, T& value) {
    return CopyNative(address, &value, sizeof(value));
}

struct NativeEnemy {
    uintptr_t actor = 0, objentry = 0, status = 0;
    std::uint32_t objectId = 0;
    std::uint32_t objectType = 0;
    std::int32_t hp = 0, maxHp = 0;
    Vec3 position {};
};

// A successful exclusion is different from a failed classifier read.
bool ReadNativeEnemy(uintptr_t actor, NativeEnemy& enemy, bool& isEnemy) {
    enemy = {};
    enemy.actor = actor;
    isEnemy = false;
    if (!ReadNative(actor + offsets::actor::OBJENTRY_PTR, enemy.objentry)) return false;
    if (!enemy.objentry) return true;
    if (enemy.objentry <= g_exeBase || enemy.objentry >= g_exeBase + 0x3000000) return false;
    std::uint8_t type = 0;
    if (!ReadNative(enemy.objentry + offsets::objentry::TYPE_FLAGS, type)) return false;
    if (type != offsets::objentry::TYPE_BOSS && type != offsets::objentry::TYPE_MOB) return true;
    char prefix[2] {};
    if (!CopyNative(enemy.objentry + offsets::objentry::NAME, prefix, sizeof(prefix))) return false;
    if (prefix[0] == 'F' && prefix[1] == '_') return true;
    enemy.objectType = type;
    if (!ReadNative(actor + ACTOR_STATUS, enemy.status)) return false;
    if (!enemy.status) return true; // native noncombat actors can have no stats
    if (!ReadNative(enemy.objentry + offsets::objentry::OBJECT_ID, enemy.objectId) ||
        !ReadNative(enemy.status, enemy.hp) || !ReadNative(enemy.status + 4, enemy.maxHp)) return false;
    const auto transform = actor + offsets::actor::ENTITY_TRANSFORM;
    if (!ReadNative(transform + offsets::entity::POS_X, enemy.position.x) ||
        !ReadNative(transform + offsets::entity::POS_Y, enemy.position.y) ||
        !ReadNative(transform + offsets::entity::POS_Z, enemy.position.z)) return false;
    isEnemy = true;
    return true;
}

bool SameNativeIdentity(const NativeEnemy& a, const NativeEnemy& b) {
    return a.actor == b.actor && a.objentry == b.objentry && a.status == b.status &&
           a.objectId == b.objectId;
}

bool ReadLocationChecked(RoomTransition& location) {
    std::uint8_t world = 0, room = 0, door = 0;
    if (!ReadNative(g_exeBase + offsets::WORLD_ID, world) ||
        !ReadNative(g_exeBase + offsets::ROOM_ID, room) ||
        !ReadNative(g_exeBase + offsets::NOW + 2, door) ||
        !ReadNative(g_exeBase + offsets::MAP_PROGRAM, location.mapProgram) ||
        !ReadNative(g_exeBase + offsets::BATTLE_PROGRAM, location.battleProgram) ||
        !ReadNative(g_exeBase + offsets::EVENT_PROGRAM, location.eventProgram)) return false;
    location.worldId = world;
    location.roomId = room;
    location.door = door;
    return true;
}

bool SameLocation(const RoomTransition& a, const RoomTransition& b) {
    return a.worldId == b.worldId && a.roomId == b.roomId && a.door == b.door &&
           a.mapProgram == b.mapProgram && a.battleProgram == b.battleProgram &&
           a.eventProgram == b.eventProgram;
}

enum class CensusState { Unavailable, Complete };
struct NativeCensus {
    CensusState state = CensusState::Unavailable;
    const char* reason = "not sampled";
    uintptr_t failedAt = 0;
    std::size_t nodeCount = 0;
    bool watchedActorPresent = false;
    std::uint32_t transition = 0, load = 0;
    RoomTransition location {};
    std::vector<NativeEnemy> enemies;
};

bool SafeNativeGameplay();

NativeCensus CaptureNativeCensus(uintptr_t watchedActor = 0) {
    using namespace offsets::active_entity_list;
    NativeCensus result;
    auto fail = [&](const char* reason, uintptr_t address) {
        result.reason = reason;
        result.failedAt = address;
        return result;
    };
    if (!SafeNativeGameplay()) return fail("gameplay gate", 0);
    result.transition = warp::TransitionSerial();
    result.load = warp::LoadSerial();
    if (!ReadLocationChecked(result.location)) return fail("location read", g_exeBase + offsets::NOW);
    uintptr_t head = 0, tail = 0;
    if (!ReadNative(g_exeBase + HEAD, head) || !ReadNative(g_exeBase + TAIL, tail))
        return fail("list roots read", g_exeBase + HEAD);
    if ((head == 0) != (tail == 0)) return fail("list roots disagree", head);
    std::array<uintptr_t, HANDLE_BUCKET_COUNT> regions {};
    std::array<bool, HANDLE_BUCKET_COUNT> usedRegions {};
    if (!CopyNative(g_exeBase + HANDLE_REGION_TABLE, regions.data(), sizeof(regions)))
        return fail("handle table read", g_exeBase + HANDLE_REGION_TABLE);
    struct Link { uintptr_t actor; std::uint32_t next; };
    std::vector<Link> links;
    links.reserve(MAX_TRAVERSAL);
    uintptr_t actor = head;
    while (actor != 0) {
        // This is a safety cap, not a native roster limit. Exact-cap/null is
        // complete; a continuation beyond it makes the whole sample unavailable.
        if (links.size() == MAX_TRAVERSAL) return fail("list cap", actor);
        if (std::any_of(links.begin(), links.end(), [actor](const Link& link) { return link.actor == actor; }))
            return fail("list cycle", actor);
        if (actor <= 0x10000 || actor >= 0x7FFFFFFFFFFFULL - 0x1000)
            return fail("actor address", actor);
        std::uint32_t next = 0;
        if (!ReadNative(actor + offsets::actor::LINKED_NEXT_HANDLE, next)) return fail("next read", actor);
        links.push_back({actor, next});
        result.nodeCount = links.size();
        if (watchedActor != 0 && actor == watchedActor) result.watchedActorPresent = true;
        NativeEnemy enemy;
        bool isEnemy = false;
        if (!ReadNativeEnemy(actor, enemy, isEnemy)) return fail("enemy metadata read", actor);
        if (isEnemy) result.enemies.push_back(enemy);
        if (next == 0) {
            if (actor != tail) return fail("tail mismatch", actor);
            actor = 0;
        } else {
            // Native 0x4AD3F0 ignores bit 31. It is not a validity/generation bit.
            const auto bucket = (next & 0x7FFFFFFFU) >> HANDLE_BUCKET_SHIFT;
            const auto region = regions[bucket];
            if (region == 0 || region == UINT64_MAX || (region & HANDLE_LOW_MASK) != 0)
                return fail("invalid handle region", g_exeBase + HANDLE_REGION_TABLE + bucket * 8);
            usedRegions[bucket] = true;
            actor = region | (next & HANDLE_LOW_MASK);
            if (actor == 0) return fail("nonnull handle resolved null", links.back().actor);
        }
    }
    // Validate internal links as well as roots; unchanged HEAD alone is insufficient.
    for (const auto& link : links) {
        std::uint32_t next = 0;
        if (!ReadNative(link.actor + offsets::actor::LINKED_NEXT_HANDLE, next) || next != link.next)
            return fail("list link changed", link.actor);
    }
    for (std::size_t i = 0; i < regions.size(); ++i) {
        uintptr_t region = 0;
        if (usedRegions[i] && (!ReadNative(g_exeBase + HANDLE_REGION_TABLE + i * 8, region) || region != regions[i]))
            return fail("handle region changed", g_exeBase + HANDLE_REGION_TABLE + i * 8);
    }
    uintptr_t finalHead = 0, finalTail = 0;
    RoomTransition finalLocation {};
    if (!ReadNative(g_exeBase + HEAD, finalHead) || !ReadNative(g_exeBase + TAIL, finalTail) ||
        finalHead != head || finalTail != tail) return fail("list roots changed", g_exeBase + HEAD);
    if (!ReadLocationChecked(finalLocation) || !SameLocation(result.location, finalLocation) ||
        result.transition != warp::TransitionSerial() || result.load != warp::LoadSerial() ||
        !SafeNativeGameplay()) return fail("lifecycle changed", 0);
    result.state = CensusState::Complete;
    result.reason = "complete";
    return result;
}


void WithdrawRecordBindings(const char* reason) {
    const auto epoch=g_role==Role::Host?g_epoch:g_host.epoch;
    const bool changed=g_recordWasAdmitted || epoch!=g_recordLastHoldEpoch ||
        g_seenLoad!=g_recordLastHoldLoad || !g_recordLastHoldReason || std::strcmp(reason,g_recordLastHoldReason)!=0;
    if (changed && g_log) {
        g_log("[record-binding] withdrawal epoch=%u frame=%u generation=%u connection=%llu load=%u transition=%u reason=%s expected=%zu rights=%zu terminal=%u",
            epoch,g_hitTraceFrame,WorldSessionGeneration(),static_cast<unsigned long long>(g_bridge.ConnectionId(0)),
            g_seenLoad,g_seenTransition,reason,recordbinding::Population({g_inst.world,g_inst.room,g_inst.door,g_inst.map,g_inst.btl,g_inst.evt}),g_recordAuthority.size(),static_cast<unsigned>(g_recordWasAdmitted));
        const auto& retired=g_recordAuthority.empty()?g_recordLastAuthority:g_recordAuthority;
        for (const auto& [actor,proof]:retired)
            g_log("[record-binding] withdrawn-body epoch=%u frame=%u actor=%llX status=%llX controller=%llX record=%llX",
                epoch,g_hitTraceFrame,static_cast<unsigned long long>(actor),static_cast<unsigned long long>(proof.roots.status),
                static_cast<unsigned long long>(proof.roots.controller),static_cast<unsigned long long>(proof.roots.record));
    }
    g_recordWasAdmitted=false;g_recordLastHoldEpoch=epoch;g_recordLastHoldLoad=g_seenLoad;g_recordLastHoldReason=reason;
    g_recordLease.Interrupt();g_recordKeys.clear();g_recordAuthority.clear();g_recordContext={};
    g_recordLastAuthority.clear();
    for (auto& spawn:g_inst.spawns) if (RecordFamily(spawn.objectId)) {spawn.netId=-1;spawn.ordinaryBinding.bound=0;}
    // Preserve unrelated queued claims. Already-delivered HP/death remains
    // cached input only: its actual write boundary requires a new current proof.
    std::size_t kept=0;
    for (std::size_t i=0;i<g_hitCount;++i) {
        const auto pending=g_pendingHits[(g_hitHead+i)%HIT_PENDING_CAP];
        if (!RecordFamily(pending.claim.objectId)) g_pendingHits[(g_hitHead+kept++)%HIT_PENDING_CAP]=pending;
    }
    g_hitCount=static_cast<decltype(g_hitCount)>(kept);
}

void InterruptCensus(const char* reason, uintptr_t address, std::size_t nodes = 0) {
    if (g_recordBindingRequested) WithdrawRecordBindings("native-census-interrupted");
    g_censusInterrupted = true;
    // Wall time spent without a complete observation never counts as absence.
    for (Spawn& spawn : g_inst.spawns) spawn.goneSinceMs = 0;
    const auto now = GetTickCount64();
    if (g_log && (g_lastCensusErrorMs == 0 || now - g_lastCensusErrorMs >= HASH_INTERVAL_MS)) {
        g_log("[enemysync] native census unavailable: %s at=%llX nodes=%zu", reason,
              static_cast<unsigned long long>(address), nodes);
        g_lastCensusErrorMs = now;
    }
}

bool CensusMatchesInstance(const NativeCensus& census) {
    RoomTransition currentLocation {};
    return census.state == CensusState::Complete && census.transition == g_seenTransition &&
           census.load == g_seenLoad && census.transition == warp::TransitionSerial() &&
           census.load == warp::LoadSerial() && ReadLocationChecked(currentLocation) &&
           SameLocation(census.location, currentLocation) && census.location.worldId == g_inst.world &&
           census.location.roomId == g_inst.room && census.location.door == g_inst.door &&
           census.location.mapProgram == g_inst.map && census.location.battleProgram == g_inst.btl &&
           census.location.eventProgram == g_inst.evt;
}

const NativeEnemy* FindNativeEnemy(const NativeCensus& census, const Spawn& spawn) {
    const auto it = std::find_if(census.enemies.begin(), census.enemies.end(), [&](const NativeEnemy& row) {
        return row.actor == spawn.actor && row.objectId == spawn.objectId &&
               row.objentry == spawn.objentry && row.status == spawn.status;
    });
    return it == census.enemies.end() ? nullptr : &*it;
}

uintptr_t ObjEntry(uintptr_t actor) {
    const auto obj = Read<uintptr_t>(actor + offsets::actor::OBJENTRY_PTR);
    return obj > g_exeBase && obj < g_exeBase + 0x3000000 ? obj : 0;
}

std::int32_t* HpPtr(uintptr_t actor) {
    const auto status = Read<uintptr_t>(actor + ACTOR_STATUS);
    return status > 0x10000 && status < 0x7FFFFFFFFFFFULL ? reinterpret_cast<std::int32_t*>(status)
                                                           : nullptr;
}

// A combat enemy: objentry type 3/4, has stats, not an F_ object.
bool IsEnemy(uintptr_t actor) {
    const uintptr_t obj = ObjEntry(actor);
    if (!obj || !HpPtr(actor)) return false;
    const auto type = Read<std::uint8_t>(obj + offsets::objentry::TYPE_FLAGS);
    if (type != offsets::objentry::TYPE_BOSS && type != offsets::objentry::TYPE_MOB) return false;
    const char* name = reinterpret_cast<const char*>(obj + offsets::objentry::NAME);
    return !(name[0] == 'F' && name[1] == '_');
}

bool Send(const std::vector<std::uint8_t>& packet) {
    ProducerWorldContext context;
    return CaptureWorldContext(context) && SendCapturedWorld(packet, context);
}

// ---- Host -------------------------------------------------------------------

bool SafeNativeGameplay() {
    __try {
        return !warp::TransitionPending() && warp::LoadSerial() != 0 &&
               Read<std::int32_t>(g_exeBase + offsets::CONTROLLABLE) == 0 &&
               Read<std::uint8_t>(g_exeBase + offsets::IN_FIELD) != 0 &&
               Read<std::uint8_t>(g_exeBase + offsets::OPEN_MENU) == 0xFF &&
               *reinterpret_cast<const std::int32_t*>(g_exeBase + offsets::CUTSCENE_STATE) == 0 &&
               *reinterpret_cast<const uintptr_t*>(g_exeBase + offsets::EVENT_CONTEXT) == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <std::size_t N>
void TraceHex(const std::array<std::uint8_t, N>& bytes, char (&out)[2 * N + 1]) {
    constexpr char digits[] = "0123456789ABCDEF";
    for (std::size_t i = 0; i < N; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[2 * N] = '\0';
}

const char* TraceOutcomeName(spawncontroller::TraceOutcome outcome) {
    using spawncontroller::TraceOutcome;
    switch (outcome) {
    case TraceOutcome::Unavailable: return "unavailable";
    case TraceOutcome::OutOfScope: return "out-of-scope";
    case TraceOutcome::NullReturn: return "null-return";
    case TraceOutcome::ActorReadUnavailable: return "actor-read-unavailable";
    case TraceOutcome::IdentityMismatch: return "identity-mismatch";
    case TraceOutcome::Observed: return "observed";
    }
    return "unknown";
}

void LogTraceState(const char* family, std::uint64_t sequence, const char* phase,
                   const spawncontroller::TraceState& state) {
    if (!g_log) return;
    g_log("%s state seq=%llu phase=%s controllerAvailable=%u header=%llX spawnArray=%llX key=%u headerId=%u nativeType=%u records=%u flags=%u stage=%u currentCountU32=%u initialCountU32=%u cooldown=%.9g activation=%u cacheAvailable=%u cacheBucket=%llX cacheRoom=%d cacheAge=%d",
          family, static_cast<unsigned long long>(sequence), phase, state.controllerAvailable ? 1u : 0u,
          static_cast<unsigned long long>(state.header), static_cast<unsigned long long>(state.spawnArray),
          state.key, state.headerId, state.nativeType, state.recordCount,
          state.flags, state.stage, state.currentCount, state.initialCount, state.cooldown,
          state.activation, state.cacheAvailable ? 1u : 0u,
          static_cast<unsigned long long>(state.cacheBucket), state.cacheRoom, state.cacheAge);
    if (!state.cacheAvailable) return;
    // Preserve every native zero slot as well as nonzero IDs. Chunks are explicit
    // so a diagnostic consumer can detect missing lines instead of truncating.
    constexpr char digits[] = "0123456789ABCDEF";
    for (std::size_t chunk = 0; chunk < 8; ++chunk) {
        char values[129] {};
        for (std::size_t i = 0; i < 32; ++i) {
            const auto id = state.cacheIds[chunk * 32 + i];
            for (std::size_t nibble = 0; nibble < 4; ++nibble)
                values[i * 4 + nibble] = digits[(id >> (12 - 4 * nibble)) & 15];
        }
        g_log("%s cache seq=%llu phase=%s chunk=%zu/8 idsU16Hex=%s",
              family, static_cast<unsigned long long>(sequence), phase, chunk, values);
    }
}

bool TraceStampMatchesCensus(const spawncontroller::TraceStamp& stamp, const NativeCensus& census) {
    const auto& loc = stamp.location;
    return census.state == CensusState::Complete && stamp.transition == census.transition && stamp.load == census.load &&
           loc[0] == census.location.worldId && loc[1] == census.location.roomId && loc[2] == census.location.door &&
           (loc[4] | (static_cast<unsigned>(loc[5]) << 8)) == census.location.mapProgram &&
           (loc[6] | (static_cast<unsigned>(loc[7]) << 8)) == census.location.battleProgram &&
           (loc[8] | (static_cast<unsigned>(loc[9]) << 8)) == census.location.eventProgram;
}

bool ConfirmDiagnosticActor(const NativeCensus& census, uintptr_t actor, uintptr_t objentry,
                            uintptr_t status, std::uint32_t objectId,
                            uintptr_t expectedController, uintptr_t expectedRecord) {
    if (!CensusMatchesInstance(census)) return false;
    const auto found = std::find_if(census.enemies.begin(), census.enemies.end(), [&](const NativeEnemy& row) {
        return row.actor == actor && row.objentry == objentry && row.status == status && row.objectId == objectId;
    });
    if (found == census.enemies.end()) return false;
    NativeEnemy current;
    bool isEnemy = false;
    uintptr_t controller = 0, record = 0;
    return ReadNativeEnemy(actor, current, isEnemy) && isEnemy && SameNativeIdentity(*found, current) &&
           ReadNative(actor + 0x9E8, controller) && ReadNative(actor + 0x9F0, record) &&
           controller == expectedController && record == expectedRecord && CensusMatchesInstance(census);
}

bool DiagnosticBindingContext(std::uint32_t generation) {
    return generation != 0 && generation == g_activationGeneration &&
           generation == g_activationOrderedGeneration && generation == g_bridge.SessionGeneration() &&
           CurrentRole() == g_role;
}

int DiagnosticNetId(uintptr_t actor, std::uint32_t objectId, std::uint32_t generation) {
    if (!DiagnosticBindingContext(generation)) return 0;
    const auto found = g_inst.byActor.find(actor);
    if (found == g_inst.byActor.end() || found->second >= g_inst.spawns.size()) return 0;
    const auto& spawn = g_inst.spawns[found->second];
    if (spawn.objectId != objectId || !spawn.present) return 0;
    if (g_role == Role::Host && spawn.announced && g_epoch != 0 && !g_hostBeginPending)
        return spawn.spawnIndex + 1;
    if (g_role == Role::Client && g_host.arrived && g_host.epoch != 0 && spawn.netId > 0) {
        const auto bound = g_host.enemies.find(static_cast<std::uint16_t>(spawn.netId));
        if (bound != g_host.enemies.end() && bound->second.objectId == objectId) return spawn.netId;
    }
    return 0;
}

void LogGeometryTrace(const spawncontroller::TraceEvent& event) {
    if (!g_log) return;
    const auto& geometry = event.geometry;
    char header[89] {}, records[641] {}, descriptors[897] {}, beforeNow[21] {}, afterNow[21] {};
    char controller[129] {}, tableEntry[33] {};
    TraceHex(geometry.header, header); TraceHex(geometry.records, records); TraceHex(geometry.descriptors, descriptors);
    TraceHex(event.stamp.location, beforeNow); TraceHex(event.postStamp.location, afterNow);
    TraceHex(geometry.controllerBytes, controller); TraceHex(geometry.tableEntry, tableEntry);
    bool allRejected = geometry.complete && geometry.calls == 7;
    for (std::size_t i = 0; i < geometry.predicates.size(); ++i)
        allRejected = allRejected && geometry.predicates[i].region == geometry.regions[i] && geometry.predicates[i].result == 0;
    g_log("[spawngeometry] tick seq=%llu tick=%llu ordinal=%u coverage=%llu controller=%llX role=%u roleAvailable=%u transition=%u load=%u now=%s postTransition=%u postLoad=%u postNow=%s lifecycleStable=%u definitionAvailable=%u definitionStable=%u originalReturned=%u calls=%u overflow=%u nested=%u unwound=%u complete=%u allSevenNativeRejected=%u emission=unclassified",
        static_cast<unsigned long long>(event.sequence), static_cast<unsigned long long>(event.tickSequence), geometry.ordinal,
        static_cast<unsigned long long>(geometry.coverageSerial), static_cast<unsigned long long>(event.controller), event.role, event.roleAvailable ? 1U : 0U,
        event.stamp.transition, event.stamp.load, beforeNow, event.postStamp.transition, event.postStamp.load, afterNow,
        event.lifecycleStable ? 1U : 0U, geometry.definitionAvailable ? 1U : 0U, geometry.definitionStable ? 1U : 0U,
        geometry.originalReturned ? 1U : 0U, geometry.calls, geometry.overflow ? 1U : 0U, geometry.nested ? 1U : 0U,
        geometry.unwound ? 1U : 0U, geometry.complete ? 1U : 0U, allRejected ? 1U : 0U);
    if (geometry.eventGate.requested) {
        const auto& gate = geometry.eventGate;
        // Callwise evidence only: AL0 does not authorize native enrollment.
        // Legacy tick is an update sequence, not elapsed milliseconds.
        g_log("[spawnenrollment] event-gate seq=%llu updateSequence=%llu ordinal=%u coverage=%llu expectedCallerRva=3FF0E5 calls=%u resultAL=%u requested=%u available=%u returned=%u unwound=%u nested=%u overflow=%u complete=%u",
            static_cast<unsigned long long>(event.sequence), static_cast<unsigned long long>(event.tickSequence), geometry.ordinal,
            static_cast<unsigned long long>(gate.coverageSerial), gate.calls, static_cast<unsigned>(gate.result), gate.requested ? 1U : 0U, gate.available ? 1U : 0U,
            gate.returned ? 1U : 0U, gate.unwound ? 1U : 0U, gate.nested ? 1U : 0U,
            gate.overflow ? 1U : 0U, gate.complete ? 1U : 0U);
    }
    if (geometry.originalPhase.requested) {
        const auto& phase = geometry.originalPhase;
        g_log("[spawnenrollment] original-phase seq=%llu updateSequence=%llu ordinal=%u coverage=%llu invocation=%llu controller=%llX updateCaller=%llX updateCallerRva=%llX updateCallerRvaAvailable=%u originalPoint=%llX source=%u hold=%u originalEntries=%u originalReturns=%u originalUnwinds=%u requested=%u available=%u overflow=%u mismatch=%u unwound=%u threadPhaseConsistent=%u fiberContinuityProven=%u originalPhaseEligibility=%u",
            static_cast<unsigned long long>(event.sequence), static_cast<unsigned long long>(event.tickSequence), geometry.ordinal,
            static_cast<unsigned long long>(phase.coverageSerial), static_cast<unsigned long long>(phase.invocation),
            static_cast<unsigned long long>(phase.controller), static_cast<unsigned long long>(phase.updateCaller),
            static_cast<unsigned long long>(phase.updateCallerRva), phase.updateCallerRvaAvailable ? 1U : 0U,
            static_cast<unsigned long long>(phase.originalPoint), static_cast<unsigned>(phase.source), static_cast<unsigned>(phase.hold),
            phase.originalEntries, phase.originalReturns, phase.originalUnwinds, phase.requested ? 1U : 0U, phase.available ? 1U : 0U,
            phase.overflow ? 1U : 0U, phase.mismatch ? 1U : 0U, phase.unwound ? 1U : 0U,
            phase.threadPhaseConsistent ? 1U : 0U, phase.fiberContinuityProven ? 1U : 0U, phase.originalPhaseEligibility ? 1U : 0U);
        for (unsigned i = 0; i < 2; ++i) {
            const auto& sample = i == 0 ? phase.gateEntry : phase.gateReturn;
            g_log("[spawnenrollment] original-phase-gate seq=%llu boundary=%s invocation=%llu updateSequence=%llu controller=%llX point=%llX phase=%u source=%u available=%u actualCaller=%llX actualCallerRva=%llX actualCallerRvaAvailable=%u",
                static_cast<unsigned long long>(event.sequence), i == 0 ? "entry" : "return",
                static_cast<unsigned long long>(sample.invocation), static_cast<unsigned long long>(sample.updateSequence),
                static_cast<unsigned long long>(sample.controller), static_cast<unsigned long long>(sample.point),
                static_cast<unsigned>(sample.phase), static_cast<unsigned>(sample.source), sample.available ? 1U : 0U,
                static_cast<unsigned long long>(sample.caller), static_cast<unsigned long long>(sample.callerRva), sample.callerRvaAvailable ? 1U : 0U);
        }
    }
    g_log("[spawngeometry] definition seq=%llu tableCount=%u tableIndex=%u tableEntryHex=%s controllerHex=%s headerHex=%s recordsHex=%s descriptorsHex=%s",
        static_cast<unsigned long long>(event.sequence), geometry.tableCount, geometry.tableIndex, tableEntry, controller, header, records, descriptors);
    for (std::size_t i = 0; i < geometry.regions.size(); ++i) {
        char bytes[225] {}, input[33] {};
        TraceHex(geometry.regionBytes[i], bytes); TraceHex(geometry.predicates[i].inputBytes, input);
        g_log("[spawngeometry] region seq=%llu index=%zu address=%llX bytesHex=%s",
            static_cast<unsigned long long>(event.sequence), i, static_cast<unsigned long long>(geometry.regions[i]), bytes);
        if (i < geometry.calls) {
            const auto& child = geometry.predicates[i];
            g_log("[spawngeometry] predicate seq=%llu index=%zu region=%llX callerRva=3FF140 inputHex=%s inputAvailable=%u member=%u returned=%u resultAL=%u unwound=%u",
                static_cast<unsigned long long>(event.sequence), i, static_cast<unsigned long long>(child.region), input,
                child.inputAvailable ? 1U : 0U, child.member ? 1U : 0U, child.returned ? 1U : 0U, child.result, child.unwound ? 1U : 0U);
        }
    }
    LogTraceState("[spawngeometry]", event.sequence, "tick-before", event.tickBefore);
    LogTraceState("[spawngeometry]", event.sequence, "tick-after", event.tickAfter);
}


// Deferred only: no native reads, census substitution, or callback/TLS lookup.
// The real EntityHook logger uses vfprintf (no fixed message buffer). Keep each
// line below 2048 bytes anyway; fixed chunks make missing output detectable.
constexpr unsigned ConstructionDrainCap = 2;
constexpr std::uint64_t ConstructionLogCap = 64;
struct ConstructionLogCounters {
    std::uint64_t consumed = 0, serialized = 0, suppressedBudget = 0, suppressedNoLogger = 0;
    std::uint64_t summaryMs = 0;
};
ConstructionLogCounters g_constructionLog; // DLL lifetime; not reset by resync.

void LogConstructionTrace(const spawncontroller::NativeConstructionLineage& e) {
    if (!g_log) return;
    const auto serial = static_cast<unsigned long long>(e.serial);
    const auto coverage = static_cast<unsigned long long>(e.coverage);
    const auto wrapperSequence = static_cast<unsigned long long>(e.wrapperSequence);
    g_log("[spawnconstruction] terminal schema=1 serial=%llu coverage=%llu wrapperSequence=%llu coverageAfter=%llu controller=%llX record=%llX callerRva=%llX callerRvaAvailable=%u threadId=%u depth=%u wrapper=%u captured=%u candidateThreadParent=%u ambiguous=%u overflow=%u normalReturn=%u unwound=%u",
        serial, coverage, wrapperSequence,
        static_cast<unsigned long long>(e.coverageAfter), static_cast<unsigned long long>(e.controller), static_cast<unsigned long long>(e.record), static_cast<unsigned long long>(e.callerRva),
        e.callerRvaAvailable ? 1U : 0U, static_cast<unsigned>(e.threadId), static_cast<unsigned>(e.depth), static_cast<unsigned>(e.wrapper),
        e.captured ? 1U : 0U, e.candidateThreadParent ? 1U : 0U, e.ambiguous ? 1U : 0U, e.overflow ? 1U : 0U,
        e.normalReturn ? 1U : 0U, e.unwound ? 1U : 0U);
    g_log("[spawnconstruction] parent schema=1 serial=%llu coverage=%llu wrapperSequence=%llu enclosingThreadSerial=%llu tickSequence=%llu dispatcherSequence=%llu scriptSequence=%llu enclosingTick=%u enclosingDispatcher=%u enclosingScript=%u dispatcherCallerRva=%llX dispatcherCallerRvaAvailable=%u scriptCallerRva=%llX scriptCallerRvaAvailable=%u",
        serial, coverage, wrapperSequence,
        static_cast<unsigned long long>(e.enclosingThreadSerial), static_cast<unsigned long long>(e.tickSequence), static_cast<unsigned long long>(e.dispatcherSequence), static_cast<unsigned long long>(e.scriptSequence),
        e.enclosingTick ? 1U : 0U, e.enclosingDispatcher ? 1U : 0U, e.enclosingScript ? 1U : 0U, static_cast<unsigned long long>(e.dispatcherCallerRva),
        e.dispatcherCallerRvaAvailable ? 1U : 0U, static_cast<unsigned long long>(e.scriptCallerRva), e.scriptCallerRvaAvailable ? 1U : 0U);
    g_log("[spawnconstruction] stability schema=1 serial=%llu coverage=%llu wrapperSequence=%llu sampledIdentityStable=%u sampledDefinitionStable=%u sampledTableStable=%u sampledLifecycleStable=%u knownMutationStable=%u coverageStable=%u traceQueueStable=%u droppedBefore=%llu droppedAfter=%llu",
        serial, coverage, wrapperSequence,
        e.sampledIdentityStable ? 1U : 0U, e.sampledDefinitionStable ? 1U : 0U, e.sampledTableStable ? 1U : 0U, e.sampledLifecycleStable ? 1U : 0U,
        e.knownMutationStable ? 1U : 0U, e.coverageStable ? 1U : 0U, e.traceQueueStable ? 1U : 0U, static_cast<unsigned long long>(e.droppedBefore),
        static_cast<unsigned long long>(e.droppedAfter));
    g_log("[spawnconstruction] limits schema=1 serial=%llu coverage=%llu wrapperSequence=%llu parentFiberAncestryUnproven=%u fiberContinuityProven=%u continuousModeProven=%u globalPendingExcluded=%u creatorExclusive=%u controllerIncarnationProven=%u atomic=%u creationAuthority=%u",
        serial, coverage, wrapperSequence,
        e.parentFiberAncestryUnproven ? 1U : 0U, e.fiberContinuityProven ? 1U : 0U, e.continuousModeProven ? 1U : 0U, e.globalPendingExcluded ? 1U : 0U,
        e.creatorExclusive ? 1U : 0U, e.controllerIncarnationProven ? 1U : 0U, e.atomic ? 1U : 0U, e.creationAuthority ? 1U : 0U);
    for (unsigned phase = 0; phase < 2; ++phase) {
        const auto& s = e.samples[phase];
        g_log("[spawnconstruction] sample schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phase=%u controllerRead=%u recordRead=%u headerRead=%u countBeforeRead=%u countAfterRead=%u stampRead=%u countBefore=%d countAfter=%d tableReadMask=%016llX recordReadMask=%u tableComplete=%u fiveRecordLayout=%u recordIndexAvailable=%u recordMatchesDefinition=%u ordinaryAssociationSampled=%u",
            serial, coverage, wrapperSequence, phase, s.controllerRead ? 1U : 0U, s.recordRead ? 1U : 0U,
            s.headerRead ? 1U : 0U, s.countBeforeRead ? 1U : 0U, s.countAfterRead ? 1U : 0U, s.stampRead ? 1U : 0U,
            s.countBefore, s.countAfter, static_cast<unsigned long long>(s.tableReadMask), static_cast<unsigned>(s.recordReadMask),
            s.tableComplete ? 1U : 0U, s.fiveRecordLayout ? 1U : 0U, s.recordIndexAvailable ? 1U : 0U,
            s.recordMatchesDefinition ? 1U : 0U, s.ordinaryAssociationSampled ? 1U : 0U);
        g_log("[spawnconstruction] definition schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phase=%u header=%llX spawnArray=%llX regionArray=%llX group=%u tableMatches=%u headerId=%u declaredRecords=%u recordIndex=%u nativeType=%u",
            serial, coverage, wrapperSequence, phase, static_cast<unsigned long long>(s.header),
            static_cast<unsigned long long>(s.spawnArray), static_cast<unsigned long long>(s.regionArray),
            s.group, s.tableMatches, s.headerId, s.declaredRecords, s.recordIndex, s.nativeType);
        char now[21] {};
        TraceHex(s.stamp.location, now);
        g_log("[spawnconstruction] context schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phase=%u transition=%u load=%u nowHex=%s mutationRevision=%llu mutationInFlight=%u mutationAvailable=%u mutationPoisoned=%u mutationCoverageComplete=%u",
            serial, coverage, wrapperSequence, phase, s.stamp.transition, s.stamp.load, now,
            static_cast<unsigned long long>(s.mutation.revision), s.mutation.inFlight, s.mutation.available ? 1U : 0U,
            s.mutation.poisoned ? 1U : 0U, s.mutation.coverageComplete ? 1U : 0U);
        char controller[129] {}, record[129] {}, header[89] {};
        TraceHex(s.controllerBytes, controller); TraceHex(s.recordBytes, record); TraceHex(s.headerBytes, header);
        g_log("[spawnconstruction] raw schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phase=%u controllerHex=%s recordHex=%s headerHex=%s",
            serial, coverage, wrapperSequence, phase, controller, record, header);
        // Always retain storage, including failed-read bytes. Only the validity
        // flags/masks qualify bytes; a zero buffer never means an empty table.
        for (unsigned index = 0; index < 5; ++index) {
            char bytes[129] {}; TraceHex(s.records[index], bytes);
            g_log("[spawnconstruction] record schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phase=%u index=%u slots=5 read=%u bytesHex=%s",
                serial, coverage, wrapperSequence, phase, index, (s.recordReadMask >> index) & 1U, bytes);
        }
        for (unsigned chunk = 0; chunk < 16; ++chunk) {
            std::array<std::uint8_t, 64> bytes {};
            for (unsigned i = 0; i < 4; ++i)
                std::copy(s.table[chunk * 4 + i].begin(), s.table[chunk * 4 + i].end(), bytes.begin() + i * 16);
            char hex[129] {}; TraceHex(bytes, hex);
            g_log("[spawnconstruction] table schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phase=%u chunk=%u chunks=16 first=%u slots=4 readMask=%u bytesHex=%s",
                serial, coverage, wrapperSequence, phase, chunk, chunk * 4,
                static_cast<unsigned>((s.tableReadMask >> (chunk * 4)) & 15U), hex);
        }
    }
    // Presence of this row proves only that these logger calls returned. It is
    // not durable-file completeness, native return success, or parent coverage.
    g_log("[spawnconstruction] end schema=1 serial=%llu coverage=%llu wrapperSequence=%llu phases=2 recordsPerPhase=5 tableChunksPerPhase=16 rows=55",
        serial, coverage, wrapperSequence);
}

void DrainConstructionTrace(const spawncontroller::TraceStats& stats) {
    if (!stats.constructionRequested) return;
    spawncontroller::NativeConstructionLineage event;
    for (unsigned drained = 0; drained < ConstructionDrainCap && spawncontroller::PopNativeConstructionLineage(event); ++drained) {
        ++g_constructionLog.consumed;
        if (!g_log) { ++g_constructionLog.suppressedNoLogger; continue; }
        if (g_constructionLog.serialized >= ConstructionLogCap) { ++g_constructionLog.suppressedBudget; continue; }
        LogConstructionTrace(event);
        ++g_constructionLog.serialized;
    }
    const auto now = GetTickCount64();
    if (g_log && (g_constructionLog.summaryMs == 0 || now - g_constructionLog.summaryMs >= 1000)) {
        // Producer stats precede this drain and are separate from local losses.
        // Neither an empty pop nor zero published establishes native absence.
        g_log("[spawnconstruction] summary schema=1 requested=%u configured=%u coverage=%llu producerSerial=%llu producerPublished=%llu producerDropped=%llu consumed=%llu serialized=%llu suppressedBudget=%llu suppressedNoLogger=%llu logCap=%llu drainCap=%u emptyMeansAbsent=0 parentCompletenessProven=0 creationAuthority=0",
            stats.constructionRequested ? 1U : 0U, stats.constructionConfigured ? 1U : 0U,
            static_cast<unsigned long long>(stats.constructionCoverage), static_cast<unsigned long long>(stats.constructionSerial),
            static_cast<unsigned long long>(stats.constructionPublished), static_cast<unsigned long long>(stats.constructionDropped),
            static_cast<unsigned long long>(g_constructionLog.consumed), static_cast<unsigned long long>(g_constructionLog.serialized),
            static_cast<unsigned long long>(g_constructionLog.suppressedBudget), static_cast<unsigned long long>(g_constructionLog.suppressedNoLogger),
            static_cast<unsigned long long>(ConstructionLogCap), ConstructionDrainCap);
        g_constructionLog.summaryMs = now;
    }
}

void DrainSpawnTrace(const NativeCensus& census) {
    const auto stats = spawncontroller::GetTraceStats();
    DrainConstructionTrace(stats);
    if (!stats.requested) return;
    const auto now = GetTickCount64();
    if (g_log && (g_traceLastSummaryMs == 0 || now - g_traceLastSummaryMs >= 1000)) {
        if (stats.geometryRequested) g_log("[spawngeometry] summary configured=%u verified=%u installed=%u priorObserved=%u terminal=%u load=%u transition=%u ticks=%u startedMs=%llu coverage=%llu dropped=%llu unwound=%llu foreign=%llu",
            stats.geometryConfigured ? 1U : 0U, stats.geometryVerified ? 1U : 0U, stats.geometryInstalled ? 1U : 0U,
            stats.geometryPriorObserved ? 1U : 0U, static_cast<unsigned>(stats.geometryTerminal), stats.geometryLoad, stats.geometryTransition,
            stats.geometryTicks, static_cast<unsigned long long>(stats.geometryStartedMs), static_cast<unsigned long long>(stats.geometryCoverageSerial),
            static_cast<unsigned long long>(stats.geometryDropped), static_cast<unsigned long long>(stats.geometryUnwound), static_cast<unsigned long long>(stats.geometryForeign));
        if (stats.enrollmentRequested) g_log("[spawnenrollment] summary requested=%u configured=%u eventGateVerified=%u eventGateInstalled=%u eventGateFailed=%u coverage=%llu foreign=%llu unwound=%llu dropped=%llu",
            stats.enrollmentRequested ? 1U : 0U, stats.enrollmentConfigured ? 1U : 0U,
            stats.eventGateVerified ? 1U : 0U, stats.eventGateInstalled ? 1U : 0U, stats.eventGateFailed ? 1U : 0U,
            static_cast<unsigned long long>(stats.eventGateCoverageSerial), static_cast<unsigned long long>(stats.eventGateForeign),
            static_cast<unsigned long long>(stats.eventGateUnwound), static_cast<unsigned long long>(stats.eventGateDropped));
        g_log("[spawntrace] summary available=%u fixedAvailable=%u generatedAvailable=%u dispatcherAvailable=%u scriptAvailable=%u started=%llu published=%llu drained=%llu dropped=%llu unsupportedCaller=%llu unavailable=%llu nativeFaults=%llu lastException=%08X factoryRequestedMask=%u factoryVerifiedMask=%u factoryInstalledMask=%u factoryFailedMask=%u factoryForeignScopes=%llu factoryUnwoundScopes=%llu factoryAmbiguousScopes=%llu flsRefusedCumulative=%llu flsFirstReason=%u",
              stats.available ? 1u : 0u, stats.fixedAvailable ? 1u : 0u, stats.generatedAvailable ? 1u : 0u,
              stats.dispatcherAvailable ? 1u : 0u, stats.scriptAvailable ? 1u : 0u,
              static_cast<unsigned long long>(stats.started),
              static_cast<unsigned long long>(stats.published), static_cast<unsigned long long>(g_traceDrained),
              static_cast<unsigned long long>(stats.dropped), static_cast<unsigned long long>(stats.unsupportedCaller),
              static_cast<unsigned long long>(stats.unavailable), static_cast<unsigned long long>(stats.nativeFaults),
              stats.lastNativeException, spawncontroller::FactoryAllHooks,
              stats.factoryVerifiedMask, stats.factoryInstalledMask, stats.factoryFailedMask,
              static_cast<unsigned long long>(stats.factoryForeignScopes),
              static_cast<unsigned long long>(stats.factoryUnwoundScopes),
              static_cast<unsigned long long>(stats.factoryAmbiguousScopes),
              static_cast<unsigned long long>(stats.flsRefused), stats.flsFirstReason);
        g_traceLastSummaryMs = now;
    }
    if (g_log && (stats.dropped != g_traceLastDropped || stats.nativeFaults != g_traceLastFaults)) {
        g_log("[spawntrace] loss started=%llu published=%llu dropped=%llu unavailable=%llu nativeFaults=%llu lastException=%08X",
              static_cast<unsigned long long>(stats.started), static_cast<unsigned long long>(stats.published),
              static_cast<unsigned long long>(stats.dropped), static_cast<unsigned long long>(stats.unavailable),
              static_cast<unsigned long long>(stats.nativeFaults), stats.lastNativeException);
        g_traceLastDropped = stats.dropped;
        g_traceLastFaults = stats.nativeFaults;
    }
    spawncontroller::TraceEvent event;
    // A concurrent producer must not make this game-thread drain unbounded.
    // Undrained events remain queued; producer loss is counted explicitly.
    for (unsigned drained = 0; drained < 16 && spawncontroller::PopTraceEvent(event); ++drained) {
        ++g_traceDrained;
        if (event.kind == spawncontroller::TraceKind::Geometry) { LogGeometryTrace(event); continue; }
        bool eligible = CensusMatchesInstance(census) && event.stampAvailable && event.postStampAvailable &&
                        event.wrapperComplete && event.lifecycleStable && event.actorAvailable &&
                        event.wrapperOutcome == spawncontroller::TraceOutcome::Observed &&
                        (!event.enclosingTick || event.tickComplete) &&
                        TraceStampMatchesCensus(event.stamp, census) && TraceStampMatchesCensus(event.postStamp, census) &&
                        event.stamp.location == event.postStamp.location;
        const bool confirmed = eligible && ConfirmDiagnosticActor(census, event.actor, event.objentry,
            event.status, event.actorObjectId, event.controller, event.record);
        const auto drainGeneration = g_bridge.SessionGeneration();
        int netId = confirmed && event.roleAvailable ? DiagnosticNetId(event.actor, event.actorObjectId, drainGeneration) : 0;
        if (!g_log) continue;
        char recordHex[129] {}, beforeNow[21] {}, afterNow[21] {};
        TraceHex(event.recordBytes, recordHex);
        TraceHex(event.stamp.location, beforeNow);
        TraceHex(event.postStamp.location, afterNow);
        // bindingEpoch is a current census correlation, NEVER a creation epoch.
        // Unbound/pre-session creations are retained; no pointer/order invents ID.
        auto bindingEpoch = netId ? (g_role == Role::Host ? g_epoch : g_host.epoch) : 0;
        if (!DiagnosticBindingContext(drainGeneration)) { netId = 0; bindingEpoch = 0; }
        g_log("[spawntrace] event seq=%llu tick=%llu wrapper=%s wrapperOutcome=%s wrapperComplete=%u callerRva=%llX callerRvaAvailable=%u outcome=%s reason=%s captureRole=%u roleAvailable=%u drainRole=%u drainGeneration=%u transition=%u load=%u postTransition=%u postLoad=%u stampAvailable=%u postStampAvailable=%u enclosingTick=%u tickComplete=%u lifecycleStable=%u controller=%llX record=%llX recordIndex=%u recordIndexAvailable=%u nativeRecordId=%u objectId=%u objectIdMatchesRecord=%u actor=%llX objentry=%llX status=%llX actorController=%llX actorRecord=%llX actorType=%u actorObjectId=%u hp=%d maxHp=%d actorAvailable=%u censusComplete=%u censusConfirmed=%u bindingEpoch=%u netId=%d enclosingDispatcher=%u dispatcherSeq=%llu dispatcherCallerRva=%llX dispatcherCallerRvaAvailable=%u dispatcherRegion=%llX enclosingScript42DC10=%u scriptSeq=%llu scriptCallerRva=%llX scriptCallerRvaAvailable=%u generatedPointAvailable=%u generatedPoint=(%.9g,%.9g,%.9g,%.9g)",
              static_cast<unsigned long long>(event.sequence), static_cast<unsigned long long>(event.tickSequence),
              event.wrapper == spawncontroller::TraceWrapper::Fixed ? "fixed" : "generated",
              TraceOutcomeName(event.wrapperOutcome), event.wrapperComplete ? 1u : 0u,
              static_cast<unsigned long long>(event.callerRva), event.callerRvaAvailable ? 1u : 0u,
              TraceOutcomeName(event.outcome), event.reason, event.role, event.roleAvailable ? 1u : 0u, static_cast<unsigned>(g_role),
              drainGeneration, event.stamp.transition, event.stamp.load,
              event.postStamp.transition, event.postStamp.load, event.stampAvailable ? 1u : 0u,
              event.postStampAvailable ? 1u : 0u, event.enclosingTick ? 1u : 0u,
              event.tickComplete ? 1u : 0u, event.lifecycleStable ? 1u : 0u,
              static_cast<unsigned long long>(event.controller), static_cast<unsigned long long>(event.record),
              event.recordIndex, event.recordIndexAvailable ? 1u : 0u, event.nativeRecordId, event.objectId,
              event.objectIdMatchesRecord ? 1u : 0u, static_cast<unsigned long long>(event.actor),
              static_cast<unsigned long long>(event.objentry), static_cast<unsigned long long>(event.status),
              static_cast<unsigned long long>(event.actorController), static_cast<unsigned long long>(event.actorRecord),
              event.actorType, event.actorObjectId, event.hp, event.maxHp, event.actorAvailable ? 1u : 0u,
              census.state == CensusState::Complete ? 1u : 0u,
              confirmed ? 1u : 0u, bindingEpoch, netId,
              event.enclosingDispatcher ? 1u : 0u, static_cast<unsigned long long>(event.dispatcherSequence),
              static_cast<unsigned long long>(event.dispatcherCallerRva), event.dispatcherCallerRvaAvailable ? 1u : 0u,
              static_cast<unsigned long long>(event.dispatcherRegion), event.enclosingScript42DC10 ? 1u : 0u,
              static_cast<unsigned long long>(event.scriptSequence), static_cast<unsigned long long>(event.scriptCallerRva),
              event.scriptCallerRvaAvailable ? 1u : 0u, event.generatedPointAvailable ? 1u : 0u,
              event.generatedPoint[0], event.generatedPoint[1], event.generatedPoint[2], event.generatedPoint[3]);
        const auto& factory = event.factory;
        g_log("[spawntrace] factory seq=%llu coverageSerial=%llu coverageMask=%u depth=%u eligible=%u complete=%u unwound=%u countOverflow=%u operandMask=%u weightBits=%08X limitBeforeBits=%08X usedBeforeBits=%08X limitAfterBits=%08X usedAfterBits=%08X admissionCalls=%u admissionReturned=%u admissionResult=%u admissionFault=%u allocationCalls=%u allocationReturned=%u allocationSize=%llu allocationResult=%llX allocationFault=%u outcome=%u",
              static_cast<unsigned long long>(event.sequence), static_cast<unsigned long long>(factory.coverageSerial),
              factory.coverageMask, factory.depth, factory.eligible ? 1u : 0u, factory.complete ? 1u : 0u,
              factory.unwound ? 1u : 0u, factory.countOverflow ? 1u : 0u, static_cast<unsigned>(factory.operandMask),
              factory.weightBits, factory.limitBeforeBits, factory.usedBeforeBits, factory.limitAfterBits, factory.usedAfterBits,
              static_cast<unsigned>(factory.admissionCalls), factory.admissionReturned ? 1u : 0u,
              static_cast<unsigned>(factory.admissionResult), factory.admissionFault ? 1u : 0u,
              static_cast<unsigned>(factory.allocationCalls), factory.allocationReturned ? 1u : 0u,
              static_cast<unsigned long long>(factory.allocationSize), static_cast<unsigned long long>(factory.allocationResult),
              factory.allocationFault ? 1u : 0u, static_cast<unsigned>(factory.outcome));
        g_log("[spawntrace] record seq=%llu available=%u beforeNow=%s afterNow=%s bytes=%s",
              static_cast<unsigned long long>(event.sequence), event.recordAvailable ? 1u : 0u,
              beforeNow, afterNow, recordHex);
        LogTraceState("[spawntrace]", event.sequence, "tick-before", event.tickBefore);
        LogTraceState("[spawntrace]", event.sequence, "tick-after", event.tickAfter);
        LogTraceState("[spawntrace]", event.sequence, "wrapper-before", event.wrapperBefore);
        LogTraceState("[spawntrace]", event.sequence, "wrapper-after", event.wrapperAfter);
    }
}

const char* LifecycleKindName(lifecycletrace::Kind kind) {
    using lifecycletrace::Kind;
    switch (kind) {
    case Kind::RemovalBookkeeping: return "removal-bookkeeping";
    case Kind::Disposal: return "disposal";
    case Kind::DeathMark: return "death-mark";
    case Kind::DeathBookkeeping: return "death-bookkeeping";
    case Kind::CountDecrement: return "count-decrement";
    case Kind::RemovalPredicate: return "removal-predicate";
    }
    return "unknown";
}

void LogLifecycleActor(std::uint64_t sequence, const char* phase,
                       const lifecycletrace::ActorSnapshot& actor) {
    if (!g_log) return;
    g_log("[lifecycletrace] actor seq=%llu phase=%s available=%u classificationAvailable=%u isCombat=%u actor=%llX objentry=%llX status=%llX controller=%llX record=%llX objectId=%u actorType=%u hp=%d maxHp=%d flags120=%u flags9B8=%u flags6C8=%u recordAvailable=%u recordId=%u recordMode=%u recordStage=%u fadeA08=%.9g slopeA0C=%.9g fadeAAC=%.9g slopeAB0=%.9g",
          static_cast<unsigned long long>(sequence), phase, actor.available ? 1u : 0u,
          actor.classificationAvailable ? 1u : 0u, actor.combat ? 1u : 0u,
          static_cast<unsigned long long>(actor.actor), static_cast<unsigned long long>(actor.objentry),
          static_cast<unsigned long long>(actor.status), static_cast<unsigned long long>(actor.controller),
          static_cast<unsigned long long>(actor.record), actor.objectId, actor.type, actor.hp, actor.maxHp,
          actor.flags120, actor.flags9B8, actor.flags6C8, actor.recordAvailable ? 1u : 0u,
          actor.recordId, actor.recordMode, actor.recordStage,
          actor.fadeA08, actor.slopeA0C, actor.fadeAAC, actor.slopeAB0);
}

void LogLifecycleStamp(std::uint64_t sequence, const char* phase,
                       const spawncontroller::TraceStamp& stamp) {
    if (!g_log) return;
    char nowHex[21] {};
    TraceHex(stamp.location, nowHex);
    g_log("[lifecycletrace] stamp seq=%llu phase=%s transition=%u load=%u nowHex=%s",
          static_cast<unsigned long long>(sequence), phase, stamp.transition, stamp.load, nowHex);
}

void LogRemovalOperands(std::uint64_t sequence, const char* phase,
                        const lifecycletrace::RemovalOperands& operands) {
    if (!g_log) return;
    g_log("[lifecycletrace] operands seq=%llu phase=%s availableMask=%u scriptState=%llX scriptTest=%u field80=%llX field98=%llX auxiliaryHandle=%u",
          static_cast<unsigned long long>(sequence), phase, operands.availableMask,
          static_cast<unsigned long long>(operands.scriptState), operands.scriptTest,
          static_cast<unsigned long long>(operands.field80), static_cast<unsigned long long>(operands.field98),
          operands.auxiliaryHandle);
}

void DrainLifecycleTrace(const NativeCensus& census) {
    const auto stats = lifecycletrace::GetStats();
    if (!stats.requested) return;
    const auto now = GetTickCount64();
    if (g_log && (g_lifecycleLastSummaryMs == 0 || now - g_lifecycleLastSummaryMs >= 1000)) {
        g_log("[lifecycletrace] summary requested=%u verifiedMask=%u installedMask=%u failedMask=%u started=%llu published=%llu drained=%llu dropped=%llu unavailable=%llu outOfScope=%llu nativeFaults=%llu unwound=%llu depthOverflow=%llu lastException=%08X predicateStarted=%llu predicatePublished=%llu predicateDropped=%llu predicateForeign=%llu predicateUnmatched=%llu predicateUnwound=%llu predicateDepthOverflow=%llu predicateCountOverflow=%llu flsRefusedCumulative=%llu flsFirstReason=%u",
              stats.requested ? 1u : 0u, stats.verifiedMask, stats.installedMask, stats.failedMask,
              static_cast<unsigned long long>(stats.started), static_cast<unsigned long long>(stats.published),
              static_cast<unsigned long long>(g_lifecycleDrained), static_cast<unsigned long long>(stats.dropped),
              static_cast<unsigned long long>(stats.unavailable), static_cast<unsigned long long>(stats.outOfScope),
              static_cast<unsigned long long>(stats.nativeFaults), static_cast<unsigned long long>(stats.unwound),
              static_cast<unsigned long long>(stats.depthOverflow), stats.lastNativeException,
              static_cast<unsigned long long>(stats.predicateStarted), static_cast<unsigned long long>(stats.predicatePublished),
              static_cast<unsigned long long>(stats.predicateDropped), static_cast<unsigned long long>(stats.predicateForeign),
              static_cast<unsigned long long>(stats.predicateUnmatched), static_cast<unsigned long long>(stats.predicateUnwound),
              static_cast<unsigned long long>(stats.predicateDepthOverflow), static_cast<unsigned long long>(stats.predicateCountOverflow),
              static_cast<unsigned long long>(stats.flsRefused), stats.flsFirstReason);
        g_lifecycleLastSummaryMs = now;
    }
    lifecycletrace::Event event;
    for (unsigned drained = 0; drained < 8 && lifecycletrace::PopEvent(event); ++drained) {
        ++g_lifecycleDrained;
        const auto& before = event.beforeActor;
        const bool confirmed = event.originalReturned && !event.unwound && event.lifecycleStable &&
            before.available && before.combat && event.beforeStampAvailable && event.afterStampAvailable &&
            TraceStampMatchesCensus(event.beforeStamp, census) && TraceStampMatchesCensus(event.afterStamp, census) &&
            ConfirmDiagnosticActor(census, before.actor, before.objentry, before.status,
                                   before.objectId, before.controller, before.record);
        const auto drainGeneration = g_bridge.SessionGeneration();
        int netId = confirmed && event.roleAvailable ? DiagnosticNetId(before.actor, before.objectId, drainGeneration) : 0;
        if (!g_log) continue;
        auto bindingEpoch = netId ? (g_role == Role::Host ? g_epoch : g_host.epoch) : 0;
        if (!DiagnosticBindingContext(drainGeneration)) { netId = 0; bindingEpoch = 0; }
        // This is a later membership observation, not a removal/death verdict,
        // pre-removal binding, allocation generation or host creation identity.
        g_log("[lifecycletrace] event seq=%llu parent=%llu depth=%u kindId=%u kind=%s callerRva=%llX callerInImage=%u role=%u roleAvailable=%u actorArgument=%llX controllerArgument=%llX originalReturned=%u unwound=%u lifecycleStable=%u postActorComparable=%u controllerComparable=%u controllerFromActor=%u actorControllerMismatch=%u beforeStampAvailable=%u afterStampAvailable=%u unavailable=%u outOfScope=%u exceptionCode=%08X censusComplete=%u censusCurrentPresent=%u bindingEpoch=%u netId=%d drainGeneration=%u",
              static_cast<unsigned long long>(event.sequence), static_cast<unsigned long long>(event.parentSequence),
              event.depth, static_cast<unsigned>(event.kind), LifecycleKindName(event.kind),
              static_cast<unsigned long long>(event.callerRva), event.callerInImage ? 1u : 0u, event.role, event.roleAvailable ? 1u : 0u,
              static_cast<unsigned long long>(event.actor), static_cast<unsigned long long>(event.controller),
              event.originalReturned ? 1u : 0u, event.unwound ? 1u : 0u, event.lifecycleStable ? 1u : 0u,
              event.postActorComparable ? 1u : 0u, event.controllerComparable ? 1u : 0u,
              event.controllerFromActor ? 1u : 0u, event.actorControllerMismatch ? 1u : 0u,
              event.beforeStampAvailable ? 1u : 0u, event.afterStampAvailable ? 1u : 0u,
              event.unavailable ? 1u : 0u, event.outOfScope ? 1u : 0u, event.exceptionCode,
              census.state == CensusState::Complete ? 1u : 0u, confirmed ? 1u : 0u,
              bindingEpoch, netId, drainGeneration);
        if (event.kind == lifecycletrace::Kind::RemovalPredicate) {
            const auto& predicate = event.removal;
            g_log("[lifecycletrace] predicate seq=%llu coverageMask=%u coverageGeneration=%llu coverageStable=%u originalReturned=%u resultAvailable=%u parentResult=%u scriptCalls=%u scriptReturned=%u scriptResult=%u auxiliaryCalls=%u auxiliaryReturned=%u auxiliaryResult=%u faultMask=%u unwindMask=%u countOverflow=%u nestedAmbiguous=%u branch=%u auxiliaryArgument=%llX auxiliaryBefore=%u auxiliaryAfter=%u auxiliaryAvailableMask=%u",
                  static_cast<unsigned long long>(event.sequence), predicate.coverageMask,
                  static_cast<unsigned long long>(predicate.coverageGeneration), predicate.coverageStable ? 1u : 0u,
                  predicate.originalReturned ? 1u : 0u, predicate.resultAvailable ? 1u : 0u,
                  static_cast<unsigned>(predicate.parentResult), predicate.scriptCalls, predicate.scriptReturned,
                  static_cast<unsigned>(predicate.scriptResult), predicate.auxiliaryCalls, predicate.auxiliaryReturned,
                  static_cast<unsigned>(predicate.auxiliaryResult), predicate.faultMask, predicate.unwindMask,
                  predicate.countOverflow ? 1u : 0u, predicate.nestedAmbiguous ? 1u : 0u,
                  static_cast<unsigned>(predicate.branch), static_cast<unsigned long long>(predicate.auxiliaryArgument),
                  predicate.auxiliaryBefore, predicate.auxiliaryAfter, predicate.auxiliaryAvailableMask);
            LogRemovalOperands(event.sequence, "before", predicate.before);
            LogRemovalOperands(event.sequence, "afterScript", predicate.afterScript);
            LogRemovalOperands(event.sequence, "after", predicate.after);
        }
        LogLifecycleActor(event.sequence, "before", event.beforeActor);
        LogLifecycleActor(event.sequence, "after", event.afterActor);
        LogLifecycleStamp(event.sequence, "before", event.beforeStamp);
        LogLifecycleStamp(event.sequence, "after", event.afterStamp);
        LogTraceState("[lifecycletrace]", event.sequence, "before", event.beforeState);
        LogTraceState("[lifecycletrace]", event.sequence, "after", event.afterState);
    }
}


// Deferred child evidence only. No callback, native pointer/string read, or
// current-parent lookup occurs here. Full receipts, never partial prefixes,
// consume the independent DLL-lifetime logging budget.
constexpr unsigned ResourceDrainCap = 8;
constexpr std::uint64_t ResourceLogCap = 512;
struct ResourceLogCounters {
    std::uint64_t consumed = 0, logged = 0, suppressedNoLogger = 0, suppressedBudget = 0;
    std::uint64_t summaryMs = 0, summaryGeneration = 0;
    resourcetrace::InstallStatus summaryStatus = resourcetrace::InstallStatus::Disabled;
};
ResourceLogCounters g_resourceLog;

void LogResourceObservation(const resourcetrace::ResourceObservation& e) {
    if (!g_log) return;
    const auto invocation = static_cast<unsigned long long>(e.invocation);
    const auto parentCallback = static_cast<unsigned long long>(e.parentCallback);
    const auto generation = static_cast<unsigned long long>(e.generation);
    const auto outerBoundary = static_cast<unsigned long long>(e.outerBoundary);
    const auto parentSerial = static_cast<unsigned long long>(e.parent.serial);
    const auto parentCoverage = static_cast<unsigned long long>(e.parent.coverage);
    const auto wrapperSequence = static_cast<unsigned long long>(e.parent.wrapperSequence);
    g_log("[resourcetrace] terminal schema=1 invocation=%llu parentCallback=%llu generation=%llu outerBoundary=%llu parentSerial=%llu parentCoverage=%llu wrapperSequence=%llu actualEnteredTarget=%llX caller=%llX callerKind=%u package=%llX filename=%llX optionalRoot=%llX threadId=%u depth=%u rawRax=%llX al=%u normalReturn=%u unwound=%u outerNormalReturn=%u outerUnwound=%u installationIdentityVerified=%u boundaryDropped=%llu",
        invocation, parentCallback, generation, outerBoundary, parentSerial, parentCoverage, wrapperSequence,
        static_cast<unsigned long long>(e.actualEnteredTarget), static_cast<unsigned long long>(e.caller), static_cast<unsigned>(e.callerKind), static_cast<unsigned long long>(e.package),
        static_cast<unsigned long long>(e.filename), static_cast<unsigned long long>(e.optionalRoot), static_cast<unsigned>(e.threadId), static_cast<unsigned>(e.depth),
        static_cast<unsigned long long>(e.rawRax), static_cast<unsigned>(e.al), e.normalReturn ? 1U : 0U, e.unwound ? 1U : 0U,
        e.outerNormalReturn ? 1U : 0U, e.outerUnwound ? 1U : 0U, e.installationIdentityVerified ? 1U : 0U, static_cast<unsigned long long>(e.boundaryDropped));
    char record[129] {}; TraceHex(e.parent.recordBytes, record);
    g_log("[resourcetrace] parent schema=1 invocation=%llu parentCallback=%llu generation=%llu outerBoundary=%llu parentSerial=%llu parentCoverage=%llu wrapperSequence=%llu controller=%llX record=%llX threadId=%u recordIndex=%u available=%u recordIndexAvailable=%u recordBytesAvailable=%u recordHex=%s",
        invocation, parentCallback, generation, outerBoundary, parentSerial, parentCoverage, wrapperSequence,
        static_cast<unsigned long long>(e.parent.controller), static_cast<unsigned long long>(e.parent.record), e.parent.threadId,
        e.parent.recordIndex, e.parent.available ? 1U : 0U, e.parent.recordIndexAvailable ? 1U : 0U, e.parent.recordBytesAvailable ? 1U : 0U, record);
    g_log("[resourcetrace] sample schema=1 invocation=%llu parentCallback=%llu generation=%llu outerBoundary=%llu parentSerial=%llu parentCoverage=%llu wrapperSequence=%llu sampledVtable=%llX sampledSlot0=%llX sampledCurrentPackage=%llX sampledCount=%d sampledIndex=%d sampledMode=%u vtableRead=%u slotRead=%u countRead=%u indexRead=%u currentPackageRead=%u modeRead=%u",
        invocation, parentCallback, generation, outerBoundary, parentSerial, parentCoverage, wrapperSequence,
        static_cast<unsigned long long>(e.sampledVtable), static_cast<unsigned long long>(e.sampledSlot0), static_cast<unsigned long long>(e.sampledCurrentPackage), e.sampledCount,
        e.sampledIndex, static_cast<unsigned>(e.sampledMode), e.vtableRead ? 1U : 0U, e.slotRead ? 1U : 0U,
        e.countRead ? 1U : 0U, e.indexRead ? 1U : 0U, e.currentPackageRead ? 1U : 0U, e.modeRead ? 1U : 0U);
    g_log("[resourcetrace] limits schema=1 invocation=%llu parentCallback=%llu generation=%llu outerBoundary=%llu parentSerial=%llu parentCoverage=%llu wrapperSequence=%llu dispatchOperandObserved=%u completeRouting=%u fiberContinuityProven=%u continuousModeProven=%u globalPendingExcluded=%u lifetimeProven=%u creatorExclusive=%u atomic=%u creationAuthority=%u",
        invocation, parentCallback, generation, outerBoundary, parentSerial, parentCoverage, wrapperSequence,
        e.dispatchOperandObserved ? 1U : 0U, e.completeRouting ? 1U : 0U, e.fiberContinuityProven ? 1U : 0U, e.continuousModeProven ? 1U : 0U,
        e.globalPendingExcluded ? 1U : 0U, e.lifetimeProven ? 1U : 0U, e.creatorExclusive ? 1U : 0U, e.atomic ? 1U : 0U,
        e.creationAuthority ? 1U : 0U);
    for (unsigned index = 0; index < 2; ++index) {
        const auto& sample = index == 0 ? e.filenameSample : e.rootSample;
        std::array<std::uint8_t, 256> raw {};
        for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<std::uint8_t>(sample.bytes[i]);
        char hex[513] {}; TraceHex(raw, hex);
        // Do not dereference sampled pointers, trust length as an extent, stop
        // at embedded NUL, or reinterpret unread/truncated storage as a string.
        g_log("[resourcetrace] string schema=1 invocation=%llu parentCallback=%llu generation=%llu outerBoundary=%llu parentSerial=%llu parentCoverage=%llu wrapperSequence=%llu index=%u capacity=256 length=%u pointerNonNull=%u readable=%u terminated=%u truncated=%u bytesHex=%s",
            invocation, parentCallback, generation, outerBoundary, parentSerial, parentCoverage, wrapperSequence,
            index, sample.length, sample.pointerNonNull ? 1U : 0U, sample.readable ? 1U : 0U,
            sample.terminated ? 1U : 0U, sample.truncated ? 1U : 0U, hex);
    }
    g_log("[resourcetrace] end schema=1 invocation=%llu parentCallback=%llu generation=%llu outerBoundary=%llu parentSerial=%llu parentCoverage=%llu wrapperSequence=%llu rows=7 stringSamples=2 stringBytes=256 parentCompletenessProven=0",
        invocation, parentCallback, generation, outerBoundary, parentSerial, parentCoverage, wrapperSequence);
}

void DrainResourceTrace() {
    const auto stats = resourcetrace::GetStatistics();
    if (stats.status == resourcetrace::InstallStatus::Disabled && !stats.recording && !stats.resourcesMayBeReferenced &&
        !stats.entered && !stats.returned && !stats.unwound && !stats.dropped && !stats.unparented && !stats.foreign && !stats.published) return;
    resourcetrace::ResourceObservation event;
    for (unsigned drained = 0; drained < ResourceDrainCap && resourcetrace::Pop(event); ++drained) {
        ++g_resourceLog.consumed;
        if (!g_log) { ++g_resourceLog.suppressedNoLogger; continue; }
        if (g_resourceLog.logged >= ResourceLogCap) { ++g_resourceLog.suppressedBudget; continue; }
        LogResourceObservation(event); ++g_resourceLog.logged;
    }
    const auto now = GetTickCount64();
    if (g_log && (g_resourceLog.summaryMs == 0 || now - g_resourceLog.summaryMs >= 1000 ||
        stats.status != g_resourceLog.summaryStatus || stats.generation != g_resourceLog.summaryGeneration)) {
        // Stats are sampled before Pop, not an atomic inventory. Failed installs
        // and retired recording still get summaries even with no child rows.
        g_log("[resourcetrace] summary schema=1 status=%u generation=%llu recording=%u resourcesMayBeReferenced=%u modulePinned=%u installationIdentityVerified=%u producerEntered=%llu producerReturned=%llu producerUnwound=%llu producerDropped=%llu producerUnparented=%llu producerForeign=%llu producerPublished=%llu consumed=%llu logged=%llu suppressedNoLogger=%llu suppressedBudget=%llu logCap=%llu drainCap=%u emptyMeansAbsent=0 parentCompletenessProven=0 creationAuthority=0 flsRefusedCumulative=%llu flsFirstReason=%u",
            static_cast<unsigned>(stats.status), static_cast<unsigned long long>(stats.generation), stats.recording ? 1U : 0U,
            stats.resourcesMayBeReferenced ? 1U : 0U, stats.modulePinned ? 1U : 0U, stats.installationIdentityVerified ? 1U : 0U,
            static_cast<unsigned long long>(stats.entered), static_cast<unsigned long long>(stats.returned), static_cast<unsigned long long>(stats.unwound),
            static_cast<unsigned long long>(stats.dropped), static_cast<unsigned long long>(stats.unparented), static_cast<unsigned long long>(stats.foreign),
            static_cast<unsigned long long>(stats.published), static_cast<unsigned long long>(g_resourceLog.consumed), static_cast<unsigned long long>(g_resourceLog.logged),
            static_cast<unsigned long long>(g_resourceLog.suppressedNoLogger), static_cast<unsigned long long>(g_resourceLog.suppressedBudget),
            static_cast<unsigned long long>(ResourceLogCap), ResourceDrainCap,
            static_cast<unsigned long long>(stats.flsRefused), stats.flsFirstReason);
        g_resourceLog.summaryMs = now; g_resourceLog.summaryStatus = stats.status; g_resourceLog.summaryGeneration = stats.generation;
    }
}

void DrainPendingSpawnTrace(bool correlate) {
    nativehittrace::Drain(g_log, g_hitTraceFrame);
    DrainResourceTrace();
    const auto stats = spawncontroller::GetTraceStats();
    const auto lifecycle = lifecycletrace::GetStats();
    if (!stats.requested && !lifecycle.requested) return;
    NativeCensus census;
    // Client native lethal can alter membership. Never correlate using the
    // pre-apply census or turn an unsafe/failed frame into an empty observation.
    if (correlate && (stats.published != g_traceDrained || lifecycle.published != g_lifecycleDrained) && g_bridge.IsOpen() &&
        g_inst.live && SafeNativeGameplay()) census = CaptureNativeCensus();
    DrainSpawnTrace(census);
    DrainLifecycleTrace(census);
}

bool ActivationContext(Role role, RoomTransition& location) {
    CheckActivationGeneration();
    if (role == Role::Off || CurrentRole() != role || g_role != role ||
        !g_orderedDeliverySerial || g_bridge.DeliverySerial() != g_orderedDeliverySerial ||
        g_activationGeneration == 0 || g_activationOrderedGeneration != g_activationGeneration ||
        !g_inst.live || g_seenTransition != warp::TransitionSerial() ||
        g_seenLoad != warp::LoadSerial() || !SafeNativeGameplay() ||
        !ReadLocationChecked(location) || location.worldId != g_inst.world ||
        location.roomId != g_inst.room || location.door != g_inst.door ||
        location.mapProgram != g_inst.map || location.battleProgram != g_inst.btl ||
        location.eventProgram != g_inst.evt) return false;
    if (role == Role::Host) {
        if (g_hostBeginPending || !progresssync::HostReady()) return false;
        location.epoch = g_epoch;
    } else {
        if (!g_host.arrived || !warp::HostTransitionArrived(g_host.epoch)) return false;
        location.epoch = g_host.epoch;
    }
    return location.epoch != 0 && g_bridge.SessionGeneration() == g_activationGeneration;
}

bool SameClientClaimScope(const ClientClaimScope& a, const ClientClaimScope& b) {
    return a.generation == b.generation && a.ordered == b.ordered && a.delivery == b.delivery &&
        a.transition == b.transition && a.load == b.load && a.roster == b.roster && a.slot == b.slot &&
        a.room.epoch == b.room.epoch && SameLocation(a.room, b.room);
}
void LogClientClaim(const char* action, const char* reason, std::size_t rows, const HitClaim* claim) {
    auto& h = g_clientClaimHold;
    if (h.sequence == UINT64_MAX) { h.poisoned = true; return; }
    ++h.sequence;
    if (!g_log || (h.sequence > 4096 && std::strcmp(action, "seal") != 0)) {
        if (h.receiptGaps != UINT64_MAX) ++h.receiptGaps;
        return;
    }
    const auto& s = h.scope;
    // No runtime session string exists in WorldBridge. Only an admitted native
    // resync supplies it; a missing identity is explicit and never fabricated.
    const auto* n = g_nativeResync ? &*g_nativeResync : nullptr;
    g_log("[client-claims] schema=1 seq=%llu holdId=%llu action=%s reason=%s held=%u bound=%u generation=%u ordered=%u delivery=%llu slot=%u host=%llu self=%llu peer0=%llu peer1=%llu peer2=%llu epoch=%u world=%u room=%u door=%u map=%u battle=%u event=%u load=%u transition=%u manifestRevision=%llu hpSequence=%llu frame=%u rows=%zu dropped=%llu submitted=%llu receiptGaps=%llu transactionAvailable=%u session=%s request=%llu phase=%u cut=%llu targetConnection=%llu targetDelivery=%llu claimAvailable=%u claimSeq=%u claimNetId=%u claimObjectId=%u claimRequester=%llu matchComplete=%u missing=%d extra=%d conflicts=%d replaySeq=%llu replayReceiptGaps=%llu",
        static_cast<unsigned long long>(h.sequence), static_cast<unsigned long long>(h.id), action, reason,
        static_cast<unsigned>(h.held), static_cast<unsigned>(h.bound), s.generation, s.ordered,
        static_cast<unsigned long long>(s.delivery), static_cast<unsigned>(s.slot),
        static_cast<unsigned long long>(s.roster[0]), static_cast<unsigned long long>(s.slot < 3 ? s.roster[s.slot] : 0),
        static_cast<unsigned long long>(s.roster[0]), static_cast<unsigned long long>(s.roster[1]), static_cast<unsigned long long>(s.roster[2]),
        s.room.epoch, s.room.worldId, s.room.roomId, s.room.door, s.room.mapProgram, s.room.battleProgram, s.room.eventProgram,
        s.load, s.transition, static_cast<unsigned long long>(g_clientManifestRevision), static_cast<unsigned long long>(g_hostHpSequence),
        g_hitTraceFrame, rows, static_cast<unsigned long long>(h.dropped), static_cast<unsigned long long>(h.submitted),
        static_cast<unsigned long long>(h.receiptGaps), static_cast<unsigned>(n != nullptr),
        n ? n->begin.key.sessionId.c_str() : "unavailable", static_cast<unsigned long long>(n ? n->begin.key.requestId : 0),
        n ? static_cast<unsigned>(n->begin.phase) : 0, static_cast<unsigned long long>(n ? n->begin.snapshotCut : 0),
        static_cast<unsigned long long>(n ? n->target.connectionId : 0), static_cast<unsigned long long>(n ? n->target.deliverySerial : 0),
        static_cast<unsigned>(claim != nullptr), claim ? claim->seq : 0, claim ? claim->netId : 0,
        claim ? claim->objectId : 0, static_cast<unsigned long long>(claim ? claim->requesterConnectionId : 0),
        static_cast<unsigned>(std::strcmp(action, "release") == 0),
        std::strcmp(action, "release") == 0 ? 0 : -1, std::strcmp(action, "release") == 0 ? 0 : -1,
        std::strcmp(action, "release") == 0 ? 0 : -1,
        static_cast<unsigned long long>(g_activationReceiptSequence), static_cast<unsigned long long>(g_activationReceiptGaps));
}
bool ReadClientClaimScope(ClientClaimScope& out) {
    out = {};
    if (!spawncontroller::IsDiagnosticGameThread() || !g_bridge.IsOpen() || CurrentRole() != Role::Client ||
        g_role != Role::Client) return false;
    out.generation = g_bridge.SessionGeneration(); out.ordered = g_activationOrderedGeneration;
    out.delivery = g_bridge.DeliverySerial(); out.slot = g_bridge.LocalSlot();
    out.transition = warp::TransitionSerial(); out.load = warp::LoadSerial();
    for (std::uint8_t i = 0; i < 3; ++i) out.roster[i] = g_bridge.ConnectionId(i);
    if (!ReadLocationChecked(out.room)) return false;
    out.room.epoch = g_host.epoch;
    if (g_resyncWriteFence == ResyncWriteFence::Waiting || g_resyncWriteFence == ResyncWriteFence::Failed) return false;
    if (!out.generation || out.generation != g_activationGeneration || out.ordered != out.generation ||
        !out.delivery || out.delivery != g_orderedDeliverySerial || out.slot < 1 || out.slot > 2 ||
        !out.roster[0] || !out.roster[out.slot] || !out.room.epoch || !out.load ||
        !SafeNativeGameplay() || !g_host.arrived || !g_inst.live ||
        out.transition != g_seenTransition || out.load != g_seenLoad ||
        !warp::MatchesArrivedHostTransition(out.room)) return false;
    return WorldSessionGeneration() == out.generation && g_bridge.DeliverySerial() == out.delivery;
}
bool EnsureClientClaimScope() {
    // This precedes any old release/replay predicate. A new header generation
    // cannot publish while its FIFO ordered marker is still waiting to drain.
    CheckActivationGeneration();
    ClientClaimScope current;
    if (!ReadClientClaimScope(current)) { InvalidateClientClaims("scope-unavailable"); return false; }
    auto& h = g_clientClaimHold;
    if (!h.bound || !SameClientClaimScope(h.scope, current)) {
        InvalidateClientClaims("scope-changed");
        if (h.id == UINT64_MAX) h.poisoned = true;
        else ++h.id;
        h.scope = current; h.bound = true; h.held = true;
        LogClientClaim("arm", "current-admitted-scope");
    }
    if (!h.held && h.releasedRevision != g_clientManifestRevision) {
        h.held = true;
        LogClientClaim("rearm", "manifest-or-hp-changed");
    }
    return !h.poisoned && h.bound && !h.held;
}
bool ReleaseClientClaims(std::uint32_t frame) {
    (void)EnsureClientClaimScope();
    auto& h = g_clientClaimHold;
    if (!h.bound || h.poisoned) return false;
    if (!h.held) return true;
    if (!g_host.manifestComplete || !g_hostHpSequence || g_host.enemies.empty() ||
        (g_activationRecovery && (!g_activationRecovery->reconciled ||
            (g_activationRecovery->phase != ActivationRecoveryPhase::LiveHold &&
             g_activationRecovery->phase != ActivationRecoveryPhase::Verified)))) return false;
    const auto scope = h.scope;
    const auto revision = g_clientManifestRevision, hpSequence = g_hostHpSequence;
    const auto census = CaptureNativeCensus();
    if (!CensusMatchesInstance(census) || !SameLocation(census.location, scope.room)) return false;
    std::size_t living = 0;
    for (const auto& [id, host] : g_host.enemies) {
        if (host.dead) continue;
        ++living;
        if (!id || !host.hpKnown || host.hp <= 0 || host.maxHp <= 0 || host.hp > host.maxHp) return false;
        const Spawn* selected = nullptr;
        for (const auto& spawn : g_inst.spawns) {
            if (!spawn.present || spawn.killed || spawn.netId != id) continue;
            if (selected) return false;
            selected = &spawn;
        }
        if (!selected || selected->objectId != host.objectId || host.battleProgram != g_inst.btl) return false;
        const auto* native = FindNativeEnemy(census, *selected);
        NativeEnemy repeated; bool combat = false;
        if (!native || native->hp != host.hp || native->maxHp != host.maxHp ||
            !ReadNativeEnemy(native->actor, repeated, combat) || !combat ||
            !SameNativeIdentity(*native, repeated) || repeated.objectType != native->objectType ||
            repeated.hp != host.hp || repeated.maxHp != host.maxHp) return false;
    }
    // All current native combat rows must be accounted for, including extras
    // and dying rows. Authoritative empty-set release is intentionally absent.
    if (!living || living != census.enemies.size()) return false;
    for (const auto& native : census.enemies) {
        unsigned matches = 0;
        for (const auto& spawn : g_inst.spawns) {
            const auto host = g_host.enemies.find(static_cast<std::uint16_t>(spawn.netId));
            if (spawn.present && !spawn.killed && spawn.netId > 0 && spawn.netId <= UINT16_MAX &&
                host != g_host.enemies.end() && !host->second.dead &&
                FindNativeEnemy(census, spawn) == &native) ++matches;
        }
        if (matches != 1) return false;
    }
    ClientClaimScope after;
    if (!ReadClientClaimScope(after) || !SameClientClaimScope(scope, after) ||
        revision != g_clientManifestRevision || hpSequence != g_hostHpSequence || !CensusMatchesInstance(census)) return false;
    LogClientClaim("match-begin", "full-living-manifest", living);
    for (const auto& spawn : g_inst.spawns) {
        if (!spawn.present || spawn.killed || spawn.netId <= 0) continue;
        const auto host = g_host.enemies.find(static_cast<std::uint16_t>(spawn.netId));
        if (host == g_host.enemies.end() || host->second.dead) continue;
        const auto* native = FindNativeEnemy(census, spawn);
        if (g_log && native && h.sequence <= 4096) g_log("[client-claims-row] schema=1 holdId=%llu witnessSeq=%llu frame=%u netId=%d objectId=%u objectType=%u actor=%llX objentry=%llX status=%llX hpObserved=%d maxHpObserved=%d hpAdmitted=%d maxHpAdmitted=%d hpSourceSequence=%llu",
            static_cast<unsigned long long>(h.id), static_cast<unsigned long long>(h.sequence), frame, spawn.netId, spawn.objectId, static_cast<unsigned>(native->objectType),
            static_cast<unsigned long long>(native->actor), static_cast<unsigned long long>(native->objentry),
            static_cast<unsigned long long>(native->status), native->hp, native->maxHp, host->second.hp, host->second.maxHp,
            static_cast<unsigned long long>(host->second.hpSourceSequence));
    }
    for (const auto& native : census.enemies) {
        NativeEnemy repeated; bool combat = false;
        if (!ReadNativeEnemy(native.actor, repeated, combat) || !combat ||
            !SameNativeIdentity(native, repeated) || native.objectType != repeated.objectType ||
            native.hp != repeated.hp || native.maxHp != repeated.maxHp) {
            InvalidateClientClaims("native-readback-bookend-changed"); return false;
        }
    }
    if (!ReadClientClaimScope(after) || !SameClientClaimScope(scope, after) ||
        revision != g_clientManifestRevision || hpSequence != g_hostHpSequence || !CensusMatchesInstance(census)) {
        InvalidateClientClaims("match-bookend-changed"); return false;
    }
    h.releasedRevision = revision; h.held = false;
    LogClientClaim("release", "complete-stable-readback", living);
    return true;
}

void RequestActivation() {
    RoomTransition location;
    if (!ActivationContext(Role::Client, location)) {
        g_activationLease.Clear();
        return;
    }
    const auto request = g_activationLease.Request(location, g_activationIncarnation,
                                                    g_bridge.LocalSlot(), GetTickCount64());
    if (request) Send(encode(*request));
}

void ReceiveActivationRequest(const ActivationRequest& request, std::uint64_t requesterDelivery) {
    RoomTransition location;
    if (request.requesterSlot < 1 || request.requesterSlot > 2 ||
        !RequesterCurrent(request.requesterSlot, g_bridge.ConnectionId(request.requesterSlot), requesterDelivery) ||
        !ActivationContext(Role::Host, location) ||
        !sameActivationLocation(request.location, location)) return;
    auto& pending = g_activationChallenges[request.requesterSlot - 1];
    if (pending && pending->request.incarnation == request.incarnation &&
        request.requestSeq <= pending->request.requestSeq) return;
    pending = ActivationChallenge {request, g_activationSourceSeq, GetTickCount64(),
                                   g_seenTransition, g_seenLoad, g_activationGeneration, {}};
    pending->requesterConnection = g_bridge.ConnectionId(request.requesterSlot);
    pending->requesterDelivery = requesterDelivery;
}

void FlushActivationResponses() {
    RoomTransition location;
    if (!ActivationContext(Role::Host, location)) {
        g_activationChallenges = {};
        return;
    }
    const auto now = GetTickCount64();
    for (auto& pending : g_activationChallenges) {
        if (!pending) continue;
        if (!RequesterCurrent(pending->request.requesterSlot, pending->requesterConnection, pending->requesterDelivery) ||
            pending->generation != g_activationGeneration || pending->transition != g_seenTransition ||
            pending->load != g_seenLoad || !sameActivationLocation(pending->request.location, location) ||
            now < pending->receivedMs || now - pending->receivedMs >= ActivationLease::kLeaseMs) {
            pending.reset();
            continue;
        }
        if (pending->response) {
            // This flush follows the fresh census and successful manifest
            // barrier, so a hook capture can never bypass its native emissions.
            if (!WorldContextCurrent(pending->responseContext)) { pending.reset(); continue; }
            if (SendCapturedWorld(encode(*pending->response), pending->responseContext)) pending.reset();
        }
    }
}

void QueueHostBeginInstance(const RoomTransition& location) {
    ClearPendingHits();
    g_pendingHostRoom = location;
    g_pendingHostRoomContext = {};
    CaptureWorldContext(g_pendingHostRoomContext);
    g_pendingHostRoom.epoch = g_epoch + 1;
    if (g_pendingHostRoom.epoch == 0) ++g_pendingHostRoom.epoch;
    // Spawn-pick shared bit: did THIS host load make every random pick through the shared path? A friend that
    // follows into a natively-picked room then keeps its own native draw instead of a mismatched shared pick.
    {
        const auto transition = warp::TransitionSerial();
        const auto outcome = g_spawnPickOutcome.transition == transition ? g_spawnPickOutcome
                                                                         : spawnpick::LoadOutcome {transition, 0, 0, 0};
        const auto stamp = spawnpick::HostStamp(g_spawnPickLive, outcome, g_bridge.SpawnPickSalt());
        g_pendingHostRoom.spawnPickShared = stamp.shared;
        g_pendingHostRoom.spawnPickSaltTag = stamp.saltTag;
    }
    g_hostBeginPending = true;
    g_lastHashMs = 0;
    g_manifestSent = false;
    for (Spawn& s : g_inst.spawns) {
        s.announced = false;
        s.deathSent = false;
    }
}

struct HostEventObservation {
    std::int32_t state = 0;
    uintptr_t context = 0;
    RoomTransition location {};
};

bool ReadHostEventObservation(HostEventObservation& out) {
    return ReadNative(g_exeBase + offsets::CUTSCENE_STATE, out.state) &&
        ReadNative(g_exeBase + offsets::EVENT_CONTEXT, out.context) &&
        ReadLocationChecked(out.location);
}

bool SameHostEventObservation(const HostEventObservation& a, const HostEventObservation& b) {
    return a.state == b.state && a.context == b.context && SameLocation(a.location, b.location);
}

// Called only by the admitted EnemySync owner. Early acquire bypasses gameplay
// safety, not affinity/session ordering. Release follows progress/room commit.
bool TickHostEventHold(std::uint32_t frame, bool allowRelease) {
    if (!g_eventHoldProducerEnabled || g_role != Role::Host || !g_epoch) return true;
    auto& publication = g_hostEventPublication;
    // g_epoch intentionally survives session retirement for monotonic room IDs;
    // it is not proof that this transport session admitted a room.
    if (!WorldContextCurrent(publication.admittedRoom)) return true;
    auto flush = [&]() {
        if (publication.failed) return false; // retire the session to recover
        if (!publication.pending) return true;
        if (GetTickCount64() >= publication.retryDeadlineMs) {
            publication.failed = true;
            if (g_log) g_log("[event-hold] failed reason=enqueue-deadline epoch=%u active=%u",
                publication.pending->epoch, publication.pending->active ? 1u : 0u);
            return false;
        }
        if (!WorldContextCurrent(publication.context) || publication.pending->epoch != g_epoch) {
            if (g_log) g_log("[event-hold] failed reason=retired-pending epoch=%u generation=%u delivery=%llu source=%llu",
                publication.pending->epoch, publication.context.generation,
                static_cast<unsigned long long>(publication.context.deliverySerial),
                static_cast<unsigned long long>(publication.context.hostSourceSerial));
            publication.failed = true;
            return false;
        }
        if (!SendCapturedWorld(encode(*publication.pending), publication.context)) return false;
        if (g_log) g_log("[event-hold] published active=%u epoch=%u evt=%u frame=%u tid=%lu generation=%u delivery=%llu source=%llu",
            publication.pending->active ? 1u : 0u, publication.pending->epoch,
            publication.pending->eventProgram, frame, GetCurrentThreadId(), publication.context.generation,
            static_cast<unsigned long long>(publication.context.deliverySerial),
            static_cast<unsigned long long>(publication.context.hostSourceSerial));
        publication.activePublished = publication.pending->active;
        publication.pending.reset();
        publication.context = {};
        return true;
    };
    // Never collapse an unqueued acquire into release or let later world
    // publications overtake its retry. The captured context is never retagged.
    if (!flush()) return false;
    HostEventObservation before {}, after {};
    if (!ReadHostEventObservation(before)) return true; // unknown is not idle
    const bool active = (before.state >= 2 && before.state <= 4) || before.context != 0;
    const bool idle = before.state == 0 && before.context == 0;
    if ((!active && !idle) || active == publication.activePublished) return true;
    if (!active && (!allowRelease || !SafeNativeGameplay() || g_hostBeginPending || !g_inst.live)) return true;
    ProducerWorldContext captured;
    if (!CaptureWorldContext(captured) || !ReadHostEventObservation(after) ||
        !SameHostEventObservation(before, after) || !WorldContextCurrent(captured)) return true;
    publication.pending = EventHold {g_epoch, active, before.location.eventProgram};
    publication.context = captured;
    publication.retryDeadlineMs = GetTickCount64() + 3000;
    if (g_log) g_log("[event-hold] observed active=%u epoch=%u state=%d context=%llX evt=%u frame=%u tid=%lu generation=%u delivery=%llu source=%llu releaseAfterRoom=%u",
        active ? 1u : 0u, g_epoch, before.state, static_cast<unsigned long long>(before.context),
        before.location.eventProgram, frame, GetCurrentThreadId(), captured.generation,
        static_cast<unsigned long long>(captured.deliverySerial),
        static_cast<unsigned long long>(captured.hostSourceSerial), allowRelease ? 1u : 0u);
    return flush();
}

bool HostBeginInstance() {
    if (!g_hostBeginPending) return g_epoch != 0;
    if (!SafeNativeGameplay() || !progresssync::HostReady()) return false;
    // Keep the exact room and candidate epoch across ring-pressure retries.
    // Progress Tick ran first, so its complete snapshot precedes this packet.
    if (!SendCapturedWorld(encode(g_pendingHostRoom), g_pendingHostRoomContext)) return false;
    const RoomTransition& t = g_pendingHostRoom;
    ClearPendingHits(); // claims captured for the previous epoch never migrate
    g_epoch = t.epoch;
    g_hostEventPublication.admittedRoom = g_pendingHostRoomContext;
    g_hostBeginPending = false;
    if (g_log) g_log("[enemysync] host arrived epoch=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u",
                     t.epoch, t.worldId, t.roomId, t.door, t.mapProgram,
                     t.battleProgram, t.eventProgram);
    return true;
}

bool CaptureRecordPopulation(const NativeCensus&, std::vector<recordbinding::Local>&, spawncontroller::NativeRecordCatalog&);
bool ResolveRecordPopulation(const NativeCensus&, bool host);

bool HostFrame(std::uint32_t frame, const std::vector<std::size_t>& newSpawns,
               const NativeCensus& census) {
    // The bounded event-control lane needs an authenticated empty host roster
    // before arming. An absent manifest is not evidence of an empty room.
    if (g_eventHoldProducerEnabled && !g_manifestSent && newSpawns.empty() &&
        g_inst.spawns.empty() && census.enemies.empty() && CensusMatchesInstance(census) &&
        g_epoch && !g_hostBeginPending && WorldContextCurrent(g_hostEventPublication.admittedRoom)) {
        EnemyManifest empty;
        empty.epoch = g_epoch;
        empty.replace = true;
        if (!Send(encode(empty))) return false;
        g_manifestSent = true;
        SYNC_LOG("[enemysync] host manifest epoch %u frame %u: complete empty census", g_epoch, frame);
    }
    // Claims can invoke native callbacks. Re-prove the complete population
    // before publishing, as well as before consuming the queued claims.
    if (g_recordBindingRequested && recordbinding::Population({g_inst.world,g_inst.room,g_inst.door,g_inst.map,g_inst.btl,g_inst.evt}))
        ResolveRecordPopulation(census, true);
    if (!newSpawns.empty()) {
        EnemyManifest m;
        m.epoch = g_epoch;
        m.replace = !g_manifestSent;
        for (const std::size_t i : newSpawns) {
            const Spawn& s = g_inst.spawns[i];
            EnemyManifestEntry e;
            e.netId = static_cast<std::uint16_t>(s.spawnIndex + 1);
            e.battleProgram = g_inst.btl;
            e.spawnIndex = s.spawnIndex;
            e.objectId = s.objectId;
            // A deferred room announcement can outlive an actor. The original
            // spawn point remains valid even after its native pointer expires.
            e.spawnPosition = s.spawnPos;
            if (g_recordBindingRequested && RecordFamily(s.objectId)) {
                const auto key=g_recordKeys.find(s.actor);
                if (key!=g_recordKeys.end()) e.recordKey=key->second;
                else return false; // retry the first complete population; no unkeyed experimental row
            }
            m.entries.push_back(e);
        }
        const bool keyed=std::any_of(m.entries.begin(),m.entries.end(),[](const auto& e){return e.recordKey.has_value();});
        if (!(keyed ? SendCapturedWorld(encode(m),g_recordContext) : Send(encode(m)))) return false;
        for (const auto i : newSpawns) g_inst.spawns[i].announced = true;
        g_manifestSent = true;
        SYNC_LOG("[enemysync] host manifest epoch %u frame %u: %zu new (%s), %zu total; last objectId=%u@%llX at (%.0f,%.0f,%.0f)",
                 g_epoch, frame, m.entries.size(), m.replace ? "replace" : "append", g_inst.spawns.size(),
                 g_inst.spawns[newSpawns.back()].objectId,
                 static_cast<unsigned long long>(g_inst.spawns[newSpawns.back()].actor),
                 m.entries.back().spawnPosition.x, m.entries.back().spawnPosition.y,
                 m.entries.back().spawnPosition.z);
    }

    // Despawns: refilled at the same point -> superseded (clients re-bind);
    // otherwise, after the grace time, reported as a death.
    const std::uint64_t now = GetTickCount64();
    for (Spawn& s : g_inst.spawns) {
        if (s.present || s.deathSent) continue;
        if (s.goneSinceMs == 0) s.goneSinceMs = now;
        bool refilled = false;
        for (const Spawn& t : g_inst.spawns) {
            if (t.spawnIndex > s.spawnIndex && t.objectId == s.objectId && SamePoint(t.spawnPos, s.spawnPos)) {
                refilled = true;
                break;
            }
        }
        if (refilled) {
            // Population terminal history includes superseded IDs as well as native deaths.
            if (g_populationRequested) {
                EnemyDeath terminal;terminal.epoch=g_epoch;
                terminal.netId=static_cast<std::uint16_t>(s.spawnIndex+1);
                if(!Send(encode(terminal)))continue;
            }
            s.deathSent = true;
            SYNC_LOG("[enemysync] host: spawn %u despawned, refilled at its point", s.spawnIndex);
        } else if (s.lastHp <= 0 || now - s.goneSinceMs > DESPAWN_GRACE_MS) {
            EnemyDeath d;
            d.epoch = g_epoch;
            d.netId = static_cast<std::uint16_t>(s.spawnIndex + 1);
            if (Send(encode(d))) {
                s.deathSent = true;
                SYNC_LOG("[enemysync] host death epoch %u netId %u (despawned, not refilled)", g_epoch, d.netId);
            }
        }
    }

    EnemyHp hp;
    hp.epoch = g_epoch;
    for (Spawn& s : g_inst.spawns) {
        if (!s.present) continue;
        s.goneSinceMs = 0;
        const auto* native = FindNativeEnemy(census, s);
        if (!native) return false;
        if (native->hp <= 0 && !s.deathSent) {
            EnemyDeath d;
            d.epoch = g_epoch;
            d.netId = static_cast<std::uint16_t>(s.spawnIndex + 1);
            if (Send(encode(d))) {
                s.deathSent = true;
                SYNC_LOG("[enemysync] host death epoch %u netId %u", g_epoch, d.netId);
            }
        }
        if (native->hp > 0) hp.entries.push_back({static_cast<std::uint16_t>(s.spawnIndex + 1),
                                                native->hp, native->maxHp});
    }
    if (frame % HP_INTERVAL_FRAMES == 0 && !hp.entries.empty()) {
        if (g_hpSourceSequence == std::numeric_limits<std::uint64_t>::max()) {
            if (!g_hpSourceExhaustionLogged) {
                g_hpSourceExhaustionLogged = true;
                if (g_log) g_log("[enemysync] host HP sequence exhausted; HP publication stopped until DLL lifetime ends");
            }
        } else {
            hp.sequence = ++g_hpSourceSequence;
            Send(encode(hp));
        }
    }
    return true;
}

// VUH-1515 fixture control file (host): first word, lower-cased; "" if absent.
void PollMirrorControl(std::uint32_t frame) {
    if (!g_mirrorControlPath[0] || frame % 30 != 0) return;
    char word[sizeof(g_mirrorPhase)] {};
    const HANDLE file = CreateFileA(g_mirrorControlPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    // A pipe or device could block ReadFile on the game thread: disk files only.
    if (file != INVALID_HANDLE_VALUE && GetFileType(file) != FILE_TYPE_DISK) {
        CloseHandle(file);
        return;
    }
    if (file != INVALID_HANDLE_VALUE) {
        char buffer[64] {};
        DWORD got = 0;
        if (ReadFile(file, buffer, sizeof(buffer) - 1, &got, nullptr)) {
            std::size_t n = 0;
            for (DWORD i = 0; i < got && n + 1 < sizeof(word); ++i) {
                const char c = buffer[i];
                if (c == ' ' || c == '\r' || c == '\n' || c == '\t') { if (n) break; continue; }
                word[n++] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
            }
        }
        CloseHandle(file);
    }
    if (std::strcmp(word, g_mirrorPhase) == 0) return;
    std::memcpy(g_mirrorPhase, word, sizeof(g_mirrorPhase));
    g_mirrorMuted = std::strcmp(word, "mute") == 0;
    if (g_log) g_log("[enemy-mirror] phase name=%s hostFrame=%u epoch=%u muted=%d", word[0] ? word : "-", frame, g_epoch,
                     g_mirrorMuted ? 1 : 0);
}

// Host values the codec would refuse never reach the wire (S3/S5 bounds).
bool MotionEntryInBounds(const EnemyMotionEntry& e) {
    const auto coord = [](float v) { return std::isfinite(v) && std::fabs(v) <= ENEMY_MOTION_MAX_COORD; };
    return e.motionId < ENEMY_MOTION_MAX_MOTION_ID && std::isfinite(e.motionTime) && e.motionTime >= 0.0f &&
        e.motionTime <= ENEMY_MOTION_MAX_TIME && std::isfinite(e.rotationY) &&
        std::fabs(e.rotationY) <= ENEMY_MOTION_MAX_ROTATION && coord(e.position.x) && coord(e.position.y) &&
        coord(e.position.z);
}

// VUH-1515 step 2: host pose/motion for bound, announced, living, allowlisted
// spawns, read from the census actors (frame-start: last frame's final pose).
void PublishHostMotion(std::uint32_t frame, const NativeCensus& census) {
    if (!g_mirrorRequested || g_role != Role::Host) return;
    PollMirrorControl(frame);
    if (frame % enemymirror::kPublishInterval != 0) return;
    EnemyMotion m;
    m.epoch = g_epoch;
    m.hostFrame = frame;
    const std::size_t count = g_inst.spawns.size();
    const std::size_t first = count ? g_motionRoundRobin % count : 0;
    bool truncated = false;
    for (std::size_t k = 0; k < count; ++k) {
        const Spawn& s = g_inst.spawns[(first + k) % count];
        if (!s.present || !s.announced || s.deathSent || !enemymirror::FamilyAllowed(s.objectId)) continue;
        const auto* native = FindNativeEnemy(census, s);
        if (!native || native->hp <= 0) continue;
        std::uint32_t parent = 0;
        EnemyMotionEntry e;
        const auto transform = native->actor + offsets::actor::ENTITY_TRANSFORM;
        if (!ReadNative(native->actor + 0x6A0, parent) || parent != 0 ||  // parented actors use +0x70: not mirrored
            !ReadNative(native->actor + offsets::actor::ANIM_ID, e.motionId) ||
            !ReadNative(native->actor + 0x158 + 0x44, e.motionTime) ||
            !ReadNative(transform + offsets::entity::POS_X, e.position.x) ||
            !ReadNative(transform + offsets::entity::POS_Y, e.position.y) ||
            !ReadNative(transform + offsets::entity::POS_Z, e.position.z) ||
            !ReadNative(transform + offsets::entity::ROT_Y, e.rotationY)) continue;
        if (!MotionEntryInBounds(e)) { ++g_motionOutOfBounds; continue; }
        if (m.entries.size() >= ENEMY_MOTION_MAX_ENTRIES) { truncated = true; break; }
        e.netId = static_cast<std::uint16_t>(s.spawnIndex + 1);
        e.objectId = s.objectId;
        e.flags = ENEMY_MOTION_ALIVE;
        m.entries.push_back(e);
        if (g_mirrorTrace && (g_latencyTraceBudget || frame % enemymirror::kTraceEvery == 0) && g_log)
            g_log("[enemy-mirror] trace-host netId=%u hostFrame=%u pos=%.1f,%.1f,%.1f motion=%u time=%.1f", e.netId, frame,
                  e.position.x, e.position.y, e.position.z, e.motionId, e.motionTime);
    }
    if (truncated) {  // N3: rotate the start so no netId is starved for good
        if (!g_motionTruncated && g_log) g_log("[enemy-mirror] host truncated frame=%u eligible>%zu", frame, ENEMY_MOTION_MAX_ENTRIES);
        ++g_motionTruncated;
        g_motionRoundRobin += ENEMY_MOTION_MAX_ENTRIES;
    }
    if (g_mirrorMuted) { g_motionMutedFrames += enemymirror::kPublishInterval; return; }  // fixture stale test
    if (m.entries.empty() || !m.epoch || g_motionSourceSequence == std::numeric_limits<std::uint64_t>::max()) return;
    m.sequence = ++g_motionSourceSequence;  // a failed enqueue consumes its number
    if (Send(encode(m))) ++g_motionPublished;
    else ++g_motionSendFailures;
    if (g_log && frame % 600 == 0)
        g_log("[enemy-mirror] host frame=%u epoch=%u entries=%zu published=%llu sendFailures=%llu muted=%llu "
              "outOfBounds=%llu truncated=%llu", frame, m.epoch, m.entries.size(),
              static_cast<unsigned long long>(g_motionPublished), static_cast<unsigned long long>(g_motionSendFailures),
              static_cast<unsigned long long>(g_motionMutedFrames), static_cast<unsigned long long>(g_motionOutOfBounds),
              static_cast<unsigned long long>(g_motionTruncated));
}

bool CopyCodeBytes(const std::uint8_t* at, std::uint8_t* out, std::size_t n) noexcept {
    __try {
        std::memcpy(out, at, n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// VUH-1788: the one native creation call, POD only so SEH is allowed. Game thread.
void* CallEnemyFactory(std::uint32_t objectId, const float* point4, float yaw, bool& fault) noexcept {
    fault = false;
    __try {
        return g_enemyFactory(objectId, point4, yaw);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fault = true;
        return nullptr;
    }
}

bool CensusHasActor(const NativeCensus& census, uintptr_t actor) {
    for (const auto& e : census.enemies) if (e.actor == actor) return true;
    return false;
}

// Actor + objentry + status, read now (C1: never the address alone).
enemypop::Identity ReadPopulationIdentity(uintptr_t actor) noexcept {
    enemypop::Identity id;
    uintptr_t objentry = 0, status = 0;
    if (!actor || !ReadNative(actor + offsets::actor::OBJENTRY_PTR, objentry) || !ReadNative(actor + 0x5C0, status))
        return id;
    id.actor = actor;
    id.objentry = objentry;
    id.status = status;
    return id;
}

// Any role, every complete census: a forced entry whose actor left the list is forgotten,
// so a later actor reusing its address/status slot is never mistaken for it (rev3 C1).
void PopulationPrune(std::uint32_t frame, const NativeCensus& census) {
    if (!g_populationRequested || census.state != CensusState::Complete) return;
    for (const auto& f : g_population.forced()) {
        if (f.id.actor && !CensusHasActor(census, f.id.actor)) {
            ++g_popForcedGone;
            if (g_log) g_log("[enemy-pop] forced-gone frame=%u netId=%u actor=%llX", frame, f.netId,
                             static_cast<unsigned long long>(f.id.actor));
            g_population.Forget(f.id.actor);
        }
    }
}

// Client, after a successful ClientFrame (bindings resolved for this census).
void PopulationTick(std::uint32_t frame, const NativeCensus& census) {
    if (!g_populationRequested || g_role != Role::Client) return;
    if (g_population.epoch() != g_host.epoch) g_population.Rebase(g_host.epoch);
    const auto isForced = [&](const Spawn& s) {
        for (const auto& f : g_population.forced())
            if (f.id.actor && f.id.actor == s.actor && f.id.objentry == s.objentry && f.id.status == s.status) return true;
        return false;
    };
    // S1: a native local copy claiming a forced copy's netId wins; the forced one goes.
    for (const auto& f : g_population.forced()) {
        if (!f.id.actor || f.yield) continue;
        for (const Spawn& s : g_inst.spawns) {
            if (s.present && s.actor != f.id.actor && !isForced(s) && s.objectId != 0 &&  // rev3: never itself
                (s.netId == f.netId || s.ordinaryLogCandidate == static_cast<int>(f.netId))) {
                ++g_popYields;
                if (g_log) g_log("[enemy-pop] yield frame=%u netId=%u forced=%llX native=%llX", frame, f.netId,
                                 static_cast<unsigned long long>(f.id.actor), static_cast<unsigned long long>(s.actor));
                g_population.MarkYield(f.id.actor);
                break;
            }
        }
    }
    // rev4 R2: the deadline and the planner act only on frames whose ordinary bindings were really
    // resolved (no resync fence: ObserveOnly/content resync publish every netId as -1).
    const bool bindingsResolved = g_resyncWriteFence == ResyncWriteFence::None;
    if (g_popBudgetGeneration != g_population.generation()) {  // rev4: a reset re-arms the budget-wait log
        g_popBudgetGeneration = g_population.generation();
        g_popBudgetWaitNetId = 0;
    }
    // rev3 C2: a forced copy that never bound within kBindDeadline resolved frames (host enemy alive) is removed.
    for (const auto& f : g_population.forced()) {
        if (!bindingsResolved || !f.id.actor || f.yield || f.boundOnce) continue;
        bool bound = false;
        for (const Spawn& s : g_inst.spawns)  // rev4 R1: any binding counts (it may serve another netId)
            if (s.present && s.actor == f.id.actor && s.netId > 0) { bound = true; break; }
        if (bound) { g_population.MarkBound(f.id.actor); continue; }
        const auto ticks = g_population.NoteUnbound(f.id.actor);
        if (ticks <= enemypop::kBindDeadline) continue;
        if (g_log) g_log("[enemy-pop] unbound-timeout frame=%u netId=%u actor=%llX age=%u resolvedFrames=%u", frame,
                         f.netId, static_cast<unsigned long long>(f.id.actor), frame - f.frame, ticks);
        g_population.MarkYield(f.id.actor);
    }
    // Admission is per candidate. One blocked heavy family cannot monopolize Plan.
    float admissionLimit = 0.0f, admissionUsed = 0.0f;
    const bool admissionRead = ReadNative(g_exeBase + RVA_ADMISSION_LIMIT, admissionLimit) &&
                               ReadNative(g_exeBase + RVA_ADMISSION_USED, admissionUsed);
    std::vector<enemypop::HostView> views;
    for (const auto& [id, h] : g_host.enemies) {
        if (h.battleProgram != g_inst.btl) continue;
        enemypop::HostView v;
        v.netId = id;
        v.objectId = h.objectId;
        v.allowed = enemymirror::FamilyAllowed(h.objectId);
        v.dead = h.dead;
        for (const Spawn& s : g_inst.spawns) if (s.present && s.netId == id) { v.boundLocally = true; break; }
        // C5: only an object id this instance's own game already spawned (its resources are loaded).
        for (const Spawn& s : g_inst.spawns) if (s.objectId == h.objectId && !isForced(s)) { v.loadedObject = true; break; }
        v.streamFresh = g_mirror.Drivable(id, g_mirrorFrame);
        uintptr_t admissionObjentry = 0;
        for (const Spawn& s : g_inst.spawns)
            if (s.objectId == h.objectId && s.objentry && !isForced(s)) {
                admissionObjentry = s.objentry; break;
            }
        std::uint8_t admissionWeight = 0;
        v.admissionAvailable = admissionRead && admissionObjentry &&
            ReadNative(admissionObjentry + 0x54, admissionWeight) &&
            enemypop::BudgetAllows(admissionLimit, admissionUsed, admissionWeight);
        for (const auto& f : g_population.forced())
            if (f.id.actor && f.netId == id && CensusHasActor(census, f.id.actor)) { v.forcedPresent = true; break; }
        if(!v.dead && !v.boundLocally && v.loadedObject && !v.admissionAvailable && frame%300==0 && g_log)
            g_log("[enemy-pop] admission-wait frame=%u netId=%u limit=%.2f used=%.2f weight=%u read=%u",
                frame,id,admissionLimit,admissionUsed,admissionWeight,admissionRead?1u:0u);
        views.push_back(v);
    }
    const bool safe = g_enemyFactory && g_inst.live && g_host.arrived && SafeNativeGameplay() &&
                      spawncontroller::IsDiagnosticGameThread();
    const std::uint16_t pick = bindingsResolved ? g_population.Plan(views, frame, safe) : std::uint16_t {0};  // rev4 R2
    bool budgetWait = false;
    if (pick) {
        const HostEnemy& h = g_host.enemies[pick];
        // rev3: the factory's admission (0x3A1F00) is a float budget; read it first. A refusal is not
        // an attempt: wait (no call) until the budget frees, logged once per netId per wait.
        uintptr_t objentry = 0;
        for (const Spawn& s : g_inst.spawns) if (s.objectId == h.objectId && s.objentry && !isForced(s)) { objentry = s.objentry; break; }
        float limit = 0.0f, used = 0.0f;
        std::uint8_t weight = 0;
        const bool budgetRead = objentry && ReadNative(g_exeBase + RVA_ADMISSION_LIMIT, limit) &&
                                ReadNative(g_exeBase + RVA_ADMISSION_USED, used) && ReadNative(objentry + 0x54, weight);
        if (!budgetRead || !enemypop::BudgetAllows(limit, used, weight)) {
            if (g_popBudgetWaitNetId != pick && g_log)
                g_log("[enemy-pop] budget-wait frame=%u netId=%u read=%d limit=%.2f used=%.2f weight=%u", frame, pick,
                      budgetRead ? 1 : 0, limit, used, weight);
            g_popBudgetWaitNetId = pick;
            ++g_popBudgetWaits;
            budgetWait = true;  // rev4: no call, but the status line below still runs
        }
    }
    if (pick && !budgetWait) {
        const HostEnemy& h = g_host.enemies[pick];
        enemymirror::Pose pose {};
        const float yaw = g_mirror.PoseAt(pick, g_mirrorFrame, pose) ? pose.rotationY : 0.0f;
        alignas(16) float point[4] = {h.spawnPos.x, h.spawnPos.y, h.spawnPos.z, 1.0f};
        uintptr_t objentry = 0;
        for (const Spawn& s : g_inst.spawns) if (s.objectId == h.objectId && s.objentry && !isForced(s)) { objentry = s.objentry; break; }
        std::uint8_t weight = 0;
        (void)(objentry && ReadNative(objentry + 0x54, weight));
        g_popBudgetWaitNetId = 0;
        // Last owner-thread admission read: no attempt or missing-clock reset on refusal.
        float finalLimit=0.0f,finalUsed=0.0f;std::uint8_t finalWeight=0;
        if(!objentry || !ReadNative(g_exeBase+RVA_ADMISSION_LIMIT,finalLimit) ||
            !ReadNative(g_exeBase+RVA_ADMISSION_USED,finalUsed) || !ReadNative(objentry+0x54,finalWeight) ||
            !enemypop::BudgetAllows(finalLimit,finalUsed,finalWeight) || !SafeNativeGameplay() ||
            !spawncontroller::IsDiagnosticGameThread() || g_resyncWriteFence!=ResyncWriteFence::None)return;
        bool fault = false;
        void* actor = CallEnemyFactory(h.objectId, point, yaw, fault);
        if (!actor && !fault && g_log) {  // rev3: say why a null came back
            float limitAfter = 0.0f, usedAfter = 0.0f;
            (void)ReadNative(g_exeBase + RVA_ADMISSION_LIMIT, limitAfter);
            (void)ReadNative(g_exeBase + RVA_ADMISSION_USED, usedAfter);
            g_log("[enemy-pop] factory-null frame=%u netId=%u limit=%.2f used=%.2f weight=%u admission=%d "
                  "(null with admission=1 means the 0xD50 allocation failed)", frame, pick, limitAfter, usedAfter, weight,
                  enemypop::BudgetAllows(limitAfter, usedAfter, weight) ? 1 : 0);
        }
        if (fault) {  // C3: never retry a faulted native constructor
            g_enemyFactory = nullptr;
            actor = nullptr;
            if (g_log) g_log("[enemy-pop] factory-disabled frame=%u netId=%u (fault)", frame, pick);
        }
        const auto identity = ReadPopulationIdentity(reinterpret_cast<uintptr_t>(actor));
        g_population.Attempted(pick, identity, frame);
        if (identity.actor) ++g_popSpawns; else ++g_popSpawnFailures;
        if (g_log)
            g_log("[enemy-pop] force-spawn frame=%u netId=%u objectId=%u point=%.1f,%.1f,%.1f yaw=%.2f actor=%llX fault=%d "
                  "forced=%zu objentry=%llX status=%llX", frame, pick, h.objectId, point[0], point[1], point[2], yaw,
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(actor)), fault ? 1 : 0,
                  g_population.forcedCount(), static_cast<unsigned long long>(identity.objentry),
                  static_cast<unsigned long long>(identity.status));
    }
    if (g_log && frame % 600 == 0)
        g_log("[enemy-pop] client frame=%u epoch=%u hostEnemies=%zu spawns=%llu failures=%llu forcedGone=%llu yields=%llu "
              "forced=%zu factory=%d", frame, g_host.epoch, views.size(), static_cast<unsigned long long>(g_popSpawns),
              static_cast<unsigned long long>(g_popSpawnFailures), static_cast<unsigned long long>(g_popForcedGone),
              static_cast<unsigned long long>(g_popYields), g_population.forcedCount(), g_enemyFactory ? 1 : 0);
}

// Client: advance the render cursor once per frame after this frame's packets.
void TickMirror(std::uint32_t frame) {
    g_mirrorFrame = frame;
    if (g_latencyTraceBudget && g_log && g_mirrorRequested && g_role != Role::Off) {
        --g_latencyTraceBudget;
        LARGE_INTEGER qpc {};
        QueryPerformanceCounter(&qpc);
        const auto& st = g_mirror.stats();
        g_log("[latency-enemy] role=%s frame=%u qpc=%lld epoch=%u newest=%u cursor=%.0f holds=%llu catchups=%llu "
              "snaps=%llu underrunFrames=%llu releases=%llu retakes=%llu accepted=%llu resets=%llu",
              g_role == Role::Host ? "host" : "client", frame, qpc.QuadPart, g_mirror.epoch(),
              g_mirror.newestFrame(), g_mirror.cursor(), static_cast<unsigned long long>(st.cursorHolds),
              static_cast<unsigned long long>(st.cursorCatchups), static_cast<unsigned long long>(st.cursorSnaps),
              static_cast<unsigned long long>(st.underrunFrames), static_cast<unsigned long long>(st.releases),
              static_cast<unsigned long long>(st.retakes), static_cast<unsigned long long>(st.accepted),
              static_cast<unsigned long long>(st.resets));
    }
    if (!g_mirrorRequested || g_role != Role::Client) return;
    if (g_mirror.epoch() != g_host.epoch) g_mirror.Reset(g_host.epoch);
    g_mirror.Tick(frame);
    // N2: on, arrived, and still no stream after 10 s: say so once (host flag off?).
    static std::uint32_t arrivedSince = 0;
    if (!g_host.arrived) arrivedSince = 0;
    else if (!arrivedSince) arrivedSince = frame ? frame : 1;
    else if (!g_mirrorFirstPacketLogged && !g_mirrorSilentLogged && frame - arrivedSince > 600) {
        g_mirrorSilentLogged = true;
        if (g_log) g_log("[enemy-mirror] client no-stream frame=%u arrivedFrame=%u (is KH2COOP_ENEMY_MIRROR=1 on the host?)",
                         frame, arrivedSince);
    }
    if (g_log && frame % 600 == 0) {
        const auto& st = g_mirror.stats();
        g_log("[enemy-mirror] client frame=%u epoch=%u accepted=%llu rejected=%llu stale=%llu releases=%llu retakes=%llu "
              "resets=%llu refusedFar=%llu",
              frame, g_mirror.epoch(), static_cast<unsigned long long>(st.accepted), static_cast<unsigned long long>(st.rejected),
              static_cast<unsigned long long>(st.staleSamples), static_cast<unsigned long long>(st.releases),
              static_cast<unsigned long long>(st.retakes), static_cast<unsigned long long>(st.resets),
              static_cast<unsigned long long>(g_mirrorRefusedFar));
    }
}

// ---- Client -----------------------------------------------------------------

void ReceiveHostHitClaim(const HitClaim& claim, std::uint64_t requesterDelivery) {
    RoomTransition location;
    const auto slot = static_cast<std::uint8_t>(claim.attackerSlot);
    if (slot < 1 || slot > 2 || claim.seq == 0 || claim.netId == 0 || claim.objectId == 0 ||
        claim.damage <= 0 || !std::isfinite(claim.attackerPosition.x) ||
        !std::isfinite(claim.attackerPosition.y) || !std::isfinite(claim.attackerPosition.z)) {
        LogClaim("reject", claim, "invalid claim");
        return;
    }
    if (!g_takeDamage || !ActivationContext(Role::Host, location) || claim.epoch != location.epoch ||
        claim.requesterConnectionId == 0 ||
        g_bridge.ConnectionId(slot) != claim.requesterConnectionId) {
        LogClaim("reject", claim, "host context or connection unavailable");
        return;
    }
    auto& sequence = g_claimSequences[slot];
    if (sequence.connection != claim.requesterConnectionId)
        sequence = {claim.requesterConnectionId, 0};
    if (claim.seq <= sequence.consumed) {
        LogClaim("reject", claim, "sequence already consumed");
        return;
    }
    for (std::size_t i = 0; i < g_hitCount; ++i) {
        const auto& queued = g_pendingHits[(g_hitHead + i) % HIT_PENDING_CAP].claim;
        if (queued.attackerSlot == claim.attackerSlot &&
            queued.requesterConnectionId == claim.requesterConnectionId && queued.seq == claim.seq) {
            LogClaim("reject", claim, "sequence already queued");
            return;
        }
    }
    if (g_hitCount == HIT_PENDING_CAP) {
        LogClaim("reject", claim, "pending queue full");
        return;
    }
    if (!RequesterCurrent(static_cast<std::uint8_t>(claim.attackerSlot), claim.requesterConnectionId, requesterDelivery)) {
        LogClaim("reject", claim, "requester delivery retired"); return;
    }
    g_pendingHits[(g_hitHead + g_hitCount) % HIT_PENDING_CAP] =
        {claim, g_activationGeneration, g_seenTransition, g_seenLoad, g_orderedDeliverySerial,
         requesterDelivery, GetTickCount64()};
    ++g_hitCount;
    LogClaim("receive", claim, "queued immutable claim");
}

bool ResyncMembership(const ResyncPlan& plan) {
    if (!g_bridge.IsOpen() || plan.request.key.hostConnectionId != g_bridge.ConnectionId(0) ||
        plan.targetCount == 0 || plan.targetCount > 2) return false;
    if (plan.request.connections[0] != g_bridge.ConnectionId(0)) return false;
    for (std::size_t i = 0; i < plan.targetCount; ++i)
        if (plan.targets[i].slot < 1 || plan.targets[i].slot > 2 ||
            g_bridge.ConnectionId(plan.targets[i].slot) != plan.targets[i].connectionId) return false;
    return true;
}

void QueueResyncFailure(ResyncResultReason reason, const char* error) {
    if (!g_resyncPlan) return;
    ResyncResult result; result.key = g_resyncPlan->request.key; result.reason = reason;
    result.targetCount = g_resyncPlan->targetCount;
    for (std::size_t i = 0; i < result.targetCount; ++i) {
        result.targets[i].target = g_resyncPlan->targets[i];
        result.targets[i].error = error;
    }
    if (CaptureWorldContext(g_resyncOutputContext)) g_resyncOutput = encode(result);
    g_resyncHostCaptured = true;
}

void QueueNativeAck(ResyncAckStatus status, const char* error, std::uint32_t frame = 0) {
    if (!g_nativeResync) return;
    auto& state = *g_nativeResync;
    if (status != ResyncAckStatus::Converged && PackPreparationActive()) g_survivingPack.Cancel();
    ResyncAck ack; ack.key = state.begin.key; ack.target = state.target; ack.phase = state.begin.phase;
    ack.snapshotCut = state.begin.snapshotCut; ack.snapshotSha256 = state.begin.sha256;
    ack.status = status; ack.error = error; ack.loadBefore = state.loadBefore;
    ack.loadAfter = warp::LoadSerial(); ack.observationFrame1 = state.firstFrame;
    ack.observationFrame2 = frame;
    if (status == ResyncAckStatus::Converged) {
        ack.observedRoom = state.snapshot.room; // just independently matched by ObserveNativeResync
        ack.observedFingerprint = state.snapshot.nativeFingerprint;
        ack.enemyCount = state.snapshot.livingCount; ack.deadCount = state.snapshot.deadCount;
        ack.checksMask = ResyncChecksComplete;
    }
    bool serialized = false;
    if (WorldContextCurrent(state.context)) {
        g_resyncOutputContext = state.context; g_resyncOutput = encode(ack);
        serialized = true;
    }
    if (ResyncLogAllowed()) {
        const auto snapshotHex = desyncDigestHex(ack.snapshotSha256);
        const auto observedHex = desyncDigestHex(ack.observedFingerprint);
        // All call-site errors are fixed one-line literals. This is a local
        // serialization receipt; the relay's matching result is separate proof.
        g_log("[resync] ack session=%s host=%llu request=%llu targetSlot=%u connection=%llu delivery=%llu phase=%u cut=%llu snapshotSHA=%s fingerprint=%s status=%u epoch=%u world=%u room=%u door=%u map=%u battle=%u event=%u enemyCount=%u deadCount=%u loadBefore=%u loadAfter=%u frame1=%llu frame2=%llu checksMask=%u serialized=%u bridgeSent=0 relayAccepted=unknown error=\"%.256s\"",
              ack.key.sessionId.c_str(), static_cast<unsigned long long>(ack.key.hostConnectionId),
              static_cast<unsigned long long>(ack.key.requestId), static_cast<unsigned>(ack.target.slot),
              static_cast<unsigned long long>(ack.target.connectionId), static_cast<unsigned long long>(ack.target.deliverySerial),
              static_cast<unsigned>(ack.phase), static_cast<unsigned long long>(ack.snapshotCut),
              snapshotHex.c_str(), observedHex.c_str(), static_cast<unsigned>(ack.status),
              ack.observedRoom.epoch, ack.observedRoom.worldId, ack.observedRoom.roomId, ack.observedRoom.door,
              ack.observedRoom.mapProgram, ack.observedRoom.battleProgram, ack.observedRoom.eventProgram,
              ack.enemyCount, ack.deadCount, ack.loadBefore, ack.loadAfter,
              static_cast<unsigned long long>(ack.observationFrame1), static_cast<unsigned long long>(ack.observationFrame2),
              ack.checksMask, serialized ? 1u : 0u, ack.error.c_str());
    }
    state.finished = true;
    if (status == ResyncAckStatus::Converged && serialized) {
        g_resyncRecordAuthority = ResyncRecordAuthority {state.snapshot, state.context};
        g_resyncWriteFence = ResyncWriteFence::Exact;
    } else {
        g_resyncWriteFence = ResyncWriteFence::Failed;
    }
}

void ReceiveResyncPlan(const ResyncPlan& plan) {
    if (!ResyncMembership(plan) || !plan.remainingMs || plan.remainingMs > RESYNC_TIMEOUT_MS) return;
    const auto role = CurrentRole();
    if ((role == Role::Host && plan.stage != ResyncPlanStage::CaptureRequested) ||
        (role == Role::Client && plan.stage != ResyncPlanStage::Fenced) || role == Role::Off) return;
    const auto now = GetTickCount64();
    if (g_resyncPlan && plan.request.key == g_resyncPlan->request.key) {
        g_resyncDeadline = (std::min)(g_resyncDeadline, now + plan.remainingMs);
        if (plan.phase == g_resyncPlan->phase) return; // duplicate never restarts capture or timeout
        if (plan.phase != ResyncPhase::Checkpoint) return;
    } else {
        if (g_resyncPlan && now < g_resyncDeadline) return;
        g_resyncDeadline = now + plan.remainingMs;
    }
    if (role == Role::Client) InvalidateClientClaims("admitted-plan-changed");
    g_resyncPlan = plan; g_resyncHostCaptured = false;
    if (role == Role::Client) {
        g_resyncWriteFence = plan.phase == ResyncPhase::Checkpoint ? ResyncWriteFence::ObserveOnly : ResyncWriteFence::Waiting;
        if (plan.phase == ResyncPhase::Bootstrap) g_resyncRecordAuthority.reset();
    }
    if (role == Role::Host) for (std::size_t i = 0; i < plan.targetCount; ++i) {
        const auto& target = plan.targets[i]; auto& floor = g_requesterDeliveries[target.slot];
        if (floor.connection != target.connectionId) floor = {target.connectionId, target.deliverySerial};
        else floor.minimum = (std::max)(floor.minimum, target.deliverySerial);
    }
    // The header can already announce the upcoming reset before this FIFO
    // record is consumed. Remember the previously armed marker, not that header.
    g_resyncPriorOrderedGeneration = g_lastOrderedGeneration;
    g_resyncFailureFloor = g_worldSendFailures;
    g_resyncOutput.clear(); g_nativeResync.reset(); g_activationRecovery.reset();
    // Retire already queued reverse claims and captured activation replies at
    // the authenticated fence; relay rejects older delivery scopes afterward.
    ClearPendingHits(); ClearActivation();
    if (ResyncLogAllowed()) {
        const auto& a = plan.targets[0]; const auto& b = plan.targets[1]; const auto& room = plan.request.room;
        g_log("[resync] plan session=%s host=%llu request=%llu phase=%u stage=%u targetCount=%u target0Slot=%u target0Connection=%llu target0Delivery=%llu target1Slot=%u target1Connection=%llu target1Delivery=%llu remainingMs=%u priorGeneration=%u epoch=%u world=%u room=%u door=%u map=%u battle=%u event=%u",
              plan.request.key.sessionId.c_str(), static_cast<unsigned long long>(plan.request.key.hostConnectionId),
              static_cast<unsigned long long>(plan.request.key.requestId), static_cast<unsigned>(plan.phase),
              static_cast<unsigned>(plan.stage), static_cast<unsigned>(plan.targetCount), static_cast<unsigned>(a.slot),
              static_cast<unsigned long long>(a.connectionId), static_cast<unsigned long long>(a.deliverySerial),
              static_cast<unsigned>(b.slot), static_cast<unsigned long long>(b.connectionId),
              static_cast<unsigned long long>(b.deliverySerial), plan.remainingMs, g_resyncPriorOrderedGeneration,
              room.epoch, room.worldId, room.roomId, room.door, room.mapProgram, room.battleProgram, room.eventProgram);
    }
}

// Reviewed Steam executable layout provenance used by KH2Offsets/native readers.
// This constant is a compatibility declaration, NOT a running-module digest.
constexpr std::array<std::uint8_t, 32> kResyncRecordLayout {
    0x90,0x02,0xB2,0xDE,0x6A,0x1F,0x91,0xA7,0x90,0xBD,0x06,0x73,0xDE,0x12,0x5D,0x1C,
    0xF8,0x33,0xF7,0x94,0x2B,0xFE,0xC8,0x27,0xCD,0xCF,0x6B,0xA6,0x4D,0x58,0x49,0xED
};
using RecordCatalog = spawncontroller::NativeRecordCatalog;
bool FreshRecordCatalog(const NativeCensus& census, RecordCatalog& catalog) {
    spawncontroller::CaptureNativeRecordCatalog(g_exeBase, kResyncRecordLayout, catalog);
    return catalog.status == NativeRecordContentStatus::Complete && catalog.associationComplete &&
        catalog.tableInventoryComplete && catalog.contentBytesComplete && catalog.lifecycleStable &&
        TraceStampMatchesCensus(catalog.before, census) && TraceStampMatchesCensus(catalog.after, census) &&
        CensusMatchesInstance(census);
}
bool SameRecordCatalogSample(const RecordCatalog& a, const RecordCatalog& b) {
    if (a.entryCount != b.entryCount || a.layoutSha256 != b.layoutSha256 ||
        a.before.location != b.before.location || a.before.load != b.before.load ||
        a.before.transition != b.before.transition) return false;
    for (std::uint32_t i = 0; i < a.entryCount; ++i) {
        const auto& x = a.entries[i]; const auto& y = b.entries[i];
        // Local bookends include +E and actual table/controller association.
        if (x.tableBefore != y.tableBefore || x.controllerBefore != y.controllerBefore ||
            x.content.header != y.content.header || x.content.records != y.content.records) return false;
    }
    return true;
}
bool ActivationState(const spawncontroller::TraceState& native, ResyncActivationState& state) {
    if (!native.controllerAvailable || !native.cacheAvailable || !std::isfinite(native.cooldown)) return false;
    state.flags = native.flags; state.currentCount = native.currentCount;
    state.initialCount = native.initialCount; state.cooldown = native.cooldown;
    state.stage = native.stage; state.activation = native.activation; state.nativeType = native.nativeType;
    state.headerId = native.headerId; state.recordCount = native.recordCount;
    state.cacheRoom = native.cacheRoom; state.cacheAge = native.cacheAge; state.cacheIds = native.cacheIds;
    return true;
}
bool SameActivationState(ResyncActivationState actual, ResyncActivationState expected, bool before) {
    // Bucket addresses/ages are local cache bookkeeping. +E is the explicitly
    // reviewed idempotent 0/1 mark, normalized by the selected type-2 dispatcher.
    actual.cacheAge = expected.cacheAge;
    if (before) {
        if (actual.activation > 1 || expected.activation > 1) return false;
        actual.activation = expected.activation;
    }
    return actual == expected;
}
void LogActivationRecovery(const char* action, const char* reason = "") {
    if (!g_nativeResync) return;
    if (g_activationReceiptSequence == UINT64_MAX) { g_activationReceiptGaps = UINT64_MAX; return; }
    ++g_activationReceiptSequence;
    if (!g_log || g_activationReceiptSequence > 4096) {
        if (g_activationReceiptGaps != UINT64_MAX) ++g_activationReceiptGaps;
        return;
    }
    const auto& snapshot = g_nativeResync->snapshot;
    const auto& transaction = *g_nativeResync;
    const auto* replay = snapshot.activationReplay ? &*snapshot.activationReplay : nullptr;
    std::array<std::uint32_t, 4> pointBits {};
    if (replay) std::memcpy(pointBits.data(), replay->point.data(), sizeof(pointBits));
    const auto definitionKey = replay && replay->definitionIndex < snapshot.recordDefinitions.size()
        ? snapshot.recordDefinitions[replay->definitionIndex].groupKey : 0;
    const auto* recovery = g_activationRecovery ? &*g_activationRecovery : nullptr;
    g_log("[resync-activation] action=%s reason=%s load=%u transition=%u controller=%llX hostFirstUpdate=%llu historicalTicks=%u liveTicks=%u reconciled=%u battleParityObserved=0",
        action, reason, recovery ? recovery->load : warp::LoadSerial(),
        recovery ? recovery->transition : warp::TransitionSerial(),
        static_cast<unsigned long long>(recovery ? recovery->controller : 0),
        static_cast<unsigned long long>(snapshot.activationReplay ? snapshot.activationReplay->firstUpdateSequence : 0),
        recovery ? recovery->historicalTicks : 0, recovery ? recovery->liveTicks : 0,
        recovery ? static_cast<unsigned>(recovery->reconciled) : 0);
    g_log("[resync-activation-key] schema=1 seq=%llu receiptGaps=%llu action=%s session=%s host=%llu request=%llu targetSlot=%u targetConnection=%llu targetDelivery=%llu phase=%u cut=%llu generation=%u delivery=%llu holdId=%llu completedSequence=%llu historicalBaseline=%llu liveBaseline=%llu load=%u transition=%u replayAvailable=%u hostFirstUpdate=%llu hostLoad=%u hostTransition=%u definitionKey=%u pointBits=%08X,%08X,%08X,%08X claimSeq=%llu historicalTicks=%u liveTicks=%u reconciled=%u",
        static_cast<unsigned long long>(g_activationReceiptSequence), static_cast<unsigned long long>(g_activationReceiptGaps),
        action, transaction.begin.key.sessionId.c_str(), static_cast<unsigned long long>(transaction.begin.key.hostConnectionId),
        static_cast<unsigned long long>(transaction.begin.key.requestId), static_cast<unsigned>(transaction.target.slot),
        static_cast<unsigned long long>(transaction.target.connectionId), static_cast<unsigned long long>(transaction.target.deliverySerial),
        static_cast<unsigned>(transaction.begin.phase), static_cast<unsigned long long>(transaction.begin.snapshotCut),
        transaction.context.generation, static_cast<unsigned long long>(transaction.context.deliverySerial),
        static_cast<unsigned long long>(g_clientClaimHold.id), static_cast<unsigned long long>(recovery ? recovery->lastSequence : 0),
        static_cast<unsigned long long>(recovery ? recovery->historicalBaseline : 0), static_cast<unsigned long long>(recovery ? recovery->liveBaseline : 0),
        recovery ? recovery->load : warp::LoadSerial(), recovery ? recovery->transition : warp::TransitionSerial(),
        static_cast<unsigned>(replay != nullptr), static_cast<unsigned long long>(replay ? replay->firstUpdateSequence : 0),
        replay ? replay->hostLoad : 0, replay ? replay->hostTransition : 0, definitionKey,
        pointBits[0], pointBits[1], pointBits[2], pointBits[3], static_cast<unsigned long long>(g_clientClaimHold.sequence),
        recovery ? recovery->historicalTicks : 0, recovery ? recovery->liveTicks : 0,
        recovery ? static_cast<unsigned>(recovery->reconciled) : 0);
}
bool ActivationDefinitionCurrent(const ActivationRecovery& recovery) {
    std::array<std::uint8_t, 16> table {};
    std::array<std::uint8_t, 64> controller {};
    std::array<std::uint8_t, 44> header {};
    std::array<std::array<std::uint8_t, 64>, 5> records {};
    uintptr_t nativeHeader = 0, nativeRecords = 0;
    if (!CopyNative(recovery.tableEntry, table.data(), table.size()) || table != recovery.tableBytes ||
        !CopyNative(recovery.controller, controller.data(), controller.size())) return false;
    std::memcpy(&nativeHeader, controller.data() + 8, sizeof(nativeHeader));
    std::memcpy(&nativeRecords, controller.data() + 0x30, sizeof(nativeRecords));
    if (nativeHeader != recovery.header || nativeRecords != recovery.records ||
        !CopyNative(recovery.header, header.data(), header.size()) || header[0xE] > 1 ||
        !CopyNative(recovery.records, records.data(), sizeof(records))) return false;
    header[0xE] = recovery.headerBytes[0xE];
    return header == recovery.headerBytes && records == recovery.recordBytes;
}
bool RecordMember(const RecordCatalog& catalog, const NativeCensus& census, uintptr_t actor,
                  spawncontroller::NativeRecordMembership& member) {
    spawncontroller::ResolveNativeRecordMembership(g_exeBase, catalog, actor, member);
    return member.status == NativeRecordContentStatus::Complete && member.contentComparisonAvailable &&
        TraceStampMatchesCensus(member.stamps[0], census) && TraceStampMatchesCensus(member.stamps[1], census) &&
        member.tableIndex < catalog.entryCount && member.recordIndex < catalog.entries[member.tableIndex].content.records.size();
}
std::uint32_t RecordObjectId(const std::array<std::uint8_t, 64>& record) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<std::uint32_t>(record[i]) << (8 * i);
    return value;
}
std::vector<NativeRecordContentCandidate> RecordCandidates(const std::vector<NativeRecordContentDefinition>& definitions) {
    std::vector<NativeRecordContentCandidate> candidates;
    for (const auto& definition : definitions)
        candidates.push_back({&definition, NativeRecordContentStatus::Complete, true});
    return candidates; // completeness/unique association supplied by the native capture or checked wire
}
#include "EnemyPopulationRepair.inl"

bool PopulationDeathHelperReady() {return g_populationDeathHelperVerified && g_applyStatDelta!=nullptr;}
bool InvokePopulationNativeDeath(uintptr_t actor,int hp) {
    __try {g_applyStatDelta(reinterpret_cast<void*>(actor),-hp,0,0);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}

struct NativeRecordWriteCheck {
    ProducerWorldContext context;
    RoomTransition room;
    std::uint32_t transition = 0, load = 0;
    uintptr_t controller = 0, record = 0, header = 0, spawnArray = 0, tableEntry = 0;
    std::int32_t tableCount = 0;
    std::array<std::uint8_t, 16> tableBytes {};
    std::array<std::uint8_t, 64> controllerBytes {};
    std::array<std::uint8_t, 44> headerBytes {};
    const void* records = nullptr;
    std::size_t recordBytes = 0;
};
struct ResyncActorBinding {
    uintptr_t actor = 0;
    std::uint16_t netId = 0;
    ResyncRecordReference record;
    NativeRecordWriteCheck write;
};
// Entire population/catalog preflight occurs before the first resync HP write.
// Missing actors and unreferenced-definition changes cannot become a passing subset.
// Returned content/pointers are frame-local sampled evidence, never incarnation or creation authority.
bool CollectResyncBindings(const ResyncSnapshot& expected, const ProducerWorldContext& context,
    const NativeCensus& census, std::vector<ResyncActorBinding>& bindings,
    std::vector<NativeRecordContentDefinition>& definitions) {
    bindings.clear(); definitions.clear();
    if (expected.coverageMask != ResyncNativeComplete || expected.deadCount || expected.hold.active ||
        !WorldContextCurrent(context) || !SafeNativeGameplay() || !g_host.arrived ||
        !CensusMatchesInstance(census) || !SameLocation(census.location, expected.room) ||
        !warp::MatchesArrivedHostTransition(expected.room) ||
        census.enemies.size() != expected.enemies.size() || g_host.enemies.size() != expected.enemies.size()) return false;
    RecordCatalog before, after;
    if (!FreshRecordCatalog(census, before) || before.entryCount != expected.recordDefinitions.size()) return false;
    std::vector<NativeRecordContentDefinition> local;
    for (std::uint32_t i = 0; i < before.entryCount; ++i) local.push_back(before.entries[i].content);
    const auto localCandidates = RecordCandidates(local);
    const auto hostCandidates = RecordCandidates(expected.recordDefinitions);
    for (const auto& definition : expected.recordDefinitions)
        if (ResolveNativeRecordContent(definition, localCandidates, before.status).status != NativeRecordContentStatus::Complete)
            return false;
    std::vector<ResyncActorBinding> staged;
    for (const auto& native : census.enemies) {
        spawncontroller::NativeRecordMembership member;
        if (!RecordMember(before, census, native.actor, member) || native.hp <= 0 || native.maxHp <= 0 ||
            native.hp > native.maxHp) return false;
        const auto resolved = ResolveNativeRecordContentRecord(local[member.tableIndex], member.recordIndex,
            hostCandidates, NativeRecordContentStatus::Complete);
        if (resolved.status != NativeRecordContentStatus::Complete || !resolved.definitionIndex || !resolved.recordIndex)
            return false;
        const ResyncRecordReference hostRef {static_cast<std::uint16_t>(*resolved.definitionIndex), *resolved.recordIndex};
        const auto row = std::find_if(expected.enemies.begin(), expected.enemies.end(), [&](const auto& e) {
            return e.record == hostRef;
        });
        if (row == expected.enemies.end() || row->life != ResyncLife::Alive ||
            row->identity.objectId != native.objectId || row->objectType != native.objectType ||
            row->maxHp != native.maxHp || RecordObjectId(local[member.tableIndex].records[member.recordIndex]) != native.objectId)
            return false;
        const auto host = g_host.enemies.find(row->identity.netId);
        if (host == g_host.enemies.end() || host->second.objectId != row->identity.objectId ||
            host->second.battleProgram != row->identity.battleProgram ||
            host->second.spawnIndex != row->identity.spawnIndex ||
            (!host->second.dead && (host->second.hp <= 0 || host->second.hp > native.maxHp))) return false;
        if (std::any_of(staged.begin(), staged.end(), [&](const auto& b) { return b.netId == row->identity.netId; })) return false;
        const auto tracked = g_inst.byActor.find(native.actor);
        if (tracked == g_inst.byActor.end() || tracked->second >= g_inst.spawns.size() ||
            !g_inst.spawns[tracked->second].present || !FindNativeEnemy(census, g_inst.spawns[tracked->second])) return false;
        NativeEnemy repeated; bool combat = false;
        if (!ReadNativeEnemy(native.actor, repeated, combat) || !combat || !SameNativeIdentity(native, repeated) ||
            native.objectType != repeated.objectType || native.hp != repeated.hp || native.maxHp != repeated.maxHp) return false;
        const auto& entry = before.entries[member.tableIndex];
        ResyncActorBinding binding;
        binding.actor = native.actor; binding.netId = row->identity.netId;
        binding.record = {static_cast<std::uint16_t>(member.tableIndex), member.recordIndex};
        binding.write.context = context; binding.write.room = expected.room;
        binding.write.transition = census.transition; binding.write.load = census.load;
        binding.write.controller = member.controller[0]; binding.write.record = member.record[0];
        binding.write.header = entry.header;
        binding.write.spawnArray = entry.spawnArray;
        binding.write.tableCount = static_cast<std::int32_t>(before.entryCount);
        binding.write.tableEntry = g_exeBase + 0x2A10010 + member.tableIndex * 16;
        binding.write.tableBytes = entry.tableBefore; binding.write.controllerBytes = entry.controllerBefore;
        binding.write.headerBytes = entry.content.header;
        staged.push_back(binding);
    }
    if (!FreshRecordCatalog(census, after) || !SameRecordCatalogSample(before, after)) return false;
    const auto repeatedCensus = CaptureNativeCensus();
    if (!CensusMatchesInstance(repeatedCensus) || repeatedCensus.enemies.size() != census.enemies.size()) return false;
    for (const auto& native : census.enemies) {
        const auto found = std::find_if(repeatedCensus.enemies.begin(), repeatedCensus.enemies.end(), [&](const auto& n) {
            return SameNativeIdentity(native, n) && native.objectType == n.objectType && native.hp == n.hp && native.maxHp == n.maxHp;
        });
        spawncontroller::NativeRecordMembership member;
        if (found == repeatedCensus.enemies.end() || !RecordMember(after, repeatedCensus, native.actor, member)) return false;
        const auto binding = std::find_if(staged.begin(), staged.end(), [&](const auto& b) { return b.actor == native.actor; });
        if (binding == staged.end() || binding->record.definitionIndex != member.tableIndex ||
            binding->record.recordIndex != member.recordIndex || binding->write.controller != member.controller[0] ||
            binding->write.record != member.record[0]) return false;
    }
    if (!WorldContextCurrent(context) || !CensusMatchesInstance(census) || !SafeNativeGameplay()) return false;
    definitions = std::move(local); bindings = std::move(staged);
    for (auto& binding : bindings) {
        const auto& records = definitions[binding.record.definitionIndex].records;
        binding.write.records = records.data(); binding.write.recordBytes = records.size() * 64;
    }
    return true;
}

bool CaptureHostResync(std::uint32_t frame, ResyncSnapshot& snapshot, ProducerWorldContext& context) {
    if (!g_resyncPlan || !ResyncMembership(*g_resyncPlan) || CurrentRole() != Role::Host ||
        g_hostBeginPending || !g_epoch || (!g_manifestSent && !g_inst.spawns.empty()) || !SafeNativeGameplay()) return false;
    const auto generation = WorldSessionGeneration(), transition = warp::TransitionSerial(), load = warp::LoadSerial();
    const auto delivery = g_bridge.DeliverySerial();
    auto before = CaptureNativeCensus();
    if (!generation || !CensusMatchesInstance(before) || g_inst.spawns.size() > RESYNC_MAX_ENEMIES) return false;
    snapshot = {}; snapshot.room = before.location; snapshot.room.epoch = g_epoch;
    if (!sameResyncRoom(snapshot.room, g_resyncPlan->request.room)) return false;
    snapshot.hold = {g_epoch, false, snapshot.room.eventProgram};
    std::uint32_t hash = 0;
    if (!progresssync::CaptureFull(snapshot.progress, hash)) return false;
    RecordCatalog recordsBefore, recordsAfter;
    if (!FreshRecordCatalog(before, recordsBefore)) return false;
    for (std::uint32_t i = 0; i < recordsBefore.entryCount; ++i)
        snapshot.recordDefinitions.push_back(recordsBefore.entries[i].content);
    for (const auto& spawn : g_inst.spawns) {
        if (!spawn.announced || spawn.spawnIndex == UINT16_MAX ||
            (spawn.objectType != offsets::objentry::TYPE_MOB && spawn.objectType != offsets::objentry::TYPE_BOSS)) return false;
        ResyncEnemyState row;
        row.identity = {static_cast<std::uint16_t>(spawn.spawnIndex + 1), g_inst.btl,
                        spawn.spawnIndex, spawn.objectId, spawn.spawnPos};
        row.objectType = spawn.objectType;
        if (spawn.present) {
            const auto* native = FindNativeEnemy(before, spawn);
            if (!native || native->objectType != spawn.objectType || native->maxHp <= 0) return false;
            spawncontroller::NativeRecordMembership member;
            if (!RecordMember(recordsBefore, before, native->actor, member) ||
                RecordObjectId(snapshot.recordDefinitions[member.tableIndex].records[member.recordIndex]) != native->objectId)
                return false;
            row.record = {static_cast<std::uint16_t>(member.tableIndex), member.recordIndex};
            row.hp = native->hp; row.maxHp = native->maxHp;
        } else {
            // A historical HP row cannot establish fresh actor-to-record membership.
            return false;
        }
        if (row.hp > 0) ++snapshot.livingCount;
        else { row.life = ResyncLife::ObservedDeadHistory; ++snapshot.deadCount; }
        snapshot.enemies.push_back(row);
    }
    const auto after = CaptureNativeCensus();
    ProgressUpdate repeated; std::uint32_t repeatedHash = 0;
    if (!CensusMatchesInstance(after) || after.enemies.size() != before.enemies.size() ||
        !progresssync::CaptureFull(repeated, repeatedHash) || hash != repeatedHash ||
        encode(repeated) != encode(snapshot.progress)) return false;
    for (const auto& native : before.enemies) {
        const auto row = std::find_if(after.enemies.begin(), after.enemies.end(), [&](const auto& value) {
            return SameNativeIdentity(native, value) && native.objectType == value.objectType &&
                native.hp == value.hp && native.maxHp == value.maxHp;
        });
        if (row == after.enemies.end()) return false;
        const auto tracked = g_inst.byActor.find(native.actor);
        if (tracked == g_inst.byActor.end() || tracked->second >= g_inst.spawns.size() ||
            !g_inst.spawns[tracked->second].present || !FindNativeEnemy(before, g_inst.spawns[tracked->second])) return false;
    }
    if (!FreshRecordCatalog(after, recordsAfter) || !SameRecordCatalogSample(recordsBefore, recordsAfter)) return false;
    for (const auto& native : after.enemies) {
        spawncontroller::NativeRecordMembership member;
        if (!RecordMember(recordsAfter, after, native.actor, member)) return false;
        const auto tracked = g_inst.byActor.find(native.actor);
        if (tracked == g_inst.byActor.end() || tracked->second >= snapshot.enemies.size()) return false;
        const auto& row = snapshot.enemies[tracked->second];
        if (row.record.definitionIndex != member.tableIndex || row.record.recordIndex != member.recordIndex) return false;
    }
    if (generation != WorldSessionGeneration() || delivery != g_bridge.DeliverySerial() ||
        transition != warp::TransitionSerial() || load != warp::LoadSerial() || !SafeNativeGameplay() ||
        g_hpSourceSequence == UINT64_MAX || !CaptureWorldContext(context)) return false;
    snapshot.hpSequence = ++g_hpSourceSequence; // explicit fresh HP sample even for an empty population
    snapshot.generation = generation; snapshot.transitionSerial = transition; snapshot.loadSerial = load;
    snapshot.captureFrameBefore = snapshot.captureFrameAfter = frame;
    snapshot.coverageMask = ResyncNativeComplete;
    if (g_survivingPackEnabled && snapshot.enemies.size() == 5 && snapshot.livingCount == 5 &&
        !snapshot.deadCount && !snapshot.hold.active) {
        const auto selected = snapshot.enemies.front().record.definitionIndex;
        const bool oneDefinition = std::all_of(snapshot.enemies.begin(), snapshot.enemies.end(),
            [selected](const auto& row) { return row.record.definitionIndex == selected; });
        spawncontroller::HostFirstEmission emission;
        ResyncActivationReplay replay;
        if (oneDefinition && selected < recordsAfter.entryCount &&
            spawncontroller::CopyHostFirstEmission(recordsAfter, selected, emission) &&
            ActivationState(emission.before, replay.before) &&
            ActivationState(spawncontroller::CaptureDiagnosticState(g_exeBase,
                recordsAfter.entries[selected].controller), replay.current)) {
            replay.definitionIndex = selected; replay.point = emission.point;
            replay.firstUpdateSequence = emission.updateSequence;
            replay.hostTransition = emission.stamp.transition; replay.hostLoad = emission.stamp.load;
            snapshot.activationReplay = replay;
            // Shared codec validates the narrow metadata before it enters a
            // snapshot; an unsupported observation simply supplies no replay.
            try { (void)resyncNativeFingerprint(snapshot); }
            catch (const std::exception&) { snapshot.activationReplay.reset(); }
        }
        if (g_log) g_log("[resync-activation] action=host-snapshot recorded=%u status=%u firstUpdate=%llu load=%u transition=%u reason=%s",
            static_cast<unsigned>(snapshot.activationReplay.has_value()), static_cast<unsigned>(emission.status),
            static_cast<unsigned long long>(emission.updateSequence), emission.stamp.load,
            emission.stamp.transition, emission.reason);
        if (g_log && snapshot.activationReplay) {
            std::array<std::uint32_t, 4> bits {};
            std::memcpy(bits.data(), snapshot.activationReplay->point.data(), sizeof(bits));
            g_log("[resync-activation] action=host-point firstUpdate=%llu initSerial=%llu pointBits=%08X,%08X,%08X,%08X firstRecord=%u actorMetadataAvailable=%u beforeFlags=%u beforeCount=%u beforeInitial=%u beforeStage=%u beforeActivation=%u",
                static_cast<unsigned long long>(emission.updateSequence),
                static_cast<unsigned long long>(emission.initSerial), bits[0], bits[1], bits[2], bits[3],
                static_cast<unsigned>(emission.firstRecordIndex), static_cast<unsigned>(emission.firstActorMetadataAvailable),
                emission.before.flags, emission.before.currentCount, emission.before.initialCount,
                static_cast<unsigned>(emission.before.stage), static_cast<unsigned>(emission.before.activation));
        }
    }
    snapshot.nativeFingerprint = resyncNativeFingerprint(snapshot);
    return WorldContextCurrent(context);
}

void ReceiveResyncSnapshot(const std::vector<std::uint8_t>& packet) {
    ResyncBegin begin; ResyncSnapshot snapshot;
    decodeNativeResyncSnapshot(packet, begin, snapshot); // exact shared validation before any mutation
    if (!g_resyncPlan || CurrentRole() != Role::Client || !ResyncMembership(*g_resyncPlan) ||
        !(begin.key == g_resyncPlan->request.key) || begin.phase != g_resyncPlan->phase ||
        !sameResyncRoom(begin.room, g_resyncPlan->request.room) ||
        begin.targetCount != g_resyncPlan->targetCount || begin.targets != g_resyncPlan->targets ||
        GetTickCount64() >= g_resyncDeadline) return;
    if (!snapshot.progress.version || snapshot.hold.eventProgram != snapshot.room.eventProgram ||
        std::any_of(snapshot.enemies.begin(), snapshot.enemies.end(), [&](const auto& row) {
            return row.identity.battleProgram != snapshot.room.battleProgram;
        })) return;
    if (g_nativeResync) return; // one immutable snapshot per phase, never reapply duplicates
    const auto slot = g_bridge.LocalSlot();
    auto target = std::find_if(begin.targets.begin(), begin.targets.begin() + begin.targetCount,
                              [slot](const auto& value) { return value.slot == slot; });
    if (target == begin.targets.begin() + begin.targetCount || target->deliverySerial != g_bridge.DeliverySerial()) return;
    ProducerWorldContext context;
    if (!CaptureWorldContext(context)) return;
    g_nativeResync = NativeResyncTarget {begin, snapshot, *target, context, warp::LoadSerial()};
    g_resyncAppliedCut = (std::max)(g_resyncAppliedCut, begin.snapshotCut);
    if (snapshot.coverageMask != ResyncNativeComplete) {
        QueueNativeAck(ResyncAckStatus::Unavailable, "native record-content evidence required"); return;
    }
    if (snapshot.deadCount || snapshot.hold.active) {
        QueueNativeAck(ResyncAckStatus::Unavailable, "dead bootstrap identity or event hold unsupported"); return;
    }
    if (begin.phase == ResyncPhase::Checkpoint) return; // observation only; no mutation/reload
    if (context.generation <= g_resyncPriorOrderedGeneration || g_host.epoch != 0 || g_host.arrived) {
        QueueNativeAck(ResyncAckStatus::Unavailable, "bootstrap requires a fresh ordered world reset"); return;
    }
    HostRoom staged;
    staged.epoch = snapshot.room.epoch; staged.world = snapshot.room.worldId;
    staged.room = snapshot.room.roomId; staged.btl = snapshot.room.battleProgram;
    staged.manifestComplete = true;
    for (const auto& row : snapshot.enemies) {
        HostEnemy enemy;
        enemy.spawnIndex = row.identity.spawnIndex; enemy.objectId = row.identity.objectId;
        enemy.spawnPos = row.identity.spawnPosition; enemy.battleProgram = row.identity.battleProgram;
        enemy.hp = row.hp; enemy.maxHp = row.maxHp; enemy.hpKnown = true;
        enemy.hpSourceSequence = snapshot.hpSequence;
        staged.enemies.emplace(row.identity.netId, enemy);
    }
    // Review F2: the snapshot room is the embedded form (no spawn-pick trailer); take the last accepted
    // RoomTransition packet's when it is this load, else the client's picks stay native (resync-unknown).
    RoomTransition resyncRoom = snapshot.room;
    const auto trailer = spawnpick::ResyncTrailer(g_lastRoomPacket ? &*g_lastRoomPacket : nullptr, snapshot.room);
    resyncRoom.spawnPickShared = trailer.shared;
    resyncRoom.spawnPickSaltTag = trailer.saltTag;
    if (!WorldContextCurrent(context) || !progresssync::StageFull(snapshot.progress, context.generation) ||
        !WorldContextCurrent(context) || !warp::QueueHostTransition(resyncRoom, context, nullptr, &begin, &*target)) {
        progresssync::Reset();
        warp::SetClientAuthority(false);
        QueueNativeAck(ResyncAckStatus::Unavailable, "native bootstrap context or progress unavailable"); return;
    }
    g_host = std::move(staged); g_hostHpSequence = snapshot.hpSequence;
    g_spawnPickUnknownEpoch = trailer.known ? 0 : snapshot.room.epoch;
    AdvanceClientManifestRevision(); InvalidateClientClaims("bootstrap-admitted");
    g_resyncRecordAuthority = ResyncRecordAuthority {snapshot, context};
    g_resyncWriteFence = ResyncWriteFence::Exact;
    g_inst.spawns.clear(); g_inst.byActor.clear(); ClearActivation(); ClearPendingHits(); g_lastHashMs = 0;
    if (g_survivingPackEnabled) {
        // This point follows parsed bootstrap membership, a newly ordered
        // context, and an accepted fresh-load request. A pure natural-input
        // attempt may start a new cycle; owned gateway attempts stay tombstoned.
        const auto& previous = g_survivingPack.Intent();
        const bool newCycle = !previous || (!(previous->begin.key == begin.key) &&
            (previous->context.generation != context.generation ||
             previous->context.deliverySerial != context.deliverySerial));
        if (snapshot.activationReplay && newCycle && !g_survivingPack.HasOwnAttempt())
            g_survivingPack = {};
        // Observe/arming still requires load != this new snapshot's loadBefore
        // and exact empty pre-state; this reset does not rearm the old native load.
        g_survivingPack.Begin(true, {begin, *target, context, g_nativeResync->loadBefore,
                                  g_resyncDeadline, snapshot}, GetTickCount64());
        if (g_survivingPack.Terminal())
            QueueNativeAck(ResyncAckStatus::Unavailable, "surviving pack preparation cannot rearm");
    }
}

void ContinuePackPreparation(std::uint64_t serial, bool materialChanged) {
    if (!PackPreparationActive()) return;
    g_survivingPack.Continue(serial, materialChanged, GetTickCount64());
    if (g_survivingPack.Terminal())
        QueueNativeAck(ResyncAckStatus::Unavailable, "surviving pack authority changed during preparation");
}

bool PackManifestChanged(const EnemyManifest& manifest) {
    if (!PackPreparationActive()) return false;
    const auto& expected = g_nativeResync->snapshot.enemies;
    if (manifest.replace && manifest.entries.size() != expected.size()) return true;
    std::vector<std::uint16_t> ids;
    for (const auto& entry : manifest.entries) {
        const auto row = std::find_if(expected.begin(), expected.end(), [&](const auto& e) {
            return e.identity.netId == entry.netId;
        });
        if (row == expected.end() || std::find(ids.begin(), ids.end(), entry.netId) != ids.end()) return true;
        ids.push_back(entry.netId);
        const auto& original = row->identity;
        if (entry.objectId != original.objectId || entry.battleProgram != original.battleProgram ||
            entry.spawnIndex != original.spawnIndex || entry.spawnPosition.x != original.spawnPosition.x ||
            entry.spawnPosition.y != original.spawnPosition.y || entry.spawnPosition.z != original.spawnPosition.z)
            return true;
    }
    return false;
}

bool PackHpChanged(const EnemyHp& hp) {
    if (!PackPreparationActive()) return false;
    const auto& expected = g_nativeResync->snapshot.enemies;
    for (const auto& entry : hp.entries) {
        const auto row = std::find_if(expected.begin(), expected.end(), [&](const auto& e) {
            return e.identity.netId == entry.netId;
        });
        if (row == expected.end() || entry.hp != row->hp || entry.maxHp != row->maxHp) return true;
    }
    return false;
}

// Fresh, sampled preparation facts only. Raw/deferred/cache/pending coverage
// remains explicitly unavailable; an empty ready census never permits creation.
struct PackReadyReference {
    uintptr_t actor = 0, object = 0, status = 0, controller = 0, record = 0;
    std::uint32_t definitionIndex = 0;
    std::uint16_t recordIndex = 0;
    std::uint32_t objectId = 0;
    std::uint32_t objectType = 0;
};

// Project sampled raw occupancy onto independently checked ready membership.
// Neither this join nor a zero list/cache closes pre-active creator coverage.
void ProjectPackOccupancy(const spawncontroller::NativeSelectedOccupancy& raw,
    const RecordCatalog& catalog, std::uint16_t room,
    const std::vector<PackReadyReference>& ready, SurvivingPackFacts& facts) {
    if (raw.selectedDefinition >= catalog.entryCount ||
        catalog.entries[raw.selectedDefinition].content.records.size() != 5) return;
    if (raw.controllerStateAvailable) {
        const auto& state = raw.state[0];
        facts.controller = {true, state.flags, static_cast<std::int32_t>(state.currentCount),
            static_cast<std::int32_t>(state.initialCount), state.cooldown, state.stage, state.activation};
    }
    facts.cacheAvailable = raw.cacheAvailable && raw.catalogStable && raw.lifecycleStable && raw.mutationStable &&
        raw.cache[0].room == room && raw.cache[1].room == room;
    // A later failed read cannot erase a duplicate already observed in a full
    // current-room bucket. This is a conservative negative, not positive scope.
    facts.cacheConflict = raw.cache[0].bytesRead && raw.cache[0].room == room && raw.cacheDuplicateIds;
    if (facts.cacheAvailable) {
        facts.cacheEntryCount = static_cast<std::uint16_t>((std::min)(raw.cacheEntryCount, 257u));
        facts.cacheConflict = raw.cacheDuplicateIds || raw.cacheEntryCount > 256;
        for (unsigned i = 0; i < 5; ++i) {
            if (raw.cacheSelectedCounts[i]) facts.cacheSelectedMask |= static_cast<std::uint8_t>(1u << i);
            if (raw.cacheSelectedCounts[i] > 1) facts.cacheConflict = true;
        }
        // Every nonzero cache ID must resolve uniquely in the complete room
        // catalog; unrelated checked entries are legitimate, unknown IDs aren't.
        for (const auto id : raw.cache[0].ids) {
            if (!id) continue;
            unsigned matches = 0;
            for (std::uint32_t d = 0; d < catalog.entryCount; ++d)
                for (const auto& record : catalog.entries[d].content.records)
                    if (static_cast<std::uint16_t>(record[0x1E] | (record[0x1F] << 8)) == id) ++matches;
            if (matches != 1) facts.cacheConflict = true;
        }
    }
    for (unsigned i = 0; i < 5; ++i) {
        if (raw.activeReferenceCounts[i] + raw.deferredReferenceCounts[i] > 1) {
            facts.listed = SurvivingPackListed::Conflict; return;
        }
        if (raw.deferredReferenceCounts[i]) { facts.listed = SurvivingPackListed::Deferred; return; }
    }
    if (raw.conflictCount) { facts.listed = SurvivingPackListed::Conflict; return; }
    if (raw.pendingNodeCount) { facts.listed = SurvivingPackListed::Pending; return; }
    if (raw.unclassifiableNodeCount) { facts.listed = SurvivingPackListed::Unclassifiable; return; }
    std::uint32_t joined = 0;
    std::uint8_t joinedMask = 0;
    for (const auto& node : raw.nodes) {
        if (!node.exactSelectedReference) continue;
        const auto& f = node.fields[0];
        if (!node.stable || node.recordIndex >= 5 || node.deferred || node.pending || node.unclassifiable ||
            node.selectedIdConflict || node.nativeLookupSkipped || (f.readMask & 1023u) != 1023u) {
            facts.listed = SurvivingPackListed::Unclassifiable; return;
        }
        unsigned matches = 0;
        for (const auto& member : ready)
            if (member.actor == node.actor && member.object == f.object && member.status == f.status &&
                member.controller == f.controller && member.record == f.record &&
                member.definitionIndex == raw.selectedDefinition && member.recordIndex == node.recordIndex &&
                member.objectId == f.objectId && member.objectType == f.objectType &&
                !(f.objectName[0] == 'F' && f.objectName[1] == '_')) ++matches;
        if (matches != 1) { facts.listed = SurvivingPackListed::Pending; return; }
        const auto bit = static_cast<std::uint8_t>(1u << node.recordIndex);
        if (joinedMask & bit) { facts.listed = SurvivingPackListed::Conflict; return; }
        joinedMask |= bit; ++joined;
    }
    if (joined != raw.selectedReferenceCount) { facts.listed = SurvivingPackListed::Conflict; return; }
    // Retain negative findings, including checked per-node contradictions, on
    // partial samples. Publish positive masks/absence only with complete scope.
    if (!raw.listedComplete || !raw.catalogStable || !raw.lifecycleStable || !raw.mutationStable) return;
    facts.listedReadyMask = joinedMask;
    facts.listed = joined ? SurvivingPackListed::SelectedReadyReferences :
        raw.noSelectedReferencesAtSamples ? SurvivingPackListed::NoSelectedReferences : SurvivingPackListed::Unavailable;
}

struct PackLogSample {
    SurvivingPackClassification classification {};
    SurvivingPackReason reason {};
    std::uint32_t missing = 0, load = 0, ready = 0, references = 0, pending = 0, unknown = 0;
    std::uint32_t conflicts = 0, excluded = 0;
    std::uint64_t revision = 0, rawIssues = 0;
    std::uint16_t cacheCount = 0;
    std::uint8_t listedMask = 0, cacheMask = 0;
    bool operator==(const PackLogSample&) const = default;
};
std::optional<PackLogSample> g_packLogSample;
void LogPackPreparation(const SurvivingPackFacts& facts, const spawncontroller::NativeSelectedOccupancy& raw) {
    const PackLogSample sample {g_survivingPack.Classification(), g_survivingPack.Reason(),
        g_survivingPack.MissingFacts(), facts.load, static_cast<std::uint32_t>(facts.readyMembers.size()),
        raw.selectedReferenceCount, raw.pendingNodeCount, raw.unclassifiableNodeCount, raw.conflictCount,
        raw.excludedNoncombatCount, facts.mutation.revision, raw.issues, facts.cacheEntryCount,
        facts.listedReadyMask, facts.cacheSelectedMask};
    if (g_packLogSample && *g_packLogSample == sample) return;
    g_packLogSample = sample;
    if (!ResyncLogAllowed()) return;
    const auto& intent = *g_survivingPack.Intent();
    g_log("[resync-pack] sample session=%s host=%llu request=%llu targetSlot=%u cut=%llu load=%u classification=%u reason=%u missing=%u ready=%u listed=%u listedMask=%u cacheAvailable=%u cacheMask=%u cacheCount=%u cacheConflict=%u rawComplete=%u rawIssues=%llu references=%u pending=%u unclassifiable=%u conflicts=%u excludedNoncombat=%u mutationRevision=%llu mutationAvailable=%u mutationCoverage=%u controllerAvailable=%u flags=%u currentCount=%d initialCount=%d cooldown=%.9g stage=%u activation=%u modeBefore=%u modeAfter=%u modeReads=%u ownAttempt=%u creationAuthority=0",
        intent.begin.key.sessionId.c_str(), static_cast<unsigned long long>(intent.begin.key.hostConnectionId),
        static_cast<unsigned long long>(intent.begin.key.requestId), static_cast<unsigned>(intent.target.slot),
        static_cast<unsigned long long>(intent.begin.snapshotCut), sample.load, static_cast<unsigned>(sample.classification),
        static_cast<unsigned>(sample.reason), sample.missing, sample.ready, static_cast<unsigned>(facts.listed),
        sample.listedMask, static_cast<unsigned>(facts.cacheAvailable), sample.cacheMask, sample.cacheCount,
        static_cast<unsigned>(facts.cacheConflict), static_cast<unsigned>(raw.listedComplete),
        static_cast<unsigned long long>(raw.issues), sample.references, sample.pending, sample.unknown, sample.conflicts,
        sample.excluded, static_cast<unsigned long long>(sample.revision), static_cast<unsigned>(facts.mutation.available),
        static_cast<unsigned>(facts.mutation.coverageComplete), static_cast<unsigned>(facts.controller.available),
        facts.controller.flags, facts.controller.currentCount, facts.controller.initialCount, facts.controller.cooldown,
        static_cast<unsigned>(facts.controller.stage), static_cast<unsigned>(facts.controller.activation),
        raw.context716750[0], raw.context716750[1],
        static_cast<unsigned>(raw.context716750Reads[0]) | (static_cast<unsigned>(raw.context716750Reads[1]) << 1),
        static_cast<unsigned>(g_survivingPack.HasOwnAttempt()));
}

struct ActivationOccupancyDecision {
    const char* unavailable = nullptr;
    bool hold = false;
};
ActivationOccupancyDecision CheckActivationOccupancy(const SurvivingPackFacts& facts,
    const spawncontroller::NativeSelectedOccupancy& raw, SurvivingPackReason reason,
    const ResyncSnapshot& expected) {
    // Positive sampled negatives survive a later incomplete read. The raw
    // conflict total also counts ordinary deferred selected records, so it is
    // deliberately not a predicate here.
    if (facts.cacheConflict) return {"duplicate or unresolved appearance-cache ID", false};
    for (std::size_t i = 0; i < raw.activeReferenceCounts.size(); ++i)
        if (static_cast<unsigned>(raw.activeReferenceCounts[i]) + raw.deferredReferenceCounts[i] > 1)
            return {"duplicate selected active/deferred reference", false};
    bool unknownPending = false;
    for (const auto& node : raw.nodes) {
        if (node.selectedIdConflict) return {"selected record has conflicting native provenance", false};
        if (node.unclassifiable && !node.pending)
            return {"nonpending native occupancy is unclassifiable; partial outcome possible", false};
        if (node.pending && !node.exactSelectedReference) unknownPending = true;
    }
    if (reason == SurvivingPackReason::ContentMismatch)
        return {"complete native content or ready membership differs", false};
    if (facts.readyMembersComplete && facts.readyMembers.size() > 5)
        return {"extra ready native actors", false};
    // Pending/deferred listing may make the reducer return before comparing
    // ready members. Check that complete subset independently, by local index.
    if (facts.readyMembersComplete && facts.catalogStatus == NativeRecordContentStatus::Complete && raw.catalogStable) {
        std::array<bool, 5> seen {};
        for (const auto& member : facts.readyMembers) {
            if (!member.exactMembership) { unknownPending = true; continue; }
            const auto index = member.record.recordIndex;
            if (member.record.definitionIndex != raw.selectedDefinition || index >= seen.size() || seen[index] ||
                member.objectId != 302 || member.objectType != 4 || member.hp <= 0 || member.hp > member.maxHp)
                return {"incompatible or duplicate ready selected member", false};
            const auto source = std::find_if(expected.enemies.begin(), expected.enemies.end(),
                [index](const auto& row) { return row.record.recordIndex == index; });
            if (source == expected.enemies.end() || source->maxHp != member.maxHp)
                return {"ready selected member maximum HP differs", false};
            seen[index] = true;
        }
    }
    // Exact selected pending/deferred records and a transient cache/ready-mask
    // mismatch can advance naturally. Missing scope or unresolved pending
    // provenance pauses input under the same absolute deadline, never retries.
    return {nullptr, unknownPending || facts.catalogStatus != NativeRecordContentStatus::Complete ||
        !facts.readyMembersComplete || !raw.listedComplete || !raw.cacheAvailable ||
        !raw.controllerStateAvailable || !raw.catalogStable || !raw.lifecycleStable || !raw.mutationStable};
}

void PollPackPreparation(const NativeCensus& census) {
    if (!PackPreparationActive()) return;
    SurvivingPackFacts facts;
    if (!CaptureWorldContext(facts.context)) { g_survivingPack.Cancel(); }
    else {
        facts.load = census.load;
        facts.arrived = g_host.arrived && CensusMatchesInstance(census) && SafeNativeGameplay() &&
            SameLocation(census.location, g_nativeResync->snapshot.room) &&
            warp::MatchesArrivedHostTransition(g_nativeResync->snapshot.room);
        facts.mutation = spawncontroller::AcquireKnownMutationTicket();
        RecordCatalog before, after;
        std::vector<NativeRecordContentDefinition> definitions;
        std::vector<SurvivingPackMember> members;
        std::vector<PackReadyReference> ready;
        spawncontroller::NativeSelectedOccupancy raw;
        if (facts.arrived && FreshRecordCatalog(census, before)) {
            for (std::uint32_t i = 0; i < before.entryCount; ++i) definitions.push_back(before.entries[i].content);
            facts.readyMembersComplete = true;
            for (const auto& native : census.enemies) {
                spawncontroller::NativeRecordMembership member;
                if (!RecordMember(before, census, native.actor, member)) {
                    facts.readyMembersComplete = false; break;
                }
                members.push_back({{static_cast<std::uint16_t>(member.tableIndex), member.recordIndex},
                    native.objectId, native.objectType, native.hp, native.maxHp, true});
                ready.push_back({native.actor, native.objentry, native.status, member.controller[0], member.record[0],
                    member.tableIndex, member.recordIndex, native.objectId, native.objectType});
            }
            const auto candidates = RecordCandidates(definitions);
            const auto& expected = g_survivingPack.Intent()->snapshot;
            const auto selected = expected.enemies.front().record.definitionIndex;
            const auto resolved = ResolveNativeRecordContent(expected.recordDefinitions[selected], candidates,
                NativeRecordContentStatus::Complete);
            if (resolved.status == NativeRecordContentStatus::Complete && resolved.definitionIndex &&
                *resolved.definitionIndex < before.entryCount) {
                spawncontroller::CaptureNativeSelectedOccupancy(g_exeBase, before,
                    static_cast<std::uint32_t>(*resolved.definitionIndex), raw);
                ProjectPackOccupancy(raw, before, census.location.roomId, ready, facts);
            }
            if (FreshRecordCatalog(census, after) && SameRecordCatalogSample(before, after))
                facts.catalogStatus = NativeRecordContentStatus::Complete;
        }
        const auto candidates = RecordCandidates(definitions);
        facts.catalog = candidates; facts.readyMembers = members;
        // Bookend the supplied facts with the synchronous negative fence.
        if (!spawncontroller::KnownMutationTicketCurrent(facts.mutation))
            facts.mutation.available = false;
        g_survivingPack.Observe(facts, GetTickCount64());
        LogPackPreparation(facts, raw);
        if (g_activationRecovery) {
            const auto decision = CheckActivationOccupancy(facts, raw, g_survivingPack.Reason(),
                g_nativeResync->snapshot);
            if (decision.unavailable) {
                g_activationRecovery->phase = ActivationRecoveryPhase::Failed;
                LogActivationRecovery("unavailable", decision.unavailable);
            }
            if (g_activationRecovery->occupancyHold != decision.hold)
                LogActivationRecovery(decision.hold ? "occupancy-hold" : "occupancy-resume",
                    decision.hold ? "incomplete occupancy or unresolved pending provenance" : "complete sampled occupancy restored");
            g_activationRecovery->occupancyHold = decision.hold;
        }
        if (!g_activationRecovery && g_nativeResync->snapshot.activationReplay &&
            g_survivingPack.Classification() == SurvivingPackClassification::ListedEmptyUnqualified &&
            facts.readyMembersComplete && facts.readyMembers.empty() && facts.arrived &&
            facts.catalogStatus == NativeRecordContentStatus::Complete && facts.mutation.available &&
            facts.mutation.coverageComplete && !facts.mutation.inFlight && !facts.mutation.poisoned &&
            raw.listedComplete && raw.noSelectedReferencesAtSamples && raw.cacheAvailable &&
            raw.cacheEntryCount == 0 && raw.pendingNodeCount == 0 && raw.unclassifiableNodeCount == 0 &&
            raw.conflictCount == 0 && raw.controllerStateAvailable &&
            raw.selectedDefinition < raw.catalogs[0].entryCount) {
            ResyncActivationState local;
            const auto& replay = *g_nativeResync->snapshot.activationReplay;
            if (ActivationState(raw.state[0], local) && SameActivationState(local, replay.before, true)) {
                ActivationRecovery recovery;
                const auto& entry = raw.catalogs[0].entries[raw.selectedDefinition];
                recovery.controller = entry.controller; recovery.header = entry.header;
                recovery.records = entry.spawnArray;
                recovery.tableEntry = g_exeBase + 0x2A10010 + raw.selectedDefinition * 16;
                recovery.tableBytes = entry.tableBefore; recovery.headerBytes = entry.content.header;
                std::copy_n(entry.content.records.begin(), 5, recovery.recordBytes.begin());
                recovery.mutation = facts.mutation; recovery.load = census.load; recovery.transition = census.transition;
                (void)spawncontroller::CopyLastClientOriginalReturn(recovery.controller, recovery.lastSequence);
                recovery.historicalBaseline = recovery.lastSequence;
                if (ActivationDefinitionCurrent(recovery) && spawncontroller::KnownMutationTicketCurrent(recovery.mutation)) {
                    g_activationRecovery = recovery;
                    LogActivationRecovery("armed", "fresh empty native state matched; header+E 0/1 exception");
                }
            }
        }
        if (g_activationRecovery && g_activationRecovery->phase == ActivationRecoveryPhase::Historical &&
            g_survivingPack.Classification() == SurvivingPackClassification::FullSetAlreadyPresent) {
            g_activationRecovery->phase = ActivationRecoveryPhase::LiveHold;
            // The just-finished fifth emission used historical input. Start
            // counting live-input returns after this exact completed sequence.
            (void)spawncontroller::CopyLastClientOriginalReturn(g_activationRecovery->controller,
                g_activationRecovery->lastSequence);
            g_activationRecovery->liveBaseline = g_activationRecovery->lastSequence;
            LogActivationRecovery("live-hold", "five exact ready records; return to current host input");
        }
    }
    if (g_activationRecovery && g_activationRecovery->phase == ActivationRecoveryPhase::Failed &&
        g_nativeResync && !g_nativeResync->finished) {
        LogActivationRecovery("failed", "scope, native definition or bounded tick deadline; partial outcome possible");
        QueueNativeAck(ResyncAckStatus::Unavailable, "historical activation retired; partial population possible");
    }
    if (g_survivingPack.Terminal())
        QueueNativeAck(ResyncAckStatus::Unavailable, "surviving pack preparation scope retired");
}

bool ObserveNativeResync(std::uint32_t frame) {
    auto& state = *g_nativeResync;
    if (!WorldContextCurrent(state.context) || !g_host.arrived || !SafeNativeGameplay() ||
        !warp::MatchesArrivedHostTransition(state.snapshot.room)) return false;
    const auto load = warp::LoadSerial();
    if (state.begin.phase == ResyncPhase::Bootstrap && load == state.loadBefore) return false;
    if (state.firstObserved && load != state.observedLoad) return false;
    const auto census = CaptureNativeCensus();
    std::vector<ResyncActorBinding> bindings;
    std::vector<NativeRecordContentDefinition> definitions;
    if (!CollectResyncBindings(state.snapshot, state.context, census, bindings, definitions)) return false;
    std::uint32_t hash = 0;
    if (!progresssync::MatchesFull(state.snapshot.progress, hash)) {
        if (state.begin.phase == ResyncPhase::Checkpoint)
            QueueNativeAck(ResyncAckStatus::Unavailable, "checkpoint progress differs; safe boundary required");
        return false;
    }
    ResyncSnapshot observed = state.snapshot; observed.enemies.clear();
    observed.activationReplay.reset(); // historical transport data is not native state
    observed.recordDefinitions = std::move(definitions);
    for (const auto& native : census.enemies) {
        NativeEnemy repeated; bool combat = false;
        if (!ReadNativeEnemy(native.actor, repeated, combat) || !combat ||
            !SameNativeIdentity(native, repeated) || native.objectType != repeated.objectType ||
            native.hp != repeated.hp || native.maxHp != repeated.maxHp) return false;
        const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto& b) { return b.actor == native.actor; });
        if (binding == bindings.end()) return false;
        const auto source = std::find_if(state.snapshot.enemies.begin(), state.snapshot.enemies.end(), [&](const auto& e) {
            return e.identity.netId == binding->netId;
        });
        if (source == state.snapshot.enemies.end()) return false;
        ResyncEnemyState row;
        row.identity = source->identity; row.identity.objectId = native.objectId;
        row.record = binding->record;
        row.objectType = native.objectType; row.hp = native.hp; row.maxHp = native.maxHp;
        if (native.hp <= 0) return false;
        if (std::any_of(observed.enemies.begin(), observed.enemies.end(), [&](const auto& old) {
            return old.identity.netId == row.identity.netId;
        })) return false;
        observed.enemies.push_back(row);
    }
    std::sort(observed.enemies.begin(), observed.enemies.end(), [](const auto& a, const auto& b) {
        return a.identity.netId < b.identity.netId;
    });
    if (resyncNativeFingerprint(observed) != state.snapshot.nativeFingerprint ||
        !WorldContextCurrent(state.context) || !CensusMatchesInstance(census)) return false;
    if (g_activationRecovery) {
        auto& recovery = *g_activationRecovery;
        if (recovery.phase != ActivationRecoveryPhase::LiveHold &&
            recovery.phase != ActivationRecoveryPhase::Verified) return false;
        if (!recovery.reconciled) {
            recovery.reconciled = true; // exact full-set HP readback, not requested stores
            LogActivationRecovery("reconciled", "complete exact record and HP readback");
        }
        if (recovery.liveTicks < kActivationLiveHoldTicks || !state.snapshot.activationReplay ||
            !ActivationDefinitionCurrent(recovery) ||
            !spawncontroller::KnownMutationTicketCurrent(recovery.mutation)) return false;
        ResyncActivationState actual;
        if (!ActivationState(spawncontroller::CaptureDiagnosticState(g_exeBase, recovery.controller), actual) ||
            !SameActivationState(actual, state.snapshot.activationReplay->current, false)) return false;
        if (recovery.phase != ActivationRecoveryPhase::Verified) {
            recovery.phase = ActivationRecoveryPhase::Verified;
            LogActivationRecovery("verified", "120 native updates on live input; exact controller/cache and HP");
        }
    }
    // Receipts describe these already-qualified samples. No additional native
    // reads, and no requested HP value is presented as a post-store readback.
    if (ResyncLogAllowed()) {
        std::size_t records = 0;
        for (const auto& definition : observed.recordDefinitions) records += definition.records.size();
        g_log("[resync-content] observe session=%s host=%llu request=%llu targetSlot=%u connection=%llu delivery=%llu phase=%u cut=%llu protocol=%u coverage=%u definitions=%llu records=%llu bindings=%llu load=%u frame=%u fence=%u fingerprintMatched=1",
              state.begin.key.sessionId.c_str(), static_cast<unsigned long long>(state.begin.key.hostConnectionId),
              static_cast<unsigned long long>(state.begin.key.requestId), static_cast<unsigned>(state.target.slot),
              static_cast<unsigned long long>(state.target.connectionId), static_cast<unsigned long long>(state.target.deliverySerial),
              static_cast<unsigned>(state.begin.phase), static_cast<unsigned long long>(state.begin.snapshotCut),
              static_cast<unsigned>(PROTOCOL_VERSION), observed.coverageMask,
              static_cast<unsigned long long>(observed.recordDefinitions.size()), static_cast<unsigned long long>(records),
              static_cast<unsigned long long>(bindings.size()), load, frame, static_cast<unsigned>(g_resyncWriteFence));
    }
    for (const auto& row : observed.enemies) {
        if (!ResyncLogAllowed()) break;
        g_log("[resync-content] observed-row session=%s host=%llu request=%llu targetSlot=%u phase=%u cut=%llu load=%u frame=%u netId=%u objectId=%u localDefinition=%u localRecord=%u hpObserved=%d maxHpObserved=%d",
              state.begin.key.sessionId.c_str(), static_cast<unsigned long long>(state.begin.key.hostConnectionId),
              static_cast<unsigned long long>(state.begin.key.requestId), static_cast<unsigned>(state.target.slot),
              static_cast<unsigned>(state.begin.phase), static_cast<unsigned long long>(state.begin.snapshotCut), load, frame,
              static_cast<unsigned>(row.identity.netId), row.identity.objectId,
              static_cast<unsigned>(row.record.definitionIndex), static_cast<unsigned>(row.record.recordIndex), row.hp, row.maxHp);
    }
    if (!state.firstObserved) { state.firstObserved = true; state.firstFrame = frame; state.observedLoad = load; }
    else if (state.firstFrame != frame) QueueNativeAck(ResyncAckStatus::Converged, "", frame);
    return true;
}

void TickNativeResync(std::uint32_t frame, bool observed) {
    if (!g_resyncPlan) return;
    try {
        if (!ResyncMembership(*g_resyncPlan)) { g_nativeResync.reset(); g_resyncPlan.reset(); g_resyncOutput.clear(); return; }
        if (!g_resyncOutput.empty()) {
            if (SendCapturedWorld(g_resyncOutput, g_resyncOutputContext)) g_resyncOutput.clear();
            else if (CurrentRole() == Role::Host &&
                     g_resyncOutput.front() == static_cast<std::uint8_t>(PacketType::NativeResyncSnapshot)) {
                g_resyncOutput.clear();
                QueueResyncFailure(ResyncResultReason::Overflow, "native snapshot enqueue failed");
                g_resyncFailureFloor = g_worldSendFailures;
            }
            return;
        }
        if (GetTickCount64() >= g_resyncDeadline) {
            if (CurrentRole() == Role::Host && !g_resyncHostCaptured)
                QueueResyncFailure(ResyncResultReason::Deadline, "native capture deadline");
            else if (g_nativeResync && !g_nativeResync->finished)
                QueueNativeAck(ResyncAckStatus::Unavailable, "native convergence deadline");
            return;
        }
        if (CurrentRole() == Role::Host) {
            if (g_worldSendFailures != g_resyncFailureFloor) {
                QueueResyncFailure(ResyncResultReason::Overflow, "required native publication failed");
                g_resyncFailureFloor = g_worldSendFailures; return;
            }
            if (!observed || g_resyncHostCaptured || !WorldSessionGeneration()) return;
            ResyncSnapshot snapshot; ProducerWorldContext context;
            if (!CaptureHostResync(frame, snapshot, context)) {
                QueueResyncFailure(ResyncResultReason::CaptureUnavailable, "complete fresh native snapshot unavailable"); return;
            }
            ResyncBegin begin;
            begin.key = g_resyncPlan->request.key; begin.phase = g_resyncPlan->phase; begin.room = snapshot.room;
            begin.targets = g_resyncPlan->targets; begin.targetCount = g_resyncPlan->targetCount;
            begin.snapshotCut = context.hostSourceSerial;
            const auto bytes = encodeResyncSnapshot(snapshot);
            begin.totalBytes = static_cast<std::uint32_t>(bytes.size());
            begin.partCount = static_cast<std::uint16_t>((bytes.size() + RESYNC_MAX_PART_BYTES - 1) / RESYNC_MAX_PART_BYTES);
            begin.sha256 = desyncSha256(bytes);
            g_resyncOutput = encodeNativeResyncSnapshot(begin, snapshot); g_resyncOutputContext = context;
            g_resyncHostCaptured = true;
            if (ResyncLogAllowed()) {
                std::size_t records = 0;
                for (const auto& definition : snapshot.recordDefinitions) records += definition.records.size();
                const auto snapshotHex = desyncDigestHex(begin.sha256);
                g_log("[resync-content] capture session=%s host=%llu request=%llu phase=%u cut=%llu protocol=%u coverage=%u definitions=%llu records=%llu references=%llu living=%u dead=%u load=%u frame=%u snapshotSHA=%s",
                      begin.key.sessionId.c_str(), static_cast<unsigned long long>(begin.key.hostConnectionId),
                      static_cast<unsigned long long>(begin.key.requestId), static_cast<unsigned>(begin.phase),
                      static_cast<unsigned long long>(begin.snapshotCut), static_cast<unsigned>(PROTOCOL_VERSION), snapshot.coverageMask,
                      static_cast<unsigned long long>(snapshot.recordDefinitions.size()), static_cast<unsigned long long>(records),
                      static_cast<unsigned long long>(snapshot.enemies.size()), snapshot.livingCount, snapshot.deadCount,
                      snapshot.loadSerial, frame, snapshotHex.c_str());
            }
        } else if (observed && g_nativeResync && !g_nativeResync->finished) {
            if (!ObserveNativeResync(frame) && !g_nativeResync->finished) g_nativeResync->firstObserved = false;
        }
    } catch (const std::exception&) {
        if (CurrentRole() == Role::Host) QueueResyncFailure(ResyncResultReason::InvalidSnapshot, "native snapshot encoding unavailable");
        else QueueNativeAck(ResyncAckStatus::Unavailable, "native snapshot observation unavailable");
    }
}

bool EventControlScopeCurrent(const eventhold::Scope& scope) {
    const auto slot = g_bridge.LocalSlot();
    return eventhold::ValidScope(scope) && scope.generation == WorldSessionGeneration() &&
        slot == scope.slot && scope.hostConnection == g_bridge.ConnectionId(0) &&
        scope.selfConnection == g_bridge.ConnectionId(slot) &&
        scope.hostDelivery == g_bridge.PeerDeliverySerial(0) &&
        scope.targetDelivery == g_bridge.DeliverySerial();
}
void ObserveEventControl(const WorldScope& source, eventhold::Kind kind, std::uint32_t epoch,
                         const RoomTransition* room, std::uint16_t eventProgram) {
    if (!eventholdnative::Enabled() || CurrentRole() != Role::Client || source.sessionId.size() != 32) return;
    eventhold::Scope scope {};
    std::memcpy(scope.session, source.sessionId.data(), sizeof(scope.session));
    scope.generation = WorldSessionGeneration(); scope.slot = g_bridge.LocalSlot();
    scope.hostConnection = source.sourceConnectionId; scope.selfConnection = source.targetConnectionId;
    scope.hostDelivery = source.sourceDeliverySerial; scope.targetDelivery = source.targetDeliverySerial;
    if (!EventControlScopeCurrent(scope)) return;
    if (eventhold::ValidScope(g_eventControlScope) && g_eventControlScope != scope) {
        eventholdnative::RetireOwner(); return;
    }
    g_eventControlScope = scope;
    if (room) g_eventControlRoom = *room;
    eventholdnative::Observe(scope, kind, source.hostSourceSerial, epoch, room, eventProgram);
}
void AckEventControl() {
    if (!eventholdnative::Enabled() || g_role != Role::Client || !EventControlScopeCurrent(g_eventControlScope)) return;
    const auto census = CaptureNativeCensus();
    const bool eligible = !g_resyncPlan && !g_nativeResync && !PackPreparationActive() &&
        g_resyncWriteFence == ResyncWriteFence::None && g_host.arrived && g_host.ackSent &&
        g_host.epoch && warp::HostTransitionArrived(g_host.epoch) && g_inst.live &&
        g_host.manifestComplete && g_host.enemies.empty() && CensusMatchesInstance(census) &&
        census.enemies.empty() && g_eventControlRoom.epoch == g_host.epoch &&
        SameLocation(census.location, g_eventControlRoom) && SafeNativeGameplay();
    const bool converged = eligible && progresssync::ClientConverged(g_eventControlScope.generation);
    // Repeat scope/census/lifecycle checks after masked SAVE readback. These are
    // normal owner facts; the raw input path sees only the independent atomic ACK.
    eventholdnative::OwnerAck(g_eventControlScope, g_host.epoch,
        eligible && EventControlScopeCurrent(g_eventControlScope) && CensusMatchesInstance(census),
        converged && EventControlScopeCurrent(g_eventControlScope) && CensusMatchesInstance(census));
}

// VUH-1504: a ReviveRequest may come from any teammate (host or client), so it
// skips the host-only client branch below and gets its own source admission:
// the source is a current roster peer other than us, at its published delivery
// serial; a host source also passes the client cut, a client source carries no
// host serial and (on the host) its requester delivery floor.
void AdmitReviveRequest(const WorldScope& scope, const std::uint8_t* payload, std::size_t size) {
    ++g_reviveStats.seen;
    if (ReviveLogAllowed())
        g_log("[downed] revive-hop dll receive source=%llu delivery=%llu hostSource=%llu bytes=%zu",
              static_cast<unsigned long long>(scope.sourceConnectionId), static_cast<unsigned long long>(scope.sourceDeliverySerial),
              static_cast<unsigned long long>(scope.hostSourceSerial), size);
    const auto local = g_bridge.LocalSlot();
    std::uint8_t source = 0xFF;
    for (std::uint8_t slot = 0; slot < 3; ++slot)
        if (slot != local && scope.sourceConnectionId && g_bridge.ConnectionId(slot) == scope.sourceConnectionId) source = slot;
    bool ok = local < 3 && source < 3 && scope.sourceDeliverySerial &&
        scope.sourceDeliverySerial == g_bridge.PeerDeliverySerial(source);
    if (ok && source == 0) ok = CurrentRole() == Role::Client && scope.hostSourceSerial > g_resyncAppliedCut;
    else if (ok) ok = !scope.hostSourceSerial &&
        (CurrentRole() != Role::Host || RequesterCurrent(source, scope.sourceConnectionId, scope.sourceDeliverySerial));
    ReviveRequest request;
    if (ok) {
        ByteReader reader(payload, size);
        read(reader, request);
        ok = reader.atEnd() && request.requesterSlot == source && request.requesterConnectionId == scope.sourceConnectionId;
    }
    DownedScope current;
    if (ok) ok = g_reviveApply && CaptureDownedScope(current);
    if (!ok) {
        ++g_reviveStats.admissionRefused;
        if (ReviveLogAllowed()) g_log("[downed] revive-request admission refused source=%u local=%u apply=%u",
                         static_cast<unsigned>(source), static_cast<unsigned>(local), g_reviveApply ? 1u : 0u);
        return;
    }
    // Consistency latch, not authentication: NetworkClient already drops any
    // envelope whose session differs from the runtime's before the WorldInbox.
    // The DLL holds no runtime session string (see the [client-claims] note);
    // the first admitted ReviveRequest of a session generation latches it, and
    // a later one under another session ID in that generation is refused.
    if (g_reviveSessionGeneration != current.context.generation) {
        g_reviveSessionGeneration = current.context.generation;
        g_reviveSession = scope.sessionId;
    }
    if (!g_reviveGate.Consume(request, scope, g_reviveSession, current.context, current.connections,
                              current.localSlot, g_localDowned, GetTickCount64())) {
        ++g_reviveStats.gateRefused;
        if (ReviveLogAllowed()) g_log("[downed] revive-request gate refused requester=%u seq=%llu episode=%llX localEpisode=%llX downed=%u",
                         static_cast<unsigned>(request.requesterSlot), static_cast<unsigned long long>(request.seq),
                         static_cast<unsigned long long>(request.targetEpisode),
                         static_cast<unsigned long long>(g_localDowned.episode), g_localDowned.downed ? 1u : 0u);
        return;
    }
    ++g_reviveStats.consumed; // episode reserved; exactly one native attempt, never retried
    // Runs inside the ReceiveWorldPackets drain: the native revive re-enters
    // HookedApplyStatDelta/hit trace only; nothing there touches this drain's
    // local packet buffer or enemysync state (round-5 review N5, checked).
    const int result = g_reviveApply(request.targetEpisode, request.requesterSlot, request.seq);
    if (result == 1) ++g_reviveStats.applied;
    if (g_log) g_log("[downed] revive-request consumed requester=%u seq=%llu episode=%llX native=%d",
                     static_cast<unsigned>(request.requesterSlot), static_cast<unsigned long long>(request.seq),
                     static_cast<unsigned long long>(request.targetEpisode), result);
}

std::array<std::uint64_t, 3> PartyRoster() {
    return {g_bridge.ConnectionId(0), g_bridge.ConnectionId(1), g_bridge.ConnectionId(2)};
}

// VUH-1519 (KH2COOP_PARTY_NATIVE=1 only). PartyLayout: host-authored Native scope from the
// host connection; a client applies it, and the host adopts only the relay's echo of its
// own layout. PartyReapply: relay-authored only.
void ReceivePartyPacket(const WorldScope& scope, PacketType type, const std::uint8_t* payload, std::size_t size) {
    const Role role = CurrentRole();
    const auto local = g_bridge.LocalSlot();
    ByteReader r(payload, size);
    if (type == PacketType::PartyLayout) {
        if (scope.kind != WorldSourceKind::Native || scope.sourceConnectionId != g_bridge.ConnectionId(0) ||
            !scope.hostSourceSerial) return;
        if (role == Role::Client) {
            if (scope.sourceDeliverySerial != g_bridge.PeerDeliverySerial(0) ||
                scope.hostSourceSerial <= g_resyncAppliedCut) return;
        } else if (role != Role::Host || local != 0) return;
        PartyLayout layout; read(r, layout);
        if (r.atEnd()) partynative::NoteLayout(layout, local, WorldSessionGeneration(), PartyRoster(), role == Role::Host);
    } else if (type == PacketType::PartyIntent) { // VUH-1786: same admission as a host PartyLayout
        if (scope.kind != WorldSourceKind::Native || scope.sourceConnectionId != g_bridge.ConnectionId(0) ||
            !scope.hostSourceSerial) return;
        if (role == Role::Client) {
            if (scope.sourceDeliverySerial != g_bridge.PeerDeliverySerial(0) ||
                scope.hostSourceSerial <= g_resyncAppliedCut) return;
        } else if (role != Role::Host || local != 0) return;
        PartyIntent intent; read(r, intent);
        if (r.atEnd()) partynative::NoteIntent(intent, local, WorldSessionGeneration(), PartyRoster(), role == Role::Host);
    } else if (type == PacketType::PartyReapply) {
        if (scope.kind != WorldSourceKind::Relay || scope.sourceConnectionId != g_bridge.ConnectionId(0)) return;
        PartyReapply reapply; read(r, reapply);
        if (r.atEnd()) partynative::NoteReapply(reapply, WorldSessionGeneration());
    }
}

// VUH-1519 host producer (KH2COOP_PARTY_NATIVE=1 only): the default layout for the one pinned
// room, once per generation/room tuple/roster (re-sent if not echoed), through the captured
// host world context.
void TickHostPartyLayout() {
    if (!partynative::Requested() || CurrentRole() != Role::Host) return;
    { // VUH-1786: room-independent intents for the pinned rooms, one per frame while due
        PartyIntent intent;
        const auto generation = WorldSessionGeneration();
        ProducerWorldContext context;
        if (partynative::HostIntentToPublish(generation, PartyRoster(), intent) && CaptureWorldContext(context) &&
            SendCapturedWorld(encode(intent), context)) partynative::NoteHostIntentSent(intent, generation);
    }
    RoomTransition location;
    if (!ActivationContext(Role::Host, location)) return;
    const auto generation = WorldSessionGeneration();
    PartyLayout layout;
    if (!partynative::HostLayoutToPublish(generation, location, PartyRoster(), layout)) return;
    ProducerWorldContext context;
    if (!CaptureWorldContext(context)) return;
    if (SendCapturedWorld(encode(layout), context)) partynative::NoteHostSent(layout, generation);
}

bool ReceiveWorldPackets() {
    bool hostSessionReset = false;
    std::vector<std::uint8_t> packet;
    const auto receive = [&]() {
        if (g_writerRecoveryPacket) {
            packet = std::move(*g_writerRecoveryPacket); g_writerRecoveryPacket.reset(); return true;
        }
        return g_bridge.ReceiveFromRuntime(packet);
    };
    while (receive()) {
        CheckActivationGeneration();
        try {
            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
            std::optional<WorldScope> scope;
            if (type == PacketType::WorldEnvelope) {
                ByteReader envelopeReader(payload, size); WorldEnvelope envelope; read(envelopeReader, envelope);
                if (!envelopeReader.atEnd() || packet.size() != size + 3 || !WorldSessionGeneration()) continue;
                scope = envelope.scope;
                const bool partyRelay = partynative::Requested() && scope->kind == WorldSourceKind::Relay &&
                    !envelope.packet.empty() && envelope.packet.front() == static_cast<std::uint8_t>(PacketType::PartyReapply);
                if ((scope->kind != WorldSourceKind::Native && !partyRelay) ||
                    scope->targetConnectionId != g_bridge.ConnectionId(g_bridge.LocalSlot()) ||
                    scope->targetDeliverySerial != g_bridge.DeliverySerial() ||
                    (g_resyncPlan && scope->sessionId != g_resyncPlan->request.key.sessionId)) {
                    // VUH-1504 hop log (diagnostic only, bounded): envelope-level drop of a ReviveRequest.
                    if (!envelope.packet.empty() && envelope.packet.front() == static_cast<std::uint8_t>(PacketType::ReviveRequest) &&
                        ReviveLogAllowed())
                        g_log("[downed] revive-hop dll envelope drop kind=%u target=%llu/%llu delivery=%llu/%llu",
                              static_cast<unsigned>(scope->kind), static_cast<unsigned long long>(scope->targetConnectionId),
                              static_cast<unsigned long long>(g_bridge.ConnectionId(g_bridge.LocalSlot())),
                              static_cast<unsigned long long>(scope->targetDeliverySerial),
                              static_cast<unsigned long long>(g_bridge.DeliverySerial()));
                    continue;
                }
                packet = std::move(envelope.packet);
                type = decodePacketHeader(packet.data(), packet.size(), payload, size);
                if (packet.size() != size + 3 || !isScopedWorldPacket(type)) continue;
                if (type == PacketType::ReviveRequest) { AdmitReviveRequest(*scope, payload, size); continue; }
                if (partynative::Requested() && (type == PacketType::PartyLayout || type == PacketType::PartyReapply ||
                                                  type == PacketType::PartyIntent)) {
                    ReceivePartyPacket(*scope, type, payload, size); // VUH-1519: before the role branches (host echo)
                    continue;
                }
                if (CurrentRole() == Role::Client) {
                    if (scope->sourceConnectionId != g_bridge.ConnectionId(0) || !scope->hostSourceSerial ||
                        scope->sourceDeliverySerial != g_bridge.PeerDeliverySerial(0) ||
                        scope->hostSourceSerial <= g_resyncAppliedCut) continue;
                } else if (CurrentRole() == Role::Host) {
                    std::uint8_t sourceSlot = 0;
                    for (std::uint8_t slot = 1; slot < 3; ++slot)
                        if (g_bridge.ConnectionId(slot) == scope->sourceConnectionId) sourceSlot = slot;
                    if (scope->hostSourceSerial ||
                        !RequesterCurrent(sourceSlot, scope->sourceConnectionId, scope->sourceDeliverySerial)) continue;
                } else continue;
            }
            if (type == PacketType::ResyncPlan) {
                ByteReader r(payload, size); ResyncPlan plan; read(r, plan);
                if (r.atEnd() && packet.size() == size + 3) ReceiveResyncPlan(plan);
                continue;
            }
            if (type == PacketType::ResyncResult) {
                ByteReader r(payload, size); ResyncResult result; read(r, result);
                if (r.atEnd() && packet.size() == size + 3 && g_resyncPlan && result.key == g_resyncPlan->request.key) {
                    if (CurrentRole() == Role::Client && result.reason != ResyncResultReason::Converged)
                        g_resyncWriteFence = ResyncWriteFence::Failed;
                    g_resyncPlan.reset(); g_nativeResync.reset(); g_resyncOutput.clear();
                }
                continue;
            }
            if (type == PacketType::SessionState && !scope && packet.size() == size + 3 &&
                (size == 0 || size == 4 || size == 12)) {
                // DLL-local boundary, ordered in the ring before new-session
                // world packets. Reset here, not before/after the drain: a
                // producer may enqueue this marker halfway through our frame.
                g_activationOrderedGeneration = 0;
                RetireWorldSession();
                if (size == 12) {
                    ByteReader resetReader(payload, size);
                    const auto generation = resetReader.readU32();
                    const auto delivery = resetReader.readU64();
                    if (RuntimeWriterLive() && generation != 0 && generation == g_bridge.SessionGeneration() &&
                        delivery != 0 && delivery == g_bridge.DeliverySerial()) {
                        g_activationOrderedGeneration = generation;
                        g_lastOrderedGeneration = generation;
                        g_orderedDeliverySerial = delivery;
                    }
                }
                g_role = CurrentRole();
                const bool armed = WorldSessionGeneration() != 0;
                warp::SetClientAuthority(armed && g_role == Role::Client);
                hostSessionReset = hostSessionReset || (armed && g_role == Role::Host);
                if (g_log) g_log("[enemysync] session reset: host epoch and pending target cleared");
                continue;
            }
            // Old ring contents cannot reconstruct authority before the reset
            // marker for the currently published header generation arrives.
            if (!WorldSessionGeneration()) continue;
            if (type == PacketType::NativeResyncSnapshot) {
                ReceiveResyncSnapshot(packet); continue;
            }
            if (!scope) continue; // native world facts must preserve authenticated transport scope
            if (type == PacketType::PopulationCut) {
                ByteReader r(payload,size);PopulationCut cut;read(r,cut);
                ReceivePopulationCut(cut,*scope);continue;
            }
            if (type == PacketType::ActivationRequest) {
                ByteReader r(payload, size);
                ActivationRequest request;
                read(r, request);
                if (g_bridge.ConnectionId(request.requesterSlot) == scope->sourceConnectionId)
                    ReceiveActivationRequest(request, scope->sourceDeliverySerial);
                continue;
            }
            if (type == PacketType::HostActivationPoint) {
                ByteReader r(payload, size);
                HostActivationPoint point;
                read(r, point);
                RoomTransition location;
                if (ActivationContext(Role::Client, location) &&
                    sameActivationLocation(point.request.location, location) &&
                    g_activationLease.Accept(point, GetTickCount64())) {
                    const auto now = GetTickCount64();
                    if (g_log && (g_activationLogMs == 0 || now - g_activationLogMs >= 1000)) {
                        g_log("[spawn-authority] lease epoch=%u source=%llu point=(%.3f,%.3f,%.3f,%.3f)",
                              location.epoch, static_cast<unsigned long long>(point.sourceSeq),
                              point.position[0], point.position[1], point.position[2], point.position[3]);
                        g_activationLogMs = now;
                    }
                }
                continue;
            }
            if (type == PacketType::HitClaim) {
                ByteReader r(payload, size);
                HitClaim claim;
                read(r, claim);
                if (claim.requesterConnectionId == scope->sourceConnectionId &&
                    g_bridge.ConnectionId(static_cast<std::uint8_t>(claim.attackerSlot)) == scope->sourceConnectionId)
                    ReceiveHostHitClaim(claim, scope->sourceDeliverySerial);
                continue;
            }
            if (type == PacketType::RemoteHit) { AdmitRemoteHit(*scope, payload, size); continue; } // VUH-1515
            if (type == PacketType::TargetAuthority) { AdmitTargetAuthority(*scope, payload, size); continue; } // VUH-1515
            if (CurrentRole() != Role::Client) continue;
            ByteReader r(payload, size);
            if (progresssync::HandlePacket(type, r)) {
                if (scope && PackPreparationActive() &&
                    !progresssync::DesiredMatchesFull(g_nativeResync->snapshot.progress, g_nativeResync->context.generation))
                    ContinuePackPreparation(scope->hostSourceSerial, true);
                continue;
            }
            if (type == PacketType::EventHold) {
                EventHold hold; read(r, hold);
                if (r.atEnd() && hold.epoch == g_host.epoch) {
                    if (PackPreparationActive())
                        ContinuePackPreparation(scope->hostSourceSerial,
                            hold.active != g_nativeResync->snapshot.hold.active ||
                            hold.eventProgram != g_nativeResync->snapshot.hold.eventProgram);
                    else ObserveEventControl(*scope, hold.active ? eventhold::Kind::Acquire : eventhold::Kind::Release,
                                             hold.epoch, nullptr, hold.eventProgram);
                }
                continue;
            }
            if (type == PacketType::RoomTransition) {
                RoomTransition t;
                readRoomTransitionPacket(r, t);
                // Reliable delivery normally orders these, but never let an
                // old/replayed epoch roll back authority. Epoch zero is unset.
                const auto advance = t.epoch - g_host.epoch;
                if (t.epoch != 0 && (g_host.epoch == 0 || (advance != 0 && advance < 0x80000000u))) {
                    ProducerWorldContext loadContext;
                    const bool loadContextAvailable = CaptureWorldContext(loadContext);
                    if (!loadContextAvailable || !warp::QueueHostTransition(t, loadContext, &*scope, nullptr, nullptr)) {
                        if (g_log) g_log("[enemysync] client transition rejected epoch=%u", t.epoch);
                        continue;
                    }
                    g_lastRoomPacket = t;         // review F2: a later resync of this load reuses its trailer
                    g_spawnPickUnknownEpoch = 0;
                    g_host = {};
                    g_mirror.Reset(0);  // VUH-1515 S4
                    g_population.Rebase(0);
                    if (!g_resyncPlan) {
                        g_resyncWriteFence = ResyncWriteFence::None;
                        g_resyncRecordAuthority.reset();
                    }
                    ClearActivation();
                    ClearPendingHits();
                    g_lastHashMs = 0;
                    g_host.epoch = t.epoch;
                    g_host.world = t.worldId;
                    g_host.room = t.roomId;
                    g_host.btl = t.battleProgram;
                    // No old-room pointer can survive even a same-room reload.
                    g_inst.spawns.clear();
                    g_inst.byActor.clear();
                    SYNC_LOG("[enemysync] client: host epoch %u room %02X/%02X btl %u", t.epoch,
                             t.worldId, t.roomId, t.battleProgram);
                    ContinuePackPreparation(scope->hostSourceSerial, true);
                    ObserveEventControl(*scope, eventhold::Kind::Transition, t.epoch, &t, t.eventProgram);
                }
            } else if (type == PacketType::EnemyManifest) {
                EnemyManifest m;
                read(r, m);
                if (!r.atEnd() || m.epoch != g_host.epoch) continue;
                bool uniqueManifest = true;
                for (std::size_t i = 0; i < m.entries.size(); ++i)
                    for (std::size_t j = 0; j < i; ++j)
                        if (m.entries[i].netId == m.entries[j].netId) uniqueManifest = false;
                if (!uniqueManifest) { g_host.manifestComplete = false; InvalidateClientClaims("duplicate-manifest-id"); continue; }
                if (!g_recordBindingRequested && std::any_of(m.entries.begin(),m.entries.end(),[](const auto& e){return e.recordKey.has_value();})) {
                    g_host.manifestComplete=false; InvalidateClientClaims("record-binding flag mismatch");
                    if (g_log) g_log("[record-binding] refused incompatible peer flag=0");
                    continue;
                }
                AdvanceClientManifestRevision();
                const bool packChanged = PackManifestChanged(m);
                if (m.replace) { g_host.enemies.clear(); g_host.manifestComplete = true; }
                for (const auto& e : m.entries) {
                    HostEnemy& h = g_host.enemies[e.netId];
                    if (h.objectId != e.objectId || h.spawnIndex != e.spawnIndex ||
                        h.battleProgram != e.battleProgram || h.recordKey != e.recordKey || !SamePoint(h.spawnPos, e.spawnPosition)) {
                        h.hpKnown = false; h.hp = h.maxHp = -1; h.hpSourceSequence = 0;
                    }
                    h.recordKey = e.recordKey;
                    h.spawnIndex = e.spawnIndex;
                    h.objectId = e.objectId;
                    h.spawnPos = e.spawnPosition;
                    h.battleProgram = e.battleProgram;
                }
                SYNC_LOG("[enemysync] client: manifest epoch %u +%zu (%zu known)", m.epoch,
                         m.entries.size(), g_host.enemies.size());
                ContinuePackPreparation(scope->hostSourceSerial, packChanged);
            } else if (type == PacketType::EnemyHp) {
                EnemyHp m;
                read(r, m);
                // Complete framing and current epoch must be established before
                // a hostile/malformed high sequence can poison the high-water.
                if (packet.size() != size + 3 || r.remaining() != 0 || !m.epoch ||
                    m.epoch != g_host.epoch || !m.sequence || m.sequence < g_hostHpSequence) continue;
                // Equal trusted relay cache replay can restore a replaced
                // manifest. NetworkClient admits equality only when reliable.
                g_hostHpSequence = m.sequence;
                const bool packChanged = PackHpChanged(m);
                for (const auto& e : m.entries) {
                    auto it = g_host.enemies.find(e.netId);
                    if (it != g_host.enemies.end()) {
                        auto& host = it->second;
                        if (!host.hpKnown || host.hp != e.hp || host.maxHp != e.maxHp) AdvanceClientManifestRevision();
                        host.hp = e.hp; host.maxHp = e.maxHp; host.hpKnown = true; host.hpSourceSequence = m.sequence;
                    }
                }
                ContinuePackPreparation(scope->hostSourceSerial, packChanged);
            } else if (type == PacketType::EnemyDeath) {
                EnemyDeath m;
                read(r, m);
                if (m.epoch != g_host.epoch) continue;
                auto it = g_host.enemies.find(m.netId);
                if (it != g_host.enemies.end()) { it->second.dead = true; AdvanceClientManifestRevision(); }
                g_mirror.Erase(m.netId);  // VUH-1515 N6: a death is not a stream release
                SYNC_LOG("[enemysync] client: host death epoch %u netId %u", m.epoch, m.netId);
                ContinuePackPreparation(scope->hostSourceSerial, true);
            } else if (type == PacketType::EnemyMotion) {
                // VUH-1515: pose/motion only; never HP, binding, manifest or hash state.
                if (!g_mirrorRequested) {  // N2: a flag mismatch is visible and costs no parse
                    if (!g_mirrorIgnoredLogged && g_log)
                        g_log("[enemy-mirror] stream-ignored flag=0 (the host has KH2COOP_ENEMY_MIRROR=1)");
                    g_mirrorIgnoredLogged = true;
                    continue;
                }
                EnemyMotion m;
                read(r, m);
                if (packet.size() != size + 3 || r.remaining() != 0 || !m.epoch || m.epoch != g_host.epoch) continue;
                if (g_mirror.Ingest(m, g_hitTraceFrame) && !g_mirrorFirstPacketLogged) {
                    g_mirrorFirstPacketLogged = true;
                    if (g_log) g_log("[enemy-mirror] client first-stream frame=%u epoch=%u hostFrame=%u entries=%zu",
                                     g_hitTraceFrame, m.epoch, m.hostFrame, m.entries.size());
                }
            }
        } catch (const std::exception&) {
            SYNC_LOG("[enemysync] client: malformed world packet");
        }
    }
    return hostSessionReset;
}

// POD-only final sampled checks. Full preflight already checked the entire
// catalog; this repeats context and selected native association/content at the
// write boundary. It is not an atomicity or continuous-lifetime guarantee.
bool ResyncWriteCurrent(const NativeEnemy& expected, const NativeRecordWriteCheck& check) {
    RoomTransition current;
    return g_resyncWriteFence == ResyncWriteFence::Exact &&
        WorldContextCurrent(check.context) && SafeNativeGameplay() &&
        check.transition == warp::TransitionSerial() && check.load == warp::LoadSerial() &&
        ReadLocationChecked(current) && SameLocation(current, check.room) &&
        Read<uintptr_t>(expected.actor + 0x9E8) == check.controller &&
        Read<uintptr_t>(expected.actor + 0x9F0) == check.record &&
        Read<std::int32_t>(g_exeBase + 0x2A10418) == check.tableCount &&
        std::memcmp(reinterpret_cast<const void*>(check.tableEntry), check.tableBytes.data(), 16) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(check.controller), check.controllerBytes.data(), 64) == 0 &&
        std::memcmp(reinterpret_cast<const void*>(check.header), check.headerBytes.data(), 44) == 0 &&
        check.records && check.recordBytes &&
        std::memcmp(reinterpret_cast<const void*>(check.spawnArray), check.records, check.recordBytes) == 0;
}
bool ResyncFinalWriteContext(const NativeRecordWriteCheck& check) {
    RoomTransition current;
    return g_resyncWriteFence == ResyncWriteFence::Exact && CurrentRole() == Role::Client &&
        WorldContextCurrent(check.context) && SafeNativeGameplay() && g_host.arrived && g_host.epoch == check.room.epoch &&
        check.transition == warp::TransitionSerial() && check.load == warp::LoadSerial() &&
        ReadLocationChecked(current) && SameLocation(current, check.room);
}
bool ResyncNativeSampleCurrent(const NativeEnemy& expected) {
    return expected.hp > 0 && expected.maxHp > 0 && expected.hp <= expected.maxHp &&
        (expected.objectType == offsets::objentry::TYPE_MOB || expected.objectType == offsets::objentry::TYPE_BOSS) &&
        Read<std::uint8_t>(expected.objentry + offsets::objentry::TYPE_FLAGS) == expected.objectType &&
        Read<std::uint16_t>(expected.objentry + offsets::objentry::NAME) != 0x5F46 && // F_ is noncombat
        Read<std::int32_t>(expected.status) == expected.hp && Read<std::int32_t>(expected.status + 4) == expected.maxHp;
}

// Sampled scope/identity/content proof; called again inside each SEH write leaf.
// Authority exists only for this complete owner-frame population. No historical
// native pointer is dereferenced before finding its checked proof.
bool RecordAuthorityCurrent(uintptr_t actor,std::uint32_t id,bool death=false) noexcept {
    if (!RecordFamily(id)) return true;
    const auto found=g_recordAuthority.find(actor);
    if (!recordbinding::ScopedAuthorityAllowed(g_recordBindingRequested,g_recordAuthorityRequested,id,
                                               found!=g_recordAuthority.end())) return false;
    const auto& p=found->second;
    if (p.terminal!=death || p.roots.objectId!=id) return false;
    RoomTransition room;
    if (!ReadLocationChecked(room)) return false;
    const recordbinding::Scope proof{p.generation,p.epoch,p.load,p.transition,p.frame,p.hostConnection,p.location};
    const recordbinding::Scope now{WorldSessionGeneration(),CurrentRole()==Role::Client?g_host.epoch:g_epoch,
        warp::LoadSerial(),warp::TransitionSerial(),g_hitTraceFrame,g_bridge.ConnectionId(0),
        {room.worldId,room.roomId,room.door,room.mapProgram,room.battleProgram,room.eventProgram}};
    if (!recordbinding::AuthorityScopeCurrent(proof,now,WorldContextCurrent(g_recordContext),SafeNativeGameplay())) return false;
    uintptr_t obj=0,status=0,controller=0,record=0;
    std::uint32_t objectId=0;
    std::array<std::uint8_t,64> bytes{};
    return ReadNative(actor+offsets::actor::OBJENTRY_PTR,obj) && obj==p.roots.objentry &&
        ReadNative(obj+offsets::objentry::OBJECT_ID,objectId) && objectId==id &&
        ReadNative(actor+ACTOR_STATUS,status) && status==p.roots.status &&
        ReadNative(actor+0x9E8,controller) && controller==p.roots.controller &&
        ReadNative(actor+0x9F0,record) && record==p.roots.record &&
        ReadNative(record,bytes) && bytes==p.bytes;
}

// No C++ objects requiring unwinding in the SEH leaves that call/write native memory.
bool WriteNativeHp(const NativeEnemy& expected, std::int32_t hp, std::uint32_t generation,
                   const NativeRecordWriteCheck* record = nullptr) {
    __try {
        if (!RecordAuthorityCurrent(expected.actor,expected.objectId)) return false;
        if (Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId) return false;
        if (!generation || WorldSessionGeneration() != generation || CurrentRole() != Role::Client) return false;
        if (record && !ResyncWriteCurrent(expected, *record)) return false;
        if (Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId) return false;
        if (record && (hp <= 0 || hp > expected.maxHp || !ResyncNativeSampleCurrent(expected) ||
                       !ResyncFinalWriteContext(*record))) return false;
        if (!RecordAuthorityCurrent(expected.actor,expected.objectId)) return false;
        *reinterpret_cast<std::int32_t*>(expected.status) = hp;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ApplyNativeDeath(const NativeEnemy& expected, int hp, std::uint32_t generation,
                      const NativeRecordWriteCheck* record = nullptr) {
    __try {
        if (!RecordAuthorityCurrent(expected.actor,expected.objectId,true)) return false;
        if (Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId) return false;
        if (!generation || WorldSessionGeneration() != generation || CurrentRole() != Role::Client) return false;
        if (record && !ResyncWriteCurrent(expected, *record)) return false;
        if (Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId) return false;
        if (record && (hp <= 0 || hp != expected.hp || !ResyncNativeSampleCurrent(expected) ||
                       !ResyncFinalWriteContext(*record))) return false;
        if (!RecordAuthorityCurrent(expected.actor,expected.objectId,true)) return false;
        g_applyStatDelta(reinterpret_cast<void*>(expected.actor), -hp, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Pointer validation and native invocation stay in one POD-only SEH leaf.
// A fault after entry has unknown side effects and must never be retried.
bool ApplyNativeClaim(const NativeEnemy& expected, const PendingHitClaim& pending, bool& attempted) {
    attempted = false;
    __try {
        if (!RecordAuthorityCurrent(expected.actor,expected.objectId) || !g_takeDamage ||
            Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId ||
            Read<std::int32_t>(expected.status) <= 0 || Read<std::int32_t>(expected.status + 4) <= 0 ||
            CurrentRole() != Role::Host || !SafeNativeGameplay() ||
            pending.transition != warp::TransitionSerial() || pending.load != warp::LoadSerial() ||
            pending.claim.epoch != g_epoch || pending.generation != g_activationOrderedGeneration ||
            pending.generation != g_bridge.SessionGeneration() ||
            pending.deliverySerial != g_bridge.DeliverySerial() ||
            !RequesterCurrent(static_cast<std::uint8_t>(pending.claim.attackerSlot),
                              pending.claim.requesterConnectionId, pending.requesterDeliverySerial) ||
            g_bridge.ConnectionId(static_cast<std::uint8_t>(pending.claim.attackerSlot)) !=
                pending.claim.requesterConnectionId) return false;
        if (!RecordAuthorityCurrent(expected.actor,expected.objectId)) return false;
        attempted = true;
        g_takeDamage(reinterpret_cast<void*>(expected.actor), -pending.claim.damage, 0, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Sample direct roots only; no record traversal and no creation/incarnation claim.
bool ReadOrdinaryIdentity(const NativeEnemy& sampled, ordinarybinding::Identity& out) {
    out = {sampled.actor, sampled.objentry, sampled.status, 0, 0, sampled.objectId};
    NativeEnemy after;
    bool isEnemy = false;
    return ReadNative(sampled.actor + 0x9E8, out.controller) &&
           ReadNative(sampled.actor + 0x9F0, out.record) &&
           ReadNativeEnemy(sampled.actor, after, isEnemy) && isEnemy &&
           SameNativeIdentity(sampled, after) && sampled.hp == after.hp && sampled.maxHp == after.maxHp;
}

// Complete frame-local content + census membership, sampled again every frame.
bool CaptureRecordPopulation(const NativeCensus& census, std::vector<recordbinding::Local>& local, RecordCatalog& verified) {
    local.clear();
    if (!CensusMatchesInstance(census) || !SafeNativeGameplay()) return false;
    const auto expected=recordbinding::Population({g_inst.world,g_inst.room,g_inst.door,g_inst.map,g_inst.btl,g_inst.evt});
    if (!expected) return false;
    RecordCatalog before, after;
    if (!FreshRecordCatalog(census,before)) return false;
    for (const auto& native : census.enemies) {
        if (!RecordFamily(native.objectId)) continue;
        const auto tracked=g_inst.byActor.find(native.actor);
        if (tracked==g_inst.byActor.end() || tracked->second>=g_inst.spawns.size()) return false;
        auto& spawn=g_inst.spawns[tracked->second];
        if (!spawn.present || FindNativeEnemy(census,spawn)!=&native) return false;
        recordbinding::Local l;
        if (!ReadOrdinaryIdentity(native,l.roots) || !spawn.identityRead ||
            l.roots.controller!=spawn.controller || l.roots.record!=spawn.record) return false;
        spawncontroller::NativeRecordMembership member;
        if (!RecordMember(before,census,native.actor,member) || member.issues ||
            member.controller[0]!=l.roots.controller || member.controller[1]!=l.roots.controller ||
            member.record[0]!=l.roots.record || member.record[1]!=l.roots.record) return false;
        const auto& definition=before.entries[member.tableIndex].content;
        const auto content=BuildNativeRecordContent(definition);
        if (content.status!=NativeRecordContentStatus::Complete || member.recordIndex>=content.records.size()) return false;
        const auto& record=content.records[member.recordIndex];
        l.key.location=definition.location; l.key.controllerKey=definition.groupKey;
        l.key.group=native_record_detail::u16(definition.header.data()+2);
        l.key.ordinal=member.recordIndex; l.key.nativeId=record.rawId;
        l.key.definition=content.comparisonSha256; l.key.header=desyncSha256(content.headerProjection);
        l.key.record=record.recordSha256;
        if (RecordObjectId(member.recordBytes[0])!=native.objectId || member.recordBytes[0]!=member.recordBytes[1] ||
            member.recordBytes[0]!=definition.records[member.recordIndex] || (!recordbinding::Admitted(native.objectId,l.key) && !recordbinding::ExclusionKnown(native.objectId,l.key))) return false;
        if (spawn.observedRecordKey && *spawn.observedRecordKey!=l.key) return false;
        spawn.observedRecordKey=l.key;
        // The entire tracked history must contain exactly this first body for
        // the record. A recycled body, including same address, consumes the load.
        unsigned recordHistory=0;
        for (const auto& old:g_inst.spawns)
            if (RecordFamily(old.objectId) &&
                ((old.controller==l.roots.controller && old.record==l.roots.record) ||
                 (old.observedRecordKey && *old.observedRecordKey==l.key))) ++recordHistory;
        if (recordHistory!=1) return false;
        l.id=static_cast<int>(spawn.spawnIndex)+1; l.living=native.hp>0;
        local.push_back(l);
    }
    std::size_t admittedHistory=0;
    for (const auto& old:g_inst.spawns) if (RecordFamily(old.objectId)) {
        if (!old.observedRecordKey || (!recordbinding::Admitted(old.objectId,*old.observedRecordKey) &&
            !recordbinding::ExclusionKnown(old.objectId,*old.observedRecordKey))) return false;
        if (recordbinding::Admitted(old.objectId,*old.observedRecordKey)) ++admittedHistory;
    }
    if (admittedHistory!=expected) return false;
    ordinarybinding::Identity checked;
    for (const auto& l:local) {
        const auto tracked=g_inst.byActor.find(l.roots.actor);
        if (tracked==g_inst.byActor.end()) return false;
        const auto* native=FindNativeEnemy(census,g_inst.spawns[tracked->second]);
        if (!native || !ReadOrdinaryIdentity(*native,checked) || checked!=l.roots) return false;
    }
    if (!FreshRecordCatalog(census,after) || !SameRecordCatalogSample(before,after) || !CensusMatchesInstance(census) || !SafeNativeGameplay()) return false;
    verified=std::move(after);return true;
}
bool CatalogContainsRecordKey(const RecordCatalog& catalog,std::uint32_t objectId,const EnemyRecordKey& key) {
    unsigned matches=0;
    for (std::uint32_t i=0;i<catalog.entryCount;++i) {
        const auto& def=catalog.entries[i].content;
        if (def.location!=key.location || def.groupKey!=key.controllerKey || native_record_detail::u16(def.header.data()+2)!=key.group) continue;
        const auto content=BuildNativeRecordContent(def);
        if (content.status!=NativeRecordContentStatus::Complete || key.ordinal>=content.records.size()) return false;
        const auto& rec=content.records[key.ordinal];
        if (RecordObjectId(def.records[key.ordinal])==objectId && rec.rawId==key.nativeId &&
            rec.recordSha256==key.record && content.comparisonSha256==key.definition && desyncSha256(content.headerProjection)==key.header) ++matches;
    }
    return matches==1;
}

bool ResolveRecordPopulation(const NativeCensus& census, bool host) {
    if (!g_recordAuthority.empty()) g_recordLastAuthority=g_recordAuthority;
    g_recordKeys.clear(); g_recordAuthority.clear();
    if (!g_recordBindingRequested) return false;
    for (auto& s:g_inst.spawns) if (RecordFamily(s.objectId)) s.netId=-1;
    ProducerWorldContext captured;
    if (!CaptureWorldContext(captured) || captured.generation!=WorldSessionGeneration()) { WithdrawRecordBindings("record-world-context-changed");return false; }
    std::vector<recordbinding::Local> allLocal;
    RecordCatalog verified;
    const bool complete=CaptureRecordPopulation(census,allLocal,verified);
    std::vector<recordbinding::Host> remote;
    if (host) {
        for (const auto& l:allLocal) remote.push_back({l.key,l.id,l.roots.objectId,false});
    } else {
        for (const auto& [id,h]:g_host.enemies) if (RecordFamily(h.objectId)) {
            if (!h.recordKey) {
                WithdrawRecordBindings("peer-missing-record-identity");
                if (g_log && g_recordLogs++<128) g_log("[record-binding] hold reason=incompatible-peer-missing-identity authority=0");
                return false;
            }
            if (!complete || !CatalogContainsRecordKey(verified,h.objectId,*h.recordKey)) {
                WithdrawRecordBindings("peer-key-not-in-fresh-native-catalog");return false;
            }
            remote.push_back({*h.recordKey,id,h.objectId,h.dead});
        }
    }
    const recordbinding::Scope scope{WorldSessionGeneration(),host?g_epoch:g_host.epoch,g_seenLoad,g_seenTransition,
        g_hitTraceFrame,g_bridge.ConnectionId(0),{g_inst.world,g_inst.room,g_inst.door,g_inst.map,g_inst.btl,g_inst.evt}};
    const auto selected=recordbinding::SelectPopulation(scope,allLocal,remote);
    const auto& local=selected.local;
    const auto result=g_recordLease.Resolve(scope,local,selected.host,complete && selected.complete && (host || g_host.manifestComplete),g_recordAuthorityRequested && !host);
    if (!result.admitted) {
        WithdrawRecordBindings(!complete?"native-membership-content-or-history":!selected.complete?selected.reason:result.reason);
        if (g_log && g_recordLogs++<128) g_log("[record-binding] hold epoch=%u frame=%u reason=%s local=%zu host=%zu authority=0",scope.epoch,scope.frame,result.reason,local.size(),remote.size());
        return false;
    }
    if (!WorldContextCurrent(captured) || captured.generation!=WorldSessionGeneration() ||
        CurrentRole()!=(host?Role::Host:Role::Client) || !CensusMatchesInstance(census)) { WithdrawRecordBindings("record-world-context-changed");return false; }
    g_recordContext=captured;
    if (!g_recordWasAdmitted && g_log) g_log("[record-binding] admission epoch=%u frame=%u load=%u transition=%u expected=%zu localExcluded=%zu hostExcluded=%zu rights=%u",
        scope.epoch,scope.frame,scope.load,scope.transition,local.size(),selected.localExcluded,selected.hostExcluded,static_cast<unsigned>(g_recordAuthorityRequested));
    const bool firstAdmission=!g_recordWasAdmitted;
    const bool emitMatches=g_recordMatchLogFrame!=scope.frame;
    g_recordWasAdmitted=true;
    // Publication includes positively validated negative keys. Those keys do
    // not enter the binding/authority map and never yield a client netId.
    for (const auto& l:allLocal) g_recordKeys[l.roots.actor]=l.key;
    if ((selected.localExcluded || selected.hostExcluded) && g_log && scope.frame%30==0)
        g_log("[record-binding] exclusion epoch=%u frame=%u load=%u transition=%u local=%zu peer=%zu authority=0 population=81",
            scope.epoch,scope.frame,scope.load,scope.transition,selected.localExcluded,selected.hostExcluded);
    if (g_log && (firstAdmission || scope.frame%30==0)) {
        for (const auto& l:allLocal) if (recordbinding::ExclusionKnown(l.roots.objectId,l.key))
            g_log("[record-binding] excluded-body role=%u epoch=%u frame=%u load=%u transition=%u objectId=%u netId=0 group=%u ordinal=%u nativeId=%u actor=%llX objentry=%llX status=%llX controller=%llX record=%llX definition=%s header=%s digest=%s membership=1 catalog=1 authority=0",
                static_cast<unsigned>(g_role),scope.epoch,scope.frame,scope.load,scope.transition,l.roots.objectId,l.key.group,l.key.ordinal,l.key.nativeId,
                static_cast<unsigned long long>(l.roots.actor),static_cast<unsigned long long>(l.roots.objentry),static_cast<unsigned long long>(l.roots.status),static_cast<unsigned long long>(l.roots.controller),static_cast<unsigned long long>(l.roots.record),
                desyncDigestHex(l.key.definition).c_str(),desyncDigestHex(l.key.header).c_str(),desyncDigestHex(l.key.record).c_str());
        for (const auto& h:remote) if (recordbinding::ExclusionKnown(h.objectId,h.key))
            g_log("[record-binding] excluded-peer role=%u epoch=%u frame=%u load=%u transition=%u objectId=%u peerNetId=%d group=%u ordinal=%u nativeId=%u definition=%s header=%s digest=%s catalog=1 authority=0",
                static_cast<unsigned>(g_role),scope.epoch,scope.frame,scope.load,scope.transition,h.objectId,h.id,h.key.group,h.key.ordinal,h.key.nativeId,
                desyncDigestHex(h.key.definition).c_str(),desyncDigestHex(h.key.header).c_str(),desyncDigestHex(h.key.record).c_str());
    }
    for (std::size_t i=0;i<local.size();++i) {
        const auto& l=local[i]; g_recordKeys[l.roots.actor]=l.key;
        const auto tracked=g_inst.byActor.find(l.roots.actor);
        if (tracked==g_inst.byActor.end()) { WithdrawRecordBindings("tracked-root-lost");return false; }
        if (!host) g_inst.spawns[tracked->second].netId=result.ids[i];
        if (g_recordAuthorityRequested) {
            RecordAuthorityProof proof; proof.roots=l.roots; proof.frame=scope.frame;
            proof.generation=scope.generation;proof.epoch=scope.epoch;proof.load=scope.load;
            proof.transition=scope.transition;proof.location=scope.location;proof.hostConnection=scope.hostConnection;
            const auto peer=std::find_if(selected.host.begin(),selected.host.end(),[&](const auto& h){return h.key==l.key;});
            proof.terminal=peer!=selected.host.end() && peer->dead;
            if (!ReadNative(l.roots.record,proof.bytes) || desyncSha256(proof.bytes)!=l.key.record) { WithdrawRecordBindings("record-bytes-changed");return false; }
            g_recordAuthority[l.roots.actor]=proof;
        }
        if (emitMatches && g_recordAuthorityRequested && g_log && (firstAdmission || g_hitTraceFrame%30==0))
            g_log("[record-authority] admitted epoch=%u frame=%u objectId=%u netId=%d actor=%llX terminal=%u authority=1",
                  scope.epoch,scope.frame,l.roots.objectId,result.ids[i],static_cast<unsigned long long>(l.roots.actor),
                  static_cast<unsigned>(g_recordAuthority[l.roots.actor].terminal));
        if (emitMatches && g_log && (firstAdmission || g_hitTraceFrame%30==0)) g_log("[record-binding] match role=%u epoch=%u frame=%u load=%u transition=%u objectId=%u netId=%d group=%u ordinal=%u nativeId=%u actor=%llX objentry=%llX status=%llX controller=%llX record=%llX definition=%s header=%s digest=%s authority=0",
            static_cast<unsigned>(g_role),scope.epoch,scope.frame,scope.load,scope.transition,l.roots.objectId,result.ids[i],l.key.group,l.key.ordinal,l.key.nativeId,
            static_cast<unsigned long long>(l.roots.actor),static_cast<unsigned long long>(l.roots.objentry),static_cast<unsigned long long>(l.roots.status),static_cast<unsigned long long>(l.roots.controller),static_cast<unsigned long long>(l.roots.record),
            desyncDigestHex(l.key.definition).c_str(),desyncDigestHex(l.key.header).c_str(),desyncDigestHex(l.key.record).c_str());
    }
    g_recordMatchLogFrame=scope.frame;
    return true;
}

bool ResolveOrdinaryBindings(const NativeCensus& census, std::uint32_t generation) {
    namespace ob = ordinarybinding;
    std::vector<ob::Local> locals;
    std::vector<std::size_t> indices;
    std::vector<ob::Host> hosts;
    const auto holdAll = [&] {
        for (auto& s : g_inst.spawns) { s.netId = -1; s.ordinaryBinding.bound = 0; }
        return false;
    };
    if (!CensusMatchesInstance(census) || !SafeNativeGameplay()) return holdAll();
    for (const auto& native : census.enemies) {
        const auto found = g_inst.byActor.find(native.actor);
        if (found == g_inst.byActor.end() || found->second >= g_inst.spawns.size()) return holdAll();
        const auto& s = g_inst.spawns[found->second];
        if (!s.present || FindNativeEnemy(census, s) != &native) return holdAll();
        if (RecordFamily(s.objectId)) continue;
        ob::Local local;
        if (!ReadOrdinaryIdentity(native, local.identity)) return holdAll();
        local.point = {s.spawnPos.x, s.spawnPos.y, s.spawnPos.z};
        local.prior = s.ordinaryBinding;
        local.available = !(s.killed && native.hp > 0); // observed lifetime contradiction stays unmatched
        local.living = native.hp > 0;
        locals.push_back(local);
        indices.push_back(found->second);
    }
    for (const auto& [id, h] : g_host.enemies) {
        if (h.battleProgram == g_inst.btl && !RecordFamily(h.objectId))
            hosts.push_back({id, h.objectId, {h.spawnPos.x, h.spawnPos.y, h.spawnPos.z}, h.dead});
    }
    const auto decisions = ob::Resolve(locals, hosts, {generation, g_host.epoch, g_hitTraceFrame});
    if (WorldSessionGeneration() != generation || !CensusMatchesInstance(census) || !SafeNativeGameplay()) return holdAll();
    // The entire fresh population is resolved before any native HP/death write.
    // Historical rows outside this census cannot retain write/hash authority.
    for (auto& s : g_inst.spawns) s.netId = -1;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        auto& s = g_inst.spawns[indices[i]];
        const auto& d = decisions[i];
        const auto& identity = locals[i].identity;
        if (g_log && g_inst.ordinaryBindingLogs < 128 &&
            (s.ordinaryBinding.bound != d.id || s.ordinaryLogReason != static_cast<int>(d.reason) ||
             s.ordinaryLogCandidate != d.candidate)) {
            ++g_inst.ordinaryBindingLogs;
            g_log("[enemy-binding] epoch=%u frame=%u spawn=%u actor=%llX objentry=%llX status=%llX controller=%llX record=%llX objectId=%u prior=%d floor=%d candidate=%d selected=%d reason=%s conflict=%u sampledIdentityOnly=1",
                  g_host.epoch, g_hitTraceFrame, s.spawnIndex,
                  static_cast<unsigned long long>(identity.actor), static_cast<unsigned long long>(identity.objentry),
                  static_cast<unsigned long long>(identity.status), static_cast<unsigned long long>(identity.controller),
                  static_cast<unsigned long long>(identity.record), identity.objectId,
                  s.ordinaryBinding.bound, s.ordinaryBinding.highWater, d.candidate, d.id, ob::Name(d.reason),
                  static_cast<unsigned>(d.conflict));
        }
        s.ordinaryLogReason = static_cast<int>(d.reason);
        s.ordinaryLogCandidate = d.candidate;
        s.ordinaryBinding = d.next;
        s.netId = d.id > 0 ? d.id : -1;
    }
    for (auto& s : g_inst.spawns) if (s.netId < 0) s.ordinaryBinding.bound = 0;
    if (g_recordBindingRequested) ResolveRecordPopulation(census, false);
    return true;
}

bool ClientFrame(const NativeCensus& initialCensus) {
    const auto generation = WorldSessionGeneration();
    if (!generation || !g_host.arrived || g_inst.world != g_host.world || g_inst.room != g_host.room || g_inst.btl != g_host.btl) {
        return false;  // not in the host's room instance: nothing to match
    }
    NativeCensus current = initialCensus;
    PollPackPreparation(current);
    const bool contentRequired = g_resyncWriteFence != ResyncWriteFence::None;
    std::vector<ResyncActorBinding> bindings;
    std::vector<NativeRecordContentDefinition> definitions;
    const auto clearBindings = [&] {
        for (auto& spawn : g_inst.spawns) spawn.netId = -1;
        if (g_nativeResync) g_nativeResync->firstObserved = false;
    };
    const auto failContentWrites = [&](const char* error) {
        if (!contentRequired) return;
        clearBindings(); g_resyncWriteFence = ResyncWriteFence::Failed;
        if (g_nativeResync && !g_nativeResync->finished)
            QueueNativeAck(ResyncAckStatus::Unavailable, error);
    };
    if (contentRequired) {
        if (g_resyncWriteFence != ResyncWriteFence::Exact || !g_resyncRecordAuthority) {
            // A checkpoint is observation only. Allow its independent observer
            // to run even when cached host HP/deaths disagree with native state.
            // Do not erase its successful first frame on every subsequent tick.
            if (g_resyncWriteFence == ResyncWriteFence::ObserveOnly) {
                for (auto& spawn : g_inst.spawns) spawn.netId = -1;
                return true;
            }
            clearBindings(); return false;
        }
        try {
            if (!CollectResyncBindings(g_resyncRecordAuthority->snapshot, g_resyncRecordAuthority->context,
                                       current, bindings, definitions)) {
                clearBindings(); return false;
            }
        } catch (const std::exception&) {
            clearBindings(); g_resyncWriteFence = ResyncWriteFence::Failed;
            if (g_nativeResync && !g_nativeResync->finished)
                QueueNativeAck(ResyncAckStatus::Unavailable, "native content preflight unavailable");
            return false;
        }
    }
    if (!contentRequired && !ResolveOrdinaryBindings(current, generation)) return false;
    for (Spawn& s : g_inst.spawns) {
        if (!s.present || s.killed || !RecordAuthorityCurrent(s.actor,s.objectId,g_host.enemies.contains(static_cast<std::uint16_t>(s.netId)) && g_host.enemies.at(static_cast<std::uint16_t>(s.netId)).dead)) continue;
        const NativeRecordWriteCheck* recordCheck = nullptr;
        ResyncRecordReference localRecord;
        if (contentRequired) {
            const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto& b) { return b.actor == s.actor; });
            if (binding == bindings.end()) { clearBindings(); return false; }
            s.netId = binding->netId;
            recordCheck = &binding->write;
            localRecord = binding->record;
        }
        if (s.netId < 0) continue;
        const HostEnemy& h = g_host.enemies[static_cast<std::uint16_t>(s.netId)];
        const auto* sampled = FindNativeEnemy(current, s);
        if (!sampled) continue; // a preceding native lethal may have removed it
        NativeEnemy native;
        bool isEnemy = false;
        if (!CensusMatchesInstance(current) || !SafeNativeGameplay() ||
            !ReadNativeEnemy(s.actor, native, isEnemy) || !isEnemy || !SameNativeIdentity(*sampled, native) ||
            (contentRequired && (native.hp != sampled->hp || native.maxHp != sampled->maxHp ||
                                 native.objectType != sampled->objectType || native.hp <= 0))) {
            failContentWrites("native target changed before write; partial application possible");
            InterruptCensus("client target changed", s.actor);
            return false;
        }
        if (!contentRequired) {
            ordinarybinding::Identity identity;
            if (RecordFamily(s.objectId) ? !RecordAuthorityCurrent(s.actor,s.objectId,h.dead) :
                (!ReadOrdinaryIdentity(native, identity) || identity != s.ordinaryBinding.identity)) {
                for (auto& held : g_inst.spawns) { held.netId = -1; held.ordinaryBinding.bound = 0; }
                InterruptCensus("ordinary binding roots changed before write", s.actor);
                return false;
            }
            // No ordinary resurrection or write based on an unavailable incarnation.
            if (native.hp <= 0) continue;
        }
        bool ordinaryLethal = false;
        if (h.dead) {
            if (native.hp > 0 && g_applyStatDelta && !s.deathAttempted) {
                s.deathAttempted = true;
                ordinaryLethal = !contentRequired;
                const int before = native.hp;
                if (!ApplyNativeDeath(native, before, generation, recordCheck)) {
                    if (contentRequired) {
                        clearBindings(); g_resyncWriteFence = ResyncWriteFence::Failed;
                        if (g_nativeResync && !g_nativeResync->finished)
                            QueueNativeAck(ResyncAckStatus::Unavailable, "native lethal outcome unavailable; partial application possible");
                    }
                    InterruptCensus("native lethal fault; outcome unknown", s.actor);
                    return false;
                }
                NativeEnemy after;
                if (!ReadNativeEnemy(s.actor, after, isEnemy) || !isEnemy || !SameNativeIdentity(native, after)) {
                    failContentWrites("native lethal read unavailable; partial application possible");
                    InterruptCensus("native lethal outcome unavailable", s.actor);
                    return false;
                }
                native = after;
                if (native.hp <= 0) {
                    if (g_log) g_log("[enemysync] client: host death netId %d applied (hp %d -> %d)",
                                     s.netId, before, native.hp);
                } else if (g_log) {
                    g_log("[enemysync] client: host death netId %d FAILED (hp %d -> %d)", s.netId,
                          before, native.hp);
                }
                // The native call may mutate the roster. Subsequent targets need
                // current membership, not addresses retained before the call.
                current = CaptureNativeCensus();
                if (!CensusMatchesInstance(current)) {
                    failContentWrites("native census unavailable after lethal; partial application possible");
                    InterruptCensus(current.state == CensusState::Complete ? "instance changed" : current.reason,
                                    current.failedAt, current.nodeCount);
                    return false;
                }
                if (contentRequired) {
                    // Never carry a pre-lethal batch into another native target.
                    // If removal changes the complete expected population, the
                    // remaining targets stay held; no historical absence inference.
                    try {
                        if (!CollectResyncBindings(g_resyncRecordAuthority->snapshot, g_resyncRecordAuthority->context,
                                                   current, bindings, definitions)) {
                            clearBindings(); g_resyncWriteFence = ResyncWriteFence::Failed;
                            if (g_nativeResync && !g_nativeResync->finished)
                                QueueNativeAck(ResyncAckStatus::Unavailable, "native population changed after lethal; partial application possible");
                            return false;
                        }
                    } catch (const std::exception&) {
                        clearBindings(); g_resyncWriteFence = ResyncWriteFence::Failed;
                        return false;
                    }
                }
            }
            s.killed = native.hp <= 0;
            if (RecordFamily(s.objectId)) {
                if (g_log) g_log("[record-authority] death epoch=%u frame=%u netId=%d objectId=%u actor=%llX hp=%d attempted=%u",
                    g_host.epoch,g_hitTraceFrame,s.netId,s.objectId,static_cast<unsigned long long>(s.actor),native.hp,static_cast<unsigned>(s.deathAttempted));
                WithdrawRecordBindings("terminal-population-retired");
            }
            // A lethal can create/remove actors. Do not carry the pre-call
            // ordinary batch into another write: next owner frame resolves
            // the whole fresh census again. Existing per-spawn once fence stays.
            if (ordinaryLethal) return WorldSessionGeneration() == generation;
        } else if (h.hp > 0 && native.hp != h.hp) {
            if (!WriteNativeHp(native, h.hp, generation, recordCheck)) { // never 0: deaths are explicit
                if (contentRequired) {
                    clearBindings(); g_resyncWriteFence = ResyncWriteFence::Failed;
                    if (g_nativeResync && !g_nativeResync->finished)
                        QueueNativeAck(ResyncAckStatus::Unavailable, "native HP write unavailable; partial application possible");
                }
                InterruptCensus("client HP write unavailable", s.actor);
                return false;
            }
            if (RecordFamily(s.objectId) && g_log) {
                std::int32_t readback=-1;const bool read=ReadNative(native.status,readback);
                g_log("[record-authority] hp epoch=%u frame=%u netId=%d objectId=%u actor=%llX before=%d requested=%d readback=%d read=%u",
                    g_host.epoch,g_hitTraceFrame,s.netId,s.objectId,static_cast<unsigned long long>(s.actor),native.hp,h.hp,readback,static_cast<unsigned>(read));
            }
            // Outside the checked leaf/final-check-to-store interval. The leaf
            // returned after storing; later observation supplies actual HP.
            if (contentRequired && g_nativeResync && ResyncLogAllowed()) {
                const auto& state = *g_nativeResync;
                const auto source = std::find_if(state.snapshot.enemies.begin(), state.snapshot.enemies.end(), [&](const auto& e) {
                    return e.identity.netId == s.netId;
                });
                if (source != state.snapshot.enemies.end())
                    g_log("[resync-content] hp-store session=%s host=%llu request=%llu targetSlot=%u connection=%llu delivery=%llu phase=%u cut=%llu netId=%u objectId=%u hostDefinition=%u hostRecord=%u localDefinition=%u localRecord=%u actor=%llX controller=%llX nativeRecord=%llX hpBefore=%d requestedHp=%d maxHp=%d storeReturned=1 postStoreReadback=0 fence=%u",
                          state.begin.key.sessionId.c_str(), static_cast<unsigned long long>(state.begin.key.hostConnectionId),
                          static_cast<unsigned long long>(state.begin.key.requestId), static_cast<unsigned>(state.target.slot),
                          static_cast<unsigned long long>(state.target.connectionId), static_cast<unsigned long long>(state.target.deliverySerial),
                          static_cast<unsigned>(state.begin.phase), static_cast<unsigned long long>(state.begin.snapshotCut),
                          static_cast<unsigned>(s.netId), native.objectId,
                          static_cast<unsigned>(source->record.definitionIndex), static_cast<unsigned>(source->record.recordIndex),
                          static_cast<unsigned>(localRecord.definitionIndex), static_cast<unsigned>(localRecord.recordIndex),
                          static_cast<unsigned long long>(native.actor), static_cast<unsigned long long>(recordCheck->controller),
                          static_cast<unsigned long long>(recordCheck->record), native.hp, h.hp, native.maxHp,
                          static_cast<unsigned>(g_resyncWriteFence));
            }
        }
    }
    return WorldSessionGeneration() == generation;
}

// ---- Both -------------------------------------------------------------------

void PublishAppliedHash(std::uint32_t frame, const NativeCensus& beforeApply) {
    if (g_role == Role::Off || !g_inst.live ||
        g_hostBeginPending || !SafeNativeGameplay()) return;
    const auto epoch = g_role == Role::Host ? g_epoch : g_host.epoch;
    if (epoch == 0 || (g_role == Role::Client &&
        (!g_host.arrived || !warp::HostTransitionArrived(epoch)))) return;

    const auto location = warp::ReadLocation();
    if (location.worldId != g_inst.world || location.roomId != g_inst.room ||
        location.door != g_inst.door || location.mapProgram != g_inst.map ||
        location.battleProgram != g_inst.btl || location.eventProgram != g_inst.evt) return;
    const auto now = GetTickCount64();
    if (g_lastHashMs != 0 && now - g_lastHashMs < HASH_INTERVAL_MS) return;

    StateHash state;
    state.epoch = epoch;
    state.worldId = location.worldId;
    state.roomId = location.roomId;
    if (!progresssync::ReadHash(state.progressHash)) return;

    const auto census = CaptureNativeCensus();
    if (!CensusMatchesInstance(census)) {
        InterruptCensus(census.state == CensusState::Complete ? "instance changed" : census.reason,
                        census.failedAt, census.nodeCount);
        return;
    }
    std::vector<AppliedEnemyState> observed;
    std::vector<uintptr_t> actors;
    for (const auto& native : census.enemies) {
        // Native membership and HP supply rows. The tracker supplies only a
        // binding; a missing tracker row must never hide a living native actor.
        std::uint16_t netId = 0;
        const auto tracked = g_inst.byActor.find(native.actor);
        if (tracked != g_inst.byActor.end()) {
            const auto& spawn = g_inst.spawns[tracked->second];
            const auto* prior = FindNativeEnemy(beforeApply, spawn);
            if (prior && SameNativeIdentity(*prior, native)) {
                if (g_role == Role::Host && spawn.announced) {
                    netId = static_cast<std::uint16_t>(spawn.spawnIndex + 1);
                } else if (g_role == Role::Client && spawn.netId > 0) {
                    const auto bound = g_host.enemies.find(static_cast<std::uint16_t>(spawn.netId));
                    if (bound != g_host.enemies.end() && bound->second.objectId == native.objectId)
                        netId = bound->first;
                }
            }
        }
        // Preserve manifest-before-hash ordering for newly appeared host actors.
        // Clients still expose all unmatched native actors with netId zero.
        if (g_role == Role::Host && netId == 0) return;
        observed.push_back({netId, native.objectId, native.hp});
        actors.push_back(native.actor);
    }
    const auto live = canonicalAppliedEnemies(observed);
    const auto unmatched = std::count_if(live.begin(), live.end(),
                                         [](const auto& record) { return record.netId == 0; });
    state.enemiesHash = hashAppliedEnemies(live);
    // Reuse only the complete, instance-checked census and canonical living
    // rows above. CaptureNativeCensus bounds their count by MAX_TRAVERSAL.
    state.nativeCensusComplete = true;
    state.nativeLivingCount = static_cast<std::uint32_t>(live.size());
    state.nativeCombatCount = static_cast<std::uint32_t>(census.enemies.size());
    g_lastHashMs = now;
    // Keep the existing capture/enqueue sequence, exposing its exact context to diagnostics.
    const auto hashPacket = encode(state);
    ProducerWorldContext hashContext;
    const auto hashSlot = g_hashDiagnosticSink ? g_bridge.LocalSlot() : WORLD_SLOT_UNKNOWN;
    const auto hashConnection = g_hashDiagnosticSink ? g_bridge.ConnectionId(hashSlot) : 0;
    if (!CaptureWorldContext(hashContext) || !SendCapturedWorld(hashPacket, hashContext)) return;
    if (g_hashDiagnosticSink) {
        const bool contextCurrent = hashConnection != 0 && hashSlot < 3 &&
            g_bridge.LocalSlot() == hashSlot && g_bridge.ConnectionId(hashSlot) == hashConnection &&
            WorldContextCurrent(hashContext) && CensusMatchesInstance(census);
        g_hashDiagnosticStream.emit(g_hashDiagnosticSink,"native-hash-publication",[&](auto& out) {
            out<<" action=publish role="<<(g_role==Role::Host?"host":"client")<<" slot="<<unsigned(hashSlot)
               <<" connection="<<hashConnection<<" epoch="<<state.epoch<<" frame="<<frame
               <<" generation="<<hashContext.generation<<" delivery="<<hashContext.deliverySerial
               <<" hostSource="<<hashContext.hostSourceSerial
               <<" contextCurrent="<<contextCurrent<<" transition="<<census.transition<<" load="<<census.load
               <<" world="<<census.location.worldId<<" room="<<census.location.roomId<<" door="<<unsigned(census.location.door)
               <<" map="<<census.location.mapProgram<<" battle="<<census.location.battleProgram<<" event="<<census.location.eventProgram
               <<" enemiesHash="<<state.enemiesHash<<" progressHash="<<state.progressHash
               <<" complete=1 censusCount="<<census.enemies.size()<<" living="<<live.size()
               <<" selectedAvailable="<<census.enemies.empty()<<" selectedPresent="<<(census.enemies.empty()?"0":"unavailable")
               <<" selectedProof="<<(census.enemies.empty()?"complete-whole-combat-census-empty":"unavailable")
               <<" bridgeEnqueued=1 relayReceived=unproven coarseHintOnly=1";
        });
        g_hashDiagnosticStream.seal(g_hashDiagnosticSink,"native-hash-publication","publish");
    }
    // These fixture observations must not disappear when the general sync log
    // budget runs out. frame+epoch associates every raw record with its hash.
    if (g_log) {
        g_log("[statehash] role=%s epoch=%u frame=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u enemies=%08X progress=%08X count=%zu unmatched=%zu observed=%zu",
              g_role == Role::Host ? "host" : "client", epoch, frame, location.worldId,
              location.roomId, location.door, location.mapProgram, location.battleProgram,
              location.eventProgram, state.enemiesHash, state.progressHash, live.size(),
              static_cast<std::size_t>(unmatched), observed.size());
        for (std::size_t i = 0; i < observed.size(); ++i) {
            const auto& record = observed[i];
            g_log("[statehash] native epoch=%u frame=%u netId=%u objectId=%u hp=%d actor=%llX",
                  epoch, frame, record.netId, record.objectId, record.hp,
                  static_cast<unsigned long long>(actors[i]));
        }
    }
}

spawnrow::Row SpawnRowOf(const Spawn& s) {
    return {{s.objectId, s.objentry, s.status, s.controller, s.record, s.identityRead}, s.present, s.lastHp};
}

// Commit presence only from a complete native list; callbacks are not a census.
std::vector<std::size_t> TrackSpawns(const NativeCensus& census) {
    std::vector<std::size_t> fresh;
    std::unordered_map<uintptr_t, std::size_t> present;
    for (const auto& native : census.enemies) {
        const auto actor = native.actor;
        auto it = g_inst.byActor.find(actor);
        std::size_t index;
        // An address seen before is the same enemy: still in the list (a
        // dying enemy stays there at 0 HP through its death animation), or
        // back after leaving it alive (e.g. burrowed). Changed checked metadata,
        // a slot whose enemy left the list dead, or (094908) a recycled address
        // with a different native spawn controller/record is a new spawn
        // (SpawnRowIdentity.hpp; unreadable controller/record: the old rule alone).
        spawnrow::Sample now {native.objectId, native.objentry, native.status, 0, 0, false};
        now.identityRead = ReadNative(actor + 0x9E8, now.controller) && ReadNative(actor + 0x9F0, now.record);
        const bool known = it != g_inst.byActor.end();
        const bool same = known && spawnrow::SameSpawnRow(SpawnRowOf(g_inst.spawns[it->second]), now);
        if (known && !same) {
            const auto& old = g_inst.spawns[it->second];
            if (old.objectId == now.objectId && old.objentry == now.objentry && old.status == now.status &&
                (old.present || old.lastHp > 0))  // the old rule would have kept this row
                SYNC_LOG("[enemysync] spawn %u @%llX recycled: controller %llX -> %llX record %llX -> %llX (new row)",
                         old.spawnIndex, static_cast<unsigned long long>(actor),
                         static_cast<unsigned long long>(old.controller), static_cast<unsigned long long>(now.controller),
                         static_cast<unsigned long long>(old.record), static_cast<unsigned long long>(now.record));
            // Recycled while still listed: the old row leaves now, but its actor stays in `present` under the new
            // row, so the absence loop below would not log it (review nit).
            if (old.present)
                SYNC_LOG("[enemysync] spawn %u @%llX left the list (recycled while listed, last hp %d)", old.spawnIndex,
                         static_cast<unsigned long long>(actor), old.lastHp);
        }
        if (same) {
            index = it->second;
        } else {
            Spawn s;
            s.spawnIndex = static_cast<std::uint16_t>(g_inst.spawns.size());
            s.actor = actor;
            s.objentry = native.objentry;
            s.status = native.status;
            s.controller = now.controller;
            s.record = now.record;
            s.identityRead = now.identityRead;
            s.objectId = native.objectId;
            s.objectType = native.objectType;
            s.spawnPos = native.position;
            if(g_role==Role::Host && g_populationRequested && g_populationPointAvailable &&
                g_populationPointLoad==census.load && g_populationPointTransition==census.transition &&
                g_populationPointLocation==PopulationLocation(census.location) &&
                g_mirrorFrame-g_populationPointFrame<=1) {
                s.populationBirthPoint=g_populationObservedPoint;s.populationPointCaptured=true;
            }
            g_inst.spawns.push_back(s);
            index = g_inst.spawns.size() - 1;
            fresh.push_back(index);
        }
        present[actor] = index;
        g_inst.byActor[actor] = index;
    }
    for (Spawn& s : g_inst.spawns) {
        if (s.present && !present.count(s.actor)) {
            SYNC_LOG("[enemysync] spawn %u @%llX left the list (last hp %d)", s.spawnIndex,
                     static_cast<unsigned long long>(s.actor), s.lastHp);
        }
        s.present = false;
    }
    for (const auto& [actor, index] : present) {
        Spawn& s = g_inst.spawns[index];
        s.present = true;
        if (const auto* native = FindNativeEnemy(census, s)) {
            s.lastHp = native->hp; s.lastMaxHp = native->maxHp; s.objectType = native->objectType;
        }
    }
    return fresh;
}

bool ProcessHostHitClaims(NativeCensus& census) {
    if (g_hostClaimProcessing) return false;
    struct ProcessingScope {
        ProcessingScope() { g_hostClaimProcessing = true; }
        ~ProcessingScope() { g_hostClaimProcessing = false; }
    } processingScope;
    unsigned nativeCalls = 0;
    while (g_hitCount != 0 && nativeCalls < HIT_NATIVE_PER_FRAME) {
        const PendingHitClaim pending = g_pendingHits[g_hitHead];
        g_hitHead = (g_hitHead + 1) % HIT_PENDING_CAP;
        --g_hitCount;
        const auto& claim = pending.claim;
        const auto slot = static_cast<std::uint8_t>(claim.attackerSlot);
        if (slot < 1 || slot > 2 || claim.requesterConnectionId == 0 ||
            claim.seq == 0 || claim.damage <= 0) {
            LogClaim("reject", claim, "invalid pending identity/damage");
            continue;
        }
        RoomTransition location;
        if (GetTickCount64() - pending.receivedMs >= HIT_DEADLINE_MS) {
            LogClaim("reject", claim, "claim expired");
            continue;
        }
        if (!ActivationContext(Role::Host, location) || pending.generation != g_activationGeneration ||
            pending.transition != g_seenTransition || pending.load != g_seenLoad ||
            claim.epoch != location.epoch || !CensusMatchesInstance(census) ||
            !RequesterCurrent(slot, claim.requesterConnectionId, pending.requesterDeliverySerial)) {
            LogClaim("reject", claim, "claim lifecycle or connection retired");
            continue;
        }
        auto& sequence = g_claimSequences[slot];
        if (sequence.connection != claim.requesterConnectionId)
            sequence = {claim.requesterConnectionId, 0};
        if (claim.seq <= sequence.consumed) {
            LogClaim("reject", claim, "sequence already consumed");
            continue;
        }
        const auto target = std::find_if(g_inst.spawns.begin(), g_inst.spawns.end(), [&](const Spawn& spawn) {
            return static_cast<std::uint32_t>(spawn.spawnIndex) + 1 == claim.netId &&
                   spawn.objectId == claim.objectId && spawn.present && spawn.announced && !spawn.deathSent;
        });
        const NativeEnemy* sampled = target == g_inst.spawns.end() ? nullptr : FindNativeEnemy(census, *target);
        NativeEnemy native;
        bool isEnemy = false;
        if (!sampled || !ReadNativeEnemy(sampled->actor, native, isEnemy) || !isEnemy ||
            !SameNativeIdentity(*sampled, native) || native.hp <= 0 || native.maxHp <= 0) {
            LogClaim("reject", claim, "announced live typed target unavailable");
            continue;
        }
        // Reliable claims are ordered by connection. Keep this high-water mark
        // across rooms, queue pressure, native no-ops and native faults.
        sequence.consumed = claim.seq;
        bool attempted = false;
        const bool nativeOk = ApplyNativeClaim(native, pending, attempted);
        if (!attempted) {
            LogClaim("reject", claim, "target/context changed before native call");
            continue;
        }
        ++nativeCalls;
        // Never retain a Spawn reference across gameplay: native damage can
        // remove actors, emit another wave or start a transition.
        census = CaptureNativeCensus();
        const bool postAvailable = CensusMatchesInstance(census) &&
            pending.generation == g_bridge.SessionGeneration() && CurrentRole() == Role::Host;
        const auto after = std::find_if(census.enemies.begin(), census.enemies.end(), [&](const NativeEnemy& row) {
            return SameNativeIdentity(native, row);
        });
        const bool afterAvailable = postAvailable && after != census.enemies.end();
        if (g_log) g_log("[enemysync] host hit claim connection=%llu seq=%u epoch=%u netId=%u objectId=%u attackId=%u damage=%d beforeHp=%d afterHp=%d nativeOk=%u afterHpAvailable=%u",
                         static_cast<unsigned long long>(claim.requesterConnectionId), claim.seq, claim.epoch,
                         claim.netId, claim.objectId, claim.attackId, claim.damage, native.hp,
                         afterAvailable ? after->hp : -1, nativeOk ? 1u : 0u, afterAvailable ? 1u : 0u);
        if (postAvailable) TrackSpawns(census);
        if (!postAvailable || !nativeOk) {
            InterruptCensus(!nativeOk ? "native claim fault; outcome unknown" : "claim post-census unavailable",
                            native.actor, census.nodeCount);
            return false;
        }
    }
    return true;
}

// KH2COOP_ROLE=host|client overrides (tests); otherwise the runtime's
// session slot from the WorldBridge decides: 0 = host, 1-2 = client,
// unknown (no runtime connected yet) = off.
Role g_envRole = Role::Off;

Role ReadEnvRole() {
    char v[16] = {};
    if (GetEnvironmentVariableA("KH2COOP_ROLE", v, sizeof(v)) == 0) return Role::Off;
    if (std::strcmp(v, "host") == 0) return Role::Host;
    if (std::strcmp(v, "client") == 0) return Role::Client;
    return Role::Off;
}

Role CurrentRole() {
    if (!RuntimeWriterLive()) return Role::Off; // includes environment-role override
    if (g_envRole != Role::Off) return g_envRole;
    const std::uint8_t slot = g_bridge.LocalSlot();
    if (slot == 0) return Role::Host;
    if (slot == 1 || slot == 2) return Role::Client;
    return Role::Off;
}

} // namespace

void SetHashDiagnosticSink(CausalSink sink) {
    char value[8] {};
    const auto length=GetEnvironmentVariableA("KH2COOP_CAUSAL_DIAGNOSTICS",value,sizeof(value));
    if(length==1 && value[0]=='1')g_hashDiagnosticSink=std::move(sink);
    else g_hashDiagnosticSink={};
}

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta, TakeDamageFn takeDamage,bool populationDeathHelperVerified) {
    g_exeBase = exeBase;
    g_log = log;
    g_applyStatDelta = applyStatDelta;
    g_populationDeathHelperVerified=populationDeathHelperVerified && applyStatDelta!=nullptr;
    g_takeDamage = takeDamage;
    char eventHold[2] {};
    g_eventHoldProducerEnabled = GetEnvironmentVariableA("KH2COOP_EVENT_HOLD_PRODUCER", eventHold, sizeof(eventHold)) == 1 && eventHold[0] == '1';
    if (g_eventHoldProducerEnabled && g_log)
        g_log("[event-hold] producer configured=1 clientControl=0 ownerFrameOnly=1");
    char prepare[4] {};
    g_survivingPackEnabled = GetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", prepare, sizeof(prepare)) == 1 &&
        prepare[0] == '1';
    progresssync::Install(exeBase, log, SendCapturedWorld);
    char mirror[2] {};
    char recordFlag[4] {};
    g_recordBindingRequested = GetEnvironmentVariableA("KH2COOP_ENEMY_RECORD_BINDING",recordFlag,sizeof(recordFlag))==1 && recordFlag[0]=='1';
    if (g_recordBindingRequested && g_log) g_log("[record-binding] enabled schema=1 protocol=15 authority=0 families=317,76");
    g_mirrorRequested = GetEnvironmentVariableA("KH2COOP_ENEMY_MIRROR", mirror, sizeof(mirror)) == 1 && mirror[0] == '1';
    char authorityFlag[4]{};
    g_recordAuthorityRequested = g_recordBindingRequested && g_mirrorRequested &&
        GetEnvironmentVariableA("KH2COOP_ENEMY_RECORD_AUTHORITY",authorityFlag,sizeof(authorityFlag))==1 && authorityFlag[0]=='1';
    if (g_log) g_log("[record-authority] configured=%u scope=first-living-population terminal=one-death families=317,76",static_cast<unsigned>(g_recordAuthorityRequested));
    char delayText[16] {};
    const DWORD delayLength = GetEnvironmentVariableA("KH2COOP_ENEMY_CURSOR_DELAY_FRAMES", delayText, sizeof(delayText));
    const auto delay = delayLength > 0 && delayLength < sizeof(delayText)
        ? latency::parseEnemyDelayFrames(std::string_view(delayText, delayLength)) : std::nullopt;
    g_mirror = enemymirror::Stream(delay.value_or(enemymirror::kDelay));
    char latencyTrace[2] {};
    g_latencyTraceBudget = g_mirrorRequested &&
        GetEnvironmentVariableA("KH2COOP_LATENCY_TRACE", latencyTrace, sizeof(latencyTrace)) == 1 && latencyTrace[0] == '1'
        ? 18000u : 0u;
    if (g_mirrorRequested && g_log)
        g_log("[latency] enemyCursorDelayFrames=%u source=%s traceBudget=%u", g_mirror.delayFrames(),
              !delayLength ? "default" : delay ? "env" : "invalid-default", g_latencyTraceBudget);
    char population[2] {};
    g_populationRequested = g_mirrorRequested &&
        GetEnvironmentVariableA("KH2COOP_ENEMY_POPULATION", population, sizeof(population)) == 1 && population[0] == '1';
    char populationSpawn[2] {};
    g_populationSpawnRequested = g_populationRequested &&
        GetEnvironmentVariableA("KH2COOP_ENEMY_POPULATION_SPAWN", populationSpawn, sizeof(populationSpawn)) == 1 &&
        populationSpawn[0] == '1';
    if (g_populationSpawnRequested) {
        const auto* entry = reinterpret_cast<const std::uint8_t*>(exeBase + RVA_ENEMY_FACTORY);
        std::uint8_t bytes[sizeof(kEnemyFactoryBytes)] {};
        const bool readable = CopyCodeBytes(entry, bytes, sizeof(bytes));
        g_enemyFactory = readable && std::memcmp(bytes, kEnemyFactoryBytes, sizeof(bytes)) == 0
            ? reinterpret_cast<EnemyFactoryFn>(exeBase + RVA_ENEMY_FACTORY) : nullptr;
        if (g_log)
            g_log("[enemy-pop] configured=1 factory=%d missing=%u gap=%u maxForced=%zu cullHold=%u", g_enemyFactory ? 1 : 0,
                  enemypop::kMissingFrames, enemypop::kSpawnGap, enemypop::kMaxForced, enemypop::kCullHoldFrames);
    } else if (g_populationRequested && g_log) {
        g_log("[enemy-pop] configured=1 spawn=0 cullHold=%u (force-spawn needs KH2COOP_ENEMY_POPULATION_SPAWN=1)",
              enemypop::kCullHoldFrames);
    }
    char mirrorTrace[2] {};
    g_mirrorTrace = g_mirrorRequested &&
        GetEnvironmentVariableA("KH2COOP_ENEMY_MIRROR_TRACE", mirrorTrace, sizeof(mirrorTrace)) == 1 && mirrorTrace[0] == '1';
    const DWORD controlLength = g_mirrorRequested
        ? GetEnvironmentVariableA("KH2COOP_ENEMY_MIRROR_CONTROL", g_mirrorControlPath, sizeof(g_mirrorControlPath)) : 0;
    if (controlLength == 0 || controlLength >= sizeof(g_mirrorControlPath)) g_mirrorControlPath[0] = '\0';
    char families[128] {};
    enemymirror::FormatFamilies(families, sizeof(families));
    if (g_mirrorRequested && g_log)
        g_log("[enemy-mirror] configured=1 families=%s interval=%u delay=%u maxLag=%u stale=%u retake=%u gap=%u settle=%u "
              "trace=%d control=%d", families, enemymirror::kPublishInterval, g_mirror.delayFrames(),
              enemymirror::kMaxLag, enemymirror::kStaleFrames, enemymirror::kRetake, enemymirror::kGapTolerance,
              enemymirror::kSpawnSettleFrames, g_mirrorTrace ? 1 : 0, g_mirrorControlPath[0] ? 1 : 0);
    g_envRole = ReadEnvRole();
    try {
        std::random_device random;
        for (auto& word : g_activationIncarnation)
            word = (static_cast<std::uint64_t>(random()) << 32) | random();
    } catch (...) {
        g_activationIncarnation = {};
        if (g_log) g_log("  WARNING: spawn authority cannot generate client incarnation; clients hold");
    }
    if (!g_bridge.Open(GetCurrentProcessId())) {
        if (g_log) g_log("  WARNING: enemy sync: world bridge failed to open (%lu)", GetLastError());
        return;
    }
    if (g_log) {
        g_log("  Enemy sync ready (Local\\kh2coop_world_%lu); role %s", GetCurrentProcessId(),
              g_envRole == Role::Host     ? "host (KH2COOP_ROLE)"
              : g_envRole == Role::Client ? "client (KH2COOP_ROLE)"
                                          : "from the runtime's slot");
    }
}

void NoteActor(uintptr_t) {
    // Retained for EntityHook API compatibility. Per-actor callback coverage
    // does not define native presence or the hash population.
}

void OnFrameStart(std::uint32_t frame) {
    // Native damage can synchronously reenter actor work. The outer claim
    // transaction owns queue/sequence/binding state until its post-call census.
    if (g_hostClaimProcessing || g_populationConsumerActive) return;
    // This known native actor-update entry establishes diagnostic affinity;
    // broader creator/removal probes may execute before it or on other threads.
    spawncontroller::RegisterDiagnosticGameThread();
    populationauthority::Frame(); // role-independent drain, including no runtime/census
    nativehittrace::RegisterOwnerThread();
    g_hitTraceFrame = frame;
    if (!g_bridge.IsOpen()) { partynative::Observe(0, {}, 0xFF, false); DrainPendingSpawnTrace(false); return; }
    if (!RuntimeWriterLive()) {
        if (!g_writerRetired) {
            const auto writer = g_bridge.RuntimeWriter();
            if (g_log) g_log("[worldbridge] writer-expired pid=%lu generation=%u timeoutMs=5000; party retires; next load native",
                            static_cast<unsigned long>(writer.pid), g_bridge.SessionGeneration());
            g_activationOrderedGeneration = 0; g_orderedDeliverySerial = 0;
            g_role = Role::Off;
            RetireWorldSession();
            g_writerRetired = true;
        }
        partynative::Observe(0, {}, WORLD_SLOT_UNKNOWN, false);
        // Sole consumer drains, discarding stale work without changing producer headers.
        std::vector<std::uint8_t> discarded;
        for (unsigned i = 0; i < 256; ++i) {
            if (RuntimeWriterLive() || !g_bridge.ReceiveFromRuntime(discarded)) break;
            // Restart may publish its header/reset during this pop. Preserve
            // the first raced record, in FIFO order, for normal admission.
            if (RuntimeWriterLive()) { g_writerRecoveryPacket = std::move(discarded); break; }
        }
        DrainPendingSpawnTrace(false);
        return;
    }
    g_writerRetired = false;
    CheckActivationGeneration();
    const Role role = CurrentRole();
    bool becameHost = role == Role::Host && g_role != Role::Host;
    if (role != g_role) {
        SYNC_LOG("[enemysync] role %s", role == Role::Host     ? "host"
                                        : role == Role::Client ? "client"
                                                               : "off");
        g_role = role;
        RetireWorldSession();
    }
    // Unknown/disconnected sessions immediately release native exit authority.
    warp::SetClientAuthority(role == Role::Client && WorldSessionGeneration() != 0);
    // Every role drains local reset markers. Each marker takes effect before
    // the next packet, and no later frame-level reset can erase a new command.
    becameHost = ReceiveWorldPackets() || becameHost;
    CheckActivationGeneration();
    TickMirror(frame);
    if (partynative::Requested()) // VUH-1519, default off
        partynative::Observe(WorldSessionGeneration(), PartyRoster(), g_bridge.LocalSlot(), true);
    TickNativeResync(frame);
    if (g_role == Role::Client) {
        (void)EnsureClientClaimScope();
        if (frame % 120 == 0) LogClientClaim("seal", "frame-interval");
    }
    if (!WorldSessionGeneration()) { DrainPendingSpawnTrace(false); return; }
    if (g_role == Role::Host && !TickHostEventHold(frame, false)) {
        DrainPendingSpawnTrace(false);
        return;
    }
    // One world-ring consumer owns ordering: snapshot/deltas are consumed
    // before this tick, and host progress goes out before RoomTransition.
    progresssync::Tick(frame, g_role == Role::Host, g_role == Role::Client);

    const auto location = warp::ReadLocation();
    const auto transition = warp::TransitionSerial();
    const auto load = warp::LoadSerial();
    const bool newLoad = load != g_seenLoad;
    if (transition != g_seenTransition || newLoad || warp::TransitionPending()) {
        // Requests invalidate cached pointers before the native fade/teardown;
        // load callbacks do the same for initial loads and same-room reloads.
        g_inst = {};
        g_population.Clear();  // VUH-1788 C1: the native teardown owns every forced copy
        ClearActivation();
        ClearPendingHits();
        g_hostBeginPending = false;
        g_lastHashMs = 0;
    }
    g_seenTransition = transition;
    g_seenLoad = load;
    const bool keyChanged = location.worldId != g_inst.world || location.roomId != g_inst.room ||
                            location.battleProgram != g_inst.btl || location.door != g_inst.door ||
                            location.mapProgram != g_inst.map || location.eventProgram != g_inst.evt;
    if (!warp::TransitionPending() && (newLoad || g_inst.world == 0xFFFF) &&
        location.worldId != 0xFF && location.roomId != 0xFF) {
        g_inst = {};
        g_population.Clear();
        g_inst.world = location.worldId;
        g_inst.room = location.roomId;
        g_inst.door = location.door;
        g_inst.map = location.mapProgram;
        g_inst.btl = location.battleProgram;
        g_inst.evt = location.eventProgram;
        g_inst.live = true;
        if (g_role == Role::Host) QueueHostBeginInstance(location);
        SYNC_LOG("[enemysync] room instance %02X/%02X btl %u", g_inst.world, g_inst.room, g_inst.btl);
    } else if (keyChanged) {
        g_inst.live = false;  // transition requested: old room is on its way out
        ClearPendingHits();
        g_hostBeginPending = false;
    } else if (g_role == Role::Host && becameHost && g_inst.live) {
        // Hosting started mid-room (the runtime just connected): announce the
        // room after progress is ready; retain everything already spawned.
        QueueHostBeginInstance(location);
    }

    if (g_role == Role::Client) {
        if (!g_host.arrived && warp::HostTransitionArrived(g_host.epoch)) {
            g_host.arrived = true;
        }
        if (g_host.arrived && !g_host.ackSent && g_inst.live) {
            TransitionAck ack;
            ack.epoch = g_host.epoch;
            ack.worldId = location.worldId;
            ack.roomId = location.roomId;
            ack.arrived = true;
            // Retry on ring pressure; only log arrival once the ack is queued.
            if (Send(encode(ack))) {
                g_host.ackSent = true;
                if (g_log) g_log("[enemysync] client arrived epoch=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u",
                                 ack.epoch, location.worldId, location.roomId, location.door,
                                 location.mapProgram, location.battleProgram, location.eventProgram);
            }
        }
        if (!g_host.arrived) {
            // rev3 C1: a client not (or no longer) in the host's room still prunes forced entries.
            if (g_populationRequested && g_population.hasForced() && g_inst.live) {
                const auto pruneCensus = CaptureNativeCensus();
                if (CensusMatchesInstance(pruneCensus)) PopulationPrune(frame, pruneCensus);
            }
            DrainPendingSpawnTrace(false);
            return;
        }
        RequestActivation();
    }
    if (!g_inst.live) { DrainPendingSpawnTrace(false); return; }
    auto census = CaptureNativeCensus();
    if (!CensusMatchesInstance(census)) {
        InterruptCensus(census.state == CensusState::Complete ? "instance changed" : census.reason,
                        census.failedAt, census.nodeCount);
        DrainSpawnTrace(census);
        DrainLifecycleTrace(census);
        nativehittrace::Drain(g_log, g_hitTraceFrame);
        DrainResourceTrace();
        return;
    }
    if (g_censusInterrupted) {
        for (Spawn& spawn : g_inst.spawns) spawn.goneSinceMs = 0;
        g_censusInterrupted = false;
    }
    auto fresh = TrackSpawns(census);
    PopulationPrune(frame, census);  // VUH-1788 rev3 C1: any role, every complete census
    if (g_role == Role::Host) {
        // Track while progress or ring capacity delays the announcement. No
        // manifest/HP/hash may escape under the previous room's epoch.
        if (!HostBeginInstance()) { DrainPendingSpawnTrace(false); return; }
        if (!TickHostEventHold(frame, true)) { DrainPendingSpawnTrace(false); return; }
        // Refresh the frame-local proof before consuming queued native claims.
        if (g_recordBindingRequested) ResolveRecordPopulation(census,true);
        if (!ProcessHostHitClaims(census)) { DrainPendingSpawnTrace(false); return; }
        // Native claim application may have emitted/removed actors. Rebuild
        // from the latest census tracking, never reuse pre-call fresh indices.
        fresh.clear();
        for (std::size_t i = 0; i < g_inst.spawns.size(); ++i) {
            Spawn& s = g_inst.spawns[i];
            if (!s.announced) {
                if (std::find(fresh.begin(), fresh.end(), i) == fresh.end()) fresh.push_back(i);
            }
        }
        // A failed manifest stays pending. Publishing its actors' hash before
        // their bindings reach the peer would report a transport backlog as a
        // native population mismatch.
        if (!HostFrame(frame, fresh, census)) { DrainPendingSpawnTrace(false); return; }
        CapturePopulationBirths(census); // only the successfully announced, current incarnation
        PublishPopulationCut(census);
        PublishHostMotion(frame, census);
        FlushActivationResponses();
        TickHostPartyLayout();
    } else if (g_role == Role::Client) {
        for (const std::size_t i : fresh) {
            SYNC_LOG("[enemysync] client: local spawn %u frame %u objectId %u @%llX at (%.0f,%.0f,%.0f)",
                     g_inst.spawns[i].spawnIndex, frame, g_inst.spawns[i].objectId,
                     static_cast<unsigned long long>(g_inst.spawns[i].actor),
                     g_inst.spawns[i].spawnPos.x, g_inst.spawns[i].spawnPos.y, g_inst.spawns[i].spawnPos.z);
        }
        if (!ClientFrame(census)) { DrainPendingSpawnTrace(false); return; }
        TickPopulationRepair(frame, census);
        if(!ConsumePopulationDisposals(frame,census)) {
            DrainPendingSpawnTrace(false);return; // mutation/fault never reuses the pre-call population
        }
        PopulationTick(frame, census);  // VUH-1788 (default off)
    }
    PublishAppliedHash(frame, census);
    TickNativeResync(frame, true);
    if (g_role == Role::Client) {
        (void)ReleaseClientClaims(frame); // fresh readback after ClientFrame stores/replay reconciliation
        AckEventControl();
    }
    // Read-only diagnostics drain after bindings have been announced/applied.
    DrainPendingSpawnTrace(true);
}

bool MirrorRequested() noexcept { return g_mirrorRequested; }

bool MirrorTrace() noexcept { return g_mirrorTrace; }
bool MirrorLatencyTrace() noexcept { return g_latencyTraceBudget != 0; }
bool PopulationRequested() noexcept { return g_populationRequested; }
// C1: works in every role, so a forced copy left behind by a retired session is still removed.
bool PopulationStaleRequested(uintptr_t actor) noexcept {
    try {return PopulationStaleAuthorized(actor);}catch(...) {return false;}
}
bool PopulationForceRemove(uintptr_t actor) noexcept {
    if (!g_populationRequested) return false;
    // Certified stale natives use the explicit owner-frame consumer, never this bit-gated predicate.
    const auto* f = g_population.ForcedFor(ReadPopulationIdentity(actor));
    if (!f) return false;
    const bool client = g_role == Role::Client;
    const auto host = g_host.enemies.find(f->netId);
    const bool known = client && host != g_host.enemies.end();
    return enemypop::ForceRemove(*f, known, known && host->second.dead, client ? g_host.epoch : 0);
}
void PopulationForget(uintptr_t actor) noexcept {
    if (!g_populationRequested) return;
    // Ordinary native cull/forced-copy cleanup grants no stale-ticket dispatch authority.
    if (const auto* f = g_population.ForcedFor(ReadPopulationIdentity(actor))) g_population.Forget(f->id.actor);
}
std::uint32_t PopulationGeneration() noexcept { return g_population.generation(); }
bool PopulationForcedHold(uintptr_t actor) noexcept {
    if (!g_populationRequested || g_role != Role::Client) return false;
    const auto* f = g_population.ForcedFor(ReadPopulationIdentity(actor));
    if (!f) return false;
    const auto host = g_host.enemies.find(f->netId);
    const bool known = host != g_host.enemies.end();
    return enemypop::ForcedHold(*f, known, known && host->second.dead, g_host.epoch);
}
double MirrorCursor() noexcept { return g_mirror.cursor(); }

// A negative publication key is not authority and is not sufficient here.
// Shared native handler hooks can receive un-driven actors: rebuild the entire
// native membership/catalog proof before letting a known outside body cull.
bool RecordExclusionCurrent(uintptr_t actor,std::uint32_t id) {
    if (!g_recordWasAdmitted || !g_recordAuthorityRequested || g_recordAuthority.contains(actor) ||
        !WorldContextCurrent(g_recordContext) || !SafeNativeGameplay()) return false;
    const auto published=g_recordKeys.find(actor);
    if (published==g_recordKeys.end() || !recordbinding::ExclusionKnown(id,published->second)) return false;
    const auto key=published->second;
    const auto census=CaptureNativeCensus();
    std::vector<recordbinding::Local> local;
    RecordCatalog catalog;
    if (!CaptureRecordPopulation(census,local,catalog)) return false;
    const auto extra=std::find_if(local.begin(),local.end(),[&](const auto& l) {
        return l.roots.actor==actor && l.roots.objectId==id && l.key==key;
    });
    if (extra==local.end() || !recordbinding::ExclusionKnown(id,extra->key)) return false;
    std::vector<recordbinding::Host> host;
    for (const auto& l:local) {
        if (recordbinding::Admitted(l.roots.objectId,l.key)) {
            const auto proof=g_recordAuthority.find(l.roots.actor);
            if (proof==g_recordAuthority.end() || proof->second.roots!=l.roots ||
                !RecordAuthorityCurrent(l.roots.actor,l.roots.objectId,proof->second.terminal)) return false;
        }
        host.push_back({l.key,l.id,l.roots.objectId,false});
    }
    RoomTransition room;
    if (!ReadLocationChecked(room)) return false;
    const recordbinding::Scope now{WorldSessionGeneration(),CurrentRole()==Role::Client?g_host.epoch:g_epoch,
        warp::LoadSerial(),warp::TransitionSerial(),g_hitTraceFrame,g_bridge.ConnectionId(0),
        {room.worldId,room.roomId,room.door,room.mapProgram,room.battleProgram,room.eventProgram}};
    // The selector also rejects aliases between outside and original roots.
    const auto selected=recordbinding::SelectPopulation(now,local,host);
    return selected.complete && WorldContextCurrent(g_recordContext) && CensusMatchesInstance(census) && SafeNativeGameplay();
}

bool RecordMirrorAuthorityCurrent(uintptr_t actor) noexcept {
    if (!g_recordBindingRequested) return true;
    if (!spawncontroller::IsDiagnosticGameThread()) return false;
    const auto found=g_inst.byActor.find(actor);
    if (found==g_inst.byActor.end() || found->second>=g_inst.spawns.size()) return false;
    const auto id=g_inst.spawns[found->second].objectId;
    if (!RecordFamily(id)) return true;
    if (RecordAuthorityCurrent(actor,id)) return true;
    // False releases this callback to native behavior. A freshly proven outside
    // actor has no rights to retire; it must not revoke the original population.
    if (RecordExclusionCurrent(actor,id)) return false;
    WithdrawRecordBindings("native-mirror-boundary-held");return false;
}

enemymirror::Gate MirrorPose(uintptr_t actor, enemymirror::Pose& out) noexcept {
    using enemymirror::Gate;
    if (!g_mirrorRequested || g_role != Role::Client || !g_inst.live || !g_host.arrived || actor == 0) return Gate::None;
    if (!spawncontroller::IsDiagnosticGameThread()) return Gate::None;
    const auto found = g_inst.byActor.find(actor);
    if (found == g_inst.byActor.end() || found->second >= g_inst.spawns.size()) return Gate::None;
    const Spawn& s = g_inst.spawns[found->second];
    if (s.actor != actor || !s.present || s.killed || s.netId <= 0 || s.netId > 0xFFFF ||
        !RecordAuthorityCurrent(s.actor,s.objectId) || !enemymirror::FamilyAllowed(s.objectId)) return Gate::None;
    const auto host = g_host.enemies.find(static_cast<std::uint16_t>(s.netId));
    if (host == g_host.enemies.end() || host->second.dead || host->second.objectId != s.objectId) return Gate::None;
    // N5: the live status pointer must still be this binding's (closes a mid-frame reuse window).
    uintptr_t liveStatus = 0;
    std::int32_t hp = 0;
    if (!s.status || !ReadNative(actor + 0x5C0, liveStatus) || liveStatus != s.status || !ReadNative(s.status, hp) ||
        hp <= 0) return Gate::None;
    out.netId = static_cast<std::uint16_t>(s.netId);
    if (!g_mirror.PoseAt(out.netId, g_mirrorFrame, out) || out.objectId != s.objectId) return Gate::Bound;
    // S5: a stream pose far from where this spawn appeared is refused, never driven.
    const float dx = out.position.x - s.spawnPos.x, dy = out.position.y - s.spawnPos.y, dz = out.position.z - s.spawnPos.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (!(d2 <= enemymirror::kMaxFromSpawn * enemymirror::kMaxFromSpawn)) {
        // The first few are logged: a refusal releases the copy to local AI, so it must be visible.
        if (g_mirrorRefusedFar++ < 16 && g_log)
            g_log("[enemy-mirror] refused-far frame=%u netId=%u distance=%.0f limit=%.0f", g_mirrorFrame, out.netId,
                  std::isfinite(d2) ? std::sqrt(d2) : -1.0f, enemymirror::kMaxFromSpawn);
        return Gate::Bound;
    }
    return Gate::Drive;
}

bool DropLocalEnemyDamage(uintptr_t victim) {
    // Foreign-thread HP suppression is outside the supported native game-thread
    // claim path. Do not read its mutable role, bindings or Warp state here.
    if (!spawncontroller::IsDiagnosticGameThread()) return false;
    // Session headers can retire authority between native frame callbacks.
    // This predicate never mutates shared lease/claim state on an unknown thread.
    const auto generation = WorldSessionGeneration();
    return CurrentRole() == Role::Client && g_role == Role::Client &&
           generation != 0 && generation == g_activationGeneration &&
           generation == g_activationOrderedGeneration &&
           g_bridge.ConnectionId(g_bridge.LocalSlot()) != 0 && g_inst.live &&
           !warp::TransitionPending() && IsEnemy(victim);
}

bool RecordLocalPlayerEnemyHit(const LocalPlayerEnemyHit& hit) noexcept {
    // All claim/lease state belongs to the native game thread. In particular,
    // do not query ActivationContext (which can invalidate state) before this.
    if (!spawncontroller::IsDiagnosticGameThread()) return false;
    try {
        HitClaim claim;
        claim.attackId = hit.attackId;
        claim.damage = hit.damage;
        claim.attackerPosition = {hit.attackerPosition[0], hit.attackerPosition[1], hit.attackerPosition[2]};
        const auto reject = [&](const char* reason) {
            LogClaim("reject-local", claim, reason);
            return false;
        };
        if (!hit.victim || !hit.attacker || hit.damage <= 0 ||
            !std::isfinite(claim.attackerPosition.x) || !std::isfinite(claim.attackerPosition.y) ||
            !std::isfinite(claim.attackerPosition.z)) return reject("invalid local HP hit");
        // Arm/refine the generic scope before consulting prior replay state.
        // Held claims do not consume claim sequence or enter the world ring.
        if (!EnsureClientClaimScope()) {
            if (g_clientClaimHold.dropped == UINT64_MAX) g_clientClaimHold.poisoned = true;
            else ++g_clientClaimHold.dropped;
            LogClientClaim("drop", "full-manifest-readback-pending");
            return reject("generic client claim hold");
        }
        // This policy supports only this one living pack. Partial native actors
        // remain at their initial HP until the whole content set is reconciled.
        if (g_activationRecovery && !g_activationRecovery->reconciled)
            return reject("activation recovery awaiting full HP reconciliation");
        RoomTransition location;
        if (!ActivationContext(Role::Client, location)) return reject("client context unavailable");
        const auto generation = g_activationGeneration;
        const auto slot = g_bridge.LocalSlot();
        if (slot < 1 || slot > 2) return reject("local client slot unavailable");
        claim.attackerSlot = static_cast<SlotType>(slot);
        claim.epoch = location.epoch;
        claim.requesterConnectionId = g_bridge.ConnectionId(slot);
        if (!claim.requesterConnectionId) return reject("local connection unavailable");
        const auto census = CaptureNativeCensus();
        if (!CensusMatchesInstance(census)) return reject("local census unavailable");
        const auto found = g_inst.byActor.find(hit.victim);
        if (found == g_inst.byActor.end() || found->second >= g_inst.spawns.size())
            return reject("local binding unavailable");
        const Spawn& spawn = g_inst.spawns[found->second];
        if (!RecordAuthorityCurrent(spawn.actor,spawn.objectId)) return reject("record binding qualification has no claim authority");
        if (!spawn.present || spawn.killed || spawn.netId <= 0 || spawn.netId > UINT16_MAX)
            return reject("local binding not live/matched");
        claim.netId = static_cast<std::uint16_t>(spawn.netId);
        claim.objectId = spawn.objectId;
        const auto host = g_host.enemies.find(claim.netId);
        if (host == g_host.enemies.end() || host->second.objectId != claim.objectId ||
            host->second.battleProgram != g_inst.btl || host->second.dead)
            return reject("host manifest target unavailable/dead");
        const auto* sampled = FindNativeEnemy(census, spawn);
        NativeEnemy native;
        bool isEnemy = false;
        if (!sampled || !ReadNativeEnemy(hit.victim, native, isEnemy) || !isEnemy ||
            !SameNativeIdentity(*sampled, native) || native.hp <= 0 || native.maxHp <= 0)
            return reject("local target metadata changed/dead");
        RoomTransition finalLocation;
        if (!ActivationContext(Role::Client, finalLocation) ||
            !sameActivationLocation(location, finalLocation) || !CensusMatchesInstance(census) ||
            generation != g_bridge.SessionGeneration() ||
            g_bridge.ConnectionId(slot) != claim.requesterConnectionId)
            return reject("local context changed during capture");
        if (g_localClaimConnection != claim.requesterConnectionId) {
            g_localClaimConnection = claim.requesterConnectionId;
            g_localClaimSequence = 0;
        }
        if (g_localClaimSequence == std::numeric_limits<std::uint32_t>::max())
            return reject("local sequence exhausted");
        if (!EnsureClientClaimScope()) return reject("claim scope retired before publication");
        claim.seq = ++g_localClaimSequence;
        // Encode/send this detection's immutable identity immediately. A full
        // ring drops the claim; it can never be relabeled after a room change.
        if (!Send(encode(claim))) { LogClientClaim("enqueue-failed", "world-ring-or-context", 0, &claim); return reject("world ring full"); }
        if (g_clientClaimHold.submitted == UINT64_MAX) g_clientClaimHold.poisoned = true;
        else ++g_clientClaimHold.submitted;
        LogClientClaim("submitted", "world-ring-enqueued", 0, &claim);
        if (g_log) g_log("[enemysync] client hit claim connection=%llu seq=%u epoch=%u netId=%u objectId=%u attackId=%u damage=%d",
                         static_cast<unsigned long long>(claim.requesterConnectionId), claim.seq, claim.epoch,
                         claim.netId, claim.objectId, claim.attackId, claim.damage);
        return true;
    } catch (...) {
        return false; // diagnostics/network allocation can never escape the hit hook
    }
}

nativehittrace::Context CaptureNativeHitContext() noexcept {
    using namespace nativehittrace;
    Context out {};
    // Mutable room/session state has one owner. Foreign callbacks retain an
    // unavailable context and never inspect that owner's native roots/maps.
    if (!spawncontroller::IsDiagnosticGameThread() || !nativehittrace::IsOwnerThread()) return out;
    out.frame = g_hitTraceFrame;
    const Role role = CurrentRole();
    out.role = static_cast<std::uint8_t>(role);
    out.slot = g_bridge.LocalSlot();
    out.generation = g_bridge.SessionGeneration();
    out.connectionId = g_bridge.ConnectionId(out.slot);
    out.hostConnectionId = g_bridge.ConnectionId(0);
    out.transitionSerial = warp::TransitionSerial();
    out.loadSerial = warp::LoadSerial();
    if ((role == Role::Host && out.slot == 0) ||
        (role == Role::Client && (out.slot == 1 || out.slot == 2))) out.readMask |= ContextRole;
    if (g_bridge.IsOpen() && out.connectionId != 0 && out.hostConnectionId != 0)
        out.readMask |= ContextBridge;
    if (out.generation != 0 && out.generation == g_activationGeneration &&
        out.generation == g_activationOrderedGeneration && role == g_role &&
        g_orderedDeliverySerial && g_bridge.DeliverySerial() == g_orderedDeliverySerial)
        out.readMask |= ContextSession;
    if (out.loadSerial != 0 && out.loadSerial == g_seenLoad &&
        out.transitionSerial == g_seenTransition && SafeNativeGameplay())
        out.readMask |= ContextNative;
    RoomTransition location {};
    const bool locationRead = ReadLocationChecked(location);
    if (locationRead) {
        out.location[0] = location.worldId; out.location[1] = location.roomId;
        out.location[2] = location.door; out.location[3] = location.mapProgram;
        out.location[4] = location.battleProgram; out.location[5] = location.eventProgram;
        if (g_inst.live && location.worldId == g_inst.world && location.roomId == g_inst.room &&
            location.door == g_inst.door && location.mapProgram == g_inst.map &&
            location.battleProgram == g_inst.btl && location.eventProgram == g_inst.evt)
            out.readMask |= ContextRoom;
    }
    if (role == Role::Host) {
        out.epoch = g_epoch;
        if (g_epoch != 0 && !g_hostBeginPending && progresssync::HostReady()) out.readMask |= ContextPhase;
    } else if (role == Role::Client) {
        out.epoch = g_host.epoch;
        location.epoch = g_host.epoch;
        if (g_host.epoch != 0 && g_host.arrived && g_host.ackSent &&
            locationRead && warp::MatchesArrivedHostTransition(location)) out.readMask |= ContextPhase;
    }
    RoomTransition repeated {};
    if (locationRead && ReadLocationChecked(repeated) && SameLocation(location, repeated) &&
        g_bridge.IsOpen() && g_bridge.SessionGeneration() == out.generation &&
        g_bridge.LocalSlot() == out.slot && g_bridge.ConnectionId(out.slot) == out.connectionId &&
        g_bridge.ConnectionId(0) == out.hostConnectionId && CurrentRole() == role &&
        g_orderedDeliverySerial && g_bridge.DeliverySerial() == g_orderedDeliverySerial &&
        warp::TransitionSerial() == out.transitionSerial && warp::LoadSerial() == out.loadSerial)
        out.readMask |= ContextRepeated;
    out.available = out.readMask == ContextComplete;
    return out;
}

bool CaptureDamageContext(nativehittrace::Context& context,
                          std::uint64_t (&roster)[3]) noexcept {
    context = CaptureNativeHitContext();
    std::fill_n(roster, 3, std::uint64_t {0});
    if (!context.available) return false;
    const auto before = context;
    std::uint64_t captured[3] {};
    for (std::uint8_t slot = 0; slot < 3; ++slot) captured[slot] = g_bridge.ConnectionId(slot);
    const auto after = CaptureNativeHitContext();
    const bool same = after.available && before.frame == after.frame &&
        before.generation == after.generation && before.epoch == after.epoch &&
        before.transitionSerial == after.transitionSerial && before.loadSerial == after.loadSerial &&
        before.connectionId == after.connectionId && before.hostConnectionId == after.hostConnectionId &&
        before.role == after.role && before.slot == after.slot &&
        std::memcmp(before.location, after.location, sizeof(before.location)) == 0 &&
        captured[before.slot] == before.connectionId && captured[0] == before.hostConnectionId;
    context = after;
    if (!same) { context.available = false; return false; }
    for (std::uint8_t slot = 0; slot < 3; ++slot) {
        if (g_bridge.ConnectionId(slot) != captured[slot]) { context.available = false; return false; }
    }
    std::copy_n(captured, 3, roster);
    return true;
}

PuppetAuthority CapturePuppetAuthority() noexcept {
    PuppetAuthority out {};
    if (!spawncontroller::IsDiagnosticGameThread() || !nativehittrace::IsOwnerThread() ||
        !g_bridge.IsOpen()) return out;
    if (!RuntimeWriterLive()) return out;
    const auto mode = g_bridge.GetPuppetAuthorityMode();
    const auto generation = g_bridge.SessionGeneration();
    const auto localSlot = g_bridge.LocalSlot();
    std::array<std::uint64_t, 3> ids {};
    for (std::uint8_t slot = 0; slot < 3; ++slot) ids[slot] = g_bridge.ConnectionId(slot);
    if (g_bridge.GetPuppetAuthorityMode() != mode ||
        g_bridge.SessionGeneration() != generation || g_bridge.LocalSlot() != localSlot) return out;
    for (std::uint8_t slot = 0; slot < 3; ++slot)
        if (g_bridge.ConnectionId(slot) != ids[slot]) return out;
    if (g_bridge.GetPuppetAuthorityMode() != mode ||
        g_bridge.SessionGeneration() != generation || g_bridge.LocalSlot() != localSlot) return out;
    if (mode == PuppetAuthorityMode::Off) {
        // Only the explicit, never-armed standalone bridge may grant Off.
        if (generation != 0 || localSlot != WORLD_SLOT_UNKNOWN ||
            ids[0] || ids[1] || ids[2] || CurrentRole() != Role::Off) return out;
    } else if (mode == PuppetAuthorityMode::Network) {
        if (generation == 0 || generation != g_activationGeneration ||
            generation != g_activationOrderedGeneration || !g_orderedDeliverySerial ||
            g_bridge.DeliverySerial() != g_orderedDeliverySerial || localSlot >= 3 ||
            ids[0] == 0 || ids[localSlot] == 0) return out;
    } else {
        return out;
    }
    out.mode = mode;
    out.generation = generation;
    out.localSlot = localSlot;
    out.connectionIds = ids;
    if (!RuntimeWriterLive()) return {};
    return out;
}

bool CurrentPuppetActor(uintptr_t actor, std::uint32_t transition,
                        std::uint32_t load) noexcept {
    if (!actor || !spawncontroller::IsDiagnosticGameThread() || !nativehittrace::IsOwnerThread())
        return false;
    try {
        const auto census = CaptureNativeCensus(actor);
        return census.state == CensusState::Complete && census.watchedActorPresent &&
            census.transition == transition && census.load == load &&
            warp::TransitionSerial() == transition && warp::LoadSerial() == load &&
            SafeNativeGameplay();
    } catch (...) {
        return false;
    }
}

bool NetStats(std::uint32_t& rttMs, std::uint32_t& lossPermille) {
    if (!g_bridge.IsOpen()) return false;
    const auto stats = g_bridge.NetStats();
    if (stats.rttMs == WORLD_NET_UNKNOWN) return false;
    rttMs = stats.rttMs;
    lossPermille = stats.lossPermille;
    return true;
}

bool HasClientAuthority() { return CurrentRole() == Role::Client; }

bool PartyLoadAdmitted(std::uint32_t generation) noexcept {
    // Loading-thread safe: only immutable view and atomic header/lease reads.
    // The plan carries the generation under which its ordered packet was admitted.
    return generation != 0 && RuntimeWriterLive(generation);
}

std::uint32_t WorldSessionGeneration() noexcept {
    if (!spawncontroller::IsDiagnosticGameThread() || !g_bridge.IsOpen()) return 0;
    const auto generation = g_bridge.SessionGeneration();
    const auto role = CurrentRole();
    return generation != 0 && generation == g_activationGeneration &&
        g_orderedDeliverySerial != 0 && g_bridge.DeliverySerial() == g_orderedDeliverySerial &&
        generation == g_activationOrderedGeneration && role != Role::Off && role == g_role &&
        CurrentRole() == role && g_bridge.SessionGeneration() == generation &&
        g_bridge.DeliverySerial() == g_orderedDeliverySerial ? generation : 0;
}

bool WorldContextCurrent(const ProducerWorldContext& context) noexcept {
    return context.generation != 0 && WorldSessionGeneration() == context.generation &&
        context.deliverySerial != 0 && g_bridge.DeliverySerial() == context.deliverySerial &&
        (CurrentRole() != Role::Host || context.hostSourceSerial != 0);
}

bool CaptureWorldContext(ProducerWorldContext& context) {
    context = {};
    const auto generation = WorldSessionGeneration();
    const auto delivery = g_bridge.DeliverySerial();
    if (!generation || !delivery) return false;
    const auto role = CurrentRole();
    std::uint64_t source = 0;
    if (role == Role::Host) {
        if (g_worldSourceSerial == UINT64_MAX) {
            if (!g_worldSourceExhaustionLogged && g_log)
                g_log("[enemysync] world source sequence exhausted; publications suspended");
            g_worldSourceExhaustionLogged = true;
            return false;
        }
        source = ++g_worldSourceSerial;
    }
    ProducerWorldContext captured {generation, delivery, source};
    if (!WorldContextCurrent(captured) || CurrentRole() != role) return false;
    context = captured;
    return true;
}

bool SendCapturedWorld(const std::vector<std::uint8_t>& packet, const ProducerWorldContext& context) {
    if (!WorldContextCurrent(context)) return false;
    if (!g_bridge.SendToRuntime(packet, context)) {
        if (!packet.empty()) {
            const auto type = static_cast<PacketType>(packet.front());
            if (type == PacketType::RoomTransition || type == PacketType::EventHold ||
                type == PacketType::EnemyManifest || type == PacketType::EnemyHp ||
                type == PacketType::EnemyDeath || type == PacketType::ProgressUpdate ||
                type == PacketType::NativeResyncSnapshot) ++g_worldSendFailures;
        }
        SYNC_LOG("[enemysync] world ring full, captured packet not enqueued");
        return false;
    }
    return true;
}

std::uint8_t ActivationRole() {
    CheckActivationGeneration();
    return static_cast<std::uint8_t>(CurrentRole());
}

void SetSpawnPickLive(bool live) { g_spawnPickLive = live; }

void NoteSpawnPickOp(bool shared, std::uint64_t salt) {
    spawnpick::NoteOp(g_spawnPickOutcome, warp::TransitionSerial(), shared, salt);
}

bool SpawnPickContext(SpawnPickInputs& out, const char*& reason) {
    out = {};
    if (!WorldSessionGeneration()) { reason = "no-session"; return false; }
    out.salt = g_bridge.SpawnPickSalt();
    if (!out.salt) { reason = "no-salt"; return false; }
    const auto location = warp::ReadLocation();
    out.world = location.worldId; out.room = location.roomId; out.map = location.mapProgram;
    out.btl = location.battleProgram; out.evt = location.eventProgram;
    const auto role = CurrentRole();
    if (role == Role::Host) {
        // The epoch this load's RoomTransition will carry: QueueHostBeginInstance takes g_epoch + 1 at load
        // completion, and HostBeginInstance cannot commit during the load (SafeNativeGameplay is false).
        out.role = 1;
        out.epoch = g_epoch + 1;
        if (out.epoch == 0) out.epoch = 1;
        return true;
    }
    if (role == Role::Client) {
        out.role = 2;
        out.epoch = warp::HostIssuedLoadEpoch(location);
        if (!out.epoch) { reason = "not-host-issued-load"; return false; }
        std::uint8_t shared = 0;
        std::uint32_t tag = 0;
        warp::HostTargetSpawnPick(shared, tag);
        const spawnpick::Trailer trailer {shared, tag, g_spawnPickUnknownEpoch == 0 || g_spawnPickUnknownEpoch != out.epoch};
        switch (spawnpick::ClientGate(trailer, out.salt)) {
        case spawnpick::Gate::HostNative: reason = "host-native"; return false;
        case spawnpick::Gate::SaltTag: reason = "salt-tag"; return false;
        case spawnpick::Gate::ResyncUnknown: reason = "resync-unknown"; return false;
        case spawnpick::Gate::Shared: break;
        }
        return true;
    }
    reason = "role-off";
    return false;
}

void CaptureHostActivation(const float* position4) {
    // The hook provides a checked, aligned local copy of its actual native
    // argument. Never read an avatar bridge or answer from a cached old point.
    RoomTransition location;
    if (!position4) return;
    std::array<float, 4> point;
    std::copy_n(position4, point.size(), point.begin());
    if (!std::all_of(point.begin(), point.end(), [](float value) { return std::isfinite(value); })) return;
    // Retain native birth provenance while the room announcement is waiting for
    // progress/ring capacity. This does not publish a lease or disposal authority.
    if(CurrentRole()==Role::Host && WorldSessionGeneration()!=0 && SafeNativeGameplay() && ReadLocationChecked(location))
        ObservePopulationActivation(point);
    if(!ActivationContext(Role::Host,location) || g_activationSourceSeq==std::numeric_limits<std::uint64_t>::max())return;
    const auto sequence = ++g_activationSourceSeq;
    const auto now = GetTickCount64();
    for (auto& pending : g_activationChallenges) {
        if (!pending || sequence <= pending->captureFloor || pending->generation != g_activationGeneration ||
            pending->transition != g_seenTransition || pending->load != g_seenLoad ||
            !sameActivationLocation(pending->request.location, location) || now < pending->receivedMs ||
            now - pending->receivedMs >= ActivationLease::kLeaseMs) continue;
        HostActivationPoint response;
        response.request = pending->request;
        response.sourceSeq = sequence;
        response.position = point;
        if (!CaptureWorldContext(pending->responseContext)) continue;
        pending->response = response;
    }
}

bool CopyHostActivation(float* position4, uintptr_t controller, std::uint64_t updateSequence) {
    RoomTransition location;
    if (!position4 || !ActivationContext(Role::Client, location) ||
        !g_activationLease.Copy(GetTickCount64(), position4) ||
        g_bridge.SessionGeneration() != g_activationGeneration) return false;
    if (CopyPopulationActivation(position4,controller))return true;
    if (!g_activationRecovery || g_activationRecovery->controller != controller ||
        !g_nativeResync || g_nativeResync->finished) return true;
    auto& recovery = *g_activationRecovery;
    if (!PackPreparationActive() || g_survivingPack.Terminal() ||
        !WorldContextCurrent(g_nativeResync->context) || GetTickCount64() >= g_resyncDeadline ||
        recovery.load != warp::LoadSerial() || recovery.transition != warp::TransitionSerial() ||
        !spawncontroller::KnownMutationTicketCurrent(recovery.mutation) ||
        !ActivationDefinitionCurrent(recovery) || !updateSequence ||
        (recovery.lastSequence && updateSequence < recovery.lastSequence)) {
        recovery.phase = ActivationRecoveryPhase::Failed;
        return false;
    }
    if (recovery.phase == ActivationRecoveryPhase::Failed) return false;
    std::uint64_t completed = 0;
    if (spawncontroller::CopyLastClientOriginalReturn(controller, completed) &&
        completed != recovery.lastSequence) {
        if (recovery.lastSequence && completed < recovery.lastSequence) {
            recovery.phase = ActivationRecoveryPhase::Failed; return false;
        }
        recovery.lastSequence = completed;
        if (recovery.phase == ActivationRecoveryPhase::Historical) {
            ++recovery.historicalTicks;
        } else if (recovery.liveTicks < kActivationLiveHoldTicks) ++recovery.liveTicks;
    }
    if (recovery.occupancyHold) return false;
    if (recovery.phase == ActivationRecoveryPhase::Historical) {
        if (recovery.historicalTicks >= kActivationRecoveryTicks || !g_nativeResync->snapshot.activationReplay) {
            recovery.phase = ActivationRecoveryPhase::Failed; return false;
        }
        const auto& historical = g_nativeResync->snapshot.activationReplay->point;
        std::copy(historical.begin(), historical.end(), position4);
    }
    return true;
}

void Shutdown() {
    g_hashDiagnosticStream.seal(g_hashDiagnosticSink,"native-hash-publication","shutdown");
    g_hashDiagnosticSink = {};
    InvalidateClientClaims("shutdown"); LogClientClaim("seal", "shutdown");
    g_activationRecovery.reset();
    if (g_survivingPack.Intent()) g_survivingPack.Cancel();
    ClearActivation();
    ClearPendingHits();
    SealPopulationDisposals("shutdown",g_mirrorFrame);
    g_takeDamage = nullptr;
    g_populationDeathHelperVerified=false;
    g_resyncPlan.reset(); g_nativeResync.reset(); g_resyncOutput.clear();
    g_resyncWriteFence = ResyncWriteFence::None; g_resyncRecordAuthority.reset();
    g_activationOrderedGeneration = 0;
    progresssync::Reset();
    g_hostBeginPending = false;
    g_lastHashMs = 0;
    g_bridge.Close();
}

bool CaptureDownedScope(DownedScope& out) noexcept {
    out = {};
    try {
        const auto role = CurrentRole();
        const auto generation = WorldSessionGeneration();
        const auto delivery = g_bridge.DeliverySerial();
        const auto local = g_bridge.LocalSlot();
        if (role == Role::Off || !generation || !delivery || local >= 3) return false;
        DownedScope s {};
        s.context = {generation, delivery, 0};
        s.localSlot = local;
        for (std::uint8_t slot = 0; slot < 3; ++slot) s.connections[slot] = g_bridge.ConnectionId(slot);
        RoomTransition room;
        if (!s.connections[local] || !ActivationContext(role, room) || !room.epoch) return false;
        if (WorldSessionGeneration() != generation || g_bridge.DeliverySerial() != delivery ||
            g_bridge.LocalSlot() != local) return false;
        s.epoch = room.epoch; s.worldId = room.worldId; s.roomId = room.roomId; s.door = room.door;
        s.mapProgram = room.mapProgram; s.battleProgram = room.battleProgram; s.eventProgram = room.eventProgram;
        out = s;
        return true;
    } catch (...) {
        out = {};
        return false;
    }
}

void NoteLocalDownedState(const LocalDownedState& state) noexcept { g_localDowned = state; }
void SetReviveApply(ReviveApplyFn apply) noexcept { g_reviveApply = apply; }
ReviveStats GetReviveStats() noexcept { return g_reviveStats; }

bool SendReviveRequest(std::uint8_t targetSlot, std::uint64_t targetEpisode, std::uint64_t& seqOut) {
    seqOut = 0;
    DownedScope s;
    if (!targetEpisode || !CaptureDownedScope(s) || targetSlot >= 3 || targetSlot == s.localSlot ||
        !s.connections[targetSlot] || g_reviveRequestSeq == UINT64_MAX) return false;
    ProducerWorldContext context;
    if (!CaptureWorldContext(context) || context.generation != s.context.generation ||
        context.deliverySerial != s.context.deliverySerial) return false;
    ReviveRequest request;
    request.location = {s.epoch, s.worldId, s.roomId, s.door, s.mapProgram, s.battleProgram, s.eventProgram};
    request.seq = ++g_reviveRequestSeq; // consumed even if the send fails
    request.requesterConnectionId = s.connections[s.localSlot];
    request.targetConnectionId = s.connections[targetSlot];
    request.targetEpisode = targetEpisode;
    request.requesterSlot = s.localSlot;
    request.targetSlot = targetSlot;
    seqOut = request.seq;
    return SendCapturedWorld(encode(request), context);
}

namespace {
// VUH-1515: RemoteHit from the host to us (the owner of the hit clone). Source admission is the
// host branch's (host connection, its delivery serial, a host source serial past the resync cut);
// then the message must name us, the host connection, our admitted host epoch and room, and a
// sequence above the last admitted one from that host connection.
void AdmitRemoteHit(const WorldScope& scope, const std::uint8_t* payload, std::size_t size) {
    ++g_remoteHitStats.seen;
    const auto local = g_bridge.LocalSlot();
    const char* why = nullptr;  // first failing admission rule (logged)
    if (!(CurrentRole() == Role::Client && local < 3 && local != 0)) why = "role-or-slot";
    else if (!(scope.sourceConnectionId && scope.sourceConnectionId == g_bridge.ConnectionId(0) && scope.hostSourceSerial &&
               scope.sourceDeliverySerial == g_bridge.PeerDeliverySerial(0) && scope.hostSourceSerial > g_resyncAppliedCut)) why = "source";
    RemoteHit hit;
    if (!why) {
        ByteReader reader(payload, size);
        read(reader, hit);
        if (!reader.atEnd()) why = "payload";
    }
    if (!why && !(hit.targetSlot == local && hit.targetConnectionId == g_bridge.ConnectionId(local) &&
                  hit.hostConnectionId == scope.sourceConnectionId && hit.seq != 0 && hit.damage > 0 && hit.damage <= 9999)) why = "fields";
    if (!why && !(g_host.arrived && g_host.epoch != 0)) why = "not-arrived";
    if (!why && hit.location.epoch != g_host.epoch) why = "epoch";
    RoomTransition room;
    if (!why && !(ActivationContext(Role::Client, room) && room.epoch == hit.location.epoch &&
                  room.worldId == hit.location.worldId && room.roomId == hit.location.roomId)) why = "room";
    if (!why && hit.hostConnectionId != g_remoteHitHost) { g_remoteHitHost = hit.hostConnectionId; g_remoteHitLastSeq = 0; }
    if (!why && hit.seq <= g_remoteHitLastSeq) why = "sequence";
    if (!why) g_remoteHitLastSeq = hit.seq;  // consumed even if the native checks refuse it
    if (!why && !g_remoteHitApply) why = "apply-unset";
    if (why) {
        ++g_remoteHitStats.admissionRefused;
        if (g_log && g_remoteHitLogs < 32) {
            ++g_remoteHitLogs;
            g_log("[enemy-target] remote-hit admission refused reason=%s local=%u seq=%llu epoch=%u/%u apply=%u", why,
                  static_cast<unsigned>(local), static_cast<unsigned long long>(hit.seq), hit.location.epoch, g_host.epoch,
                  g_remoteHitApply ? 1u : 0u);
        }
        return;
    }
    const int result = g_remoteHitApply(hit.damage, hit.attackId, hit.objectId, hit.seq);
    if (result == 1) ++g_remoteHitStats.applied; else ++g_remoteHitStats.nativeRefused;
}

// VUH-1515: TargetAuthority from the host (same host-source admission as RemoteHit). A refused or
// absent advertisement simply leaves the previous one to age out: the owner then never cancels.
void AdmitTargetAuthority(const WorldScope& scope, const std::uint8_t* payload, std::size_t size) {
    const auto local = g_bridge.LocalSlot();
    const char* why = nullptr;  // first failing admission rule (logged)
    if (!(CurrentRole() == Role::Client && local < 3 && local != 0)) why = "role-or-slot";
    else if (!(scope.sourceConnectionId && scope.sourceConnectionId == g_bridge.ConnectionId(0) && scope.hostSourceSerial &&
               scope.sourceDeliverySerial == g_bridge.PeerDeliverySerial(0) && scope.hostSourceSerial > g_resyncAppliedCut)) why = "source";
    TargetAuthority a;
    if (!why) {
        ByteReader reader(payload, size);
        read(reader, a);
        if (!reader.atEnd()) why = "payload";
    }
    if (!why && !(a.hostConnectionId == scope.sourceConnectionId && a.seq != 0 && (a.slotMask & ~0x06u) == 0 &&
                  (a.familyMask & ~1u) == 0 && a.mode <= 1)) why = "fields";
    // Expected right after a load: advertisements that arrive before our own room arrival is admitted.
    if (!why && !(g_host.arrived && g_host.epoch != 0)) why = "not-arrived";
    if (!why && a.location.epoch != g_host.epoch) why = "epoch";
    RoomTransition room;
    if (!why && !(ActivationContext(Role::Client, room) && room.epoch == a.location.epoch &&
                  room.worldId == a.location.worldId && room.roomId == a.location.roomId)) why = "room";
    if (!why && a.hostConnectionId != g_authorityFloorHost) { g_authorityFloorHost = a.hostConnectionId; g_authorityFloor = 0; }
    if (!why && a.seq <= g_authorityFloor) why = "sequence";
    if (why) {
        if (g_log && g_authorityLogs < 32) {
            ++g_authorityLogs;
            g_log("[enemy-target] authority admission refused reason=%s local=%u seq=%llu epoch=%u/%u arrived=%u", why,
                  static_cast<unsigned>(local), static_cast<unsigned long long>(a.seq), a.location.epoch, g_host.epoch,
                  g_host.arrived ? 1u : 0u);
        }
        return;
    }
    g_authorityFloor = a.seq;
    g_authority = {GetTickCount64(), a.hostConnectionId, a.seq, a.location.epoch, a.familyMask, a.location.worldId,
                   a.location.roomId, a.slotMask, a.mode};
}
} // namespace

std::uint16_t HostEnemyNetId(uintptr_t actor, std::uint32_t objectId) noexcept {
    if (CurrentRole() != Role::Host || g_epoch == 0 || g_hostBeginPending || !actor) return 0;
    const auto found = g_inst.byActor.find(actor);
    if (found == g_inst.byActor.end() || found->second >= g_inst.spawns.size()) return 0;
    const auto& spawn = g_inst.spawns[found->second];
    if (!spawn.present || !spawn.announced || spawn.objectId != objectId || spawn.spawnIndex + 1 > 0xFFFF) return 0;
    return static_cast<std::uint16_t>(spawn.spawnIndex + 1);
}

bool SendRemoteHit(std::uint8_t targetSlot, std::uint16_t netId, std::uint32_t objectId, std::uint32_t attackId,
                   std::int32_t damage, std::uint64_t& seqOut) {
    seqOut = 0;
    DownedScope s;
    if (CurrentRole() != Role::Host || !netId || damage <= 0 || damage > 9999 || !CaptureDownedScope(s) ||
        targetSlot == 0 || targetSlot >= 3 || targetSlot == s.localSlot || !s.connections[targetSlot] ||
        g_remoteHitSeq == UINT64_MAX) { ++g_remoteHitStats.sendFailed; return false; }
    ProducerWorldContext context;
    if (!CaptureWorldContext(context) || context.generation != s.context.generation ||
        context.deliverySerial != s.context.deliverySerial) { ++g_remoteHitStats.sendFailed; return false; }
    RemoteHit hit;
    hit.location = {s.epoch, s.worldId, s.roomId, s.door, s.mapProgram, s.battleProgram, s.eventProgram};
    hit.seq = ++g_remoteHitSeq;  // consumed even if the send fails
    hit.hostConnectionId = s.connections[s.localSlot];
    hit.targetConnectionId = s.connections[targetSlot];
    hit.targetSlot = targetSlot;
    hit.netId = netId;
    hit.objectId = objectId;
    hit.attackId = attackId;
    hit.damage = damage;
    seqOut = hit.seq;
    const bool sent = SendCapturedWorld(encode(hit), context);
    if (sent) ++g_remoteHitStats.sent; else ++g_remoteHitStats.sendFailed;
    return sent;
}

bool SendTargetAuthority(std::uint8_t slotMask, std::uint32_t familyMask, std::uint8_t mode, std::uint64_t& seqOut) {
    seqOut = 0;
    DownedScope s;
    if (CurrentRole() != Role::Host || (slotMask & ~0x06u) != 0 || (familyMask & ~1u) != 0 || mode > 1 ||
        !CaptureDownedScope(s) || s.localSlot != 0 || !s.connections[0] || g_authoritySeq == UINT64_MAX) return false;
    ProducerWorldContext context;
    if (!CaptureWorldContext(context) || context.generation != s.context.generation ||
        context.deliverySerial != s.context.deliverySerial) return false;
    TargetAuthority a;
    a.location = {s.epoch, s.worldId, s.roomId, s.door, s.mapProgram, s.battleProgram, s.eventProgram};
    a.seq = ++g_authoritySeq;  // consumed even if the send fails
    a.hostConnectionId = s.connections[0];
    a.slotMask = slotMask;
    a.familyMask = familyMask;
    a.mode = mode;
    seqOut = a.seq;
    return SendCapturedWorld(encode(a), context);
}

TargetAuthorityView CurrentTargetAuthority(std::uint64_t maxAgeMs) noexcept {
    TargetAuthorityView v;
    try {
        v.localSlot = g_bridge.LocalSlot();
        const auto& a = g_authority;
        RoomTransition room;
        const auto now = GetTickCount64();
        v.held = CurrentRole() == Role::Client && a.rxMs != 0 && now >= a.rxMs && now - a.rxMs <= maxAgeMs &&
            a.hostConnection != 0 && a.hostConnection == g_bridge.ConnectionId(0) && g_host.arrived &&
            a.epoch == g_host.epoch && ActivationContext(Role::Client, room) && room.epoch == a.epoch &&
            room.worldId == a.worldId && room.roomId == a.roomId;
        if (v.held) { v.slotMask = a.slotMask; v.mode = a.mode; v.familyMask = a.familyMask; v.seq = a.seq; v.rxMs = a.rxMs; }
    } catch (...) {
        v = {};
    }
    return v;
}

void SetRemoteHitApply(RemoteHitApplyFn apply) noexcept { g_remoteHitApply = apply; }
RemoteHitStats GetRemoteHitStats() noexcept { return g_remoteHitStats; }

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
