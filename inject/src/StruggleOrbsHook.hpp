#pragma once
// VUH-2466: the Struggle orb hooks (StruggleOrbs.hpp has the native facts and the rules). On unless
// KH2COOP_STRUGGLE_ORBS=0; every target's first bytes are pinned, and nothing is hooked on a mismatch.
// No Protocol.hpp here: EntityHook includes this beside HitChannel. Collectors are OrbCollector values.
#include <cstdint>
#include <vector>

namespace kh2coop::inject::struggleorbs {

struct CreateRequest;
struct VanishRequest;

using LogFn = void (*)(const char* fmt, ...);
// OrbCollector's values (Protocol.hpp), checked in StruggleOrbsHook.cpp.
inline constexpr std::uint8_t kCollectorNone = 0, kCollectorHost = 1, kCollectorFriend = 2, kCollectorEnemy = 3,
                               kCollectorUnresolved = 4;

enum class Mode : std::uint8_t { Off, Host, Follow };
// Copied on the entity thread. A worker never consults EnemySync or an actor mapping.
struct Collector { std::uintptr_t actor{}; std::uint8_t kind{}; std::uint16_t netId{}; };
struct Scope {
    std::uint32_t generation{}, epoch{}, transition{}, load{};
    std::uint64_t delivery{};
    std::uintptr_t manager{};
    Mode mode{};
    bool operator==(const Scope&) const = default;
};

// Supplied by EnemySync and EntityHook; every one runs on the game thread.
struct Callbacks {
    void (*hostBirth)(std::uintptr_t manager, std::uintptr_t record, std::uint8_t type, const float* point,
                      const float* velocity) = nullptr;
    void (*hostCollect)(std::uintptr_t manager, std::uintptr_t record, std::uint8_t collector,
                        std::uint16_t netId) = nullptr;
    std::uintptr_t (*actorFor)(std::uint8_t collector, std::uint16_t netId) = nullptr;  // client
    void (*collectors)(std::vector<Collector>& out) = nullptr; // copied host identities, game thread only
};

// Checks the pins and installs the seven detours, all or none (StruggleOrbsAdapter.hpp). Logs one
// "[struggle-orbs] configured=..." line.
bool Install(std::uintptr_t exeBase, LogFn log, const Callbacks& callbacks);
bool Installed();
// EntityHook's frame start. Logs its TID; this is the only thread allowed to drain the mailbox.
void NoteGameThread();
// Called by OrbTick on that thread. Publishes copied policy/identities and books matching receipts.
void Pump(const Scope& scope);
// A room/session reset withdraws policy and retires receipts, including in-flight detour tokens.
void ResetContext();

// This game's orb manager (0 outside a match, or when not installed).
std::uintptr_t Manager();
// Friend: a display orb through the native allocator with the host's scatter; 0 when refused.
std::uintptr_t CreateDisplay(const CreateRequest& request);
// Friend: ends a display orb the host collected, after checking the record is still that orb of this
// manager: it flies to this game's copy of the collector, or, with none (or an unresolved collector),
// fades where it lies through its own life timer. False when the record is not that orb.
bool StartFlight(const VanishRequest& request);

struct HookStats {
    std::uint64_t births{}, collects{}, unresolved{}, dropsSuppressed{}, picksSuppressed{}, offThread{}, inactive{};
    std::uint64_t queued{}, drained{}, stale{}, overflow{}, elided{};
};
HookStats Stats();

}  // namespace kh2coop::inject::struggleorbs
