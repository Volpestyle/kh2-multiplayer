// VUH-2897: combat-admission lease (inject/src/CombatAdmission.hpp), no native code.
// Ownership, release order and idempotence, every named exit, ACK negative controls,
// the scoped publication proof and the monotonic 15 s deadline.
#include "CombatAdmission.hpp"

#include <cstdio>
#include <initializer_list>

using namespace kh2coop::inject::combatadmission;

namespace {
unsigned checks = 0, failures = 0;
void check(bool pass, const char* what) {
    ++checks;
    if (!pass) { ++failures; std::printf("FAIL %s\n", what); }
}
constexpr Destination Mansion {2, 0x13, 0, 134, 134, 134};
constexpr std::uint32_t Load = 9, Transition = 12;
constexpr std::uint64_t Intro = 0x7000, Challenge = 0xC0FFEE1234ull;

NativeView Intro0() { // inside the destination intro's teardown, before 3AC000 clears its context
    NativeView v;
    v.readable = true; v.load = Load; v.transition = Transition; v.room = AxelTarget;
    v.eventContext = Intro; v.mask = 3; v.inField = true;
    return v;
}
NativeView AfterIntro(std::uint32_t mask = 0b11011) {
    auto v = Intro0(); v.eventContext = 0; v.mask = mask; return v;
}
SessionView Session(unsigned friends = 1) {
    SessionView s;
    s.host = true; s.generation = 4; s.hostDelivery = 70; s.publicationPending = true;
    for (unsigned slot = 1; slot <= friends; ++slot) { s.connection[slot] = 100 + slot; s.delivery[slot] = 200 + slot; }
    return s;
}
void Arm(Machine& m) {
    m.NoteRequest(Mansion, AxelTarget, Transition);
    (void)m.NoteLoad(Load, Transition, 0);
}
// Arms, defers both releases, owns 3/4 and publishes epoch 5 at t=1000.
void Hold(Machine& m, unsigned friends = 1, std::uint64_t challenge = Challenge) {
    Arm(m);
    ForwardWhy why;
    check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(friends), 1000, challenge, why) == Decision::Defer,
          "setup: group 0 deferred");
    check(m.OwnCandidates(Intro0()) == OwnableGroups, "setup: both BOSS/ZAKO bits are free to own");
    check(m.NoteOwned(m.Current().incarnation, OwnableGroups), "setup: lease owns 3/4");
    check(m.OnUnfreeze(1, ReleaseGroup1Caller, Intro0(), Session(friends), 1000, 0, why) == Decision::Defer,
          "setup: group 1 deferred");
    check(m.PublicationChallenge(AfterIntro()) == challenge, "setup: challenge offered for publication");
    check(m.NotePublished(5, challenge), "setup: published epoch 5");
}
bool Acks(Machine& m, std::uint8_t slot, std::uint64_t challenge = Challenge, std::uint32_t epoch = 5) {
    return m.Ack(slot, 100 + slot, 200 + slot, epoch, AxelTarget, challenge) == AckResult::Accepted;
}

