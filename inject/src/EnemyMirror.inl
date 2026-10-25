// EnemyMirror.inl - VUH-1515 step 2: client-side driver for mirrored enemies.
// Included by EntityHook.cpp after the hit-trace readers and g_resolveHandle.
// Default off: everything returns at once unless KH2COOP_ENEMY_MIRROR=1
// (enemysync::MirrorRequested) and this machine is a client with a fresh
// host EnemyMotion stream for a bound, living, mirrored (not denied) enemy.
//
// Per updated actor, owner thread (HookedPerEntityUpdate):
//   PreUpdate   enemysync::MirrorPose -> Gate None/Bound/Drive for this frame;
//               hook its class's brain thunk (handler +0x20) on first use.
//   BrainDetour skip the brain (0x3B4460) for a driven actor; pass-through otherwise
//               (counted per actor while it is bound but not driven).
//   BlockMotion swallow the game's motion sets on a driven, living actor
//               (idle return, hit reactions); ours and death motions pass.
//   PostUpdate  set the host motion (guarded 0x3C86A0) and its time, write the
//               pose, zero velocity/carried/accel (ApplyPuppetTransform terms).
//               The first driven frame of a run and a same-id restart always
//               re-issue the set, so a local attack never carries over (review B1/S2).
//   NoteHit     one line per hit built by a tracked attacker (HookedBuildHit), with its damage.
// The motion tick, hurtbox upkeep (+0x28), physics and the global hit pass
// still run, so the host's attack hitbox hits this client's own Sora natively
// (DamagePolicy LocalVictim). Nothing is written to team, collision, HP or flags.
//
// VUH-1788 (KH2COOP_ENEMY_POPULATION=1, enemysync::PopulationRequested):
//   CullDetour  the class's removal predicate (handler +0x40, `mov rcx,rdx; jmp
//               0x3DAC30`: "AI script threads ended") runs as native; a mirrored
//               copy that is bound now, living and driven within kCullHoldFrames
//               is refused removal (the generic update 0x3BFD30 only calls the
//               removal slot +0x48 when +0x40 says so). A forced copy whose host
//               enemy is gone is removed through that same native slot.
//
// Log channels (each with its own limit, so one never starves another):
//   events  brain hook / refusal lines, kLogBudget total
//   runs    take-over / run end / retire lines with per-actor counters, kRunLogBudget
//   hits    one line per tracked attacker's hit, kHitLogBudget
//   stats   one global line + one per tracked actor every kStatsInterval frames (no budget)
//   trace   KH2COOP_ENEMY_MIRROR_TRACE=1 only: every tracked actor's observed position on host
//           frames % kTraceEvery, with driven=0/1 (so an undriven copy is data, not a gap)

