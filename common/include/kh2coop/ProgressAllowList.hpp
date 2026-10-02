#pragma once
// ============================================================================
// Candidate allow list for ProgressMirror — UNVERIFIED (VUH-1497).
//
// Offsets are relative to the in-memory save body (Steam Global SAVE =
// exe+0x09A98B0, which starts with "KH2J"). OpenKH's SaveDataFinalMix offsets
// are 8 lower: OpenKH WorldId 0x04 / WorldPartyMembers 0x3534 vs the GoA ROM's
// in-memory Save+0x0C / Save+0x353C. Confirm live before relying on them.
//
// Shared (host-authoritative): what decides which room programs load, story
// beats, and visited rooms. Per player (D8) and deliberately absent:
// characters/stats (0x24F8..), drive forms, puzzle pieces, munny, timers,
// world party members (puppets use them), inventory (0x3588..), EXP.
// Chest-opened flags are not located yet.
// ============================================================================

#include "kh2coop/ProgressMirror.hpp"

#include <vector>

namespace kh2coop {

inline std::vector<ProgressRange> candidateProgressAllowList() {
    return {
        // Per-room map/battle/event program table: 19 worlds x 64 rooms x
        // 3 shorts (GoA ROM Warp(): Save+0x10+0x180*W+0x6*R). Drives which
        // spawn/event program a room loads, so enemy keys (D5) depend on it.
        {0x0010, 0x1C80},
        // Story progress, 20 x 0x20 (OpenKH StoryProgress 0x1C90 + 8). The GoA
        // ROM's progress checks (Save+0x1CFF, +0x1D2E, +0x1EDE) fall inside.
        {0x1C98, 0x280},
        // Room-visited flags, 8 bytes x 19 worlds (OpenKH 0x22F8 + 8).
        {0x2300, 0x98},
    };
}

} // namespace kh2coop
