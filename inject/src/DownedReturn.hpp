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
// is not "everyone down".
constexpr bool EveryoneDown(bool teammateHere, bool everyTeammateHereDowned, bool teammateSeenInSession) noexcept {
    return !teammateSeenInSession || (teammateHere && everyTeammateHereDowned);
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
enum class Disposition : std::uint8_t { Solo, FullParty, TeammateUp, Unavailable, Returned };
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
constexpr const char* DispositionName(Disposition d) noexcept {
    switch (d) {
    case Disposition::Solo: return "solo";
    case Disposition::FullParty: return "full-party";
    case Disposition::TeammateUp: return "teammate-up";
    case Disposition::Unavailable: return "unavailable";
    case Disposition::Returned: return "returned";
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
