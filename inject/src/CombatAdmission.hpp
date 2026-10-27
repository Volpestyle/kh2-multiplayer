#pragma once
// ============================================================================
// VUH-2897: combat-admission barrier, owner-thread state machine.
//
// In Axel I's room the host's destination intro releases frozen groups 0 and 1
// (3AC750 teardown -> 3BF2E0(0) from 3ACC03, 3BF2E0(1) from 3ACC0D) about 5.7 s
// before the friend has played its own intro. The barrier defers exactly those
// two native calls on the host, keeping the native freezes already in force,
// and replays them once every admitted friend has acknowledged arrival with
// the host's fresh challenge, or as an explicit failed-barrier fallback.
//
// A frozen group is the actor's objentry type (3B5DC0: 0 PLAYER, 1 FRIEND,
// 3 BOSS, 4 ZAKO), so groups 0/1 hold only Roxas and the party; the intro's
// event context is what held Axel. The lease therefore also freezes BOSS and
// ZAKO (3BF2C0(3/4)) when it defers, owning only the bits it set itself, and
// releases in one owner pass: the deferred 0/1 calls first, then its own 3/4.
//
// Every input comes from the game's own thread (field dispatch fibers, the
// entity-head frame and the raw-input collector share it); the native adapter
// refuses other threads. Terminal transitions are first-writer-wins and keyed
// to the lease incarnation, so an ACK, the deadline, a downing or a reload for
// one lease can never act on another.
//
// The machine never touches native memory: the adapter supplies a sampled
// NativeView and performs the release it is told to perform.
// ============================================================================

#include <array>
#include <cstdint>

