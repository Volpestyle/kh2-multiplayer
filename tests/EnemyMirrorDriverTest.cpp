// VUH-1515 step 2: the native client driver (inject/src/EnemyMirror.inl) over
// fake game memory. Headless: MinHook, the motion setter, the handle resolver
// and enemysync::MirrorPose are stand-ins; no game process, no real hooks.
#include <Windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "kh2coop/KH2Offsets.hpp"
#include "EnemyMirrorPose.hpp"

enum MH_STATUS { MH_OK = 0, MH_ERROR_NOT_EXECUTABLE = 9 };
static int g_mhCreate = 0;
static MH_STATUS g_mhResult = MH_OK;
static void* g_mhDetour = nullptr;
static void** g_mhOriginal = nullptr;
static MH_STATUS MH_CreateHook(void*, void* detour, void** original) { ++g_mhCreate; g_mhDetour = detour; g_mhOriginal = original; return g_mhResult; }
static MH_STATUS MH_EnableHook(void*) { return MH_OK; }

namespace kh2coop {
namespace inject {
namespace offsets = kh2coop::offsets;
static uintptr_t g_exeBase = 0;
static std::uint32_t g_frameCounter = 0;
static constexpr float PUPPET_TIME_DRIFT_FRAMES = 2.0f;
static int g_logLines = 0;
static char g_lastLog[1024] {};
static void Log(const char* fmt, ...) {
    ++g_logLines;
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(g_lastLog, sizeof(g_lastLog), fmt, a);
    va_end(a);
    std::printf("%s\n", g_lastLog);
}
static bool ReadHitTraceMemory(uintptr_t address, void* out, std::size_t size) {
    __try { std::memcpy(out, reinterpret_cast<const void*>(address), size); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
template <typename T> static bool ReadHitTrace(uintptr_t address, T& out) { return ReadHitTraceMemory(address, &out, sizeof(out)); }
using PFN_ResolveHandle = uintptr_t(__fastcall*)(std::uint32_t);
static uintptr_t g_handler = 0;
static uintptr_t __fastcall FakeResolve(std::uint32_t) { return g_handler; }
static PFN_ResolveHandle g_resolveHandle = &FakeResolve;
using PFN_SetAnimationDirect = void(__fastcall*)(void*, int, float, float);
static int g_sets = 0, g_lastSet = -1;
static void __fastcall FakeSetAnim(void* motCtrl, int id, float, float) {
    ++g_sets; g_lastSet = id;
    *reinterpret_cast<std::uint32_t*>(reinterpret_cast<uintptr_t>(motCtrl) + 0x28) = static_cast<std::uint32_t>(id);
    *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(motCtrl) + 0x44) = 0.0f;
}
static PFN_SetAnimationDirect g_setAnimationUnderlying = &FakeSetAnim;
namespace enemysync {
static bool g_requested = true;
static enemymirror::Gate g_gate = enemymirror::Gate::None;
static uintptr_t g_for = 0;
static enemymirror::Pose g_pose {};
static bool MirrorRequested() noexcept { return g_requested; }
static bool g_trace = false;
static double g_cursor = 30.0;
static bool MirrorTrace() noexcept { return g_trace; }
static double MirrorCursor() noexcept { return g_cursor; }
static enemymirror::Gate MirrorPose(uintptr_t actor, enemymirror::Pose& out) noexcept {
    if (actor != g_for) return enemymirror::Gate::None;
    out.netId = g_pose.netId;
    if (g_gate == enemymirror::Gate::Drive) out = g_pose;
    return g_gate;
}
}  // namespace enemysync
#include "EnemyMirror.inl"
}  // namespace inject
}  // namespace kh2coop

using namespace kh2coop::inject;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)

static int g_brainCalls = 0;
static uintptr_t g_brainActor = 0;
static void __fastcall FakeBrain(void*, void* actor) { ++g_brainCalls; g_brainActor = reinterpret_cast<uintptr_t>(actor); }