void ownership() {
    ForwardWhy why;
    {   Machine m;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::NotArmed, "no armed Mansion->Axel request forwards");
    }
    {   Machine m; Arm(m);
        check(m.OnUnfreeze(0, 0x3ACC0D, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              m.OnUnfreeze(1, ReleaseGroup0Caller, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              m.OnUnfreeze(0, 0x395660, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              m.OnUnfreeze(3, ReleaseGroup0Caller, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              !m.Live(), "same args from another caller, or another arg from a pinned caller, forward");
        check(m.OnUnfreeze(1, ReleaseGroup1Caller, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::NotArmed, "group 1 before group 0 forwards");
        auto v = Intro0(); v.room.battle = 138;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, v, Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::Room, "another room forwards");
        v = Intro0(); v.eventContext = 0;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, v, Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::EventContext, "a release outside an installed event forwards");
        v = Intro0(); v.load = Load + 1;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, v, Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::Identity, "another load forwards");
        v = Intro0(); v.pending = true;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, v, Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::Identity, "a pending transition forwards");
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(0), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::Solo, "solo/offline never arms");
        auto s = Session(); s.host = false;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), s, 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::NotHost, "a client never arms");
        s = Session(); s.publicationPending = false;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), s, 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::Published, "a room already published without a challenge never arms");
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 0, 0, why) == Decision::Forward,
              "no fresh challenge, no lease");
        m.enabled = false;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 0, Challenge, why) == Decision::Forward &&
              why == ForwardWhy::Disabled, "disabled barrier forwards");
    }
    {   Machine m;
        m.NoteRequest({2, 0x12, 0, 1, 133, 1}, AxelTarget, Transition); (void)m.NoteLoad(Load, Transition, 0);
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 0, Challenge, why) == Decision::Forward,
              "a request from another source room never arms");
        m.NoteRequest(Mansion, AxelTarget, Transition); (void)m.NoteLoad(Load, Transition + 1, 0);
        check(!m.Armed(), "a load of another transition unbinds the request");
        m.NoteRequest(Mansion, {2, 0x14, 0, 137, 137, 138}, Transition);
        (void)m.NoteLoad(Load, Transition, 0);
        check(!m.Armed(), "the full destination tuple is pinned");
    }
    {   Machine m; Hold(m);
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 2000, 7, why) == Decision::Forward &&
              why == ForwardWhy::Busy, "a later intro teardown during the hold forwards (its own freeze)");
        auto other = Intro0(); other.eventContext = Intro + 0x10;
        Machine n; Arm(n);
        (void)n.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 0, Challenge, why);
        check(n.OnUnfreeze(1, ReleaseGroup1Caller, other, Session(), 0, 0, why) == Decision::Forward &&
              n.Current().deferred == 1, "group 1 from another event context forwards");
        check(n.OwnCandidates(AfterIntro(0b01011)) == 0b10000, "a BOSS bit the game already set is never owned");
    }
}

void releaseSemantics() {
    {   Machine m; Hold(m);
        check(m.Current().deferred == 3 && m.Current().owned == OwnableGroups && m.HeldGroups() == 0b11011,
              "hold: deferred 0/1 plus owned 3/4");
        auto a = m.Service(AfterIntro(), Session(), 2000);
        check(a.kind == Action::None && m.Held(), "no ACK, before the deadline: still held");
        check(Acks(m, 1), "valid ACK accepted");
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::Duplicate, "duplicate ACK is idempotent");
        a = m.Service(AfterIntro(), Session(), 2100);
        check(a.kind == Action::Release && a.groups == 0b11011 && m.Current().outcome == Outcome::Arrived,
              "all seats arrived: one action releases 0,1 then 3,4");
        a = m.Service(AfterIntro(), Session(), 2101);
        check(a.kind == Action::Release && a.groups == 0b11011, "an unapplied action is offered again, never split");
        check(m.NoteReleased(a.incarnation, a.groups) && m.Current().phase == Phase::Done, "released once");
        check(!m.NoteReleased(a.incarnation, a.groups), "a second release report is refused");
        check(m.Service(AfterIntro(0), Session(), 2200).kind == Action::None, "done lease has no further action");
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::NoLease, "ACK after release is a no-op");
    }
    {   Machine m; Hold(m);
        // Unrelated groups (2, 5) and a game-set BOSS bit are never in an action.
        check(Acks(m, 1), "ack");
        const auto a = m.Service(AfterIntro(0b111111), Session(), 2000);
        check(a.groups == 0b11011, "never touches bits the lease did not defer or own");
    }
    {   Machine m; Hold(m);
        (void)m.Service(AfterIntro(), Session(), 1000 + DeadlineMs);
        check(m.Current().outcome == Outcome::Deadline, "deadline decided first");
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::NotHeld && m.Current().outcome == Outcome::Deadline,
              "ACK after the deadline cannot rewrite the outcome");
    }
    {   Machine m; Hold(m);
        check(Acks(m, 1), "ack before deadline tick");
        (void)m.Service(AfterIntro(), Session(), 1000 + DeadlineMs + 50);
        check(m.Current().outcome == Outcome::Arrived, "an arrival that raced the deadline tick wins, never fabricated");
    }
    {   Machine m; Hold(m);
        auto s = Session(); s.hostDowned = true;
        check(Acks(m, 1), "ack");
        (void)m.Service(AfterIntro(), s, 2000);
        check(m.Current().outcome == Outcome::HostDowned, "downing beats a same-tick ACK; first decision only");
        m.RequestShutdown(2001);
        check(m.Current().outcome == Outcome::HostDowned, "shutdown after a decision does not rewrite it");
    }
    {   Machine m; Hold(m);
        m.RequestShutdown(2000);
        const auto a = m.Service(AfterIntro(), Session(), 1000 + DeadlineMs);
        check(m.Current().outcome == Outcome::Shutdown && a.kind == Action::Release && a.groups == 0b11011,
              "shutdown vs deadline: first decision wins, one ordered release");
    }
}

