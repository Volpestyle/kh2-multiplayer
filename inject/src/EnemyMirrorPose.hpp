#pragma once
// ============================================================================
// EnemyMirrorPose — VUH-1515 step 2 shared constants, the sampled Pose and the
// pure helpers used by both the stream (EnemyMirrorState.hpp) and the native
// driver (EnemyMirror.inl). Types.hpp only: safe beside HitChannel.hpp.
// ============================================================================

#include "kh2coop/Types.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace kh2coop::inject::enemymirror {


// Per-family opt-in (family = native objentry object id). M_EX020 Shadow is
// object 302 in both live probe runs (20261006-203449, -205838).
inline constexpr std::uint32_t kShadowObjectId = 302;
inline bool FamilyAllowed(std::uint32_t objectId) noexcept { return objectId == kShadowObjectId; }

inline constexpr std::uint32_t kPublishInterval = 3;  // host frames between EnemyMotion packets
// Render cursor behind the newest host frame: two packet intervals plus jitter
// (review S1). Below kMinLag the cursor holds and the overflow keeps the motion
// time running; past kMaxLag it snaps back to kDelay.
inline constexpr std::uint32_t kDelay = 9;
inline constexpr std::uint32_t kMinLag = 1, kMaxLag = 18;
inline constexpr std::uint32_t kStaleFrames = 30;     // local frames without a sample -> release
inline constexpr std::uint32_t kRetake = 2;           // new samples needed after a release
inline constexpr std::uint32_t kBlendFrames = 8;      // take-over position blend
inline constexpr std::uint32_t kGapTolerance = 4;     // missed updates within a run (S6), not a take-over
inline constexpr std::uint32_t kSpawnSettleFrames = 60; // bound this long before the first take-over (S7)
inline constexpr float kRestartBackFrames = 10.0f;    // same id, time back by more: re-issue the set (S2)
// Client refuses a stream pose farther than this from the spawn point (S5, a
// sanity bound against garbage, not a leash). 2000 was a leash: in live
// fixture-03 the host's Sora drifted ~1300 u during a colocated segment and its
// Shadows legitimately chased him up to 3103 u from their BB-courtyard spawns, so
// the client refused 4112 poses and those copies fell back to local AI.
inline constexpr float kMaxFromSpawn = 8000.0f;
inline constexpr std::uint32_t kTraceEvery = 30;      // trace lines on host frames that are multiples of this
inline constexpr std::size_t kRing = 8;
inline constexpr std::size_t kMaxTracks = 256;        // client stream tracks per epoch (defence in depth)

// What the client binding says about one actor this frame.
enum class Gate : std::uint8_t {
    None = 0,   // not a bound, living, allowlisted host enemy: never touched
    Bound = 1,  // bound and allowlisted, but no drivable stream pose (stale, settling, refused)
    Drive = 2,  // drive it from the pose
};

struct Sample {
    std::uint32_t hostFrame = 0, objectId = 0, motionId = 0;
    float motionTime = 0.0f;
    Vec3 position {};
    float rotationY = 0.0f;
};

struct Pose {
    std::uint16_t netId = 0;
    std::uint32_t objectId = 0, motionId = 0;
    float motionTime = 0.0f;
    Vec3 position {};
    float rotationY = 0.0f;
    double cursor = 0.0;  // displayed host-frame cursor (integral)
};

struct StreamStats {
    std::uint64_t accepted = 0, rejected = 0, resets = 0, staleSamples = 0, releases = 0, retakes = 0, trackCap = 0;
};

// Closed form (review S3): a loop never ends for a finite but huge angle.
inline float WrapPi(float a) noexcept {
    if (!std::isfinite(a)) return 0.0f;
    return static_cast<float>(std::remainder(static_cast<double>(a), 6.283185307179586476925));
}
inline float LerpAngle(float a, float b, float t) noexcept { return WrapPi(a + WrapPi(b - a) * t); }

// Brain thunk shape (spike ghidra-evidence/thunks.txt): every enemy base
// class's handler +0x20 is `48 8B CA E9 rel32` (mov rcx,rdx; jmp 0x3B4460).
inline bool BrainThunk(const std::uint8_t (&bytes)[8], std::uintptr_t at, std::uintptr_t brain) noexcept {
    if (bytes[0] != 0x48 || bytes[1] != 0x8B || bytes[2] != 0xCA || bytes[3] != 0xE9) return false;
    std::int32_t rel = 0;
    std::memcpy(&rel, bytes + 4, sizeof(rel));
    return at + 8 + static_cast<std::intptr_t>(rel) == brain;
}

// Motion time to apply: the stream time, clamped just inside a finite native
// motion end so a held non-looping attack does not wrap to frame 0.
inline float ClampMotionTime(float stream, float end) noexcept {
    if (std::isfinite(end) && end > 1.0f && stream > end - 0.5f) return end - 0.5f;
    return stream < 0.0f ? 0.0f : stream;
}

// Take-over blend: weight of the stream pose for blend frames remaining.
inline float BlendWeight(std::uint32_t remaining) noexcept {
    if (remaining == 0 || remaining > kBlendFrames) return 1.0f;
    return static_cast<float>(kBlendFrames - remaining + 1) / static_cast<float>(kBlendFrames + 1);
}

}  // namespace kh2coop::inject::enemymirror
