// Windows-only test of the actual private lifecycle capture/SEH/queue helpers.
// No native executable, process, MinHook installation, or bridge is used.
#if !defined(_WIN32) || !defined(_MSC_VER)
#error NativeLifecycleTraceTest requires Windows and MSVC-compatible SEH.
#endif
#include "../inject/src/NativeLifecycleTrace.cpp"
#include <iostream>

namespace {
int errors = 0;
unsigned hookCalls = 0, stateReads = 0;
std::atomic<DWORD> diagnosticThread {0};
std::atomic<unsigned> roleCalls {0}, serialReads {0};
std::uint32_t transition = 7, load = 11;
bool stampAvailable = true, stateAvailable = true;
uintptr_t testController = 0;
kh2coop::inject::spawncontroller::TraceState controllerState;
constexpr DWORD deliberateException = 0xE0424C54;
void Check(bool value, const char* label) {
    std::cout << (value ? "PASS: " : "FAIL: ") << label << '\n';
    if (!value) ++errors;
}
}

// Shared snapshot service is a synthetic callback boundary in this test. The
// real lifecycle actor reader, native call wrappers, queue and unwind code run.
namespace kh2coop::inject::spawncontroller {
void RegisterDiagnosticGameThread() {
    DWORD none = 0;
    diagnosticThread.compare_exchange_strong(none, GetCurrentThreadId());
}

bool IsDiagnosticGameThread() {
    const auto registered = diagnosticThread.load();
    return registered != 0 && registered == GetCurrentThreadId();
}
bool CaptureDiagnosticStamp(uintptr_t, TraceStamp& stamp) {
    stamp = {};
    stamp.location[0] = 5; stamp.location[1] = 6;
    if (!IsDiagnosticGameThread()) return false;
    ++serialReads;
    stamp.transition = transition; stamp.load = load;
    return stampAvailable;
}
TraceState CaptureDiagnosticState(uintptr_t, uintptr_t controller) {
    ++stateReads;
    auto result = controllerState;
    result.controllerAvailable = stateAvailable && controller && controller == testController;
    result.cacheAvailable = stateAvailable;
    return result;
}
}

namespace kh2coop::inject::warp {
std::uint32_t TransitionSerial() {return transition;}
std::uint32_t LoadSerial() {return load;}
}

MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_EnableHook(LPVOID) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_DisableHook(LPVOID) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }
MH_STATUS WINAPI MH_RemoveHook(LPVOID) { ++hookCalls; return MH_ERROR_NOT_INITIALIZED; }

namespace {
using namespace kh2coop::inject::lifecycletrace;
unsigned removalCalls = 0, disposalCalls = 0, deathMarkCalls = 0, deathBookCalls = 0, countCalls = 0;
bool nestedCount = false, faultCount = false, destroyStatus = false, changeLifecycle = false;
bool argumentsPreserved = true;
void* expectedActor = nullptr;
void* expectedController = nullptr;

// Scopes live in the retained FLS-owned frame store (NativeTraceFiber.hpp), not
// thread_local stack pointers. Idle means this fiber's retained store is still
// usable and has no open lifecycle or predicate frame.
bool Idle() {
    auto* local = g_storage.Current();
    return local && local->depth == 0;
}
// Runs fn on a fresh thread (fresh FLS cell) and returns its exit code.
DWORD RunOnThread(LPTHREAD_START_ROUTINE fn, void* argument) {
    HANDLE thread = CreateThread(nullptr, 0, fn, argument, 0, nullptr);
    if (!thread) return 0xFFFF;
    // Do not release stack-owned synthetic memory while worker may use it.
    if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) ExitProcess(1);
    DWORD code = 0xFFFF;
    if (!GetExitCodeThread(thread, &code)) code = 0xFFFF;
    CloseHandle(thread);
    return code;
}

