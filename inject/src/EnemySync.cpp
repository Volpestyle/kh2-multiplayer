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
#include "NativeSpawnController.hpp"
#include "NativeResourceTrace.hpp"
#include "NativeLifecycleTrace.hpp"
#include "ProgressSync.hpp"
#include "Warp.hpp"

#include "kh2coop/AppliedStateHash.hpp"
#include "kh2coop/ActivationLease.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/NativeRecordContent.hpp"
#include "kh2coop/SurvivingPackPreparation.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/WorldBridge.hpp"

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
uintptr_t g_exeBase = 0;
StatDeltaFn g_applyStatDelta = nullptr;
TakeDamageFn g_takeDamage = nullptr;
Role g_role = Role::Off;
WorldBridge g_bridge;

// One enemy as this machine saw it spawn in the current room instance.
struct Spawn {
    std::uint16_t spawnIndex = 0;
    std::uint32_t objectId = 0;
    std::uint32_t objectType = 0;
    uintptr_t actor = 0;
    uintptr_t objentry = 0, status = 0; // metadata at this binding's creation
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
};

struct Instance {
    std::uint16_t world = 0xFFFF, room = 0xFFFF, btl = 0;
    std::uint16_t door = 0, map = 0, evt = 0;
    bool live = false;        // tracking spawns (not mid-transition)
    std::vector<Spawn> spawns;
    std::unordered_map<uintptr_t, std::size_t> byActor;  // every address seen -> latest spawn there
};

Instance g_inst;
std::uint32_t g_seenTransition = 0;
std::uint32_t g_seenLoad = 0;
std::uint32_t g_epoch = 0;          // host: current epoch
// Source ordering belongs to this DLL lifetime, not a room, manifest or
// transport generation. Failed enqueue attempts consume their sample number.
std::uint64_t g_hpSourceSequence = 0;
bool g_hpSourceExhaustionLogged = false;
// Independent of HostRoom so replacement manifests and room changes cannot
// admit older HP. Only an actual bridge generation change retires this floor.
std::uint64_t g_hostHpSequence = 0;
bool g_manifestSent = false;        // host: first manifest of the epoch went out
bool g_hostBeginPending = false;
RoomTransition g_pendingHostRoom {};
ProducerWorldContext g_pendingHostRoomContext {};
std::uint64_t g_lastHashMs = 0;
bool g_censusInterrupted = false;
std::uint64_t g_lastCensusErrorMs = 0;
std::uint64_t g_traceLastDropped = 0, g_traceLastFaults = 0;
std::uint64_t g_traceDrained = 0, g_traceLastSummaryMs = 0;
std::uint64_t g_lifecycleDrained = 0, g_lifecycleLastSummaryMs = 0;
std::uint32_t g_hitTraceFrame = 0;

// Client view of the host.
struct HostEnemy {
    std::uint16_t spawnIndex = 0;
    std::uint32_t objectId = 0;
    Vec3 spawnPos {};
    std::uint16_t battleProgram = 0;
    std::int32_t hp = -1;
    bool dead = false;
};
struct HostRoom {
    std::uint32_t epoch = 0;
    std::uint16_t world = 0xFFFF, room = 0xFFFF, btl = 0;
    bool arrived = false;
    bool ackSent = false;
    std::map<std::uint16_t, HostEnemy> enemies;  // by netId
};
HostRoom g_host;

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
    std::uint64_t lastSequence = 0;
    bool reconciled = false, occupancyHold = false;
};
std::optional<ActivationRecovery> g_activationRecovery;
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

void ClearActivation() {
    g_activationLease.Clear();
    g_activationChallenges = {};
}

