#pragma once
// VUH-2881: the Struggle orb detours' bodies and their installation, free of MinHook and of the real
// natives, so tests can drive them with fakes (StruggleOrbsHook.cpp supplies the real ones).
//   Gate     publishes copied policy/collector identities, and serializes receipts from any native
//            thread. Only the entity thread drains to EnemySync. No lock spans a native call.
//   Install  creates, queues and applies every hook; only then publishes active. Any failure keeps the
//            gate inactive. A hook never applied was never reachable and is removed (each failed removal
//            counted). Once Apply has run, any hook may be reachable and a thread may be inside its detour,
//            about to call the original trampoline: nothing is removed then. Every hook and trampoline is
//            kept, inactive (passing straight through), for the DLL's lifetime.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <utility>

#include "StruggleOrbs.hpp"
#include "StruggleOrbsHook.hpp"

namespace kh2coop::inject::struggleorbs::adapter {

enum class Site : std::size_t { Create, HitDrop, Drop, PickA, PickB, Frame, Count };
inline constexpr const char* kSiteNames[] = {"create", "hit-drop", "drop", "pick-a", "pick-b", "NoteGameThread"};

struct Event {
    enum class Kind { Birth, Collect } kind{};
    std::uintptr_t manager{}, record{};  // opaque book keys; never dereferenced by the consumer
    std::uint8_t type{}, collector{};
    std::uint16_t netId{};
    std::array<float, 4> point{};
    std::array<float, 3> velocity{};
};
struct Context {
    Scope scope{};
    std::uint64_t serial{};
    std::vector<Collector> collectors;
};

struct Gate {
    static constexpr std::size_t kCapacity = 4096;
    std::atomic<bool> active{false};
    std::atomic<std::uint32_t> gameThread{0};
    std::atomic<std::uint64_t> inactive{0}, offThread{0};
    std::atomic<std::uint64_t> births{0}, collects{0}, unresolved{0}, dropsSuppressed{0}, picksSuppressed{0};
    std::atomic<std::uint64_t> queued{0}, drained{0}, stale{0}, overflow{0};
    struct ThreadSite {
        std::atomic<std::uint32_t> last{0}, changes{0};
        std::atomic<std::uint64_t> entries{0}, foreign{0};
    } sites[static_cast<std::size_t>(Site::Count)];
    // No lock spans a native call or a callback. In particular the entity thread may wait for KH2's
    // workers, so a worker must never wait for a lock held across an entity/native update.
    std::mutex mutex;
    std::shared_ptr<const Context> context;
    std::vector<Event> pending;
    std::uint64_t serial = 1;

