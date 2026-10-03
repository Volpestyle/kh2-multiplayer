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
#include "ProgressSync.hpp"
#include "Warp.hpp"

#include "kh2coop/AppliedStateHash.hpp"
#include "kh2coop/Codec.hpp"
#include "kh2coop/KH2Offsets.hpp"
#include "kh2coop/WorldBridge.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
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
Role g_role = Role::Off;
WorldBridge g_bridge;

// One enemy as this machine saw it spawn in the current room instance.
struct Spawn {
    std::uint16_t spawnIndex = 0;
    std::uint32_t objectId = 0;
    uintptr_t actor = 0;
    Vec3 spawnPos {};         // where it first appeared (identical across instances)
    bool present = false;     // in the latest complete native census
    std::int32_t lastHp = 1;  // HP when last seen (<= 0: dead, the slot may be reused)
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
bool g_manifestSent = false;        // host: first manifest of the epoch went out
bool g_hostBeginPending = false;
RoomTransition g_pendingHostRoom {};
std::uint64_t g_lastHashMs = 0;
bool g_censusInterrupted = false;
std::uint64_t g_lastCensusErrorMs = 0;

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
    std::uint32_t transition = 0, load = 0;
    RoomTransition location {};
    std::vector<NativeEnemy> enemies;
};

bool SafeNativeGameplay();

NativeCensus CaptureNativeCensus() {
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
        return row.actor == spawn.actor && row.objectId == spawn.objectId;
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
    if (!g_bridge.IsOpen()) return false;
    if (!g_bridge.SendToRuntime(packet)) {
        SYNC_LOG("[enemysync] world ring full, packet dropped");
        return false;
    }
    return true;
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

void QueueHostBeginInstance(const RoomTransition& location) {
    g_pendingHostRoom = location;
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
    if (!Send(encode(g_pendingHostRoom))) return false;
    const RoomTransition& t = g_pendingHostRoom;
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
    if (frame % HP_INTERVAL_FRAMES == 0 && !hp.entries.empty()) Send(encode(hp));
    return true;
}

// ---- Client -----------------------------------------------------------------

bool ReceiveWorldPackets() {
    bool hostSessionReset = false;
    std::vector<std::uint8_t> packet;
    while (g_bridge.ReceiveFromRuntime(packet)) {
        try {
            const std::uint8_t* payload = nullptr;
            std::size_t size = 0;
            const auto type = decodePacketHeader(packet.data(), packet.size(), payload, size);
            if (type == PacketType::SessionState && size == 0) {
                // DLL-local boundary, ordered in the ring before new-session
                // world packets. Reset here, not before/after the drain: a
                // producer may enqueue this marker halfway through our frame.
                g_host = {};
                progresssync::Reset();
                g_hostBeginPending = false;
                g_lastHashMs = 0;
                warp::SetClientAuthority(false);
                for (Spawn& s : g_inst.spawns) {
                    s.netId = -1;
                    s.killed = false;
                    s.deathAttempted = false;
                }
                g_role = CurrentRole();
                warp::SetClientAuthority(g_role == Role::Client);
                hostSessionReset = hostSessionReset || g_role == Role::Host;
                if (g_log) g_log("[enemysync] session reset: host epoch and pending target cleared");
                continue;
            }
            if (CurrentRole() != Role::Client) continue;
            ByteReader r(payload, size);
            if (progresssync::HandlePacket(type, r)) continue;
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
                }
            } else if (type == PacketType::EnemyManifest) {
                EnemyManifest m;
                read(r, m);
                if (m.epoch != g_host.epoch) continue;
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
            } else if (type == PacketType::EnemyHp) {
                EnemyHp m;
                read(r, m);
                if (m.epoch != g_host.epoch) continue;
                for (const auto& e : m.entries) {
                    auto it = g_host.enemies.find(e.netId);
                    if (it != g_host.enemies.end()) it->second.hp = e.hp;
                }
            } else if (type == PacketType::EnemyDeath) {
                EnemyDeath m;
                read(r, m);
                if (m.epoch != g_host.epoch) continue;
                auto it = g_host.enemies.find(m.netId);
                if (it != g_host.enemies.end()) it->second.dead = true;
                SYNC_LOG("[enemysync] client: host death epoch %u netId %u", m.epoch, m.netId);
            }
        } catch (const std::exception&) {
            SYNC_LOG("[enemysync] client: malformed world packet");
        }
    }
    return hostSessionReset;
}

