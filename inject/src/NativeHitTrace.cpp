#include "NativeHitTrace.hpp"
#include <Windows.h>
#include <atomic>
#include <cstring>
#include <limits>
#include <type_traits>

namespace kh2coop::inject::nativehittrace {
namespace {
std::atomic<std::uint64_t> g_config {0}; // serial in upper bits; requested/verified/installed in low byte
std::atomic<DWORD> g_owner {0};
std::atomic<std::uint64_t> g_sequence {0}, g_started {0}, g_published {0}, g_drained {0};
std::atomic<std::uint64_t> g_dropped {0}, g_foreign {0}, g_unwound {0}, g_nested {0}, g_overflow {0}, g_unmatched {0};
SRWLOCK g_lock = SRWLOCK_INIT;
Event g_queue[QueueCapacity] {};
unsigned g_read = 0, g_size = 0;
thread_local ApplyToken* g_apply = nullptr;
thread_local ChildToken* g_take = nullptr;
thread_local unsigned g_depth = 0;
static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_destructible_v<ApplyToken> && std::is_trivially_destructible_v<ChildToken>);
std::uint32_t Coverage(std::uint64_t config) noexcept { return static_cast<std::uint32_t>((config >> 1) & (config >> 4) & AllHooks); }
bool ActorCompleteRead(const ActorSnapshot& a) noexcept {
    return (a.readMask & ActorComplete) == ActorComplete && a.actor && a.objentry && a.status;
}
bool SameActor(const ActorSnapshot& a, const ActorSnapshot& b) noexcept {
    return ActorCompleteRead(a) && ActorCompleteRead(b) && a.actor == b.actor && a.objentry == b.objentry &&
        a.status == b.status && a.objectId == b.objectId && a.type == b.type && a.team == b.team &&
        a.namePrefix == b.namePrefix && a.maxHp == b.maxHp;
}
bool SameContext(const Context& a, const Context& b) noexcept {
    return a.available && b.available && (a.readMask & ContextComplete) == ContextComplete &&
        (b.readMask & ContextComplete) == ContextComplete && a.frame == b.frame && a.generation == b.generation &&
        a.epoch == b.epoch && a.transitionSerial == b.transitionSerial && a.loadSerial == b.loadSerial &&
        a.connectionId == b.connectionId && a.hostConnectionId == b.hostConnectionId && a.role == b.role &&
        a.slot == b.slot && std::memcmp(a.location, b.location, sizeof(a.location)) == 0;
}
bool Witness(const Event& e) noexcept {
    const auto& a = e.before; const auto& b = e.after; const auto& h = a.hit;
    if (!e.ownerThread || !e.returned || e.unwound || e.nested || e.overflow || !e.coverageStable ||
        e.coverageMask != AllHooks || !e.contextStable || !e.metadataStable || !e.lossStable ||
        !e.callerAvailable || e.callerRva != 0x3D613C || e.takeCalls != 1 || e.statCalls != 1 || e.childCount != 2 ||
        a.context.role != 2 || (a.context.slot != 1 && a.context.slot != 2) || !a.context.loadSerial ||
        !a.context.generation || !a.context.epoch || !a.context.connectionId || !a.context.hostConnectionId ||
        (h.readMask & HitComplete) != HitComplete || (b.hit.readMask & HitComplete) != HitComplete ||
        !h.hit || h.hit != b.hit.hit || (h.flags & 2) || !(b.hit.flags & 2) || h.stat != 0 || h.damage <= 0 ||
        h.kind == 5 || h.kind == 6 || h.syncDrop || h.manualFilterOn || h.manualDrop ||
        b.hit.syncDrop || b.hit.manualFilterOn || b.hit.manualDrop ||
        !h.attackHandle || !h.ownerHandle || !h.attack || !h.owner || h.owner != a.source.actor ||
        h.attack != b.hit.attack || h.owner != b.hit.owner || h.attackHandle != b.hit.attackHandle ||
        h.atkpHandle != b.hit.atkpHandle || h.ownerHandle != b.hit.ownerHandle || h.attackId != b.hit.attackId ||
        h.kind != b.hit.kind || h.stat != b.hit.stat ||
        !SameActor(a.source, b.source) || a.source.type != 4 || a.source.namePrefix == 0x5F46 ||
        a.source.hp <= 0 || a.source.maxHp <= 0 ||
        a.victim.type != 0 || a.victim.maxHp <= 0 ||
        h.canonicalPlayer != a.victim.actor || h.head != a.victim.actor || h.tracked != a.victim.actor ||
        b.hit.canonicalPlayer != a.victim.actor || b.hit.head != a.victim.actor || b.hit.tracked != a.victim.actor) return false;
    const Child* take = nullptr; const Child* stat = nullptr;
    for (unsigned i = 0; i < e.childCount; ++i) {
        const auto& c = e.children[i];
        if (!c.returned || c.unwound || !c.matching || c.actor != a.victim.actor || c.stat != 0 || c.delta >= 0 ||
            !SameActor(a.victim, c.before) || !SameActor(a.victim, c.after)) return false;
        if (c.kind == ChildKind::Take) take = &c; else stat = &c;
    }
    if (!take || !stat || stat->takeSequence != take->sequence || take->delta != stat->delta ||
        take->before.hp != a.victim.hp || stat->before.hp != a.victim.hp || take->after.hp != b.victim.hp ||
        stat->after.hp != b.victim.hp || stat->result != b.victim.hp || a.victim.hp <= 0 || b.victim.hp < 0 ||
        b.victim.hp >= a.victim.hp) return false;
    const auto adjusted = static_cast<std::int64_t>(a.victim.hp) + stat->delta;
    return b.victim.hp == (adjusted < 0 ? 0 : adjusted);
}
void Publish(const Event& event) noexcept {
    if (!TryAcquireSRWLockExclusive(&g_lock)) { ++g_dropped; return; }
    if (g_size == QueueCapacity) ++g_dropped;
    else { g_queue[(g_read + g_size) % QueueCapacity] = event; ++g_size; ++g_published; }
    ReleaseSRWLockExclusive(&g_lock);
}
void BeginChild(ChildToken& token, ChildKind kind, uintptr_t actor, std::int32_t delta, std::int32_t stat,
    std::int32_t react, uintptr_t caller, bool callerAvailable, const ActorSnapshot& before) noexcept {
    token = {}; token.isTake = kind == ChildKind::Take;
    if (!CanCaptureChild()) return;
    token.active = true; token.parent = g_apply; token.previousTake = g_take;
    auto& e = g_apply->event;
    auto& count = token.isTake ? e.takeCalls : e.statCalls;
    if (count != (std::numeric_limits<std::uint32_t>::max)()) ++count; else e.overflow = true;
    if (token.isTake && g_take) { e.nested = true; ++g_nested; }
    if (e.childCount == ChildCapacity) { e.overflow = true; ++g_overflow; }
    else {
        token.index = e.childCount++;
        auto& c = e.children[token.index];
        c.sequence = ++g_sequence; c.kind = kind; c.actor = actor; c.delta = delta; c.stat = stat; c.react = react;
        c.callerRva = caller; c.callerAvailable = callerAvailable; c.before = before;
        c.matching = actor == e.before.victim.actor && before.actor == actor;
        if (!token.isTake) {
            c.matching = c.matching && g_take && g_take->parent == g_apply && g_take->index < ChildCapacity &&
                e.children[g_take->index].actor == actor;
            if (g_take && g_take->parent == g_apply && g_take->index < ChildCapacity) c.takeSequence = e.children[g_take->index].sequence;
        }
        if (!c.matching) ++g_unmatched;
    }
    if (token.isTake) g_take = &token;
}
void EndChild(ChildToken& token, bool normal, std::int32_t result, const ActorSnapshot* after) noexcept {
    if (!token.active) return;
    if (token.isTake) g_take = token.previousTake;
    if (token.parent && token.index < ChildCapacity) {
        auto& c = token.parent->event.children[token.index];
        c.returned = normal; c.unwound = !normal; c.result = normal ? result : 0;
        if (normal && after) c.after = *after;
    }
    if (!normal && token.parent) token.parent->event.unwound = true;
    token.active = false;
}
using U = unsigned long long;
void LogContext(LogFn log, std::uint64_t seq, unsigned phase, const Context& c, const char* family = "hittrace") {
    log("[%s] context schema=1 seq=%llu phase=%u readMask=%u available=%u frame=%llu generation=%llu epoch=%llu transitionSerial=%llu loadSerial=%llu connectionId=%llu hostConnectionId=%llu role=%u slot=%u location=%04X%04X%04X%04X%04X%04X", family,U(seq), phase,c.readMask,unsigned(c.available),U(c.frame),U(c.generation),U(c.epoch),U(c.transitionSerial),U(c.loadSerial),U(c.connectionId),U(c.hostConnectionId),unsigned(c.role),unsigned(c.slot),unsigned(c.location[0]),unsigned(c.location[1]),unsigned(c.location[2]),unsigned(c.location[3]),unsigned(c.location[4]),unsigned(c.location[5]));
}
void LogActor(LogFn log, std::uint64_t seq, unsigned phase, unsigned subject, const ActorSnapshot& a, const char* family = "hittrace") {
    log("[%s] actor schema=1 seq=%llu phase=%u subject=%u actor=%llX objentry=%llX status=%llX objectId=%u readMask=%u type=%u team=%u hp=%d maxHp=%d namePrefix=%04X",family,U(seq),phase,subject,U(a.actor),U(a.objentry),U(a.status),a.objectId,a.readMask,unsigned(a.type),unsigned(a.team),a.hp,a.maxHp,unsigned(a.namePrefix));
}
void LogInput(LogFn log, std::uint64_t seq, unsigned phase, const HitSnapshot& h, const char* family = "hittrace") {
    log("[%s] input schema=1 seq=%llu phase=%u hit=%llX attack=%llX owner=%llX canonicalPlayer=%llX head=%llX tracked=%llX flags=%u attackHandle=%u atkpHandle=%u ownerHandle=%u attackId=%u readMask=%u damage=%d stat=%u kind=%u syncDrop=%u manualFilterOn=%u manualDrop=%u",family,U(seq),phase,U(h.hit),U(h.attack),U(h.owner),U(h.canonicalPlayer),U(h.head),U(h.tracked),h.flags,h.attackHandle,h.atkpHandle,h.ownerHandle,h.attackId,h.readMask,h.damage,unsigned(h.stat),unsigned(h.kind),unsigned(h.syncDrop),unsigned(h.manualFilterOn),unsigned(h.manualDrop));
}
}
void Configure(bool requested, std::uint32_t verified, std::uint32_t installed) noexcept {
    auto previous = g_config.load();
    std::uint64_t next = 0;
    do { next = (((previous >> 8) + 1) << 8) | (requested ? 1ULL : 0ULL) |
        (static_cast<std::uint64_t>(verified & AllHooks) << 1) | (static_cast<std::uint64_t>(installed & AllHooks) << 4);
    } while (!g_config.compare_exchange_weak(previous, next));
}
void Shutdown() noexcept { const auto s = GetStats(); Configure(false, s.verifiedMask, s.installedMask); }
void RegisterOwnerThread() noexcept { DWORD expected = 0; g_owner.compare_exchange_strong(expected, GetCurrentThreadId()); }
bool IsOwnerThread() noexcept { return g_owner.load() == GetCurrentThreadId(); }
bool Requested() noexcept { return (g_config.load() & 1) != 0; }
bool CanCaptureApply() noexcept {
    if (!Requested()) return false;
    if (!IsOwnerThread()) { ++g_foreign; return false; }
    return true;
}
bool CanCaptureChild() noexcept {
    if (!Requested()) return false;
    if (!IsOwnerThread()) { ++g_foreign; return false; }
    if (!g_apply || !g_apply->active) { ++g_unmatched; return false; }
    return true;
}
void BeginApply(ApplyToken& token, uintptr_t caller, bool callerAvailable, const ApplyFacts& before) noexcept {
    token = {};
    const auto config = g_config.load();
    if (!(config & 1)) return;
    if (!IsOwnerThread()) { ++g_foreign; return; }
    token.previous = g_apply; token.previousTake = g_take; token.scoped = true; ++g_depth;
    if (g_apply) { g_apply->event.nested = true; ++g_nested; }
    g_apply = nullptr; g_take = nullptr; // Overflow scopes cannot leak children to their parent.
    if (g_depth > MaxDepth) { ++g_overflow; if (token.previous) token.previous->event.overflow = true; return; }
    token.active = true; g_apply = &token;
    auto& e = token.event;
    e.sequence = ++g_sequence; ++g_started;
    e.parentSequence = token.previous ? token.previous->event.sequence : 0;
    e.depth = g_depth; e.nested = token.previous != nullptr;
    e.coverageSerial = config >> 8; e.coverageMask = Coverage(config); e.lossSerial = g_dropped.load();
    e.ownerThread = true; e.callerRva = caller; e.callerAvailable = callerAvailable; e.before = before;
}
void EndApply(ApplyToken& token, bool normal, uintptr_t rawResult, const ApplyFacts* after) noexcept {
    if (!token.scoped) return;
    g_apply = token.previous; g_take = static_cast<ChildToken*>(token.previousTake); --g_depth; token.scoped = false;
    if (!token.active) return;
    auto& e = token.event; token.active = false;
    e.returned = normal; e.unwound = e.unwound || !normal; e.rawResult = normal ? rawResult : 0;
    if (normal && after) e.after = *after;
    const auto config = g_config.load();
    e.coverageStable = (config & 1) && e.coverageSerial == (config >> 8) && e.coverageMask == Coverage(config);
    e.lossStable = e.lossSerial == g_dropped.load();
    e.contextStable = normal && SameContext(e.before.context, e.after.context);
    e.metadataStable = normal && SameActor(e.before.victim, e.after.victim);
    if (e.unwound) ++g_unwound;
    e.witness = Witness(e); Publish(e);
}
void BeginTake(ChildToken& t, uintptr_t a, std::int32_t d, std::int32_t s, std::int32_t r, uintptr_t c, bool ca, const ActorSnapshot& b) noexcept { BeginChild(t,ChildKind::Take,a,d,s,r,c,ca,b); }
void EndTake(ChildToken& t, bool normal, const ActorSnapshot* a) noexcept { EndChild(t,normal,0,a); }
void BeginStat(ChildToken& t, uintptr_t a, std::int32_t d, std::int32_t s, std::int32_t r, uintptr_t c, bool ca, const ActorSnapshot& b) noexcept { BeginChild(t,ChildKind::Stat,a,d,s,r,c,ca,b); }
void EndStat(ChildToken& t, bool normal, std::int32_t result, const ActorSnapshot* a) noexcept { EndChild(t,normal,result,a); }
Stats GetStats() noexcept {
    Stats s {}; const auto config = g_config.load(); s.requested = (config & 1) != 0;
    s.verifiedMask = static_cast<std::uint32_t>((config >> 1) & AllHooks); s.installedMask = static_cast<std::uint32_t>((config >> 4) & AllHooks); s.coverageSerial = config >> 8;
    s.started=g_started.load();s.published=g_published.load();s.drained=g_drained.load();s.dropped=g_dropped.load();s.foreign=g_foreign.load();s.unwound=g_unwound.load();s.nested=g_nested.load();s.overflow=g_overflow.load();s.unmatched=g_unmatched.load();return s;
}
bool PopEvent(Event& e) noexcept {
    if (!TryAcquireSRWLockExclusive(&g_lock)) return false;
    const bool present = g_size != 0;
    if (present) { e = g_queue[g_read]; g_read = (g_read + 1) % QueueCapacity; --g_size; ++g_drained; }
    ReleaseSRWLockExclusive(&g_lock); return present;
}
void Drain(LogFn log) {
    if (!log) return;
    static std::uint64_t lastSerial = 0, lastActivity = 0;
    auto stats = GetStats();
    if (lastSerial != stats.coverageSerial) {
        log("[hittrace] ready schema=1 requested=%u verifiedMask=%u installedMask=%u coverageSerial=%llu",unsigned(stats.requested),stats.verifiedMask,stats.installedMask,U(stats.coverageSerial));
        lastSerial = stats.coverageSerial;
    }
    Event e {};
    for (unsigned n = 0; n < 16 && PopEvent(e); ++n) {
        log("[hittrace] event schema=1 seq=%llu parent=%llu depth=%u coverageSerial=%llu coverageMask=%u lossSerial=%llu callerRva=%llX callerAvailable=%u rawResult=%llX ownerThread=%u returned=%u unwound=%u nested=%u overflow=%u coverageStable=%u contextStable=%u metadataStable=%u lossStable=%u witness=%u takeCalls=%u statCalls=%u childCount=%u",U(e.sequence),U(e.parentSequence),e.depth,U(e.coverageSerial),e.coverageMask,U(e.lossSerial),U(e.callerRva),unsigned(e.callerAvailable),U(e.rawResult),unsigned(e.ownerThread),unsigned(e.returned),unsigned(e.unwound),unsigned(e.nested),unsigned(e.overflow),unsigned(e.coverageStable),unsigned(e.contextStable),unsigned(e.metadataStable),unsigned(e.lossStable),unsigned(e.witness),e.takeCalls,e.statCalls,e.childCount);
        for (unsigned phase = 0; phase < 2; ++phase) {
            const auto& f = phase ? e.after : e.before;
            LogContext(log,e.sequence,phase,f.context);LogActor(log,e.sequence,phase,0,f.victim);LogActor(log,e.sequence,phase,1,f.source);LogInput(log,e.sequence,phase,f.hit);
        }
        for (unsigned i = 0; i < e.childCount; ++i) {
            const auto& c = e.children[i];
            log("[hittrace] child schema=1 seq=%llu index=%u childSeq=%llu takeSeq=%llu kind=%u actor=%llX callerRva=%llX callerAvailable=%u delta=%d stat=%d react=%d result=%d matching=%u returned=%u unwound=%u",U(e.sequence),i,U(c.sequence),U(c.takeSequence),unsigned(c.kind),U(c.actor),U(c.callerRva),unsigned(c.callerAvailable),c.delta,c.stat,c.react,c.result,unsigned(c.matching),unsigned(c.returned),unsigned(c.unwound));
            LogActor(log,e.sequence,0,2+i,c.before);LogActor(log,e.sequence,1,2+i,c.after);
        }
        if (e.policy.recorded) {
            const auto& p = e.policy;
            const auto& f = p.facts;
            log("[damagepolicy] event schema=1 seq=%llu recorded=%u role=%u ownerThread=%u contextAvailable=%u victimClass=%u sourceClass=%u flags=%u readMask=%u amount=%d stat=%u kind=%u action=%u reason=%u supported=%u roster0=%llu roster1=%llu roster2=%llu revalidationAttempted=%u revalidationPassed=%u zeroAttempted=%u zeroResult=%u claimAttempted=%u claimQueued=%u",
                U(e.sequence),unsigned(p.recorded),unsigned(f.role),unsigned(f.ownerThread),unsigned(f.contextAvailable),
                unsigned(f.victim),unsigned(f.source),f.hit.flags,f.hit.readMask,f.hit.amount,unsigned(f.hit.stat),unsigned(f.hit.kind),
                unsigned(p.decision.action),unsigned(p.decision.reason),unsigned(p.decision.supported),
                U(p.roster[0]),U(p.roster[1]),U(p.roster[2]),unsigned(p.revalidationAttempted),unsigned(p.revalidationPassed),
                unsigned(p.zeroAttempted),unsigned(p.zeroResult),unsigned(p.claimAttempted),unsigned(p.claimQueued));
            LogContext(log,e.sequence,0,p.authority.context,"damagepolicy");
            LogInput(log,e.sequence,0,p.authority.hit,"damagepolicy");
            LogActor(log,e.sequence,0,0,p.authority.victim,"damagepolicy");
            LogActor(log,e.sequence,0,1,p.authority.source,"damagepolicy");
        }
    }
    stats = GetStats();
    const auto activity = stats.coverageSerial + stats.started + stats.published + stats.drained + stats.dropped +
        stats.foreign + stats.unwound + stats.nested + stats.overflow + stats.unmatched;
    if (activity != lastActivity) {
        log("[hittrace] summary schema=1 requested=%u verifiedMask=%u installedMask=%u coverageSerial=%llu started=%llu published=%llu drained=%llu dropped=%llu foreign=%llu unwound=%llu nested=%llu overflow=%llu unmatched=%llu",unsigned(stats.requested),stats.verifiedMask,stats.installedMask,U(stats.coverageSerial),U(stats.started),U(stats.published),U(stats.drained),U(stats.dropped),U(stats.foreign),U(stats.unwound),U(stats.nested),U(stats.overflow),U(stats.unmatched));
        lastActivity=activity;
    }
}
}