template <typename T, std::size_t N> void Put(std::array<std::uint8_t, N>& bytes, std::size_t offset, T value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
std::uint8_t Role() { ++roleCalls; return 1; }
std::uint8_t OffRole() { ++roleCalls; return 0; }

void __fastcall OriginalCount(void* controller) {
    ++countCalls;
    argumentsPreserved = argumentsPreserved && controller == expectedController;
    if (faultCount) RaiseException(deliberateException, 0, 0, nullptr);
    --controllerState.currentCount;
}
void __fastcall OriginalRemoval(void* controller, void* actor) {
    ++removalCalls;
    argumentsPreserved = argumentsPreserved && controller == expectedController && actor == expectedActor;
    if (nestedCount) Run(Kind::CountDecrement, controller, nullptr, g_exeBase + 0x3FFE16);
    controllerState.cooldown = 8;
    controllerState.cacheIds[0] = 0;
    if (changeLifecycle) ++transition;
}
void __fastcall OriginalDisposal(void* actor) {
    ++disposalCalls;
    argumentsPreserved = argumentsPreserved && actor == expectedActor;
    if (destroyStatus) {
        const uintptr_t zero = 0;
        std::memcpy(static_cast<std::uint8_t*>(actor) + 0x5C0, &zero, sizeof(zero));
    }
}
void __fastcall OriginalDeathBook(void* controller, void* actor) {
    ++deathBookCalls;
    argumentsPreserved = argumentsPreserved && controller == expectedController && actor == expectedActor;
    Run(Kind::CountDecrement, controller, nullptr, g_exeBase + 0x3FED3A);
}
void __fastcall OriginalDeathMark(void* actor) {
    ++deathMarkCalls;
    argumentsPreserved = argumentsPreserved && actor == expectedActor;
    Run(Kind::DeathBookkeeping, expectedController, actor, g_exeBase + 0x3D4A6C);
    std::uint32_t flags = 4;
    std::memcpy(static_cast<std::uint8_t*>(actor) + 0x9B8, &flags, sizeof(flags));
}

bool CatchRemoval() {
    __try { Run(Kind::RemovalBookkeeping, expectedController, expectedActor, g_exeBase + 0x41181D); }
    __except (GetExceptionCode() == deliberateException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
DWORD WINAPI ForeignObservation(void*) {
    // First registration already belongs to main; foreign callers cannot steal it.
    kh2coop::inject::spawncontroller::RegisterDiagnosticGameThread();
    Run(Kind::RemovalBookkeeping, expectedController, expectedActor, g_exeBase + 0x41181D);
    return Idle() ? 0 : 1;
}
// Fills this fresh thread's retained store to the depth cap with a valid
// enclosing frame, then observes once. Exit 0: the original ran, nothing was
// recorded, and the overfull store was abandoned (fail-closed, never reused).
DWORD WINAPI DepthCapObservation(void* predicate) {
    auto* local = g_storage.Current();
    if (!local || local->depth) return 2;
    if (predicate) diagnosticThread.store(GetCurrentThreadId());
    auto& top = local->frames[kDepthCap - 1];
    top = {};
    top.anchor = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
    top.serial = ++g_scopeSerial;
    top.event.sequence = predicate ? 77 : 999;
    local->depth = kDepthCap;
    if (predicate) RunPredicate(expectedActor, g_exeBase + 0x3BFD6F);
    else if (CatchRemoval()) return 3;
    return g_storage.Current() == nullptr ? 0 : 1;
}

unsigned predicateCalls = 0, scriptCalls = 0, auxiliaryCalls = 0;
unsigned predicateCase = 0, predicateRecursion = 0;
bool scriptFault = false, auxiliaryFault = false, parentFault = false;
bool repeatScript = false, nestedPredicate = false, coverageChange = false;
bool wrongScriptActor = false, noPredicateChildren = false;
bool auxiliaryNoWrite = false;
std::uint8_t parentByte = 1, scriptByte = 1, auxiliaryByte = 1;
void* expectedAuxiliary = nullptr;

std::uint8_t __fastcall OriginalScript(void* actor) {
    ++scriptCalls;
    argumentsPreserved = argumentsPreserved && actor == expectedActor;
    if (scriptFault) RaiseException(deliberateException, 0, 0, nullptr);
    if (actor == expectedActor) {
        // Model native script service changing operands. Snapshot must be after
        // this actual invocation, not an extra diagnostic service invocation.
        std::uint32_t value = 0x12345678;
        std::memcpy(static_cast<std::uint8_t*>(actor) + 0x5B8, &value, sizeof(value));
    }
    return scriptByte;
}
std::uint8_t __fastcall OriginalAuxiliary(void* argument) {
    ++auxiliaryCalls;
    argumentsPreserved = argumentsPreserved && argument == expectedAuxiliary;
    if (auxiliaryFault) RaiseException(deliberateException, 0, 0, nullptr);
    if (!auxiliaryNoWrite) {
        std::uint32_t value = 0x87654321;
        std::memcpy(static_cast<std::uint8_t*>(argument) + 0x14, &value, sizeof(value));
    }
    return auxiliaryByte;
}
std::uint8_t __fastcall OriginalPredicate(void* actor) {
    ++predicateCalls;
    argumentsPreserved = argumentsPreserved && actor == expectedActor;
    if (parentFault) RaiseException(deliberateException, 0, 0, nullptr);
    if (coverageChange) ++g_coverageGeneration;
    if (nestedPredicate && predicateRecursion == 0) {
        ++predicateRecursion;
        RunPredicate(actor, g_exeBase + 0x3BFD6F);
        --predicateRecursion;
    }
    if (!noPredicateChildren) {
        const auto result = RunPredicateChild(true, actor, g_exeBase + (wrongScriptActor ? 0x3DAC3F : 0x3DAC3E));
        argumentsPreserved = argumentsPreserved && result == scriptByte;
        if (repeatScript) RunPredicateChild(true, actor, g_exeBase + 0x3DAC3E);
        if (predicateCase == 2 || predicateCase == 3) {
            const auto auxiliary = RunPredicateChild(false, expectedAuxiliary, g_exeBase + 0x3DAC9C);
            argumentsPreserved = argumentsPreserved && auxiliary == auxiliaryByte;
        }
    }
    return parentByte;
}
bool CatchPredicate(std::uint8_t* result) {
    __try { *result = RunPredicate(expectedActor, g_exeBase + 0x3BFD6F); }
    __except (GetExceptionCode() == deliberateException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
DWORD WINAPI ForeignPredicateObservation(void*) {
    RunPredicate(expectedActor, g_exeBase + 0x3BFD6F);
    return Idle() ? 0 : 1;
}

void TestPredicates() {
    std::array<std::uint8_t, 0xC00> actor {};
    std::array<std::uint8_t, 0xC00> otherActor {};
    std::array<std::uint8_t, 0x20> auxiliary {};
    auto* previousActor = expectedActor;
    expectedActor = actor.data(); expectedAuxiliary = auxiliary.data();
    Put(actor, 0x5B0, uintptr_t {0x12340000});
    Put(actor, 0x5B8, std::uint32_t {7});
    Put(actor, 0x80, uintptr_t {0x45670000}); Put(actor, 0x98, uintptr_t {0x78900000});
    Put(actor, 0xBB4, std::uint32_t {0xABCDEF12});
    Put(auxiliary, 0x14, std::uint32_t {42});
    // Deliberately no status/objentry: genuine branch facts must survive missing
    // HP/classification, without turning that absence into a lifetime claim.
    g_predicate = OriginalPredicate; g_script = OriginalScript; g_auxiliary = OriginalAuxiliary;
    g_verified.store(RemovalPredicateHooks); g_installed.store(RemovalPredicateHooks);
    const auto observed = CaptureRemovalOperands(reinterpret_cast<uintptr_t>(actor.data()));
    Check(observed.availableMask == 31 && observed.scriptState == 0x12340000 && observed.scriptTest == 7 &&
          observed.field80 == 0x45670000 && observed.field98 == 0x78900000 && observed.auxiliaryHandle == 0xABCDEF12,
          "predicate operands preserve independent pointer/dword/handle boundary reads");
    Check(!CaptureRemovalOperands(1).availableMask, "unreadable predicate operands stay explicitly unavailable");

    constexpr RemovalBranch branches[] {RemovalBranch::ScriptBlocked, RemovalBranch::PointersBlocked,
        RemovalBranch::AuxiliaryBlocked, RemovalBranch::AuxiliaryAllowed, RemovalBranch::AuxiliaryMissingAllowed};
    Event event;
    for (unsigned i = 0; i < 5; ++i) {
        predicateCase = i; scriptByte = i == 0 ? 0 : 0xA7;
        auxiliaryByte = i == 2 ? 0xD2 : 0;
        parentByte = i >= 3 ? 0xB5 : 0;
        const auto p = predicateCalls, s = scriptCalls, a = auxiliaryCalls;
        std::uint8_t result = 0xEE;
        const bool fault = CatchPredicate(&result);
        Check(!fault && result == parentByte && predicateCalls == p + 1 && scriptCalls == s + 1 &&
              auxiliaryCalls == a + ((i == 2 || i == 3) ? 1u : 0u) && argumentsPreserved && PopEvent(event) &&
              event.kind == Kind::RemovalPredicate && event.originalReturned && event.removal.originalReturned &&
              event.removal.resultAvailable && event.removal.parentResult == parentByte &&
              event.removal.scriptResult == scriptByte && event.removal.branch == branches[i] &&
              event.removal.scriptCalls == 1 && event.removal.scriptReturned == 1 &&
              event.removal.coverageMask == RemovalPredicateHooks && event.removal.coverageStable &&
              event.removal.afterScript.scriptTest == 0x12345678 && event.removal.after.availableMask == 31 &&
              !event.beforeActor.available && Idle(),
              "each of five normal removal outcomes uses genuine AL and calls each actual original once without HP gating");
        if (i == 2 || i == 3)
            Check(event.removal.auxiliaryArgument == reinterpret_cast<uintptr_t>(auxiliary.data()) &&
                  event.removal.auxiliaryAvailableMask == 3 && event.removal.auxiliaryAfter == 0x87654321 &&
                  event.removal.auxiliaryResult == auxiliaryByte,
                  "auxiliary argument and dword+14 samples come from genuine child, with exact AL");
    }
    Check(!PopEvent(event), "predicate children do not enqueue separate lifecycle events");

    // Completed predicate is no longer an enclosing scope of later removal.
    const auto previousRemoval = g_removal;
    g_removal = [](void*, void*) {};
    Run(Kind::RemovalBookkeeping, nullptr, actor.data(), g_exeBase + 0x41181D);
    Check(PopEvent(event) && event.parentSequence == 0 && event.depth == 0,
          "later bookkeeping is not nested beneath an already returned predicate");
    g_removal = previousRemoval;

    g_installed.store(HookBit(Kind::RemovalPredicate) | ScriptPredicateHook);
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && event.removal.branch == RemovalBranch::Unknown && event.removal.originalReturned &&
          event.removal.scriptReturned == 1 && event.removal.coverageStable,
          "partial child coverage cannot infer a no-auxiliary branch");
    g_installed.store(RemovalPredicateHooks);
    scriptByte = 0; parentByte = 1;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && event.removal.resultAvailable && event.removal.parentResult == 1 &&
          event.removal.scriptResult == 0 && event.removal.branch == RemovalBranch::Unknown,
          "contradictory genuine return combination remains unknown without changing native return");
    scriptByte = 1;
    coverageChange = true;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && !event.removal.coverageStable && event.removal.branch == RemovalBranch::Unknown,
          "coverage generation change invalidates branch completion even when masks are equal");
    coverageChange = false;
    repeatScript = true;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && event.removal.scriptCalls == 2 && event.removal.scriptReturned == 2 &&
          event.removal.branch == RemovalBranch::Unknown, "repeated child calls are counted rather than silently overwritten");
    repeatScript = false;
    wrongScriptActor = true; // Wrong exact return RVA, while argument is unchanged.
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && event.removal.scriptCalls == 0 && event.removal.branch == RemovalBranch::Unknown,
          "unmatched child caller passes through without supplement reads or attribution");
    wrongScriptActor = false;

    // Open a synthetic retained predicate parent frame for the current actor,
    // anchored at this test frame so the parent validates as live.
    auto* scope = g_storage.Current();
    Check(scope && scope->depth == 0, "retained scope store is idle before synthetic predicate parent");
    if (scope && scope->depth == 0) {
        auto& unmatched = scope->frames[0];
        unmatched = {};
        unmatched.anchor = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
        unmatched.serial = ++g_scopeSerial;
        unmatched.predicate = true;
        unmatched.event.actor = reinterpret_cast<uintptr_t>(actor.data());
        scope->depth = 1;
        expectedActor = otherActor.data(); // Synthetic original expects the distinct argument.
        const auto unmatchedCalls = scriptCalls;
        Check(RunPredicateChild(true, expectedActor, g_exeBase + 0x3DAC3E) == scriptByte &&
              scriptCalls == unmatchedCalls + 1 && unmatched.event.removal.scriptCalls == 0 &&
              g_storage.Current() == scope && scope->depth == 1,
              "exact child caller with another actor cannot attach observations to the current parent");
        unmatched = {}; scope->depth = 0;
    }
    expectedActor = actor.data();

    predicateCase = 3; parentByte = scriptByte = 1; auxiliaryByte = 0;
    expectedAuxiliary = reinterpret_cast<void*>(1); auxiliaryNoWrite = true;
    const auto readFaults = GetStats().nativeFaults;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && event.removal.auxiliaryAvailableMask == 0 &&
          event.removal.auxiliaryReturned == 1 && event.removal.branch == RemovalBranch::AuxiliaryAllowed &&
          GetStats().nativeFaults == readFaults,
          "diagnostic read failure preserves actual child outcome and is not a native exception");
    expectedAuxiliary = auxiliary.data(); auxiliaryNoWrite = false;
    predicateCase = 4;

    const auto reads = stateReads, serials = serialReads.load(), roles = roleCalls.load();
    const auto calls = predicateCalls;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD70);
    Check(predicateCalls == calls + 1 && !PopEvent(event) && stateReads == reads && serialReads.load() == serials &&
          roleCalls.load() == roles && Idle(),
          "unmatched parent executes once without snapshot/role callbacks or inherited child attribution");
    nestedPredicate = true;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Event nested;
    Check(PopEvent(nested) && PopEvent(event) && nested.parentSequence == event.sequence && nested.depth == 1 &&
          nested.removal.scriptCalls == 1 && event.removal.scriptCalls == 1 &&
          event.removal.nestedAmbiguous && event.removal.branch == RemovalBranch::Unknown && Idle(),
          "recursive parents bind children to the top scope and explicitly invalidate outer branch inference");
    nestedPredicate = false;
    RemovalPredicates saturated;
    saturated.scriptCalls = UINT32_MAX;
    const auto overflow = GetStats().predicateCountOverflow;
    IncrementPredicateCount(saturated, saturated.scriptCalls);
    IncrementPredicateCount(saturated, saturated.scriptCalls);
    Check(saturated.scriptCalls == UINT32_MAX && saturated.countOverflow && GetStats().predicateCountOverflow == overflow + 1,
          "predicate counters saturate and report ambiguity once per scope");

    for (unsigned site = 0; site < 3; ++site) {
        predicateCase = 2; scriptByte = auxiliaryByte = 1; parentByte = 0;
        parentFault = site == 0; scriptFault = site == 1; auxiliaryFault = site == 2;
        const auto beforeReads = stateReads, beforeSerials = serialReads.load();
        const auto beforeCalls = predicateCalls;
        std::uint8_t result = 0xEE;
        Check(CatchPredicate(&result) && result == 0xEE && predicateCalls == beforeCalls + 1 && PopEvent(event) &&
              event.unwound && event.exceptionCode == deliberateException && !event.removal.originalReturned &&
              !event.removal.resultAvailable && event.removal.branch == RemovalBranch::Unknown &&
              event.removal.after.availableMask == 0 && !event.afterStampAvailable &&
              !event.afterState.controllerAvailable && stateReads == beforeReads + 1 && serialReads.load() == beforeSerials + 1 &&
              Idle(),
              "parent or child native exception propagates unchanged with no post-read cleanup or stale TLS");
        if (site != 0) {
            const auto bit = site == 1 ? ScriptPredicateHook : AuxiliaryPredicateHook;
            Check((event.removal.faultMask & bit) && (event.removal.unwindMask & bit) &&
                  (site == 1 ? event.removal.scriptReturned == 0 && event.removal.afterScript.availableMask == 0 :
                               event.removal.auxiliaryReturned == 0 && event.removal.auxiliaryAvailableMask == 1),
                  "child interruption preserves entry operands but never fabricates its normal return");
        }
        parentFault = scriptFault = auxiliaryFault = false;
    }
    predicateCase = 4; parentByte = scriptByte = 1;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(PopEvent(event) && event.removal.branch == RemovalBranch::AuxiliaryMissingAllowed,
          "fresh predicate scope works after propagated native faults");

    // Complete first observation remains retained if a later parent is dropped.
    for (std::size_t i = 0; i < kQueueCap; ++i) Publish(event);
    const auto drops = GetStats().predicateDropped;
    const auto originalCalls = predicateCalls;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(GetStats().predicateDropped == drops + 1 && predicateCalls == originalCalls + 1 && Idle(),
          "full predicate queue exposes loss without replacing records or suppressing native execution");
    while (PopEvent(event)) {}
    event.kind = Kind::RemovalPredicate;
    for (std::size_t i = 0; i < kQueueCap; ++i) Publish(event);
    parentFault = true;
    std::uint8_t ignored = 0xEE;
    const auto faultDrops = GetStats().predicateDropped;
    Check(CatchPredicate(&ignored) && GetStats().predicateDropped == faultDrops + 1 &&
          Idle() && ignored == 0xEE,
          "predicate queue loss during unwind preserves original exception and restores scopes");
    parentFault = false;
    while (PopEvent(event)) {}
    // A depth-cap store is fail-closed: run it on its own fiber-local cell so the
    // main thread's retained store stays usable for the remaining checks.
    const auto depthDrops = GetStats().predicateDepthOverflow;
    const auto depthPredicateCalls = predicateCalls;
    const auto registered = diagnosticThread.load();
    const auto depthCode = RunOnThread(DepthCapObservation, reinterpret_cast<void*>(1));
    diagnosticThread.store(registered);
    Check(depthCode == 0 && GetStats().predicateDepthOverflow == depthDrops + 1 &&
          predicateCalls == depthPredicateCalls + 1 && !PopEvent(event) && Idle(),
          "predicate depth loss calls the original once, hides unobserved children and abandons the overfull scope");

    diagnosticThread.store(0);
    const auto unknownReads = stateReads, unknownSerials = serialReads.load(), unknownRoles = roleCalls.load();
    const auto unknownCalls = predicateCalls;
    RunPredicate(actor.data(), g_exeBase + 0x3BFD6F);
    Check(predicateCalls == unknownCalls + 1 && !PopEvent(event) && stateReads == unknownReads &&
          serialReads.load() == unknownSerials && roleCalls.load() == unknownRoles,
          "unregistered predicate path passes through without semantic reads or role sampling");
    kh2coop::inject::spawncontroller::RegisterDiagnosticGameThread();

    const auto foreignReads = stateReads, foreignSerials = serialReads.load(), foreignRoles = roleCalls.load();
    const auto foreignCalls = predicateCalls;
    HANDLE thread = CreateThread(nullptr, 0, ForeignPredicateObservation, nullptr, 0, nullptr);
    Check(thread != nullptr, "synthetic foreign predicate thread starts");
    if (thread) {
        if (WaitForSingleObject(thread, 10000) != WAIT_OBJECT_0) ExitProcess(1);
        DWORD code = 1; const bool exited = GetExitCodeThread(thread, &code) != FALSE; CloseHandle(thread);
        Check(exited && code == 0 && predicateCalls == foreignCalls + 1 && stateReads == foreignReads &&
              serialReads.load() == foreignSerials && roleCalls.load() == foreignRoles && !PopEvent(event),
              "foreign predicate path calls originals once and never reads diagnostic operands, roles or serials");
    }
    auto bytes = std::array<std::uint8_t, sizeof(kPredicateBytes)> {};
    std::memcpy(bytes.data(), kPredicateBytes, bytes.size());
    Check(VerifyBytes(reinterpret_cast<uintptr_t>(bytes.data()), kPredicateBytes, bytes.size()),
          "full parent body guard accepts exact synthetic saved-byte sequence");
    bytes.back() ^= 1;
    Check(!VerifyBytes(reinterpret_cast<uintptr_t>(bytes.data()), kPredicateBytes, bytes.size()) &&
          !VerifyBytes(reinterpret_cast<uintptr_t>(bytes.data()), kPredicateBytes, 129),
          "full guard rejects a final-body mismatch and oversize length before detour installation");
    g_installed.store(0); g_verified.store(0);
    expectedActor = previousActor;
}
}

