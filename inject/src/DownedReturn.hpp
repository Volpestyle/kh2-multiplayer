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
//   SavePoint        a living player stands at a save point: that seat's settled native reaction
//                    command is the save point's "Save" (55). Any save point counts, so returning to
//                    one and reaching a new one are the same trigger. The save point's own heal does
//                    the rest.
// One return per target episode. No game memory, no Windows calls.
// ============================================================================

#include <cstdint>

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

// Whether this seat has seen a teammate in its current session (any frame, any state). Ends with the
// session.
class SessionTeammates {
public:
    void Step(bool session, unsigned teammatesKnown) noexcept {
        if (!session) seen_ = false;
        else if (teammatesKnown) seen_ = true;
    }
    [[nodiscard]] bool Seen() const noexcept { return seen_; }

private:
    bool seen_ = false;
};

// A room change while logically downed (D17): the player sits out into the next room, still downed,
// unless everyone is down, which is the native game over's case (the lead, VUH-2632 #142: a lone
// player down is everyone down):
//   FullParty    every teammate it saw in the old room was downed too (the host's Continue reloads
//                the room for everyone);
//   Solo         it has never seen a teammate in this session, or there is no session: nobody could
//                return it, so it is resolved, as solo play was before D17.
// A teammate seen earlier in the session but not during this downing is unavailable, not absent: the
// player sits out, as when a teammate was up (Quill R1).
enum class AfterTransition : std::uint8_t { Resolve, SitOut };
enum class Disposition : std::uint8_t { Solo, FullParty, TeammateUp, Unavailable };
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
constexpr const char* DispositionName(Disposition d) noexcept {
    switch (d) {
    case Disposition::Solo: return "solo";
    case Disposition::FullParty: return "full-party";
    case Disposition::TeammateUp: return "teammate-up";
    case Disposition::Unavailable: return "unavailable";
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

// Host side: an encounter defeat stays pending in its room generation until the host is living and
// published, then sends for kEncounterReturnFrames (the latch spaces and bounds the requests).
class PendingEncounter {
public:
    enum class Step : std::uint8_t { Idle, Wait, Send, Dropped, Expired };
    void Arm(std::uint32_t transition, std::uint32_t load) noexcept {
        armed_ = true;
        sending_ = false;
        transition_ = transition;
        load_ = load;
        sendFrames_ = 0;
    }
    Step Tick(std::uint32_t transition, std::uint32_t load, bool livingSettled) noexcept {
        if (!armed_) return Step::Idle;
        if (transition != transition_ || load != load_) { armed_ = false; return Step::Dropped; }
        if (!sending_ && !livingSettled) return Step::Wait;
        sending_ = true;
        if (sendFrames_++ >= kEncounterReturnFrames) { armed_ = false; return Step::Expired; }
        return Step::Send;
    }
    [[nodiscard]] bool Armed() const noexcept { return armed_; }

private:
    bool armed_ = false, sending_ = false;
    std::uint32_t transition_ = 0, load_ = 0, sendFrames_ = 0;
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
