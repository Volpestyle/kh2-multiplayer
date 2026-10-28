// VUH-2912 (lead #76, run vuh2912-hp-09) offline controls for inject/src/ReactionCalls.hpp, the 0x3D6FC0 calls one
// hit witness counts and its hit-frame lookback, and inject/src/StatsMark.hpp, the runner's request to log the
// mirror's stats at a mark. Pure bookkeeping only; no game process.
#include "ReactionCalls.hpp"
#include "StatsMark.hpp"

#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <type_traits>

using namespace kh2coop::inject;
namespace rc = kh2coop::inject::reactioncalls;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

constexpr std::uintptr_t kVictim = 0x7FF7ABE7E050, kAxel = 0x7FF7ABE98DA0;
constexpr std::uintptr_t kSora = 0x7FF7AC000100, kShadow = 0x7FF7AC000200, kAxelEntry = 0x7FF7AC000300;  // objentries

int main() {
    // A window counts every call (all) and, on its victim, calls, requests (any id but 0, an unread id too) and
    // applied calls; other actors only add to all.
    {
        rc::Window w = rc::Arm(kVictim, kSora);
        rc::Count(w, kVictim, kSora, 0, false);
        rc::Count(w, kVictim, kSora, 7, true);
        rc::Count(w, kVictim, kSora, rc::kNone, false);
        rc::Count(w, kAxel, kAxelEntry, 3, true);
        rc::Count(w, 0, 0, 0, false);
        CHECK(w.all == 5 && w.calls == 3 && w.requests == 2 && w.applied == 1);
        CHECK(rc::Available(w) && w.state == rc::Identity::Ok);
    }
    // The hook never ran in the window, or ran only on others.
    {
        rc::Window w = rc::Arm(kVictim, kSora);
        CHECK(w.all == 0 && w.calls == 0 && rc::Available(w));
        rc::Count(w, kAxel, kAxelEntry, 0, false);
        CHECK(w.all == 1 && w.calls == 0 && rc::Available(w));
    }
    // Ione (10d8b494) eviction: arm at victim count 3, call many other actors, then call the victim fewer, as many
    // and more times than that baseline. Nothing is evicted and no earlier count is subtracted: the window holds
    // exactly the calls after arming.
    for (std::uint32_t after : {2u, 3u, 4u}) {
        rc::Window before = rc::Arm(kVictim, kSora);
        for (int i = 0; i < 3; ++i) rc::Count(before, kVictim, kSora, 0, false);  // the calls before this witness
        rc::Window w = rc::Arm(kVictim, kSora);
        for (std::uintptr_t a = 0x1000; a < 0x1000 + 200; ++a) rc::Count(w, a, kShadow, 0, false);
        for (std::uint32_t i = 0; i < after; ++i) rc::Count(w, kVictim, kSora, 0, false);
        CHECK(w.calls == after && w.all == 200 + after && rc::Available(w));
    }
    // Ione (10d8b494) address reuse: another actor, or an unread one, at the victim's pointer during the window
    // makes the victim's counts unavailable, never mixed in.
    for (std::uintptr_t other : {kShadow, std::uintptr_t {0}}) {
        rc::Window w = rc::Arm(kVictim, kSora);
        rc::Count(w, kVictim, kSora, 0, false);
        rc::Count(w, kVictim, other, 40, true);
        CHECK(w.state == rc::Identity::Changed && !rc::Available(w));
        rc::Count(w, kVictim, kSora, 0, false);  // a later call with the armed identity does not restore it
        CHECK(!rc::Available(w));
    }
    // A victim whose identity was unread at arm has no available counts.
    {
        rc::Window w = rc::Arm(kVictim, 0);
        rc::Count(w, kVictim, 0, 0, false);
        CHECK(w.state == rc::Identity::Unread && !rc::Available(w) && w.all == 1);
    }

    // Lookback: the victim's reaction applied in the hit frame before the witness armed.
    {
        rc::Recent r;
        rc::Applied a;
        rc::Note(r, kVictim, kSora, 6, 1, 99);
        CHECK(rc::Before(r, kVictim, kSora, 100, a) == rc::Lookback::None);  // only the hit frame counts
        rc::Note(r, kVictim, kSora, 7, 2, 100);
        rc::Note(r, kVictim, kSora, 8, 3, 100);
        rc::Note(r, kAxel, kAxelEntry, 9, 4, 100);
        CHECK(rc::Before(r, kVictim, kSora, 100, a) == rc::Lookback::Found && a.kind == 3 && a.request == 8);
        CHECK(rc::Before(r, kVictim, kSora, 101, a) == rc::Lookback::None);  // a hit frame that applied nothing
    }
    // Ione (10d8b494) address reuse: actor A applies request 40 / kind 2 at frame 100, B later lives at that address
    // and is hit at frame 200 with only no-request calls. A's reaction is not B's.
    {
        rc::Recent r;
        rc::Applied a;
        rc::Note(r, kVictim, kShadow, 40, 2, 100);
        CHECK(rc::Before(r, kVictim, kSora, 200, a) == rc::Lookback::None);
        // In the hit frame itself, another or an unread identity at the victim's pointer is ambiguous.
        rc::Note(r, kVictim, kShadow, 40, 2, 200);
        CHECK(rc::Before(r, kVictim, kSora, 200, a) == rc::Lookback::Unavailable);
        rc::Recent u;
        rc::Note(u, kVictim, 0, 40, 2, 200);
        CHECK(rc::Before(u, kVictim, kSora, 200, a) == rc::Lookback::Unavailable);
        CHECK(rc::Before(u, kVictim, 0, 200, a) == rc::Lookback::Unavailable);  // the victim's own identity unread
    }
    // Ring capacity: the victim's applied call first in its frame, then other applied calls to fewer than, exactly
    // and more than the ring holds. Past the ring the frame is unavailable, never "none".
    for (unsigned total : {rc::kRecent - 1, rc::kRecent, rc::kRecent + 1}) {
        rc::Recent r;
        rc::Applied a;
        rc::Note(r, kVictim, kSora, 7, 2, 300);
        for (unsigned i = 1; i < total; ++i) rc::Note(r, 0x1000 + i, kShadow, 1, 1, 300);
        const rc::Lookback got = rc::Before(r, kVictim, kSora, 300, a);
        CHECK(total <= rc::kRecent ? got == rc::Lookback::Found && a.request == 7 : got == rc::Lookback::Unavailable);
    }
    // A new frame restarts the overflow count.
    {
        rc::Recent r;
        rc::Applied a;
        for (unsigned i = 0; i < rc::kRecent + 4; ++i) rc::Note(r, 0x1000 + i, kShadow, 1, 1, 400);
        CHECK(rc::Before(r, kVictim, kSora, 400, a) == rc::Lookback::Unavailable);
        rc::Note(r, kVictim, kSora, 5, 1, 401);
        CHECK(rc::Before(r, kVictim, kSora, 401, a) == rc::Lookback::Found && a.request == 5);
    }

    // Stats marks: the layout run.py writes, on every platform (Ione 10d8b494: `long` made it 24 bytes on macOS),
    // and one log per new request.
    {
        static_assert(std::is_same_v<std::remove_volatile_t<decltype(statsmark::Channel::requested)>, std::int32_t>,
                      "requested is 32-bit everywhere");
        static_assert(std::is_same_v<std::remove_volatile_t<decltype(statsmark::Channel::logged)>, std::int32_t>,
                      "logged is 32-bit everywhere");
        static_assert(sizeof(statsmark::Channel) == 16, "16 bytes");
        static_assert(offsetof(statsmark::Channel, magic) == 0, "magic");
        static_assert(offsetof(statsmark::Channel, version) == 4, "version");
        static_assert(offsetof(statsmark::Channel, requested) == 8, "requested");
        static_assert(offsetof(statsmark::Channel, logged) == 12, "logged");
        statsmark::Channel c {statsmark::kMagic, statsmark::kVersion, 0, 0};
        CHECK(!statsmark::Due(c, 0));
        c.requested = 1;
        CHECK(statsmark::Due(c, 0));
        CHECK(!statsmark::Due(c, 1));
        c.magic = 0;
        CHECK(!statsmark::Due(c, 0));
    }

    if (failures) std::fprintf(stderr, "%d failure(s)\n", failures);
    else std::printf("reaction calls and stats marks: all checks passed\n");
    return failures ? 1 : 0;
}
