#pragma once
// ============================================================================
// DownedPolicy — pure rules for who may revive a downed teammate, and how long
// the hold takes, by the session's native difficulty (D17, VUH-2632).
//
// James (2026-10-21): "this easy revive tho should be more for
// testing/automation/beginner level only. maybe standard lets u revive but it
// takes longer?" The session difficulty is the host's native one (its save's
// difficulty byte); until the host's value is known, Beginner rules apply.
//
//   Beginner (and automation fixtures, KH2COOP_REVIVE_POLICY=beginner): today's
//            quick revive, a 60-frame hold.
//   Standard: revive allowed, a longer hold (300 frames, ~5 s;
//            KH2COOP_REVIVE_HOLD_STANDARD=<frames>, clamped to 60..1800).
//   Proud / Critical: no partner revive (KH2COOP_REVIVE_HARD=off, the default),
//            pending James's confirmation; KH2COOP_REVIVE_HARD=standard gives
//            them Standard's rule instead.
// A downed player who isn't revived sits out until a return (boss/encounter
// defeat, a save point); that is the return path, not this policy.
// No game memory, no Windows calls.
// ============================================================================

#include <cstdint>
#include <string_view>

namespace kh2coop::inject::downedpolicy {

// The save's difficulty byte (save + 0x2498): 0 Beginner, 1 Standard, 2 Proud, 3 Critical.
enum class Difficulty : std::uint8_t { Beginner = 0, Standard = 1, Proud = 2, Critical = 3, Unknown = 0xFF };
inline Difficulty DifficultyFromByte(std::uint8_t raw) noexcept {
    return raw <= 3 ? static_cast<Difficulty>(raw) : Difficulty::Unknown;
}
constexpr const char* DifficultyName(Difficulty d) noexcept {
    switch (d) {
    case Difficulty::Beginner: return "beginner";
    case Difficulty::Standard: return "standard";
    case Difficulty::Proud: return "proud";
    case Difficulty::Critical: return "critical";
    case Difficulty::Unknown: return "unknown";
    }
    return "?";
}

constexpr std::uint32_t kBeginnerHoldFrames = 60;   // ~1 s at 60 fps: the original VUH-1504 hold
constexpr std::uint32_t kStandardHoldFrames = 300;  // ~5 s: "takes longer" (tunable)
constexpr std::uint32_t kMinHoldFrames = 60, kMaxHoldFrames = 1800;

enum class HardRule : std::uint8_t { NoRevive, Standard }; // the one switch for Proud/Critical

struct Config {
    bool forceBeginner = false;                    // automation fixtures
    std::uint32_t standardHoldFrames = kStandardHoldFrames;
    HardRule hard = HardRule::NoRevive;
};

struct Policy {
    bool reviveAllowed = true;
    std::uint32_t holdFrames = kBeginnerHoldFrames;
};

inline Policy For(Difficulty d, const Config& c) noexcept {
    const std::uint32_t standard = c.standardHoldFrames < kMinHoldFrames ? kMinHoldFrames
                                 : c.standardHoldFrames > kMaxHoldFrames ? kMaxHoldFrames : c.standardHoldFrames;
    if (c.forceBeginner) return {true, kBeginnerHoldFrames};
    switch (d) {
    case Difficulty::Standard: return {true, standard};
    case Difficulty::Proud:
    case Difficulty::Critical:
        return c.hard == HardRule::Standard ? Policy {true, standard} : Policy {false, 0};
    case Difficulty::Beginner:
    case Difficulty::Unknown:
        break;
    }
    return {true, kBeginnerHoldFrames};
}

// Environment values (each nullptr-safe as an empty view): KH2COOP_REVIVE_POLICY,
// KH2COOP_REVIVE_HOLD_STANDARD, KH2COOP_REVIVE_HARD. Anything unrecognized keeps the default.
inline Config ParseConfig(std::string_view policy, std::string_view standardHold, std::string_view hard) noexcept {
    Config c {};
    c.forceBeginner = policy == "beginner";
    if (!standardHold.empty() && standardHold.size() <= 5) {
        std::uint32_t v = 0;
        bool digits = true;
        for (const char ch : standardHold) {
            if (ch < '0' || ch > '9') { digits = false; break; }
            v = v * 10 + static_cast<std::uint32_t>(ch - '0');
        }
        if (digits) c.standardHoldFrames = v;
    }
    if (hard == "standard") c.hard = HardRule::Standard;
    return c;
}

} // namespace kh2coop::inject::downedpolicy