namespace enemymirror {

constexpr uintptr_t RVA_BRAIN = 0x3B4460;           // shared enemy brain entry (spike d5/d6)
constexpr uintptr_t RVA_REMOVE_PREDICATE = 0x3DAC30; // removal predicate (VUH-1788 d_3DAC30.txt)
constexpr uintptr_t HANDLER_REMOVE_SLOT = 0x40;
constexpr uintptr_t ACTOR_MOTION_CTRL = 0x158;
constexpr uintptr_t MOTION_END = 0x40, MOTION_TIME = 0x44;
constexpr uintptr_t ENTITY_COS = 0x40, ENTITY_SIN = 0x48;
constexpr uintptr_t MIRROR_VELOCITY = 0xB98, MIRROR_CARRIED = 0x690, MIRROR_ACCEL = 0xA48;
constexpr size_t MIRROR_ACCEL_BYTES = 0x18;
constexpr uintptr_t MIRROR_ACTOR_STATUS = 0x5C0;
// D14 mirrors every censused class, bosses included: room for more hooked classes than the type-4 one.
constexpr unsigned kMaxDriven = 96, kMaxBrains = 16, kLogBudget = 48, kRunLogBudget = 512, kHitLogBudget = 512;
constexpr std::uint32_t kStatsInterval = 300;
constexpr std::uint32_t kNoMotion = 0xFFFFFFFFu;

using Brain = void(__fastcall*)(void* handler, void* actor);

struct Driven {
    uintptr_t actor = 0;
    std::uint16_t netId = 0;
    std::uint32_t frame = 0;       // last driven frame (DrivenNow: == g_frameCounter)
    std::uint32_t boundFrame = 0;  // last frame seen bound (Gate Bound or Drive)
    std::uint32_t firstBound = 0;  // first frame seen bound (spawn settle, S7)
    std::uint32_t runStart = 0;    // first frame of the current driven run
    bool running = false, forceSet = false;
    bool hostBrainOnly = false;
    std::uint32_t blend = 0;
    std::uint32_t lastSkipFrame = 0;  // BrainBody skipped the brain on this frame
    std::uint32_t seenFrame = 0;      // last PreUpdate with a slot (trace)
    double seenCursor = -1.0;         // stream cursor at that PreUpdate (trace)
    bool seenDriven = false;          // driven at that PreUpdate (trace)
    std::uint32_t noBrainRun = 0, missMotion = kNoMotion;
    std::uint32_t setMotion = kNoMotion, setFrame = 0;  // our last successful SetMotion
    Pose pose {};
    // Per actor, for this slot's lifetime (fixture: skips == drivenUpdates).
    std::uint64_t drivenUpdates = 0, skips = 0, passBound = 0, ourSets = 0, blocked = 0, runs = 0, gaps = 0,
                  restarts = 0, settleHolds = 0;
    // Driven updates whose brain was not called (hit-stop, reaction states, another path): rev-2 F3.
    std::uint64_t noBrain = 0, maxNoBrainRun = 0;
    std::uint64_t cullHolds = 0;  // VUH-1788: native removals refused for this copy
};
struct BrainHook { uintptr_t target = 0; Brain original = nullptr; };
using Predicate = bool(__fastcall*)(void* handler, void* actor);
struct CullHook { uintptr_t target = 0; Predicate original = nullptr; };
struct Stats {
    std::uint64_t driven = 0, skips = 0, passBound = 0, blocked = 0, ourSets = 0, setFaults = 0, writes = 0,
                  writeFaults = 0, takeovers = 0, gaps = 0, restarts = 0, settleHolds = 0, refusedClass = 0,
                  tableFull = 0, retired = 0, hits = 0, runLogDropped = 0, hitLogDropped = 0;
    std::uint64_t cullHolds = 0, cullPassed = 0, forceRemoves = 0, cullRefused = 0, cullLogDropped = 0, forceWaits = 0;
};

static Driven g_driven[kMaxDriven] {};
static BrainHook g_brains[kMaxBrains] {};
static unsigned g_brainCount = 0;
static std::uint32_t g_hostBrainBoundFrame = 0; // VUH-2640: a host-brain-only boss (Thorn) last bound to the host
std::uint32_t HostBrainBoundFrame() noexcept { return g_hostBrainBoundFrame; }
static uintptr_t g_refusedTargets[kMaxBrains] {};
static unsigned g_refusedCount = 0;
static bool g_guard = false;
static CullHook g_culls[kMaxBrains] {};
static unsigned g_cullCount = 0;
static uintptr_t g_cullRefusedTargets[kMaxBrains] {};
static unsigned g_cullRefusedCount = 0;
static unsigned g_cullLogBudget = 256;
static Stats g_mstats {};
static unsigned g_mlogBudget = kLogBudget, g_runLogBudget = kRunLogBudget, g_hitLogBudget = kHitLogBudget;
static std::uint32_t g_lastStatsFrame = 0;
static DWORD g_ownerThread = 0;

static Driven* Find(uintptr_t actor) noexcept {
    if (!actor) return nullptr;
    for (auto& d : g_driven) if (d.actor == actor) return &d;
    return nullptr;
}
static bool OwnerThread() noexcept { return GetCurrentThreadId() == g_ownerThread; }
// VUH-2617 trace: the actor being updated now was just bound (this frame) as the host-brain-only boss copy.
// Call only from that actor's own update, after PreUpdate; it reads no actor memory.
inline bool HostBrainBoundNow(uintptr_t actor) noexcept {
    if (!OwnerThread()) return false;
    const Driven* d = Find(actor);
    return d && d->hostBrainOnly && d->boundFrame == g_frameCounter;
}
// The thread check comes first: off the game thread the table is never read (rev-2 N-a).
static void Retire(Driven& d, const char* why);
static bool DrivenNow(uintptr_t actor) noexcept {
    if (!OwnerThread()) return false;
    Driven* d = Find(actor);
    if (!d || !d->running || d->frame!=g_frameCounter) return false;
    if (d->pose.field && !enemysync::FieldActorCurrent(actor)) {Retire(*d,"field-withdraw");return false;}
    if (!d->pose.field && !enemysync::RecordMirrorAuthorityCurrent(actor)) { Retire(*d,"record-withdraw");return false; }
    return true;
}

static bool FieldBrainDecision(uintptr_t actor);
bool EnterFieldCallback() noexcept;
void LeaveFieldCallback() noexcept;

static void BrainBodyEntered(void* handler, void* actor, Brain original) {
    Driven* d = OwnerThread() ? Find(reinterpret_cast<uintptr_t>(actor)) : nullptr;
    if(OwnerThread() && FieldBrainDecision(reinterpret_cast<uintptr_t>(actor)))return;
    if (d) {
        if (DrivenNow(reinterpret_cast<uintptr_t>(actor)) ||
            (d->hostBrainOnly && d->boundFrame==g_frameCounter &&
             enemysync::RecordMirrorAuthorityCurrent(reinterpret_cast<uintptr_t>(actor)))) {
            ++d->skips;
            ++g_mstats.skips;
            d->lastSkipFrame = g_frameCounter;
            return;
        }
        if (d->boundFrame == g_frameCounter) { ++d->passBound; ++g_mstats.passBound; }
    }
    original(handler, actor);
}
static void BrainBody(void* handler, void* actor, Brain original) {
    // Keep every field-state read admitted until the shared callback returns.
    if(!EnterFieldCallback()){original(handler,actor);return;}
    __try {BrainBodyEntered(handler,actor,original);}
    __finally {LeaveFieldCallback();}
}
// One detour per hooked thunk, each bound to its own trampoline slot.
template <unsigned I> static void __fastcall BrainDetour(void* handler, void* actor) {
    BrainBody(handler, actor, g_brains[I].original);
}
using DetourFn = void(__fastcall*)(void*, void*);
static constexpr DetourFn kDetours[kMaxBrains] = {&BrainDetour<0>, &BrainDetour<1>, &BrainDetour<2>, &BrainDetour<3>,
                                                  &BrainDetour<4>, &BrainDetour<5>, &BrainDetour<6>, &BrainDetour<7>,
                                                  &BrainDetour<8>, &BrainDetour<9>, &BrainDetour<10>, &BrainDetour<11>,
                                                  &BrainDetour<12>, &BrainDetour<13>, &BrainDetour<14>, &BrainDetour<15>};

// The actor's family (objentry object id), for per-family hook lines; 0 if unreadable.
static std::uint32_t FamilyOf(uintptr_t actor) noexcept {
    uintptr_t objentry = 0;
    std::uint32_t id = 0;
    return ReadHitTrace(actor + offsets::actor::OBJENTRY_PTR, objentry) && objentry &&
        ReadHitTrace(objentry + offsets::objentry::OBJECT_ID, id) ? id : 0;
}

static void NoteLog(const char* what, uintptr_t actor, uintptr_t a, uintptr_t b) {
    if (g_mlogBudget == 0) return;
    --g_mlogBudget;
    Log("[enemy-mirror] %s frame=%u actor=%llX a=%llX b=%llX objectId=%u", what, g_frameCounter,
        static_cast<unsigned long long>(actor), static_cast<unsigned long long>(a), static_cast<unsigned long long>(b),
        FamilyOf(actor));
}

// One per-actor counter line: run events (budgeted) and the stats channel.
static void ActorLine(const char* what, const Driven& d) {
    Log("[enemy-mirror] %s frame=%u netId=%u actor=%llX running=%d runStart=%u updates=%llu skips=%llu passBound=%llu "
        "sets=%llu blocked=%llu runs=%llu gaps=%llu restarts=%llu settle=%llu noBrain=%llu maxNoBrainRun=%llu "
        "missMotion=%d motion=%u cursor=%.0f objectId=%u",
        what, g_frameCounter, d.netId, static_cast<unsigned long long>(d.actor), d.running ? 1 : 0, d.runStart,
        d.drivenUpdates, d.skips, d.passBound, d.ourSets, d.blocked, d.runs, d.gaps, d.restarts, d.settleHolds,
        d.noBrain, d.maxNoBrainRun, d.missMotion == kNoMotion ? -1 : static_cast<int>(d.missMotion), d.pose.motionId,
        d.pose.cursor, d.pose.objectId);
}
static void RunLog(const char* what, const Driven& d) {
    if (g_runLogBudget == 0) { ++g_mstats.runLogDropped; return; }
    --g_runLogBudget;
    ActorLine(what, d);
}
static void EndRun(Driven& d, const char* why) {
    d.running = false;
    d.forceSet = false;
    RunLog(why, d);
}
static void Retire(Driven& d, const char* why) {
    if (d.running) d.running = false;
    ++g_mstats.retired;
    RunLog(why, d);
    d = {};
}

static void LogStats() {
    Log("[enemy-mirror] stats frame=%u driven=%llu skips=%llu passBound=%llu blocked=%llu sets=%llu setFaults=%llu "
        "writes=%llu writeFaults=%llu takeovers=%llu gaps=%llu restarts=%llu settle=%llu refusedClass=%llu brains=%u "
        "tableFull=%llu retired=%llu hits=%llu runLogDropped=%llu hitLogDropped=%llu",
        g_frameCounter, g_mstats.driven, g_mstats.skips, g_mstats.passBound, g_mstats.blocked, g_mstats.ourSets,
        g_mstats.setFaults, g_mstats.writes, g_mstats.writeFaults, g_mstats.takeovers, g_mstats.gaps, g_mstats.restarts,
        g_mstats.settleHolds, g_mstats.refusedClass, g_brainCount, g_mstats.tableFull, g_mstats.retired, g_mstats.hits,
        g_mstats.runLogDropped, g_mstats.hitLogDropped);
    if (enemysync::PopulationRequested())  // VUH-1788 counters, only while population is on (S4)
        Log("[enemy-pop] stats frame=%u cullHolds=%llu cullPassed=%llu forceRemoves=%llu forceWaits=%llu culls=%u",
            g_frameCounter, g_mstats.cullHolds, g_mstats.cullPassed, g_mstats.forceRemoves, g_mstats.forceWaits, g_cullCount);
    for (const auto& d : g_driven) if (d.actor) ActorLine("actor", d);
}
static void MaybeStats() {
    if (g_frameCounter - g_lastStatsFrame < kStatsInterval) return;
    g_lastStatsFrame = g_frameCounter;
    LogStats();
}

// The actor's class brain thunk is hooked (or already was). Shape-checked:
// `mov rcx,rdx; jmp 0x3B4460`, a frameless tail thunk, so MinHook's relocated
// copy needs no unwind data (spike review). Anything else stays on local AI.
static bool EnsureHandlerBrainHook(uintptr_t handler, uintptr_t actor) {
    uintptr_t vtable = 0, target = 0;
    std::uint8_t bytes[8] {};
    if (!handler || !ReadHitTrace(handler, vtable) || !ReadHitTrace(vtable + 0x20, target)) return false;
    for (unsigned i = 0; i < g_brainCount; ++i) if (g_brains[i].target == target) return true;
    for (unsigned i = 0; i < g_refusedCount; ++i) if (g_refusedTargets[i] == target) return false;
    const bool shape = target >= g_exeBase && target < g_exeBase + 0x3000000 &&
        ReadHitTraceMemory(target, bytes, sizeof(bytes)) && BrainThunk(bytes, target, g_exeBase + RVA_BRAIN);
    if (!shape || g_brainCount >= kMaxBrains) {
        if (g_refusedCount < kMaxBrains) g_refusedTargets[g_refusedCount++] = target;
        ++g_mstats.refusedClass;
        NoteLog(shape ? "brain-refused-full" : "brain-refused-shape", actor, target - g_exeBase, vtable - g_exeBase);
        return false;
    }
    const unsigned slot = g_brainCount;
    MH_STATUS st = MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(kDetours[slot]),
                                 reinterpret_cast<void**>(&g_brains[slot].original));
    if (st == MH_OK) st = MH_EnableHook(reinterpret_cast<void*>(target));
    if (st != MH_OK) {
        if (g_refusedCount < kMaxBrains) g_refusedTargets[g_refusedCount++] = target;
        ++g_mstats.refusedClass;
        NoteLog("brain-hook-failed", actor, target - g_exeBase, static_cast<uintptr_t>(st));
        return false;
    }
    g_brains[slot].target = target;
    g_brainCount = slot + 1;  // published after the original is set: the detour only runs once enabled
    NoteLog("brain-hook", actor, target - g_exeBase, vtable - g_exeBase);
    return true;
}
static bool EnsureBrainHook(uintptr_t actor) {
    std::uint32_t handle = 0;
    uintptr_t handler = 0;
    if (!g_resolveHandle || !ReadHitTrace(actor, handle) || handle == 0) return false;
    __try { handler = g_resolveHandle(handle); } __except (EXCEPTION_EXECUTE_HANDLER) { handler = 0; }
    return EnsureHandlerBrainHook(handler, actor);
}

