#pragma once
// VUH-2912 witness diagnostics (lead #76, run vuh2912-hp-09): 0x3D6FC0's calls as one hit witness sees them, so it
// can say whether the hook ran at all in its window, ran only on other actors, ran on the victim without a request,
// or applied the victim's reaction in the hit's frame before the witness armed. Pure bookkeeping, no game reads:
// EntityHook.cpp feeds it from the hook and logs it per witness.
//
// No history outlives a witness (Ione, a8cd4f9 CHANGES 10d8b494). The inject has no native incarnation id for an
// actor, so the victim is its pointer plus its objentry pointer (actor+0x918), read when the witness arms and on
// every call at that pointer, with the canonical-player bookends EntityHook checks at arm and log. A call at the
// victim's pointer with another or an unread identity, or a victim identity unread at arm, makes the window's
// victim counts unavailable rather than zero, and so does a victim that is no longer the canonical player or whose
// objentry changed or went unread by the time the witness logs (Close). NOTE (Ione e33b049c): a reuse by an actor of
// the same kind inside the 4-frame window is not detected, so these fields are diagnostic only and never
// reaction or targeting acceptance proof; that would need a verified allocation/lifetime generation. The pre-arm lookback covers only the hit's own frame, from a ring of the last kRecent applied calls; a
// frame that applied more than the ring holds makes it unavailable.
#include <cstdint>

namespace kh2coop::inject::reactioncalls {

constexpr std::uint32_t kNone = UINT32_MAX;

enum class Identity : std::uint8_t { Ok, Unread, Changed };

// Counted while one witness is pending: every call (all), and the calls on its victim.
struct Window {
    std::uintptr_t victim = 0, identity = 0;  // identity 0: unread at arm
    std::uint32_t all = 0, calls = 0, requests = 0, applied = 0;
    Identity state = Identity::Unread;
};

inline Window Arm(std::uintptr_t victim, std::uintptr_t identity) noexcept {
    Window w;
    w.victim = victim;
    w.identity = identity;
    w.state = identity ? Identity::Ok : Identity::Unread;
    return w;
}

// One call: `identity` is the actor's objentry pointer (0 unread), `request` the id read before the call (kNone when
// unread; any id but 0 counts as a request) and `applied` whether it returned an entry.
inline void Count(Window& w, std::uintptr_t actor, std::uintptr_t identity, std::uint32_t request,
                  bool applied) noexcept {
    ++w.all;
    if (!actor || actor != w.victim) return;
    if (w.state == Identity::Ok && identity != w.identity) w.state = Identity::Changed;
    ++w.calls;
    if (request != 0) ++w.requests;
    if (applied) ++w.applied;
}

// The final bookend, when the witness logs (Ione e33b049c): `canonical` is whether the victim's pointer is still the
// canonical player and `identity` its objentry read now (0 unread). An identity lost after the last call never
// reaches Count, so a pointer no longer canonical or an objentry changed gives Changed, and an objentry unread gives
// Unread. An earlier unavailable state stays.
inline void Close(Window& w, bool canonical, std::uintptr_t identity) noexcept {
    if (w.state != Identity::Ok) return;
    if (!canonical || (identity && identity != w.identity)) w.state = Identity::Changed;
    else if (!identity) w.state = Identity::Unread;
}

inline bool Available(const Window& w) noexcept { return w.state == Identity::Ok; }

// The last applied calls, for the lookback into the hit's frame.
constexpr unsigned kRecent = 16;
struct Applied {
    std::uintptr_t actor = 0, identity = 0;
    std::uint32_t frame = kNone, request = kNone;
    std::int32_t kind = -1;
};
struct Recent {
    Applied rows[kRecent] {};
    unsigned next = 0;
    std::uint32_t frame = kNone, inFrame = 0;  // applied calls in the newest frame seen
};

inline void Note(Recent& r, std::uintptr_t actor, std::uintptr_t identity, std::uint32_t request, std::int32_t kind,
                 std::uint32_t frame) noexcept {
    if (frame != r.frame) {
        r.frame = frame;
        r.inFrame = 0;
    }
    ++r.inFrame;
    r.rows[r.next] = {actor, identity, frame, request, kind};
    r.next = (r.next + 1) % kRecent;
}

enum class Lookback : std::uint8_t { None, Found, Unavailable };
// The victim's last reaction applied in `frame` before now: Found (into `out`), None, or Unavailable when its
// identity is unread, that frame applied more calls than the ring holds, or the newest applied call at its pointer
// in that frame carries another or an unread identity.
inline Lookback Before(const Recent& r, std::uintptr_t victim, std::uintptr_t identity, std::uint32_t frame,
                       Applied& out) noexcept {
    if (!victim || !identity) return Lookback::Unavailable;
    if (r.frame == frame && r.inFrame > kRecent) return Lookback::Unavailable;
    for (unsigned i = 1; i <= kRecent; ++i) {
        const Applied& a = r.rows[(r.next + kRecent - i) % kRecent];
        if (a.frame != frame || a.actor != victim) continue;
        if (a.identity != identity) return Lookback::Unavailable;  // another or an unread actor at that pointer
        out = a;
        return Lookback::Found;
    }
    return Lookback::None;
}

} // namespace kh2coop::inject::reactioncalls
