#pragma once
#include <cstdint>

namespace kh2coop::inject::eventholdnative {
// No memory, world or OS access. The caller validates current scope and the
// actual collector instance, then overlays only this callback's fresh input.
class InputState {
public:
    enum class Phase { Idle, OpenRelease, Opening, Paused, CloseRelease, Closing,
                       OwnerWait, ReleaseBoundary, Complete, Failed };
    struct Sample {
        std::uint64_t now = 0;
        unsigned menu = 255;
        bool safeToPause = false;
        bool healthy = false;
        bool ownerReleased = false;
    };
    struct Output {
        bool neutral = false;
        bool start = false;
        bool failed = false;
        bool released = false;
        bool timedOut = false;
        std::uint16_t buttons = 0;  // a press other than Start (the close unwind's Circle or Cross)
        bool stuck = false;         // the pause did not close within UnwindPresses
    };
    static constexpr std::uint16_t StartButton = 0x0008, CircleButton = 0x2000, CrossButton = 0x4000;
    // VUH-2537: Start does not close a modal page over the pause menu (KH2's first-open tutorials, such
    // as "What is Auto-Reload?", advance only on Cross). While the pause is still up UnwindStepMs after a
    // press, the close goes on with Circle (backs out of a submenu, closes the plain menu), Start (closes
    // the plain menu), then Cross (advances a page both of those ignore), stopping the moment the menu
    // reads closed. Cross comes only after Circle and Start have both failed, so it never opens a submenu.
    static constexpr std::uint64_t UnwindStepMs = 400;
    static constexpr unsigned UnwindPresses = 24;
    // VUH-2268: after the host's release, a client that has not converged in
    // this long gets its input back instead of staying frozen.
    static constexpr std::uint64_t ReleaseTimeoutMs = 10000;
    Phase Current() const noexcept { return phase_; }
    unsigned Unwound() const noexcept { return unwound_; }
    // VUH-2640: this hold's own pause is open (it saw menu 8 or 10 after its Start) and not yet closed.
    bool OwnedPauseOpen() const noexcept {
        return owned_ && (phase_ == Phase::Paused || phase_ == Phase::CloseRelease || phase_ == Phase::Closing);
    }
    unsigned PauseMenu() const noexcept { return pauseMenu_; }
    bool Holding() const noexcept {
        return phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Failed;
    }
    // Refusal leaves the state as it was: the caller skips this one hold and
    // later holds still work.
    bool Acquire(const Sample& s) noexcept {
        if (Holding() || !s.healthy || !s.safeToPause || s.menu != 255 || !s.now) return false;
        phase_ = Phase::OpenRelease;
        deadline_ = s.now + 1000;
        releaseDeadline_ = 0;
        owned_ = false;
        wake_ = false;
        follow_ = false;
        press_ = true;
        return true;
    }
    // A successor transition wakes world servicing, but does not release input.
    void Wake() noexcept { wake_ = true; }
    // VUH-2359: a successor transition into a qualified scene also closes the pause, then gives input
    // back as soon as it has closed: the friend plays the host's scene and advances its own dialog.
    void FollowScene() noexcept { wake_ = true; follow_ = true; }
    void ReleaseReceived(std::uint64_t now) noexcept { releaseDeadline_ = now + ReleaseTimeoutMs; }
    // A new host event supersedes a hold whose pause has already closed and
    // that is only waiting on the owner's convergence.
    bool Supersede() noexcept {
        if (phase_ != Phase::OwnerWait && phase_ != Phase::ReleaseBoundary) return false;
        phase_ = Phase::Complete;
        return true;
    }
    void Fail() noexcept { phase_ = Phase::Failed; press_ = false; }
    Output Tick(const Sample& s) noexcept {
        if (phase_ == Phase::Idle || phase_ == Phase::Complete) return {};
        if (phase_ == Phase::Failed) return {false, false, true, false};
        if (!s.healthy || !s.now) { Fail(); return {false, false, true, false}; }
        if (press_) { press_ = false; return {true, true, false, false}; }
        switch (phase_) {
        case Phase::OpenRelease:
            phase_ = Phase::Opening;
            return {true, false, false, false};
        case Phase::Opening:
            // Start opens the full menu (10) in the field and the combat
            // pause (8) in fights where the menu is locked, such as boss
            // rooms (VUH-2268 follow-up). Either one is the owned pause.
            if (s.menu == 10 || s.menu == 8) { owned_ = true; pauseMenu_ = s.menu; phase_ = Phase::Paused; }
            else if (s.menu != 255 || s.now >= deadline_) { Fail(); break; }
            return {true, false, false, false};
        case Phase::Paused:
            if (!owned_ || s.menu != pauseMenu_) { Fail(); break; }
            if (wake_) {
                phase_ = Phase::CloseRelease;
                lastPress_ = s.now;
                unwound_ = 0;
                return {true, true, false, false};
            }
            return {true, false, false, false};
        case Phase::CloseRelease:
            phase_ = Phase::Closing;
            return {true, false, false, false};
        case Phase::Closing:
            if (s.menu == 255) { owned_ = false; phase_ = Phase::OwnerWait; }
            else if (!unwound_ && s.menu != pauseMenu_) { Fail(); break; }
            else if (s.now >= lastPress_ + UnwindStepMs) {
                if (unwound_ >= UnwindPresses) {
                    Fail();
                    Output out {false, false, true, false};
                    out.stuck = true;
                    return out;
                }
                static constexpr std::uint16_t order[] = {CircleButton, StartButton, CrossButton};
                const std::uint16_t press = order[unwound_ % 3];
                ++unwound_;
                lastPress_ = s.now;
                phase_ = Phase::CloseRelease;
                Output out {true, press == StartButton, false, false};
                if (press != StartButton) out.buttons = press;
                return out;
            }
            return {true, false, false, false};
        case Phase::OwnerWait:
            if (s.menu != 255) { Fail(); break; }
            if (follow_) {
                phase_ = Phase::Complete;
                return {true, false, false, true};
            }
            if (s.ownerReleased) phase_ = Phase::ReleaseBoundary;
            else if (releaseDeadline_ && s.now >= releaseDeadline_) {
                phase_ = Phase::Complete;
                return {true, false, false, true, true};
            }
            return {true, false, false, false};
        case Phase::ReleaseBoundary:
            if (s.menu != 255 || !s.ownerReleased) { Fail(); break; }
            phase_ = Phase::Complete;
            return {true, false, false, true}; // discard last held automation sample
        default: Fail(); break;
        }
        return {false, false, true, false};
    }
private:
    Phase phase_ = Phase::Idle;
    std::uint64_t deadline_ = 0, releaseDeadline_ = 0, lastPress_ = 0;
    unsigned unwound_ = 0;
    bool owned_ = false, wake_ = false, follow_ = false, press_ = false;
    unsigned pauseMenu_ = 10;
};

// VUH-2640 (F2): the pause a hold opened, left open when its lifetime expired (HeartbeatExpired). The failed
// hold never presses the close, and recovery refuses while a menu is open, so the friend would stay paused
// for good. Closed here with the hold's own close sequence: Start, then the Circle/Start/Cross unwind every
// UnwindStepMs, a neutral frame after each press, at most UnwindPresses. It reads only the native menu: no
// world state, and the expired lifetime stays terminal (never cleared or rebound). Armed only for the hold's
// own pause (owned, menu 8 or 10, still showing). A different menu before the first press (the player's or
// the game's) is left alone, as is a pause still open after the last press.
class OrphanedPause {
public:
    struct Output {
        bool neutral = false, start = false, done = false, gaveUp = false;
        std::uint16_t buttons = 0;
    };
    bool Arm(bool ownedOpen, unsigned pauseMenu, unsigned menuNow, std::uint64_t now) noexcept {
        if (armed_ || !ownedOpen || (pauseMenu != 8 && pauseMenu != 10) || menuNow != pauseMenu || !now) return false;
        *this = {};
        armed_ = true;
        pauseMenu_ = pauseMenu;
        return true;
    }
    bool Active() const noexcept { return armed_; }
    unsigned Presses() const noexcept { return presses_; }
    Output Tick(unsigned menu, std::uint64_t now) noexcept {
        Output out;
        if (!armed_) return out;
        if (menu == 255) { armed_ = false; out.done = true; return out; }
        if (!presses_ && menu != pauseMenu_) { armed_ = false; out.gaveUp = true; return out; }
        if (release_) { release_ = false; out.neutral = true; return out; }
        if (presses_ && now < lastPress_ + InputState::UnwindStepMs) { out.neutral = true; return out; }
        if (presses_ > InputState::UnwindPresses) { armed_ = false; out.gaveUp = true; return out; }
        static constexpr std::uint16_t order[] = {InputState::CircleButton, InputState::StartButton, InputState::CrossButton};
        const std::uint16_t press = presses_ ? order[(presses_ - 1) % 3] : InputState::StartButton;
        ++presses_;
        lastPress_ = now;
        release_ = true;
        out.neutral = true;
        if (press == InputState::StartButton) out.start = true;
        else out.buttons = press;
        return out;
    }

private:
    bool armed_ = false, release_ = false;
    unsigned pauseMenu_ = 10, presses_ = 0;
    std::uint64_t lastPress_ = 0;
};
} // namespace kh2coop::inject::eventholdnative
