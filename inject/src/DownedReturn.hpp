#pragma once
// ============================================================================
// DownedReturn — pure rules for when a downed teammate who sits out comes back (D17, VUH-2632).
// James: "theyll come back after the boss is beat or the player goes back to the save point"
// ... "or reaches a new save point". On every difficulty, whether or not a partner revive is allowed.
//
//   EncounterDefeat  the host's forced battle ends: BATTLE_STATUS (0x2A11404) goes from 2 to 0 in
//                    settled field play, within one room generation. 2 is the script's "command cage"
//                    (0x3AB690: BDX trap_command_cage_on, or a battle program flagged forced); in field it
//                    is cleared by trap_command_cage_off or the battle program's end (both 0x3AB4A0), on a
//                    load by the area reset 0x3ABAB0 (a new generation, so never an edge). The host issues
//                    it once it is living and published; the downed player returns at 25% HP.
//                    Or the host's mission_complete (table-4 trap 3) in its room generation: a wave fight
//                    that is a mission (FM-02's Basement Hall, TT19_MS602) ends in mission_complete and
//                    goes straight into an event and a scripted room change, never clearing its cage in
//                    field (run 20261021-164443). A return that reaches the downed player during that
//                    event is owed to its downing (OwedReturn).
//   SavePoint        a living player stands at a save point: that seat's settled native reaction
//                    command is the save point's "Save" (55). Any save point counts, so returning to
//                    one and reaching a new one are the same trigger. The save point's own heal does
//                    the rest.
// One return per target episode. No game memory, no Windows calls.
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>

#include "kh2coop/PuppetProvenance.hpp"

