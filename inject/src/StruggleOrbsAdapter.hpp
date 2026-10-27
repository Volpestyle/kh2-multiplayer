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
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "StruggleOrbs.hpp"
#include "StruggleOrbsHook.hpp"

namespace kh2coop::inject::struggleorbs::adapter {

enum class Site : std::size_t { Create, Init, HitDrop, Drop, PickA, PickB, Fly, Frame, Count };
inline constexpr const char* kSiteNames[] = {"create", "init", "hit-drop", "drop", "pick-a", "pick-b", "fly", "NoteGameThread"};

struct Event {
    enum class Kind { Birth, Collect } kind{};
    std::uintptr_t manager{}, record{};  // record is an incarnation key, never a native pointer at drain
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
    std::atomic<std::uint64_t> queued{0}, drained{0}, stale{0}, overflow{0}, elided{0};
    struct ThreadSite {
        std::atomic<std::uint32_t> last{0}, changes{0};
        std::atomic<std::uint64_t> entries{0}, foreign{0};
    } sites[static_cast<std::size_t>(Site::Count)];
    // No lock spans a native call or a callback. In particular the entity thread may wait for KH2's
    // workers, so a worker must never wait for a lock held across an entity/native update.
    std::mutex mutex;
    std::shared_ptr<const Context> context;
    struct Receipt {
        Event event{};
        bool ready = false, cancelled = false, consumed = false;
        std::uintptr_t nativeRecord{};
        std::shared_ptr<Receipt> birth;
    };
    using Ticket = std::shared_ptr<Receipt>;
    std::deque<Ticket> pending;
    std::unordered_map<std::uintptr_t, Ticket> records;
    std::uintptr_t incarnation = 0;
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
        stale += pending.size(); pending.clear(); records.clear(); context.reset();
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
            stale += pending.size(); pending.clear(); records.clear(); ++serial;
        }
        if (!live) { context.reset(); return {}; }
        context = std::make_shared<Context>(Context{scope, serial, std::move(collectors)});
        std::vector<Event> out;
        // A birth is reserved BEFORE native initialization can publish its record. Do not let a
        // completed collect pass its unfinished birth. A native call never waits for this drain.
        while (!pending.empty() && pending.front()->ready) {
            auto receipt = std::move(pending.front()); pending.pop_front(); ++drained;
            receipt->consumed = true;
            if (receipt->cancelled || (receipt->birth && receipt->birth->cancelled)) {
                if (receipt->event.kind == Event::Kind::Birth) ++elided;
                continue;
            }
            out.push_back(receipt->event);
        }
        return out;
    }
    bool CurrentLocked(const Token& token) const {
        return token && context && token->serial == context->serial;
    }
    void RetireRecord(std::uintptr_t nativeRecord) {
        std::lock_guard lock(mutex);
        const auto it = records.find(nativeRecord);
        if (it == records.end()) return;
        if (!it->second->consumed) it->second->cancelled = true;
        records.erase(it); // even an untracked initializer must not leave the old incarnation current
    }
    Ticket ReserveBirth(const Token& token, std::uintptr_t nativeRecord, std::uint8_t type,
                        const std::array<float, 4>& point) {
        std::lock_guard lock(mutex);
        if (!CurrentLocked(token)) { ++stale; return {}; }
        if (pending.size() == kCapacity || (!records.contains(nativeRecord) && records.size() == kCapacity) ||
            incarnation == UINTPTR_MAX) { ++overflow; return {}; }
        auto& prior = records[nativeRecord];
        if (prior && !prior->consumed) prior->cancelled = true; // reused before a birth was observable
        auto receipt = std::make_shared<Receipt>();
        receipt->nativeRecord = nativeRecord;
        receipt->event.kind = Event::Kind::Birth; receipt->event.manager = token->scope.manager;
        receipt->event.record = ++incarnation; receipt->event.type = type; receipt->event.point = point;
        prior = receipt; pending.push_back(receipt); ++queued;
        return receipt;
    }
    bool IsCurrentBirth(const Token& token, const Ticket& birth) {
        std::lock_guard lock(mutex);
        const auto it = birth ? records.find(birth->nativeRecord) : records.end();
        return CurrentLocked(token) && birth && !birth->cancelled && it != records.end() && it->second == birth;
    }
    void CompleteBirth(const Token& token, const Ticket& birth, const std::array<float, 3>& velocity, bool valid) {
        std::lock_guard lock(mutex);
        if (!birth) return;
        if (!CurrentLocked(token)) { ++stale; return; }
        const auto it = records.find(birth->nativeRecord);
        birth->cancelled = birth->cancelled || !valid || it == records.end() || it->second != birth;
        birth->event.velocity = velocity; birth->ready = true;
    }
    Ticket ReserveCollect(const Token& token, std::uintptr_t nativeRecord, std::uint8_t collector, std::uint16_t netId) {
        std::lock_guard lock(mutex);
        if (!CurrentLocked(token)) { ++stale; return {}; }
        const auto it = records.find(nativeRecord);
        if (it == records.end()) return {}; // born before this scope was observed
        if (pending.size() == kCapacity) { ++overflow; return {}; }
        auto receipt = std::make_shared<Receipt>(); receipt->birth = it->second;
        receipt->event.kind = Event::Kind::Collect; receipt->event.manager = token->scope.manager;
        receipt->event.record = it->second->event.record;
        receipt->event.collector = collector; receipt->event.netId = netId;
        // A collection before the birth's owner drain is an atomic elision. Never read its flight
        // velocity as birth scatter, or show an already-collected/reused native record to a friend.
        if (!it->second->consumed) it->second->cancelled = true;
        pending.push_back(receipt); ++queued; return receipt;
    }
    void CompleteCollect(const Token& token, const Ticket& receipt, bool valid = true) {
        std::lock_guard lock(mutex);
        if (!receipt) return;
        if (!CurrentLocked(token)) { ++stale; return; }
        receipt->cancelled = !valid; receipt->ready = true;
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
    std::uintptr_t (*stack)() = nullptr; // current native/fiber stack identity, production supplies StackBase
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

// Per-native-create call, on the calling stack. The initializer has the record argument before it
// writes/publishes that incarnation; the outer allocator does not know it until too late. Select by
// native stack identity as well as arguments: TLS alone must not borrow another fiber's call.
struct BirthCall {
    inline static thread_local BirthCall* calls = nullptr;
    BirthCall* next = calls;
    Gate::Token token;
    Gate::Ticket ticket;
    std::uintptr_t manager{}, descriptor{}, pointAddress{}, stack{};
    std::array<float, 4> point{};
    BirthCall() { calls = this; }
    BirthCall(const BirthCall&) = delete;
    BirthCall& operator=(const BirthCall&) = delete;
    ~BirthCall() {
        for (auto** at = &calls; *at; at = &(*at)->next) if (*at == this) { *at = next; break; }
    }
};
inline std::uintptr_t Stack(const Env& env) { return env.stack ? env.stack() : env.thread(); }

template <class Original>
std::uintptr_t Init(Gate& g, const Env& env, Original&& original, std::uintptr_t record, std::uintptr_t manager,
                    std::uintptr_t descriptor, const float* point, std::uintptr_t update) {
    g.RetireRecord(record);
    const auto stack = Stack(env);
    for (auto* call = BirthCall::calls; call; call = call->next) {
        if (call->stack != stack || call->manager != manager || call->descriptor != descriptor ||
            call->pointAddress != reinterpret_cast<std::uintptr_t>(point)) continue;
        if (record && call->token && !call->ticket && Current(env, call->token))
            call->ticket = g.ReserveBirth(call->token, record,
                static_cast<std::uint8_t>(DescriptorType(env.exeBase, descriptor)), call->point);
        break;
    }
    return original(record, manager, descriptor, point, update);
}

template <class Original>
std::uintptr_t Create(Gate& g, const Env& env, Original&& original, std::uintptr_t manager, std::uintptr_t descriptor,
                      const float* point) {
    BirthCall call;
    call.token = g.Capture(env.thread()); call.manager = manager; call.descriptor = descriptor;
    call.pointAddress = reinterpret_cast<std::uintptr_t>(point); call.stack = Stack(env);
    const int type = DescriptorType(env.exeBase, descriptor);
    if (!call.token || call.token->scope.mode != Mode::Host || manager != call.token->scope.manager ||
        type < 0 || !point || !env.read(call.pointAddress, call.point.data(), sizeof(call.point))) call.token.reset();
    const auto finish = [&](std::uintptr_t record) {
        std::array<float, 3> velocity{};
        const bool valid = record && call.ticket && call.ticket->nativeRecord == record && Current(env, call.token) &&
            g.IsCurrentBirth(call.token, call.ticket) && env.read(record + kRecordVelocity, velocity.data(), sizeof(velocity)) &&
            g.IsCurrentBirth(call.token, call.ticket);
        g.CompleteBirth(call.token, call.ticket, velocity, valid);
    };
    std::uintptr_t record;
    try { record = original(manager, descriptor, point); }
    catch (...) { finish(0); throw; }
    finish(record);
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
    original();
    (void)record; // host collection is observed at the native flight transition, before this call returns
}

// FUN_413650 unconditionally starts flight to its actor argument. Capture the incarnation BEFORE
// calling it, not from record fields after a pickup returns (the pool may already have reused them).
template<class Original>
void Flight(Gate& g, const Env& env, Original&& original, std::uintptr_t record, std::uintptr_t actor, int mode) {
    const auto token = g.Capture(env.thread());
    std::uintptr_t manager = 0, descriptor = 0;
    Gate::Ticket receipt;
    if (record && actor && mode == 0 && token && token->scope.mode == Mode::Host && Current(env, token) &&
        Read(env, record + kRecordManager, manager) && manager == token->scope.manager &&
        Read(env, record + kRecordDescriptor, descriptor) && DescriptorType(env.exeBase, descriptor) >= 0) {
        std::uint8_t collector = kCollectorUnresolved; std::uint16_t netId = 0;
        // Resolve copied identities before native code, never a recycled address at owner drain.
        for (const auto& c : token->collectors) if (c.actor == actor) {
            collector = c.kind; netId = c.kind == kCollectorEnemy ? c.netId : 0;
            break;
        }
        if (collector == kCollectorNone || (collector == kCollectorEnemy && !netId)) {
            collector = kCollectorUnresolved; netId = 0;
        }
        receipt = g.ReserveCollect(token, record, collector, netId);
    }
    try { original(record, actor, mode); }
    catch (...) { g.CompleteCollect(token, receipt, false); throw; } // no false collect or unfinished prefix on unwind
    g.CompleteCollect(token, receipt);
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