namespace kh2coop::inject::combatadmission {

constexpr std::uint64_t DeadlineMs = 15000;
constexpr std::uintptr_t ReleaseGroup0Caller = 0x3ACC03; // return RVA of 3ACBFE call 3BF2E0, ecx=0
constexpr std::uintptr_t ReleaseGroup1Caller = 0x3ACC0D; // return RVA of 3ACC08 call 3BF2E0, ecx=1
constexpr std::uint8_t DeferrableGroups = 0b00011;  // PLAYER, FRIEND: the intro's own freezes
constexpr std::uint8_t OwnableGroups = 0b11000;     // BOSS, ZAKO: frozen by the lease itself
constexpr unsigned MaxSlots = 3;

struct Destination {
    std::uint16_t world = 0, room = 0, door = 0, map = 0, battle = 0, event = 0;
    bool operator==(const Destination&) const = default;
};
// The only admitted handoff: Mansion: Basement Corridor (btl134) -> Axel I.
constexpr Destination AxelTarget {2, 0x14, 0, 137, 137, 137};
inline bool AxelSource(const Destination& d) { return d.world == 2 && d.room == 0x13 && d.battle == 134; }

// Whether the installed event froze groups 0/1 itself. Event install (3AC350, 3AD2D0)
// calls 3BF2C0(0/1) only when its descriptor's flag bit 3 is clear; Unknown when the
// descriptor could not be read, which is treated as freezing.
enum class EventFreeze : std::uint8_t { Unknown, Freezes, Spares };

// Native scalars, sampled by the adapter on the owner thread. Not atomic as a
// set; every decision rechecks the ones it depends on.
struct NativeView {
    bool readable = false;
    std::uint32_t load = 0, transition = 0;
    bool pending = false;            // warp::TransitionPending
    Destination room {};             // NOW
    std::uint64_t eventContext = 0;  // EVENT_CONTEXT, an identity scalar
    EventFreeze eventFreeze = EventFreeze::Unknown; // the installed event's descriptor, read under SEH
    std::int32_t cutscene = 0;
    std::uint32_t mask = 0;          // CONTROLLABLE word 0 (groups 0..31)
    std::uint8_t menu = 0xFF;
    bool inField = false;
};

// Session facts from EnemySync, sampled on the owner thread.
struct SessionView {
    bool host = false;                 // authenticated host role
    std::uint32_t generation = 0;      // world session generation
    std::uint64_t hostDelivery = 0;    // this DLL's delivery serial
    std::array<std::uint64_t, MaxSlots> connection {}, delivery {};
    bool publicationPending = false;   // this load's room is not yet published
    bool hostDowned = false;
    bool peerDowned = false;
};

enum class Phase : std::uint8_t { Idle, Held, Releasing, Done };
enum class Outcome : std::uint8_t {
    None, Arrived, Deadline, SeatLeft, SeatReplaced, SessionRetired, HostDowned, PeerDowned,
    Transition, LoadChanged, Shutdown, NativeCleared,
};
inline const char* OutcomeName(Outcome o) {
    switch (o) {
    case Outcome::None: return "none";
    case Outcome::Arrived: return "all-arrived";
    case Outcome::Deadline: return "deadline";
    case Outcome::SeatLeft: return "seat-left";
    case Outcome::SeatReplaced: return "seat-replaced";
    case Outcome::SessionRetired: return "session-retired";
    case Outcome::HostDowned: return "host-downed";
    case Outcome::PeerDowned: return "peer-downed";
    case Outcome::Transition: return "native-transition";
    case Outcome::LoadChanged: return "load-changed";
    case Outcome::Shutdown: return "shutdown";
    case Outcome::NativeCleared: return "native-cleared";
    }
    return "unknown";
}
// Only Arrived is an admission; every other outcome is a failed barrier that
// returns native control.
inline bool Failed(Outcome o) { return o != Outcome::Arrived && o != Outcome::None; }

enum class AckResult : std::uint8_t {
    Accepted, Duplicate, NoLease, NotHeld, NotPublished, Challenge, Epoch, Room, Seat, Connection, Delivery,
};
inline const char* AckName(AckResult r) {
    switch (r) {
    case AckResult::Accepted: return "accepted";
    case AckResult::Duplicate: return "duplicate";
    case AckResult::NoLease: return "no-lease";
    case AckResult::NotHeld: return "not-held";
    case AckResult::NotPublished: return "before-publication";
    case AckResult::Challenge: return "challenge";
    case AckResult::Epoch: return "epoch";
    case AckResult::Room: return "room";
    case AckResult::Seat: return "seat-not-required";
    case AckResult::Connection: return "connection";
    case AckResult::Delivery: return "delivery";
    }
    return "unknown";
}

struct Seat {
    std::uint64_t connection = 0, delivery = 0;
    bool required = false, acked = false;
};

// Why a decided lease has not released yet (diagnostic), and how it released.
enum class Wait : std::uint8_t { None, Event };
inline const char* WaitName(Wait w) { return w == Wait::Event ? "release-waiting-event" : "none"; }
enum class Via : std::uint8_t { Clear, SparingEvent };
inline const char* ViaName(Via v) { return v == Via::SparingEvent ? "event-spares-party" : "clear"; }

struct Lease {
    std::uint64_t incarnation = 0, challenge = 0;
    std::uint32_t generation = 0, load = 0, transition = 0, epoch = 0;
    std::uint64_t hostDelivery = 0;
    std::uint64_t eventContext = 0;   // the intro whose teardown was deferred
    std::uint64_t deferredMs = 0;     // first deferral; the deadline never moves
    std::uint64_t decidedMs = 0;
    std::uint8_t deferred = 0;        // bit g: 3BF2E0(g) deferred (groups 0/1)
    std::uint8_t owned = 0;           // bit g: set by this lease through 3BF2C0 (groups 3/4); a bit the game
                                      // had already set is never owned and never cleared by the lease
    std::uint8_t released = 0;        // bit g: cleared through the original 3BF2E0
    std::array<Seat, MaxSlots> seats {};
    Phase phase = Phase::Idle;
    Outcome outcome = Outcome::None;
    bool retiredWithoutRelease = false;
    Via via = Via::Clear;
    std::uint8_t waitsLogged = 0;     // bit w: Wait w already logged
};

enum class Decision : std::uint8_t { Forward, Defer };
// Why a candidate call was forwarded (diagnostic; Forward is always safe).
enum class ForwardWhy : std::uint8_t {
    NotCandidate, Disabled, NotArmed, NotHost, Solo, Identity, Room, EventContext, Published, Busy, Order,
};
inline const char* ForwardName(ForwardWhy w) {
    switch (w) {
    case ForwardWhy::NotCandidate: return "not-candidate";
    case ForwardWhy::Disabled: return "disabled";
    case ForwardWhy::NotArmed: return "not-armed";
    case ForwardWhy::NotHost: return "not-host";
    case ForwardWhy::Solo: return "solo";
    case ForwardWhy::Identity: return "identity";
    case ForwardWhy::Room: return "room";
    case ForwardWhy::EventContext: return "event-context";
    case ForwardWhy::Published: return "already-published";
    case ForwardWhy::Busy: return "lease-busy";
    case ForwardWhy::Order: return "order";
    }
    return "unknown";
}

// Release instruction for the adapter: call the original 3BF2E0 for each set
// group bit in ascending order (deferred 0, 1, then owned 3, 4), all in one
// owner pass, then report NoteReleased. Owned bits are never freed while a
// deferred bit is still set: both come from the same action, player first.
struct Action {
    enum Kind : std::uint8_t { None, Release, Retire } kind = None;
    std::uint8_t groups = 0;
    std::uint64_t incarnation = 0;
    Wait wait = Wait::None;           // kind None on a decided lease: what it waits for
    Via via = Via::Clear;
};

class Machine {
public:
    bool enabled = true;

