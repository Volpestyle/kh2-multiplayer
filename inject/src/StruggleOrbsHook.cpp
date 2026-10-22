// StruggleOrbsHook: detours of the Struggle orb allocator, drops and pickups (VUH-2466).
// Host: each orb born at the orb manager, and each pickup, goes to EnemySync's emission outbox.
// Friend, while it follows the host's room: its own drops and pickups are skipped; EnemySync creates
// the host's orbs here and ends them when the host collected them. The detour bodies and the
// all-or-none installation are in StruggleOrbsAdapter.hpp.
#include "StruggleOrbsHook.hpp"
#include "StruggleOrbs.hpp"
#include "StruggleOrbsAdapter.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <MinHook.h>

#include <cstring>
#include <iterator>

namespace kh2coop::inject::struggleorbs {
static_assert(kCollectorNone == static_cast<std::uint8_t>(OrbCollector::None) &&
              kCollectorHost == static_cast<std::uint8_t>(OrbCollector::Host) &&
              kCollectorFriend == static_cast<std::uint8_t>(OrbCollector::Friend) &&
              kCollectorEnemy == static_cast<std::uint8_t>(OrbCollector::Enemy) &&
              kCollectorUnresolved == static_cast<std::uint8_t>(OrbCollector::Unresolved));
namespace {

using CreateFn = std::uintptr_t(__fastcall*)(std::uintptr_t manager, std::uintptr_t descriptor, const float* point);
using PassFn = void(__fastcall*)(void* a, void* b, void* c, void* d);
using FlyFn = void(__fastcall*)(std::uintptr_t record, std::uintptr_t actor, int mode);

LogFn g_log = nullptr;
Callbacks g_cb{};
std::uintptr_t g_exeBase = 0;
CreateFn g_create = nullptr;
PassFn g_hitDrop = nullptr, g_drop = nullptr, g_pickA = nullptr, g_pickB = nullptr;
FlyFn g_fly = nullptr;
adapter::Gate g_gate;
adapter::Env g_env{};

bool CopyCode(std::uintptr_t address, void* out, std::size_t size) {
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
std::uint32_t ThreadNow() { return static_cast<std::uint32_t>(GetCurrentThreadId()); }
bool ReadNative(std::uintptr_t address, void* out, std::size_t size) {
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}
template <class T> T Field(std::uintptr_t address) {
    T v{};
    std::memcpy(&v, reinterpret_cast<const void*>(address), sizeof(T));
    return v;
}
bool Active() { return g_gate.active.load(std::memory_order_acquire); }

std::uintptr_t __fastcall CreateDetour(std::uintptr_t manager, std::uintptr_t descriptor, const float* point) {
    return adapter::Create(g_gate, g_env, [](std::uintptr_t m, std::uintptr_t d, const float* p) { return g_create(m, d, p); },
                           manager, descriptor, point);
}
void __fastcall HitDropDetour(void* a, void* b, void* c, void* d) {
    adapter::Drop(g_gate, g_env, [&] { g_hitDrop(a, b, c, d); });
}
void __fastcall DropDetour(void* a, void* b, void* c, void* d) {
    adapter::Drop(g_gate, g_env, [&] { g_drop(a, b, c, d); });
}
void __fastcall PickADetour(void* a, void* b, void* c, void* d) {
    adapter::Pickup(g_gate, g_env, [&] { g_pickA(a, b, c, d); }, reinterpret_cast<std::uintptr_t>(c));
}
void __fastcall PickBDetour(void* a, void* b, void* c, void* d) {
    adapter::Pickup(g_gate, g_env, [&] { g_pickB(a, b, c, d); }, reinterpret_cast<std::uintptr_t>(b));
}

struct Target { std::uintptr_t rva; void* detour; void** original; };
const Target kTargets[] = {
    {kCreateRva, reinterpret_cast<void*>(&CreateDetour), reinterpret_cast<void**>(&g_create)},
    {kHitDropRva, reinterpret_cast<void*>(&HitDropDetour), reinterpret_cast<void**>(&g_hitDrop)},
    {kDropRva, reinterpret_cast<void*>(&DropDetour), reinterpret_cast<void**>(&g_drop)},
    {kPickARva, reinterpret_cast<void*>(&PickADetour), reinterpret_cast<void**>(&g_pickA)},
    {kPickBRva, reinterpret_cast<void*>(&PickBDetour), reinterpret_cast<void**>(&g_pickB)},
};
struct MinHookOps {
    std::uintptr_t base;
    MH_STATUS last = MH_OK;
    void* At(std::size_t i) const { return reinterpret_cast<void*>(base + kTargets[i].rva); }
    bool Create(std::size_t i) { return (last = MH_CreateHook(At(i), kTargets[i].detour, kTargets[i].original)) == MH_OK; }
    bool Queue(std::size_t i) { return (last = MH_QueueEnableHook(At(i))) == MH_OK; }
    bool Apply() { return (last = MH_ApplyQueued()) == MH_OK; }
    bool Remove(std::size_t i) { return MH_RemoveHook(At(i)) == MH_OK; }
};

}  // namespace

bool Install(std::uintptr_t exeBase, LogFn log, const Callbacks& callbacks) {
    g_log = log;
    char setting[2] {};
    if (GetEnvironmentVariableA("KH2COOP_STRUGGLE_ORBS", setting, sizeof(setting)) == 1 && setting[0] == '0') {
        if (log) log("[struggle-orbs] configured=0 (KH2COOP_STRUGGLE_ORBS=0): each game keeps its own orbs");
        return false;
    }
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
    // Every prerequisite is in place before the first hook exists; the gate opens only after all five apply.
    g_exeBase = exeBase;
    g_cb = callbacks;
    g_fly = reinterpret_cast<FlyFn>(exeBase + kFlyRva);
    g_env.exeBase = exeBase;
    g_env.thread = &ThreadNow;
    g_env.read = &ReadNative;
    g_env.cb = &g_cb;
    MinHookOps ops{exeBase};
    const auto r = adapter::Install(g_gate, std::size(kTargets), ops);
    if (!r.active) {
        if (log) log("[struggle-orbs] configured=1 hooked=0 reason=minhook at=%s status=%d created=%zu rollbackFailures=%zu%s",
                     r.failedAt, static_cast<int>(ops.last), r.created, r.rollbackFailures,
                     r.rollbackFailures ? " (those detours stay, inactive: they pass straight through)" : "");
        return false;
    }
    if (log) log("[struggle-orbs] configured=1 hooked=1 create=%llX drops=%llX/%llX picks=%llX/%llX manager=%llX",
                 static_cast<unsigned long long>(kCreateRva), static_cast<unsigned long long>(kHitDropRva),
                 static_cast<unsigned long long>(kDropRva), static_cast<unsigned long long>(kPickARva),
                 static_cast<unsigned long long>(kPickBRva), static_cast<unsigned long long>(kManagerRva));
    return true;
}

bool Installed() { return Active(); }

void NoteGameThread() {
    g_gate.gameThread.store(ThreadNow(), std::memory_order_release);
    // A stats line every 600 frames while anything changed (and never when nothing happened).
    static unsigned frames = 0;
    static HookStats logged{};
    if (!Active() || !g_log || ++frames < 600) return;
    frames = 0;
    const HookStats n = Stats();
    if (n.births == logged.births && n.collects == logged.collects && n.dropsSuppressed == logged.dropsSuppressed &&
        n.picksSuppressed == logged.picksSuppressed && n.offThread == logged.offThread)
        return;
    logged = n;
    g_log("[struggle-orbs] stats births=%llu collects=%llu unresolved=%llu dropsSuppressed=%llu picksSuppressed=%llu offThread=%llu",
          static_cast<unsigned long long>(n.births), static_cast<unsigned long long>(n.collects),
          static_cast<unsigned long long>(n.unresolved), static_cast<unsigned long long>(n.dropsSuppressed),
          static_cast<unsigned long long>(n.picksSuppressed), static_cast<unsigned long long>(n.offThread));
}

std::uintptr_t Manager() { return Active() ? Field<std::uintptr_t>(g_exeBase + kManagerRva) : 0; }

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
    const std::uintptr_t actor = request.collector == OrbCollector::Unresolved
                                     ? 0 : g_cb.actorFor(static_cast<std::uint8_t>(request.collector), request.netId);
    if (!actor) {
        // No copy of the collector here (or the host could not name one): the orb fades out where it lies
        // through its own life timer, rather than flying to someone who did not take it.
        *reinterpret_cast<std::uint32_t*>(request.record + kRecordFlags) &= ~kFlagKeep;
        *reinterpret_cast<float*>(request.record + kRecordLife) = kFadeLife;
        return true;
    }
    g_fly(request.record, actor, 0);
    return true;
}

HookStats Stats() {
    HookStats s;
    s.births = g_gate.births.load(std::memory_order_relaxed);
    s.collects = g_gate.collects.load(std::memory_order_relaxed);
    s.unresolved = g_gate.unresolved.load(std::memory_order_relaxed);
    s.dropsSuppressed = g_gate.dropsSuppressed.load(std::memory_order_relaxed);
    s.picksSuppressed = g_gate.picksSuppressed.load(std::memory_order_relaxed);
    s.offThread = g_gate.offThread.load(std::memory_order_relaxed);
    s.inactive = g_gate.inactive.load(std::memory_order_relaxed);
    return s;
}

}  // namespace kh2coop::inject::struggleorbs
