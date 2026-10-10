#pragma once
// Ally player-to-player hit filter (co-op) and its live measurement.
// Unset (default, VUH-1808): Puppet mode, refusing at 3D2060 only the hits whose attacker is a puppet
// actor driven right now (DrivenFn), so a remote player's swing never makes a hit record on the local
// player; with no driven puppet every answer is native. KH2COOP_ALLY_HIT=0: off (no hook);
// =1: refuse every ally player pair and trace them; =trace: trace only. Policy: AllyHitPolicy.hpp.
#include "AllyHitPolicy.hpp"
#include <cstdint>

namespace kh2coop::inject::allyhit {

using LogFn = void (*)(const char* fmt, ...);
using ResolveFn = std::uintptr_t(__fastcall*)(std::uint32_t handle); // the engine's 4AD270 handle lookup
using FrameFn = std::uint32_t (*)();
// Owner thread (3D2060 runs inside the entity update): writes the puppet actors driven right now into
// out[0..1] and returns how many (0..2). Never reads game memory.
using DrivenFn = unsigned (*)(std::uintptr_t (&out)[2]);

constexpr std::uint64_t RVA_CAN_HIT = 0x3D2060;        // bool(attack, victim): may this attack hit this victim
constexpr std::uint64_t RVA_NATIVE_PLAYER = 0x2A105D0; // the canonical (local) player actor
constexpr std::uintptr_t ACTOR_OBJENTRY = 0x918, ACTOR_TEAM = 0x4DC;
constexpr std::uintptr_t OBJENTRY_TYPE = 0x04, OBJENTRY_NAME = 0x08;
constexpr std::uintptr_t ATTACK_OWNER = 0x10, ATTACK_SOURCE = 0x14, ATTACK_TEAM = 0x38, ATTACK_MASK = 0x39, ATTACK_ATKP = 0x30;
constexpr std::uintptr_t ATKP_ID = 0x02, ATKP_KIND = 0x04;
// 3D2060 prologue, exe 9002b2de: mov [rsp+8],rbx; push rdi; sub rsp,20h; mov rdi,rdx; mov rbx,rcx
inline constexpr std::uint8_t kCanHitBytes[16] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9};
constexpr unsigned TRACE_BUDGET = 160;
// Rows the native check already refused (every swing is checked against every player before collision)
// may use at most this share, so refusals stay visible; owner == victim pairs are never traced.
constexpr unsigned TRACE_NATIVE_ZERO_BUDGET = 32;

struct Stats {
    std::uint64_t calls = 0, playerPairs = 0, nativeAllowed = 0, refused = 0, kindKept = 0, faults = 0, viaSource = 0;
    std::uint64_t puppetRefused = 0; // Puppet mode refusals (also counted in refused)
    unsigned traced = 0, tracedNativeZero = 0;
};

// Reads KH2COOP_ALLY_HIT; Off: no hook, no reads. Returns false only on a refused install (then native).
// Puppet mode (the default) also needs `driven`.
bool Install(std::uintptr_t exeBase, LogFn log, ResolveFn resolve, FrameFn frame, DrivenFn driven = nullptr);
// Owner thread, once per frame: a stats line every 600 frames while anything changed.
void Tick(std::uint32_t frame);
Mode CurrentMode();
Stats GetStats();
#ifdef KH2COOP_ALLYHIT_TESTING
using CanHitFn = std::uint64_t(__fastcall*)(std::uintptr_t attack, std::uintptr_t victim);
std::uint64_t __fastcall HookedCanHit(std::uintptr_t attack, std::uintptr_t victim);
void TestSetOriginal(CanHitFn original);
#endif

} // namespace kh2coop::inject::allyhit
