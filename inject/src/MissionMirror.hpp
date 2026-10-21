#pragma once
// VUH-2342 (D14 item 3): mission gauges, counters and timers follow the host. The native
// adapter is MissionMirror.inl (included in EnemySync.cpp); docs/MISSION_MIRROR.md.
// Pure rules only: no game memory, no Windows calls.
//
// The mission M = *(exe+0x2A0FF68) (exe+0x3A3970 returns it). Read for this build (Steam
// 9002b2de) with headless Ghidra; TCM-Murray's VERIFIED_OFFSETS.md named the leads (GPL-3.0,
// docs/research/PRIOR_ART.md). No code is copied.
//   M+4      flags: bit 2 = setters refused (exe+0x3A39F0), | 0x14 once ended or failed
//   M+0x40   the mission file's path, e.g. "msn/us/MU01_MS102.bar" (the language folder differs)
//   M+0x120  2 once failed (exe+0x3A3800, trap 4/11 mission_failed)
//   M+0x6A0  nonzero: set_gauge does nothing
//   gauge i  M+0x250 + i*0x48: +0x18 flags (bit 0 in use), +0x24 max (int), +0x38 value (float).
//            Setter exe+0x3FA310(gauge, f) clamps and signals 0x88 at 0, 0x89 at max;
//            set_gauge exe+0x3FBCB0(f, slot) checks the setters and the slot first.
//   counter  M+0x160 + i*0x50: the same flags/max, +0x38 value (int). exe+0x3FA230(counter, n)
//            signals 0xE at 0, 0x7B at max; set_count exe+0x3FBAE0(n, slot).
//   timer    globals: clock exe+0xABB850, start 0xABB858, duration 0xABB85C, value 0xABB860,
//            state 0xABB864 (2 running), mode u8 0xABB868. The tick (exe+0x1571B0): mode 1 counts
//            down, value = start + duration - clock; otherwise value = clock - start.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "kh2coop/MissionFlags.hpp"

namespace kh2coop::inject::missionmirror::rules {

inline constexpr std::uintptr_t kMissionPtr = 0x2A0FF68;
inline constexpr std::uintptr_t kFlags = 0x4, kPath = 0x40, kResult = 0x120, kGaugeLock = 0x6A0;
inline constexpr std::uintptr_t kGauges = 0x250, kGaugeStride = 0x48, kCounters = 0x160, kCounterStride = 0x50;
inline constexpr std::uintptr_t kSlotFlags = 0x18, kSlotMax = 0x24, kSlotValue = 0x38;
inline constexpr std::uintptr_t kTimerClock = 0xABB850, kTimerStart = 0xABB858, kTimerDuration = 0xABB85C,
                                 kTimerValue = 0xABB860, kTimerState = 0xABB864, kTimerMode = 0xABB868;
inline constexpr std::uint32_t kSettersRefused = 0x4;
inline constexpr std::size_t kPathBytes = 64, kFileBytes = 24, kSlots = 3;
inline constexpr std::uint32_t kTimerRunning = 2, kTimerCountDown = 1;
inline constexpr std::int32_t kTimerSlackFrames = 15;  // closer than this is left alone
inline constexpr float kGaugeEpsilon = 0.001f;
inline constexpr std::uint32_t kTagInt = 0x544E4940;  // '@INT': a script int argument

// The file name after the last '/' or '\' of a mission path: the language folder ('msn/us/',
// 'msn/sp/') differs between games, the file does not. False when it doesn't fit or isn't text.
inline bool FileOf(const char* path, std::size_t n, char (&out)[kFileBytes]) noexcept {
    std::memset(out, 0, sizeof(out));
    std::size_t end = 0;
    while (end < n && path[end]) ++end;
    if (end == n) return false;  // no terminator inside the buffer
    std::size_t start = end;
    while (start > 0 && path[start - 1] != '/' && path[start - 1] != '\\') --start;
    const std::size_t len = end - start;
    if (len == 0 || len >= kFileBytes) return false;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = path[start + i];
        if (c < 0x21 || c > 0x7E) return false;
        out[i] = c;
    }
    return true;
}
inline bool SameFile(const char* a, const char* b) noexcept { return a[0] && std::strncmp(a, b, kFileBytes) == 0; }

