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
    };
    Phase Current() const noexcept { return phase_; }
    bool Holding() const noexcept {
        return phase_ != Phase::Idle && phase_ != Phase::Complete && phase_ != Phase::Failed;
    }
    bool Acquire(const Sample& s) noexcept {
        if (phase_ != Phase::Idle || !s.healthy || !s.safeToPause || s.menu != 255 || !s.now) {
            Fail(); return false;
        }
        phase_ = Phase::OpenRelease;
        deadline_ = s.now + 1000;
        press_ = true;
        return true;
    }
    // A successor transition wakes world servicing, but does not release input.
    void Wake() noexcept { wake_ = true; }
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
            if (s.menu == 10) { owned_ = true; phase_ = Phase::Paused; }
            else if (s.menu != 255 || s.now >= deadline_) { Fail(); break; }
            return {true, false, false, false};
        case Phase::Paused:
            if (!owned_ || s.menu != 10) { Fail(); break; }
            if (wake_) {
                phase_ = Phase::CloseRelease;
                deadline_ = s.now + 1000;
                return {true, true, false, false};
            }
            return {true, false, false, false};
        case Phase::CloseRelease:
            phase_ = Phase::Closing;
            return {true, false, false, false};
        case Phase::Closing:
            if (s.menu == 255) { owned_ = false; phase_ = Phase::OwnerWait; }
            else if (s.menu != 10 || s.now >= deadline_) { Fail(); break; }
            return {true, false, false, false};
        case Phase::OwnerWait:
            if (s.menu != 255) { Fail(); break; }
            if (s.ownerReleased) phase_ = Phase::ReleaseBoundary;
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
    std::uint64_t deadline_ = 0;
    bool owned_ = false, wake_ = false, press_ = false;
};
} // namespace kh2coop::inject::eventholdnative