static float& F(uintptr_t a) { return *reinterpret_cast<float*>(a); }
static std::uint32_t& U(uintptr_t a) { return *reinterpret_cast<std::uint32_t*>(a); }
static uintptr_t& Q(uintptr_t a) { return *reinterpret_cast<uintptr_t*>(a); }

using Detour = void(__fastcall*)(void*, void*);
static Detour g_detour = nullptr;
// One native update of `a`: PreUpdate, the class brain (through our detour once hooked), PostUpdate.
static void Update(uintptr_t a) {
    enemymirror::PreUpdate(a);
    if (g_detour) g_detour(reinterpret_cast<void*>(g_handler), reinterpret_cast<void*>(a));
    enemymirror::PostUpdate(a);
}

int main() {
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x3000000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    auto* heap = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image || !heap) return 2;
    g_exeBase = reinterpret_cast<uintptr_t>(image);
    // The brain thunk at a fake class: mov rcx,rdx; jmp exe+0x3B4460.
    const uintptr_t thunk = g_exeBase + 0x419B10, brain = g_exeBase + 0x3B4460;
    const std::uint8_t head[4] = {0x48, 0x8B, 0xCA, 0xE9};
    std::memcpy(image + 0x419B10, head, 4);
    const std::int32_t rel = static_cast<std::int32_t>(static_cast<std::intptr_t>(brain) - static_cast<std::intptr_t>(thunk + 8));
    std::memcpy(image + 0x419B14, &rel, 4);
    const uintptr_t vtable = g_exeBase + 0x5D2D68;
    Q(vtable + 0x20) = thunk;
    g_handler = g_exeBase + 0x700000;
    Q(g_handler) = vtable;
    const uintptr_t actor = reinterpret_cast<uintptr_t>(heap) + 0x1000;
    const uintptr_t other = reinterpret_cast<uintptr_t>(heap) + 0x4000;
    const uintptr_t status = reinterpret_cast<uintptr_t>(heap) + 0x8000;
    for (uintptr_t a : {actor, other}) { U(a) = 0x80001234; Q(a + 0x5C0) = status; }
    *reinterpret_cast<std::int32_t*>(status) = 20;
    const uintptr_t e = actor + offsets::actor::ENTITY_TRANSFORM;
    const uintptr_t motCtrl = actor + 0x158;
    F(e + 0x30) = 0.0f; F(e + 0x34) = -20.0f; F(e + 0x38) = 0.0f;
    U(actor + 0x180) = 9;                       // motCtrl+0x28: the LOCAL AI is already in attack 9
    F(motCtrl + 0x40) = 40.0f;                  // motion end
    F(motCtrl + 0x44) = 30.0f;                  // local attack time
    F(actor + 0xB98) = 5.0f; F(actor + 0x690) = 5.0f; F(actor + 0xA48) = 5.0f;
    enemysync::g_pose.netId = 7; enemysync::g_pose.objectId = 302; enemysync::g_pose.motionId = 9;
    enemysync::g_pose.motionTime = 12.0f; enemysync::g_pose.position = {80.0f, -20.0f, 8.0f};
    enemysync::g_pose.rotationY = 1.0f; enemysync::g_pose.cursor = 500.0;

    // Off: nothing happens.
    enemysync::g_requested = false;
    enemysync::g_for = actor; enemysync::g_gate = enemymirror::Gate::Drive;
    g_frameCounter = 100;
    Update(actor);
    CHECK(g_mhCreate == 0 && !enemymirror::BlockMotion(actor) && !enemymirror::Find(actor));
    CHECK(F(e + 0x30) == 0.0f && g_sets == 0 && !enemymirror::Tracked(actor));

    // On, not bound (Gate None): untouched, no slot.
    enemysync::g_requested = true;
    enemysync::g_gate = enemymirror::Gate::None;
    Update(actor);
    CHECK(g_mhCreate == 0 && !enemymirror::Find(actor) && !enemymirror::DrivenNow(actor));

    // Bound, stream not drivable: tracked slot, not driven, no hook yet.
    enemysync::g_gate = enemymirror::Gate::Bound;
    ++g_frameCounter;
    Update(actor);
    enemymirror::Driven* d = enemymirror::Find(actor);
    CHECK(d && !d->running && d->netId == 7 && d->firstBound == g_frameCounter && g_mhCreate == 0);
    CHECK(enemymirror::Tracked(actor) && !enemymirror::DrivenNow(actor));

    // Drivable, but the spawn has not settled (S7): held on local AI.
    enemysync::g_gate = enemymirror::Gate::Drive;
    ++g_frameCounter;
    Update(actor);
    CHECK(!d->running && d->settleHolds == 1 && g_mhCreate == 0);
    g_frameCounter = d->firstBound + enemymirror::kSpawnSettleFrames - 1;
    Update(actor);
    CHECK(!d->running && d->settleHolds == 2);

    // Settled: take-over, the class brain thunk is hooked once.
    ++g_frameCounter;
    enemymirror::PreUpdate(actor);
    CHECK(g_mhCreate == 1 && enemymirror::DrivenNow(actor) && enemymirror::g_brainCount == 1);
    CHECK(d->running && d->runStart == g_frameCounter && d->runs == 1 && d->forceSet && d->drivenUpdates == 1);
    *g_mhOriginal = reinterpret_cast<void*>(&FakeBrain);
    g_detour = reinterpret_cast<Detour>(g_mhDetour);
    // Brain: skipped for the driven actor (per-actor count), passed through for another.
    g_detour(reinterpret_cast<void*>(g_handler), reinterpret_cast<void*>(actor));
    CHECK(g_brainCalls == 0 && d->skips == 1 && enemymirror::g_mstats.skips == 1);
    g_detour(reinterpret_cast<void*>(g_handler), reinterpret_cast<void*>(other));
    CHECK(g_brainCalls == 1 && g_brainActor == other);
    // Motion guard: the game's set is swallowed while living; ours passes.
    CHECK(enemymirror::BlockMotion(actor) && !enemymirror::BlockMotion(other) && d->blocked == 1);
    enemymirror::g_guard = true;   // our own guarded set (SetMotion) must pass the guard
    CHECK(!enemymirror::BlockMotion(actor));
    enemymirror::g_guard = false;
    // Post-update, first driven frame: the set is FORCED although the local id is already 9
    // (B1 carry-over), then the stream time, blended position, facing, zeroed motion terms.
    enemymirror::PostUpdate(actor);
    CHECK(g_sets == 1 && g_lastSet == 9 && d->ourSets == 1 && d->setMotion == 9 && d->setFrame == g_frameCounter && !d->forceSet);
    CHECK(std::fabs(F(motCtrl + 0x44) - 12.0f) < 1e-4f);
    const float w = enemymirror::BlendWeight(enemymirror::kBlendFrames);
    CHECK(std::fabs(F(e + 0x30) - 80.0f * w) < 1e-3f && F(e + 0x4C) == 1.0f &&
          std::fabs(F(e + 0x40) - std::cos(1.0f)) < 1e-5f && std::fabs(F(e + 0x48) - std::sin(1.0f)) < 1e-5f);
    CHECK(F(actor + 0xB98) == 0.0f && F(actor + 0x690) == 0.0f && F(actor + 0xA48) == 0.0f);
    // A hit built now is attributed: driven, run started at our set of this motion.
    const int linesBefore = g_logLines;
    enemymirror::g_mlogBudget = 0; enemymirror::g_runLogBudget = 0;  // other channels exhausted: hits still log
    enemymirror::NoteHit(actor, other, 9, "P_EX100", 8, 0);
    CHECK(g_logLines == linesBefore + 1 && std::strstr(g_lastLog, "driven=1") && std::strstr(g_lastLog, "setMotion=9") &&
          std::strstr(g_lastLog, "netId=7") && std::strstr(g_lastLog, "damage=8 stat=0") && enemymirror::g_mstats.hits == 1);
    // Following frames: blend converges; same motion -> no new set; skips == updates.
    for (unsigned i = 0; i < enemymirror::kBlendFrames + 1; ++i) {
        ++g_frameCounter;
        enemysync::g_pose.motionTime += 1.0f;
        Update(actor);
    }
    CHECK(F(e + 0x30) == 80.0f && F(e + 0x38) == 8.0f && g_sets == 1);
    CHECK(d->skips == d->drivenUpdates && d->drivenUpdates == enemymirror::kBlendFrames + 2 && g_brainCalls == 1);
    CHECK(d->noBrain == 0 && d->maxNoBrainRun == 0);
    // Driven updates that never reach the brain (hit-stop, another path) are counted, with the longest run (rev-2 F3).
    for (int i = 0; i < 2; ++i) {
        ++g_frameCounter;
        enemysync::g_pose.motionTime += 1.0f;
        enemymirror::PreUpdate(actor);
        enemymirror::PostUpdate(actor);
    }
    CHECK(d->noBrain == 2 && d->maxNoBrainRun == 2 && d->missMotion == 9 && d->skips + 2 == d->drivenUpdates);
    ++g_frameCounter;
    enemysync::g_pose.motionTime += 1.0f;
    Update(actor);
    CHECK(d->noBrain == 2 && d->noBrainRun == 0 && d->maxNoBrainRun == 2);
    // Back-to-back repeat of the same attack (time drops from 21 to 1 under id 9): re-issued (S2).
    enemysync::g_pose.motionTime = 1.0f;
    ++g_frameCounter;
    Update(actor);
    CHECK(g_sets == 2 && d->restarts == 1 && F(motCtrl + 0x44) <= 1.0f);  // restarted from 0, within the drift threshold
    // Held past the native motion end: clamped just inside it.
    enemysync::g_pose.motionTime = 55.0f;
    ++g_frameCounter;
    Update(actor);
    CHECK(std::fabs(F(motCtrl + 0x44) - 39.5f) < 1e-4f && g_sets == 2);
    // A missed update or three is not a take-over (S6): no new blend, no new set, counted as a gap.
    const auto takeovers = enemymirror::g_mstats.takeovers;
    g_frameCounter += 2;
    Update(actor);
    g_frameCounter += enemymirror::kGapTolerance;
    Update(actor);
    CHECK(enemymirror::g_mstats.takeovers == takeovers && d->gaps == 2 && d->blend == 0 && g_sets == 2 && d->runs == 1);
    // A longer gap is a new run: blend and a forced set again.
    g_frameCounter += enemymirror::kGapTolerance + 1;
    enemymirror::PreUpdate(actor);
    CHECK(enemymirror::g_mstats.takeovers == takeovers + 1 && d->runs == 2 && d->blend == enemymirror::kBlendFrames && d->forceSet);
    g_detour(reinterpret_cast<void*>(g_handler), reinterpret_cast<void*>(actor));
    enemymirror::PostUpdate(actor);
    CHECK(g_sets == 3 && d->setFrame == g_frameCounter && d->runStart == g_frameCounter);
    // Death: a lethal HP stops the guard and the writes; the death motion stays native.
    *reinterpret_cast<std::int32_t*>(status) = 0;
    CHECK(!enemymirror::BlockMotion(actor));
    F(e + 0x30) = 1.0f;
    enemymirror::PostUpdate(actor);
    CHECK(F(e + 0x30) == 1.0f);
    *reinterpret_cast<std::int32_t*>(status) = 20;
    // Stream gap (Gate Bound): the run ends, the brain runs and is counted as a bound pass-through.
    enemysync::g_gate = enemymirror::Gate::Bound;
    enemysync::g_trace = true;
    ++g_frameCounter;
    Update(actor);
    CHECK(!d->running && !enemymirror::DrivenNow(actor) && !enemymirror::BlockMotion(actor));
    CHECK(g_brainCalls == 2 && d->passBound == 1 && enemymirror::g_mstats.passBound == 1);
    // An attack landing now is NOT attributed as driven.
    enemymirror::NoteHit(actor, other, 9, "P_EX100", 8, 0);
    CHECK(std::strstr(g_lastLog, "hit ") && std::strstr(g_lastLog, "driven=0") != nullptr);
    // Re-take: a new run with blend and a forced set. The trace line for the undriven
    // frame before it is logged too, marked driven=0 (rev-2 F1).
    enemysync::g_gate = enemymirror::Gate::Drive;
    ++g_frameCounter;
    enemymirror::PreUpdate(actor);
    CHECK(std::strstr(g_lastLog, "trace-client netId=7 hostFrame=30") && std::strstr(g_lastLog, "driven=0"));
    enemysync::g_trace = false;
    g_detour(reinterpret_cast<void*>(g_handler), reinterpret_cast<void*>(actor));
    enemymirror::PostUpdate(actor);
    CHECK(d->running && d->runs == 3 && g_sets == 4 && d->skips + d->noBrain == d->drivenUpdates);
    // The stats channel logs with every budget at zero.
    const int statsBefore = g_logLines;
    g_frameCounter += enemymirror::kStatsInterval;
    Update(actor);
    CHECK(g_logLines >= statsBefore + 2);  // global line + this actor's line (another run started: run budget is 0)
    // Unbound (death / unbind): the slot retires; the actor is no longer tracked.
    enemysync::g_gate = enemymirror::Gate::None;
    ++g_frameCounter;
    Update(actor);
    CHECK(!enemymirror::Find(actor) && !enemymirror::Tracked(actor) && g_brainCalls == 3);
    // A class whose +0x20 is not the verified thunk stays on local AI, refused once.
    g_detour = nullptr;
    const uintptr_t vtable2 = g_exeBase + 0x5C71D8, thunk2 = g_exeBase + 0x3DB4C0;
    image[0x3DB4C0] = 0xCC;
    Q(vtable2 + 0x20) = thunk2;
    Q(g_handler) = vtable2;
    enemysync::g_for = other; enemysync::g_gate = enemymirror::Gate::Drive;
    ++g_frameCounter;
    enemymirror::PreUpdate(other);  // first bound frame: settling, no hook attempt yet
    g_frameCounter += enemymirror::kSpawnSettleFrames;
    enemymirror::PreUpdate(other);
    CHECK(!enemymirror::DrivenNow(other) && enemymirror::g_mstats.refusedClass == 1 && g_mhCreate == 1);
    ++g_frameCounter;
    enemymirror::PreUpdate(other);
    CHECK(enemymirror::g_mstats.refusedClass == 1);  // remembered, no second scan
    // A failed MinHook also leaves the class on local AI.
    const uintptr_t vtable3 = g_exeBase + 0x5C9C40, thunk3 = g_exeBase + 0x3F8AA0;
    std::memcpy(image + 0x3F8AA0, head, 4);
    const std::int32_t rel3 = static_cast<std::int32_t>(static_cast<std::intptr_t>(brain) - static_cast<std::intptr_t>(thunk3 + 8));
    std::memcpy(image + 0x3F8AA4, &rel3, 4);
    Q(vtable3 + 0x20) = thunk3;
    Q(g_handler) = vtable3;
    g_mhResult = MH_ERROR_NOT_EXECUTABLE;
    ++g_frameCounter;
    enemymirror::PreUpdate(other);
    CHECK(!enemymirror::DrivenNow(other) && enemymirror::g_brainCount == 1 && enemymirror::g_mstats.refusedClass == 2);
    std::printf("EnemyMirrorDriverTest: %s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
