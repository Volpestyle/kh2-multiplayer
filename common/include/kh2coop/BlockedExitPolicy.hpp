#pragma once
// A client's host-led room load issued from its own blocked native exit (pure policy, testable).
//
// While client authority holds, the client's own room requests are blocked and the host's room is
// queued instead. A free client takes it at the safe field gate. A client whose own script asked
// for a room (an event's exit, waiting in its fade) is never at that gate, so it takes the host's
// room from that blocked exit only in the cases below.
#include <cstdint>

namespace kh2coop::blockedexit {

struct Room {
    std::uint16_t world {}, room {}, door {};
};

enum class Issue : std::uint8_t {
    No,                  // wait for the safe field gate
    Matching,            // VUH-2344: the client's own script asked for the host's room
    PrologueSite,        // VUH-2464: the host's room is a checked prologue site (a title-joined friend)
    PrologueSuperseded,  // VUH-2555: in the TT prologue a script that ran ahead asked for another room
};

// blockedThisLoad: the client's own exit was blocked at its current load.
inline Issue FromBlockedExit(bool blockedThisLoad, const Room& blocked, const Room& host, bool hostIsPrologueSite,
                             bool prologueScope) noexcept {
    if (!blockedThisLoad) return Issue::No;
    if (blocked.world == host.world && blocked.room == host.room && blocked.door == host.door) return Issue::Matching;
    if (hostIsPrologueSite) return Issue::PrologueSite;
    if (blocked.world == 2 && host.world == 2 && prologueScope) return Issue::PrologueSuperseded;
    return Issue::No;
}

} // namespace kh2coop::blockedexit
