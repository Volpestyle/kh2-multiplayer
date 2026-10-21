#pragma once
// ============================================================================
// PuppetHold — pure DLL-side rules around VUH-1787's stalled-stream hold.
// No game memory, no Windows calls; EntityHook supplies copied facts.
//
// Sender: an owner's own room load stops its avatar stream (Sora is not
// updated), which receivers would otherwise hold as a ghost at the door. The
// local avatar is therefore flagged AvatarInCutscene while a load is pending,
// and the transition request re-publishes the last pose with the flag so the
// final snapshot before the gap always carries it.
//
// Receiver: a held pose has a frozen position and motion clock; after a short
// grace the puppet idles in place instead of running in place, unless the
// owner was downed (standing a downed puppet up would misreport it).
// ============================================================================

#include "kh2coop/Types.hpp"

#include <cstdint>
#include <optional>

namespace kh2coop::inject::puppethold {

constexpr std::uint32_t kHeldIdleFrames = 15; // ~250 ms of AvatarHeld before idling

class LocalPublisher {
public:
    // Sora's post-update capture. Returns the avatar to publish.
    AvatarState Captured(AvatarState avatar, bool loadPending) {
        if (loadPending) avatar.flags = static_cast<std::uint8_t>(avatar.flags | AvatarInCutscene);
        last_ = avatar;
        have_ = true;
        return avatar;
    }
    // A room transition was requested this frame: re-publish the last pose
    // flagged, unless nothing was published or it already carries the flag.
    std::optional<AvatarState> OnTransition(std::uint32_t frame) {
        if (!have_ || (last_.flags & AvatarInCutscene)) return std::nullopt;
        last_.flags = static_cast<std::uint8_t>(last_.flags | AvatarInCutscene);
        last_.seq = frame;
        return last_;
    }
    void Reset() { have_ = false; last_ = {}; }

private:
    AvatarState last_ {};
    bool have_ {false};
};

// Held-frame counter for one puppet driver: frames since the pose turned held.
struct HeldClock {
    bool held {false};
    std::uint32_t since {0};
    void Note(bool nowHeld, std::uint32_t frame) {
        if (nowHeld && !held) since = frame;
        held = nowHeld;
    }
    [[nodiscard]] std::uint32_t Frames(std::uint32_t frame) const { return held ? frame - since : 0; }
};

// Motion to drive for a puppet whose stream motion is `motion`.
inline std::uint32_t HeldMotion(std::uint32_t motion, std::uint8_t poseFlags, std::uint32_t heldFrames,
                                std::uint32_t idleMotion) noexcept {
    if (!(poseFlags & AvatarHeld) || (poseFlags & AvatarDowned)) return motion;
    return heldFrames >= kHeldIdleFrames ? idleMotion : motion;
}

// VUH-2613: a stream motion the puppet's banks lack is never set. The owner can play a motion borrowed
// from another actor's bank (a reaction command: Reversal's 241/242 are entries of the Dusk's bank), and
// the native setter clears the playback chain before its lookup fails, leaving a T-pose. It stands in
// idle, or run while the stream moves, as a companion puppet does for motions it lacks. Holding the
// previous motion instead would keep that motion from restarting when the stream returns to it.
inline std::uint32_t BankedMotion(std::uint32_t motion, bool inBanks, float speed2, std::uint32_t idleMotion,
                                  std::uint32_t runMotion) noexcept {
    if (inBanks) return motion;
    return speed2 > 1.0f ? runMotion : idleMotion;
}

// The stream's motion clock is authoritative only for a live pose.
constexpr bool SnapMotionTime(std::uint8_t poseFlags) noexcept { return !(poseFlags & AvatarHeld); }

} // namespace kh2coop::inject::puppethold
