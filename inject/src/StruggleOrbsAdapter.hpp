#pragma once
// VUH-2466 r2: the Struggle orb detours' bodies and their installation, free of MinHook and of the real
// natives, so tests can drive them with fakes (StruggleOrbsHook.cpp supplies the real ones).
//   Gate     one published state. A detour runs its own logic only while every hook is installed
//            (active) and it is on the noted game thread; otherwise it calls its original once and
//            returns its answer, before any native read or callback.
//   Install  creates, queues and applies every hook; only then publishes active. Any failure keeps the
//            gate inactive. A hook never applied was never reachable and is removed (each failed removal
//            counted). Once Apply has run, any hook may be reachable and a thread may be inside its detour,
//            about to call the original trampoline: nothing is removed then. Every hook and trampoline is
//            kept, inactive (passing straight through), for the DLL's lifetime.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "StruggleOrbs.hpp"
#include "StruggleOrbsHook.hpp"

namespace kh2coop::inject::struggleorbs::adapter {

struct Gate {
    std::atomic<bool> active{false};
    std::atomic<std::uint32_t> gameThread{0};
    std::atomic<std::uint64_t> inactive{0}, offThread{0};
    std::atomic<std::uint64_t> births{0}, collects{0}, unresolved{0}, dropsSuppressed{0}, picksSuppressed{0};
    // Reads no native memory and calls nothing.
    bool Admit(std::uint32_t thread) noexcept {
        if (!active.load(std::memory_order_acquire)) { inactive.fetch_add(1, std::memory_order_relaxed); return false; }
        const std::uint32_t owner = gameThread.load(std::memory_order_acquire);
        if (!owner || owner != thread) { offThread.fetch_add(1, std::memory_order_relaxed); return false; }
        return true;
    }
};

// Everything an admitted detour touches beyond its own arguments.
struct Env {
    std::uintptr_t exeBase = 0;
    std::uint32_t (*thread)() = nullptr;
    bool (*read)(std::uintptr_t address, void* out, std::size_t size) = nullptr;
    const Callbacks* cb = nullptr;
};
template <class T> bool Read(const Env& env, std::uintptr_t address, T& out) { return env.read(address, &out, sizeof(T)); }

// FUN_1403dc950. Host: an orb born at the current manager is booked.
template <class Original>
std::uintptr_t Create(Gate& g, const Env& env, Original&& original, std::uintptr_t manager, std::uintptr_t descriptor,
                      const float* point) {
    if (!g.Admit(env.thread())) return original(manager, descriptor, point);
    const std::uintptr_t record = original(manager, descriptor, point);
    if (!record || !manager || !point || !env.cb->hostLive()) return record;
    std::uintptr_t now = 0;
    const int type = DescriptorType(env.exeBase, descriptor);
    float velocity[3] {};
    if (type < 0 || !Read(env, env.exeBase + kManagerRva, now) || now != manager ||
        !env.read(record + kRecordVelocity, velocity, sizeof(velocity)))
        return record;
    g.births.fetch_add(1, std::memory_order_relaxed);
    env.cb->hostBirth(manager, record, static_cast<std::uint8_t>(type), point, velocity);
    return record;
}

// FUN_140410100 / FUN_140410090. Friend: its own drops are skipped (the host's hits on it drop orbs).
template <class Original>
void Drop(Gate& g, const Env& env, Original&& original) {
    if (!g.Admit(env.thread())) { original(); return; }
    if (env.cb->clientFollow()) { g.dropsSuppressed.fetch_add(1, std::memory_order_relaxed); return; }
    original();
}

// FUN_14040ff50 / FUN_140410250. Friend: every pickup is the host's. Host: a pickup switches the record's
// update to the flight (+0x10) and names the collector (+0x48).
template <class Original>
void Pickup(Gate& g, const Env& env, Original&& original, std::uintptr_t record) {
    if (!g.Admit(env.thread())) { original(); return; }
    if (env.cb->clientFollow()) { g.picksSuppressed.fetch_add(1, std::memory_order_relaxed); return; }
    if (!record || !env.cb->hostLive()) { original(); return; }
    std::uintptr_t manager = 0, owner = 0;
    std::uint32_t before = 0;
    const bool host = Read(env, env.exeBase + kManagerRva, manager) && manager && Read(env, record + kRecordManager, owner) &&
                      owner == manager && Read(env, record + kRecordUpdate, before);
    original();
    std::uint32_t after = 0;
    std::uintptr_t actor = 0;
    if (!host || !Read(env, record + kRecordUpdate, after) || after == before || !Read(env, record + kRecordCollector, actor))
        return;
    std::uint16_t netId = 0;
    std::uint8_t collector = actor ? env.cb->collectorOf(actor, netId) : kCollectorNone;
    if (collector == kCollectorNone || (collector == kCollectorEnemy && !netId)) {
        g.unresolved.fetch_add(1, std::memory_order_relaxed);
        collector = kCollectorUnresolved; netId = 0;
    }
    g.collects.fetch_add(1, std::memory_order_relaxed);
    env.cb->hostCollect(manager, record, collector, collector == kCollectorEnemy ? netId : 0);
}

struct InstallResult {
    bool active = false;
    const char* failedAt = nullptr;  // "create", "queue" or "apply"
    std::size_t created = 0, rollbackFailures = 0;
    std::size_t retained = 0;  // after a failed Apply: hooks kept, inactive, for the DLL's lifetime
};
// ops: bool Create(i), bool Queue(i), bool Apply(), bool Remove(i) for hooks 0..n-1.
template <class Ops>
InstallResult Install(Gate& g, std::size_t n, Ops& ops) {
    InstallResult r;
    g.active.store(false, std::memory_order_release);
    const auto fail = [&](const char* where) {
        r.failedAt = where;
        for (std::size_t i = r.created; i-- > 0;) if (!ops.Remove(i)) ++r.rollbackFailures;
        return r;  // never applied, never reachable: reclaimed; the gate stays inactive
    };
    for (; r.created < n; ++r.created) if (!ops.Create(r.created)) return fail("create");
    for (std::size_t i = 0; i < n; ++i) if (!ops.Queue(i)) return fail("queue");
    if (!ops.Apply()) {
        r.failedAt = "apply";
        r.retained = r.created;  // possibly reachable: kept, never removed (see above)
        return r;
    }
    g.active.store(true, std::memory_order_release);
    r.active = true;
    return r;
}

}  // namespace kh2coop::inject::struggleorbs::adapter
