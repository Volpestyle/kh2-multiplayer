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


// Per-family opt-in (family = native objentry object id). M_EX020 Shadow is
// object 302 in both live probe runs (20261006-203449, -205838). M_EX520, the
// Hook Bat (a winged flyer with the "Bat Cry" reaction command; live run 073546;
// first mis-named Soldier), is object 4 (BB courtyard second wave). Both are objentry
// type 4: the generic factory 0x3DF930 builds every type-4 enemy with constructor
// 0x419E30, whose handler 0x7528E8 has vtable 0x5D2D68, so the Hook Bat's brain
// (+0x20 = 0x419B10 -> 0x3B4460) and removal predicate (+0x40 = 0x419B90 ->
// 0x3DAC30) are the same shape-checked thunks the Shadow uses. M_EX010, the real
// Soldier, is object 301: also type 4, same class (enemy family census T1; BB
// courtyard battle program 2, groups 68/69; live run 085322). Its world skins M_EX010_NM (1838, Halloween
// Town), M_EX010_TR (1839, Space Paranoids) and M_EX010_WI (1849, Timeless River) share its enemy stats id
// (neoStatus 1000), so the same AI and motion layout: allowlisted with it, without a separate live run.
inline constexpr std::uint32_t kShadowObjectId = 302;
inline constexpr std::uint32_t kHookBatObjectId = 4;
inline constexpr std::uint32_t kSoldierObjectId = 301;
inline constexpr std::uint32_t kSoldierSkinObjectIds[] = {1838, 1839, 1849};
// T1 batch 2 (enemy family census; BB Entrance Hall 05/00, battle program 3; live run 092758): Lance Soldier
// M_EX690 (17) and Large Body M_EX050 (303). Both objentry type 4: the same class (0x5D2D68) and shape-checked
// hooks as the Shadow. Gargoyle Warrior M_BB010_AX (368; a statue that wakes, BB Entrance Hall b_81) is type 4
// as well: live runs 20261007-121127 (visit 1) and 121709 (both visits) mirrored it with motion agreement 94/94
// and 235/235, position p95 0. Gargoyle Knight M_BB010_SWORD (367) stays off until a run sees it engage (it
// stayed a dormant statue in 121127 visit 2).
inline constexpr std::uint32_t kLanceSoldierObjectId = 17;
inline constexpr std::uint32_t kLargeBodyObjectId = 303;
inline constexpr std::uint32_t kGargoyleWarriorObjectId = 368;
// T1 batch 3: Nightwalker M_EX580 (10; Land of Dragons 08/03 btl 1) is type 4 as well: live run 20261007-131606
// mirrored it on both visits with motion agreement 50/50 and 78/78, position p95 0, 30/30 hits attributed.
// Rapid Thruster M_EX660 (304; LoD 08/05 and 08/06 btl 1) passed LoD fixture-02 (run 20261007-132550) at three
// visits with position p95 0. Its world skins M_EX660_WI (1843, Timeless River) and M_EX660_AL (1889, Agrabah)
// share its enemy stats id (neoStatus 1020): allowlisted with it, as the Soldier skins were.
inline constexpr std::uint32_t kNightwalkerObjectId = 10;
inline constexpr std::uint32_t kRapidThrusterObjectId = 304;
inline constexpr std::uint32_t kRapidThrusterSkinObjectIds[] = {1843, 1889};
// T1 batch 4 (candidate, test-only until the batch-4 fixture passes; legs from the no-mirror sweep-02): Armored Knight
// M_EX760 (305; HB 04/02 btl 1), Driller Mole M_EX700 (18; TT 02/0D btl 4), Neoshadow M_EX420 (120; TT 02/1F btl 5)
// and the Gargoyle Knight M_BB010_SWORD (367; BB 05/09 btl 10, the engagement retry). All objentry type 4.
inline constexpr std::uint32_t kArmoredKnightObjectId = 305;
inline constexpr std::uint32_t kDrillerMoleObjectId = 18;
inline constexpr std::uint32_t kNeoshadowObjectId = 120;
inline constexpr std::uint32_t kGargoyleKnightObjectId = 367;
// T1 batch 5: Nobody families qualified live in TWTNW (run 20261007-220021):
// 12/0F btl1 Dusk; 12/0C btl1 Samurai and Dancer. Creeper 317 stays native:
// its client copy failed initial binding (no-point-match) in that run.
inline constexpr std::uint32_t kDuskObjectId = 318;
inline constexpr std::uint32_t kCreeperObjectId = 317;  // not allowlisted; see above
inline constexpr std::uint32_t kSamuraiObjectId = 310;
inline constexpr std::uint32_t kDancerObjectId = 312;
// T1 batch7 test-only candidate; direct-ARD smoke qualification pending.
inline constexpr std::uint32_t kRabidDogObjectId = 3;
inline constexpr std::uint32_t kHammerFrameObjectId = 8;
inline constexpr std::uint32_t kAerialChampObjectId = 2410;
inline constexpr std::uint32_t kBeffudlerObjectId = 2404;
// The whole allowlist, in the order the configured line prints it.
inline constexpr std::uint32_t kFamilies[] = {kShadowObjectId, kHookBatObjectId, kSoldierObjectId,
                                              kSoldierSkinObjectIds[0], kSoldierSkinObjectIds[1], kSoldierSkinObjectIds[2],
                                              kLanceSoldierObjectId, kLargeBodyObjectId, kGargoyleWarriorObjectId,
                                              kNightwalkerObjectId, kRapidThrusterObjectId, kRapidThrusterSkinObjectIds[0],
                                              kRapidThrusterSkinObjectIds[1], kArmoredKnightObjectId, kDrillerMoleObjectId,
                                              kNeoshadowObjectId, kGargoyleKnightObjectId,
                                              kDuskObjectId, kSamuraiObjectId, kDancerObjectId,
                                              kRabidDogObjectId, kHammerFrameObjectId, kAerialChampObjectId, kBeffudlerObjectId};
inline bool FamilyAllowed(std::uint32_t objectId) noexcept {
    for (const auto family : kFamilies)
        if (objectId == family) return true;
    return false;
}
// "302,4,..." into out (always NUL-terminated when cap > 0); returns the characters written. A family that
// would not fit is dropped whole, never cut, so a short buffer cannot print a wrong id.
inline std::size_t FormatFamilies(char* out, std::size_t cap) noexcept {
    if (!out || cap == 0) return 0;
    std::size_t at = 0;
    for (const auto family : kFamilies) {
        char digits[10];
        std::size_t n = 0;
        for (std::uint32_t v = family; n == 0 || v != 0; v /= 10) digits[n++] = static_cast<char>('0' + v % 10);
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
