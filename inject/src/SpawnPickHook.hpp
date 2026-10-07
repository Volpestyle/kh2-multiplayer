#pragma once
// SpawnPickHook: the area-script dispatcher detour for host-agreed random spawn picks
// (SpawnPick.hpp has the rules). Default off: KH2COOP_SPAWN_PICK=1 installs it.
#include <cstdint>

namespace kh2coop::inject::spawnpick {

using LogFn = void (*)(const char* fmt, ...);

// The shared inputs for this load, supplied by EnemySync on the game thread. False (with a short
// reason) leaves the native draw in place for this op.
struct Context {
    std::uint64_t salt = 0;
    std::uint32_t epoch = 0;
    std::uint16_t world = 0, room = 0, map = 0, btl = 0, evt = 0;
    std::uint8_t role = 0;  // 1 host, 2 client
};
using ContextFn = bool (*)(Context& out, const char*& reason);
using NoteFn = void (*)(bool shared, std::uint64_t salt);  // EnemySync keys each op to the current load

// Shape-checks the native bytes and installs the detour when KH2COOP_SPAWN_PICK=1. Logs one
// "[spawn-pick] configured=..." line either way. Returns true only when the detour is live.
bool Install(std::uintptr_t exeBase, LogFn log, ContextFn context, NoteFn note);

}  // namespace kh2coop::inject::spawnpick