// No C++ objects requiring unwinding in the SEH leaves that call/write native memory.
bool WriteNativeHp(const NativeEnemy& expected, std::int32_t hp) {
    __try {
        if (Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId) return false;
        *reinterpret_cast<std::int32_t*>(expected.status) = hp;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ApplyNativeDeath(const NativeEnemy& expected, int hp) {
    __try {
        if (Read<uintptr_t>(expected.actor + offsets::actor::OBJENTRY_PTR) != expected.objentry ||
            Read<uintptr_t>(expected.actor + ACTOR_STATUS) != expected.status ||
            Read<std::uint32_t>(expected.objentry + offsets::objentry::OBJECT_ID) != expected.objectId) return false;
        g_applyStatDelta(reinterpret_cast<void*>(expected.actor), -hp, 0, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool ClientFrame(const NativeCensus& initialCensus) {
    if (!g_host.arrived || g_inst.world != g_host.world || g_inst.room != g_host.room || g_inst.btl != g_host.btl) {
        return false;  // not in the host's room instance: nothing to match
    }
    NativeCensus current = initialCensus;
    for (Spawn& s : g_inst.spawns) {
        if (!s.present || s.killed) continue;
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
        if (s.netId < 0) continue;
        const HostEnemy& h = g_host.enemies[static_cast<std::uint16_t>(s.netId)];
        const auto* sampled = FindNativeEnemy(current, s);
        if (!sampled) continue; // a preceding native lethal may have removed it
        NativeEnemy native;
        bool isEnemy = false;
        if (!CensusMatchesInstance(current) || !SafeNativeGameplay() ||
            !ReadNativeEnemy(s.actor, native, isEnemy) || !isEnemy || !SameNativeIdentity(*sampled, native)) {
            InterruptCensus("client target changed", s.actor);
            return false;
        }
        if (h.dead) {
            if (native.hp > 0 && g_applyStatDelta && !s.deathAttempted) {
                s.deathAttempted = true;
                const int before = native.hp;
                if (!ApplyNativeDeath(native, before)) {
                    InterruptCensus("native lethal fault; outcome unknown", s.actor);
                    return false;
                }
                NativeEnemy after;
                if (!ReadNativeEnemy(s.actor, after, isEnemy) || !isEnemy || !SameNativeIdentity(native, after)) {
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
                    InterruptCensus(current.state == CensusState::Complete ? "instance changed" : current.reason,
                                    current.failedAt, current.nodeCount);
                    return false;
                }
            }
            s.killed = native.hp <= 0;
        } else if (h.hp > 0 && native.hp != h.hp) {
            if (!WriteNativeHp(native, h.hp)) { // never 0: deaths are explicit
                InterruptCensus("client HP write unavailable", s.actor);
                return false;
            }
        }
    }
    return true;
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
        // back after leaving it alive (e.g. burrowed). A changed object or
        // a slot whose enemy left the list dead is a new spawn.
        const bool same = it != g_inst.byActor.end() &&
                          g_inst.spawns[it->second].objectId == native.objectId &&
                          (g_inst.spawns[it->second].present || g_inst.spawns[it->second].lastHp > 0);
        if (same) {
            index = it->second;
        } else {
            Spawn s;
            s.spawnIndex = static_cast<std::uint16_t>(g_inst.spawns.size());
            s.actor = actor;
            s.objectId = native.objectId;
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
        if (const auto* native = FindNativeEnemy(census, s)) s.lastHp = native->hp;
    }
    return fresh;
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

void Install(uintptr_t exeBase, LogFn log, StatDeltaFn applyStatDelta) {
    g_exeBase = exeBase;
    g_log = log;
    g_applyStatDelta = applyStatDelta;
    progresssync::Install(exeBase, log, Send);
    g_envRole = ReadEnvRole();
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
    if (!g_bridge.IsOpen()) return;
    const Role role = CurrentRole();
    bool becameHost = role == Role::Host && g_role != Role::Host;
    if (role != g_role) {
        SYNC_LOG("[enemysync] role %s", role == Role::Host     ? "host"
                                        : role == Role::Client ? "client"
                                                               : "off");
        g_role = role;
        g_host = {};
        progresssync::Reset();
        g_hostBeginPending = false;
        g_lastHashMs = 0;
        for (Spawn& s : g_inst.spawns) {
            s.netId = -1;
            s.killed = false;
            s.deathAttempted = false;
        }
    }
    // Unknown/disconnected sessions immediately release native exit authority.
    warp::SetClientAuthority(role == Role::Client);
    // Every role drains local reset markers. Each marker takes effect before
    // the next packet, and no later frame-level reset can erase a new command.
    becameHost = ReceiveWorldPackets() || becameHost;
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
            if (g_bridge.SendToRuntime(encode(ack))) {
                g_host.ackSent = true;
                if (g_log) g_log("[enemysync] client arrived epoch=%u room=%02X/%02X door=%u map=%u btl=%u evt=%u",
                                 ack.epoch, location.worldId, location.roomId, location.door,
                                 location.mapProgram, location.battleProgram, location.eventProgram);
            }
        }
        if (!g_host.arrived) return;
    }
    if (!g_inst.live) return;
    const auto census = CaptureNativeCensus();
    if (!CensusMatchesInstance(census)) {
        InterruptCensus(census.state == CensusState::Complete ? "instance changed" : census.reason,
                        census.failedAt, census.nodeCount);
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
        if (!HostBeginInstance()) return;
        for (std::size_t i = 0; i < g_inst.spawns.size(); ++i) {
            Spawn& s = g_inst.spawns[i];
            if (!s.announced) {
                if (std::find(fresh.begin(), fresh.end(), i) == fresh.end()) fresh.push_back(i);
            }
        }
        // A failed manifest stays pending. Publishing its actors' hash before
        // their bindings reach the peer would report a transport backlog as a
        // native population mismatch.
        if (!HostFrame(frame, fresh, census)) return;
    } else if (g_role == Role::Client) {
        for (const std::size_t i : fresh) {
            SYNC_LOG("[enemysync] client: local spawn %u frame %u objectId %u @%llX at (%.0f,%.0f,%.0f)",
                     g_inst.spawns[i].spawnIndex, frame, g_inst.spawns[i].objectId,
                     static_cast<unsigned long long>(g_inst.spawns[i].actor),
                     g_inst.spawns[i].spawnPos.x, g_inst.spawns[i].spawnPos.y, g_inst.spawns[i].spawnPos.z);
        }
        if (!ClientFrame(census)) return;
    }
    PublishAppliedHash(frame, census);
}

bool DropLocalEnemyDamage(uintptr_t victim) {
    return g_role == Role::Client && g_inst.live && !warp::TransitionPending() && IsEnemy(victim);
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

void Shutdown() {
    progresssync::Reset();
    g_hostBeginPending = false;
    g_lastHashMs = 0;
    g_bridge.Close();
}

} // namespace enemysync
} // namespace inject
} // namespace kh2coop