// VUH-2605: the brain hook a script-born child depends on (option C). actor 0, before the factory call: the
// type-4 mob class handler every 317 child is built with (0x419E30 sets object 0x7528E8, vtable 0x5D2D68).
// Otherwise the returned body's own handler, checked before its first update.
static constexpr uintptr_t kMobHandlerRva = 0x7528E8, kMobVtableRva = 0x5D2D68;
bool ScriptChildBrainReady(uintptr_t actor) noexcept {
    if (actor) return EnsureBrainHook(actor);
    uintptr_t vtable = 0;
    return ReadHitTrace(g_exeBase + kMobHandlerRva, vtable) && vtable == g_exeBase + kMobVtableRva &&
        EnsureHandlerBrainHook(g_exeBase + kMobHandlerRva, 0);
}

#include "FieldBrain.inl"

static bool LivingNow(uintptr_t actor) noexcept;

static void CullLog(const char* what, uintptr_t actor, std::uint16_t netId, std::uint32_t since) {
    if (g_cullLogBudget == 0) { ++g_mstats.cullLogDropped; return; }
    --g_cullLogBudget;
    Log("[enemy-pop] %s frame=%u actor=%llX netId=%u sinceDriven=%u holds=%llu passed=%llu forced=%llu", what,
        g_frameCounter, static_cast<unsigned long long>(actor), netId, since, g_mstats.cullHolds, g_mstats.cullPassed,
        g_mstats.forceRemoves);
}

