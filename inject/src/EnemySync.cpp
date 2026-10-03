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
    bool present = false;     // in the entity list last frame
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
std::vector<uintptr_t> g_frameActors, g_lastActors;
std::uint64_t g_lastHashMs = 0;

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

bool HostFrame(std::uint32_t frame, const std::vector<std::size_t>& newSpawns) {
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
        const std::int32_t* p = HpPtr(s.actor);
        if (!p) continue;
        if (*p <= 0 && !s.deathSent) {
            EnemyDeath d;
            d.epoch = g_epoch;
            d.netId = static_cast<std::uint16_t>(s.spawnIndex + 1);
            if (Send(encode(d))) {
                s.deathSent = true;
                SYNC_LOG("[enemysync] host death epoch %u netId %u", g_epoch, d.netId);
            }
        }
        if (*p > 0) hp.entries.push_back({static_cast<std::uint16_t>(s.spawnIndex + 1), p[0], p[1]});
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
                g_lastActors.clear();
                g_frameActors.clear();
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
                    g_lastActors.clear();
                    g_frameActors.clear();
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

void ClientFrame() {
    if (!g_host.arrived || g_inst.world != g_host.world || g_inst.room != g_host.room || g_inst.btl != g_host.btl) {
        return;  // not in the host's room instance: nothing to match
    }
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
        std::int32_t* p = HpPtr(s.actor);
        if (!p) continue;
        if (h.dead) {
            if (*p > 0 && g_applyStatDelta && !s.deathAttempted) {
                s.deathAttempted = true;
                const int before = *p;
                g_applyStatDelta(reinterpret_cast<void*>(s.actor), -before, 0, 0);
                if (*p <= 0) {
                    if (g_log) g_log("[enemysync] client: host death netId %d applied (hp %d -> %d)",
                                     s.netId, before, *p);
                } else if (g_log) {
                    g_log("[enemysync] client: host death netId %d FAILED (hp %d -> %d)", s.netId,
                          before, *p);
                }
            }
            s.killed = *p <= 0;
        } else if (h.hp > 0 && *p != h.hp) {
            *p = h.hp;  // hold at the host's absolute HP (never 0: deaths are explicit)
        }
    }
}

// ---- Both -------------------------------------------------------------------

void PublishAppliedHash(std::uint32_t frame) {
    if (g_role == Role::Off || !g_inst.live || g_lastActors.empty() ||
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

    std::vector<AppliedEnemyState> observed;
    std::vector<uintptr_t> actors;
    for (const Spawn& spawn : g_inst.spawns) {
        // Presence and native HP decide life, independently of received deaths,
        // killed/deathSent flags or historical host manifest entries.
        if (!spawn.present || !IsEnemy(spawn.actor)) continue;
        const auto objectId = Read<std::uint32_t>(ObjEntry(spawn.actor) + offsets::objentry::OBJECT_ID);
        const auto hp = *HpPtr(spawn.actor);
        std::uint16_t netId = 0;
        if (g_role == Role::Host) {
            netId = static_cast<std::uint16_t>(spawn.spawnIndex + 1);
        } else if (spawn.netId > 0) {
            const auto bound = g_host.enemies.find(static_cast<std::uint16_t>(spawn.netId));
            if (bound != g_host.enemies.end() && bound->second.objectId == objectId) {
                netId = bound->first;
            }
        }
        observed.push_back({netId, objectId, hp});
        actors.push_back(spawn.actor);
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

// Folds last frame's actor list into the instance's spawns. Returns the
// indices of new spawns.
std::vector<std::size_t> TrackSpawns() {
    std::vector<std::size_t> fresh;
    std::unordered_map<uintptr_t, std::size_t> present;
    for (const uintptr_t actor : g_lastActors) {
        if (!IsEnemy(actor)) continue;
        auto it = g_inst.byActor.find(actor);
        std::size_t index;
        // An address seen before is the same enemy: still in the list (a
        // dying enemy stays there at 0 HP through its death animation), or
        // back after leaving it alive (e.g. burrowed). Only a slot whose
        // enemy left the list dead is a new spawn.
        const bool same = it != g_inst.byActor.end() &&
                          (g_inst.spawns[it->second].present || g_inst.spawns[it->second].lastHp > 0);
        if (same) {
            index = it->second;
        } else {
            Spawn s;
            s.spawnIndex = static_cast<std::uint16_t>(g_inst.spawns.size());
            s.actor = actor;
            s.objectId = Read<std::uint32_t>(ObjEntry(actor) + offsets::objentry::OBJECT_ID);
            const uintptr_t ent = actor + offsets::actor::ENTITY_TRANSFORM;
            s.spawnPos = {Read<float>(ent + offsets::entity::POS_X), Read<float>(ent + offsets::entity::POS_Y),
                          Read<float>(ent + offsets::entity::POS_Z)};
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
        if (const std::int32_t* p = HpPtr(actor)) s.lastHp = *p;
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

void NoteActor(uintptr_t actor) {
    if (g_bridge.IsOpen() && !warp::TransitionPending() &&
        (g_role != Role::Client || g_host.arrived)) {
        g_frameActors.push_back(actor);
    }
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
    g_lastActors.swap(g_frameActors);
    g_frameActors.clear();

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
        g_lastActors.clear();
        g_frameActors.clear();
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
        g_lastActors.clear();
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
    auto fresh = TrackSpawns();
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
        if (!HostFrame(frame, fresh)) return;
    } else if (g_role == Role::Client) {
        for (const std::size_t i : fresh) {
            const uintptr_t ent = g_inst.spawns[i].actor + offsets::actor::ENTITY_TRANSFORM;
            SYNC_LOG("[enemysync] client: local spawn %u frame %u objectId %u @%llX at (%.0f,%.0f,%.0f)",
                     g_inst.spawns[i].spawnIndex, frame, g_inst.spawns[i].objectId,
                     static_cast<unsigned long long>(g_inst.spawns[i].actor),
                     Read<float>(ent + offsets::entity::POS_X), Read<float>(ent + offsets::entity::POS_Y),
                     Read<float>(ent + offsets::entity::POS_Z));
        }
        ClientFrame();
    }
    PublishAppliedHash(frame);
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
