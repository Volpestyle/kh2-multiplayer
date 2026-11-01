#pragma once
// VUH-2916: exact-boss victories. Each row is a boss the host's own mission ends with its own end trap,
// and the in-field victory event that end starts. The friend never replays that trap through the generic
// mission selector (docs/MISSION_MIRROR.md, "Other ends"); the codec and the mission mirror admit and seal
// a boss end only for a row's mission file. Thorn is the only row. Axel I and II join as rows once PC1
// reads their installed end traps and victory events (docs/testing/vuh2916-pc-read/README.md). Unlike
// bossarena::Arenas, a row grants no sit-out. Rows are decimal.
#include <array>
#include <cstdint>
#include <string_view>

namespace kh2coop::bossvictory {

struct Victory {
    std::array<std::uint16_t, 6> location;   // world, room, door, map, battle, event
    std::uint32_t bossObjectId, bossObjectType;
    std::string_view missionFile;            // a literal: the mission mirror compares its NUL-terminated data()
    std::uint16_t missionId;                 // the battle program's mission opcode argument
    std::uint8_t endTable, endTrap;          // the trap the mission's defeat state calls, with endArg
    std::uint32_t endArg;
    std::uint32_t eventId;                   // the victory event that end starts in the event program
    std::uint8_t eventFlags;
    std::string_view name;
};
// tt34.ard battle program 157 runs mission 181 (TT34_MS304) against object 307 (B_EX100). Its ms_boss state 2
// calls 4/16 (mission_dead_boss) with -1, and event program 157 maps signal 16 to event 0x353133 ("315"),
// flags 0x12 (docs/TWILIGHT_THORN.md installed evidence; docs/PARTY_SETUP.md "Thorn victory").
inline constexpr std::array<Victory, 1> Victories {{
    {{2, 34, 0, 157, 157, 157}, 307, 12, "TT34_MS304.bar", 181, 4, 16, 0xFFFFFFFF, 0x353133, 0x12, "tt34-twilight-thorn"},
}};
inline constexpr const Victory& TwilightThorn = Victories[0];

inline constexpr const Victory* ByMission(std::string_view missionFile) noexcept {
    for (const auto& v : Victories)
        if (v.missionFile == missionFile) return &v;
    return nullptr;
}

} // namespace kh2coop::bossvictory
