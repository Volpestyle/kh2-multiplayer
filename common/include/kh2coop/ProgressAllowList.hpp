#pragma once
// ============================================================================
// Native-verified progress allow list (VUH-1497).
//
// Offsets are relative to the in-memory save body (Steam Global SAVE =
// exe+0x09A98B0, which starts with "KH2J"). No universal +8 shift applies.
// Native accessor evidence: docs/pointer_map_v1.md; build/rig/progress-ranges.md.
//
// Shared (host-authoritative): what decides which room programs load, story
// beats, and visited rooms. Per player (D8) and deliberately absent:
// characters/stats (0x24F0..), drive forms, puzzle pieces, munny, timers,
// world party members (puppets use them), inventory (0x3580..), EXP.
// All unspecified bytes and unverified chest bits remain local.
// ============================================================================

#include "kh2coop/ProgressMirror.hpp"

#include <vector>

namespace kh2coop {

inline std::vector<ProgressRange> verifiedProgressAllowList() {
    return {
        // Per-room map/battle/event program table: 19 worlds x 64 rooms x
        // 3 shorts (GoA ROM Warp(): Save+0x10+0x180*W+0x6*R). Drives which
        // spawn/event program a room loads, so enemy keys (D5) depend on it.
        {0x0010, 0x1C80},
        // Native story tables: 19 x 0x20; auxiliary state starts at 0x1EF0.
        {0x1C90, 0x260},
        // Room-visited flags, 8 bytes x 19 worlds.
        {0x22F8, 0x98},
        // Chest flags 1..411, with partial endpoint masks below.
        {0x23AC, 0x34},
    };
}

inline constexpr std::uint8_t verifiedProgressByteMask(std::uint32_t offset) {
    if ((offset >= 0x0010 && offset < 0x1EF0) ||
        (offset >= 0x22F8 && offset < 0x2390)) return 0xFF;
    if (offset == 0x23AC) return 0xFE; // index 0 unverified
    if (offset == 0x23DF) return 0x0F; // indices 412..415 unverified
    if (offset > 0x23AC && offset < 0x23DF) return 0xFF;
    return 0;
}

} // namespace kh2coop