    const Lease& Current() const { return lease_; }
    bool Live() const { return lease_.phase == Phase::Held || lease_.phase == Phase::Releasing; }
    bool Held() const { return lease_.phase == Phase::Held; }

    // Host's own native request (before it is forwarded). Arms only the exact
    // Mansion -> Axel request; any other request disarms.
    void NoteRequest(const Destination& from, const Destination& to, std::uint32_t transition) {
        armed_ = {};
        if (AxelSource(from) && to == AxelTarget) { armed_.transition = transition; armed_.requested = true; }
    }
    // Native load finalizers returned. Binds an armed request to its load and
    // retires (never replays) a lease from an older load: the native load path
    // owns the mask by then (S2), and old releases must not reach a new room.
    Action NoteLoad(std::uint32_t load, std::uint32_t transition, std::uint64_t now) {
        Action action;
        if (Live() && lease_.load != load) {
            if (lease_.phase == Phase::Held) Decide(Outcome::LoadChanged, now);
            action = Retire(now);
        }
        if (armed_.requested && armed_.transition == transition) { armed_.load = load; armed_.bound = true; }
        else armed_ = {};
        return action;
    }

    // Detour decision for one 3BF2E0 call. A Defer allocates or extends the
    // lease; everything else forwards to the native code unchanged.
    Decision OnUnfreeze(std::uint32_t group, std::uintptr_t callerRva, const NativeView& v, const SessionView& s,
                        std::uint64_t now, std::uint64_t freshChallenge, ForwardWhy& why) {
        why = ForwardWhy::NotCandidate;
        const bool first = group == 0 && callerRva == ReleaseGroup0Caller;
        const bool second = group == 1 && callerRva == ReleaseGroup1Caller;
        if (!first && !second) return Decision::Forward;
        if (!enabled) { why = ForwardWhy::Disabled; return Decision::Forward; }
        if (second) {
            // Only the same intro teardown, straight after its group-0 deferral.
            if (lease_.phase != Phase::Held || lease_.deferred != 1 || lease_.load != v.load ||
                lease_.transition != v.transition || lease_.eventContext != v.eventContext || !v.readable) {
                why = Live() ? ForwardWhy::Order : ForwardWhy::NotArmed;
                return Decision::Forward;
            }
            lease_.deferred |= 2;
            return Decision::Defer;
        }
        if (Live()) { why = ForwardWhy::Busy; return Decision::Forward; }
        if (!armed_.bound) { why = ForwardWhy::NotArmed; return Decision::Forward; }
        if (!v.readable || armed_.load != v.load || armed_.transition != v.transition || v.pending) {
            why = ForwardWhy::Identity; return Decision::Forward;
        }
        if (!(v.room == AxelTarget)) { why = ForwardWhy::Room; return Decision::Forward; }
        if (!v.eventContext) { why = ForwardWhy::EventContext; return Decision::Forward; }
        if (!s.host || !s.generation || !s.hostDelivery) { why = ForwardWhy::NotHost; return Decision::Forward; }
        if (!s.publicationPending) { why = ForwardWhy::Published; return Decision::Forward; }
        std::array<Seat, MaxSlots> seats {};
        unsigned required = 0;
        for (unsigned slot = 1; slot < MaxSlots; ++slot) {
            if (!s.connection[slot]) continue;
            seats[slot] = {s.connection[slot], s.delivery[slot], true, false};
            ++required;
        }
        if (!required) { why = ForwardWhy::Solo; return Decision::Forward; }
        if (!freshChallenge) { why = ForwardWhy::Identity; return Decision::Forward; }
        lease_ = {};
        lease_.incarnation = ++incarnations_;
        lease_.challenge = freshChallenge;
        lease_.generation = s.generation;
        lease_.hostDelivery = s.hostDelivery;
        lease_.load = v.load;
        lease_.transition = v.transition;
        lease_.eventContext = v.eventContext;
        lease_.deferredMs = now;
        lease_.deferred = 1;
        lease_.owned = 0; // NoteOwned records what the adapter actually set
        lease_.seats = seats;
        lease_.phase = Phase::Held;
        armed_ = {}; // one lease per issued load; a repeat needs a new request
        return Decision::Defer;
    }