void exits() {
    struct Case { const char* name; Outcome expected; void (*mutate)(NativeView&, SessionView&, Machine&); std::uint64_t at; };
    const Case cases[] = {
        {"deadline with no ACK", Outcome::Deadline, [](NativeView&, SessionView&, Machine&) {}, 1000 + DeadlineMs},
        {"partial ACK then deadline", Outcome::Deadline, [](NativeView&, SessionView&, Machine& m) { Acks(m, 1); }, 1000 + DeadlineMs},
        {"friend disconnect", Outcome::SeatLeft, [](NativeView&, SessionView& s, Machine&) { s.connection[2] = 0; }, 2000},
        {"replaced seat", Outcome::SeatReplaced, [](NativeView&, SessionView& s, Machine&) { s.connection[1] = 999; }, 2000},
        {"reconnected seat delivery", Outcome::SeatReplaced, [](NativeView&, SessionView& s, Machine&) { s.delivery[2] = 777; }, 2000},
        {"host downing", Outcome::HostDowned, [](NativeView&, SessionView& s, Machine&) { s.hostDowned = true; }, 2000},
        {"peer downing", Outcome::PeerDowned, [](NativeView&, SessionView& s, Machine&) { s.peerDownedEpoch[0] = 5; }, 2000},
        {"session retirement", Outcome::SessionRetired, [](NativeView&, SessionView& s, Machine&) { s.generation = 5; }, 2000},
        {"host delivery retired", Outcome::SessionRetired, [](NativeView&, SessionView& s, Machine&) { s.hostDelivery = 71; }, 2000},
        {"role lost", Outcome::SessionRetired, [](NativeView&, SessionView& s, Machine&) { s.host = false; }, 2000},
        {"new transition", Outcome::Transition, [](NativeView& v, SessionView&, Machine&) { v.transition = Transition + 1; }, 2000},
        {"Continue before forwarding", Outcome::Transition, [](NativeView&, SessionView&, Machine& m) { m.BeforeNativeTransition(2000); }, 2000},
        {"DLL shutdown", Outcome::Shutdown, [](NativeView&, SessionView&, Machine& m) { m.RequestShutdown(2000); }, 2000},
        {"native cleared 0/1", Outcome::NativeCleared, [](NativeView& v, SessionView&, Machine&) { v.mask = 0b11000; }, 2000},
    };
    for (const auto& c : cases) {
        Machine m; Hold(m, 2);
        auto v = AfterIntro(); auto s = Session(2);
        c.mutate(v, s, m);
        const auto before = m.Current().deferredMs;
        const auto a = m.Service(v, s, c.at);
        char label[160];
        std::snprintf(label, sizeof(label), "%s: named outcome", c.name);
        check(m.Current().outcome == c.expected && Failed(c.expected), label);
        std::snprintf(label, sizeof(label), "%s: released through the one ordered path", c.name);
        check(a.kind == Action::Release && a.groups == (v.mask & 0b11011) && m.Current().deferredMs == before, label);
    }
    {   Machine m; Hold(m);
        (void)m.Service(AfterIntro(), Session(), 1000 + DeadlineMs - 1);
        check(m.Held(), "deadline is not reached at 14999 ms");
        (void)m.Service(AfterIntro(), Session(), 1000 + DeadlineMs);
        check(m.Current().outcome == Outcome::Deadline && m.Current().decidedMs - m.Current().deferredMs == DeadlineMs,
              "deadline decided on the first service at 15000 ms from the first deferral");
    }
    {   Machine m; Hold(m, 2);
        check(Acks(m, 1), "first of two seats");
        (void)m.Service(AfterIntro(), Session(2), 5000);
        check(m.Held() && m.Current().deferredMs == 1000 && m.MissingSeats() == 0b100,
              "ACK traffic never extends the deadline; missing seat named");
        check(Acks(m, 2), "second seat");
        (void)m.Service(AfterIntro(), Session(2), 5001);
        check(m.Current().outcome == Outcome::Arrived, "both seats arrived");
    }
    {   // Reload: the native load zeroed the mask (S2); the old lease retires, touching nothing.
        Machine m; Hold(m);
        const auto a = m.NoteLoad(Load + 1, Transition + 1, 3000);
        check(a.kind == Action::Retire && m.Current().retiredWithoutRelease && m.Current().outcome == Outcome::LoadChanged,
              "a new load retires the old lease without a release");
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::NoLease, "old ACK after reload is a no-op");
    }
    {   // Continue decided, release blocked by an event; then the reload lands: retire, never replay.
        Machine m; Hold(m);
        m.BeforeNativeTransition(2000);
        auto v = AfterIntro(); v.eventContext = 0x9000;
        check(m.Service(v, Session(), 2001).kind == Action::None, "release waits while an event is installed");
        v.load = Load + 1;
        check(m.Service(v, Session(), 2002).kind == Action::Retire, "a changed load retires the pending release");
    }
    {   // Repeated 02/14 load: a new incarnation and challenge; the previous lease's ACK cannot release it.
        Machine m; Hold(m);
        (void)m.NoteLoad(Load + 1, Transition + 1, 3000);
        m.NoteRequest(Mansion, AxelTarget, Transition + 2);
        (void)m.NoteLoad(Load + 2, Transition + 2, 3000);
        auto v = Intro0(); v.load = Load + 2; v.transition = Transition + 2;
        ForwardWhy why;
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, v, Session(), 4000, Challenge + 1, why) == Decision::Defer &&
              m.Current().incarnation == 2, "repeated load: new incarnation");
        (void)m.NoteOwned(2, OwnableGroups);
        (void)m.OnUnfreeze(1, ReleaseGroup1Caller, v, Session(), 4000, 0, why);
        auto after = v; after.eventContext = 0; after.mask = 0b11011;
        (void)m.NotePublished(6, m.PublicationChallenge(after));
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::Challenge &&
              m.Ack(1, 101, 201, 6, AxelTarget, Challenge) == AckResult::Challenge,
              "an ACK for lease N cannot release lease N+1");
        check(m.Ack(1, 101, 201, 6, AxelTarget, Challenge + 1) == AckResult::Accepted, "the current challenge admits");
    }
}

