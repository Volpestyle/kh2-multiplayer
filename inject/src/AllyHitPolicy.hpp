#pragma once
// ============================================================================
// Ally player-to-player hits (co-op). Pure policy; no reads, no hooks.
//
// Native rule (static, 3D2060 "can this attack hit this victim"): the attack's hit mask at
// attack+0x39 is ~((1 << attackTeam) | 1), attackTeam = atkp+0x08 or the attacker's team at
// actor+0x4DC (docs/probes/DAMAGE_STATIC_NOTES.md). Atkp kinds 5/6 bypass the mask.
// Our puppet driver writes team 0 on every driven clone ("untouchable", EntityHook). So:
//   - a local attack never hits a clone (bit 0 is always cleared);
//   - a clone's attack carries team 0, its mask is ~1, and it hits the local player (team 1).
// DamagePolicy then zeroes the HP (RemoteSource), but the native hit record and its reaction
// still play (star burst, portrait flash). Run 20261007-115923: 15 such hits on one machine.
//
// Co-op refuses an ally player pair (two distinct player-class actors) before the hit exists.
// PvP (reserved, not implemented): Decide returns Native for RemotePlayer -> LocalPlayer, and
// DamagePolicy's RemoteSource branch routes that damage on purpose instead of zeroing it.
// ============================================================================
#include <cstdint>
#include <cstring>

namespace kh2coop::inject::allyhit {

enum class Mode : std::uint8_t { Off, Trace, CoOp }; // PvP: reserved (see above)
// Side of an actor in a hit: a player-class (objentry type 0) actor is the canonical local player
// or another player (a party-native clone / puppet); anything else is Other.
enum class Side : std::uint8_t { Other, LocalPlayer, RemotePlayer };
enum class Verdict : std::uint8_t { Native, Refuse };

struct Pair {
    Mode mode = Mode::Off;
    Side attacker = Side::Other, victim = Side::Other;
    bool nativeAllows = false; // the original 3D2060 result
    std::uint8_t kind = 0;     // atkp +0x04; 5/6 bypass the native mask
    std::uint32_t victimTeam = 0xFFFFFFFF; // victim actor+0x4DC (1 = party: Donald, Goofy, world allies)
};

constexpr bool PlayerSide(Side s) { return s == Side::LocalPlayer || s == Side::RemotePlayer; }
constexpr bool BypassKind(std::uint8_t kind) { return kind == 5 || kind == 6; }

// Only ever narrows the native answer.
constexpr Verdict Decide(const Pair& p) {
    if (p.mode != Mode::CoOp || !p.nativeAllows) return Verdict::Native;
    // Gap 2 (two-player party: a clone beside native Goofy): a clone's team-0 mask (~1) includes team 1, which its
    // native mask ~((1 << 1) | 1) excludes. A clone never hits a team-1 non-player (atkp kinds 5/6 aside).
    if (p.attacker == Side::RemotePlayer && p.victim == Side::Other && p.victimTeam == 1 && !BypassKind(p.kind))
        return Verdict::Refuse;
    if (!PlayerSide(p.attacker) || !PlayerSide(p.victim)) return Verdict::Native;
    if (BypassKind(p.kind)) return Verdict::Native; // kept native (heal-like kinds; their effect between players is untested)
    return Verdict::Refuse; // clone -> local, local -> clone, clone -> clone
}

// KH2COOP_ALLY_HIT: unset/"0" Off, "1" CoOp (refuse + measure), "trace" Trace (measure only).
// `valid` is false for any other text (the module then stays off and says so).
inline Mode ParseMode(const char* text, bool& valid) {
    valid = true;
    if (!text || !*text || std::strcmp(text, "0") == 0) return Mode::Off;
    if (std::strcmp(text, "1") == 0) return Mode::CoOp;
    if (std::strcmp(text, "trace") == 0) return Mode::Trace;
    valid = false;
    return Mode::Off;
}

inline const char* SideName(Side s) {
    return s == Side::LocalPlayer ? "local" : s == Side::RemotePlayer ? "clone" : "other";
}

} // namespace kh2coop::inject::allyhit
