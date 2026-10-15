#pragma once
#include "kh2coop/LatencyConfig.hpp"
// ============================================================================
// EnemyMirrorPose — VUH-1515 step 2 shared constants, the sampled Pose and the
// pure helpers used by both the stream (EnemyMirrorState.hpp) and the native
// driver (EnemyMirror.inl). Types.hpp only: safe beside HitChannel.hpp.
// ============================================================================

#include "kh2coop/Types.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace kh2coop::inject::enemymirror {


// D14 (2026-10-14), one brain: the host's AI drives every censused enemy on clients (objentry type 3 boss or
// type 4 mob, not an F_ object; EnemySync's census). A family (native objentry object id) stays on local AI
// only when it observably breaks, and goes in kDenied with a one-line reason. This replaced the per-family
// allowlist of VUH-1515/VUH-1789; that qualification history is in docs/ENEMY_PARITY.md.
// Families named by tests and comments.
inline constexpr std::uint32_t kShadowObjectId = 302;
inline constexpr std::uint32_t kHookBatObjectId = 4;
inline constexpr std::uint32_t kSoldierObjectId = 301;
inline constexpr std::uint32_t kSoldierSkinObjectIds[] = {1838, 1839, 1849};
inline constexpr std::uint32_t kLanceSoldierObjectId = 17;
inline constexpr std::uint32_t kLargeBodyObjectId = 303;
inline constexpr std::uint32_t kGargoyleWarriorObjectId = 368;
inline constexpr std::uint32_t kNightwalkerObjectId = 10;
inline constexpr std::uint32_t kRapidThrusterObjectId = 304;
inline constexpr std::uint32_t kRapidThrusterSkinObjectIds[] = {1843, 1889};
inline constexpr std::uint32_t kArmoredKnightObjectId = 305;
inline constexpr std::uint32_t kDrillerMoleObjectId = 18;
inline constexpr std::uint32_t kNeoshadowObjectId = 120;
inline constexpr std::uint32_t kGargoyleKnightObjectId = 367;
inline constexpr std::uint32_t kDuskObjectId = 318;
inline constexpr std::uint32_t kSamuraiObjectId = 310;
inline constexpr std::uint32_t kDancerObjectId = 312;
inline constexpr std::uint32_t kSilverRockObjectId = 71;
inline constexpr std::uint32_t kIcyCubeObjectId = 77;
inline constexpr std::uint32_t kAssassinObjectId = 309;
inline constexpr std::uint32_t kRabidDogObjectId = 3;
inline constexpr std::uint32_t kHammerFrameObjectId = 8;
inline constexpr std::uint32_t kAerialChampObjectId = 2410;
inline constexpr std::uint32_t kBeffudlerObjectId = 2404;
inline constexpr std::uint32_t kIronHammerObjectId = 2405;
inline constexpr std::uint32_t kSurveillanceRobotObjectId = 306;
inline constexpr std::uint32_t kBookmasterObjectId = 5;
inline constexpr std::uint32_t kCreeperPlantObjectId = 125;
inline constexpr std::uint32_t kMagicPhantomObjectId = 2403;
inline constexpr std::uint32_t kCreeperObjectId = 317;
inline constexpr std::uint32_t kFieryGlobeObjectId = 76;
inline constexpr std::uint32_t kLanceWarriorObjectId = 2409;

struct DeniedFamily { std::uint32_t objectId; const char* reason; };
// The denylist, in the order the configured line prints it. 317 and 76 are the scoped record-binding families
// (docs/ENEMY_RECORD_BINDING.md); while they are listed here that default-off opt-in drives neither.
inline constexpr DeniedFamily kDenied[] = {
    {kCreeperObjectId, "client copy fails ordinary point binding (no-point-match, run 20261007-220021)"},
    {kFieryGlobeObjectId, "first-point mismatch on all five bodies (run b6-02)"},
    {kLanceWarriorObjectId, "HB 04/17 btl25 load faults even with co-op flags off (run 20261013-034336)"},
};
inline const char* DeniedReason(std::uint32_t objectId) noexcept {
    for (const auto& d : kDenied)
        if (objectId == d.objectId) return d.reason;
    return nullptr;
}
inline bool FamilyAllowed(std::uint32_t objectId) noexcept { return objectId != 0 && !DeniedReason(objectId); }
// "317,76,2409" into out (always NUL-terminated when cap > 0); returns the characters written. A family that
// would not fit is dropped whole, never cut, so a short buffer cannot print a wrong id.
inline std::size_t FormatDenied(char* out, std::size_t cap) noexcept {
    if (!out || cap == 0) return 0;
    std::size_t at = 0;
    for (const auto& d : kDenied) {
        char digits[10];
        std::size_t n = 0;
        for (std::uint32_t v = d.objectId; n == 0 || v != 0; v /= 10) digits[n++] = static_cast<char>('0' + v % 10);
        const std::size_t need = n + (at ? 1 : 0);
        if (at + need + 1 > cap) break;
        if (at) out[at++] = ',';
        while (n) out[at++] = digits[--n];
    }
    out[at] = '\0';
    return at;
}

inline constexpr std::uint32_t kPublishInterval = 3;  // host frames between EnemyMotion packets
// Render cursor behind the newest host frame: qualified six-frame default
// (runtime-configurable within bounded hysteresis). Below kMinLag the cursor holds and the overflow keeps the motion
// time running; past kMaxLag it snaps back to kDelay.
inline constexpr std::uint32_t kDelay = latency::kEnemyDelayFrames;
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
    None = 0,   // not a bound, living, mirrored (not denied) host enemy: never touched
    Bound = 1,  // bound and mirrored, but no drivable stream pose (stale, settling, refused)
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
    std::uint64_t cursorHolds = 0, cursorCatchups = 0, cursorSnaps = 0, underrunFrames = 0;
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