void ackNegatives() {
    Machine m; Arm(m);
    ForwardWhy why;
    (void)m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(2), 1000, Challenge, why);
    (void)m.OnUnfreeze(1, ReleaseGroup1Caller, Intro0(), Session(2), 1000, 0, why);
    check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::NotPublished, "ACK before publication refused");
    check(!m.NotePublished(5, Challenge + 1), "publication of another challenge does not bind");
    check(m.NotePublished(5, Challenge) && !m.NotePublished(6, Challenge), "publication binds once");
    check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge ^ 1) == AckResult::Challenge, "stale/wrong challenge refused");
    check(m.Ack(1, 101, 201, 4, AxelTarget, Challenge) == AckResult::Epoch, "wrong epoch refused");
    check(m.Ack(1, 101, 201, 5, {2, 0x14, 0, 137, 137, 138}, Challenge) == AckResult::Room, "wrong destination refused");
    check(m.Ack(0, 1, 1, 5, AxelTarget, Challenge) == AckResult::Seat, "the host's own ACK refused");
    check(m.Ack(3, 103, 203, 5, AxelTarget, Challenge) == AckResult::Seat, "non-admitted slot refused");
    check(m.Ack(1, 999, 201, 5, AxelTarget, Challenge) == AckResult::Connection, "wrong connection refused");
    check(m.Ack(1, 101, 999, 5, AxelTarget, Challenge) == AckResult::Delivery, "wrong delivery refused");
    check(m.MissingSeats() == 0b110, "no refused ACK admitted a seat");
    Machine late; Arm(late);
    (void)late.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(1), 1000, Challenge, why);
    (void)late.NotePublished(5, Challenge);
    check(late.Ack(2, 102, 202, 5, AxelTarget, Challenge) == AckResult::Seat,
          "a seat that joined after deferral is not required and cannot satisfy the lease");
    Machine none;
    check(none.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::NoLease && !none.Live(),
          "with no lease an ACK is a no-op and never arms");
}