struct Slot { std::uint32_t flags = 0; std::int32_t max = 0; float gauge = 0; std::int32_t count = 0; };
struct Mission {
    bool active = false;
    char file[kFileBytes] {};
    std::uint32_t flags = 0, result = 0, gaugeLock = 0;
    Slot gauges[kSlots] {}, counters[kSlots] {};
    std::uint32_t timerState = 0, timerValue = 0, timerDuration = 0;
    std::uint8_t timerMode = 0;
    std::uint8_t endTrap = 0;  // host only: the table-4 trap that ended it (3 or 22), from its detour
    std::uint32_t endArg = 0;
};
inline bool Failed(const Mission& m) noexcept { return m.active && m.result == kFailedResult; }
inline bool Ended(const Mission& m) noexcept { return m.active && (m.flags & kEnded) == kEnded; }
// The host's mission ended, not by failing and not through an end trap its hooks saw (an engine end,
// or a host without hooks): the friend can't replay it, so it stops following and keeps its own end.
inline bool HostEndedUnknown(const Mission& host) noexcept { return Ended(host) && !Failed(host) && (!host.endTrap || host.endTrap == 16); }

// VUH-2343: give the final Bailey death at most three seconds at 60 owner
// frames/s. A stuck orphan must not hold the native end forever. Reset this
// alongside the local mission pointer/load/file, never from packet receipt.
struct EndReplayDeferral {
    static constexpr std::uint32_t kMaxFrames = 180;
    std::uint32_t firstFrame{};
    bool active{};
    bool Capped(std::uint32_t frame) noexcept {
        if(!active || frame<firstFrame) {active=true;firstFrame=frame;return false;}
        return frame-firstFrame>=kMaxFrames;
    }
};

// The host's mission is shown here only when both games run the same mission file and this one
// hasn't ended. Everything below assumes Follows() was true.
// VUH-2606: a mid-mission event (the Day 5 Struggle match) closes the host's gameplay gate and holds its
// enemy census. Mission state still goes out on those frames, or the friend's copy goes stale, it stops
// following, and its own clock runs the match to a different end. Only the gameplay gate qualifies: any
// other census failure (a load, a changed instance) still stops publishing.
inline bool PublishWhileHeld(bool host, bool censusComplete, const char* reason) noexcept {
    return host && !censusComplete && reason && std::strcmp(reason, "gameplay gate") == 0;
}
inline bool Follows(const Mission& host, const Mission& local) noexcept {
    return host.active && local.active && SameFile(host.file, local.file) && !Ended(local);
}

// A gauge or counter takes the host's value when both games use that slot and the values differ.
inline bool GaugeDiffers(const Slot& host, const Slot& local) noexcept {
    return (host.flags & kInUse) && (local.flags & kInUse) && std::isfinite(host.gauge) &&
           std::fabs(host.gauge - local.gauge) > kGaugeEpsilon;
}
inline bool CountDiffers(const Slot& host, const Slot& local) noexcept {
    return (host.flags & kInUse) && (local.flags & kInUse) && host.count != local.count;
}

// The amount to add to this game's timer start so its value matches the host's, or 0 when both
// aren't running the same timer or they are already within kTimerSlackFrames.
inline std::int32_t TimerStartDelta(const Mission& host, const Mission& local) noexcept {
    if (host.timerState != kTimerRunning || local.timerState != kTimerRunning || host.timerMode != local.timerMode ||
        host.timerDuration != local.timerDuration)
        return 0;
    const std::int64_t diff = static_cast<std::int64_t>(host.timerValue) - static_cast<std::int64_t>(local.timerValue);
    if (diff > -kTimerSlackFrames && diff < kTimerSlackFrames) return 0;
    if (diff > 0x7FFFFFFF || diff < -0x7FFFFFFF) return 0;
    // Counting down a later start leaves more time; counting up it shows less.
    return static_cast<std::int32_t>(host.timerMode == kTimerCountDown ? diff : -diff);
}

