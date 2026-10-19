#pragma once
// VUH-2359: host-selected native scene playback. A qualified scene is an event-only location the host
// loads in the middle of its own event (an event program that never reaches field play). The host
// announces it like any room; a friend held for that event loads the same location natively, plays the
// scene with its own scripted camera and advances its dialog itself, and follows the host's next room
// from the scene's own exit as usual (client authority blocks the friend's own exit). Each row is an
// observed host location of the Roxas prologue chain (runs 20261018-215915, 20261019-055604); anything
// else keeps the event hold. The TT basement rows are the host's post-Axel chain in run 20261017-035405,
// logged in hex as 02/14 Axel -> 02/13 135 -> 12/03 83 -> 02/13 136 -> field 02/13 door 50 (world 0x12 is
// The World That Never Was, room 0x13 the Basement Hall), where the friend used to wait about 48 s at its
// own blocked exit to 02/13 135; the friend follows the whole chain natively in run 20261019-082117.
// Rows are decimal.
#include "kh2coop/Protocol.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace kh2coop::eventscenes {

struct Scene {
    std::uint16_t world, room, door, map, battle, event;
    std::string_view name;
};
inline constexpr std::array<Scene, 6> Scenes {{
    {2, 7, 0, 0, 0, 3, "tt07-armor-shop"},           // event 104: Tram Common evt95 -> evt97
    {2, 7, 0, 0, 0, 4, "tt07-accessory-shop"},       // event 106: evt97 -> evt99
    {2, 4, 0, 81, 81, 81, "tt04-cat-lock-on"},       // event 108: Tram Common evt99 -> Sandlot evt76
    {2, 19, 0, 135, 135, 135, "tt13-after-axel"},    // basement hall 0x13: after the Axel fight (0x14, 137)
    {18, 3, 0, 83, 83, 83, "twtnw03-after-axel"},    // The World That Never Was (0x12) scene
    {2, 19, 0, 136, 136, 136, "tt13-after-twtnw"},   // back to the hall, then field 0x13 door 50
}};

inline const Scene* Qualified(std::uint16_t world, std::uint16_t room, std::uint16_t door, std::uint16_t map,
                              std::uint16_t battle, std::uint16_t event) noexcept {
    for (const auto& s : Scenes)
        if (s.world == world && s.room == room && s.door == door && s.map == map && s.battle == battle && s.event == event)
            return &s;
    return nullptr;
}
inline const Scene* Qualified(const RoomTransition& t) noexcept {
    return Qualified(t.worldId, t.roomId, t.door, t.mapProgram, t.battleProgram, t.eventProgram);
}

}  // namespace kh2coop::eventscenes