    // Groups 3/4 the adapter should set for a fresh lease: only those clear now.
    std::uint8_t OwnCandidates(const NativeView& v) const {
        return lease_.phase == Phase::Held && lease_.deferred == 1 && !lease_.owned
            ? static_cast<std::uint8_t>(OwnableGroups & ~v.mask) : 0;
    }
    // The adapter set these bits through 3BF2C0 (a pinned, unconditional OR):
    // recorded from what was set, never from a readback.
    bool NoteOwned(std::uint64_t incarnation, std::uint8_t groups) {
        if (incarnation != lease_.incarnation || lease_.phase != Phase::Held || lease_.owned) return false;
        lease_.owned = static_cast<std::uint8_t>(groups & OwnableGroups);
        return true;
    }
    std::uint8_t HeldGroups() const { return static_cast<std::uint8_t>(lease_.deferred | lease_.owned); }

    // S3: the one relaxation of the host's mask==0 gates. True only while this
    // lease holds exactly its own groups after the intro has cleared, in the
    // same load, before publication.
    bool PublicationHoldQualifies(const NativeView& v) const {
        return lease_.phase == Phase::Held && !lease_.epoch && v.readable && !v.pending &&
               v.load == lease_.load && v.transition == lease_.transition && v.room == AxelTarget &&
               v.eventContext == 0 && v.cutscene == 0 && v.mask == HeldGroups() && v.inField &&
               v.menu == 0xFF;
    }
    // Same proof for progress sampling, which runs before publication.
    bool SampleHoldQualifies(const NativeView& v) const { return PublicationHoldQualifies(v); }

    std::uint64_t PublicationChallenge(const NativeView& v) const {
        return PublicationHoldQualifies(v) ? lease_.challenge : 0;
    }
    // HostBeginInstance sent the RoomTransition carrying the challenge.
    bool NotePublished(std::uint32_t epoch, std::uint64_t challenge) {
        if (lease_.phase != Phase::Held || lease_.epoch || !epoch || challenge != lease_.challenge) return false;
        lease_.epoch = epoch;
        return true;
    }

    // An authenticated, relay-stamped arrival ACK from a friend (EnemySync has
    // already proved the envelope source is the slot's current connection and
    // delivery). The lease binds the rest.
    AckResult Ack(std::uint8_t slot, std::uint64_t connection, std::uint64_t delivery, std::uint32_t epoch,
                  const Destination& room, std::uint64_t challenge) {
        if (lease_.phase == Phase::Idle || lease_.phase == Phase::Done) return AckResult::NoLease;
        if (challenge != lease_.challenge) return AckResult::Challenge;
        if (lease_.phase != Phase::Held) return AckResult::NotHeld;
        if (!lease_.epoch) return AckResult::NotPublished;
        if (epoch != lease_.epoch) return AckResult::Epoch;
        if (!(room == AxelTarget)) return AckResult::Room;
        if (slot == 0 || slot >= MaxSlots || !lease_.seats[slot].required) return AckResult::Seat;
        auto& seat = lease_.seats[slot];
        if (connection != seat.connection) return AckResult::Connection;
        if (seat.delivery && delivery != seat.delivery) return AckResult::Delivery;
        if (seat.acked) return AckResult::Duplicate;
        seat.acked = true;
        return AckResult::Accepted;
    }

    // Before the host forwards any native transition (door, Continue, retry):
    // decide while the old native identity still matches, so Service can
    // release on this owner pass.
    void BeforeNativeTransition(std::uint64_t now) {
        if (lease_.phase == Phase::Held) Decide(Outcome::Transition, now);
    }
    void RequestShutdown(std::uint64_t now) {
        if (lease_.phase == Phase::Held) Decide(Outcome::Shutdown, now);
    }