void RetireWorldSession() {
    g_activationRecovery.reset();
    if (g_survivingPack.Intent()) g_survivingPack.Cancel();
    g_nativeResync.reset();
    g_resyncRecordAuthority.reset();
    if (g_resyncWriteFence == ResyncWriteFence::Exact) g_resyncWriteFence = ResyncWriteFence::Waiting;
    g_resyncOutput.clear(); g_resyncOutputContext = {};
    g_host = {};
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

void InterruptCensus(const char* reason, uintptr_t address, std::size_t nodes = 0) {
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
        g_log("[spawntrace] summary available=%u fixedAvailable=%u generatedAvailable=%u dispatcherAvailable=%u scriptAvailable=%u started=%llu published=%llu drained=%llu dropped=%llu unsupportedCaller=%llu unavailable=%llu nativeFaults=%llu lastException=%08X factoryRequestedMask=%u factoryVerifiedMask=%u factoryInstalledMask=%u factoryFailedMask=%u factoryForeignScopes=%llu factoryUnwoundScopes=%llu factoryAmbiguousScopes=%llu",
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
              static_cast<unsigned long long>(stats.factoryAmbiguousScopes));
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
        g_log("[lifecycletrace] summary requested=%u verifiedMask=%u installedMask=%u failedMask=%u started=%llu published=%llu drained=%llu dropped=%llu unavailable=%llu outOfScope=%llu nativeFaults=%llu unwound=%llu depthOverflow=%llu lastException=%08X predicateStarted=%llu predicatePublished=%llu predicateDropped=%llu predicateForeign=%llu predicateUnmatched=%llu predicateUnwound=%llu predicateDepthOverflow=%llu predicateCountOverflow=%llu",
              stats.requested ? 1u : 0u, stats.verifiedMask, stats.installedMask, stats.failedMask,
              static_cast<unsigned long long>(stats.started), static_cast<unsigned long long>(stats.published),
              static_cast<unsigned long long>(g_lifecycleDrained), static_cast<unsigned long long>(stats.dropped),
              static_cast<unsigned long long>(stats.unavailable), static_cast<unsigned long long>(stats.outOfScope),
              static_cast<unsigned long long>(stats.nativeFaults), static_cast<unsigned long long>(stats.unwound),
              static_cast<unsigned long long>(stats.depthOverflow), stats.lastNativeException,
              static_cast<unsigned long long>(stats.predicateStarted), static_cast<unsigned long long>(stats.predicatePublished),
              static_cast<unsigned long long>(stats.predicateDropped), static_cast<unsigned long long>(stats.predicateForeign),
              static_cast<unsigned long long>(stats.predicateUnmatched), static_cast<unsigned long long>(stats.predicateUnwound),
              static_cast<unsigned long long>(stats.predicateDepthOverflow), static_cast<unsigned long long>(stats.predicateCountOverflow));
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
        g_log("[resourcetrace] summary schema=1 status=%u generation=%llu recording=%u resourcesMayBeReferenced=%u modulePinned=%u installationIdentityVerified=%u producerEntered=%llu producerReturned=%llu producerUnwound=%llu producerDropped=%llu producerUnparented=%llu producerForeign=%llu producerPublished=%llu consumed=%llu logged=%llu suppressedNoLogger=%llu suppressedBudget=%llu logCap=%llu drainCap=%u emptyMeansAbsent=0 parentCompletenessProven=0 creationAuthority=0",
            static_cast<unsigned>(stats.status), static_cast<unsigned long long>(stats.generation), stats.recording ? 1U : 0U,
            stats.resourcesMayBeReferenced ? 1U : 0U, stats.modulePinned ? 1U : 0U, stats.installationIdentityVerified ? 1U : 0U,
            static_cast<unsigned long long>(stats.entered), static_cast<unsigned long long>(stats.returned), static_cast<unsigned long long>(stats.unwound),
            static_cast<unsigned long long>(stats.dropped), static_cast<unsigned long long>(stats.unparented), static_cast<unsigned long long>(stats.foreign),
            static_cast<unsigned long long>(stats.published), static_cast<unsigned long long>(g_resourceLog.consumed), static_cast<unsigned long long>(g_resourceLog.logged),
            static_cast<unsigned long long>(g_resourceLog.suppressedNoLogger), static_cast<unsigned long long>(g_resourceLog.suppressedBudget),
            static_cast<unsigned long long>(ResourceLogCap), ResourceDrainCap);
        g_resourceLog.summaryMs = now; g_resourceLog.summaryStatus = stats.status; g_resourceLog.summaryGeneration = stats.generation;
    }
}

void DrainPendingSpawnTrace(bool correlate) {
    nativehittrace::Drain(g_log);
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
    g_hostBeginPending = true;
    g_lastHashMs = 0;
    g_manifestSent = false;
    for (Spawn& s : g_inst.spawns) {
        s.announced = false;
        s.deathSent = false;
    }
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
    g_hostBeginPending = false;
    if (g_log) g_log("[enemysync] host arrived epoch=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u",
                     t.epoch, t.worldId, t.roomId, t.door, t.mapProgram,
                     t.battleProgram, t.eventProgram);
    return true;
}

