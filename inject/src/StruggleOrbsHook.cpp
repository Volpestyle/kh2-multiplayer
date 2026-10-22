// StruggleOrbsHook: detours of the Struggle orb allocator, drops and pickups (VUH-2466).
// Host: each orb born at the orb manager, and each pickup, goes to EnemySync's emission outbox.
// Friend, while it follows the host's room: its own drops and pickups are skipped; EnemySync creates
// the host's orbs here and starts their flights when the host collected them.
#include "StruggleOrbsHook.hpp"
#include "StruggleOrbs.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <MinHook.h>

#include <cstring>

namespace kh2coop::inject::struggleorbs {
static_assert(kCollectorNone == static_cast<std::uint8_t>(OrbCollector::None) &&
              kCollectorHost == static_cast<std::uint8_t>(OrbCollector::Host) &&
              kCollectorFriend == static_cast<std::uint8_t>(OrbCollector::Friend) &&
              kCollectorEnemy == static_cast<std::uint8_t>(OrbCollector::Enemy));
namespace {

using CreateFn = std::uintptr_t(__fastcall*)(std::uintptr_t manager, std::uintptr_t descriptor, const float* point);
using PassFn = void(__fastcall*)(void* a, void* b, void* c, void* d);
using FlyFn = void(__fastcall*)(std::uintptr_t record, std::uintptr_t actor, int mode);

LogFn g_log = nullptr;
Callbacks g_cb{};
std::uintptr_t g_exeBase = 0;
bool g_installed = false;
CreateFn g_create = nullptr;
PassFn g_hitDrop = nullptr, g_drop = nullptr, g_pickA = nullptr, g_pickB = nullptr;
FlyFn g_fly = nullptr;
HookStats g_stats{};
DWORD g_gameThread = 0;  // noted each frame by EntityHook; the hooks act only there

bool CopyCode(std::uintptr_t address, void* out, std::size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
template <class T> T Field(std::uintptr_t address) {
    T v{};
    std::memcpy(&v, reinterpret_cast<const void*>(address), sizeof(T));
    return v;
}
std::uintptr_t ManagerNow() { return Field<std::uintptr_t>(g_exeBase + kManagerRva); }
bool OnGameThread() {
    if (g_gameThread && GetCurrentThreadId() == g_gameThread) return true;
    ++g_stats.offThread;
    return false;
}
bool Following() { return g_cb.clientFollow && OnGameThread() && g_cb.clientFollow(); }
bool HostLive() { return g_cb.hostLive && OnGameThread() && g_cb.hostLive(); }

std::uintptr_t __fastcall CreateDetour(std::uintptr_t manager, std::uintptr_t descriptor, const float* point) {
    const std::uintptr_t record = g_create(manager, descriptor, point);
    if (!record || !manager || !point || manager != ManagerNow() || !g_cb.hostBirth) return record;
    const int type = DescriptorType(g_exeBase, descriptor);
    if (type < 0 || !HostLive()) return record;
    float velocity[3] {};
    std::memcpy(velocity, reinterpret_cast<const void*>(record + kRecordVelocity), sizeof(velocity));
    ++g_stats.births;
    g_cb.hostBirth(manager, record, static_cast<std::uint8_t>(type), point, velocity);
    return record;
}

void __fastcall HitDropDetour(void* a, void* b, void* c, void* d) {
    if (Following()) { ++g_stats.dropsSuppressed; return; }  // the host's hit on this player drops its orbs
    g_hitDrop(a, b, c, d);
}
void __fastcall DropDetour(void* a, void* b, void* c, void* d) {
    if (Following()) { ++g_stats.dropsSuppressed; return; }
    g_drop(a, b, c, d);
}

// Host: a pickup changes the record's update to the flight (+0x10) and names the collector (+0x48).
void Pickup(PassFn original, void* a, void* b, void* c, void* d, std::uintptr_t record) {
    if (Following()) { ++g_stats.picksSuppressed; return; }  // the host decides every pickup
    const std::uintptr_t manager = ManagerNow();
    const bool host = record && manager && Field<std::uintptr_t>(record + kRecordManager) == manager && HostLive();
    const std::uint32_t before = host ? Field<std::uint32_t>(record + kRecordUpdate) : 0;
    original(a, b, c, d);
    if (!host || Field<std::uint32_t>(record + kRecordUpdate) == before || !g_cb.hostCollect) return;
    const std::uintptr_t actor = Field<std::uintptr_t>(record + kRecordCollector);
    std::uint16_t netId = 0;
    const std::uint8_t collector = actor && g_cb.collectorOf ? g_cb.collectorOf(actor, netId) : 0;
    if (collector == static_cast<std::uint8_t>(OrbCollector::None)) ++g_stats.unknownCollectors;
    ++g_stats.collects;
    g_cb.hostCollect(manager, record, collector, collector == static_cast<std::uint8_t>(OrbCollector::Enemy) ? netId : 0);
}
void __fastcall PickADetour(void* a, void* b, void* c, void* d) {
    Pickup(g_pickA, a, b, c, d, reinterpret_cast<std::uintptr_t>(c));
}
void __fastcall PickBDetour(void* a, void* b, void* c, void* d) {
    Pickup(g_pickB, a, b, c, d, reinterpret_cast<std::uintptr_t>(b));
}

}  // namespace

bool Install(std::uintptr_t exeBase, LogFn log, const Callbacks& callbacks) {
    g_log = log;
    char setting[2] {};
    if (GetEnvironmentVariableA("KH2COOP_STRUGGLE_ORBS", setting, sizeof(setting)) == 1 && setting[0] == '0') {
        if (log) log("[struggle-orbs] configured=0 (KH2COOP_STRUGGLE_ORBS=0): each game keeps its own orbs");
        return false;
    }
    g_exeBase = exeBase;
    g_cb = callbacks;
    for (const auto& s : kShapes) {
        std::uint8_t bytes[sizeof(s.bytes)] {};
        if (!CopyCode(exeBase + s.rva, bytes, sizeof(bytes)) || std::memcmp(bytes, s.bytes, sizeof(bytes)) != 0) {
            if (log) log("[struggle-orbs] configured=1 hooked=0 reason=shape rva=%llX", static_cast<unsigned long long>(s.rva));
            return false;
        }
    }
    if (!callbacks.hostLive || !callbacks.clientFollow || !callbacks.hostBirth || !callbacks.hostCollect ||
        !callbacks.collectorOf || !callbacks.actorFor) {
        if (log) log("[struggle-orbs] configured=1 hooked=0 reason=callbacks");
        return false;
    }
    struct Target { std::uintptr_t rva; void* detour; void** original; };
    const Target targets[] = {
        {kCreateRva, reinterpret_cast<void*>(&CreateDetour), reinterpret_cast<void**>(&g_create)},
        {kHitDropRva, reinterpret_cast<void*>(&HitDropDetour), reinterpret_cast<void**>(&g_hitDrop)},
        {kDropRva, reinterpret_cast<void*>(&DropDetour), reinterpret_cast<void**>(&g_drop)},
        {kPickARva, reinterpret_cast<void*>(&PickADetour), reinterpret_cast<void**>(&g_pickA)},
        {kPickBRva, reinterpret_cast<void*>(&PickBDetour), reinterpret_cast<void**>(&g_pickB)},
    };
    std::size_t created = 0;
    MH_STATUS st = MH_OK;
    for (; created < std::size(targets) && st == MH_OK; ++created)
        st = MH_CreateHook(reinterpret_cast<void*>(exeBase + targets[created].rva), targets[created].detour,
                           targets[created].original);
    if (st != MH_OK) --created;
    // All or none: a partly hooked set could drop the friend's orbs without showing the host's.
    if (st == MH_OK)
        for (const auto& t : targets)
            if ((st = MH_QueueEnableHook(reinterpret_cast<void*>(exeBase + t.rva))) != MH_OK) break;
    if (st == MH_OK) st = MH_ApplyQueued();
    if (st != MH_OK) {
        for (std::size_t i = 0; i < created; ++i) MH_RemoveHook(reinterpret_cast<void*>(exeBase + targets[i].rva));
        if (log) log("[struggle-orbs] configured=1 hooked=0 reason=minhook status=%d", static_cast<int>(st));
        return false;
    }
    g_fly = reinterpret_cast<FlyFn>(exeBase + kFlyRva);
    g_installed = true;
    if (log) log("[struggle-orbs] configured=1 hooked=1 create=%llX drops=%llX/%llX picks=%llX/%llX manager=%llX",
                 static_cast<unsigned long long>(kCreateRva), static_cast<unsigned long long>(kHitDropRva),
                 static_cast<unsigned long long>(kDropRva), static_cast<unsigned long long>(kPickARva),
                 static_cast<unsigned long long>(kPickBRva), static_cast<unsigned long long>(kManagerRva));
    return true;
}

bool Installed() { return g_installed; }
void NoteGameThread() {
    g_gameThread = GetCurrentThreadId();
    // A stats line every 600 frames while anything changed (and never when nothing happened).
    static unsigned frames = 0;
    static HookStats logged{};
    if (!g_installed || !g_log || ++frames < 600) return;
    frames = 0;
    const auto& n = g_stats;
    if (n.births == logged.births && n.collects == logged.collects && n.dropsSuppressed == logged.dropsSuppressed &&
        n.picksSuppressed == logged.picksSuppressed && n.offThread == logged.offThread)
        return;
    logged = n;
    g_log("[struggle-orbs] stats births=%llu collects=%llu unknownCollectors=%llu dropsSuppressed=%llu picksSuppressed=%llu offThread=%llu",
          static_cast<unsigned long long>(n.births), static_cast<unsigned long long>(n.collects),
          static_cast<unsigned long long>(n.unknownCollectors), static_cast<unsigned long long>(n.dropsSuppressed),
          static_cast<unsigned long long>(n.picksSuppressed), static_cast<unsigned long long>(n.offThread));
}
std::uintptr_t Manager() { return g_installed ? ManagerNow() : 0; }

std::uintptr_t CreateDisplay(const CreateRequest& request) {
    const std::uintptr_t manager = Manager();
    if (!manager || request.type >= ORB_TYPES) return 0;
    alignas(16) float point[4] {request.point[0], request.point[1], request.point[2], request.point[3]};
    const std::uintptr_t record = g_create(manager, g_exeBase + kDescriptorsRva + kDescriptorStride * request.type, point);
    if (!record) return 0;
    // The host's scatter replaces this game's draw; like both native callers, the orb keeps until taken.
    std::memcpy(reinterpret_cast<void*>(record + kRecordVelocity), request.velocity.data(), sizeof(float) * 3);
    *reinterpret_cast<std::uint32_t*>(record + kRecordFlags) |= kFlagKeep;
    return record;
}

bool StartFlight(const VanishRequest& request) {
    const std::uintptr_t manager = Manager();
    if (!manager || !request.record || request.type >= ORB_TYPES) return false;
    const std::uintptr_t descriptor = g_exeBase + kDescriptorsRva + kDescriptorStride * request.type;
    if (Field<std::uintptr_t>(request.record + kRecordManager) != manager ||
        Field<std::uintptr_t>(request.record + kRecordDescriptor) != descriptor ||
        Field<std::uintptr_t>(request.record + kRecordCollector) != 0)
        return false;  // not that display orb any more, or already flying
    const std::uintptr_t actor =
        g_cb.actorFor ? g_cb.actorFor(static_cast<std::uint8_t>(request.collector), request.netId) : 0;
    if (!actor) return false;
    g_fly(request.record, actor, 0);
    return true;
}

HookStats Stats() { return g_stats; }

}  // namespace kh2coop::inject::struggleorbs
