#pragma once
// ============================================================================
// RevivePrompt — pure rules for the player-facing revive trigger (VUH-1504).
// Stand within kRange of a downed teammate and hold Triangle for kHoldFrames;
// the request fires on RELEASE after a full hold. No game memory, no Windows
// calls: the native adapter (DownedSpike.inl) supplies copied facts once per
// game frame on the owner thread and sends through RequestRevive.
// Default off: KH2COOP_REVIVE_PROMPT=1, only with KH2COOP_DOWNED_SPIKE=1.
// ============================================================================

#include <cstdint>

namespace kh2coop::inject::reviveprompt {

constexpr float kRange = 150.0f;              // under SessionHost's 200-unit policy
constexpr std::uint32_t kHoldFrames = 60;     // ~1 s at 60 fps (game frames, not wall time)
constexpr std::uint32_t kPendingFrames = 120; // "Reviving" at most ~2 s after a fire
constexpr std::uint16_t kTriangle = 0x1000;   // raw slot-0 button bit (PS2 order)
constexpr std::uint16_t kL1 = 0x0400;         // L1+Triangle is the native shortcut: never a revive hold
constexpr bool TriangleHeld(std::uint16_t raw) noexcept { return (raw & kTriangle) && !(raw & kL1); }

enum class Kind : std::uint8_t { Hidden = 0, Prompt = 1, Reviving = 2 };
enum class Hide : std::uint8_t {
    None = 0, Off, LocalUnavailable, LocalDowned, Event, Menu, Transition, NativeReaction,
    NoTarget, OutOfRange, Fired
};

struct Facts {
    bool enabled = false;          // prompt flag on and the downed owner is Ready
    bool localCanonical = false;   // canonical local player this frame
    bool localDowned = false;      // native dead flag or spike Downed
    bool inEvent = false;          // cutscene/event context
    bool menuOpen = false;         // menu id only: OPEN_MENU != 0xFF (pause stops Tick itself)
    bool transition = false;       // warp/room transition pending
    std::uint16_t nativeReaction = 0; // native RC id; nonzero = the game's RC owns Triangle.
                                      // UNVERIFIED address: the adapter passes 0 until calibrated.
    int localHp = 0;
    bool targetValid = false;      // nearest validated, fresh, downed teammate puppet
    std::uint8_t targetSlot = 0xFF;
    std::uint64_t targetEpisode = 0;
    float distance = 1e30f;
    bool triangle = false;         // Triangle seen in this frame's raw slot-0 input
};

struct State {
    std::uint8_t slot = 0xFF;
    std::uint64_t episode = 0;
    std::uint32_t held = 0;          // frames held toward kHoldFrames
    bool full = false;               // held reached kHoldFrames; fire on release
    bool wasTriangle = false;
    int lastHp = -1;
    std::uint64_t firedEpisode[3] {}; // latch: one request per target episode
    std::uint32_t pending = 0;       // frames left in "Reviving"
    std::uint8_t pendingSlot = 0xFF;
};

struct Output {
    Kind kind = Kind::Hidden;
    Hide hide = Hide::Off;
    std::uint8_t slot = 0xFF;
    std::uint16_t progress = 0;      // 0..1000 of the hold
    bool fire = false;               // send RequestRevive(slot) this frame
};

constexpr const char* HideName(Hide h) noexcept {
    switch (h) {
    case Hide::None: return "none";
    case Hide::Off: return "off";
    case Hide::LocalUnavailable: return "local-unavailable";
    case Hide::LocalDowned: return "local-downed";
    case Hide::Event: return "event";
    case Hide::Menu: return "menu";
    case Hide::Transition: return "transition";
    case Hide::NativeReaction: return "native-reaction";
    case Hide::NoTarget: return "no-target";
    case Hide::OutOfRange: return "out-of-range";
    case Hide::Fired: return "fired";
    }
    return "?";
}

inline void Cancel(State& s) noexcept { s.held = 0; s.full = false; }

// One game frame. Pure; deterministic for the same (state, facts).
inline Output Step(State& s, const Facts& f) noexcept {
    Output out {};
    const bool hit = s.lastHp >= 0 && f.localHp < s.lastHp; // took damage this frame
    s.lastHp = f.localCanonical ? f.localHp : -1;
    const bool risingEdge = f.triangle && !s.wasTriangle;
    const bool released = !f.triangle && s.wasTriangle;
    s.wasTriangle = f.triangle;

    if (s.pending) --s.pending;
    Hide hide = Hide::None;
    if (!f.enabled) hide = Hide::Off;
    else if (!f.localCanonical) hide = Hide::LocalUnavailable;
    else if (f.localDowned) hide = Hide::LocalDowned;
    else if (f.inEvent) hide = Hide::Event;
    else if (f.menuOpen) hide = Hide::Menu;
    else if (f.transition) hide = Hide::Transition;
    else if (f.nativeReaction) hide = Hide::NativeReaction;
    else if (!f.targetValid || f.targetSlot >= 3 || !f.targetEpisode) hide = Hide::NoTarget;
    else if (!(f.distance <= kRange)) hide = Hide::OutOfRange;
    else if (s.firedEpisode[f.targetSlot] == f.targetEpisode) hide = Hide::Fired;

    if (hide != Hide::None) {
        Cancel(s);
        s.slot = 0xFF; s.episode = 0;
        out.hide = hide;
        if (hide == Hide::Fired && s.pending && s.pendingSlot == f.targetSlot) {
            out.kind = Kind::Reviving; out.slot = f.targetSlot;
        }
        return out;
    }
    // A different target or episode restarts the hold; a hold must start with a fresh press.
    if (s.slot != f.targetSlot || s.episode != f.targetEpisode) {
        Cancel(s);
        s.slot = f.targetSlot; s.episode = f.targetEpisode;
        if (f.triangle && !risingEdge) s.wasTriangle = true; // already held: require release first
    }
    if (hit) Cancel(s);
    out.kind = Kind::Prompt;
    out.hide = Hide::None;
    out.slot = f.targetSlot;
    if (f.triangle && !hit) {
        if (risingEdge || s.held) {
            if (s.held < kHoldFrames) ++s.held;
            if (s.held >= kHoldFrames) s.full = true;
        }
    } else if (released) {
        if (s.full) {
            out.fire = true;
            s.firedEpisode[f.targetSlot] = f.targetEpisode;
            s.pending = kPendingFrames; s.pendingSlot = f.targetSlot;
            out.kind = Kind::Reviving;
        }
        Cancel(s);
    }
    out.progress = static_cast<std::uint16_t>(s.held * 1000 / kHoldFrames);
    return out;
}

} // namespace kh2coop::inject::reviveprompt