    using Token = std::shared_ptr<const Context>;
    Token Capture(std::uint32_t thread) {
        if (!active.load(std::memory_order_acquire)) { ++inactive; return {}; }
        const auto owner = gameThread.load(std::memory_order_acquire);
        if (owner != thread) ++offThread; // diagnostic now: a worker is marshalled, not refused
        std::lock_guard lock(mutex);
        return context;
    }
    bool Owns(const Token& token) {
        std::lock_guard lock(mutex);
        return token && context && token->serial == context->serial;
    }
    void Reset() {
        std::lock_guard lock(mutex);
        stale += pending.size(); pending.clear(); context.reset();
        ++serial;
    }
    // Only the noted entity thread publishes/drains. Matching scopes retain in-flight tokens while
    // collector snapshots refresh each frame. A changed scope/reset invalidates tokens even if the
    // same native addresses return later (A -> 0 -> A).
    std::vector<Event> Publish(std::uint32_t thread, Scope scope, std::vector<Collector> collectors) {
        if (thread != gameThread.load(std::memory_order_acquire)) return {};
        std::lock_guard lock(mutex);
        const bool live = active.load(std::memory_order_acquire) && scope.mode != Mode::Off && scope.manager &&
                          scope.generation && scope.epoch && scope.delivery;
        if (!live || !context || context->scope != scope) {
            stale += pending.size(); pending.clear(); ++serial;
        }
        if (!live) { context.reset(); return {}; }
        context = std::make_shared<Context>(Context{scope, serial, std::move(collectors)});
        std::vector<Event> out;
        out.swap(pending);
        drained += out.size();
        return out;
    }
    bool Submit(const Token& token, const Event& event) {
        std::lock_guard lock(mutex);
        if (!context || token->serial != context->serial) { ++stale; return false; }
        if (pending.size() == kCapacity) { ++overflow; return false; }
        pending.push_back(event); ++queued;
        return true;
    }
    // Bounded TID changes per site, including the first entry; counts continue after the log cap.
    bool Trace(Site site, std::uint32_t thread) {
        auto& s = sites[static_cast<std::size_t>(site)];
        ++s.entries;
        if (site != Site::Frame && thread != gameThread.load(std::memory_order_acquire)) ++s.foreign;
        return s.last.exchange(thread, std::memory_order_relaxed) != thread &&
               s.changes.fetch_add(1, std::memory_order_relaxed) < 16;
    }
};

struct Env {
    std::uintptr_t exeBase = 0;
    std::uint32_t (*thread)() = nullptr;
    bool (*read)(std::uintptr_t address, void* out, std::size_t size) = nullptr;
    const Callbacks* cb = nullptr; // used only by the entity-thread Drain below
};
template <class T> bool Read(const Env& env, std::uintptr_t address, T& out) { return env.read(address, &out, sizeof(T)); }
inline bool Current(const Env& env, const Gate::Token& token) {
    std::uintptr_t manager = 0;
    return token && Read(env, env.exeBase + kManagerRva, manager) && manager == token->scope.manager;
}

// Called only by Pump after Publish, outside the mailbox lock. No captured native pointer is read.
inline void Drain(Gate& g, const Env& env, const std::vector<Event>& events) {
    for (const auto& e : events) {
        if (e.kind == Event::Kind::Birth) {
            ++g.births;
            env.cb->hostBirth(e.manager, e.record, e.type, e.point.data(), e.velocity.data());
        } else {
            ++g.collects;
            if (e.collector == kCollectorUnresolved) ++g.unresolved;
            env.cb->hostCollect(e.manager, e.record, e.collector, e.netId);
        }
    }
}

// Birth and collect take the same mailbox, including entity-thread births: record reuse cannot let
// a newer birth overtake an earlier worker collection. A native call is made exactly once.
template <class Original>
std::uintptr_t Create(Gate& g, const Env& env, Original&& original, std::uintptr_t manager, std::uintptr_t descriptor,
                      const float* point) {
    const auto token = g.Capture(env.thread());
    const std::uintptr_t record = original(manager, descriptor, point);
    if (!record || !point || !token || token->scope.mode != Mode::Host || manager != token->scope.manager ||
        !Current(env, token)) return record;
    const int type = DescriptorType(env.exeBase, descriptor);
    Event e; e.kind = Event::Kind::Birth; e.manager = manager; e.record = record;
    if (type < 0 || !env.read(reinterpret_cast<std::uintptr_t>(point), e.point.data(), sizeof(e.point)) ||
        !env.read(record + kRecordVelocity, e.velocity.data(), sizeof(e.velocity))) return record;
    e.type = static_cast<std::uint8_t>(type);
    g.Submit(token, e);
    return record;
}

template <class Original>
void Drop(Gate& g, const Env& env, Original&& original) {
    const auto token = g.Capture(env.thread());
    if (token && token->scope.mode == Mode::Follow && Current(env, token) && g.Owns(token)) { ++g.dropsSuppressed; return; }
    original();
}

template <class Original>
void Pickup(Gate& g, const Env& env, Original&& original, std::uintptr_t record) {
    const auto token = g.Capture(env.thread());
    if (!Current(env, token)) { original(); return; }
    if (token->scope.mode == Mode::Follow) {
        if (g.Owns(token)) ++g.picksSuppressed; else original();
        return;
    }
    std::uintptr_t owner = 0, descriptor = 0, actorBefore = 0;
    std::uint32_t before = 0;
    const bool host = record && token->scope.mode == Mode::Host &&
        Read(env, record + kRecordManager, owner) && owner == token->scope.manager &&
        Read(env, record + kRecordDescriptor, descriptor) && DescriptorType(env.exeBase, descriptor) >= 0 &&
        Read(env, record + kRecordCollector, actorBefore) && !actorBefore && Read(env, record + kRecordUpdate, before);
    original();
    std::uint32_t after = 0;
    std::uintptr_t actor = 0, ownerAfter = 0, descriptorAfter = 0;
    if (!host || !Current(env, token) || !Read(env, record + kRecordManager, ownerAfter) || ownerAfter != owner ||
        !Read(env, record + kRecordDescriptor, descriptorAfter) || descriptorAfter != descriptor ||
        !Read(env, record + kRecordUpdate, after) || after == before ||
        !Read(env, record + kRecordCollector, actor) || !actor) return;
    Event e; e.kind = Event::Kind::Collect; e.manager = owner; e.record = record; e.collector = kCollectorUnresolved;
    // Resolve against the identities copied before this native call, not a possibly recycled actor
    // address at drain time. Unknown collectors still remove the orb (the friend fades it).
    for (const auto& c : token->collectors) if (c.actor == actor) {
        e.collector = c.kind; e.netId = c.kind == kCollectorEnemy ? c.netId : 0;
        break;
    }
    if (e.collector == kCollectorNone || (e.collector == kCollectorEnemy && !e.netId)) {
        e.collector = kCollectorUnresolved; e.netId = 0;
    }
    g.Submit(token, e);
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
