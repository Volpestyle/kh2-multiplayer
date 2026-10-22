#pragma once
// VUH-2640 Leg A (docs/FENCED_FRIEND_BOSS_ARENA.md): reviewed boss arenas into which the host may carry a
// fenced returning seat, sitting out. Each row is one exact six-field location and the boss the host alone
// owns there. The host attests the row in its snapshot (ResyncBossSitOut); nothing about the boss is
// captured, and the friend's own load creates its own copy, never bound to the host. Rows are decimal.
#include <array>
#include <cstdint>
#include <string_view>

namespace kh2coop::bossarena {

struct Arena {
    std::uint16_t id;                              // wire identity, 1-based; 0 is none
    std::array<std::uint16_t, 6> location;         // world, room, door, map, battle, event
    std::uint32_t bossObjectId, bossObjectType;
    std::string_view missionFile;                  // the host's own native mission while the boss is fought
    std::string_view name;
};
// tt34 battle program 157 starts TT34_MS304 and spawns object 307 (B_EX100) as it loads (docs/TWILIGHT_THORN.md
// installed evidence; host location 2/34/0/157/157/157 in bots05 attempt01, 20261022-080847).
inline constexpr std::array<Arena, 1> Arenas {{
    {1, {2, 34, 0, 157, 157, 157}, 307, 12, "TT34_MS304.bar", "tt34-twilight-thorn"},
}};

inline constexpr const Arena* Find(std::uint16_t world, std::uint16_t room, std::uint16_t door, std::uint16_t map,
                                   std::uint16_t battle, std::uint16_t event) noexcept {
    for (const auto& a : Arenas)
        if (a.location == std::array<std::uint16_t, 6> {world, room, door, map, battle, event}) return &a;
    return nullptr;
}
inline constexpr const Arena* ById(std::uint16_t id) noexcept {
    for (const auto& a : Arenas)
        if (a.id == id) return &a;
    return nullptr;
}

} // namespace kh2coop::bossarena