// Calls this game's own mission script makes to change a gauge or counter, or to end it.
// While the host's mission is followed they are dropped: the host's values arrive instead, and the
// mission ends when and how the host's does. On the host the same hooks record which end ran.
// Table 4 of the bdscript traps (names after OpenKH BdxTraps.cs where it has one).
enum class Kind : std::uint8_t { Value, Fail, End, BossEnd };
struct Trap { std::uint16_t index; std::uint32_t fnRva, flags; Kind kind; const std::uint8_t* body; std::size_t bodySize; const char* name; };
// Each stub loads its arguments and jumps to the helper (mov edx,[rcx+8]; mov/movss ...; jmp rel32).
inline constexpr std::uint8_t kSetCount[] = {0x8B, 0x51, 0x08, 0x8B, 0x09, 0xE9, 0x66, 0x84, 0xFC, 0xFF};       // -> 0x3FBAE0
inline constexpr std::uint8_t kIncCount[] = {0x8B, 0x11, 0xB9, 0x01, 0x00, 0x00, 0x00, 0xE9, 0x24, 0x7C, 0xFC, 0xFF};  // -> 0x3FB2B0(1)
inline constexpr std::uint8_t kSetGauge[] = {0x8B, 0x51, 0x08, 0xF3, 0x0F, 0x10, 0x01, 0xE9, 0x64, 0x85, 0xFC, 0xFF};  // -> 0x3FBCB0
inline constexpr std::uint8_t kAddGauge[] = {0x8B, 0x51, 0x08, 0xF3, 0x0F, 0x10, 0x01, 0xE9, 0x44, 0x7C, 0xFC, 0xFF};  // -> 0x3FB3A0
inline constexpr std::uint8_t kSetGaugeRatio[] = {0x8B, 0x51, 0x08, 0xF3, 0x0F, 0x10, 0x01, 0xE9, 0x24, 0x86, 0xFC, 0xFF};  // -> 0x3FBD90
inline constexpr std::uint8_t kMissionFailed[] = {0xE9, 0x1B, 0x03, 0xF7, 0xFF};                               // -> 0x3A3800
inline constexpr std::uint8_t kDecCount[] = {0x8B, 0x11, 0xB9, 0xFF, 0xFF, 0xFF, 0xFF, 0xE9, 0x14, 0x7C, 0xFC, 0xFF};  // -> 0x3FB2B0(-1)
inline constexpr std::uint8_t kAddCount[] = {0x8B, 0x51, 0x08, 0x8B, 0x09, 0xE9, 0xC6, 0x7B, 0xFC, 0xFF};       // -> 0x3FB2B0
// mov ecx,[rcx]; jmp: 0x3A33C0(arg) completes through 0x3A41E0(M, 7 or 0x9C, arg) (banner, trigger);
// 0x3A37A0(arg) ends it and fires signal 0xF with arg. Both set M+4 |= 0x14.
inline constexpr std::uint8_t kComplete[] = {0x8B, 0x09, 0xE9, 0xF9, 0xFE, 0xF6, 0xFF};                         // -> 0x3A33C0
inline constexpr std::uint8_t kEndSignal[] = {0x8B, 0x09, 0xE9, 0xA9, 0x02, 0xF7, 0xFF};                        // -> 0x3A37A0
inline constexpr std::uint8_t kDeadBoss[] = {0x8B, 0x09, 0xE9, 0x69, 0xFF, 0xF6, 0xFF}; // -> 0x3A3440
#define KH2_PIN(b) b, sizeof(b)
inline constexpr Trap kTraps[] = {
    {5, 0x433670, 0x2, Kind::Value, KH2_PIN(kSetCount), "set_count"},
    {6, 0x433680, 0x1, Kind::Value, KH2_PIN(kIncCount), "inc_count"},
    {8, 0x433740, 0x2, Kind::Value, KH2_PIN(kSetGauge), "set_gauge"},
    {9, 0x433750, 0x2, Kind::Value, KH2_PIN(kAddGauge), "add_gauge"},
    {10, 0x433760, 0x2, Kind::Value, KH2_PIN(kSetGaugeRatio), "set_gauge_ratio"},
    {11, 0x4334E0, 0x0, Kind::Fail, KH2_PIN(kMissionFailed), "mission_failed"},
    {20, 0x433690, 0x1, Kind::Value, KH2_PIN(kDecCount), "dec_count"},
    {41, 0x4336E0, 0x2, Kind::Value, KH2_PIN(kAddCount), "add_count"},
    {3, 0x4334C0, 0x1, Kind::End, KH2_PIN(kComplete), "mission_complete"},
    {22, 0x4334F0, 0x1, Kind::End, KH2_PIN(kEndSignal), "mission_end_signal"},
    {16, 0x4334D0, 0x1, Kind::BossEnd, KH2_PIN(kDeadBoss), "mission_dead_boss"},
};
#undef KH2_PIN
inline constexpr std::size_t kTrapCount = sizeof(kTraps) / sizeof(kTraps[0]);
inline constexpr std::uint8_t kTable = 4;
inline constexpr std::size_t kFailedTrap = 5;  // kTraps index of mission_failed
inline int EndTrapIndex(std::uint8_t index) noexcept {  // kTraps index of an end trap's table index, or -1
    for (std::size_t i = 0; i < kTrapCount; ++i)
        if (kTraps[i].kind == Kind::End && kTraps[i].index == index) return static_cast<int>(i);
    return -1;
}
inline constexpr std::uintptr_t kTrapTables = 0x753490;

