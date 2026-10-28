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
inline constexpr std::array<Scene, 17> Scenes {{
    {2, 7, 0, 0, 0, 3, "tt07-armor-shop"},           // event 104: Tram Common evt95 -> evt97
    {2, 7, 0, 0, 0, 4, "tt07-accessory-shop"},       // event 106: evt97 -> evt99
    {2, 4, 0, 81, 81, 81, "tt04-cat-lock-on"},       // event 108: Tram Common evt99 -> Sandlot evt76
    {2, 19, 0, 135, 135, 135, "tt13-after-axel"},    // basement hall 0x13: after the Axel fight (0x14, 137)
    {18, 3, 0, 83, 83, 83, "twtnw03-after-axel"},    // The World That Never Was (0x12) scene
    {2, 19, 0, 136, 136, 136, "tt13-after-twtnw"},   // back to the hall, then field 0x13 door 50
    // VUH-2487, run 20261019-113001: entering the hall from the Computer Room loads it with event
    // program 1, whose load starts event "612" (about 8 s), then the hall's Dusk fight at 134.
    {2, 19, 0, 3, 0, 1, "tt13-basement-entry"},
    // VUH-2617, run 20261021-192441 (bot04): the host's chain after Thorn's victory event 315, each an
    // event-only load started by the previous one's native start; the friend's own 315 exit is the first.
    // Logged in hex (rooms) as 02/00 51 -> 02/04 83 -> 02/02 67 -> 02/06 168 -> 02/02 167 -> 02/15 142;
    // programs are logged decimal. Room 0x15 is 21.
    // VUH-2918, run 20261027-114125 (VUH-2763 qualification): after 02/15 142 the host loads 02/01 54,
    // where it plays the me4 movie, then 02/01 59, each event-only (the gameplay gate stays closed and
    // field play lasts only the frame of the next native start). With neither 02/01 load announced, the
    // friend waited at its own blocked exit to 02/01 54 for the rest of the run.
    // VUH-2921 also qualifies the next two observed event loads (run 20261027-125601): the Back Alley
    // reminder to hurry to the Sandlot (native event402), then the Day4 Struggle opening. Both seats
    // should play this story setup. The final 02/05 door50 evt2 is field play, not a qualified scene.
    {2, 0, 0, 51, 51, 51, "tt00-after-thorn"},
    {2, 4, 0, 83, 83, 83, "tt04-after-thorn"},
    {2, 2, 0, 67, 67, 67, "tt02-after-thorn-67"},
    {2, 6, 0, 168, 168, 168, "tt06-after-thorn"},
    {2, 2, 0, 167, 167, 167, "tt02-after-thorn-167"},
    {2, 21, 0, 142, 142, 142, "tt15-after-thorn"},
    {2, 1, 0, 54, 54, 54, "tt01-after-thorn-54"},
    {2, 1, 0, 59, 59, 59, "tt01-after-thorn-59"},
    {2, 3, 50, 2, 0, 2, "tt03-after-thorn-402"},
    {2, 5, 0, 0, 0, 1, "tt05-struggle-opening"},
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

// In-field events: a host event that starts inside a field room (no location to load) through the
// native start FUN_1403AC350(id, flags, params). The host publishes the exact arguments with its event
// hold, and a friend standing in the same field location starts the same event natively, then follows
// the host out of it as from a scene. The initial rows are observed host starts, matched exactly (run
// 20261019-083939: the Armor Shop event, about 70 s, which then loads tt07-armor-shop above; run
// 20261019-103812, VUH-2487: the Accessory Shop event, about 41 s, and the cat lock-on event, about
// 93 s, the other observed in-field starts of the prologue). Thorn's victory row
// below is derived from installed data/native decoding and still needs live proof.
// Anything else keeps the event hold.
struct InFieldEvent {
    std::uint16_t world, room, eventProgram;
    std::uint32_t id;
    std::uint8_t flags;
    std::array<std::uint32_t, 9> params;
    std::string_view name;
    bool hostLed = false;  // a HostLedStageEnds row (VUH-2555)
};
// VUH-2569: installed tt34 evt program157's signal16 selects event "315".
// 3A4BD0/39FBC0 decode the authored exit as world2/room0/door0, programs51.
// Native39FBC0 leaves destination byte3 unwritten; 3A4BD0 copies it into
// params[1]'s high byte. That padding alone is ignored when qualifying this row.
// Publication and replay retain the host's full raw descriptor, including it.
inline constexpr InFieldEvent TwilightThornVictory {
    2,34,157,0x353133,0x12,{2,2,0x00330033,0x33,1,0xFFFFFFFF,3,0,0},"tt34-after-thorn-315"
};
inline constexpr std::array<InFieldEvent, 4> InFieldEvents {{
    {2, 7, 95, 0x343031, 9, {1, 0x702, 0xFFFFFFFF, 0xFFFF, 1, 0xFFFFFFFF, 3, 0, 0}, "tt07-armor-shop-104"},
    {2, 7, 97, 0x363031, 9, {1, 0x702, 0xFFFFFFFF, 0xFFFF, 1, 0xFFFFFFFF, 3, 0, 0}, "tt07-accessory-shop-106"},
    {2, 7, 99, 0x383031, 9, {2, 0x402, 0x510051, 0x51, 1, 0xFFFFFFFF, 3, 0, 0}, "tt07-cat-lock-on-108"},
    TwilightThornVictory,
}};

// VUH-2555: host-led stage ends. Each seat runs a fight stage's script on its own copy of the fight and
// ends it on its own player's combo; a friend's own start of one of these is reported to the host
// (StageEnd), which makes the same start, so the stage ends on the host too and the host's own script
// leads both seats on. They are not InFieldEvents: the host's own start of one is not published for the
// friend to replay (the friend plays its own copy, or keeps the ordinary hold). The Day 1 Sandlot bat duel
// against Seifer: stage 1 (evt77) ends with event "111", which leads to evt78; stage 2 (evt78) ends with
// event "112", which leads to the Woods. Identical starts on both seats (runs 20261020-093747, -100758).
inline constexpr std::array<InFieldEvent, 2> HostLedStageEnds {{
    {2, 4, 77, 0x313131, 9, {2, 0x402, 0x4E004E, 0x4E, 1, 0xFFFFFFFF, 3, 0, 0}, "tt04-duel-stage1-111", true},
    {2, 4, 78, 0x323131, 8, {2, 0xD02, 0x7E007E, 0x7E, 1, 0xFFFFFFFF, 3, 0, 0}, "tt04-duel-stage2-112", true},
}};
inline const InFieldEvent* QualifiedStageEnd(std::uint16_t world, std::uint16_t room, std::uint16_t eventProgram,
                                             std::uint32_t id, std::uint8_t flags,
                                             const std::array<std::uint32_t, 9>& params) noexcept {
    for (const auto& e : HostLedStageEnds)
        if (e.world == world && e.room == room && e.eventProgram == eventProgram && e.id == id && e.flags == flags &&
            e.params == params)
            return &e;
    return nullptr;
}
inline const InFieldEvent* QualifiedStart(std::uint16_t world, std::uint16_t room, std::uint16_t eventProgram,
                                          std::uint32_t id, std::uint8_t flags,
                                          const std::array<std::uint32_t, 9>& params) noexcept {
    auto compared=params;
    const auto& thorn=TwilightThornVictory;
    if(world==thorn.world && room==thorn.room && eventProgram==thorn.eventProgram && id==thorn.id && flags==thorn.flags)
        compared[1]&=0x00FFFFFF;
    for (const auto& e : InFieldEvents)
        if (e.world == world && e.room == room && e.eventProgram == eventProgram && e.id == id && e.flags == flags &&
            e.params == compared)
            return &e;
    return nullptr;
}

// VUH-2555: the host-led row at this field location (world, room, evt), if any: where a seat's own
// replayed attacks must not end the stage on the other seat.
inline const InFieldEvent* HostLedAt(std::uint16_t world, std::uint16_t room, std::uint16_t eventProgram) noexcept {
    for (const auto& e : HostLedStageEnds)
        if (e.world == world && e.room == room && e.eventProgram == eventProgram) return &e;
    return nullptr;
}

}  // namespace kh2coop::eventscenes