bool HostFrame(std::uint32_t frame, const std::vector<std::size_t>& newSpawns,
               const NativeCensus& census) {
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
            m.entries.push_back(e);
        }
        if (!Send(encode(m))) return false;
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
    if (!g_log || !g_nativeResync) return;
    const auto& snapshot = g_nativeResync->snapshot;
    const auto* recovery = g_activationRecovery ? &*g_activationRecovery : nullptr;
    g_log("[resync-activation] action=%s reason=%s load=%u transition=%u controller=%llX hostFirstUpdate=%llu historicalTicks=%u liveTicks=%u reconciled=%u battleParityObserved=0",
        action, reason, recovery ? recovery->load : warp::LoadSerial(),
        recovery ? recovery->transition : warp::TransitionSerial(),
        static_cast<unsigned long long>(recovery ? recovery->controller : 0),
        static_cast<unsigned long long>(snapshot.activationReplay ? snapshot.activationReplay->firstUpdateSequence : 0),
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
    for (const auto& row : snapshot.enemies) {
        HostEnemy enemy;
        enemy.spawnIndex = row.identity.spawnIndex; enemy.objectId = row.identity.objectId;
        enemy.spawnPos = row.identity.spawnPosition; enemy.battleProgram = row.identity.battleProgram;
        enemy.hp = row.hp;
        staged.enemies.emplace(row.identity.netId, enemy);
    }
    if (!WorldContextCurrent(context) || !progresssync::StageFull(snapshot.progress, context.generation) ||
        !WorldContextCurrent(context) || !warp::QueueHostTransition(snapshot.room)) {
        progresssync::Reset();
        warp::SetClientAuthority(false);
        QueueNativeAck(ResyncAckStatus::Unavailable, "native bootstrap context or progress unavailable"); return;
    }
    g_host = std::move(staged); g_hostHpSequence = snapshot.hpSequence;
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
        recovery.reconciled = true; // full exact population and HP readback, not requested stores
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

bool ReceiveWorldPackets() {
    bool hostSessionReset = false;
    std::vector<std::uint8_t> packet;
    while (g_bridge.ReceiveFromRuntime(packet)) {
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
                if (scope->kind != WorldSourceKind::Native ||
                    scope->targetConnectionId != g_bridge.ConnectionId(g_bridge.LocalSlot()) ||
                    scope->targetDeliverySerial != g_bridge.DeliverySerial() ||
                    (g_resyncPlan && scope->sessionId != g_resyncPlan->request.key.sessionId)) continue;
                packet = std::move(envelope.packet);
                type = decodePacketHeader(packet.data(), packet.size(), payload, size);
                if (packet.size() != size + 3 || !isScopedWorldPacket(type)) continue;
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
                    if (generation != 0 && generation == g_bridge.SessionGeneration() &&
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
            if (CurrentRole() != Role::Client) continue;
            ByteReader r(payload, size);
            if (progresssync::HandlePacket(type, r)) {
                if (scope && PackPreparationActive() &&
                    !progresssync::DesiredMatchesFull(g_nativeResync->snapshot.progress, g_nativeResync->context.generation))
                    ContinuePackPreparation(scope->hostSourceSerial, true);
                continue;
            }
            if (type == PacketType::EventHold && PackPreparationActive()) {
                EventHold hold; read(r, hold);
                if (r.atEnd() && hold.epoch == g_host.epoch)
                    ContinuePackPreparation(scope->hostSourceSerial,
                        hold.active != g_nativeResync->snapshot.hold.active ||
                        hold.eventProgram != g_nativeResync->snapshot.hold.eventProgram);
                continue;
            }
            if (type == PacketType::RoomTransition) {
                RoomTransition t;
                read(r, t);
                // Reliable delivery normally orders these, but never let an
                // old/replayed epoch roll back authority. Epoch zero is unset.
                const auto advance = t.epoch - g_host.epoch;
                if (t.epoch != 0 && (g_host.epoch == 0 || (advance != 0 && advance < 0x80000000u))) {
                    if (!warp::QueueHostTransition(t)) {
                        if (g_log) g_log("[enemysync] client transition rejected epoch=%u", t.epoch);
                        continue;
                    }
                    g_host = {};
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
                }
            } else if (type == PacketType::EnemyManifest) {
                EnemyManifest m;
                read(r, m);
                if (m.epoch != g_host.epoch) continue;
                const bool packChanged = PackManifestChanged(m);
                if (m.replace) g_host.enemies.clear();
                for (const auto& e : m.entries) {
                    HostEnemy& h = g_host.enemies[e.netId];
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
                    if (it != g_host.enemies.end()) it->second.hp = e.hp;
                }
                ContinuePackPreparation(scope->hostSourceSerial, packChanged);
            } else if (type == PacketType::EnemyDeath) {
                EnemyDeath m;
                read(r, m);
                if (m.epoch != g_host.epoch) continue;
                auto it = g_host.enemies.find(m.netId);
                if (it != g_host.enemies.end()) it->second.dead = true;
                SYNC_LOG("[enemysync] client: host death epoch %u netId %u", m.epoch, m.netId);
                ContinuePackPreparation(scope->hostSourceSerial, true);
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

// No C++ objects requiring unwinding in the SEH leaves that call/write native memory.
bool WriteNativeHp(const NativeEnemy& expected, std::int32_t hp, std::uint32_t generation,
                   const NativeRecordWriteCheck* record = nullptr) {
    __try {
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
        *reinterpret_cast<std::int32_t*>(expected.status) = hp;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ApplyNativeDeath(const NativeEnemy& expected, int hp, std::uint32_t generation,
                      const NativeRecordWriteCheck* record = nullptr) {
    __try {
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
        if (!g_takeDamage ||
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
        attempted = true;
        g_takeDamage(reinterpret_cast<void*>(expected.actor), -pending.claim.damage, 0, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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
    for (Spawn& s : g_inst.spawns) {
        if (!s.present || s.killed) continue;
        const NativeRecordWriteCheck* recordCheck = nullptr;
        ResyncRecordReference localRecord;
        if (contentRequired) {
            const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto& b) { return b.actor == s.actor; });
            if (binding == bindings.end()) { clearBindings(); return false; }
            s.netId = binding->netId;
            recordCheck = &binding->write;
            localRecord = binding->record;
        } else {
        // Match by spawn point + object, to the host's newest enemy there: a
        // spawn point the host refilled (its enemy despawned and respawned
        // on the host only) re-binds this copy to the replacement. Fallback:
        // the same spawn index.
        int best = -1;
        for (const auto& [netId, h] : g_host.enemies) {
            // A bound copy follows the newest entry at its point even when the
            // host reported it dead (then the copy dies with it). A new,
            // unbound spawn binds only to a live entry: if it appeared before
            // the host's manifest for it arrived (e.g. an enemy that vanished
            // and reappeared as a new actor), it waits instead of inheriting
            // an older death.
            if (h.objectId != s.objectId || (s.netId < 0 && h.dead)) continue;
            if (SamePoint(h.spawnPos, s.spawnPos)) {
                best = netId;  // map is ordered by netId: the last hit is the newest
            }
        }
        if (best < 0 && s.netId < 0) {
            for (const auto& [netId, h] : g_host.enemies) {
                if (h.spawnIndex == s.spawnIndex && h.objectId == s.objectId && !h.dead) best = netId;
            }
        }
        if (best >= 0 && best != s.netId) {
            SYNC_LOG("[enemysync] client: spawn %u %s netId %d", s.spawnIndex,
                     s.netId < 0 ? "matched to" : "re-bound to", best);
            s.netId = best;
        }
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
        if (h.dead) {
            if (native.hp > 0 && g_applyStatDelta && !s.deathAttempted) {
                s.deathAttempted = true;
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
    g_lastHashMs = now;
    if (!Send(encode(state))) return;
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
        // back after leaving it alive (e.g. burrowed). Changed checked metadata or
        // a slot whose enemy left the list dead is a new spawn.
        const bool same = it != g_inst.byActor.end() &&
                          g_inst.spawns[it->second].objectId == native.objectId &&
                          g_inst.spawns[it->second].objentry == native.objentry &&
                          g_inst.spawns[it->second].status == native.status &&
                          (g_inst.spawns[it->second].present || g_inst.spawns[it->second].lastHp > 0);
        if (same) {
            index = it->second;
        } else {
            Spawn s;
            s.spawnIndex = static_cast<std::uint16_t>(g_inst.spawns.size());
            s.actor = actor;
            s.objentry = native.objentry;
            s.status = native.status;
            s.objectId = native.objectId;
            s.objectType = native.objectType;
            s.spawnPos = native.position;
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
    if (g_envRole != Role::Off) return g_envRole;
    const std::uint8_t slot = g_bridge.LocalSlot();
    if (slot == 0) return Role::Host;
    if (slot == 1 || slot == 2) return Role::Client;
    return Role::Off;
}

} // namespace

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta, TakeDamageFn takeDamage) {
    g_exeBase = exeBase;
    g_log = log;
    g_applyStatDelta = applyStatDelta;
    g_takeDamage = takeDamage;
    char prepare[4] {};
    g_survivingPackEnabled = GetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", prepare, sizeof(prepare)) == 1 &&
        prepare[0] == '1';
    progresssync::Install(exeBase, log, SendCapturedWorld);
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
    if (g_hostClaimProcessing) return;
    // This known native actor-update entry establishes diagnostic affinity;
    // broader creator/removal probes may execute before it or on other threads.
    spawncontroller::RegisterDiagnosticGameThread();
    nativehittrace::RegisterOwnerThread();
    g_hitTraceFrame = frame;
    if (!g_bridge.IsOpen()) { DrainPendingSpawnTrace(false); return; }
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
    TickNativeResync(frame);
    if (!WorldSessionGeneration()) { DrainPendingSpawnTrace(false); return; }
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
        if (!g_host.arrived) { DrainPendingSpawnTrace(false); return; }
        RequestActivation();
    }
    if (!g_inst.live) { DrainPendingSpawnTrace(false); return; }
    auto census = CaptureNativeCensus();
    if (!CensusMatchesInstance(census)) {
        InterruptCensus(census.state == CensusState::Complete ? "instance changed" : census.reason,
                        census.failedAt, census.nodeCount);
        DrainSpawnTrace(census);
        DrainLifecycleTrace(census);
        nativehittrace::Drain(g_log);
        DrainResourceTrace();
        return;
    }
    if (g_censusInterrupted) {
        for (Spawn& spawn : g_inst.spawns) spawn.goneSinceMs = 0;
        g_censusInterrupted = false;
    }
    auto fresh = TrackSpawns(census);
    if (g_role == Role::Host) {
        // Track while progress or ring capacity delays the announcement. No
        // manifest/HP/hash may escape under the previous room's epoch.
        if (!HostBeginInstance()) { DrainPendingSpawnTrace(false); return; }
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
        FlushActivationResponses();
    } else if (g_role == Role::Client) {
        for (const std::size_t i : fresh) {
            SYNC_LOG("[enemysync] client: local spawn %u frame %u objectId %u @%llX at (%.0f,%.0f,%.0f)",
                     g_inst.spawns[i].spawnIndex, frame, g_inst.spawns[i].objectId,
                     static_cast<unsigned long long>(g_inst.spawns[i].actor),
                     g_inst.spawns[i].spawnPos.x, g_inst.spawns[i].spawnPos.y, g_inst.spawns[i].spawnPos.z);
        }
        if (!ClientFrame(census)) { DrainPendingSpawnTrace(false); return; }
    }
    PublishAppliedHash(frame, census);
    TickNativeResync(frame, true);
    // Read-only diagnostics drain after bindings have been announced/applied.
    DrainPendingSpawnTrace(true);
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
        claim.seq = ++g_localClaimSequence;
        // Encode/send this detection's immutable identity immediately. A full
        // ring drops the claim; it can never be relabeled after a room change.
        if (!Send(encode(claim))) return reject("world ring full");
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

void CaptureHostActivation(const float* position4) {
    // The hook provides a checked, aligned local copy of its actual native
    // argument. Never read an avatar bridge or answer from a cached old point.
    RoomTransition location;
    if (!position4 || !ActivationContext(Role::Host, location) ||
        g_activationSourceSeq == std::numeric_limits<std::uint64_t>::max()) return;
    std::array<float, 4> point;
    std::copy_n(position4, point.size(), point.begin());
    if (!std::all_of(point.begin(), point.end(), [](float value) { return std::isfinite(value); })) return;
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
    g_activationRecovery.reset();
    if (g_survivingPack.Intent()) g_survivingPack.Cancel();
    ClearActivation();
    ClearPendingHits();
    g_takeDamage = nullptr;
    g_resyncPlan.reset(); g_nativeResync.reset(); g_resyncOutput.clear();
    g_resyncWriteFence = ResyncWriteFence::None; g_resyncRecordAuthority.reset();
    g_activationOrderedGeneration = 0;
    progresssync::Reset();
    g_hostBeginPending = false;
    g_lastHashMs = 0;
    g_bridge.Close();
}

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
