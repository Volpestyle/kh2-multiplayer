// VUH-1515: spawn-row identity on a recycled actor address (inject/src/SpawnRowIdentity.hpp).
// Pure rules; no game, no hooks, no sockets.
#include "SpawnRowIdentity.hpp"
#include <cstdio>

using namespace kh2coop::inject::spawnrow;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

// Live run 094908: friend spawn 1 and its successor at the same actor address.
static Sample First() { return {303, 0x7FF76D749F78, 0x7FF76D742BB8, 0x7FF76D8084F8, 0x7FF76D705704, true}; }
static Sample Recycled() { return {303, 0x7FF76D749F78, 0x7FF76D742BB8, 0x7FF76D8085A8, 0x7FF76D70591C, true}; }

int main() {
    // 1. Burrowed / left the list alive and back with the same native record: the same row.
    Row left {First(), false, 90};
    CHECK(SameSpawnRow(left, First()));
    Row present {First(), true, 90};
    CHECK(SameSpawnRow(present, First()));

    // 2. Recycled address (094908): same actor/objentry/status, different record or controller: a new row.
    CHECK(!SameSpawnRow(left, Recycled()));
    Sample recordOnly = First(); recordOnly.record = 0x7FF76D70591C;
    CHECK(!SameSpawnRow(left, recordOnly));
    Sample controllerOnly = First(); controllerOnly.controller = 0x7FF76D8085A8;
    CHECK(!SameSpawnRow(left, controllerOnly));
    CHECK(!SameSpawnRow(present, Recycled()));  // even while still listed: a different native spawn

    // 3. Dead slot: a new spawn, as before, whatever the record says.
    Row dead {First(), false, 0};
    CHECK(!SameSpawnRow(dead, First()));
    Row negative {First(), false, -5};
    CHECK(!SameSpawnRow(negative, First()));
    Row deadListed {First(), true, 0};  // still in the list at 0 HP (death animation): the same enemy
    CHECK(SameSpawnRow(deadListed, First()));

    // 4. Record unreadable on either side: the previous rule decides alone (no behaviour change).
    Sample unread = Recycled(); unread.identityRead = false;
    CHECK(SameSpawnRow(left, unread));
    Row oldRow {First(), false, 90}; oldRow.created.identityRead = false;
    CHECK(SameSpawnRow(oldRow, Recycled()));
    CHECK(!SameSpawnRow(Row {Sample {303, 1, 2, 0, 0, false}, false, 0}, Sample {303, 1, 2, 0, 0, false}));

    // The checked metadata still decides first.
    Sample otherObject = First(); otherObject.objectId = 17;
    CHECK(!SameSpawnRow(left, otherObject));
    Sample otherStatus = First(); otherStatus.status += 0x40;
    CHECK(!SameSpawnRow(left, otherStatus));
    Sample otherEntry = First(); otherEntry.objentry += 0x60;
    CHECK(!SameSpawnRow(left, otherEntry));

    std::printf("%d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