void publication() {
    Machine m; Arm(m);
    ForwardWhy why;
    (void)m.OnUnfreeze(0, ReleaseGroup0Caller, Intro0(), Session(), 1000, Challenge, why);
    (void)m.NoteOwned(1, OwnableGroups);
    (void)m.OnUnfreeze(1, ReleaseGroup1Caller, Intro0(), Session(), 1000, 0, why);
    check(!m.PublicationHoldQualifies(Intro0()), "not while the intro's context is installed");
    check(m.PublicationHoldQualifies(AfterIntro()) && m.SampleHoldQualifies(AfterIntro()),
          "progress sample then publication admitted under exactly the held groups");
    auto v = AfterIntro(0b11111);
    check(!m.PublicationHoldQualifies(v), "an extra frozen group keeps publication closed");
    v = AfterIntro(0b00011);
    check(!m.PublicationHoldQualifies(v), "a missing owned group keeps publication closed");
    v = AfterIntro(); v.menu = 10;
    check(!m.PublicationHoldQualifies(v), "an open menu keeps publication closed");
    v = AfterIntro(); v.cutscene = 2;
    check(!m.PublicationHoldQualifies(v), "a cutscene keeps publication closed");
    v = AfterIntro(); v.pending = true;
    check(!m.PublicationHoldQualifies(v), "a pending transition keeps publication closed");
    check(m.NotePublished(5, m.PublicationChallenge(AfterIntro())), "publish once");
    check(!m.PublicationHoldQualifies(AfterIntro()) && !m.PublicationChallenge(AfterIntro()),
          "after publication the relaxation closes again (one announcement)");
    Machine idle;
    check(!idle.PublicationHoldQualifies(AfterIntro()), "no lease, no relaxation");
}