// S2: per-actor bind history and per-netId drive history, kept across a Driven
// slot's retirement, so a one-frame None gate (e.g. a binding holdAll) cannot wipe a hold.
struct RecentBind { uintptr_t actor = 0; std::uint16_t netId = 0; std::uint32_t frame = 0; };
struct NetDrive { std::uint16_t netId = 0; std::uint32_t frame = 0; };
static RecentBind g_recent[64] {};
static NetDrive g_netDrive[64] {};
static std::uint32_t g_historyGeneration = 0;
// rev3 S2: netIds are reused across epochs and rooms; a planner Rebase/Clear drops the history.
static void SyncHistory() noexcept {
    const std::uint32_t generation = enemysync::PopulationGeneration();
    if (generation == g_historyGeneration) return;
    g_historyGeneration = generation;
    for (auto& r : g_recent) r = {};
    for (auto& n : g_netDrive) n = {};
}
template <class T, class Match> static T* Slot(T (&table)[64], Match match) noexcept {
    for (auto& e : table) if (match(e)) return &e;
    T* oldest = &table[0];
    for (auto& e : table) {
        if (e.frame == 0) return &e;  // free
        if (g_frameCounter - e.frame > g_frameCounter - oldest->frame) oldest = &e;
    }
    return oldest;
}
static void NoteBound(uintptr_t actor, std::uint16_t netId) noexcept {
    RecentBind* r = Slot(g_recent, [&](const RecentBind& e) { return e.actor == actor && e.frame; });
    if (r->actor != actor) *r = {actor, netId, 0};
    if (netId) r->netId = netId;
    r->frame = g_frameCounter ? g_frameCounter : 1;
}
static const RecentBind* FindRecent(uintptr_t actor) noexcept {
    for (const auto& r : g_recent) if (r.frame && r.actor == actor) return &r;
    return nullptr;
}
static void NoteDriven(std::uint16_t netId) noexcept {
    if (!netId) return;
    NetDrive* n = Slot(g_netDrive, [&](const NetDrive& e) { return e.netId == netId && e.frame; });
    n->netId = netId;
    n->frame = g_frameCounter ? g_frameCounter : 1;
}
static std::uint32_t SinceDriven(std::uint16_t netId) noexcept {
    for (const auto& n : g_netDrive) if (netId && n.frame && n.netId == netId) return g_frameCounter - n.frame;
    return 0xFFFFFFFFu;
}
// C4: a forced removal overrides only the script-thread term of 0x3DAC30. The
// native +0x80/+0x98 term, the +0xBB4 child-busy check (child +0x14) and, VUH-2614,
// the reference count +0x5B8 still apply (enemypop::ForcedRemovalTermsAllow).
static bool NativeRemovalTermsAllow(uintptr_t actor) noexcept {
    uintptr_t p80 = 0, p98 = 0;
    std::uint32_t childHandle = 0, busy = 0;
    std::int32_t references = 0;
    if (!ReadHitTrace(actor + 0x80, p80) || !ReadHitTrace(actor + 0x98, p98) ||
        !ReadHitTrace(actor + 0x5B8, references) || !ReadHitTrace(actor + 0xBB4, childHandle)) return false;
    if (childHandle && g_resolveHandle) {
        uintptr_t child = 0;
        __try { child = g_resolveHandle(childHandle); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        if (child && !ReadHitTrace(child + 0x14, busy)) return false;
    }
    return enemypop::ForcedRemovalTermsAllow(p80 != 0, p98 != 0, busy != 0, references);
}

// VUH-2304: +0x80 is the actor effect table; +0x98 is its list head.
// Fade completion (slot +0x38 -> 0x3B4700) stops scripts/motion and sets bit28,
// but leaves those effects alive. Native slot +0x30 (0x3DABE0) drains them with
// 0x2C6490(actor+0x80). Use that same leaf, never zero the list or bypass C4.
// Full Steam Global leaf body, independently decompiled with trap 0x42D690
// (obj_effect_kill_all), which supplies precisely this actor-domain argument.
static constexpr std::uint8_t kEndActorEffects[] = {
    0x48,0x89,0x5C,0x24,0x18,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x59,0x18,0x48,0x8B,
    0xF1,0x48,0x85,0xDB,0x0F,0x84,0xEA,0x00,0x00,0x00,0x48,0x89,0x6C,0x24,0x30,0x48,
    0x89,0x7C,0x24,0x38,0x8B,0x4B,0x74,0xE8,0xB4,0x6D,0x1E,0x00,0x48,0x8B,0x7E,0x18,
    0x48,0x8B,0xE8,0x48,0x3B,0xDF,0x75,0x10,0x8B,0x4B,0x74,0xE8,0xA0,0x6D,0x1E,0x00,
    0x48,0x89,0x46,0x18,0x33,0xFF,0xEB,0x38,0x8B,0x4F,0x74,0xE8,0x90,0x6D,0x1E,0x00,
    0x48,0x3B,0xC3,0x74,0x18,0x8B,0x4F,0x74,0xE8,0x83,0x6D,0x1E,0x00,0x48,0x8B,0xF8,
    0x8B,0x48,0x74,0xE8,0x78,0x6D,0x1E,0x00,0x48,0x3B,0xC3,0x75,0xE8,0x8B,0x4B,0x74,
    0xE8,0x6B,0x6D,0x1E,0x00,0x48,0x8B,0xC8,0xE8,0x33,0x6D,0x1E,0x00,0x89,0x47,0x74,
    0x48,0x3B,0x5E,0x20,0x75,0x04,0x48,0x89,0x7E,0x20,0x33,0xC9,0xE8,0x1F,0x6D,0x1E,
    0x00,0x48,0x8B,0x4B,0x40,0x89,0x43,0x74,0xE8,0x73,0x83,0xFF,0xFF,0x8B,0x4B,0x60,
    0x85,0xC9,0x78,0x0A,0xBA,0x78,0x00,0x00,0x00,0xE8,0xC2,0x7F,0xF1,0xFF,0x8B,0x4B,
    0x70,0x8B,0xC1,0xC1,0xE8,0x02,0xA8,0x01,0x74,0x0B,0x48,0x8B,0x53,0x68,0x48,0x85,
    0xD2,0x74,0x1B,0xEB,0x0C,0xC1,0xE9,0x03,0xF6,0xC1,0x01,0x74,0x11,0x48,0x8B,0x53,
    0x68,0x48,0x8B,0x0D,0xB8,0xD0,0x89,0x00,0x48,0x8B,0x01,0xFF,0x50,0x10,0x48,0x8B,
    0x0D,0xAB,0xD0,0x89,0x00,0x48,0x8B,0xD3,0x48,0x8B,0x01,0xFF,0x50,0x10,0x48,0x8B,
    0xDD,0x48,0x85,0xED,0x0F,0x85,0x2A,0xFF,0xFF,0xFF,0x48,0x8B,0x7C,0x24,0x38,0x48,
    0x8B,0x6C,0x24,0x30,0x48,0x8B,0x5C,0x24,0x40,0x48,0x83,0xC4,0x20,0x5E,0xC3
};
static bool g_removalEffectsFaulted = false; // DLL lifetime: never retry an ambiguous native call
static bool EndLeftCopyEffects(uintptr_t actor) noexcept {
    __try {
        uintptr_t effects = 0, list = 0;
        if (g_removalEffectsFaulted || !ReadHitTrace(actor + 0x80, effects) ||
            !ReadHitTrace(actor + 0x98, list)) return false;
        if (!effects || !list) return true;
        if (std::memcmp(reinterpret_cast<const void*>(g_exeBase + 0x2C6490),
                        kEndActorEffects, sizeof(kEndActorEffects)) != 0 ||
            !enemysync::PopulationLeftCleanupRequested(actor)) return false;
        using EndEffects = void(__fastcall*)(void*);
        reinterpret_cast<EndEffects>(g_exeBase + 0x2C6490)(reinterpret_cast<void*>(actor + 0x80));
        uintptr_t after = 0;
        const bool read = ReadHitTrace(actor + 0x98, after);
        Log("[enemy-pop] leave-effects-ended frame=%u actor=%llX before=%llX after=%llX read=%u",
            g_frameCounter, static_cast<unsigned long long>(actor), static_cast<unsigned long long>(list),
            static_cast<unsigned long long>(after), read ? 1u : 0u);
        return read && after == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_removalEffectsFaulted = true;
        Log("[enemy-pop] leave-effects-fault actor=%llX; forced removal disabled", static_cast<unsigned long long>(actor));
        return false;
    }
}

// Native first (the predicate also steps the actor's AI script), then the override.
static bool CullBody(void* handler, void* actor, Predicate original) {
    const bool native = original(handler, actor);
    if (!OwnerThread()) return native;
    const auto a = reinterpret_cast<uintptr_t>(actor);
    if (!enemysync::RecordMirrorAuthorityCurrent(a)) {
        if (auto* stale=Find(a)) Retire(*stale,"record-withdraw");
        return native;
    }
    SyncHistory();
    const Driven* d = Find(a);
    const RecentBind* r = FindRecent(a);
    const std::uint16_t netId = d && d->netId ? d->netId : (r ? r->netId : 0);
    const bool recent = r && g_frameCounter - r->frame <= enemypop::kBindGap;
    const bool force = enemysync::PopulationForceRemove(a);
    const bool forcedHold = !force && enemysync::PopulationForcedHold(a);
    const std::uint32_t since = SinceDriven(netId);
    const bool living = (native || force) ? LivingNow(a) : false;
    const int decision = enemypop::CullDecision(native, force, recent, living, d && d->running, since, forcedHold);
    if (decision == 1) {
        if (g_removalEffectsFaulted) return native;
        if (enemysync::PopulationLeftCleanupRequested(a) && !EndLeftCopyEffects(a)) return native;
        if (!NativeRemovalTermsAllow(a)) {  // retried on its next update (C4); counted, logged sparsely
            if (g_mstats.forceWaits++ % 300 == 0) {
                CullLog("force-remove-wait", a, netId, since);
                uintptr_t effects = 0, effectList = 0, child = 0;
                std::uint32_t childHandle = 0, busy = 0;
                std::int32_t references = 0;  // +0x5B8: the actor's reference count (VUH-2614), not script threads
                const bool read = ReadHitTrace(a + 0x80, effects) && ReadHitTrace(a + 0x98, effectList) &&
                    ReadHitTrace(a + 0xBB4, childHandle) && ReadHitTrace(a + 0x5B8, references);
                __try { if (childHandle && g_resolveHandle) child = g_resolveHandle(childHandle); }
                __except (EXCEPTION_EXECUTE_HANDLER) { child = 0; }
                const bool childRead = !child || ReadHitTrace(child + 0x14, busy);
                Log("[enemy-pop] removal-terms actor=%llX read=%u effects=%llX list=%llX child=%X childRead=%u busy=%u refs=%d",
                    static_cast<unsigned long long>(a), read ? 1u : 0u, static_cast<unsigned long long>(effects),
                    static_cast<unsigned long long>(effectList), childHandle, childRead ? 1u : 0u, busy, references);
            }
            return native;
        }
        if (!enemysync::PopulationForceRemove(a)) return native; // final identity/scope guard after child reads
        ++g_mstats.forceRemoves;
        CullLog("force-remove", a, netId, since);
        enemysync::PopulationForget(a);  // rev3 C1: the +0x48 dispose always follows a true return
        return true;
    }
    if (decision == 0) {
        if (d) { if (const_cast<Driven*>(d)->cullHolds++ == 0) CullLog("cull-hold", a, netId, since); }
        else if (g_mstats.cullHolds == 0 || forcedHold) CullLog(forcedHold ? "cull-hold-forced" : "cull-hold", a, netId, since);
        ++g_mstats.cullHolds;
        return false;
    }
    if (native && (d || r)) {
        ++g_mstats.cullPassed;
        CullLog("cull-pass", a, netId, since);
    }
    if (native) enemysync::PopulationForget(a);  // rev4 C1: any true return is followed by the +0x48 dispose
    return native;
}
template <unsigned I> static bool __fastcall CullDetour(void* handler, void* actor) {
    return CullBody(handler, actor, g_culls[I].original);
}
using CullFn = bool(__fastcall*)(void*, void*);
static constexpr CullFn kCullDetours[kMaxBrains] = {&CullDetour<0>, &CullDetour<1>, &CullDetour<2>, &CullDetour<3>,
                                                    &CullDetour<4>, &CullDetour<5>, &CullDetour<6>, &CullDetour<7>,
                                                    &CullDetour<8>, &CullDetour<9>, &CullDetour<10>, &CullDetour<11>,
                                                    &CullDetour<12>, &CullDetour<13>, &CullDetour<14>, &CullDetour<15>};

// Same discovery and refusal rules as the brain hook, on handler slot +0x40.
static bool EnsureCullHook(uintptr_t actor) {
    std::uint32_t handle = 0;
    uintptr_t handler = 0, vtable = 0, target = 0;
    std::uint8_t bytes[8] {};
    if (!g_resolveHandle || !ReadHitTrace(actor, handle) || handle == 0) return false;
    __try { handler = g_resolveHandle(handle); } __except (EXCEPTION_EXECUTE_HANDLER) { handler = 0; }
    if (!handler || !ReadHitTrace(handler, vtable) || !ReadHitTrace(vtable + HANDLER_REMOVE_SLOT, target)) return false;
    for (unsigned i = 0; i < g_cullCount; ++i) if (g_culls[i].target == target) return true;
    for (unsigned i = 0; i < g_cullRefusedCount; ++i) if (g_cullRefusedTargets[i] == target) return false;
    const bool shape = target >= g_exeBase && target < g_exeBase + 0x3000000 &&
        ReadHitTraceMemory(target, bytes, sizeof(bytes)) && BrainThunk(bytes, target, g_exeBase + RVA_REMOVE_PREDICATE);
    if (!shape || g_cullCount >= kMaxBrains) {
        if (g_cullRefusedCount < kMaxBrains) g_cullRefusedTargets[g_cullRefusedCount++] = target;
        ++g_mstats.cullRefused;
        NoteLog(shape ? "cull-refused-full" : "cull-refused-shape", actor, target - g_exeBase, vtable - g_exeBase);
        return false;
    }
    const unsigned slot = g_cullCount;
    MH_STATUS st = MH_CreateHook(reinterpret_cast<void*>(target), reinterpret_cast<void*>(kCullDetours[slot]),
                                 reinterpret_cast<void**>(&g_culls[slot].original));
    if (st == MH_OK) st = MH_EnableHook(reinterpret_cast<void*>(target));
    if (st != MH_OK) {
        if (g_cullRefusedCount < kMaxBrains) g_cullRefusedTargets[g_cullRefusedCount++] = target;
        ++g_mstats.cullRefused;
        NoteLog("cull-hook-failed", actor, target - g_exeBase, static_cast<uintptr_t>(st));
        return false;
    }
    g_culls[slot].target = target;
    g_cullCount = slot + 1;
    NoteLog("cull-hook", actor, target - g_exeBase, vtable - g_exeBase);
    return true;
}

static bool LivingNow(uintptr_t actor) noexcept {
    if(const auto* d=Find(actor);d && d->pose.field)return enemysync::FieldActorCurrent(actor);
    uintptr_t status = 0;
    std::int32_t hp = 0;
    return ReadHitTrace(actor + MIRROR_ACTOR_STATUS, status) && status && ReadHitTrace(status, hp) && hp > 0;
}

static Driven* Allocate(uintptr_t actor) {
    for (auto& slot : g_driven) {
        if (slot.actor == 0) { slot = {}; slot.actor = actor; return &slot; }
    }
    for (auto& slot : g_driven) {  // an actor no longer updated at all (despawned without a None gate)
        const std::uint32_t last = slot.boundFrame > slot.frame ? slot.boundFrame : slot.frame;
        if (g_frameCounter - last > 2 * kStaleFrames) { Retire(slot, "retire-stale"); slot.actor = actor; return &slot; }
    }
    return nullptr;
}

// Trace (fixture only), for every tracked actor, driven or not: its position as
// the previous frame left it, labelled with the host frame the stream cursor
// stood at then, and whether it was driven then. Host side logs the same frames.
static void TraceObserved(uintptr_t actor, const Driven& d) {
    const double c = d.seenCursor;
    if (c < 0.0 || (!enemysync::MirrorLatencyTrace() && std::fmod(c, static_cast<double>(kTraceEvery)) != 0.0)) return;
    Vec3 at {};
    std::uint32_t motion = kNoMotion;
    const uintptr_t e = actor + offsets::actor::ENTITY_TRANSFORM;
    if (!ReadHitTrace(e + offsets::entity::POS_X, at.x) || !ReadHitTrace(e + offsets::entity::POS_Y, at.y) ||
        !ReadHitTrace(e + offsets::entity::POS_Z, at.z)) return;
    (void)ReadHitTrace(actor + offsets::actor::ANIM_ID, motion);
    Log("[enemy-mirror] trace-client netId=%u hostFrame=%.0f frame=%u driven=%d blend=%u pos=%.1f,%.1f,%.1f motion=%u",
        d.netId, c, g_frameCounter, d.seenDriven ? 1 : 0, d.blend, at.x, at.y, at.z, motion);
}
static void NoteSeen(uintptr_t actor, Driven& d, bool driven) {
    if (enemysync::MirrorTrace() && d.seenFrame && d.seenFrame + 1 == g_frameCounter) TraceObserved(actor, d);
    d.seenFrame = g_frameCounter;
    d.seenCursor = enemysync::MirrorCursor();
    d.seenDriven = driven;
}

void PreUpdateEntered(uintptr_t actor) {
    if (!enemysync::MirrorRequested()) return;
    if (g_ownerThread == 0) g_ownerThread = GetCurrentThreadId();
    if (!OwnerThread()) return;
    PrepareFieldActor(actor);
    // VUH-2605: a script-born child's class brain must be hooked before its first brain call, which follows
    // this pre-update in the same native update (its initializer runs natively first). Without the hook the
    // child is held: the entity hook skips its whole native update, so neither its initializer nor its brain runs.
    if (enemysync::ScriptChildOwned(actor) && !enemysync::ScriptChildHeld(actor) && !EnsureBrainHook(actor)) {
        NoteLog("script-child-brain-refused-hold", actor, 0, 0);
        enemysync::HoldScriptChild(actor);
    }
    if (enemysync::ScriptChildHeld(actor)) return;
    // Certified unbound stale copies are consumed at the owner-frame boundary, independently of native bit28.
    Pose pose {};
    const Gate gate = enemysync::MirrorPose(actor, pose);
    Driven* d = Find(actor);
    if (gate == Gate::None) {
        if (d) Retire(*d, "retire");
        return;
    }
    if (!d) d = Allocate(actor);
    if (!d) { ++g_mstats.tableFull; return; }
    d->pose.field=pose.field;
    d->hostBrainOnly=pose.objectId==enemyclass::TwilightThorn;
    if (d->hostBrainOnly) g_hostBrainBoundFrame = g_frameCounter ? g_frameCounter : 1;
    if (!d->firstBound) d->firstBound = g_frameCounter ? g_frameCounter : 1;
    d->boundFrame = g_frameCounter;
    if (enemysync::PopulationRequested() && !pose.field) { SyncHistory(); NoteBound(actor, pose.netId); }
    if (pose.netId) d->netId = pose.netId;
    bool drive = gate == Gate::Drive;
    if(d->hostBrainOnly && !EnsureBrainHook(actor)) {
        NoteLog("owned-brain-refused",actor,pose.netId,pose.objectId);
        return;
    }
    if (drive && g_frameCounter - d->firstBound < kSpawnSettleFrames) {  // let the local spawn-in finish (S7)
        drive = false;
        ++d->settleHolds;
        ++g_mstats.settleHolds;
    }
    if (enemysync::PopulationRequested() && !pose.field) (void)EnsureCullHook(actor);  // bound copies, driven or not
    if (drive && !(pose.field && EnsureFieldPuzzleHook(actor)) && !EnsureBrainHook(actor)) drive = false;
    NoteSeen(actor, *d, drive);
    if (!drive) {
        if (d->running) EndRun(*d, "release");
        return;
    }
    const std::uint32_t since = g_frameCounter - d->frame;
    if (!d->running || since > kGapTolerance) {  // a take-over (S6: a short gap is not one)
        if (d->running) EndRun(*d, "gap-release");
        d->running = true;
        d->runStart = g_frameCounter;
        d->noBrainRun = 0;
        d->blend = kBlendFrames;
        d->forceSet = true;  // never let a local attack carry over (B1)
        ++d->runs;
        ++g_mstats.takeovers;
        d->pose = pose;
        RunLog("take-over", *d);
    } else {
        if (since > 1) { ++d->gaps; ++g_mstats.gaps; }
        // Same motion restarted on the host (back-to-back attack): re-issue the set (S2).
        if (pose.motionId == d->pose.motionId && pose.motionTime + kRestartBackFrames < d->pose.motionTime) {
            d->forceSet = true;
            ++d->restarts;
            ++g_mstats.restarts;
        }
    }
    d->frame = g_frameCounter;
    if (enemysync::PopulationRequested()) NoteDriven(d->netId);
    d->pose = pose;
    ++d->drivenUpdates;
    ++g_mstats.driven;
}

// HookedMotionChainSetAnim entry (0x3C88C0): true = swallow the game's set.
bool BlockMotionEntered(uintptr_t actor) noexcept {
    if (!enemysync::MirrorRequested() || g_guard || !DrivenNow(actor)) return false;  // DrivenNow: thread first
    if (!LivingNow(actor)) return false;  // a death sequence always stays native
    ++Find(actor)->blocked;
    ++g_mstats.blocked;
    return true;
}

static bool SetMotion(uintptr_t actor, std::uint32_t motion) noexcept {
    if (!g_setAnimationUnderlying || !DrivenNow(actor)) return false;
    bool ok = false;
    g_guard = true;
    __try {
        __try {
            g_setAnimationUnderlying(reinterpret_cast<void*>(actor + ACTOR_MOTION_CTRL), static_cast<int>(motion), 0.0f, 0.0f);
            ok = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    } __finally { g_guard = false; }
    if (ok) ++g_mstats.ourSets; else ++g_mstats.setFaults;
    return ok;
}

static bool WritePose(uintptr_t actor, Driven& d) noexcept {
    if (!DrivenNow(actor)) return false;
    __try {
        const uintptr_t e = actor + offsets::actor::ENTITY_TRANSFORM;
        auto* pos = reinterpret_cast<float*>(e + offsets::entity::POS_X);
        Vec3 at = d.pose.position;
        if (d.blend) {
            const float w = BlendWeight(d.blend);
            at = {pos[0] + (at.x - pos[0]) * w, pos[1] + (at.y - pos[1]) * w, pos[2] + (at.z - pos[2]) * w};
            --d.blend;
        }
        pos[0] = at.x;
        pos[1] = at.y;
        pos[2] = at.z;
        *reinterpret_cast<float*>(e + ENTITY_COS) = std::cos(d.pose.rotationY);
        *reinterpret_cast<float*>(e + ENTITY_SIN) = std::sin(d.pose.rotationY);
        *reinterpret_cast<float*>(e + offsets::entity::ROT_Y) = d.pose.rotationY;
        std::memset(reinterpret_cast<void*>(actor + MIRROR_VELOCITY), 0, 3 * sizeof(float));
        std::memset(reinterpret_cast<void*>(actor + MIRROR_CARRIED), 0, 3 * sizeof(float));
        std::memset(reinterpret_cast<void*>(actor + MIRROR_ACCEL), 0, MIRROR_ACCEL_BYTES);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void PostUpdateEntered(uintptr_t actor) {
    if (!enemysync::MirrorRequested() || !DrivenNow(actor)) return;
    MaybeStats();
    Driven* d = Find(actor);
    if (!d) return;
    std::uint32_t current = 0;
    const bool haveCurrent = ReadHitTrace(actor + offsets::actor::ANIM_ID, current);
    if (d->lastSkipFrame != g_frameCounter) {  // driven, but this update never reached the brain
        ++d->noBrain;
        if (++d->noBrainRun > d->maxNoBrainRun) d->maxNoBrainRun = d->noBrainRun;
        d->missMotion = haveCurrent ? current : kNoMotion;
    } else {
        d->noBrainRun = 0;
    }
    if (!LivingNow(actor)) return;  // died during its own update: leave it native
    float time = 0.0f, end = 0.0f;
    const uintptr_t motCtrl = actor + ACTOR_MOTION_CTRL;
    if (d->pose.field && d->pose.motionId==kNoMotion) { (void)WritePose(actor,*d);return; }
    if (d->forceSet || (haveCurrent && current != d->pose.motionId)) {
        if (!SetMotion(actor, d->pose.motionId) || !DrivenNow(actor)) return; // native motion setter can reenter lifecycle hooks
        d->forceSet = false;
        d->setMotion = d->pose.motionId;
        d->setFrame = g_frameCounter;
        ++d->ourSets;
    }
    if (ReadHitTrace(motCtrl + MOTION_TIME, time) && ReadHitTrace(motCtrl + MOTION_END, end)) {
        const float target = ClampMotionTime(d->pose.motionTime, end);
        if (std::fabs(time - target) > PUPPET_TIME_DRIFT_FRAMES) {
            if (!DrivenNow(actor)) return;
            __try { *reinterpret_cast<float*>(motCtrl + MOTION_TIME) = target; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    if (WritePose(actor, *d)) ++g_mstats.writes; else ++g_mstats.writeFaults;
}

// HookedBuildHit: is this attacker one of ours (bound or driven)?
bool Tracked(uintptr_t attacker) noexcept {
    return enemysync::MirrorRequested() && OwnerThread() && Find(attacker) != nullptr;
}

// One line per hit a tracked attacker builds, with the hit record's damage and
// stat (own budget: never limited by the [hit] lines). A fixture PASS hit needs
// driven=1 and an attack motion our set entered mid-run (runStart < setFrame <=
// frame, setMotion == motion), i.e. the host's motion, not a carried local one.
void NoteHit(uintptr_t attacker, uintptr_t victim, std::uint32_t atkpId, const char* victimName, std::int32_t damage,
             std::uint32_t stat) noexcept {
    const Driven* d = Find(attacker);
    if (!d) return;
    ++g_mstats.hits;
    if (g_hitLogBudget == 0) { ++g_mstats.hitLogDropped; return; }
    --g_hitLogBudget;
    std::uint32_t motion = kNoMotion;
    float time = -1.0f;
    (void)ReadHitTrace(attacker + offsets::actor::ANIM_ID, motion);
    (void)ReadHitTrace(attacker + ACTOR_MOTION_CTRL + MOTION_TIME, time);
    const bool driven = DrivenNow(attacker);
    Log("[enemy-mirror] hit frame=%u netId=%u actor=%llX driven=%d runStart=%u runFrames=%u motion=%u time=%.1f "
        "setMotion=%d setFrame=%u cursor=%.0f atkp=%u victim=%s@%llX damage=%d stat=%u updates=%llu skips=%llu",
        g_frameCounter, d->netId, static_cast<unsigned long long>(attacker), driven ? 1 : 0, d->runStart,
        driven ? g_frameCounter - d->runStart + 1 : 0u, motion, time,
        d->setMotion == kNoMotion ? -1 : static_cast<int>(d->setMotion), d->setFrame, d->pose.cursor, atkpId,
        victimName ? victimName : "?", static_cast<unsigned long long>(victim), damage, stat, d->drivenUpdates, d->skips);
}

void PreUpdate(uintptr_t actor) {
    if(!EnterFieldCallback())return;
    __try {PreUpdateEntered(actor);}
    __finally {LeaveFieldCallback();}
}

void PostUpdate(uintptr_t actor) {
    if(!EnterFieldCallback())return;
    __try {PostUpdateEntered(actor);}
    __finally {LeaveFieldCallback();}
}

bool BlockMotion(uintptr_t actor) noexcept {
    if(!EnterFieldCallback())return false;
    __try {return BlockMotionEntered(actor);}
    __finally {LeaveFieldCallback();}
}

}  // namespace enemymirror