// The helpers the client calls itself, pinned whole (set_gauge, set_count) or by the head that
// reads M (mission_failed): exe+0x3FBCB0(f, slot), exe+0x3FBAE0(n, slot), exe+0x3A3800().
inline constexpr std::uintptr_t kSetGaugeRva = 0x3FBCB0, kSetCountRva = 0x3FBAE0, kFailRva = 0x3A3800;
inline constexpr std::uint8_t kSetGaugeBody[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20, 0x0F, 0x28, 0xF0, 0x48, 0x63, 0xDA, 0xE8, 0x2A, 0x7D,
    0xFA, 0xFF, 0x84, 0xC0, 0x74, 0x39, 0xE8, 0xA1, 0x7C, 0xFA, 0xFF, 0x83, 0xB8, 0xA0, 0x06, 0x00, 0x00, 0x00, 0x75, 0x2B,
    0x48, 0x8D, 0x1C, 0xDB, 0xE8, 0x8F, 0x7C, 0xFA, 0xFF, 0xF6, 0x84, 0xD8, 0x68, 0x02, 0x00, 0x00, 0x01, 0x74, 0x18, 0xE8,
    0x80, 0x7C, 0xFA, 0xFF, 0x0F, 0x28, 0xCE, 0x48, 0x8D, 0x88, 0x50, 0x02, 0x00, 0x00, 0x48, 0x8D, 0x0C, 0xD9, 0xE8, 0x0D,
    0xE6, 0xFF, 0xFF, 0x0F, 0x28, 0x74, 0x24, 0x20, 0x48, 0x83, 0xC4, 0x30, 0x5B, 0xC3};
inline constexpr std::uint8_t kSetCountBody[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x63, 0xDA, 0x8B, 0xF9, 0xE8, 0xFC, 0x7E, 0xFA, 0xFF,
    0x84, 0xC0, 0x74, 0x2D, 0x48, 0x8D, 0x1C, 0x9B, 0x48, 0xC1, 0xE3, 0x04, 0xE8, 0x6B, 0x7E, 0xFA, 0xFF, 0xF6, 0x84, 0x18,
    0x78, 0x01, 0x00, 0x00, 0x01, 0x74, 0x16, 0xE8, 0x5C, 0x7E, 0xFA, 0xFF, 0x8B, 0xD7, 0x48, 0x8D, 0x88, 0x60, 0x01, 0x00,
    0x00, 0x48, 0x03, 0xCB, 0xE8, 0x0B, 0xE7, 0xFF, 0xFF, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3};
inline constexpr std::uint8_t kFailHead[] = {0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x0D, 0x5D, 0xC7, 0x66, 0x02, 0x48,
                                             0x8B, 0x41, 0x08, 0x8B, 0x50, 0x18, 0x85, 0xD2, 0x74, 0x0E, 0x8B, 0xCA};

// A client-side trap call. While the host's mission is followed, gauge and counter changes and the
// mission's own ends are dropped (the host's end is replayed instead); a failure runs only after the
// host's. Once the host's end has been replayed here, every later end or failure of that same
// mission is dropped, so it never ends twice.
inline bool DropLocalCall(std::size_t trap, bool following, bool hostFailed, bool replayedEnd, bool scriptedBoss = false) noexcept {
    if (trap >= kTrapCount) return false;
    const Kind kind = kTraps[trap].kind;
    if (kind == Kind::BossEnd) return scriptedBoss && (following || replayedEnd);
    if (replayedEnd && kind != Kind::Value) return true;
    if (!following) return false;
    return kind != Kind::Fail || !hostFailed;
}

}  // namespace kh2coop::inject::missionmirror::rules