void ownedGroups() {
    {   Machine m; Hold(m);
        (void)Acks(m, 1);
        const auto a = m.Service(AfterIntro(0b01011 | 0b10000), Session(), 2000);
        check(a.groups == 0b11011, "player/party first then BOSS/ZAKO in one action");
    }
    {   // A BOSS bit the game set before the lease armed survives release.
        Machine m; Arm(m);
        ForwardWhy why;
        auto pre = Intro0(); pre.mask = 0b01011;
        (void)m.OnUnfreeze(0, ReleaseGroup0Caller, pre, Session(), 1000, Challenge, why);
        const auto own = m.OwnCandidates(pre);
        check(own == 0b10000, "only the free ZAKO bit is a candidate");
        (void)m.NoteOwned(1, own);
        (void)m.OnUnfreeze(1, ReleaseGroup1Caller, pre, Session(), 1000, 0, why);
        auto after = AfterIntro(0b11011);
        check(!m.PublicationHoldQualifies(after),
              "a game-set extra group keeps the strict publication proof closed (deadline fallback)");
        const auto a = m.Service(after, Session(), 1000 + DeadlineMs);
        check(a.kind == Action::Release && a.groups == 0b10011 && m.Current().outcome == Outcome::Deadline,
              "the game's own BOSS bit is never cleared by the lease");
    }
    {   // A new event during the hold blocks every clear, 3/4 included.
        Machine m; Hold(m);
        (void)Acks(m, 1);
        auto v = AfterIntro(); v.eventContext = 0xBEEF;
        check(m.Service(v, Session(), 2000).kind == Action::None, "new event installed: nothing released");
        // Its own teardown clears 0/1 natively; then only the owned 3/4 remain to release.
        v.eventContext = 0; v.mask = 0b11000;
        const auto a = m.Service(v, Session(), 2100);
        check(a.kind == Action::Release && a.groups == 0b11000, "after the event, only the lease's own 3/4 remain");
    }
    {   // Nothing frees 3/4 while 0/1 are held: no action ever carries 3/4 without the set 0/1 bits.
        bool ok = true;
        for (std::uint32_t mask = 0; mask < 32; ++mask) {
            Machine m; Hold(m);
            (void)Acks(m, 1);
            const auto a = m.Service(AfterIntro(mask), Session(), 2000);
            if (a.kind == Action::Release && (a.groups & 0b11000) && ((mask & 3) & ~a.groups)) ok = false;
            if (a.kind == Action::Release && a.groups != (mask & 0b11011)) ok = false;
        }
        check(ok, "for every mask, a release frees 0/1 before or with 3/4, never 3/4 alone while 0/1 stay set");
    }
    {   // Owning is once, for the current incarnation only.
        Machine m; Hold(m);
        check(!m.NoteOwned(m.Current().incarnation, OwnableGroups) && !m.NoteOwned(99, OwnableGroups),
              "owned bits are recorded once, for the current lease");
    }
}

