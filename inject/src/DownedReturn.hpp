#pragma once
// ============================================================================
// DownedReturn — pure rules for when a downed teammate who sits out comes back (D17, VUH-2632).
// James: "theyll come back after the boss is beat or the player goes back to the save point"
// ... "or reaches a new save point". On every difficulty, whether or not a partner revive is allowed.
//
//   EncounterDefeat  the host's forced battle ends: BATTLE_STATUS (0x2A11404) goes from 2 (forced,
//                    set by 0x3AB690) to 0 (cleared by 0x3ABAB0) in settled field play, within one
//                    room generation. The host issues it; the downed player returns at 25% HP.
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

// A room change while logically downed (D17): the player sits out into the next room, still downed,
// unless it is a full-party retry: every teammate known was downed too (the host's Continue reloads the
// room for everyone), or there is no teammate at all (solo play).
enum class AfterTransition : std::uint8_t { Resolve, SitOut };
constexpr AfterTransition SitOutAfterTransition(unsigned teammatesKnown, unsigned teammatesDowned) noexcept {
    return teammatesKnown == 0 || teammatesDowned >= teammatesKnown ? AfterTransition::Resolve : AfterTransition::SitOut;
}

// One return per target episode, per slot.
class ReturnLatch {
public:
    bool Take(std::uint8_t slot, std::uint64_t episode) noexcept {
        if (slot >= 3 || !episode || returned_[slot] == episode) return false;
        returned_[slot] = episode;
        return true;
    }
    void Reset() noexcept { returned_[0] = returned_[1] = returned_[2] = 0; }

private:
    std::uint64_t returned_[3] {};
};

} // namespace kh2coop::inject::downedreturn
