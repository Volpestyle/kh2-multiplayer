#pragma once
// ============================================================================
// Prologue seats (VUH-2464): each seat's selected profile, its acknowledgement
// of the whole roster's profiles, and the first-field barrier that releases
// control only when every seat has built both gameplay actors in the same room.
// Pure policy: the DLL feeds it SeatState packets (Protocol.hpp) and its own
// construction result; offline checks live in tests/PrologueSeatsTest.cpp.
// ============================================================================
#include "kh2coop/PlayerKits.hpp"
#include "kh2coop/Protocol.hpp"

#include <array>
#include <cstdint>

namespace kh2coop::prologue {

// Roxas+Roxas is this slice's first bounded proof: only Roxas is admitted to the
// prologue layout. Every other profile refuses with a reason until it has its own
// fixture (VUH-1512 Sora, VUH-1514 Donald/Goofy).
inline constexpr std::uint16_t DEFAULT_PROFILE = 0x5A;
inline bool ProfileQualified(std::uint16_t profile) {
    const auto* kit = qualifiedKit(profile);
    return kit && kit->member == 0x5A;
}

enum class Hold : std::uint8_t {
    None,              // released: both seats bound here
    NoSession,         // no armed network session
    Solo,              // only this seat is connected
    TooMany,           // the prologue layout is two players only
    MissingProfile,    // a connected seat has not declared a profile
    UnqualifiedProfile,// a declared profile is not admitted to the prologue
    NotAcknowledged,   // a seat has not acknowledged this roster's profiles
    LocalUnbound,      // this seat's construction here did not bind (or has not run)
    RemoteUnbound,     // the other seat has not reported its first-field binding here
};
inline const char* HoldName(Hold h) {
    switch (h) {
    case Hold::None: return "released";
    case Hold::NoSession: return "no-session";
    case Hold::Solo: return "solo";
    case Hold::TooMany: return "too-many-seats";
    case Hold::MissingProfile: return "missing-profile";
    case Hold::UnqualifiedProfile: return "unqualified-profile";
    case Hold::NotAcknowledged: return "not-acknowledged";
    case Hold::LocalUnbound: return "local-unbound";
    case Hold::RemoteUnbound: return "remote-unbound";
    }
    return "?";
}

struct Location {
    std::uint16_t world = 0xFFFF, room = 0xFFFF, evt = 0xFFFF;
    friend bool operator==(const Location&, const Location&) = default;
};

struct Seat {
    bool valid = false;
    std::uint32_t seq = 0;
    std::uint16_t profile = 0;
    std::array<std::uint16_t, 3> latched {};
    SeatPhase phase = SeatPhase::Declared;
    Location location {};
};

struct Seats {
    std::uint32_t generation = 0;
    std::array<std::uint64_t, 3> roster {};
    std::uint8_t localSlot = 0xFF;
    std::array<Seat, 3> seat {};
};

inline unsigned Present(const std::array<std::uint64_t, 3>& roster) {
    return (roster[0] != 0) + (roster[1] != 0) + (roster[2] != 0);
}

// A new generation, roster or local slot forgets every report: a seat's
// acknowledgement belongs to one roster. True when anything was forgotten.
inline bool Rebase(Seats& s, std::uint32_t generation, const std::array<std::uint64_t, 3>& roster, std::uint8_t localSlot) {
    if (s.generation == generation && s.roster == roster && s.localSlot == localSlot) return false;
    s = {};
    s.generation = generation; s.roster = roster; s.localSlot = localSlot;
    return true;
}

// An admitted (relay-stamped) report for the current roster. Older or repeated
// sequence numbers from the same seat are ignored.
inline bool Accept(Seats& s, const SeatState& m) {
    if (!s.generation || m.ownerSlot > 2 || m.roster != s.roster || !s.roster[m.ownerSlot] || !validSeatPhase(m.phase)) return false;
    auto& seat = s.seat[m.ownerSlot];
    if (seat.valid && m.seq <= seat.seq) return false;
    seat.valid = true; seat.seq = m.seq; seat.profile = m.profile; seat.latched = m.latched;
    seat.phase = m.phase; seat.location = {m.worldId, m.roomId, m.eventProgram};
    return true;
}

// Every connected seat's declared profile, 0 for an empty slot. False while a
// connected seat has not declared.
inline bool Vector(const Seats& s, std::array<std::uint16_t, 3>& out) {
    out = {};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!s.roster[i]) continue;
        if (!s.seat[i].valid || !s.seat[i].profile) return false;
        out[i] = s.seat[i].profile;
    }
    return true;
}

// Why the two-player prologue layout cannot use this roster's profiles yet
// (None when it can). Construction and release both start here.
inline Hold Agreement(const Seats& s) {
    if (!s.generation || s.localSlot > 2 || !s.roster[s.localSlot]) return Hold::NoSession;
    const unsigned n = Present(s.roster);
    if (n < 2) return Hold::Solo;
    if (n > 2) return Hold::TooMany;
    std::array<std::uint16_t, 3> v {};
    if (!Vector(s, v)) return Hold::MissingProfile;
    for (std::size_t i = 0; i < 3; ++i) if (s.roster[i] && !ProfileQualified(v[i])) return Hold::UnqualifiedProfile;
    for (std::size_t i = 0; i < 3; ++i)
        if (s.roster[i] && (s.seat[i].phase == SeatPhase::Declared || s.seat[i].latched != v)) return Hold::NotAcknowledged;
    return Hold::None;
}

// The one remote seat's profile for the clone this seat builds.
inline bool RemoteProfile(const Seats& s, std::uint16_t& profile, Hold& reason) {
    profile = 0;
    reason = Agreement(s);
    if (reason != Hold::None) return false;
    for (std::size_t i = 0; i < 3; ++i)
        if (i != s.localSlot && s.roster[i]) { profile = s.seat[i].profile; return true; }
    reason = Hold::Solo;
    return false;
}

// Control at `here` (a qualified prologue field) is released only when the
// profiles are agreed and acknowledged, this seat bound both actors here, and
// every other seat reported the same binding at the same location.
inline Hold Release(const Seats& s, const Location& here, bool localBound) {
    const Hold agreed = Agreement(s);
    if (agreed != Hold::None) return agreed;
    if (!localBound) return Hold::LocalUnbound;
    for (std::size_t i = 0; i < 3; ++i) {
        if (i == s.localSlot || !s.roster[i]) continue;
        if (s.seat[i].phase != SeatPhase::Bound || !(s.seat[i].location == here)) return Hold::RemoteUnbound;
    }
    return Hold::None;
}

} // namespace kh2coop::prologue
