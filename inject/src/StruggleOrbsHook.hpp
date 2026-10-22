#pragma once
// VUH-2466: the Struggle orb hooks (StruggleOrbs.hpp has the native facts and the rules). On unless
// KH2COOP_STRUGGLE_ORBS=0; every target's first bytes are pinned, and nothing is hooked on a mismatch.
// No Protocol.hpp here: EntityHook includes this beside HitChannel. Collectors are OrbCollector values.
#include <cstdint>

namespace kh2coop::inject::struggleorbs {

struct CreateRequest;
struct VanishRequest;

using LogFn = void (*)(const char* fmt, ...);
// OrbCollector's values (Protocol.hpp), checked in StruggleOrbsHook.cpp.
inline constexpr std::uint8_t kCollectorNone = 0, kCollectorHost = 1, kCollectorFriend = 2, kCollectorEnemy = 3;

// Supplied by EnemySync and EntityHook; every one runs on the game thread.
struct Callbacks {
    bool (*hostLive)() = nullptr;     // this game is the session host, in its activation context
    bool (*clientFollow)() = nullptr; // this game is a client in the host's room: show the host's orbs
    void (*hostBirth)(std::uintptr_t manager, std::uintptr_t record, std::uint8_t type, const float* point,
                      const float* velocity) = nullptr;
    void (*hostCollect)(std::uintptr_t manager, std::uintptr_t record, std::uint8_t collector,
                        std::uint16_t netId) = nullptr;
    std::uint8_t (*collectorOf)(std::uintptr_t actor, std::uint16_t& netId) = nullptr;  // host
    std::uintptr_t (*actorFor)(std::uint8_t collector, std::uint16_t netId) = nullptr;  // client
};

// Checks the pins and installs the five detours. Logs one "[struggle-orbs] configured=..." line.
bool Install(std::uintptr_t exeBase, LogFn log, const Callbacks& callbacks);
bool Installed();
// EntityHook's frame start, on the game thread. A hook that runs on any other thread passes straight
// through to the native code (and counts offThread).
void NoteGameThread();

// This game's orb manager (0 outside a match, or when not installed).
std::uintptr_t Manager();
// Friend: a display orb through the native allocator with the host's scatter; 0 when refused.
std::uintptr_t CreateDisplay(const CreateRequest& request);
// Friend: starts a display orb's flight to the host's collector, after checking the record is still that
// orb of this manager. False when it is not, or no collector actor resolves.
bool StartFlight(const VanishRequest& request);

struct HookStats { std::uint64_t births{}, collects{}, dropsSuppressed{}, picksSuppressed{}, unknownCollectors{}, offThread{}; };
HookStats Stats();

}  // namespace kh2coop::inject::struggleorbs