int main() {
    using namespace kh2coop::inject::lifecycletrace;
    using namespace kh2coop;
    std::array<std::uint8_t, 0xB00> actor {};
    std::array<std::uint8_t, 0x60> object {};
    std::array<std::uint8_t, 0x40> record {};
    std::array<std::int32_t, 2> status {160, 160};
    int controllerToken = 0;
    expectedActor = actor.data(); expectedController = &controllerToken;
    testController = reinterpret_cast<uintptr_t>(expectedController);
    Put(actor, offsets::actor::OBJENTRY_PTR, reinterpret_cast<uintptr_t>(object.data()));
    Put(actor, 0x5C0, reinterpret_cast<uintptr_t>(status.data()));
    Put(actor, 0x9E8, testController); Put(actor, 0x9F0, reinterpret_cast<uintptr_t>(record.data()));
    Put(actor, 0x6C8, std::uint32_t {0x800});
    Put(actor, 0xA08, 1.0f); Put(actor, 0xA0C, -0.25f); Put(actor, 0xAAC, 0.5f); Put(actor, 0xAB0, -0.125f);
    Put(object, offsets::objentry::OBJECT_ID, std::uint32_t {309});
    object[offsets::objentry::TYPE_FLAGS] = offsets::objentry::TYPE_MOB;
    object[offsets::objentry::NAME] = 'M'; object[offsets::objentry::NAME + 1] = '_';
    Put(record, 0x1E, std::uint16_t {23}); record[0x1C] = 2; record[0x30] = 3;
    controllerState.header = 0x100000; controllerState.spawnArray = 0x10002C;
    controllerState.key = 55; controllerState.headerId = 115; controllerState.nativeType = 2;
    controllerState.recordCount = 6; controllerState.currentCount = 6; controllerState.initialCount = 6;
    controllerState.cacheIds[0] = 23;
    g_exeBase = 0x140000000; g_imageSize = 0x3000000; g_role = Role;
    g_removal = OriginalRemoval; g_disposal = OriginalDisposal; g_deathMark = OriginalDeathMark;
    g_deathBook = OriginalDeathBook; g_count = OriginalCount;
    kh2coop::inject::spawncontroller::RegisterDiagnosticGameThread();

    Check(!Install(g_exeBase, nullptr, Role, false) && hookCalls == 0 && !GetStats().requested,
          "explicit opt-out performs no hook or native operation");
    // Install() would arm recording and the retained FLS store before hooking;
    // the headless test arms only those two so no MinHook call is made.
    Check(!g_recording && !g_storage.Ready(), "opt-out leaves recording and retained scope storage unarmed");
    Check(g_storage.Init(reinterpret_cast<const void*>(&Install)) && Idle(),
          "retained fiber-local scope storage initializes idle");
    g_recording = true;
    const auto snap = CaptureActor(reinterpret_cast<uintptr_t>(actor.data()));
    Check(snap.available && snap.classificationAvailable && snap.combat && snap.recordAvailable &&
          snap.hp == 160 && snap.recordId == 23 && snap.recordMode == 2 && snap.recordStage == 3 &&
          snap.fadeA08 == 1 && snap.slopeAB0 == -0.125f, "actual checked actor reader captures synthetic combat metadata and fades");

    Check(!CatchRemoval() && removalCalls == 1 && argumentsPreserved, "removal original receives exact arguments once");
    Event normal;
    Check(PopEvent(normal) && normal.originalReturned && !normal.unwound && !normal.unavailable && normal.roleAvailable && normal.role == 1 &&
          normal.postActorComparable && normal.controllerComparable && normal.lifecycleStable &&
          normal.beforeState.cacheIds[0] == 23 && normal.afterState.cacheIds[0] == 0 && normal.afterState.cooldown == 8 &&
          normal.callerInImage && normal.callerRva == 0x41181D && normal.depth == 0 && normal.parentSequence == 0,
          "normal event retains truthful before/after native state and caller");
    Check(Idle(), "normal completion restores TLS");

    Run(Kind::DeathMark, nullptr, actor.data(), g_exeBase + 0x42DD6F);
    Event countEvent, bookEvent, markEvent;
    Check(PopEvent(countEvent) && PopEvent(bookEvent) && PopEvent(markEvent) &&
          countEvent.kind == Kind::CountDecrement && bookEvent.kind == Kind::DeathBookkeeping && markEvent.kind == Kind::DeathMark &&
          countEvent.parentSequence == bookEvent.sequence && bookEvent.parentSequence == markEvent.sequence &&
          countEvent.depth == 2 && bookEvent.depth == 1 && markEvent.depth == 0 &&
          markEvent.sequence < bookEvent.sequence && bookEvent.sequence < countEvent.sequence,
          "nested death observations identify shared ancestry despite child-first publication");
    Check(deathMarkCalls == 1 && deathBookCalls == 1 && countCalls == 1 && argumentsPreserved &&
          markEvent.beforeActor.flags9B8 == 0 && markEvent.afterActor.flags9B8 == 4 &&
          markEvent.controllerFromActor && countEvent.actor == 0,
          "all death ABIs preserve arguments and controller-only count event invents no actor");

    destroyStatus = true;
    Run(Kind::Disposal, nullptr, actor.data(), g_exeBase + 0x41182A);
    Event disposed;
    Check(PopEvent(disposed) && disposalCalls == 1 && disposed.originalReturned && disposed.beforeActor.available &&
          !disposed.afterActor.available && !disposed.postActorComparable && disposed.unavailable,
          "disposal resource teardown is explicit unavailable post-actor evidence");
    Put(actor, 0x5C0, reinterpret_cast<uintptr_t>(status.data()));
    destroyStatus = false;

    changeLifecycle = true;
    const auto readsBefore = stateReads;
    Check(!CatchRemoval(), "lifecycle-changing original returns normally");
    Event changed;
    Check(PopEvent(changed) && changed.unavailable && !changed.lifecycleStable &&
          !changed.afterActor.available && !changed.afterState.controllerAvailable && stateReads == readsBefore + 1,
          "changed lifecycle prevents every poststate read through retained native addresses");
    changeLifecycle = false;

    nestedCount = true; faultCount = true;
    const auto faultsBefore = GetStats().nativeFaults;
    const auto removalsBefore = removalCalls, countsBefore = countCalls;
    Check(CatchRemoval() && removalCalls == removalsBefore + 1 && countCalls == countsBefore + 1,
          "nested native exception propagates unchanged without retrying either original");
    Event interruptedCount, interruptedRemoval;
    Check(PopEvent(interruptedCount) && PopEvent(interruptedRemoval) &&
          interruptedCount.parentSequence == interruptedRemoval.sequence &&
          interruptedCount.unwound && interruptedRemoval.unwound && !interruptedRemoval.originalReturned &&
          !interruptedRemoval.afterStampAvailable && !interruptedRemoval.afterState.cacheAvailable &&
          !interruptedRemoval.afterActor.available && interruptedRemoval.exceptionCode == deliberateException &&
          GetStats().nativeFaults == faultsBefore + 2 && Idle(),
          "native unwind publishes unavailable nested events and restores TLS (faults are per-observer)");
    nestedCount = faultCount = false;
    Check(!CatchRemoval() && PopEvent(normal) && normal.originalReturned && !normal.unwound,
          "normal observation works after native exception recovery");

    const auto originalActor = expectedActor;
    expectedActor = reinterpret_cast<void*>(1);
    Check(!CatchRemoval(), "invalid diagnostic actor read does not suppress original");
    Event unknown;
    Check(PopEvent(unknown) && unknown.unavailable && !unknown.outOfScope && !unknown.beforeActor.classificationAvailable,
          "failed classification remains unknown rather than noncombat");
    expectedActor = originalActor;
    object[offsets::objentry::TYPE_FLAGS] = 1;
    Check(!CatchRemoval() && PopEvent(unknown) && unknown.outOfScope && unknown.beforeActor.classificationAvailable,
          "successfully checked noncombat classification is explicit");
    object[offsets::objentry::TYPE_FLAGS] = offsets::objentry::TYPE_MOB;

    // Depth cap runs on its own fiber-local cell: the overfull store is abandoned
    // (fail-closed) and must not disturb the main thread's retained scopes.
    const auto droppedBefore = GetStats().dropped;
    const auto depthCalls = removalCalls;
    Check(RunOnThread(DepthCapObservation, nullptr) == 0 && removalCalls == depthCalls + 1 &&
          GetStats().dropped == droppedBefore + 1 && !PopEvent(unknown) && Idle(),
          "depth cap preserves original call, exposes loss and abandons the overfull retained scope");

    for (std::size_t i = 0; i < kQueueCap; ++i) Publish(normal);
    const auto fullDrops = GetStats().dropped;
    nestedCount = faultCount = true;
    Check(CatchRemoval() && GetStats().dropped == fullDrops + 2 && Idle(),
          "queue overflow during unwind preserves exception/TLS and counts both lost observations");
    unsigned retained = 0;
    while (PopEvent(unknown)) ++retained;
    Check(retained == kQueueCap, "queue never overwrites already retained events");

    nestedCount = faultCount = false;
    diagnosticThread.store(0); // Test-only reset models observation before registration.
    const auto rolesBeforeUnknown = roleCalls.load(), serialsBeforeUnknown = serialReads.load();
    const auto callsBeforeUnknown = removalCalls;
    Check(!CatchRemoval() && removalCalls == callsBeforeUnknown + 1 && argumentsPreserved && PopEvent(unknown) &&
          unknown.originalReturned && !unknown.roleAvailable && unknown.role == 0 &&
          !unknown.beforeStampAvailable && !unknown.afterStampAvailable && unknown.beforeStamp.transition == 0 &&
          unknown.beforeStamp.location[0] == 5 && unknown.beforeActor.available && unknown.beforeState.cacheAvailable &&
          unknown.unavailable && roleCalls.load() == rolesBeforeUnknown && serialReads.load() == serialsBeforeUnknown,
          "unregistered thread preserves original/raw snapshots without role callback or serial reads");

    kh2coop::inject::spawncontroller::RegisterDiagnosticGameThread();
    const auto registeredThread = diagnosticThread.load();
    const auto rolesBeforeForeign = roleCalls.load(), serialsBeforeForeign = serialReads.load();
    const auto callsBeforeForeign = removalCalls;
    HANDLE foreign = CreateThread(nullptr, 0, ForeignObservation, nullptr, 0, nullptr);
    Check(foreign != nullptr, "synthetic foreign diagnostic thread starts");
    if (foreign) {
        const auto waited = WaitForSingleObject(foreign, 10000);
        if (waited != WAIT_OBJECT_0) {
            // Do not release stack-owned synthetic memory while worker may use it.
            std::cerr << "FAIL: synthetic foreign diagnostic thread did not finish\n";
            ExitProcess(1);
        }
        DWORD code = 1;
        const bool exited = GetExitCodeThread(foreign, &code) != FALSE;
        CloseHandle(foreign);
        Check(exited && code == 0 && removalCalls == callsBeforeForeign + 1 && argumentsPreserved &&
              roleCalls.load() == rolesBeforeForeign && serialReads.load() == serialsBeforeForeign &&
              diagnosticThread.load() == registeredThread && PopEvent(unknown) && unknown.originalReturned &&
              !unknown.roleAvailable && unknown.role == 0 && !unknown.beforeStampAvailable && !unknown.afterStampAvailable &&
              unknown.beforeStamp.load == 0 && unknown.afterStamp.load == 0 && unknown.beforeActor.available &&
              unknown.beforeState.cacheAvailable && unknown.unavailable && !unknown.postActorComparable,
              "foreign thread preserves native call/raw evidence, cannot steal registration or call role/serial readers");
    }
    g_role = OffRole;
    Check(!CatchRemoval() && PopEvent(unknown) && unknown.roleAvailable && unknown.role == 0 &&
          unknown.beforeStampAvailable && unknown.afterStampAvailable && unknown.lifecycleStable,
          "registered-thread Off is explicitly distinguishable from an unsampled role");
    g_role = Role;
    TestPredicates();
    Check(hookCalls == 0, "headless tests never invoke hook installation or teardown");
    return errors ? 1 : 0;
}