// An event installed during the hold: wait only when it froze 0/1 itself; an event whose
// descriptor spared the party cannot extend the hold past the deadline.
void releaseWaits() {
    auto during = [](EventFreeze freeze) { auto v = AfterIntro(); v.eventContext = 0xBEEF; v.eventFreeze = freeze; return v; };
    {   Machine m; Hold(m);
        const auto v = during(EventFreeze::Spares);
        check(m.Service(v, Session(), 2000).kind == Action::None && m.Held(), "a sparing event does not end the hold early");
        const auto a = m.Service(v, Session(), 1000 + DeadlineMs);
        check(a.kind == Action::Release && a.groups == 0b11011 && a.via == Via::SparingEvent &&
              m.Current().outcome == Outcome::Deadline && m.Current().via == Via::SparingEvent,
              "deadline under an event that spared the party releases 0/1 and the owned 3/4 through it");
        check(m.NoteReleased(a.incarnation, a.groups) && !m.Live(), "released once");
    }
    {   Machine m; Hold(m);
        (void)Acks(m, 1);
        const auto a = m.Service(during(EventFreeze::Spares), Session(), 2000);
        check(a.kind == Action::Release && a.groups == 0b11011 && a.via == Via::SparingEvent,
              "all-arrived under a sparing event releases at once");
    }
    for (const auto kind : {EventFreeze::Freezes, EventFreeze::Unknown}) {
        Machine m; Hold(m);
        auto v = during(kind);
        auto a = m.Service(v, Session(), 1000 + DeadlineMs);
        check(a.kind == Action::None && a.wait == Wait::Event && m.Current().outcome == Outcome::Deadline,
              "an event that froze 0/1 (or an unreadable descriptor) holds the release at the deadline");
        check(m.FirstWait(a.wait) && !m.FirstWait(a.wait), "the event wait is logged once");
        a = m.Service(v, Session(), 1000 + DeadlineMs + 60000);
        check(a.kind == Action::None && a.wait == Wait::Event, "and for as long as the freezing event is installed");
        v.eventContext = 0; v.mask = 0b11000; // its teardown cleared 0/1 natively
        a = m.Service(v, Session(), 1000 + DeadlineMs + 60001);
        check(a.kind == Action::Release && a.groups == 0b11000 && a.via == Via::Clear,
              "after the freezing event only the lease's own 3/4 are released");
    }
}
// VUH-2906: a game over's Continue in Axel I lands at the Computer Room checkpoint (02/15, live on
// 4edbff6) and never arms; walking back reaches Axel through the ordinary Mansion -> Axel request,
// which arms a fresh lease. The Continue's load retires any previous lease (S2), and that lease's
// challenge and epoch never admit the new one.
void routeBack() {
    static constexpr Destination Checkpoint {2, 0x15, 99, 2, 0, 0};
    constexpr std::uint32_t ContinueTransition = Transition + 3, CheckpointLoad = Load + 1;
    constexpr std::uint32_t RetryTransition = Transition + 5, RetryLoad = Load + 2;
    constexpr std::uint64_t RetryChallenge = Challenge + 7;
    auto retryIntro = [] { auto v = Intro0(); v.load = RetryLoad; v.transition = RetryTransition; return v; };
    auto retryAfter = [] { auto v = AfterIntro(); v.load = RetryLoad; v.transition = RetryTransition; return v; };
    auto continueToCheckpoint = [](Machine& m, std::uint64_t at) {
        m.NoteRequest(AxelTarget, Checkpoint, ContinueTransition);
        return m.NoteLoad(CheckpointLoad, ContinueTransition, at);
    };
    auto walkBack = [](Machine& m, std::uint64_t at) {
        m.NoteRequest(Mansion, AxelTarget, RetryTransition);
        return m.NoteLoad(RetryLoad, RetryTransition, at);
    };
    ForwardWhy why;
    {   Machine m;
        m.NoteRequest(AxelTarget, Checkpoint, ContinueTransition);
        check(!m.Requested(), "the Continue to the checkpoint never arms");
        m.NoteRequest(AxelTarget, AxelTarget, RetryTransition);
        check(!m.Requested(), "an Axel -> Axel request never arms");
        m.NoteRequest(Mansion, AxelTarget, RetryTransition);
        check(m.Requested(), "the route back's Mansion -> Axel request arms");
        m.NoteRequest(Mansion, Mansion, RetryTransition + 1);
        check(!m.Requested(), "any later request disarms it");
    }
    {   // First arrival admitted and released; later a game over, Continue and the walk back to Axel.
        Machine m; Hold(m);
        const auto first = m.Current().incarnation;
        (void)Acks(m, 1);
        auto a = m.Service(AfterIntro(), Session(), 2000);
        check(a.kind == Action::Release && m.NoteReleased(a.incarnation, a.groups), "setup: the first lease released");
        check(continueToCheckpoint(m, 20000).kind == Action::None && !m.Armed(), "the checkpoint load is not armed");
        check(walkBack(m, 30000).kind == Action::None && m.Armed(), "the route back's load binds its request");
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, retryIntro(), Session(), 31000, RetryChallenge, why) == Decision::Defer,
              "the route back's intro group 0 release is deferred");
        const auto& l = m.Current();
        check(l.incarnation == first + 1 && l.challenge == RetryChallenge && l.load == RetryLoad &&
              l.transition == RetryTransition && l.deferredMs == 31000 && !l.epoch && !l.owned && l.seats[1].required,
              "a fresh lease: new incarnation, challenge, load, transition and deadline");
        check(m.OwnCandidates(retryIntro()) == OwnableGroups && m.NoteOwned(l.incarnation, OwnableGroups),
              "the route back owns BOSS/ZAKO again");
        check(m.OnUnfreeze(1, ReleaseGroup1Caller, retryIntro(), Session(), 31000, 0, why) == Decision::Defer,
              "and defers group 1");
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::Challenge, "the first lease's ACK is refused");
        check(m.PublicationChallenge(retryAfter()) == RetryChallenge && m.NotePublished(6, RetryChallenge),
              "the route back publishes its own challenge, epoch 6");
        check(m.Ack(1, 101, 201, 5, AxelTarget, RetryChallenge) == AckResult::Epoch &&
              m.Ack(1, 101, 201, 6, AxelTarget, Challenge) == AckResult::Challenge && m.Held(),
              "neither the old epoch nor the old challenge admits the route back");
        check(m.Service(retryAfter(), Session(), 31000 + DeadlineMs - 1).kind == Action::None && m.Held(),
              "the route back's deadline runs from its own deferral");
        a = m.Service(retryAfter(), Session(), 31000 + DeadlineMs);
        check(a.kind == Action::Release && a.groups == 0b11011 && m.Current().outcome == Outcome::Deadline,
              "the route back's deadline releases 0, 1, 3, 4 in one ordered pass");
    }
    {   // A downing stamped with the previous epoch (the game over's) never ends the route back's hold;
        // a downing in its own published epoch does.
        Machine m;
        (void)continueToCheckpoint(m, 20000);
        (void)walkBack(m, 30000);
        (void)m.OnUnfreeze(0, ReleaseGroup0Caller, retryIntro(), Session(), 31000, RetryChallenge, why);
        (void)m.NoteOwned(m.Current().incarnation, OwnableGroups);
        (void)m.OnUnfreeze(1, ReleaseGroup1Caller, retryIntro(), Session(), 31000, 0, why);
        auto s = Session(); s.peerDownedEpoch[0] = 5;
        check(m.Service(retryAfter(), s, 31100).kind == Action::None && m.Held(),
              "a friend downed in the previous epoch does not end the unpublished hold");
        check(m.NotePublished(6, RetryChallenge), "the route back publishes epoch 6");
        check(m.Service(retryAfter(), s, 31200).kind == Action::None && m.Held(),
              "nor the published one");
        s.peerDownedEpoch[1] = 6;
        const auto a = m.Service(retryAfter(), s, 31300);
        check(a.kind == Action::Release && m.Current().outcome == Outcome::PeerDowned,
              "a downing in the route back's own epoch releases it");
    }
    {   // Continue while the first lease is still held: released before the request is forwarded; the
        // route back then arms the second lease.
        Machine m; Hold(m);
        m.BeforeNativeTransition(9000);
        auto v = AfterIntro(); v.transition = ContinueTransition; v.pending = true;
        const auto a = m.Service(v, Session(), 9000);
        check(a.kind == Action::Release && a.groups == 0b11011 && m.Current().outcome == Outcome::Transition &&
              m.NoteReleased(a.incarnation, a.groups), "a held lease is released before the Continue is forwarded");
        check(continueToCheckpoint(m, 10000).kind == Action::None && !m.Armed(), "the Continue lands unarmed");
        check(walkBack(m, 20000).kind == Action::None && m.Armed(), "then the route back arms");
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, retryIntro(), Session(), 21000, RetryChallenge, why) == Decision::Defer &&
              m.Current().incarnation == 2, "and its intro starts the second lease");
    }
    {   // The first lease's release still blocked by an event when the Continue's load lands: S2 retires
        // it without any release, and the route back still arms a fresh lease.
        Machine m; Hold(m);
        m.BeforeNativeTransition(9000);
        auto v = AfterIntro(); v.eventContext = 0x9000;
        check(m.Service(v, Session(), 9000).kind == Action::None, "setup: the first release waits on an event");
        const auto a = continueToCheckpoint(m, 10000);
        check(a.kind == Action::Retire && a.incarnation == 1 && m.Current().retiredWithoutRelease && !m.Armed(),
              "the Continue's load retires the old lease without a release (S2)");
        check(walkBack(m, 20000).kind == Action::None && m.Armed(), "the route back binds");
        check(m.OnUnfreeze(0, ReleaseGroup0Caller, retryIntro(), Session(), 21000, RetryChallenge, why) == Decision::Defer &&
              m.Current().incarnation == 2 && !m.Current().retiredWithoutRelease, "the route back's intro starts a fresh lease");
        check(m.Ack(1, 101, 201, 5, AxelTarget, Challenge) == AckResult::Challenge, "the retired lease's ACK is refused");
    }
}
} // namespace

int main() {
    ownership();
    releaseSemantics();
    exits();
    ackNegatives();
    publication();
    ownedGroups();
    releaseWaits();
    routeBack();
    std::printf("%u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