namespace kh2coop::inject::downedreturn {

enum class Kind : std::uint8_t { None = 0, EncounterDefeat = 1, SavePoint = 2 };
constexpr const char* KindName(Kind k) noexcept {
    switch (k) {
    case Kind::None: return "none";
    case Kind::EncounterDefeat: return "encounter-defeat";
    case Kind::SavePoint: return "save-point";
    }
    return "?";
}

constexpr std::uint8_t kForcedBattle = 2;
constexpr std::uint16_t kSaveReaction = 55; // the save point's "Save" (route pilot SAVE_REACTIONS)

// Host side: one EncounterDefeat per forced battle that ends in field play in the room generation it
// began in. A transition or load forgets it (a boss's own post-fight transition is handled separately).
class EncounterTracker {
public:
    bool Step(std::uint8_t battleStatus, std::uint32_t transition, std::uint32_t load, bool fieldPlay) noexcept {
        if (transition != transition_ || load != load_) {
            transition_ = transition;
            load_ = load;
            forced_ = false;
        }
        if (!fieldPlay) return false;
        if (battleStatus == kForcedBattle) { forced_ = true; return false; }
        if (battleStatus == 0 && forced_) { forced_ = false; return true; }
        return false;
    }
    [[nodiscard]] bool Forced() const noexcept { return forced_; }

private:
    std::uint32_t transition_ = 0, load_ = 0;
    bool forced_ = false;
};

// Host side: the mission mirror's host detour counts mission_complete (trap 3 only, once per mission:
// trap 22's end signal, a failure, a retry's load and a boss end never count). An edge is a count that
// rose between two samples with the same room generation and with no transition or load window open at
// either sample (Quill r3 R3: a window already open, its serials stable, or one that opened and was
// aborted between them, is not proof the complete was in field play's generation). A count not taken is
// consumed: it never becomes a later edge. Not limited to field play: the event that follows can start
// on the next frame (run 20261021-164443: the last Dusk and mission_complete at host frame 5617, the
// event at 5618).
class MissionCompleteTracker {
public:
    bool Step(std::uint32_t completes, std::uint32_t transition, std::uint32_t load, bool windowOpen) noexcept {
        const bool edge = sampled_ && completes != completes_ && transition == transition_ && load == load_ &&
                          !window_ && !windowOpen;
        sampled_ = true;
        completes_ = completes;
        transition_ = transition;
        load_ = load;
        window_ = windowOpen;
        return edge;
    }

private:
    bool sampled_ = false, window_ = false;
    std::uint32_t completes_ = 0, transition_ = 0, load_ = 0;
};

// Target side: a return (an encounter defeat or a save point) that the relay delivered while this downed
// player was in an event, where the owner's native revive refuses (InEvent). The relay and the owner gate
// have consumed the episode by then, so the return is owed to this downing instead of lost: a checked
// receipt of the relay session's identity, kept for the downing's whole life (Quill r3 R1). One per
// downing; a partner revive (kind 0) is never owed; any other refusal stays final.
//   in the room       out of the event, the one revive entry is tried. Its native funnel runs at most
//                     once per receipt: applied, a fault or an incomplete revive finish the receipt (Quill
//                     r3 R2); a refusal before the funnel (not ready yet) is tried again kOwedRetryFrames
//                     later; a wrong state or the spike off can never apply and finish it too;
//   a room change     carried (the room change's decision is "returned"), not turned into the generic
//                     resolve: the player comes back at 25% on the new room's first settled field frame.
//   every use         a known identity equal to the receipt's authorizes it; unknown keeps the receipt and
//                     authorizes nothing; another known identity (0, the session's end, included) forgets
//                     it, or, once carried, revokes it to the room change's own rule (sit out, or resolve
//                     when everyone was down);
//   otherwise         a new downing or the full party's game over clears it; not downed and not carried
//                     (revived some other way) forgets it.
enum class AfterTransition : std::uint8_t { Resolve, SitOut };
constexpr std::uint32_t kOwedRetryFrames = 30;
enum class OwedOutcome : std::uint8_t { Applied, NotReady, Terminal };
enum class OwedAction : std::uint8_t { None, Attempt, Resolve, Forgotten, RevokedSitOut, RevokedResolve };
struct OwedFacts {
    bool identityKnown = false;
    std::uint64_t identity = 0;
    bool downed = false;       // the downing is still held
    bool applyReady = false;   // no event, no menu, no transition or load window
    bool resolveReady = false; // the new room's first settled field frame (the resolve's own conditions)
    std::uint32_t frame = 0;
};
class OwedReturn {
public:
    bool Owe(std::uint8_t kind, bool refusedInEvent, bool identityKnown, std::uint64_t identity) noexcept {
        if (!kind || !refusedInEvent || !identityKnown || !identity || kind_) return false;
        *this = {};
        kind_ = kind;
        identity_ = identity;
        return true;
    }
    // VUH-2656 (v33): a downed client's own receipt from the host's mission end, once HostEndFor says the
    // end is this downing's (see there). The same receipt and rules as a relayed return; one per downing.
    bool OweHostEnd(bool downedHeld, bool identityKnown, std::uint64_t identity) noexcept {
        if (!downedHeld) return false;
        return Owe(static_cast<std::uint8_t>(Kind::EncounterDefeat), true, identityKnown, identity);
    }
    // The room change: an owed return is carried; the plain decision is kept as its fallback.
    bool AtTransition(AfterTransition plain) noexcept {
        if (!kind_) return false;
        carried_ = true;
        fallback_ = plain;
        return true;
    }
    OwedAction Frame(const OwedFacts& f) noexcept {
        if (!kind_) return OwedAction::None;
        if (f.identityKnown && f.identity != identity_) {
            const bool carried = carried_;
            const auto fallback = fallback_;
            *this = {};
            return !carried ? OwedAction::Forgotten
                 : fallback == AfterTransition::SitOut ? OwedAction::RevokedSitOut : OwedAction::RevokedResolve;
        }
        if (!f.identityKnown) return OwedAction::None;
        if (carried_) {
            if (!f.resolveReady) return OwedAction::None;
            *this = {};
            return OwedAction::Resolve;
        }
        if (!f.downed) { *this = {}; return OwedAction::Forgotten; }
        if (!f.applyReady || (waits_ && f.frame - lastWait_ < kOwedRetryFrames)) return OwedAction::None;
        return OwedAction::Attempt;
    }
    void Attempted(OwedOutcome outcome, std::uint32_t frame) noexcept {
        if (outcome != OwedOutcome::NotReady) { *this = {}; return; }
        ++waits_;
        lastWait_ = frame;
    }
    void Clear() noexcept { *this = {}; }
    [[nodiscard]] bool Owed() const noexcept { return kind_ != 0; }
    [[nodiscard]] bool Carried() const noexcept { return carried_; }
    [[nodiscard]] std::uint8_t KindOwed() const noexcept { return kind_; }

private:
    std::uint8_t kind_ = 0;
    std::uint64_t identity_ = 0;
    std::uint32_t waits_ = 0, lastWait_ = 0;
    bool carried_ = false;
    AfterTransition fallback_ = AfterTransition::SitOut;
};
// VUH-2656 (v33): whether the host's admitted mission end (MissionState end trap 3) is this downing's
// encounter. A cutscene that starts on the same frame as mission_complete leaves the host no field frame
// to send a return (run 20261021-174207), so the downed client owes itself the return, but only for the
// end bound to it (Quill r4):
//   the end was admitted while this very downing was held (EntityHook runs this owner before the mission
//   packets each frame, so an end admitted while Ready, followed by a lethal hit later that frame, names
//   no downing and is not the new downing's);
//   no transition or load window at admission, the room generation (transition and load) the downing's own
//   and unchanged since, and the admitted producer (session identity, generation, host connection and
//   delivery) still current, with the session the downing's (a replacement session cannot inherit it);
//   the mission is the one the host was running here when the downing began (captured then; unknown fails
//   closed), and the end's room is the downing's room.
// VUH-2660: where this downing's encounter (the mission an end must match) comes from, at BeginDowned. The
// host's current mission first (its admitted, current MissionState). When no current host packet exists
// yet, a seat downed in a mission's first moments (before the host's first MissionState for it arrives:
// run 20261021-184352, where the encounter was unknown and the return failed closed) takes its own native
// mission at the downing: the client runs the same mission script, so that is the encounter it went down
// in. Neither (no readable, unended native mission either): unknown, which grants nothing. The host's end
// must still match the captured file.
enum class EncounterSource : std::uint8_t { Unknown, Host, Local };
constexpr EncounterSource EncounterAtDowning(bool hostCurrent, bool localReadable) noexcept {
    return hostCurrent ? EncounterSource::Host : localReadable ? EncounterSource::Local : EncounterSource::Unknown;
}
constexpr const char* EncounterSourceName(EncounterSource s) noexcept {
    switch (s) {
    case EncounterSource::Unknown: return "unknown";
    case EncounterSource::Host: return "host";
    case EncounterSource::Local: return "local";
    }
    return "?";
}

enum class HostEndVerdict : std::uint8_t {
    Owes, NotDowned, OtherDowning, Window, OtherGeneration, OtherSession, UnknownEncounter, OtherEncounter, OtherRoom
};
struct HostEndFacts {
    bool downedNow = false;
    std::uint64_t downingNow = 0, downingAtAdmission = 0;
    bool windowAtAdmission = false;
    std::uint32_t transitionNow = 0, transitionAtAdmission = 0, loadNow = 0, loadAtAdmission = 0;
    std::uint32_t generationNow = 0, generationAtAdmission = 0;
    bool sessionKnown = false;
    std::uint64_t sessionNow = 0, sessionAtAdmission = 0;
    bool producerCurrent = false;     // the admitted producer (session, generation, host delivery) still current
    // The downing's own scope, captured at BeginDowned: the end must fall inside it.
    std::uint64_t sessionAtDowning = 0;
    std::uint32_t transitionAtDowning = 0, loadAtDowning = 0;
    bool encounterKnown = false, sameMission = false, sameRoom = false;
};
constexpr HostEndVerdict HostEndFor(const HostEndFacts& f) noexcept {
    if (!f.downedNow || !f.downingNow) return HostEndVerdict::NotDowned;
    if (f.downingAtAdmission != f.downingNow) return HostEndVerdict::OtherDowning;
    if (f.windowAtAdmission) return HostEndVerdict::Window;
    if (f.transitionNow != f.transitionAtAdmission || f.loadNow != f.loadAtAdmission ||
        f.generationNow != f.generationAtAdmission || !f.generationNow) return HostEndVerdict::OtherGeneration;
    if (f.transitionAtAdmission != f.transitionAtDowning || f.loadAtAdmission != f.loadAtDowning)
        return HostEndVerdict::OtherGeneration;
    if (!f.sessionKnown || !f.sessionNow || f.sessionNow != f.sessionAtAdmission || f.sessionAtDowning != f.sessionAtAdmission ||
        !f.producerCurrent) return HostEndVerdict::OtherSession;
    if (!f.encounterKnown) return HostEndVerdict::UnknownEncounter;
    if (!f.sameMission) return HostEndVerdict::OtherEncounter;
    if (!f.sameRoom) return HostEndVerdict::OtherRoom;
    return HostEndVerdict::Owes;
}
constexpr const char* HostEndName(HostEndVerdict v) noexcept {
    switch (v) {
    case HostEndVerdict::Owes: return "owes";
    case HostEndVerdict::NotDowned: return "not-downed";
    case HostEndVerdict::OtherDowning: return "other-downing";
    case HostEndVerdict::Window: return "window";
    case HostEndVerdict::OtherGeneration: return "other-generation";
    case HostEndVerdict::OtherSession: return "other-session";
    case HostEndVerdict::UnknownEncounter: return "unknown-encounter";
    case HostEndVerdict::OtherEncounter: return "other-encounter";
    case HostEndVerdict::OtherRoom: return "other-room";
    }
    return "?";
}

// The adapter's per-frame owed step (DownedSpike Tick, after the room change's decision and before the
// generic resolve): the receipt's decision, then for Attempt the one revive entry, whose outcome
// apply() reports. The adapter performs Resolve and the revocations; tests drive this same function.
template <class Apply> OwedAction OwedTick(OwedReturn& owed, const OwedFacts& f, Apply&& apply) {
    const auto action = owed.Frame(f);
    if (action == OwedAction::Attempt) owed.Attempted(apply(), f.frame);
    return action;
}

// A living seat at a save point (its settled, field-play reaction command).
constexpr bool AtSavePoint(std::uint16_t settledReaction, bool localLiving) noexcept {
    return localLiving && settledReaction == kSaveReaction;
}

// The old room's teammates, as the downed player last saw them there. Observed every downed frame
// before the transition: the transition erases every teammate pose before the hold is found lost, so
// the decision below must not count them at that point (Quill, VUH-2632 R1). A sample with no fresh
// teammate keeps the last one that had some. Reset with each new downing.
class DispositionLatch {
public:
    void Observe(unsigned teammatesKnown, unsigned teammatesDowned) noexcept {
        if (!teammatesKnown) return;
        sampled_ = true;
        known_ = teammatesKnown;
        downed_ = teammatesDowned;
    }
    void Reset() noexcept { *this = {}; }
    [[nodiscard]] bool Sampled() const noexcept { return sampled_; }
    [[nodiscard]] unsigned Known() const noexcept { return known_; }
    [[nodiscard]] unsigned Downed() const noexcept { return downed_; }

private:
    bool sampled_ = false;
    unsigned known_ = 0, downed_ = 0;
};

// Whether this seat has seen a teammate in its current session (any frame, any state), keyed to the
// relay session's identity (Quill R4), not to whether the runtime is live this frame:
//   identity unknown (no bridge)      keep: nothing proves a new session;
//   identity 0                        the session ended: forget;
//   another identity                  a replacement session, even with no Off frame between: start over;
//   the same identity                 the same session, through resyncs and liveness gaps: keep.
// A teammate pose that may enter that history: fresh, and owned by the current puppet authority (its
// generation and connections). EntityHook runs the downed owner before EnemySync's generation drain and
// puppet polling, so on a replacement session's first frame the new identity is visible while the old
// session's pose is still cached; its provenance names the old generation and fails here (Quill R4).
inline bool CountsForSession(bool have, std::uint32_t ageFrames, const PuppetProvenance& tag, std::uint8_t ownerSlot,
                             int puppetIndex, const PuppetAuthority& current) noexcept {
    return have && ageFrames <= 600 && ValidPuppetProvenance(tag, ownerSlot, puppetIndex, current);
}

class SessionTeammates {
public:
    void Step(bool identityKnown, std::uint64_t identity, unsigned teammatesKnown) noexcept {
        if (!identityKnown) return;
        if (identity != identity_) {
            identity_ = identity;
            seen_ = false;
        }
        if (identity_ && teammatesKnown) seen_ = true;
    }
    // The adapter's sample, bookended: the identity read before the authority capture and the owned
    // count, and again after (Quill R4). The runtime retires a session's puppet authority before it
    // publishes the next identity (both interlocked), so an identity unchanged across the reads proves the
    // count belongs to it. Unknown or changed across them: the count is discarded and only the later
    // identity is taken (a replacement still starts over).
    void StepBookended(bool knownBefore, std::uint64_t before, unsigned owned, bool knownAfter, std::uint64_t after) noexcept {
        if (knownBefore && knownAfter && before == after) Step(true, before, owned);
        else Step(knownAfter, after, 0);
    }
    [[nodiscard]] bool Seen() const noexcept { return seen_; }

private:
    std::uint64_t identity_ = 0;
    bool seen_ = false;
};

// The downed host, in its room (D17, the lead's #143): everyone is down when every teammate whose fresh
// pose is in the room is downed too, or when it has never seen a teammate in this session (a lone player
// down). Then the host takes the native game over and Continue. A teammate seen earlier but absent now
// is not "everyone down", unless (VUH-2640, the lead's #176 and #178) every other seat is absent on real
// evidence: it has no connection in the host's current world roster (the relay dropped it: its heartbeat
// expired), or it is a returning seat quarantined until its recovery converges (the host runtime's pending
// returning-seat hint, for that seat's current connection). Pose silence alone never counts: a lagging or
// loading teammate keeps its seat. Unknown evidence (no current roster) counts nothing. Otherwise a downed host that nobody can reach would
// wait forever, and its capture (not controllable) could never return the absent seat either.
struct SeatEvidence {
    bool rosterKnown = false;                 // the host's current world roster (puppet authority, Network)
    std::uint8_t localSlot = 0xFF;
    std::array<bool, 3> connected {};         // a connection in that roster
    std::array<bool, 3> returning {};         // quarantined, rejoining (the runtime's pending hint)
};
// One bound observation (Quill r2 P1-3): the host runtime's returning connections, read before and after
// the puppet-authority capture and unchanged across it, with that capture's Network roster. A slot is
// returning only while the connection its quarantine names is the slot's connection in that roster, so a
// newer connection in the slot never inherits an older one's quarantine. Unbound or not Network: no roster.
inline SeatEvidence SeatEvidenceFrom(bool bound, bool network, std::uint8_t localSlot,
                                     const std::array<std::uint64_t, 3>& roster,
                                     const std::array<std::uint64_t, 3>& returning) noexcept {
    SeatEvidence e {};
    e.rosterKnown = bound && network;
    e.localSlot = localSlot;
    for (std::size_t slot = 0; slot < 3; ++slot) {
        e.connected[slot] = roster[slot] != 0;
        e.returning[slot] = returning[slot] != 0 && returning[slot] == roster[slot];
    }
    return e;
}
constexpr bool EveryOtherSeatAbsent(const SeatEvidence& e) noexcept {
    if (!e.rosterKnown || e.localSlot >= 3) return false;
    for (std::size_t slot = 0; slot < 3; ++slot)
        if (slot != e.localSlot && e.connected[slot] && !e.returning[slot]) return false;
    return true;
}
constexpr bool EveryoneDown(bool teammateHere, bool everyTeammateHereDowned, bool teammateSeenInSession,
                            bool everyOtherSeatAbsent) noexcept {
    return !teammateSeenInSession || (teammateHere && everyTeammateHereDowned) || everyOtherSeatAbsent;
}

// VUH-2640 (F1): a friend whose event-control lifetime expires (HeartbeatExpired) during a host-owned boss
// fight sits out until the fight resolves. There is no mid-fight rejoin: a boss's script and AI state come
// only from host capture and are never invented. After the expiry the retired world session lets the
// friend's local copy of the boss run its own brain, so the friend is held as a D17 logical downing tagged
// outage: it can neither fight and win that unsynced boss nor be killed by it. The proof of a host-owned
// boss is a retained receipt (Quill r1 P1-1): the friend's mirror bound a host-brain-only boss (Thorn) to
// the host's authority within kOutageBossFrames of the expiry, under the session identity and generation
// this seat last had live (the lifetime that expired) and in the room generation (transition and load
// serials) still current; and this seat's own native mission is Thorn's (TT34_MS304). A retired boss from
// another room, load or session never qualifies. VUH-2628's recovery Bootstrap load ends the downing.
// The receipt's frame is the mirror's newest host-brain-only bind, a frame scalar; its scope (identity,
// generation, room serials) is stamped by DownedSpike when it reads that scalar on a live frame, not by the
// mirror at the bind (Quill r2). The comparisons narrow it to binds read under the expiring lifetime; they
// do not prove the producer's own scope at the bind.
constexpr std::uint32_t kOutageBossFrames = 120;
struct BossReceipt {
    std::uint32_t frame = 0;                  // 0: none
    std::uint64_t identity = 0;
    std::uint32_t generation = 0, transition = 0, load = 0;
};
struct OutageExpiryFacts {
    std::uint32_t frame = 0;
    std::uint64_t lastLiveIdentity = 0;       // this seat's session identity while its lifetime was live
    std::uint32_t lastLiveGeneration = 0;
    std::uint32_t transition = 0, load = 0;   // now
    bool thornMission = false;                // this seat's native mission is TT34_MS304, running
};
constexpr bool OutageBossQualified(const BossReceipt& r, const OutageExpiryFacts& f) noexcept {
    return r.frame && f.frame - r.frame <= kOutageBossFrames && r.identity && r.identity == f.lastLiveIdentity &&
           r.generation && r.generation == f.lastLiveGeneration && r.transition == f.transition && r.load == f.load &&
           f.thornMission;
}
// The pending outage downing (the friend was not downed yet at the expiry): begun on the first settled
// frame it can be (the same conditions as a sit-out re-down), only in the room of the expiry and only while
// that lifetime is still the expired one; a room change or a recovered lifetime drops it first (Quill r1:
// a downing in another room or after a recovery is never promoted to the outage). Still pending, same room
// and lifetime, and a lethal hit downed it meanwhile: that downing is the outage (Promote).
enum class OutageStart : std::uint8_t { Idle, Wait, Begin, Promote, Drop };
// A logical hold keeps engine HP at 1. The per-frame undo of a heal that bypassed the stat funnel (an HP orb)
// is computed from the HP read now, after any hold begun earlier in the same frame: never from the frame's
// opening snapshot, which still has the pre-hold HP (VUH-2640 L1: 20 -> 1 by the hold, then 1 - 20 -> 0).
constexpr int HeldHpUndo(bool logical, int hpNow) noexcept { return logical && hpNow > 1 ? 1 - hpNow : 0; }
constexpr OutageStart OutageStartStep(bool pending, bool sameRoom, bool lifetimeExpired, bool downedNow,
                                      bool ready) noexcept {
    if (!pending) return OutageStart::Idle;
    if (!sameRoom || !lifetimeExpired) return OutageStart::Drop;
    if (downedNow) return OutageStart::Promote;
    return ready ? OutageStart::Begin : OutageStart::Wait;
}

// A room change while logically downed (D17): the player sits out into the next room, still downed,
// unless everyone is down, which is the native game over's case (the lead, VUH-2632 #142: a lone
// player down is everyone down):
//   FullParty    every teammate it saw in the old room was downed too (the host's Continue reloads
//                the room for everyone);
//   Solo         it has never seen a teammate in this session, or there is no session: nobody could
//                return it, so it is resolved, as solo play was before D17.
// A teammate seen earlier in the session but not during this downing is unavailable, not absent: the
// player sits out, as when a teammate was up (Quill R1).
enum class Disposition : std::uint8_t { Solo, FullParty, TeammateUp, Unavailable, Returned, Outage };
struct TransitionDecision {
    AfterTransition after;
    Disposition why;
};
inline TransitionDecision SitOutAfterTransition(const DispositionLatch& old, bool teammateSeenInSession) noexcept {
    if (old.Sampled())
        return old.Downed() >= old.Known() ? TransitionDecision {AfterTransition::Resolve, Disposition::FullParty}
                                           : TransitionDecision {AfterTransition::SitOut, Disposition::TeammateUp};
    return teammateSeenInSession ? TransitionDecision {AfterTransition::SitOut, Disposition::Unavailable}
                                 : TransitionDecision {AfterTransition::Resolve, Disposition::Solo};
}
// An owed return comes first: the encounter was won before the room changed. It is carried in the
// receipt (Resolve, "returned"), with the plain decision as the fallback should the session change.
inline TransitionDecision AfterTransitionFor(OwedReturn& owed, const DispositionLatch& old, bool teammateSeenInSession) noexcept {
    const auto plain = SitOutAfterTransition(old, teammateSeenInSession);
    return owed.AtTransition(plain.after) ? TransitionDecision {AfterTransition::Resolve, Disposition::Returned} : plain;
}
// VUH-2640 (F1), round 3 (Quill r2 P1-1/P1-2): the outage is this seat's obligation, not one hold's. It
// begins at the qualified expiry, anchored to the recovery Bootstrap serial then, and outlives every hold
// it spans: an actor, room, handle or ObjEntry lost while it is open sits out again (still the outage). It
// ends only on positive evidence, whatever the hold saw meanwhile:
//   Recovered     a recovery Bootstrap load completed since the expiry (attributed to the host's resync
//                 Bootstrap: an actor and room replaced by it, an actor lost before it completed, or a
//                 Bootstrap into the same room alike): D17's return after a boss, resolved at 25%;
//   SessionEnded  this seat's runtime stopped networking for good (ClientRecovery Terminal: no rejoin
//                 follows), resolved solo, as play without a session.
// An unavailable, expired or retired runtime writer lease is neither: the outage holds.
enum class OutageEnd : std::uint8_t { Open, Recovered, SessionEnded };
class OutageDuty {
public:
    void Begin(std::uint32_t recoveryAtExpiry) noexcept {
        if (!active_) { active_ = true; recoveryAt_ = recoveryAtExpiry; }
    }
    bool Active() const noexcept { return active_; }
    OutageEnd End(std::uint32_t recoveryLoadNow, bool networkTerminal) const noexcept {
        if (!active_) return OutageEnd::Open;
        if (recoveryLoadNow && recoveryLoadNow != recoveryAt_) return OutageEnd::Recovered;
        return networkTerminal ? OutageEnd::SessionEnded : OutageEnd::Open;
    }
    void Clear() noexcept { active_ = false; recoveryAt_ = 0; }
private:
    bool active_ = false;
    std::uint32_t recoveryAt_ = 0;
};
// Where the downed owner stands when it steps an open duty (once per frame, in the adapter's order):
//   Held       a logical downing (holdLost: its actor, room, handle or ObjEntry changed this frame);
//   SitOut     between rooms, waiting to be downed again;
//   Elsewhere  neither: the duty can only end.
enum class OutagePlace : std::uint8_t { Held, SitOut, Elsewhere };
struct OutageStep {
    bool act = false;   // end the hold (Held) or the pending re-down (SitOut) as `decision`
    TransitionDecision decision {AfterTransition::SitOut, Disposition::Outage};
};
inline OutageStep OutageFrame(OutageDuty& duty, std::uint32_t recoveryLoadNow, bool networkTerminal,
                              OutagePlace place, bool holdLost) noexcept {
    if (!duty.Active()) return {};
    const auto end = duty.End(recoveryLoadNow, networkTerminal);
    if (end != OutageEnd::Open) {
        duty.Clear();
        return {place != OutagePlace::Elsewhere,
                {AfterTransition::Resolve, end == OutageEnd::Recovered ? Disposition::Outage : Disposition::Solo}};
    }
    if (place == OutagePlace::Held && holdLost) return {true, {AfterTransition::SitOut, Disposition::Outage}};
    return {};
}
constexpr const char* DispositionName(Disposition d) noexcept {
    switch (d) {
    case Disposition::Solo: return "solo";
    case Disposition::FullParty: return "full-party";
    case Disposition::TeammateUp: return "teammate-up";
    case Disposition::Unavailable: return "unavailable";
    case Disposition::Returned: return "returned";
    case Disposition::Outage: return "outage";
    }
    return "?";
}

// A return asks the relay as this seat, and the relay refuses a requester whose last avatar there is
// downed or at HP 0 (requester-not-alive). So a seat sends only after it has published itself living for
// kLivingSettleFrames in a row (Quill, VUH-2632 R2: a downed host whose teammate wins the encounter
// revives itself first). The relay's copy is not visible here, so a target still downed with the same
// episode is asked again, kReturnRetryFrames apart, at most kReturnAttempts times; the relay and the
// target's owner still accept one return per episode.
constexpr std::uint32_t kLivingSettleFrames = 30;
constexpr std::uint32_t kReturnRetryFrames = 90;
constexpr std::uint8_t kReturnAttempts = 3;
constexpr std::uint32_t kEncounterReturnFrames = kReturnRetryFrames * kReturnAttempts;

class LivingGate {
public:
    void Step(bool livingPublished) noexcept {
        frames_ = !livingPublished ? 0 : frames_ < kLivingSettleFrames ? frames_ + 1 : frames_;
    }
    [[nodiscard]] bool Settled() const noexcept { return frames_ >= kLivingSettleFrames; }

private:
    std::uint32_t frames_ = 0;
};

// One target of an encounter defeat, fixed at the defeat's edge (Quill R3): the seat, the connection in
// it and the downed episode. A later downing, a revive or another player in the seat is not this claim.
struct ReturnClaim {
    std::uint8_t slot = 0xFF;
    std::uint64_t connection = 0, episode = 0;
};
enum class ClaimCheck : std::uint8_t { Send, Keep, Drop };
// The claim against the target's current state: Drop when the seat's connection changed, or the target's
// fresh pose is no longer downed or carries another episode (revived, or downed anew after the victory).
// Keep (wait) while the pose is stale, the target is in another room, or the connection is unknown.
constexpr ClaimCheck CheckClaim(const ReturnClaim& c, bool connectionKnown, std::uint64_t connectionNow, bool freshPose,
                                bool downed, std::uint64_t episode, bool sameRoom) noexcept {
    if (connectionKnown && connectionNow != c.connection) return ClaimCheck::Drop;
    if (!freshPose) return ClaimCheck::Keep;
    if (!downed || episode != c.episode) return ClaimCheck::Drop;
    return connectionKnown && sameRoom ? ClaimCheck::Send : ClaimCheck::Keep;
}

// Host side: an encounter defeat stays pending in its room generation until the host is living and
// published, then sends for kEncounterReturnFrames to the claims taken at its edge only (the latch spaces
// and bounds the requests). It ends early once every claim is dropped.
class PendingEncounter {
public:
    static constexpr unsigned kMaxClaims = 3;
    enum class Step : std::uint8_t { Idle, Wait, Send, Dropped, Expired };
    void Arm(std::uint32_t transition, std::uint32_t load) noexcept {
        armed_ = true;
        sending_ = false;
        transition_ = transition;
        load_ = load;
        sendFrames_ = 0;
        count_ = 0;
    }
    bool Claim(const ReturnClaim& c) noexcept {
        if (!armed_ || sending_ || c.slot >= 3 || !c.connection || !c.episode || count_ >= kMaxClaims) return false;
        for (unsigned i = 0; i < count_; ++i)
            if (claims_[i].slot == c.slot) return false;
        claims_[count_++] = c;
        return true;
    }
    Step Tick(std::uint32_t transition, std::uint32_t load, bool livingSettled) noexcept {
        if (!armed_) return Step::Idle;
        if (transition != transition_ || load != load_) { armed_ = false; count_ = 0; return Step::Dropped; }
        if (!sending_ && !livingSettled) return Step::Wait;
        sending_ = true;
        if (!count_ || sendFrames_++ >= kEncounterReturnFrames) { armed_ = false; count_ = 0; return Step::Expired; }
        return Step::Send;
    }
    // check(claim) -> ClaimCheck; a Drop removes that claim.
    template <class F> void ForEachClaim(F&& check) {
        unsigned kept = 0;
        for (unsigned i = 0; i < count_; ++i)
            if (check(static_cast<const ReturnClaim&>(claims_[i])) != ClaimCheck::Drop) claims_[kept++] = claims_[i];
        count_ = kept;
    }
    [[nodiscard]] bool Armed() const noexcept { return armed_; }
    [[nodiscard]] unsigned Claims() const noexcept { return count_; }

private:
    bool armed_ = false, sending_ = false;
    std::uint32_t transition_ = 0, load_ = 0, sendFrames_ = 0;
    ReturnClaim claims_[kMaxClaims] {};
    unsigned count_ = 0;
};

// Per target slot: the first request for an episode, then a bounded retry while it is still downed.
class ReturnLatch {
public:
    bool Take(std::uint8_t slot, std::uint64_t episode, std::uint32_t frame) noexcept {
        if (slot >= 3 || !episode) return false;
        Entry& e = entries_[slot];
        if (e.episode != episode) { e = {episode, frame, 1}; return true; }
        if (e.attempts >= kReturnAttempts || frame - e.frame < kReturnRetryFrames) return false;
        e.frame = frame;
        ++e.attempts;
        return true;
    }
    void Reset() noexcept { for (auto& e : entries_) e = {}; }

private:
    struct Entry {
        std::uint64_t episode = 0;
        std::uint32_t frame = 0;
        std::uint8_t attempts = 0;
    };
    Entry entries_[3] {};
};

} // namespace kh2coop::inject::downedreturn