    // Owner service: input collector (also runs during native pause) and the
    // entity-head frame. Decides at most once per lease, then releases when
    // the native identity allows it.
    Action Service(const NativeView& v, const SessionView& s, std::uint64_t now) {
        if (!Live()) return {};
        if (lease_.phase == Phase::Held) {
            const auto outcome = Evaluate(v, s, now);
            if (outcome != Outcome::None) Decide(outcome, now);
        }
        if (lease_.phase != Phase::Releasing) return {};
        if (!v.readable) return {};
        // S2: every field build zeroes CONTROLLABLE before the new room's intro installs
        // (152CD0 -> 39CA10 -> 3BF810, dword 2A171E8), so a lease from an older load is
        // retired without touching any group of the new room.
        if (v.load != lease_.load) return Retire(now);
        // A new event that froze 0/1 itself owns them (the bits are a set, not a count): wait,
        // touching nothing, until its context clears; its own teardown clears 0/1 natively.
        // An event whose descriptor spared the party never set them, and waiting would hold
        // Roxas and Axel for its whole lifetime past the deadline, so release through it.
        Via via = Via::Clear;
        if (v.eventContext != 0) {
            if (v.eventFreeze != EventFreeze::Spares) return Waiting(Wait::Event);
            via = Via::SparingEvent;
        }
        const auto groups = static_cast<std::uint8_t>(v.mask & HeldGroups() & ~lease_.released);
        if (!groups) return Retire(now); // already cleared natively; nothing is ours to clear
        lease_.via = via;
        return {Action::Release, groups, lease_.incarnation, Wait::None, via};
    }
    // The adapter replayed `groups` through the original, in order.
    bool NoteReleased(std::uint64_t incarnation, std::uint8_t groups) {
        if (incarnation != lease_.incarnation || lease_.phase != Phase::Releasing) return false;
        lease_.released |= groups;
        lease_.phase = Phase::Done;
        return true;
    }

    // Diagnostics only: true the first time this lease waits for `w`.
    bool FirstWait(Wait w) {
        const auto bit = static_cast<std::uint8_t>(1u << static_cast<unsigned>(w));
        if (w == Wait::None || lease_.phase != Phase::Releasing || (lease_.waitsLogged & bit)) return false;
        lease_.waitsLogged |= bit;
        return true;
    }
    std::uint8_t MissingSeats() const {
        std::uint8_t missing = 0;
        for (unsigned slot = 1; slot < MaxSlots; ++slot)
            if (lease_.seats[slot].required && !lease_.seats[slot].acked) missing |= static_cast<std::uint8_t>(1u << slot);
        return missing;
    }
    bool Armed() const { return armed_.bound; }

private:
    struct Armed {
        std::uint32_t transition = 0, load = 0;
        bool requested = false, bound = false;
    } armed_;
    Lease lease_;
    std::uint64_t incarnations_ = 0;

    Outcome Evaluate(const NativeView& v, const SessionView& s, std::uint64_t now) const {
        if (v.readable && v.load != lease_.load) return Outcome::LoadChanged;
        if (v.readable && v.transition != lease_.transition) return Outcome::Transition;
        // Native code cleared the deferred player/party groups itself (a room script, or a later
        // event's own teardown): admission is moot, so release the groups the lease still owns.
        if (v.readable && !v.eventContext && !(v.mask & lease_.deferred)) return Outcome::NativeCleared;
        if (!s.generation || s.generation != lease_.generation || s.hostDelivery != lease_.hostDelivery || !s.host)
            return Outcome::SessionRetired;
        for (unsigned slot = 1; slot < MaxSlots; ++slot) {
            const auto& seat = lease_.seats[slot];
            if (!seat.required) continue;
            if (!s.connection[slot]) return Outcome::SeatLeft;
            if (s.connection[slot] != seat.connection || (seat.delivery && s.delivery[slot] != seat.delivery))
                return Outcome::SeatReplaced;
        }
        if (s.hostDowned) return Outcome::HostDowned;
        if (s.peerDowned) return Outcome::PeerDowned;
        if (lease_.epoch && !MissingSeats()) return Outcome::Arrived;
        if (now - lease_.deferredMs >= DeadlineMs) return Outcome::Deadline;
        return Outcome::None;
    }
    // First writer wins; only a held lease can be decided.
    void Decide(Outcome outcome, std::uint64_t now) {
        if (lease_.phase != Phase::Held) return;
        lease_.outcome = outcome;
        lease_.decidedMs = now;
        lease_.phase = Phase::Releasing;
    }
    static Action Waiting(Wait w) {
        Action action;
        action.wait = w;
        return action;
    }
    Action Retire(std::uint64_t now) {
        if (lease_.phase == Phase::Held) Decide(Outcome::LoadChanged, now);
        lease_.retiredWithoutRelease = true;
        lease_.phase = Phase::Done;
        return {Action::Retire, 0, lease_.incarnation};
    }
};

} // namespace kh2coop::inject::combatadmission
