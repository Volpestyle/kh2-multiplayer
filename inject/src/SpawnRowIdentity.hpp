#pragma once
// ============================================================================
// SpawnRowIdentity: when is a native enemy at a known actor address the same
// enemy as the spawn row already tracked there? A pure rule used by EnemySync's
// TrackSpawns; no game memory, no hooks.
//
// Live run 094908 (BB Entrance Hall, battle program 3): the friend's Large Body
// row left the actor list alive (hp 90), then a different Large Body (another
// spawn group) was allocated at the same actor, objentry and status addresses.
// The old rule ("same metadata, and the row is present or left alive") kept the
// old row and its historical spawn point, so binding matched the dead host
// netId 2 instead of netId 4 and the new enemy ran unbound. The native spawn
// controller (actor+0x9E8) and spawn record (actor+0x9F0) differed; they tell a
// recycled address from an enemy coming back (e.g. burrowed), which keeps both.
// ============================================================================

#include <cstdint>

namespace kh2coop::inject::spawnrow {

// The checked metadata of a native enemy as sampled now, or as recorded when its
// row was created. identityRead is false when controller/record were unreadable.
struct Sample {
    std::uint32_t objectId = 0;
    std::uintptr_t objentry = 0, status = 0;
    std::uintptr_t controller = 0, record = 0;
    bool identityRead = false;
};

// The tracked row at that address: what it recorded at creation, whether it was in
// the latest complete census, and the HP it was last seen with.
struct Row {
    Sample created {};
    bool present = false;
    std::int32_t lastHp = 1;
};

// Same enemy: same checked metadata; the row is still in the list or left it
// alive (a dead slot is a new spawn); and, when both sides read their native
// controller and spawn record, both are unchanged. If either side could not
// read them, the previous rule decides alone (no behaviour change).
inline bool SameSpawnRow(const Row& row, const Sample& now) noexcept {
    const auto& was = row.created;
    if (was.objectId != now.objectId || was.objentry != now.objentry || was.status != now.status) return false;
    if (!row.present && row.lastHp <= 0) return false;
    if (was.identityRead && now.identityRead)
        return was.controller == now.controller && was.record == now.record;
    return true;
}

}  // namespace kh2coop::inject::spawnrow
