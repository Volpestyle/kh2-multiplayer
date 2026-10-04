// Windows-only, headless regression for the actual trace scope implementation.
// Include the implementation in this TU to exercise its private queue/TLS/SEH
// boundaries without adding production test APIs or installing any native hooks.
#if !defined(_WIN32) || !defined(_MSC_VER)
#error NativeSpawnTraceTest requires Windows and MSVC-compatible native SEH.
#endif

#include "../inject/src/NativeSpawnController.cpp"

#include <iostream>
#include <cstdlib>
#include <memory>

namespace {
int errors = 0;
unsigned hookCalls = 0;
bool mutationHookMock = false;
uintptr_t mutationMockBase = 0, mutationEnableFailRva = 0;
void* mutationMockCtor = nullptr;
void* mutationMockInit = nullptr;
void* mutationMockTeardown = nullptr;
bool mutationRemoveFail = false;
std::atomic<unsigned> roleReads {0}, transitionReads {0}, loadReads {0};
std::uint32_t syntheticTransition = 17, syntheticLoad = 29;
constexpr DWORD kDeliberateException = 0xE0424B32;

void Check(bool passed, const char* description) {
    std::cout << (passed ? "PASS: " : "FAIL: ") << description << '\n';
    if (!passed) ++errors;
}
} // namespace

// Never link to or operate the game's lifecycle/MinHook in this executable.
namespace kh2coop::inject::warp {
std::uint32_t TransitionSerial() { ++transitionReads; return syntheticTransition; }
std::uint32_t LoadSerial() { ++loadReads; return syntheticLoad; }
} // namespace kh2coop::inject::warp

MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID, LPVOID* original) {
    ++hookCalls;
    if (mutationHookMock) {
        const auto rva = reinterpret_cast<uintptr_t>(target) - mutationMockBase;
        *original = rva == 0x3FDCE0 ? mutationMockCtor :
            rva == 0x3FF550 ? mutationMockInit : rva == 0x3FDE60 ? mutationMockTeardown : nullptr;
        return *original ? MH_OK : MH_ERROR_NOT_INITIALIZED;
    }
    return MH_ERROR_NOT_INITIALIZED;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID target) {
    ++hookCalls;
    if (mutationHookMock) return reinterpret_cast<uintptr_t>(target) - mutationMockBase == mutationEnableFailRva ?
        MH_ERROR_MEMORY_PROTECT : MH_OK;
    return MH_ERROR_NOT_INITIALIZED;
}
MH_STATUS WINAPI MH_DisableHook(LPVOID) {
    ++hookCalls;
    if (mutationHookMock) return MH_OK;
    return MH_ERROR_NOT_INITIALIZED;
}
MH_STATUS WINAPI MH_RemoveHook(LPVOID) {
    ++hookCalls;
    if (mutationHookMock) return mutationRemoveFail ? MH_ERROR_MEMORY_PROTECT : MH_OK;
    return MH_ERROR_NOT_INITIALIZED;
}

namespace {
using namespace kh2coop::inject::spawncontroller;

unsigned updateCalls = 0, wrapperCalls = 0, depthSeen = 0;
unsigned generatedCalls = 0, dispatcherCalls = 0, scriptCalls = 0;
bool faultUpdate = false, faultWrapper = false;
bool faultGenerated = false, faultDispatcher = false;
uintptr_t expectedController = 0;
const float* expectedPoint = nullptr;
bool argumentsPreserved = true;
int actorToken = 0;
const void* expectedRecord = nullptr;
const void* expectedScriptArgs = nullptr;
void* expectedRegion = nullptr;
void* generatedResult = &actorToken;
void* fixedResult = &actorToken;
bool generatedArgumentsPreserved = true, dispatcherArgumentsPreserved = true, scriptArgumentsPreserved = true;
enum class FactoryTestMode { Off, Reject, Allocate, Repeated, WrongCaller, WrongSize, Nested,
                             NestedFault, CoverageChange, Overflow };
FactoryTestMode factoryMode = FactoryTestMode::Off;
unsigned admissionOriginalCalls = 0, allocationOriginalCalls = 0;
std::uint32_t admissionArgumentBits = 0, inputWeightBits = 0x7FC12345;
std::size_t allocationArgument = 0;
std::uint8_t admissionOriginalResult = 1;
void* allocationOriginalResult = nullptr;
bool faultAdmission = false, faultAllocation = false, nestedScopeRestored = false;
bool factoryArgumentsPreserved = true;
TraceEvent nestedFactoryEvent;

std::uint8_t __fastcall SyntheticAdmission(float weight) {
    ++admissionOriginalCalls;
    std::memcpy(&admissionArgumentBits, &weight, sizeof(weight));
    if (faultAdmission) RaiseException(kDeliberateException, 0, 0, nullptr);
    return admissionOriginalResult;
}
void* __fastcall SyntheticAllocation(std::size_t size) {
    ++allocationOriginalCalls;
    allocationArgument = size;
    if (faultAllocation) RaiseException(kDeliberateException, 0, 0, nullptr);
    return allocationOriginalResult;
}

bool CatchFactoryException(TraceEvent* event) {
    __try {
        RunFactoryWrapper(event, expectedRecord, reinterpret_cast<void*>(expectedController), expectedPoint);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

void* SyntheticFactoryBody() {
    float weight = 0;
    std::memcpy(&weight, &inputWeightBits, sizeof(weight));
    if ((factoryMode == FactoryTestMode::Nested || factoryMode == FactoryTestMode::NestedFault) &&
        g_factoryDepth == 1) {
        const auto savedMode = factoryMode;
        auto* const parent = g_factoryScope;
        nestedFactoryEvent = {};
        nestedFactoryEvent.recordAvailable = true;
        nestedFactoryEvent.objectId = 0x309;
        factoryMode = FactoryTestMode::Allocate;
        if (savedMode == FactoryTestMode::NestedFault) {
            faultAdmission = true;
            nestedScopeRestored = CatchFactoryException(&nestedFactoryEvent);
            faultAdmission = false;
        } else {
            RunFactoryWrapper(&nestedFactoryEvent, expectedRecord,
                reinterpret_cast<void*>(expectedController), expectedPoint);
            nestedScopeRestored = true;
        }
        nestedScopeRestored = nestedScopeRestored && g_factoryScope == parent && g_factoryDepth == 1;
        factoryMode = savedMode;
    }
    if (factoryMode == FactoryTestMode::Overflow && g_factoryScope)
        g_factoryScope->admissionCalls = 0xFFFFU;
    const uintptr_t callerOffset = factoryMode == FactoryTestMode::WrongCaller ? 1 : 0;
    const auto admitted = ObserveAdmission(weight, g_exeBase + ADMISSION_RETURN_RVA + callerOffset);
    if (factoryMode == FactoryTestMode::Repeated)
        ObserveAdmission(weight, g_exeBase + ADMISSION_RETURN_RVA);
    if (factoryMode == FactoryTestMode::CoverageChange) ++g_factoryCoverageSerial;
    if (!admitted) return nullptr;
    return ObserveAllocation(factoryMode == FactoryTestMode::WrongSize ? 0xD48 : 0xD50,
                             g_exeBase + ALLOCATION_RETURN_RVA + callerOffset);
}

std::uint8_t OffRole() { ++roleReads; return 0; }

void SeedBufferedEvent() {
    auto& event = g_traceTick.events[g_traceTick.count++];
    event = {};
    event.sequence = updateCalls;
    event.tickSequence = g_traceTick.sequence;
    event.controller = g_traceTick.controller;
    event.actor = reinterpret_cast<uintptr_t>(&actorToken);
    event.stamp = g_traceTick.stamp;
    event.stampAvailable = true;
    event.tickBefore = g_traceTick.before;
    event.outcome = TraceOutcome::Observed;
    event.reason = "synthetic original completed creation";
    event.enclosingTick = true;
    // Poison every poststate validity field so cleanup must actively clear it.
    event.tickComplete = event.postStampAvailable = event.lifecycleStable = true;
    event.postStamp.transition = 99;
    event.postStamp.location.fill(0xCC);
    event.tickAfter.controllerAvailable = event.tickAfter.cacheAvailable = true;
    event.tickAfter.header = 99;
    event.tickAfter.flags = 99;
    event.tickAfter.cacheIds.fill(99);
}

void __fastcall SyntheticOriginal(void* controller, const float* point) {
    ++updateCalls;
    argumentsPreserved = argumentsPreserved &&
        reinterpret_cast<uintptr_t>(controller) == expectedController && point == expectedPoint;
    SeedBufferedEvent();
    if (faultUpdate) RaiseException(kDeliberateException, 0, 0, nullptr);
    auto* state = static_cast<Controller*>(controller);
    state->cooldown = 8.0f;
    state->flags = 0x18;
}

void __fastcall NestedOriginal(void*, const float*) {
    ++updateCalls;
    depthSeen = g_traceNestedDepth;
    if (faultUpdate) RaiseException(kDeliberateException, 0, 0, nullptr);
}

void* __fastcall SyntheticWrapper(const void* record, void* controller) {
    ++wrapperCalls;
    if (faultWrapper) RaiseException(kDeliberateException, 0, 0, nullptr);
    if (factoryMode != FactoryTestMode::Off) {
        factoryArgumentsPreserved = factoryArgumentsPreserved && record == expectedRecord &&
            reinterpret_cast<uintptr_t>(controller) == expectedController;
        return SyntheticFactoryBody();
    }
    return fixedResult;
}

void* __fastcall SyntheticGenerated(const void* record, void* controller, const float* point) {
    ++generatedCalls;
    generatedArgumentsPreserved = generatedArgumentsPreserved && record == expectedRecord &&
        reinterpret_cast<uintptr_t>(controller) == expectedController && point == expectedPoint;
    if (faultGenerated) RaiseException(kDeliberateException, 0, 0, nullptr);
    if (factoryMode != FactoryTestMode::Off) return SyntheticFactoryBody();
    return generatedResult;
}

constexpr std::uint64_t kDispatcherResult = 0xFEDCBA9876543210ULL;
std::uint64_t __fastcall SyntheticDispatcher(void* controller, void* region) {
    ++dispatcherCalls;
    dispatcherArgumentsPreserved = dispatcherArgumentsPreserved &&
        reinterpret_cast<uintptr_t>(controller) == expectedController && region == expectedRegion &&
        g_dispatcherScope.active && g_dispatcherScope.controller == expectedController &&
        g_dispatcherScope.region == reinterpret_cast<uintptr_t>(expectedRegion);
    ObserveWrapper(expectedRecord, controller, expectedPoint, TraceWrapper::Generated, g_exeBase + 0x3FE989);
    if (faultDispatcher) RaiseException(kDeliberateException, 0, 0, nullptr);
    return kDispatcherResult;
}

std::uint64_t __fastcall SyntheticScript(const void* args) {
    ++scriptCalls;
    scriptArgumentsPreserved = scriptArgumentsPreserved && args == expectedScriptArgs && g_scriptScope.active;
    // With the script detoured, its original runs under RunScriptScope. The
    // original tail jump therefore reaches the dispatcher with a DLL return
    // address, not the native script caller. Exercise the real hook capture:
    // this test executable is likewise outside the synthetic native image.
    return HookedDispatcher(reinterpret_cast<void*>(expectedController), expectedRegion);
}

bool foreignWasGameThread = true, foreignRegistrationRejected = false, foreignReturnPreserved = false;
DWORD WINAPI ForeignObservation(void*) {
    foreignWasGameThread = IsDiagnosticGameThread();
    RegisterDiagnosticGameThread();
    foreignRegistrationRejected = !IsDiagnosticGameThread();
    foreignReturnPreserved = ObserveWrapper(expectedRecord, reinterpret_cast<void*>(expectedController),
        expectedPoint, TraceWrapper::Generated, g_exeBase + 0x3FE989) == generatedResult;
    return 0;
}

// The outer handler is deliberately outside the production __finally/filter.
// These helper functions contain only POD locals, as required by MSVC SEH.
bool CatchTraceException(void* controller, const float* point) {
    __try {
        RunTraceScope(controller, point, g_exeBase + RETURN_RVA);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

bool CatchNestedException(void* controller, const float* point) {
    __try {
        RunNestedUpdate(controller, point, g_exeBase + RETURN_RVA);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

bool CatchWrapperException() {
    __try {
        CallOriginalWrapper(nullptr, nullptr);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

bool CatchGeneratedException() {
    __try {
        CallOriginalGenerated(expectedRecord, reinterpret_cast<void*>(expectedController), expectedPoint);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

bool CatchScriptException(const void* args) {
    __try {
        RunScriptScope(args, g_exeBase + 0x551111);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

std::array<uintptr_t, 7> geometryRegions {};
std::array<float, 4> geometryPoint {-318.9f, -1.082f, 762.986f, 1.0f};
const void* geometryObject = nullptr;
unsigned geometryOriginalCalls = 0, boxOriginalCalls = 0, geometryCopies = 0;
unsigned geometryChildren = 7, geometryFaultAt = UINT32_MAX;
unsigned geometryFinalRoles = 0, geometryFinalTransitions = 0, geometryFinalLoads = 0;
bool geometryBoundaryPreserved = true, geometryArgumentsPreserved = true;
bool geometryNested = false, geometryMutateRecord = false;
bool geometryMutateRegion = false, geometryChangeCoverage = false;
bool geometryHoldCopy = false, gateDuringCopy = false, geometryUseGate = false, nestedUpdateDuringCopy = false;
unsigned geometryHoldCopyAt = 0;
bool phaseChangeCoverage = false, phaseChangeToken = false, phaseFiberProbe = false;
void* phaseHomeFiber = nullptr;
void* phaseChildFiber = nullptr;
OriginalPhaseFrame phaseFrameSeen;
uintptr_t phaseControllerSeen = 0;
const float* phasePointSeen = nullptr;
std::array<std::uint8_t, 3> phaseRoles {2, 2, 2};
unsigned phaseRoleIndex = 0, phaseCaptureCalls = 0;
bool gateFault = false, gateNested = false, gateChangeCoverage = false, gateOverflow = false;
bool gateWrongCaller = false, gateResultsPreserved = true;
unsigned gateCallsPerUpdate = 1, gateOriginalCalls = 0;
std::uint8_t gateResult = 0;
std::uint8_t geometryResult = 0;
std::uint8_t __fastcall SyntheticEventGate() {
    ++gateOriginalCalls;
    if (gateNested) { gateNested = false; ObserveEventGate(g_exeBase + EVENT_GATE_RETURN_RVA); }
    if (gateChangeCoverage) SaturatingIncrement(g_eventGateCoverage);
    if (phaseChangeCoverage) SaturatingIncrement(g_originalPhaseCoverage);
    if (phaseChangeToken) ++g_originalPhaseFrame.invocation;
    if (gateFault) RaiseException(kDeliberateException, 0, 0, nullptr);
    return gateResult;
}
std::uint8_t ClientRole() { ++roleReads; return 2; }
std::uint8_t PhaseRole() {
    ++roleReads;
    const auto index = phaseRoleIndex < phaseRoles.size() ? phaseRoleIndex : phaseRoles.size() - 1;
    ++phaseRoleIndex;
    return phaseRoles[index];
}
void PhaseCapture(const float*) { ++phaseCaptureCalls; }
void __fastcall PhasePassOriginal(void* controller, const float* point) {
    ++geometryOriginalCalls;
    phaseFrameSeen = g_originalPhaseFrame;
    phaseControllerSeen = reinterpret_cast<uintptr_t>(controller);
    phasePointSeen = point;
}
void WINAPI PhaseOtherFiber(void*) {
    gateResultsPreserved = ObserveEventGate(g_exeBase + EVENT_GATE_RETURN_RVA) == gateResult && gateResultsPreserved;
    SwitchToFiber(phaseHomeFiber);
}
bool CatchObservedUpdateException(void* controller, const float* point) {
    __try {
        RunObservedUpdateEntry(controller, point, g_exeBase + RETURN_RVA);
    } __except (GetExceptionCode() == kDeliberateException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}
const void* __fastcall GeometryLookup(std::int32_t id) { return id == 302 ? geometryObject : nullptr; }
bool GeometryCopy(float* out, uintptr_t, std::uint64_t) {
    ++geometryCopies;
    if (gateDuringCopy) ObserveEventGate(g_exeBase + EVENT_GATE_RETURN_RVA);
    if (nestedUpdateDuringCopy) {
        nestedUpdateDuringCopy = false;
        HookedUpdate(reinterpret_cast<void*>(g_traceTick.controller), geometryPoint.data());
    }
    std::memcpy(out, geometryPoint.data(), 16);
    geometryFinalRoles = roleReads.load(); geometryFinalTransitions = transitionReads.load(); geometryFinalLoads = loadReads.load();
    return !geometryHoldCopy && geometryCopies != geometryHoldCopyAt;
}
std::uint8_t __fastcall GeometryBox(void* region, const float* point) {
    const unsigned call = boxOriginalCalls++;
    geometryArgumentsPreserved = geometryArgumentsPreserved && region != nullptr &&
        std::memcmp(point, geometryPoint.data(), 16) == 0;
    if (call == geometryFaultAt) RaiseException(kDeliberateException, 0, 0, nullptr);
    return geometryResult;
}
void __fastcall GeometryOriginal(void* controller, const float* point) {
    ++geometryOriginalCalls;
    phaseFrameSeen = g_originalPhaseFrame;
    phaseControllerSeen = reinterpret_cast<uintptr_t>(controller);
    phasePointSeen = point;
    geometryBoundaryPreserved = geometryBoundaryPreserved && roleReads.load() == geometryFinalRoles &&
        transitionReads.load() == geometryFinalTransitions + 1 && loadReads.load() == geometryFinalLoads + 1;
    if (phaseFiberProbe) SwitchToFiber(phaseChildFiber);
    if (geometryUseGate) {
        if (gateOverflow && g_geometryScope) g_geometryScope->geometry.eventGate.calls = UINT32_MAX;
        for (unsigned i = 0; i < gateCallsPerUpdate; ++i)
            gateResultsPreserved = ObserveEventGate(g_exeBase + EVENT_GATE_RETURN_RVA + (gateWrongCaller ? 1 : 0)) == gateResult && gateResultsPreserved;
        if (gateResult) return; // native nonzero suppresses cooldown/BOX work
    }
    for (unsigned i = 0; i < geometryChildren; ++i) {
        const auto result = ObserveBox(reinterpret_cast<void*>(geometryRegions[i % 7]), point, g_exeBase + BOX_RETURN_RVA);
        geometryArgumentsPreserved = geometryArgumentsPreserved && result == geometryResult;
    }
    if (geometryNested) {
        geometryNested = false;
        RunNestedUpdate(controller, point, g_exeBase + RETURN_RVA);
    }
    if (geometryMutateRecord) {
        auto* native = static_cast<Controller*>(controller);
        *reinterpret_cast<std::uint8_t*>(native->spawnArray + 0x30) ^= 1;
    }
    if (geometryMutateRegion) *reinterpret_cast<std::uint8_t*>(geometryRegions[0] + 0x48) ^= 1;
    if (geometryChangeCoverage) ++g_geometryCoverage;
}
DWORD WINAPI ForeignGeometry(void*) {
    // Even a parked TLS event on a foreign thread cannot enroll predicate data.
    TraceEvent event;
    g_geometryScope = &event;
    ObserveBox(reinterpret_cast<void*>(geometryRegions[0]), geometryPoint.data(), g_exeBase + BOX_RETURN_RVA);
    const bool untouched = event.geometry.calls == 0;
    g_geometryScope = nullptr;
    return untouched ? 0 : 1;
}

DWORD WINAPI ForeignEventGate(void*) {
    TraceEvent event;
    event.geometry.eventGate.available = true;
    g_geometryScope = &event;
    const auto result = ObserveEventGate(g_exeBase + EVENT_GATE_RETURN_RVA);
    const bool untouched = event.geometry.eventGate.calls == 0 && result == gateResult;
    g_geometryScope = nullptr;
    return untouched ? 0 : 1;
}

void GeometryControls(std::uint8_t* image) {
    auto put = [](void* address, const auto& value) { std::memcpy(address, &value, sizeof(value)); };
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY", "1");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOAD", "29");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_TRANSITION", "17");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOCATION", "5,6,0,1,1,0");
    Check(!ReadGeometryConfig(false).requested, "geometry cannot enable without existing trace opt-in");
    Check(ReadGeometryConfig(true).configured, "geometry parses explicit full tuple and target native load/transition");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOAD", "4294967296");
    Check(!ReadGeometryConfig(true).configured, "geometry rejects overflowing load");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOAD", "29");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOCATION", "5,6,0,1,1,0 trailing");
    Check(!ReadGeometryConfig(true).configured, "geometry rejects noncanonical trailing tuple input");
    SetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOCATION", "5,6,0,1,1,0");
    const auto config = ReadGeometryConfig(true);
    for (const auto* name : {"KH2COOP_SPAWN_GEOMETRY", "KH2COOP_SPAWN_GEOMETRY_LOAD", "KH2COOP_SPAWN_GEOMETRY_TRANSITION", "KH2COOP_SPAWN_GEOMETRY_LOCATION"}) SetEnvironmentVariableA(name, nullptr);
    Check(!ReadGeometryConfig(true).requested, "geometry is default OFF without process-local opt-in");
    std::memcpy(image + BOX_RVA, kBoxBytes, sizeof(kBoxBytes));
    std::memcpy(image + 0x3FF134, kBoxCallBytes, sizeof(kBoxCallBytes));
    Check(Matches(g_exeBase + BOX_RVA, kBoxBytes) && Matches(g_exeBase + 0x3FF134, kBoxCallBytes),
          "owned native image accepts exact full-instruction BOX and exact native caller gates");
    image[BOX_RVA + 17] ^= 1;
    Check(!Matches(g_exeBase + BOX_RVA, kBoxBytes), "one-byte BOX gate mismatch is unavailable");

    auto* controller = reinterpret_cast<Controller*>(image + 0x13000);
    auto* definition = image + 0x12000;
    *controller = {};
    controller->key = 808476514; controller->header = reinterpret_cast<uintptr_t>(definition);
    controller->spawnArray = controller->header + 0x2C; controller->regionArray = controller->spawnArray + 320;
    definition[0] = 2; put(definition + 2, std::uint16_t {30}); put(definition + 4, std::uint16_t {5}); put(definition + 6, std::uint16_t {7});
    for (unsigned i = 0; i < 5; ++i) {
        auto* record = definition + 0x2C + i * 64;
        put(record, std::uint32_t {302}); record[0x1C] = 2; record[0x1D] = 0;
        put(record + 0x1E, static_cast<std::uint16_t>(11 + i));
    }
    for (unsigned i = 0; i < 7; ++i) {
        geometryRegions[i] = reinterpret_cast<uintptr_t>(image + 0x10000 + i * 0x80);
        auto* region = reinterpret_cast<std::uint8_t*>(geometryRegions[i]);
        put(region, g_exeBase + BOX_VTABLE_RVA);
        put(region + 0x68, controller->regionArray + i * 64);
        if (i != 6) {
            const auto next = reinterpret_cast<uintptr_t>(image + 0x10000 + (i + 1) * 0x80);
            put(image + HANDLE_REGIONS_RVA, next & ~uintptr_t {0x1FFFFFF});
            put(region + 0x58, static_cast<std::uint32_t>(next & 0x1FFFFFF));
        }
    }
    controller->regionHead = geometryRegions.front(); controller->regionTail = geometryRegions.back();
    put(image + CONTROLLER_COUNT, std::int32_t {1});
    const TableEntry table {controller->key, 0, reinterpret_cast<uintptr_t>(controller)};
    put(image + CONTROLLER_TABLE, table);
    put(image + OBJECT_TABLES, reinterpret_cast<uintptr_t>(image + 0x14000));
    put(image + 0x14004, std::int32_t {1});
    put(image + 0x14008, std::uint32_t {302}); image[0x1400C] = 4; image[0x14010] = 'M';
    geometryObject = image + 0x14008;
    std::memcpy(image + kh2coop::offsets::NOW, config.location.data(), 10);
    g_role = &ClientRole; g_copy = &GeometryCopy; g_lookup = &GeometryLookup;
    g_original = &GeometryOriginal; g_originalBox = &GeometryBox;
    g_originalEventGate = &SyntheticEventGate;
    auto reset = [&] {
        TraceEvent discard;
        while (PopTraceEvent(discard)) {}
        g_geometryConfig = config; g_geometryInstalled = true; g_geometryVerified = true;
        g_geometryCoverage = 1; g_geometryPrior = false; g_geometryTicks = 0;
        g_geometryTerminal = GeometryTerminal::Waiting; g_geometryStarted = 0;
        geometryOriginalCalls = boxOriginalCalls = geometryCopies = 0;
        geometryChildren = 7; geometryFaultAt = UINT32_MAX; geometryResult = 0;
        geometryNested = geometryMutateRecord = geometryMutateRegion = geometryChangeCoverage = false;
        geometryArgumentsPreserved = geometryBoundaryPreserved = true;
        geometryHoldCopy = gateDuringCopy = geometryUseGate = nestedUpdateDuringCopy = false;
        geometryHoldCopyAt = 0;
        phaseChangeCoverage = phaseChangeToken = phaseFiberProbe = false;
        phaseFrameSeen = {}; phasePointSeen = nullptr; phaseControllerSeen = 0;
        g_originalPhaseRequested = false; g_originalPhaseConfigured = false; g_originalPhaseCoverage = 0;
        g_originalPhaseFrame = {}; g_originalPhaseSerial = 0;
        gateFault = gateNested = gateChangeCoverage = gateOverflow = gateWrongCaller = false;
        gateResultsPreserved = true; gateCallsPerUpdate = 1; gateOriginalCalls = 0; gateResult = 0;
        g_enrollmentRequested = false; g_enrollmentConfigured = false;
        g_eventGateInstalled = false; g_eventGateVerified = false; g_eventGateFailed = false;
        g_eventGateCoverage = 0; g_eventGateDepth = 0;
    };
    auto prior = [&] {
        Stamp stamp; stamp.location = config.location; stamp.load = 28; stamp.transition = 16;
        ObserveGeometryLifecycle(stamp, GetTickCount64(), 2);
    };
    auto run = [&] { RunTraceScope(controller, geometryPoint.data(), g_exeBase + RETURN_RVA); };
    TraceEvent event;
    reset(); run();
    Check(g_geometryTicks == 0 && !PopTraceEvent(event) && geometryOriginalCalls == 1,
          "target load alone does not arm without earlier observed lifecycle; original executes");
    Stamp target; target.load = config.load; target.transition = config.transition; target.location = config.location;
    prior(); ObserveGeometryLifecycle(target, GetTickCount64(), 1);
    Check(g_geometryTerminal == GeometryTerminal::Waiting && g_geometryTicks == 0,
          "matching host lifecycle cannot arm client-only geometry collection");
    target.location[1] = 7; ObserveGeometryLifecycle(target, GetTickCount64(), 2);
    Check(g_geometryTerminal == GeometryTerminal::Waiting, "wrong full native room tuple cannot arm collection");
    prior(); run();
    Check(PopTraceEvent(event) && event.kind == TraceKind::Geometry && event.geometry.complete && event.geometry.calls == 7 &&
          event.geometry.tableCount == 1 && event.geometry.records[0] == 0x2E && event.geometry.tableEntry[0] != 0 &&
          event.geometry.definitionStable && geometryArgumentsPreserved && geometryBoundaryPreserved && !g_geometryScope,
          "actual qualified no-wrapper tick retains full rooted definitions and seven native predicate results");
    Check(geometryCopies == 4 && boxOriginalCalls == 14 && geometryOriginalCalls == 2,
          "geometry preserves two production lease copies and exact-once native update/predicate calls");
    reset(); prior(); geometryChildren = 0; run();
    Check(PopTraceEvent(event) && event.geometry.complete && event.geometry.calls == 0,
          "zero-predicate original-return tick is retained without claiming exhaustive rejection");
    reset(); prior(); geometryChildren = 1; geometryResult = 0x80; run();
    Check(PopTraceEvent(event) && event.geometry.complete && event.geometry.predicates[0].result == 0x80 && geometryArgumentsPreserved,
          "predicate AL is preserved byte-for-byte including noncanonical true");
    reset(); prior(); geometryChildren = 8; run();
    Check(PopTraceEvent(event) && !event.geometry.complete && event.geometry.overflow && boxOriginalCalls == 8,
          "predicate budget overflow reports incomplete while every original still executes");
    reset(); prior(); geometryMutateRecord = true; run();
    Check(PopTraceEvent(event) && !event.geometry.complete && !event.geometry.definitionStable,
          "record mutation invalidates definition instead of certifying stale bytes");
    definition[0x2C + 0x30] = 0;
    reset(); prior(); geometryMutateRegion = true; run();
    Check(PopTraceEvent(event) && !event.geometry.complete && !event.geometry.definitionStable,
          "native region mutation invalidates original definition rather than reusing stale geometry");
    *reinterpret_cast<std::uint8_t*>(geometryRegions[0] + 0x48) ^= 1;
    reset(); prior(); geometryChangeCoverage = true; run();
    Check(PopTraceEvent(event) && !event.geometry.complete && event.geometry.coverageSerial != g_geometryCoverage,
          "coverage serial change prevents complete geometry result");
    reset(); prior(); put(image + CONTROLLER_TABLE + 16, table); put(image + CONTROLLER_COUNT, std::int32_t {2}); run();
    Check(PopTraceEvent(event) && !event.geometry.definitionAvailable && !event.geometry.complete && geometryOriginalCalls == 1,
          "duplicate ordinary table identity invalidates diagnostic only, preserving production call");
    put(image + CONTROLLER_COUNT, std::int32_t {1});
    reset(); prior(); put(reinterpret_cast<void*>(geometryRegions.back() + 0x58), std::uint32_t {0x80000000}); run();
    Check(PopTraceEvent(event) && !event.geometry.definitionAvailable && !event.geometry.complete,
          "nonzero encoded handle with zero masked offset is not invented terminal null");
    put(reinterpret_cast<void*>(geometryRegions.back() + 0x58), std::uint32_t {0});
    reset(); prior(); geometryNested = true; run();
    Check(PopTraceEvent(event) && event.geometry.nested && !event.geometry.complete && event.geometry.calls == 7 && geometryOriginalCalls == 2 &&
          !g_geometryScope && !g_geometryPredicateDepth && !g_traceNestedDepth,
          "nested update hides parent geometry attribution and restores TLS");
    reset(); prior(); geometryFaultAt = 1;
    Check(CatchTraceException(controller, geometryPoint.data()) && PopTraceEvent(event) && event.geometry.unwound &&
          event.geometry.predicates[1].unwound && !event.geometry.complete && !event.postStampAvailable &&
          !g_geometryScope && !g_geometryPredicateDepth && !g_traceTick.active,
          "native predicate SEH propagates and publishes incomplete POD while clearing TLS");
    reset(); prior();
    for (unsigned i = 0; i < 64; ++i) { run(); Check(PopTraceEvent(event), "selected bounded geometry tick drains"); }
    run();
    Check(g_geometryTicks == 64 && g_geometryTerminal == GeometryTerminal::TickLimit && !PopTraceEvent(event) && geometryOriginalCalls == 65,
          "64-tick terminal stops diagnostics while native update continues");
    reset(); prior(); run(); while (PopTraceEvent(event)) {}
    GeometryDeadline(g_geometryStarted.load() + 2000); run();
    Check(g_geometryTerminal == GeometryTerminal::Deadline && g_geometryTicks == 1 && !PopTraceEvent(event) && geometryOriginalCalls == 2,
          "hard 2000ms deadline stops selected capture without stopping originals");
    reset(); prior(); run(); while (PopTraceEvent(event)) {}
    Stamp different; different.location = config.location; different.load = 30; different.transition = 18;
    ObserveGeometryLifecycle(different, GetTickCount64(), 2); run();
    Check(g_geometryTerminal == GeometryTerminal::LifecycleChanged && g_geometryTicks == 1 && !PopTraceEvent(event),
          "later native lifecycle permanently closes configured collection");
    reset(); prior();
    TraceEvent filler;
    for (unsigned i = 0; i < TRACE_QUEUE_CAP; ++i) PublishTrace(filler);
    const auto lost = g_geometryDropped.load(); run();
    Check(g_geometryDropped.load() == lost + 1 && geometryOriginalCalls == 1 && !g_geometryScope,
          "full queue counts geometry loss and never blocks or changes native execution");
    while (PopTraceEvent(event)) {}
    reset();
    const auto foreign = g_geometryForeign.load();
    HANDLE thread = CreateThread(nullptr, 0, &ForeignGeometry, nullptr, 0, nullptr);
    Check(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "foreign geometry control completes");
    if (thread) { DWORD result = 1; GetExitCodeThread(thread, &result); CloseHandle(thread);
        Check(result == 0 && g_geometryForeign == foreign + 1 && boxOriginalCalls == 1,
              "foreign predicate preserves original but cannot enroll native geometry data"); }

    // Extend the stabilized actual-source geometry harness; the event gate has
    // independent opt-in/coverage and does not change prior completeness enums.
    reset();
    SetEnvironmentVariableA("KH2COOP_SPAWN_ENROLL_OBSERVE", nullptr);
    const auto initialHookCalls = hookCalls;
    InstallEventGateObserver(true);
    Check(!g_enrollmentRequested && !g_enrollmentConfigured && !g_eventGateInstalled && hookCalls == initialHookCalls,
          "event gate defaults OFF even with configured geometry");
    SetEnvironmentVariableA("KH2COOP_SPAWN_ENROLL_OBSERVE", "true");
    Check(!EnrollmentRequested(), "event gate requires exact explicit 1 opt-in");
    SetEnvironmentVariableA("KH2COOP_SPAWN_ENROLL_OBSERVE", "1");
    InstallEventGateObserver(false);
    Check(g_enrollmentRequested && !g_enrollmentConfigured && hookCalls == initialHookCalls,
          "event gate opt-in cannot install without existing trace opt-in");
    g_geometryConfig.configured = false; InstallEventGateObserver(true);
    Check(!g_enrollmentConfigured && hookCalls == initialHookCalls,
          "event gate opt-in cannot install without valid geometry configuration");
    g_geometryConfig = config;
    std::memcpy(image + EVENT_GATE_RVA, kEventGateBytes, sizeof(kEventGateBytes));
    std::memcpy(image + EVENT_GATE_CALL_RVA, kEventGateCallBytes, sizeof(kEventGateCallBytes));
    bool allBytesGuarded = true;
    for (unsigned i = 0; i < sizeof(kEventGateBytes); ++i) {
        image[EVENT_GATE_RVA + i] ^= 1;
        allBytesGuarded = allBytesGuarded && !Matches(g_exeBase + EVENT_GATE_RVA, kEventGateBytes);
        image[EVENT_GATE_RVA + i] ^= 1;
    }
    for (unsigned i = 0; i < sizeof(kEventGateCallBytes); ++i) {
        image[EVENT_GATE_CALL_RVA + i] ^= 1;
        allBytesGuarded = allBytesGuarded && !Matches(g_exeBase + EVENT_GATE_CALL_RVA, kEventGateCallBytes);
        image[EVENT_GATE_CALL_RVA + i] ^= 1;
    }
    Check(allBytesGuarded && Matches(g_exeBase + EVENT_GATE_RVA, kEventGateBytes) &&
          EVENT_GATE_RVA + 10 + Field<std::uint32_t>(kEventGateBytes, 6) == 0x2A11400,
          "full 65-byte predicate and 9-byte callsite gate guard every byte including RIP-relative load target");
    image[EVENT_GATE_CALL_RVA + 8] ^= 1; InstallEventGateObserver(true);
    Check(!g_eventGateVerified && g_eventGateFailed && hookCalls == initialHookCalls && g_geometryInstalled,
          "event gate callsite mismatch stays explicit without disabling BOX");
    image[EVENT_GATE_CALL_RVA + 8] ^= 1; InstallEventGateObserver(true);
    Check(g_eventGateVerified && !g_eventGateInstalled && g_eventGateFailed && !g_originalEventGate &&
          hookCalls == initialHookCalls + 1 && g_geometryInstalled,
          "verified event entry with MinHook create failure remains independent unavailable coverage");
    SetEnvironmentVariableA("KH2COOP_SPAWN_ENROLL_OBSERVE", nullptr);
    g_originalEventGate = &SyntheticEventGate;
    auto armGate = [&] {
        reset(); prior(); geometryUseGate = true;
        g_enrollmentRequested = true; g_enrollmentConfigured = true;
        g_eventGateVerified = true; g_eventGateInstalled = true; g_eventGateCoverage = 1;
    };
    reset(); prior(); geometryUseGate = true; run();
    Check(PopTraceEvent(event) && event.geometry.complete && !event.geometry.eventGate.requested && !event.geometry.eventGate.available &&
          !event.geometry.eventGate.calls && gateOriginalCalls == 1,
          "disabled event witness preserves original and existing complete geometry semantics");
    armGate(); g_eventGateInstalled = false; g_eventGateFailed = true; run();
    Check(PopTraceEvent(event) && event.geometry.complete && event.geometry.eventGate.requested &&
          !event.geometry.eventGate.available && !event.geometry.eventGate.complete && !event.geometry.eventGate.returned &&
          gateOriginalCalls == 1 && geometryOriginalCalls == 1,
          "requested unavailable gate retains explicit per-tick receipt with unchanged BOX and original update");
    for (const auto result : {std::uint8_t {0}, std::uint8_t {1}, std::uint8_t {0x80}}) {
        armGate(); gateResult = result; run();
        Check(PopTraceEvent(event) && event.geometry.eventGate.available && event.geometry.eventGate.complete &&
              event.geometry.eventGate.calls == 1 && event.geometry.eventGate.returned && event.geometry.eventGate.result == result &&
              gateOriginalCalls == 1 && geometryOriginalCalls == 1 && gateResultsPreserved && geometryBoundaryPreserved &&
              event.geometry.complete && event.geometry.calls == (result ? 0U : 7U),
              "actual original gate AL0/AL1/nonboolean is returned once unchanged with independent complete witness");
    }
    armGate(); gateCallsPerUpdate = 0; run();
    Check(PopTraceEvent(event) && event.geometry.complete && event.geometry.eventGate.available &&
          !event.geometry.eventGate.complete && !event.geometry.eventGate.calls && !event.geometry.eventGate.returned,
          "missing actual gate is unavailable as a witness even on complete normal geometry return");
    armGate(); gateCallsPerUpdate = 2; run();
    Check(PopTraceEvent(event) && event.geometry.complete && event.geometry.eventGate.calls == 2 &&
          event.geometry.eventGate.returned && !event.geometry.eventGate.complete && gateOriginalCalls == 2,
          "repeated gates execute unchanged but cannot claim an unambiguous single witness");
    armGate(); gateDuringCopy = true; run();
    Check(PopTraceEvent(event) && !event.geometry.eventGate.complete && event.geometry.eventGate.calls == 3 &&
          gateOriginalCalls == 3 && geometryBoundaryPreserved,
          "extra callback predicate calls remain ambiguous; callwise receipt does not invent phase authority");
    armGate(); nestedUpdateDuringCopy = true; run();
    Check(PopTraceEvent(event) && event.geometry.eventGate.nested && !event.geometry.eventGate.complete &&
          event.geometry.eventGate.calls == 1 && gateOriginalCalls == 2 && geometryOriginalCalls == 2 && !g_traceNestedDepth,
          "reentrant hooked update during lease callback permanently contaminates the parent gate receipt");
    armGate(); geometryHoldCopy = true; run();
    Check(PopTraceEvent(event) && !event.geometry.originalReturned && !event.geometry.eventGate.complete &&
          !event.geometry.eventGate.calls && !gateOriginalCalls && !geometryOriginalCalls,
          "held lease does not execute original or manufacture a gate return");
    armGate(); gateWrongCaller = true; run();
    Check(PopTraceEvent(event) && !event.geometry.eventGate.calls && !event.geometry.eventGate.complete && gateOriginalCalls == 1,
          "foreign callsite passes through without satisfying selected original gate receipt");
    armGate(); gateNested = true; run();
    Check(PopTraceEvent(event) && event.geometry.eventGate.nested && !event.geometry.eventGate.complete &&
          event.geometry.eventGate.calls == 1 && gateOriginalCalls == 2 && !g_eventGateDepth,
          "nested predicate preserves both originals while invalidating ambiguity and restoring depth");
    armGate(); geometryNested = true; run();
    Check(PopTraceEvent(event) && event.geometry.eventGate.nested && !event.geometry.eventGate.complete &&
          event.geometry.eventGate.calls == 1 && gateOriginalCalls == 2 && !g_traceNestedDepth,
          "nested original hides parent gate attribution and restores gate TLS");
    armGate(); gateChangeCoverage = true; run();
    Check(PopTraceEvent(event) && event.geometry.complete && !event.geometry.eventGate.complete &&
          event.geometry.eventGate.coverageSerial != g_eventGateCoverage,
          "gate coverage change invalidates only its own witness");
    armGate(); gateOverflow = true; run();
    Check(PopTraceEvent(event) && event.geometry.eventGate.calls == UINT32_MAX && event.geometry.eventGate.overflow &&
          !event.geometry.eventGate.complete && gateOriginalCalls == 1,
          "gate counter saturates and overflow preserves original pass-through");
    armGate(); gateFault = true;
    Check(CatchTraceException(controller, geometryPoint.data()) && PopTraceEvent(event) && event.geometry.unwound &&
          event.geometry.eventGate.unwound && !event.geometry.eventGate.returned && !event.geometry.eventGate.complete &&
          !event.postStampAvailable && !g_eventGateDepth && !g_geometryScope && !g_traceTick.active,
          "gate native SEH propagates with explicit partial receipt and cleared scope/depth TLS");
    armGate(); gateNested = true; gateFault = true;
    Check(CatchTraceException(controller, geometryPoint.data()) && PopTraceEvent(event) &&
          event.geometry.eventGate.nested && event.geometry.eventGate.unwound && !event.geometry.eventGate.complete &&
          gateOriginalCalls == 2 && !g_eventGateDepth && !g_geometryScope && !g_traceTick.active,
          "nested gate SEH preserves exception and clears TLS without claiming parent completion");
    armGate(); geometryFaultAt = 0;
    Check(CatchTraceException(controller, geometryPoint.data()) && PopTraceEvent(event) && event.geometry.unwound &&
          event.geometry.eventGate.returned && !event.geometry.eventGate.unwound && !event.geometry.eventGate.complete &&
          !g_eventGateDepth,
          "gate return followed by later native fault stays partial rather than complete");
    armGate();
    for (unsigned i = 0; i < TRACE_QUEUE_CAP; ++i) PublishTrace(filler);
    const auto gateDrops = g_eventGateDropped.load(); run();
    Check(g_eventGateDropped == gateDrops + 1 && gateOriginalCalls == 1,
          "queue pressure counts lost gate witness separately without changing original execution");
    while (PopTraceEvent(event)) {}
    armGate();
    bool boundedGates = true;
    for (unsigned i = 0; i < 64; ++i) {
        run(); boundedGates = PopTraceEvent(event) && event.geometry.eventGate.complete && boundedGates;
    }
    run();
    Check(boundedGates && g_geometryTicks == 64 && g_geometryTerminal == GeometryTerminal::TickLimit &&
          !PopTraceEvent(event) && gateOriginalCalls == 65,
          "event observer shares exact existing 64-tick budget while every later native gate still executes");
    armGate(); run(); while (PopTraceEvent(event)) {}
    GeometryDeadline(g_geometryStarted.load() + 2000); run();
    Check(g_geometryTicks == 1 && g_geometryTerminal == GeometryTerminal::Deadline && !PopTraceEvent(event) && gateOriginalCalls == 2,
          "event observer shares existing deadline without changing native gate execution");
    armGate();
    const auto foreignGates = g_eventGateForeign.load();
    const auto gateRoles = roleReads.load(), gateTransitions = transitionReads.load(), gateLoads = loadReads.load();
    thread = CreateThread(nullptr, 0, &ForeignEventGate, nullptr, 0, nullptr);
    Check(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0, "foreign gate control completes");
    if (thread) { DWORD result = 1; GetExitCodeThread(thread, &result); CloseHandle(thread);
        Check(result == 0 && gateOriginalCalls == 1 && g_eventGateForeign == foreignGates + 1 &&
              roleReads == gateRoles && transitionReads == gateTransitions && loadReads == gateLoads,
              "foreign thread with parked TLS cannot enroll gate witness or query game-owned callbacks"); }
    g_eventGateForeign = UINT64_MAX;
    Check(HookedEventGate() == gateResult && g_eventGateForeign == UINT64_MAX && gateOriginalCalls == 2,
          "unmatched real detour entry preserves AL and saturates foreign count");

    // Exercise the actual opt-in owner, seven in-place original call sites and
    // gate boundary. No second phase algorithm, game process or installed hook.
    armGate();
    SetEnvironmentVariableA("KH2COOP_SPAWN_ORIGINAL_PHASE_OBSERVE", nullptr);
    ConfigureOriginalPhase(true);
    Check(!g_originalPhaseRequested && !g_originalPhaseConfigured,
          "original phase observer is independently default OFF");
    SetEnvironmentVariableA("KH2COOP_SPAWN_ORIGINAL_PHASE_OBSERVE", "true");
    Check(!OriginalPhaseRequested(), "original phase observer requires exact 1 opt-in");
    SetEnvironmentVariableA("KH2COOP_SPAWN_ORIGINAL_PHASE_OBSERVE", "1");
    ConfigureOriginalPhase(false);
    Check(g_originalPhaseRequested && !g_originalPhaseConfigured, "phase cannot configure without trace");
    g_eventGateInstalled = false; ConfigureOriginalPhase(true);
    Check(!g_originalPhaseConfigured, "phase cannot configure without installed event gate");
    g_eventGateInstalled = true; g_geometryInstalled = false; ConfigureOriginalPhase(true);
    Check(!g_originalPhaseConfigured, "phase cannot configure without installed geometry observer");
    g_geometryInstalled = true; ConfigureOriginalPhase(true);
    Check(g_originalPhaseConfigured && hookCalls == initialHookCalls + 1, "phase configuration adds no native hooks");
    SetEnvironmentVariableA("KH2COOP_SPAWN_ORIGINAL_PHASE_OBSERVE", nullptr);
    auto armPhase = [&] {
        armGate();
        g_originalPhaseRequested = true; g_originalPhaseConfigured = true; g_originalPhaseCoverage = 1;
        g_role = &ClientRole; g_original = &GeometryOriginal; g_capture = &PhaseCapture;
        phaseRoleIndex = phaseCaptureCalls = 0;
    };
    auto runPhase = [&] { RunObservedUpdateEntry(controller, geometryPoint.data(), g_exeBase + RETURN_RVA); };
    for (const auto result : {std::uint8_t {0}, std::uint8_t {1}, std::uint8_t {0xA5}}) {
        armPhase(); gateResult = result; runPhase();
        Check(PopTraceEvent(event) && event.geometry.eventGate.complete && event.geometry.eventGate.result == result &&
              event.geometry.originalPhase.threadPhaseConsistent && event.geometry.originalPhase.originalEntries == 1 &&
              event.geometry.originalPhase.originalReturns == 1 && event.geometry.originalPhase.originalUnwinds == 0 &&
              event.geometry.originalPhase.source == OriginalCallSource::ClientLeaseApply &&
              event.geometry.originalPhase.gateEntry.caller == g_exeBase + EVENT_GATE_RETURN_RVA &&
              event.geometry.originalPhase.gateReturn.callerRva == EVENT_GATE_RETURN_RVA &&
              event.geometry.originalPhase.updateCaller == g_exeBase + RETURN_RVA &&
              event.geometry.originalPhase.updateCallerRvaAvailable &&
              !event.geometry.originalPhase.fiberContinuityProven && !event.geometry.originalPhase.originalPhaseEligibility &&
              !g_originalPhaseFrame.available && geometryOriginalCalls == 1 && gateOriginalCalls == 1 &&
              geometryCopies == 2 && geometryBoundaryPreserved && gateResultsPreserved &&
              phaseFrameSeen.phase == UpdatePhase::InsideOriginalCall && phasePointSeen != geometryPoint.data() &&
              phaseControllerSeen == reinterpret_cast<uintptr_t>(controller),
              "phase observes actual client original boundary/arguments and exact AL0/AL1/nonboolean without authority");
    }
    armPhase(); geometryHoldCopy = true; gateDuringCopy = true; runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.calls == 1 && event.geometry.eventGate.returned &&
          event.geometry.originalPhase.gateEntry.phase == UpdatePhase::RunUpdateWork &&
          event.geometry.originalPhase.gateReturn.phase == UpdatePhase::RunUpdateWork &&
          !event.geometry.originalPhase.threadPhaseConsistent && !event.geometry.originalPhase.originalEntries &&
          event.geometry.originalPhase.hold == OriginalHold::FirstLeaseOrLifecycle && !geometryOriginalCalls,
          "gate from actual lease callback is recorded as work phase and cannot become original-phase witness");
    for (const unsigned heldCopy : {1U, 2U}) {
        armPhase(); geometryHoldCopyAt = heldCopy; runPhase();
        Check(PopTraceEvent(event) && !geometryOriginalCalls && !gateOriginalCalls && !event.geometry.eventGate.returned &&
              !event.geometry.originalPhase.originalEntries && !event.geometry.originalPhase.originalReturns &&
              !event.geometry.originalPhase.threadPhaseConsistent &&
              event.geometry.originalPhase.hold == (heldCopy == 1 ? OriginalHold::FirstLeaseOrLifecycle : OriginalHold::FinalLeaseOrLifecycle) &&
              !g_originalPhaseFrame.available,
              "first/final lease holds restore token and retain no original or observed AL return");
    }
    auto passThrough = [&](OriginalCallSource source, std::array<std::uint8_t, 3> roles, uintptr_t caller, bool unqualified = false) {
        armPhase(); phaseRoles = roles; g_role = &PhaseRole; g_original = &PhasePassOriginal;
        if (unqualified) definition[0] = 1;
        RunObservedUpdateEntry(controller, geometryPoint.data(), caller);
        Check(geometryOriginalCalls == 1 && phaseFrameSeen.source == source &&
              phaseFrameSeen.phase == UpdatePhase::InsideOriginalCall && phasePointSeen == geometryPoint.data() &&
              phaseControllerSeen == reinterpret_cast<uintptr_t>(controller) && !g_originalPhaseFrame.available,
              "each production pass-through branch preserves original native argument once with exact source tag");
        if (source == OriginalCallSource::RoleChangedAfterFirstCopy)
            Check(PopTraceEvent(event) && event.geometry.originalPhase.originalEntries == 1 &&
                  event.geometry.originalPhase.originalReturns == 1 && !event.geometry.originalPhase.threadPhaseConsistent &&
                  !event.geometry.originalReturned && geometryCopies == 1,
                  "selected role-change fallback does not upgrade legacy client-apply originalReturned");
        if (source == OriginalCallSource::HostPassThrough)
            Check(phaseCaptureCalls == 1, "host capture callback count is unchanged");
        definition[0] = 2;
    };
    passThrough(OriginalCallSource::RoleOffPassThrough, {0,0,0}, g_exeBase + RETURN_RVA);
    passThrough(OriginalCallSource::UnverifiedCallerPassThrough, {2,2,2}, g_exeBase + RETURN_RVA + 1);
    passThrough(OriginalCallSource::UnqualifiedControllerPassThrough, {2,2,2}, g_exeBase + RETURN_RVA, true);
    passThrough(OriginalCallSource::HostPassThrough, {1,1,1}, g_exeBase + RETURN_RVA);
    passThrough(OriginalCallSource::RoleChangedBeforeSelection, {2,0,0}, g_exeBase + RETURN_RVA);
    passThrough(OriginalCallSource::RoleChangedAfterFirstCopy, {2,2,0}, g_exeBase + RETURN_RVA);
    armPhase(); nestedUpdateDuringCopy = true; runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.nested && !event.geometry.originalPhase.threadPhaseConsistent &&
          geometryOriginalCalls == 2 && !g_originalPhaseFrame.available && !g_traceNestedDepth &&
          event.geometry.originalPhase.originalEntries == 1 && event.geometry.originalPhase.originalReturns == 1,
          "actual reentrant HookedUpdate restores parent token and preserves existing nested invalidation");
    armPhase(); phaseChangeToken = true; runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.complete && event.geometry.originalPhase.mismatch &&
          !event.geometry.originalPhase.threadPhaseConsistent && !g_originalPhaseFrame.available,
          "token mismatch invalidates only new witness while original gate result and legacy completeness survive");
    armPhase(); phaseChangeCoverage = true; runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.complete && !event.geometry.originalPhase.threadPhaseConsistent,
          "phase coverage change remains independent of legacy event completeness");
    armPhase(); gateFault = true;
    Check(CatchObservedUpdateException(controller, geometryPoint.data()) && PopTraceEvent(event) &&
          event.geometry.originalPhase.originalEntries == 1 && event.geometry.originalPhase.originalReturns == 0 &&
          event.geometry.originalPhase.originalUnwinds == 1 && event.geometry.originalPhase.unwound &&
          event.geometry.originalPhase.gateEntry.phase == UpdatePhase::InsideOriginalCall &&
          !event.geometry.originalPhase.gateReturn.available && !event.geometry.originalPhase.threadPhaseConsistent &&
          !g_originalPhaseFrame.available && !g_eventGateDepth && !g_traceTick.active && gateOriginalCalls == 1 && geometryOriginalCalls == 1,
          "actual Windows SEH propagates once and restores owner/original/gate boundaries without invented return");
    armPhase(); geometryFaultAt = 0;
    Check(CatchObservedUpdateException(controller, geometryPoint.data()) && PopTraceEvent(event) &&
          event.geometry.eventGate.returned && !event.geometry.eventGate.unwound &&
          event.geometry.originalPhase.gateReturn.available && event.geometry.originalPhase.originalUnwinds == 1 &&
          !event.geometry.originalPhase.threadPhaseConsistent && !g_originalPhaseFrame.available,
          "later native SEH retains genuine gate return but prevents original-bracket completion");
    armPhase(); g_originalPhaseSerial = UINT64_MAX; runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.complete && event.geometry.originalPhase.overflow &&
          !event.geometry.originalPhase.available && !event.geometry.originalPhase.threadPhaseConsistent &&
          g_originalPhaseSerial == UINT64_MAX && geometryOriginalCalls == 1,
          "phase serial exhaustion fails diagnostic closed without wrapping or suppressing original");
    armPhase(); g_originalPhaseFrame.depth = ORIGINAL_PHASE_DEPTH_CAP; runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.complete && event.geometry.originalPhase.overflow &&
          !event.geometry.originalPhase.threadPhaseConsistent && g_originalPhaseFrame.depth == ORIGINAL_PHASE_DEPTH_CAP,
          "phase depth cap preserves original and restores parked parent frame");
    armPhase(); g_role = &OffRole; g_original = &PhasePassOriginal;
    HookedUpdate(controller, geometryPoint.data());
    Check(geometryOriginalCalls == 1 && phaseFrameSeen.caller != 0 && phaseFrameSeen.caller != g_exeBase + RETURN_RVA &&
          phaseFrameSeen.source == OriginalCallSource::RoleOffPassThrough && !g_originalPhaseFrame.available,
          "real HookedUpdate captures its actual unmatched executable caller through enabled owner boundary");
    g_originalPhaseConfigured = false; geometryOriginalCalls = 0;
    const auto serialBeforeDisabled = g_originalPhaseSerial;
    HookedUpdate(controller, geometryPoint.data());
    Check(geometryOriginalCalls == 1 && !phaseFrameSeen.available && g_originalPhaseSerial == serialBeforeDisabled &&
          phasePointSeen == geometryPoint.data(), "disabled HookedUpdate retains direct dispatch with no observed-entry frame or token");
    armGate(); g_role = &ClientRole; g_original = &GeometryOriginal; run();
    TraceEvent legacyPhaseProjection;
    Check(PopTraceEvent(legacyPhaseProjection), "disabled phase baseline receipt captured");
    armPhase(); runPhase();
    Check(PopTraceEvent(event) && event.geometry.eventGate.calls == legacyPhaseProjection.geometry.eventGate.calls &&
          event.geometry.eventGate.result == legacyPhaseProjection.geometry.eventGate.result &&
          event.geometry.eventGate.complete == legacyPhaseProjection.geometry.eventGate.complete &&
          event.geometry.calls == legacyPhaseProjection.geometry.calls && event.geometry.complete == legacyPhaseProjection.geometry.complete &&
          event.geometry.originalReturned == legacyPhaseProjection.geometry.originalReturned && geometryOriginalCalls == 1 &&
          geometryCopies == 2 && boxOriginalCalls == 7 && gateOriginalCalls == 1 && geometryBoundaryPreserved,
          "enabled phase preserves disabled legacy gate/BOX verdicts and production callback/original schedule");
    armPhase(); geometryUseGate = false; phaseFiberProbe = true;
    const bool convertedForPhaseTest = IsThreadAFiber() == FALSE;
    phaseHomeFiber = convertedForPhaseTest ? ConvertThreadToFiber(nullptr) : GetCurrentFiber();
    phaseChildFiber = phaseHomeFiber ? CreateFiber(0, &PhaseOtherFiber, nullptr) : nullptr;
    Check(phaseHomeFiber && phaseChildFiber, "test-owned Windows fibers created for TLS attribution limitation");
    if (phaseHomeFiber && phaseChildFiber) {
        runPhase();
        Check(PopTraceEvent(event) && event.geometry.originalPhase.threadPhaseConsistent &&
              !event.geometry.originalPhase.fiberContinuityProven && !event.geometry.originalPhase.originalPhaseEligibility &&
              gateOriginalCalls == 1 && geometryOriginalCalls == 1 && !g_originalPhaseFrame.available,
              "actual two-fiber interleave can share thread bracket but never gains fiber continuity or eligibility");
    }
    if (phaseChildFiber) DeleteFiber(phaseChildFiber);
    if (convertedForPhaseTest && phaseHomeFiber)
        Check(ConvertFiberToThread() != FALSE, "test-owned fiber conversion restored");
    phaseHomeFiber = phaseChildFiber = nullptr; phaseFiberProbe = false;
    g_originalPhaseConfigured = false; g_originalPhaseRequested = false; g_originalPhaseFrame = {};
    g_enrollmentRequested = false; g_enrollmentConfigured = false; g_eventGateInstalled = false;
    g_originalEventGate = nullptr;
    g_geometryInstalled = false; g_geometryConfig = {}; g_geometryTerminal = GeometryTerminal::Disabled;
    g_geometryScope = nullptr; g_originalBox = nullptr;
}
// Reader controls run the production implementation over owned byte buffers;
// the optional read leaf only schedules deterministic changes/read failures.
uintptr_t catalogTrigger = 0, catalogMutation = 0, catalogFailure = 0;
unsigned catalogTriggerNth = 0, catalogSeen = 0;
unsigned catalogFailureNth = 0, catalogFailureSeen = 0;
bool catalogChangeLifecycle = false;
bool CatalogControlRead(uintptr_t p, void* out, std::size_t n) {
    if (p == catalogTrigger && ++catalogSeen == catalogTriggerNth) {
        if (catalogMutation) *reinterpret_cast<std::uint8_t*>(catalogMutation) ^= 1;
        if (catalogChangeLifecycle) ++syntheticTransition;
    }
    if (p == catalogFailure && (++catalogFailureSeen == catalogFailureNth || !catalogFailureNth)) return false;
    return CopyNative(p, out, n);
}
void RecordCatalogControls(std::uint8_t* image) {
    using S = kh2coop::NativeRecordContentStatus;
    const auto savedTransition = syntheticTransition, savedLoad = syntheticLoad;
    const auto base = reinterpret_cast<uintptr_t>(image);
    std::array<std::uint8_t, 32> layout {}; layout[0] = 0x90;
    std::array<Controller, 64> controllers {};
    std::array<std::vector<std::uint8_t>, 64> data;
    NativeRecordCatalog catalog;
    NativeRecordMembership member;
    alignas(8) std::array<std::uint8_t, 0xA00> actor {};
    const auto actorAddress = reinterpret_cast<uintptr_t>(actor.data());
    auto put = [](void* p, const auto& value) { std::memcpy(p, &value, sizeof(value)); };
    auto seed = [&](const std::vector<std::uint16_t>& counts) {
        catalogTrigger = catalogMutation = catalogFailure = 0;
        catalogTriggerNth = catalogSeen = 0; catalogChangeLifecycle = false;
        catalogFailureNth = catalogFailureSeen = 0;
        const auto count = static_cast<std::int32_t>(counts.size());
        put(image + CONTROLLER_COUNT, count);
        for (std::size_t i = 0; i < counts.size(); ++i) {
            data[i].assign(44 + static_cast<std::size_t>(counts[i]) * 64 + 1, 0);
            data[i][0] = static_cast<std::uint8_t>(1 + i % 2);
            put(data[i].data() + 2, static_cast<std::uint16_t>(30 + i));
            put(data[i].data() + 4, counts[i]);
            controllers[i] = {};
            controllers[i].key = 808476514; // Repeated group is normal.
            controllers[i].header = reinterpret_cast<uintptr_t>(data[i].data());
            controllers[i].spawnArray = controllers[i].header + 44;
            controllers[i].regionArray = controllers[i].spawnArray + counts[i] * 64;
            for (std::size_t r = 0; r < counts[i]; ++r) {
                put(data[i].data() + 44 + r * 64, static_cast<std::uint32_t>(302 + i));
                put(data[i].data() + 44 + r * 64 + 30, static_cast<std::uint16_t>(1 + i * 300 + r));
            }
            const TableEntry table {controllers[i].key, 0, reinterpret_cast<uintptr_t>(&controllers[i])};
            put(image + CONTROLLER_TABLE + i * 16, table);
        }
        const std::array<std::uint8_t, 10> now {5, 6, 7, 0xEE, 0x34, 0x12, 0x78, 0x56, 0xBC, 0x9A};
        std::memcpy(image + kh2coop::offsets::NOW, now.data(), now.size());
    };
    auto capture = [&] { CaptureRecordCatalogImpl(base, layout, catalog, &CatalogControlRead, &CaptureDiagnosticStamp); };
    auto associate = [&](std::size_t i, std::size_t r) {
        put(actor.data() + 0x9E8, reinterpret_cast<uintptr_t>(&controllers[i]));
        put(actor.data() + 0x9F0, controllers[i].spawnArray + r * 64);
    };
    seed({5, 0, 0, 0, 3, 4, 4, 4, 3, 3});
    CaptureNativeRecordCatalog(base, layout, catalog);
    Check(catalog.status == S::Complete && catalog.tableInventoryComplete && catalog.contentBytesComplete &&
        catalog.associationComplete && catalog.entryCount == 10 && catalog.declaredLogicalRecords == 26 &&
        catalog.capturedLogicalRecords == 26, "catalog inventories ten definitions and all 26 records including three empty definitions");
    const auto& loc = catalog.entries[0].content.location;
    Check(loc.world == 5 && loc.room == 6 && loc.door == 7 && loc.mapProgram == 0x1234 &&
        loc.battleProgram == 0x5678 && loc.eventProgram == 0x9ABC && catalog.before.location[3] == 0xEE,
        "portable location has the six exact fields; raw NOW padding remains only in sidecar");
    Check(!catalog.behaviorEligibility && !catalog.lifetimeProven && !catalog.globalPendingExcluded && !catalog.atomic,
        "complete catalog grants no behavior lifetime pending or atomic authority");
    associate(0, 4);
    ResolveNativeRecordMembership(base, catalog, actorAddress, member);
    Check(member.status == S::Complete && member.contentComparisonAvailable && member.tableIndex == 0 &&
        member.recordIndex == 4 && member.actorControllerReads[0] && member.actorControllerReads[1] &&
        member.actorRecordReads[0] && member.actorRecordReads[1] && member.definitionRecords[0].size() == 5 &&
        member.definitionRecords[1] == catalog.entries[0].content.records,
        "public membership resolves integral native index using repeated pointers and full ordered definition bytes");
    Check(!member.behaviorEligibility && !member.lifetimeProven && !member.globalPendingExcluded && !member.atomic,
        "complete native membership remains content comparison only");
    auto badMembership = [&](uintptr_t record, const char* label) {
        put(actor.data() + 0x9F0, record);
        ResolveNativeRecordMembership(base, catalog, actorAddress, member);
        Check(!member.contentComparisonAvailable && member.status != S::Complete, label);
    };
    badMembership(controllers[0].spawnArray + 1, "unaligned interior record is not integral native membership");
    badMembership(controllers[0].regionArray, "one-past record array is not membership");
    badMembership(controllers[0].spawnArray - 64, "preceding record is not membership");
    associate(1, 0);
    ResolveNativeRecordMembership(base, catalog, actorAddress, member);
    Check(!member.contentComparisonAvailable, "empty definition cannot supply a native record index");
    associate(0, 0);
    data[0][44 + 64 + 40] ^= 1;
    ResolveNativeRecordMembership(base, catalog, actorAddress, member);
    Check(!member.contentComparisonAvailable && (member.issues & Issue(RecordCatalogIssue::Changed)),
        "changed non-selected record invalidates the whole definition comparison");
    data[0][44 + 64 + 40] ^= 1;
    catalogTrigger = controllers[0].spawnArray; catalogTriggerNth = 2; catalogSeen = 0;
    catalogMutation = actorAddress + 0x9F0;
    ResolveRecordMembershipImpl(base, catalog, actorAddress, member, &CatalogControlRead, &CaptureDiagnosticStamp);
    Check(!member.contentComparisonAvailable && member.record[0] != member.record[1] && member.actorRecordReads[1],
        "actor record-pointer mutation during byte reads is caught by the actual final pointer read");
    seed({}); capture();
    Check(catalog.status == S::Complete && catalog.entryCount == 0 && catalog.declaredLogicalRecords == 0 &&
        catalog.tableInventoryComplete, "zero table is a complete sampled empty catalog with two count reads");
    seed({1}); std::int32_t negative = -1; put(image + CONTROLLER_COUNT, negative); capture();
    Check(catalog.status != S::Complete && catalog.countBefore == -1 && catalog.entryCount == 0,
        "negative signed table count is retained and rejected without truncation");
    std::int32_t tooMany = 65; put(image + CONTROLLER_COUNT, tooMany); capture();
    Check(catalog.entryCount == 0 && (catalog.issues & Issue(RecordCatalogIssue::Count)), "table count above 64 is rejected without a prefix catalog");
    seed({257}); capture();
    Check(catalog.status == S::Unsupported && catalog.declaredLogicalRecords == 257 &&
        catalog.capturedLogicalRecords == 0 && catalog.entries[0].content.records.empty(),
        "per-definition 256 cap retains declared 257 and omits the whole array");
    seed({256, 256, 256, 256, 1}); capture();
    Check(catalog.status == S::Unsupported && catalog.declaredLogicalRecords == 1025 &&
        catalog.capturedLogicalRecords == 1024 && catalog.entries[4].content.records.empty() && !catalog.contentBytesComplete,
        "aggregate 1024 cap retains overflow definition and cannot claim complete prefix");
    seed({1, 1}); data[1][0] = 9; capture();
    Check(catalog.status == S::Unsupported && catalog.contentBytesComplete && catalog.entries[1].content.records.size() == 1,
        "unsupported header type remains in complete raw byte inventory but not supported association");
    seed({1, 1}); std::uint32_t flags = 1; put(image + CONTROLLER_TABLE + 16 + 4, flags); capture();
    Check(catalog.status == S::Unsupported && catalog.entryCount == 2 && catalog.tableInventoryComplete &&
        !catalog.entries[1].controllerBeforeAvailable && !catalog.contentBytesComplete,
        "mixed script slot retains raw table and never chases alternate pointer as ordinary controller");
    seed({1}); flags = 4; put(image + CONTROLLER_TABLE + 4, flags); capture();
    Check(catalog.status == S::Unsupported && catalog.contentBytesComplete && !catalog.associationComplete,
        "unknown table flags remain raw and unavailable for supported association");
    flags = 2; put(image + CONTROLLER_TABLE + 4, flags); capture();
    Check(catalog.status == S::Complete && Field<std::uint32_t>(catalog.entries[0].tableBefore.data(), 4) == 2,
        "reviewed ordinary table-state bit is retained without becoming behavior authority");
    seed({1}); uintptr_t nullPointer = 0; put(image + CONTROLLER_TABLE + 8, nullPointer); capture();
    Check(catalog.status == S::Partial && catalog.tableInventoryComplete && !catalog.contentBytesComplete,
        "null ordinary controller is an explicit partial definition not an empty catalog");
    seed({1}); controllers[0].key ^= 1; capture();
    Check(!catalog.associationComplete && (catalog.issues & Issue(RecordCatalogIssue::Key)) && catalog.contentBytesComplete,
        "mismatched native group key cannot qualify association despite retained bytes");
    seed({1}); ++controllers[0].spawnArray; capture();
    Check(!catalog.contentBytesComplete && (catalog.issues & Issue(RecordCatalogIssue::Layout)),
        "header plus44 array layout is checked before record traversal");
    seed({1}); controllers[0].regionArray += 64; capture();
    Check(!catalog.contentBytesComplete && (catalog.issues & Issue(RecordCatalogIssue::Layout)),
        "full declared array extent must terminate at the region-array pointer");
    seed({1}); controllers[0].header = USER_END - 20; capture();
    Check(!catalog.contentBytesComplete && (catalog.issues & Issue(RecordCatalogIssue::Pointer)),
        "overflowing header range is rejected before pointer arithmetic or dereference");
    seed({1, 1}); put(image + CONTROLLER_TABLE + 16 + 8, reinterpret_cast<uintptr_t>(&controllers[0])); capture();
    Check(catalog.status == S::Ambiguous && catalog.entryCount == 2 && catalog.contentBytesComplete,
        "duplicate controller entries are retained and never resolved by first match");
    seed({1, 1}); controllers[1].header = controllers[0].header; controllers[1].spawnArray = controllers[0].spawnArray;
    controllers[1].regionArray = controllers[0].regionArray; capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::Alias)),
        "shared full definition span fails native association even with distinct controllers");
    seed({0, 0}); data[1][2] = data[0][2]; data[1][0] = data[0][0]; capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::DuplicateContent)),
        "equal empty content descriptors stay duplicated rather than first-match selected");
    seed({0, 0}); data[1][2] = data[0][2]; data[1][0] = data[0][0]; data[1][15] ^= 1; capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::DuplicateContent)) &&
        catalog.contentBytesComplete && !catalog.associationComplete && !catalog.entries[0].associationUnique &&
        !catalog.entries[1].associationUnique && catalog.entries[0].content.header[15] != catalog.entries[1].content.header[15],
        "duplicate empty descriptor with different header sidecar remains raw complete but association ambiguous");
    seed({1, 1}); std::memcpy(data[1].data(), data[0].data(), 44 + 64); data[1][15] ^= 1; capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::DuplicateContent)) &&
        (catalog.issues & Issue(RecordCatalogIssue::DuplicateId)) && catalog.contentBytesComplete &&
        !catalog.associationComplete && catalog.entries[0].content.records == catalog.entries[1].content.records &&
        catalog.entries[0].content.header[15] != catalog.entries[1].content.header[15],
        "nonempty duplicate descriptor retains differing header sidecars and both ambiguity reasons");
    seed({1, 1}); put(data[1].data() + 44 + 30, std::uint16_t{1}); capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::DuplicateId)),
        "duplicate raw record IDs across different definitions invalidate unique scope");
    seed({2}); put(data[0].data() + 44 + 30, std::uint16_t{0}); put(data[0].data() + 44 + 64 + 30, std::uint16_t{0x8000}); capture();
    Check(catalog.status == S::Complete && Field<std::uint16_t>(catalog.entries[0].content.records[0].data(), 30) == 0 &&
        Field<std::uint16_t>(catalog.entries[0].content.records[1].data(), 30) == 0x8000,
        "zero and high-bit IDs remain unsigned content without lookup remapping or behavior claims");
    seed({1}); catalogFailure = controllers[0].spawnArray; capture();
    Check(!catalog.contentBytesComplete && catalog.entries[0].recordReadMask[0] == 0 &&
        catalog.entries[0].content.records.size() == 1 && catalog.entries[0].headerAfterAvailable,
        "record read failures retain declared slot and successful surrounding raw reads");
    seed({1}); catalogFailure = controllers[0].spawnArray; catalogFailureNth = 2; capture();
    Check(!catalog.contentBytesComplete && catalog.entries[0].recordReadMask[0] == 1 &&
        Field<std::uint32_t>(catalog.entries[0].content.records[0].data(), 0) == 302,
        "after-only record failure preserves genuine before bytes with explicit mask");
    seed({1}); catalogFailure = controllers[0].spawnArray; catalogFailureNth = 1; capture();
    Check(!catalog.contentBytesComplete && catalog.entries[0].recordReadMask[0] == 2 &&
        Field<std::uint32_t>(catalog.entries[0].recordsAfter[0].data(), 0) == 302,
        "before-only record failure preserves genuine after bytes without fabricating a before zero");
    seed({1}); controllers[0].header = base + CONTROLLER_TABLE;
    controllers[0].spawnArray = controllers[0].header + 44; controllers[0].regionArray = controllers[0].spawnArray; capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::Alias)),
        "definition header alias to table metadata is explicit ambiguous evidence");
    seed({1}); controllers[0].header = reinterpret_cast<uintptr_t>(&controllers[0]);
    controllers[0].spawnArray = controllers[0].header + 44; controllers[0].regionArray = controllers[0].spawnArray; capture();
    Check(catalog.status == S::Ambiguous && (catalog.issues & Issue(RecordCatalogIssue::Alias)),
        "header overlapping its own controller cannot qualify native content association");
    seed(std::vector<std::uint16_t>(64, 0)); capture();
    Check(catalog.status == S::Complete && catalog.entryCount == 64 && catalog.declaredLogicalRecords == 0,
        "all 64 distinct empty definitions are inventoried at the table cap");
    seed({1}); catalogTrigger = base + CONTROLLER_TABLE; catalogTriggerNth = 2; catalogMutation = controllers[0].spawnArray + 40; capture();
    Check(!catalog.contentBytesComplete && catalog.entries[0].recordReadMask[0] == 3 &&
        catalog.entries[0].content.records[0] != catalog.entries[0].recordsAfter[0],
        "between-pass full-record mutation retains both actual byte arrays and invalidates completeness");
    seed({1}); catalogTrigger = base + CONTROLLER_TABLE; catalogTriggerNth = 2; catalogMutation = controllers[0].header + 14; capture();
    Check(!catalog.contentBytesComplete && catalog.entries[0].content.header[14] != catalog.entries[0].headerAfter[14],
        "activation-marker drift is a raw catalog change despite portable projection exclusion");
    seed({1}); catalogTrigger = base + CONTROLLER_TABLE; catalogTriggerNth = 2; catalogMutation = base + CONTROLLER_TABLE + 4; capture();
    Check(!catalog.tableInventoryComplete && catalog.entries[0].tableBefore != catalog.entries[0].tableAfter,
        "table flag drift prevents complete sampled inventory");
    seed({1}); catalogTrigger = base + CONTROLLER_COUNT; catalogTriggerNth = 2; catalogChangeLifecycle = true; capture();
    Check(!catalog.lifecycleStable && !catalog.contentBytesComplete && catalog.entries[0].bytesComplete,
        "lifecycle drift invalidates global completeness while preserving complete local bytes");
    seed({1}); std::array<std::uint8_t, 32> unknownLayout {}; CaptureNativeRecordCatalog(base, unknownLayout, catalog);
    Check(!catalog.associationComplete && catalog.contentBytesComplete && (catalog.issues & Issue(RecordCatalogIssue::LayoutIdentity)),
        "unknown layout provenance cannot become supported association");
    seed({1}); const auto registered = g_diagnosticGameThread.exchange(0);
    const auto oldLoads = loadReads.load(), oldTransitions = transitionReads.load();
    CaptureNativeRecordCatalog(base, layout, catalog);
    Check(catalog.status == S::Unavailable && (catalog.issues & Issue(RecordCatalogIssue::Thread)) &&
        oldLoads == loadReads.load() && oldTransitions == transitionReads.load(),
        "unregistered reader fails closed before game-thread lifecycle calls");
    g_diagnosticGameThread = registered;
    // Restore the minimal caller state; subsequent legacy geometry controls
    // initialize their own complete fixture and retain all old assertions.
    catalogTrigger = catalogMutation = catalogFailure = 0;
    catalogChangeLifecycle = false;
    syntheticTransition = savedTransition;
    syntheticLoad = savedLoad;
}
unsigned mutationCtorCalls = 0, mutationInitCalls = 0, mutationTeardownCalls = 0;
bool mutationArguments = true, mutationNested = false, mutationFault = false, mutationWait = false;
bool mutationEnteredUnavailable = true;
void* mutationExpectedController = nullptr;
const void* mutationExpectedHeader = nullptr;
std::uint32_t mutationExpectedKey = 0;
void* mutationReturn = nullptr;
HANDLE mutationEntered = nullptr, mutationRelease = nullptr;
std::uint32_t mutationMaxDepth = 0;

void ObserveMutationEntry(void* controller) {
    const auto ticket = AcquireKnownMutationTicket();
    mutationEnteredUnavailable = mutationEnteredUnavailable && !ticket.available &&
        (ticket.inFlight != 0 || ticket.poisoned);
    mutationMaxDepth = (std::max)(mutationMaxDepth, ticket.inFlight);
    mutationArguments = mutationArguments && controller == mutationExpectedController;
    if (mutationFault) RaiseException(kDeliberateException, 0, 0, nullptr);
}
void* __fastcall MutationCtor(void* controller, std::uint32_t key, const void* header) {
    ++mutationCtorCalls;
    ObserveMutationEntry(controller);
    mutationArguments = mutationArguments && key == mutationExpectedKey && header == mutationExpectedHeader;
    return mutationReturn;
}
void __fastcall MutationInit(void* controller) {
    ++mutationInitCalls;
    ObserveMutationEntry(controller);
    if (mutationNested) {
        mutationNested = false;
        HookedControllerTeardown(controller);
        mutationEnteredUnavailable = mutationEnteredUnavailable && AcquireKnownMutationTicket().inFlight == 1;
    }
    if (mutationWait) {
        SetEvent(mutationEntered);
        WaitForSingleObject(mutationRelease, INFINITE);
    }
}
void __fastcall MutationTeardown(void* controller) {
    ++mutationTeardownCalls;
    ObserveMutationEntry(controller);
}
DWORD WINAPI MutationForeignThread(void*) {
    HookedControllerInit(mutationExpectedController);
    return 0;
}
bool CatchMutationException(unsigned which) {
    __try {
        if (which == 0) HookedControllerCtor(mutationExpectedController, mutationExpectedKey, mutationExpectedHeader);
        else if (which == 1) HookedControllerInit(mutationExpectedController);
        else HookedControllerTeardown(mutationExpectedController);
    } __except (GetExceptionCode() == kDeliberateException ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

void KnownMutationControls(std::uint8_t* image) {
    using kh2coop::KnownControllerMutationFence;
    // All image reads and callbacks below are owned test memory. The MinHook
    // seam reports lifecycle results but never patches executable instructions.
    g_knownMutation.word.store(0);
    g_knownMutationRequested = false;
    g_knownMutationInstalled = 0;
    const auto disabled = AcquireKnownMutationTicket();
    Check(!disabled.available && !disabled.coverageComplete && !disabled.poisoned &&
          !KnownMutationTicketCurrent(disabled), "known mutation default state cannot issue a ticket");
    char prior[32768] {};
    const DWORD priorLength = GetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", prior, sizeof(prior));
    SetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", nullptr);
    Check(!KnownMutationRequested(), "preparation absent is default off");
    for (const char* value : {"0", "true", "01", "1 "}) {
        SetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", value);
        Check(!KnownMutationRequested(), "preparation accepts only literal one");
    }
    SetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", "1");
    Check(KnownMutationRequested(), "preparation literal one opts in independently of trace");
    SetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", priorLength && priorLength < sizeof(prior) ? prior : nullptr);
    const auto hookBaseline = hookCalls;
    const bool oldTrace = g_traceRequested.load();
    g_traceRequested = true;
    InstallKnownMutationHooks();
    Check(hookCalls == hookBaseline && !AcquireKnownMutationTicket().available,
          "ordinary trace request does not install or enable known mutation fence");
    g_traceRequested = oldTrace;

    std::array<std::uint8_t, 0x58> controller {};
    std::array<std::uint8_t, 44> header {};
    int returnedObject = 0;
    mutationExpectedController = controller.data(); mutationExpectedHeader = header.data();
    mutationExpectedKey = 0xFEDCBA98; mutationReturn = &returnedObject;
    mutationMockBase = reinterpret_cast<uintptr_t>(image);
    mutationMockCtor = reinterpret_cast<void*>(&MutationCtor);
    mutationMockInit = reinterpret_cast<void*>(&MutationInit);
    mutationMockTeardown = reinterpret_cast<void*>(&MutationTeardown);
    g_exeBase = mutationMockBase;
    std::memcpy(image + CONTROLLER_CTOR_RVA, kControllerCtorBytes, sizeof(kControllerCtorBytes));
    std::memcpy(image + CONTROLLER_INIT_RVA, kControllerInitBytes, sizeof(kControllerInitBytes));
    std::memcpy(image + CONTROLLER_TEARDOWN_RVA, kControllerTeardownBytes, sizeof(kControllerTeardownBytes));
    mutationHookMock = true;
    g_knownMutationRequested = true;
    InstallKnownMutationHooks();
    const auto originalTicket = AcquireKnownMutationTicket();
    Check(originalTicket.available && originalTicket.coverageComplete && !originalTicket.poisoned &&
          originalTicket.revision && KnownMutationTicketCurrent(originalTicket) && g_knownMutationInstalled == 7,
          "all three byte-gated mock installations publish a negative-fence ticket");
    Check(HookedControllerCtor(controller.data(), mutationExpectedKey, header.data()) == &returnedObject &&
          mutationCtorCalls == 1 && mutationArguments && mutationEnteredUnavailable &&
          !KnownMutationTicketCurrent(originalTicket),
          "real constructor pass-through retires before original and preserves all arguments and exact return");
    const auto beforeInit = AcquireKnownMutationTicket();
    const auto controllerCopy = controller;
    HookedControllerInit(controller.data());
    Check(controller == controllerCopy && mutationInitCalls == 1 && !KnownMutationTicketCurrent(beforeInit) &&
          AcquireKnownMutationTicket().available && AcquireKnownMutationTicket().revision > beforeInit.revision,
          "identical-byte same-address reinit cancels old ticket without relying on native field change");
    const auto beforeNested = AcquireKnownMutationTicket();
    mutationNested = true;
    HookedControllerInit(controller.data());
    Check(mutationInitCalls == 2 && mutationTeardownCalls == 1 && mutationMaxDepth == 2 &&
          mutationEnteredUnavailable && !KnownMutationTicketCurrent(beforeNested) &&
          AcquireKnownMutationTicket().available && !AcquireKnownMutationTicket().inFlight,
          "nested mutation preserves each original once and cannot publish idle state before outer return");
    const auto beforeReuse = AcquireKnownMutationTicket();
    HookedControllerTeardown(controller.data());
    HookedControllerCtor(controller.data(), mutationExpectedKey, header.data());
    Check(controller == controllerCopy && !KnownMutationTicketCurrent(beforeReuse) && mutationArguments,
          "same-address teardown and reconstruction cannot revive identical-byte ticket");

    const auto roles = roleReads.load(), transitions = transitionReads.load(), loads = loadReads.load();
    const auto published = g_tracePublished.load();
    g_traceInstalled = false; g_traceRequested = false;
    const auto beforeForeign = AcquireKnownMutationTicket();
    mutationEntered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    mutationRelease = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    mutationWait = true;
    HANDLE thread = mutationEntered && mutationRelease ? CreateThread(nullptr, 0, MutationForeignThread, nullptr, 0, nullptr) : nullptr;
    const bool entered = thread && WaitForSingleObject(mutationEntered, 5000) == WAIT_OBJECT_0;
    Check(entered && AcquireKnownMutationTicket().inFlight == 1 && !AcquireKnownMutationTicket().available &&
          !KnownMutationTicketCurrent(beforeForeign), "foreign thread invalidates and remains unavailable throughout blocked original");
    if (mutationRelease) SetEvent(mutationRelease);
    const bool joined = thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0;
    Check(joined && AcquireKnownMutationTicket().available && !KnownMutationTicketCurrent(beforeForeign),
          "foreign normal return closes in-flight count but never restores old revision");
    if (thread && !joined) std::abort(); // Do not free owned state under a live test thread.
    if (thread) CloseHandle(thread);
    if (mutationEntered) CloseHandle(mutationEntered);
    if (mutationRelease) CloseHandle(mutationRelease);
    mutationWait = false;
    Check(roleReads.load() == roles && transitionReads.load() == transitions && loadReads.load() == loads &&
          g_tracePublished.load() == published,
          "queue-disabled cancellation does not read lifecycle/role or require diagnostic event publication");

    for (unsigned which = 0; which < 3; ++which) {
        // Explicit test reset only; production never resets sticky poison.
        g_knownMutation.word.store(KnownControllerMutationFence::CoverageBit | KnownControllerMutationFence::RevisionStep);
        const auto beforeFault = AcquireKnownMutationTicket();
        const auto calls = mutationCtorCalls + mutationInitCalls + mutationTeardownCalls;
        mutationFault = true;
        Check(CatchMutationException(which), "real native mutation SEH propagates unchanged to caller");
        mutationFault = false;
        const auto failed = AcquireKnownMutationTicket();
        Check(failed.poisoned && !failed.available && !failed.inFlight && !KnownMutationTicketCurrent(beforeFault) &&
              mutationCtorCalls + mutationInitCalls + mutationTeardownCalls == calls + 1,
              "each mutation unwind poisons permanently and closes only its own call count");
        g_knownMutation.CompleteCoverage();
        Check(!AcquireKnownMutationTicket().available, "complete installation cannot clear an unwind poison");
    }
    g_knownMutation.word.store(KnownControllerMutationFence::CoverageBit | KnownControllerMutationFence::RevisionMask);
    const auto overflowCalls = mutationInitCalls;
    HookedControllerInit(controller.data());
    Check(AcquireKnownMutationTicket().poisoned && !AcquireKnownMutationTicket().available &&
          AcquireKnownMutationTicket().revision == KnownControllerMutationFence::RevisionMask / KnownControllerMutationFence::RevisionStep &&
          mutationInitCalls == overflowCalls + 1, "revision exhaustion poisons without wrap or suppressing original");
    g_knownMutation.word.store(KnownControllerMutationFence::CoverageBit | KnownControllerMutationFence::RevisionStep |
                              KnownControllerMutationFence::InFlightMask);
    HookedControllerTeardown(controller.data());
    Check(AcquireKnownMutationTicket().poisoned && AcquireKnownMutationTicket().inFlight == KnownControllerMutationFence::InFlightMask,
          "in-flight exhaustion cannot decrement another invocation or manufacture idle coverage");

    g_knownMutation.word.store(KnownControllerMutationFence::CoverageBit | KnownControllerMutationFence::RevisionStep);
    const auto beforeShutdown = AcquireKnownMutationTicket();
    mutationRemoveFail = true;
    ShutdownKnownMutationHooks();
    Check(!KnownMutationTicketCurrent(beforeShutdown) && AcquireKnownMutationTicket().poisoned &&
          !AcquireKnownMutationTicket().coverageComplete && g_knownMutationInstalled == 7 && g_originalControllerInit,
          "shutdown poisons before teardown and failed removal retains callable trampolines");
    mutationRemoveFail = false;
    ShutdownKnownMutationHooks();
    Check(!g_knownMutationInstalled && !g_originalControllerCtor && !g_originalControllerInit && !g_originalControllerTeardown,
          "successful quiesced teardown removes all installed mutation hooks");
    InstallKnownMutationHooks();
    Check(!AcquireKnownMutationTicket().available && AcquireKnownMutationTicket().poisoned,
          "reinstallation after shutdown cannot revive old namespace");
    ShutdownKnownMutationHooks();

    g_knownMutation.word.store(0);
    mutationEnableFailRva = CONTROLLER_INIT_RVA;
    InstallKnownMutationHooks();
    Check(g_knownMutationInstalled == 5 && AcquireKnownMutationTicket().poisoned && !AcquireKnownMutationTicket().available &&
          !g_originalControllerInit, "partial enable failure poisons despite two installed pass-throughs");
    HookedControllerCtor(controller.data(), mutationExpectedKey, header.data());
    Check(AcquireKnownMutationTicket().poisoned, "partial-coverage original call cannot heal unavailable namespace");
    ShutdownKnownMutationHooks();
    mutationEnableFailRva = 0;
    g_knownMutation.word.store(0);
    image[CONTROLLER_INIT_RVA] ^= 1;
    InstallKnownMutationHooks();
    Check(g_knownMutationInstalled == 5 && AcquireKnownMutationTicket().poisoned && !g_originalControllerInit,
          "mismatched initializer bytes cannot claim complete known-entry coverage");
    image[CONTROLLER_INIT_RVA] ^= 1;
    ShutdownKnownMutationHooks();
    mutationHookMock = false;
    g_knownMutationRequested = false;
}

uintptr_t occupancyTrigger = 0, occupancyMutation = 0, occupancyFailure = 0;
unsigned occupancyNth = 0, occupancySeen = 0, occupancyFailureNth = 0, occupancyFailureSeen = 0;
std::size_t occupancyTriggerSize = 0;
bool occupancyLoadChange = false, occupancyRevisionChange = false;
bool OccupancyControlRead(uintptr_t address, void* out, std::size_t size) {
    if (address == occupancyFailure && ++occupancyFailureSeen == occupancyFailureNth) return false;
    const bool ok = CopyNative(address, out, size);
    if (address == occupancyTrigger && (!occupancyTriggerSize || size == occupancyTriggerSize) && ++occupancySeen == occupancyNth) {
        if (occupancyMutation) *reinterpret_cast<std::uint8_t*>(occupancyMutation) ^= 1;
        if (occupancyLoadChange) ++syntheticLoad;
        if (occupancyRevisionChange) { const bool counted = g_knownMutation.Begin(); g_knownMutation.End(counted, false); }
    }
    return ok;
}

void SelectedOccupancyControls(std::uint8_t* image) {
    using namespace kh2coop;
    using namespace kh2coop::offsets::active_entity_list;
    const uintptr_t base = reinterpret_cast<uintptr_t>(image);
    alignas(8) std::array<std::array<std::uint8_t, 0x58>, 2> controllers {};
    std::array<std::vector<std::uint8_t>, 2> definitions;
    std::vector<std::array<std::uint8_t, 0xA98>> actors(257);
    alignas(8) std::array<std::array<std::uint8_t, 0x60>, 2> objects {};
    std::array<std::int32_t, 2> hp {20, 20};
    std::array<std::uint8_t, 32> layout {}; layout[0] = 9;
    auto catalog = std::make_unique<NativeRecordCatalog>();
    auto out = std::make_unique<NativeSelectedOccupancy>();
    const auto oldLoad = syntheticLoad;
    const auto owner = g_diagnosticGameThread.load();
    const auto put = [](void* p, const auto& value) { std::memcpy(p, &value, sizeof(value)); };
    auto handle = [&](uintptr_t p) {
        const auto region = p & ~static_cast<uintptr_t>(HANDLE_LOW_MASK);
        for (unsigned i = 0; i < 64; ++i) {
            uintptr_t current = 0; std::memcpy(&current, image + HANDLE_REGION_TABLE + i * 8, 8);
            if (current == region || current == UINTPTR_MAX) {
                put(image + HANDLE_REGION_TABLE + i * 8, region);
                return static_cast<std::uint32_t>(0x80000000U | (i << 25) | (p & HANDLE_LOW_MASK));
            }
        }
        return std::uint32_t{0};
    };
    auto actor = [&](unsigned n, unsigned definition, unsigned record) {
        const auto c = reinterpret_cast<uintptr_t>(controllers[definition].data());
        const auto r = reinterpret_cast<uintptr_t>(definitions[definition].data()) + 44 + record * 64;
        put(actors[n].data() + 0x918, reinterpret_cast<uintptr_t>(objects[0].data()));
        put(actors[n].data() + 0x5C0, reinterpret_cast<uintptr_t>(hp.data()));
        put(actors[n].data() + 0x9E8, c); put(actors[n].data() + 0x9F0, r);
    };
    auto list = [&](bool deferred, std::initializer_list<unsigned> nodes) {
        uintptr_t head = 0, tail = 0;
        for (const auto n : nodes) {
            const auto p = reinterpret_cast<uintptr_t>(actors[n].data());
            if (!head) head = p;
            if (tail) put(reinterpret_cast<void*>(tail + 0xA90), handle(p));
            tail = p; put(actors[n].data() + 0xA90, std::uint32_t{0});
        }
        put(image + (deferred ? FREE_HEAD : HEAD), head);
        put(image + (deferred ? FREE_TAIL : TAIL), tail);
    };
    auto seed = [&] {
        occupancyTrigger = occupancyMutation = occupancyFailure = 0;
        occupancyNth = occupancySeen = occupancyFailureNth = occupancyFailureSeen = 0;
        occupancyTriggerSize = 0; occupancyLoadChange = occupancyRevisionChange = false;
        syntheticLoad = oldLoad; g_diagnosticGameThread = owner;
        g_knownMutation.word.store(KnownControllerMutationFence::CoverageBit | KnownControllerMutationFence::RevisionStep);
        for (auto& a : actors) a.fill(0);
        controllers = {}; objects = {};
        for (unsigned i = 0; i < 64; ++i) put(image + HANDLE_REGION_TABLE + i * 8, UINTPTR_MAX);
        for (unsigned i = 0; i < 2; ++i) {
            definitions[i].assign(44 + (i ? 1 : 5) * 64 + 1, 0);
            auto* h = definitions[i].data(); h[0] = 2;
            put(h + 2, static_cast<std::uint16_t>(30 + i));
            put(h + 4, static_cast<std::uint16_t>(i ? 1 : 5));
            Controller c; c.key = 808476514; c.header = reinterpret_cast<uintptr_t>(h);
            c.spawnArray = c.header + 44; c.regionArray = c.spawnArray + (i ? 1 : 5) * 64;
            c.currentCount = c.initialCount = i ? 1 : 5;
            put(controllers[i].data(), c);
            put(image + CONTROLLER_TABLE + i * 16, TableEntry{c.key, 0, reinterpret_cast<uintptr_t>(controllers[i].data())});
            for (unsigned r = 0; r < (i ? 1U : 5U); ++r) {
                put(h + 44 + r * 64, std::uint32_t{302});
                put(h + 44 + r * 64 + 0x1E, static_cast<std::uint16_t>(i ? 50 : 11 + r));
            }
        }
        put(image + CONTROLLER_COUNT, std::int32_t{2});
        put(objects[0].data(), std::uint32_t{302}); objects[0][4] = 4; objects[0][8] = 'M';
        put(objects[1].data(), std::uint32_t{1}); objects[1][4] = 0; objects[1][8] = 'P';
        std::memset(image + CACHE_ROOT, 0, 0x828);
        put(image + CACHE_ROOT + 0x820, base + CACHE_ROOT);
        put(image + CACHE_ROOT, std::int32_t{6}); put(image + CACHE_ROOT + 4, std::int32_t{9});
        put(image + 0x716750, std::uint32_t{7});
        list(false, {}); list(true, {});
        CaptureNativeRecordCatalog(base, layout, *catalog);
    };
    auto capture = [&] { CaptureSelectedOccupancyImpl(base, *catalog, 0, *out, &OccupancyControlRead, &CaptureDiagnosticStamp); };
    seed();
    CaptureNativeSelectedOccupancy(base, *catalog, 0, *out);
    Check(catalog->associationComplete && out->listedComplete && out->noSelectedReferencesAtSamples && out->cacheAvailable &&
          out->cacheEntryCount == 0 && out->controllerStateAvailable && out->state[0].currentCount == 5 &&
          out->state[0].cacheRoom == 6 && out->state[0].cacheAge == 9 && out->context716750Reads[0] &&
          out->context716750[0] == 7 && out->context716750Reads[1],
          "public raw reader captures empty lists complete cache controller and independent context sample");
    Check(!out->globalPendingExcluded && !out->atomic && !out->lifetimeProven && !out->creationAuthority,
          "sampled listed emptiness retains every global lifetime and creation authority false");
    for (unsigned i = 0; i < 5; ++i) { actor(i, 0, i); put(image + CACHE_ROOT + 8 + i * 2, static_cast<std::uint16_t>(11 + i)); }
    list(false, {0,1,2,3,4}); capture();
    Check(out->listedComplete && out->nodes.size() == 5 && out->selectedReferenceCount == 5 && !out->pendingNodeCount &&
          !out->conflictCount && !out->noSelectedReferencesAtSamples && out->cacheAvailable && out->cacheEntryCount == 5 &&
          out->activeReferenceCounts[4] == 1 && out->cacheSelectedCounts[4] == 1 &&
          out->nodes[4].fields[0].recordId == 15 && out->nodes[4].fields[1].recordId == 15,
          "exact active native indices and all cache occurrences retained without inventing ready classification");
    put(actors[1].data() + 0x5C0, uintptr_t{0}); capture();
    Check(out->listedComplete && out->pendingNodeCount == 1 && (out->nodes[1].fields[0].readMask & 8) &&
          !out->nodes[1].fields[0].status && out->selectedReferenceCount == 5,
          "legitimate readable null status remains a selected pending actor not absence");
    put(actors[1].data() + 0x9E8, uintptr_t{0}); put(actors[1].data() + 0x9F0, uintptr_t{0}); capture();
    Check(out->listedComplete && out->pendingNodeCount == 1 && out->unclassifiableNodeCount == 1 &&
          !(out->nodes[1].fields[0].readMask & 64), "selected object with null provenance stays pending without synthetic ID zero read");
    seed();
    put(actors[0].data() + 0x918, reinterpret_cast<uintptr_t>(objects[1].data()));
    list(false, {0}); capture();
    Check(out->listedComplete && out->noSelectedReferencesAtSamples && out->excludedNoncombatCount == 1 &&
          out->nodes[0].excludedKnownNoncombat && !out->pendingNodeCount,
          "checked unrelated noncombat null references are retained and explicitly excluded");
    put(objects[1].data(),std::uint32_t{0});capture();
    Check(out->listedComplete && !out->noSelectedReferencesAtSamples && !out->excludedNoncombatCount &&
          out->unclassifiableNodeCount == 1, "zero-filled object metadata is not a known noncombat exclusion");
    put(objects[1].data(),std::uint32_t{1475});objects[1][4]=255;capture();
    Check(out->listedComplete && !out->noSelectedReferencesAtSamples && !out->excludedNoncombatCount &&
          out->unclassifiableNodeCount == 1, "unknown object type255 cannot qualify a noncombat exclusion");
    objects[1][4]=22;capture();
    Check(out->listedComplete && out->noSelectedReferencesAtSamples && out->excludedNoncombatCount==1,
          "sampled known prize type22 null provenance is retained and excluded");
    objects[1][4]=5;capture();
    Check(out->listedComplete && out->noSelectedReferencesAtSamples && out->excludedNoncombatCount==1,
          "sampled known weapon type5 null provenance is retained and excluded");
    put(actors[0].data() + 0x918, uintptr_t{0}); capture();
    Check(out->listedComplete && !out->noSelectedReferencesAtSamples && out->unclassifiableNodeCount == 1 &&
          !out->excludedNoncombatCount, "unknown null object cannot inherit noncombat exclusion");
    seed(); actor(0,0,0); list(true,{0}); capture();
    Check(out->listedComplete && out->deferredReferenceCounts[0] == 1 && out->conflictCount == 1 && out->nodes[0].deferred,
          "deferred selected node is a retained conflict irrespective of ready list");
    list(false,{0}); capture();
    Check(!out->listedComplete && (out->issues & OccIssue(SelectedOccupancyIssue::CrossList)),
          "same actor in active and deferred lists fails cross-list coverage");
    seed(); actor(0,0,0); actor(1,0,0); list(false,{0,1}); capture();
    Check(out->listedComplete && out->activeReferenceCounts[0] == 2 && out->conflictCount,
          "duplicate selected record references remain counts and conflicts");
    put(actors[1].data() + 0x9E8, reinterpret_cast<uintptr_t>(controllers[1].data())); capture();
    Check(out->nodes[1].selectedIdConflict && out->conflictCount && out->unclassifiableNodeCount,
          "foreign controller with selected raw ID cannot be silently assigned to selected definition");
    seed(); actor(0,0,0); list(false,{0}); put(actors[0].data() + 0x120, std::uint32_t{0x10080000}); capture();
    Check(out->listedComplete && out->nodes[0].nativeLookupSkipped && out->pendingNodeCount == 1 && out->selectedReferenceCount == 1,
          "native lookup filter flags never remove raw selected occupancy");
    seed(); put(definitions[0].data()+44+0x1E,std::uint16_t{0x8011}); CaptureNativeRecordCatalog(base,layout,*catalog);
    actor(0,0,0); list(false,{0}); capture();
    Check(out->listedComplete && out->nodes[0].fields[0].recordId == 0x8011 && out->nodes[0].exactSelectedReference,
          "raw high-bit record ID is retained without signed or handle normalization");
    seed(); put(definitions[0].data()+44+0x1E,std::uint16_t{0}); CaptureNativeRecordCatalog(base,layout,*catalog);
    actor(0,0,0); list(false,{0}); capture();
    Check(out->listedComplete && (out->nodes[0].fields[0].readMask & 64) && out->nodes[0].fields[0].recordId == 0 &&
          !out->cacheSelectedCounts[0], "readable record ID zero is distinct from null pointer and empty cache slot");
    seed(); put(image+CACHE_ROOT+8,std::uint16_t{11}); put(image+CACHE_ROOT+10,std::uint16_t{11});
    put(image+CACHE_ROOT+12,std::uint16_t{0x8011}); capture();
    Check(out->cacheAvailable && out->cacheEntryCount==3 && out->cacheSelectedCounts[0]==2 && out->cacheDuplicateIds &&
          out->cache[1].ids[2]==0x8011, "all cache slots preserve duplicates nonselected and high-bit IDs");
    put(image+CACHE_ROOT+0x820,uintptr_t{0}); capture();
    Check(out->listedComplete && !out->cacheAvailable && out->cache[0].pointerRead && !out->cache[0].bucketInRange,
          "readable null cache bucket is unavailable not an empty cache");

    seed(); actor(0,0,0); list(false,{0});
    put(actors[0].data()+0xA90,handle(reinterpret_cast<uintptr_t>(actors[0].data()))); capture();
    Check(!out->listedComplete && out->nodes.size()==1 && (out->issues & OccIssue(SelectedOccupancyIssue::Cycle)),
          "raw next-handle cycle retains visited node and explicit partial reason");
    seed();actor(0,0,0);list(false,{0});
    const auto overlap = reinterpret_cast<uintptr_t>(actors[0].data()) + 8;
    put(actors[0].data()+0xA90,handle(overlap));put(image+TAIL,overlap);capture();
    Check(!out->listedComplete && (out->issues & OccIssue(SelectedOccupancyIssue::Alias)) && out->nodes.size()==2,
          "overlapping raw actor extents remain retained explicit alias rather than distinct complete nodes");
    seed();
    for(unsigned i=0;i<257;++i) { actor(i,0,0); if(i) put(actors[i-1].data()+0xA90,handle(reinterpret_cast<uintptr_t>(actors[i].data()))); }
    put(image+HEAD,reinterpret_cast<uintptr_t>(actors[0].data()));put(image+TAIL,reinterpret_cast<uintptr_t>(actors[256].data()));capture();
    Check(!out->listedComplete && out->nodes.size()==256 && (out->issues & OccIssue(SelectedOccupancyIssue::Cap)),
          "aggregate 256-node traversal cap never certifies truncated occupancy");

    seed();actor(0,0,0);list(false,{0});
    const uintptr_t node = reinterpret_cast<uintptr_t>(actors[0].data());
    occupancyTrigger=node+0x120;occupancyNth=1;occupancyMutation=node+0x120;capture();
    Check(!out->listedComplete && out->nodes[0].fields[0].flags120==0 && out->nodes[0].fields[1].flags120==1,
          "field readback drift retains both actual values and fails coverage");
    seed();actor(0,0,0);list(false,{0});occupancyTrigger=base+HEAD;occupancyNth=1;occupancyMutation=base+HEAD;capture();
    Check(!out->listedComplete && out->roots[0]!=out->roots[1], "root/tail change across capture cannot remain complete");
    seed();occupancyTrigger=base+HANDLE_REGION_TABLE+63*8;occupancyNth=1;occupancyMutation=occupancyTrigger;capture();
    Check(!out->listedComplete && out->buckets[0][63]!=out->buckets[1][63], "unused bucket63 change still invalidates sampled handle scope");
    seed();occupancyTrigger=reinterpret_cast<uintptr_t>(controllers[0].data());occupancyTriggerSize=0x58;
    occupancyNth=1;occupancyMutation=occupancyTrigger+0x40;capture();
    Check(!out->controllerStateAvailable && out->controllerBytes[0][0x40]!=out->controllerBytes[1][0x40],
          "full 0x58 selected controller readback includes fields beyond catalog prefix");
    seed();occupancyTrigger=base+CACHE_ROOT;occupancyTriggerSize=0x208;occupancyNth=1;occupancyMutation=base+CACHE_ROOT+8;capture();
    Check(out->listedComplete && !out->cacheAvailable && out->cache[0].ids[0]==0 && out->cache[1].ids[0]==1,
          "cache drift is separate from listed coverage and preserves both full arrays");
    seed();actor(0,0,0);list(false,{0});occupancyFailure=node+0x9F0;occupancyFailureNth=2;capture();
    Check(!out->listedComplete && (out->nodes[0].fields[0].readMask&32) && !(out->nodes[0].fields[1].readMask&32),
          "late pointer read failure retains before fields and explicit after availability");
    seed();occupancyTrigger=base+HEAD;occupancyNth=1;occupancyLoadChange=true;capture();
    Check(!out->listedComplete && !out->lifecycleStable && out->catalogs[1].after.load!=out->catalogs[0].before.load,
          "lifecycle drift preserves actual catalog bookends and denies combined sample");
    seed();occupancyTrigger=base+HEAD;occupancyNth=1;occupancyRevisionChange=true;capture();
    Check(!out->listedComplete && !out->mutationStable && out->mutation[0].revision!=out->mutation[1].revision,
          "known mutation during sampling retains old ticket and cannot rebaseline away cancellation");
    seed();occupancyTrigger=base+HEAD;occupancyNth=1;occupancyMutation=reinterpret_cast<uintptr_t>(definitions[1].data())+44+10;capture();
    Check(!out->catalogStable && !out->listedComplete && out->catalogs[1].entries[1].content.records[0][10]==1,
          "unselected ordinary record change invalidates whole-room association scope");
    seed();put(definitions[1].data()+44+0x1E,std::uint16_t{11});capture();
    Check(!out->catalogStable && (out->catalogs[0].issues & Issue(RecordCatalogIssue::DuplicateId)),
          "fresh whole-catalog duplicate ID alias is retained and rejects supplied stale catalog");
    seed();catalog->status=NativeRecordContentStatus::Partial;capture();
    Check(!out->listedComplete && out->nodes.empty() && (out->issues & OccIssue(SelectedOccupancyIssue::Catalog)),
          "partial supplied catalog cannot be promoted by new raw reads");
    seed();g_diagnosticGameThread=0;capture();
    Check(out->issues==OccIssue(SelectedOccupancyIssue::Thread) && out->nodes.empty() && !out->mutation[0].available,
          "foreign/unregistered reader performs no scope-qualified capture");g_diagnosticGameThread=owner;
    seed();actor(0,0,0);list(false,{0});
    auto* protectedRecord=VirtualAlloc(nullptr,0x1000,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    Check(protectedRecord!=nullptr,"owned no-access record page allocated");
    if(protectedRecord) {
        put(actors[0].data()+0x9F0,reinterpret_cast<uintptr_t>(protectedRecord));capture();
        Check(!out->listedComplete && (out->nodes[0].fields[0].readMask&32) && !(out->nodes[0].fields[0].readMask&64) &&
              (out->issues & OccIssue(SelectedOccupancyIssue::RecordRead)),
              "actual PAGE_NOACCESS nonnull record retains pointer and failed ID read independently");
        actor(0,0,0);put(actors[0].data()+0x918,reinterpret_cast<uintptr_t>(protectedRecord));capture();
        Check(!out->listedComplete && (out->nodes[0].fields[0].readMask&4) && !(out->nodes[0].fields[0].readMask&128) &&
              (out->issues & OccIssue(SelectedOccupancyIssue::ObjectRead)),
              "actual unreadable nonnull object keeps pointer without false noncombat exclusion");
        VirtualFree(protectedRecord,0,MEM_RELEASE);
    }
    syntheticLoad=oldLoad;g_diagnosticGameThread=owner;
}

} // namespace

enum class LineageMode { Normal, Nested, Fault, RecordDrift, HeaderDrift, CoverageDrift, FiberSwitch };
LineageMode lineageMode = LineageMode::Normal;
NativeConstructionLineage lineageEntry {}, lineageNestedEntry {};
unsigned lineageOriginalCalls = 0;
bool lineageEntryCopied = false, lineageRestored = false, lineageArgumentsPreserved = true;
uintptr_t lineageController = 0, lineageRecord = 0, lineageMutationAddress = 0;
void* lineageParentFiber = nullptr;
void* lineageChildFiber = nullptr;
bool lineageInheritedCopied = false;
NativeConstructionLineage lineageInherited;
void WINAPI LineageFiberChild(void*) {
    lineageInheritedCopied = CopyNativeConstructionLineage(lineageInherited);
    SwitchToFiber(lineageParentFiber);
}
void* __fastcall LineageOriginal(const void* record, void* controller) {
    ++lineageOriginalCalls;
    lineageArgumentsPreserved = lineageArgumentsPreserved &&
        reinterpret_cast<uintptr_t>(controller) == lineageController &&
        reinterpret_cast<uintptr_t>(record) == lineageRecord;
    NativeConstructionLineage current;
    const bool copied = CopyNativeConstructionLineage(current);
    if (g_constructionDepth == 1) { lineageEntry = current; lineageEntryCopied = copied; }
    else lineageNestedEntry = current;
    if (lineageMode == LineageMode::Nested && g_constructionDepth == 1) {
        TraceEvent nested;
        nested.sequence = 42;
        nested.wrapper = TraceWrapper::Generated;
        const auto before = current.serial;
        RunFactoryWrapper(&nested, record, controller, nullptr);
        NativeConstructionLineage restored;
        lineageRestored = CopyNativeConstructionLineage(restored) && restored.serial == before;
    }
    if (lineageMode == LineageMode::Fault) RaiseException(kDeliberateException, 0, 0, nullptr);
    if (lineageMode == LineageMode::RecordDrift || lineageMode == LineageMode::HeaderDrift)
        *reinterpret_cast<std::uint8_t*>(lineageMutationAddress) ^= 1;
    if (lineageMode == LineageMode::CoverageDrift) ++g_constructionCoverage;
    if (lineageMode == LineageMode::FiberSwitch) SwitchToFiber(lineageChildFiber);
    return &actorToken;
}

bool CatchLineageException(TraceEvent* event) {
    __try {
        RunFactoryWrapper(event, reinterpret_cast<const void*>(lineageRecord),
                          reinterpret_cast<void*>(lineageController), nullptr);
    } __except (GetExceptionCode() == kDeliberateException ?
                EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return true;
    }
    return false;
}

DWORD WINAPI LineageForeignThread(void*) {
    TraceEvent event;
    event.sequence = 81;
    RunFactoryWrapper(&event, reinterpret_cast<const void*>(lineageRecord),
                      reinterpret_cast<void*>(lineageController), nullptr);
    return 0;
}

void ConstructionLineageControls(std::uint8_t* image) {
    const auto oldWrapper = g_originalWrapper;
    const auto oldGenerated = g_originalGenerated;
    const auto oldConfigured = g_constructionConfigured.load();
    const auto oldCoverage = g_constructionCoverage.load();
    const auto oldSerial = g_constructionSerial.load();
    const auto oldDepth = g_constructionDepth;
    auto* const oldScope = g_constructionScope;
    auto* const oldFactoryScope = g_factoryScope;
    const auto oldFactoryDepth = g_factoryDepth;
    g_originalWrapper = &LineageOriginal;
    g_originalGenerated = [](const void* record, void* controller, const float*) -> void* {
        return LineageOriginal(record, controller);
    };
    g_constructionConfigured = true;
    g_constructionCoverage = 101;
    g_constructionSerial = 0;
    g_constructionScope = nullptr; g_constructionDepth = 0;
    g_factoryScope = nullptr; g_factoryDepth = 0;
    RegisterDiagnosticGameThread();
    Controller controller;
    std::array<std::uint8_t, 44 + 5 * 64> definition {};
    definition[0] = 2;
    const std::uint16_t headerId = 30, count = 5;
    std::memcpy(definition.data() + 2, &headerId, 2);
    std::memcpy(definition.data() + 4, &count, 2);
    for (unsigned i = 0; i < 5; ++i) {
        const std::uint32_t object = 302;
        std::memcpy(definition.data() + 44 + i * 64, &object, 4);
        definition[44 + i * 64 + 0x1C] = 2;
    }
    controller.key = 808476514;
    controller.header = reinterpret_cast<uintptr_t>(definition.data());
    controller.spawnArray = controller.header + 44;
    controller.regionArray = controller.spawnArray + 5 * 64;
    lineageController = reinterpret_cast<uintptr_t>(&controller);
    lineageRecord = controller.spawnArray + 2 * 64;
    const TableEntry entry {controller.key, 0, lineageController};
    std::memcpy(image + CONTROLLER_TABLE, &entry, sizeof(entry));
    std::int32_t tableCount = 1;
    std::memcpy(image + CONTROLLER_COUNT, &tableCount, 4);
    TraceEvent event;
    NativeConstructionLineage terminal;
    auto run = [&] {
        event = {};
        terminal = {};
        lineageEntry = {}; lineageNestedEntry = {}; lineageEntryCopied = false;
        event.sequence = 41;
        event.wrapper = TraceWrapper::Fixed;
        event.callerRva = WRAPPER_RETURN_RVA; event.callerRvaAvailable = true;
        const auto callsBefore = lineageOriginalCalls;
        const auto result = RunFactoryWrapper(&event, reinterpret_cast<const void*>(lineageRecord), &controller, nullptr);
        NativeConstructionLineage queued;
        while (PopNativeConstructionLineage(queued)) terminal = queued;
        Check(result == &actorToken && lineageOriginalCalls == callsBefore +
            (lineageMode == LineageMode::Nested ? 2U : 1U) && lineageArgumentsPreserved,
            "construction sidecar preserves actual original arguments/result and once-only wrapper calls");
    };
    lineageMode = LineageMode::Normal; run();
    Check(lineageEntryCopied && lineageEntry.captured && lineageEntry.wrapperSequence == 41 &&
          lineageEntry.controller == lineageController && lineageEntry.record == lineageRecord &&
          lineageEntry.samples[0].recordIndex == 2 && lineageEntry.samples[0].recordMatchesDefinition &&
          !lineageEntry.normalReturn && !lineageEntry.samples[1].controllerRead,
          "resource child copies actual before-only parent by value before native wrapper return");
    Check(terminal.normalReturn && terminal.samples[1].ordinaryAssociationSampled &&
          terminal.sampledIdentityStable && terminal.sampledDefinitionStable &&
          terminal.sampledTableStable && terminal.sampledLifecycleStable &&
          terminal.coverageStable && terminal.traceQueueStable &&
          terminal.serial == lineageEntry.serial && terminal.coverage == lineageEntry.coverage,
          "terminal sidecar joins complete full-definition/table boundary samples by exact parent identity");
    Check(terminal.parentFiberAncestryUnproven && !terminal.fiberContinuityProven &&
          !terminal.continuousModeProven && !terminal.globalPendingExcluded &&
          !terminal.creatorExclusive && !terminal.controllerIncarnationProven &&
          !terminal.atomic && !terminal.creationAuthority,
          "full sampled construction lineage never grants fiber continuity lifetime exclusion or creation");
    NativeConstructionLineage absent;
    Check(!CopyNativeConstructionLineage(absent) && !absent.captured && !g_constructionScope && g_constructionDepth == 0,
          "resource child outside an actual wrapper cannot borrow a terminal or stale parent");
    lineageMode = LineageMode::Nested; run();
    Check(lineageNestedEntry.captured && lineageNestedEntry.wrapper == TraceWrapper::Generated &&
          lineageNestedEntry.wrapperSequence == 42 && lineageNestedEntry.ambiguous &&
          lineageNestedEntry.enclosingThreadSerial == lineageEntry.serial &&
          lineageNestedEntry.serial != lineageEntry.serial && lineageRestored,
          "nested generated wrapper has its own ambiguous lineage and restores the actual outer frame");
    lineageMode = LineageMode::RecordDrift;
    lineageMutationAddress = controller.spawnArray + 4 * 64 + 0x3F; run();
    Check(!terminal.sampledDefinitionStable &&
          terminal.samples[0].records[4][0x3F] != terminal.samples[1].records[4][0x3F] &&
          terminal.samples[0].recordMatchesDefinition && terminal.samples[1].recordMatchesDefinition,
          "change in a different record's unnamed byte invalidates the whole sampled definition");
    lineageMode = LineageMode::HeaderDrift;
    lineageMutationAddress = controller.header + 0xE; run();
    Check(!terminal.sampledDefinitionStable &&
          terminal.samples[0].headerBytes[0xE] != terminal.samples[1].headerBytes[0xE],
          "raw header activation drift is retained rather than hidden by portable content normalization");
    lineageMode = LineageMode::Normal;
    tableCount = 2;
    std::memcpy(image + CONTROLLER_COUNT, &tableCount, 4);
    std::memcpy(image + CONTROLLER_TABLE + 16, &entry, sizeof(entry)); run();
    Check(terminal.samples[0].tableComplete && terminal.samples[0].tableMatches == 2 &&
          !terminal.samples[0].ordinaryAssociationSampled && !terminal.sampledTableStable,
          "duplicate ordinary table association remains raw evidence and cannot become a unique parent");
    tableCount = 1; std::memcpy(image + CONTROLLER_COUNT, &tableCount, 4);
    const std::uint16_t tooMany = 6; std::memcpy(definition.data() + 4, &tooMany, 2); run();
    Check(terminal.samples[0].declaredRecords == 6 && !terminal.samples[0].fiveRecordLayout &&
          !terminal.samples[0].recordReadMask && !terminal.sampledDefinitionStable,
          "six-row definition is unavailable to the fixed five-row diagnostic bound without truncation");
    std::memcpy(definition.data() + 4, &count, 2);
    NativeConstructionLineage parked; parked.captured = parked.candidateThreadParent = true; parked.serial = 777;
    g_constructionScope = &parked; g_constructionDepth = CONSTRUCTION_DEPTH_CAP; run();
    Check(!lineageEntryCopied && terminal.overflow && !terminal.candidateThreadParent &&
          g_constructionScope == &parked && g_constructionDepth == CONSTRUCTION_DEPTH_CAP,
          "depth saturation hides parked ancestry while executing the native original and restoring the parent");
    g_constructionScope = nullptr; g_constructionDepth = 0;
    g_constructionSerial = UINT64_MAX; run();
    Check(terminal.overflow && !terminal.serial && !lineageEntryCopied,
          "saturated construction serial never wraps into a reusable parent identity");
    g_constructionSerial = 20;
    lineageMode = LineageMode::CoverageDrift; run();
    Check(!terminal.coverageStable && terminal.coverage != terminal.coverageAfter &&
          terminal.samples[1].controllerRead,
          "coverage generation drift remains explicit even when sampled native bytes are unchanged");
    lineageMode = LineageMode::Normal;
    const auto expectedRecords = definition;
    const auto expectedControllerBytes = controller;
    controller.header = 0; run();
    Check(terminal.samples[0].controllerRead && !terminal.samples[0].headerRead &&
          terminal.samples[0].recordRead && !terminal.sampledDefinitionStable,
          "failed header read retains successful actual record and controller samples without a definition");
    controller = expectedControllerBytes;
    lineageRecord = controller.spawnArray + 1; run();
    Check(!terminal.samples[0].recordIndexAvailable && !terminal.sampledDefinitionStable,
          "misaligned actual wrapper record cannot inherit a guessed index from the five-row definition");
    lineageRecord = controller.spawnArray + 2 * 64;
    tableCount = 65; std::memcpy(image + CONTROLLER_COUNT, &tableCount, 4); run();
    Check(terminal.samples[0].countBefore == 65 && !terminal.samples[0].tableComplete &&
          !terminal.samples[0].tableReadMask && !terminal.sampledTableStable,
          "over-cap ordinary count is retained without claiming a truncated table inventory");
    tableCount = 1; std::memcpy(image + CONTROLLER_COUNT, &tableCount, 4);
    const bool wasFiber = IsThreadAFiber() != FALSE;
    lineageParentFiber = wasFiber ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
    lineageChildFiber = lineageParentFiber ? CreateFiber(0, LineageFiberChild, nullptr) : nullptr;
    Check(lineageParentFiber && lineageChildFiber, "owned two-fiber construction ancestry control starts");
    if (lineageChildFiber) {
        lineageMode = LineageMode::FiberSwitch; run();
        Check(lineageInheritedCopied && lineageInherited.serial == lineageEntry.serial &&
              lineageInherited.parentFiberAncestryUnproven && !lineageInherited.fiberContinuityProven &&
              !terminal.fiberContinuityProven && !terminal.creationAuthority,
              "real different fiber inherits TLS but neither parent copy nor terminal certifies fiber ancestry");
        DeleteFiber(lineageChildFiber);
    }
    if (lineageParentFiber && !wasFiber) ConvertFiberToThread();
    lineageParentFiber = lineageChildFiber = nullptr;
    lineageMode = LineageMode::Normal;
    definition = expectedRecords;
    NativeConstructionLineage filler;
    for (std::size_t i = 0; i < CONSTRUCTION_QUEUE_CAP; ++i) {
        filler.serial = 100 + i; PublishConstructionLineage(filler);
    }
    const auto beforeDrop = g_constructionDropped.load();
    const auto callsBeforeDrop = lineageOriginalCalls;
    event = {}; event.sequence = 71;
    Check(RunFactoryWrapper(&event, reinterpret_cast<const void*>(lineageRecord), &controller, nullptr) == &actorToken &&
          lineageOriginalCalls == callsBeforeDrop + 1 && g_constructionDropped.load() == beforeDrop + 1 &&
          !g_constructionScope && !g_constructionDepth,
          "full independent construction queue drops only diagnostics and preserves native call and scope cleanup");
    std::size_t retained = 0;
    bool preservedOrder = true;
    while (PopNativeConstructionLineage(terminal)) {
        preservedOrder = preservedOrder && terminal.serial == 100 + retained;
        ++retained;
    }
    Check(retained == CONSTRUCTION_QUEUE_CAP && preservedOrder,
          "construction queue pressure cannot overwrite retained earlier parent receipts");
    AcquireSRWLockExclusive(&g_constructionLock);
    const auto lockedDropBefore = g_constructionDropped.load();
    event = {}; event.sequence = 72;
    RunFactoryWrapper(&event, reinterpret_cast<const void*>(lineageRecord), &controller, nullptr);
    ReleaseSRWLockExclusive(&g_constructionLock);
    Check(g_constructionDropped.load() == lockedDropBefore + 1 && !PopNativeConstructionLineage(terminal) &&
          !g_constructionScope && !g_constructionDepth,
          "contended construction publication never waits or retries inside the native wrapper boundary");
    lineageMode = LineageMode::Fault; event = {}; event.sequence = 51;
    const auto faultCalls = lineageOriginalCalls;
    const bool faultCaught = CatchLineageException(&event);
    const bool faultTerminal = PopNativeConstructionLineage(terminal);
    Check(faultCaught && faultTerminal && lineageOriginalCalls == faultCalls + 1 &&
          !g_constructionScope && !g_constructionDepth && terminal.unwound &&
          !terminal.normalReturn && !terminal.samples[1].controllerRead,
          "native SEH propagates once and terminal parent marks unwind without post-fault native reads");
    TraceEvent interrupted;
    Check(PopTraceEvent(interrupted) && terminal.wrapperSequence == 51 &&
          terminal.unwound && !terminal.normalReturn,
          "existing bounded trace queue retains an interrupted construction sidecar");
    lineageMode = LineageMode::Normal;
    g_constructionConfigured = false; g_constructionScope = &parked; run();
    Check(!terminal.captured && g_constructionScope == &parked,
          "disabled sidecar leaves original dispatch intact and hides ancestry during a nested native call");
    g_constructionScope = nullptr;
    g_constructionConfigured = true;
    lineageMode = LineageMode::Normal;
    const auto rolesBefore = roleReads.load(), transitionsBefore = transitionReads.load(), loadsBefore = loadReads.load();
    HANDLE foreignThread = CreateThread(nullptr, 0, LineageForeignThread, nullptr, 0, nullptr);
    Check(foreignThread != nullptr, "owned foreign-thread construction control starts");
    if (foreignThread) {
        const auto waitResult = WaitForSingleObject(foreignThread, 5000);
        Check(waitResult == WAIT_OBJECT_0, "owned foreign-thread construction control completes");
        CloseHandle(foreignThread);
        Check(PopNativeConstructionLineage(terminal) && terminal.wrapperSequence == 81 && terminal.normalReturn &&
              !terminal.candidateThreadParent && !terminal.samples[0].controllerRead &&
              !terminal.samples[1].controllerRead && roleReads.load() == rolesBefore &&
              transitionReads.load() == transitionsBefore && loadReads.load() == loadsBefore,
              "foreign native wrapper preserves return but cannot read owner metadata or borrow thread ancestry");
    }
    g_originalWrapper = oldWrapper; g_originalGenerated = oldGenerated;
    g_constructionConfigured = oldConfigured; g_constructionCoverage = oldCoverage; g_constructionSerial = oldSerial;
    g_constructionScope = oldScope; g_constructionDepth = oldDepth;
    g_factoryScope = oldFactoryScope; g_factoryDepth = oldFactoryDepth;
}

int main() {
    // No game executable or process is opened. The implementation's native
    // offset reads resolve entirely inside this zeroed, test-owned allocation.
    constexpr std::size_t imageSize = HANDLE_REGIONS_RVA + 0x1000;
    auto* image = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, imageSize,
                                                        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!image) { std::cerr << "FAIL: synthetic image allocation\n"; return 1; }
    g_exeBase = reinterpret_cast<uintptr_t>(image);
    g_imageSize = static_cast<std::uint32_t>(imageSize);
    g_role = &OffRole;
    g_original = &SyntheticOriginal;
    g_originalWrapper = &SyntheticWrapper;
    g_originalGenerated = &SyntheticGenerated;
    g_originalDispatcher = &SyntheticDispatcher;
    g_originalScript = &SyntheticScript;
    g_originalAdmission = &SyntheticAdmission;
    g_originalAllocation = &SyntheticAllocation;
    g_factoryInstalled = g_factoryVerified = FactoryAllHooks;
    g_factoryCoverageSerial = 1;
    image[kh2coop::offsets::NOW] = 5;
    image[kh2coop::offsets::NOW + 1] = 6;
    // Unknown thread: real observer must call original once, retain raw NOW,
    // and avoid both the game-owned role callback and plain Warp serial reads.
    fixedResult = nullptr;
    factoryMode = FactoryTestMode::Reject;
    admissionOriginalResult = 0;
    Check(!IsDiagnosticGameThread() && ObserveWrapper(nullptr, nullptr, nullptr, TraceWrapper::Fixed,
          g_exeBase + WRAPPER_RETURN_RVA) == nullptr && wrapperCalls == 1,
          "unregistered observer still invokes the original exactly once");
    TraceEvent unregistered;
    Check(PopTraceEvent(unregistered) && unregistered.wrapperComplete && !unregistered.roleAvailable &&
          !unregistered.stampAvailable && !unregistered.postStampAvailable &&
          unregistered.stamp.location[0] == 5 && unregistered.postStamp.location[1] == 6 &&
          unregistered.stamp.transition == 0 && unregistered.stamp.load == 0 &&
          roleReads.load() == 0 && transitionReads.load() == 0 && loadReads.load() == 0,
          "unregistered diagnostics retain raw NOW but never query role or native serials");
    Check(!unregistered.factory.eligible && !unregistered.factory.complete &&
          unregistered.factory.admissionCalls == 0 && admissionOriginalCalls == 1 &&
          admissionArgumentBits == inputWeightBits && g_factoryScope == nullptr && g_factoryDepth == 0,
          "unregistered factory executes genuine child once without reading operands or retaining scope");
    factoryMode = FactoryTestMode::Off;
    fixedResult = &actorToken;
    wrapperCalls = 0;
    RegisterDiagnosticGameThread();
    Check(IsDiagnosticGameThread(), "known frame owner registers diagnostic thread once");
    std::array<std::uint8_t, 0x2C> header {};
    header[0] = 2;
    Controller controller;
    controller.header = reinterpret_cast<uintptr_t>(header.data());
    controller.key = 123;
    alignas(16) float point[4] {1, 2, 3, 1};
    expectedController = reinterpret_cast<uintptr_t>(&controller);
    expectedPoint = point;

    faultUpdate = true;
    Check(CatchTraceException(&controller, point), "native SEH propagates through trace finally to outer handler");
    Check(updateCalls == 1 && argumentsPreserved, "interrupted original update runs exactly once with original arguments");
    Check(!g_traceTick.active && g_traceTick.count == 0 && g_traceTick.controller == 0 &&
          !g_traceTick.stampAvailable && g_traceNestedDepth == 0, "native unwind clears TLS scope and depth");
    TraceEvent interrupted;
    Check(PopTraceEvent(interrupted), "interrupted buffered event remains explicitly observable");
    Check(interrupted.outcome == TraceOutcome::Unavailable && !interrupted.tickComplete &&
          !interrupted.postStampAvailable && !interrupted.lifecycleStable &&
          interrupted.postStamp.transition == 0 && interrupted.postStamp.location == TraceStamp{}.location &&
          !interrupted.tickAfter.controllerAvailable && !interrupted.tickAfter.cacheAvailable &&
          interrupted.tickAfter.header == 0 && interrupted.tickAfter.flags == 0 &&
          interrupted.tickAfter.cacheIds == TraceState{}.cacheIds,
          "interrupted event cannot expose poisoned poststate as completed evidence");
    Check(interrupted.actor == reinterpret_cast<uintptr_t>(&actorToken) &&
          interrupted.stamp.transition == 17 && interrupted.stamp.load == 29,
          "cleanup retains captured local actor and native pre-stamp");
    TraceEvent extra;
    Check(!PopTraceEvent(extra), "interrupted event is published exactly once");

    faultUpdate = false;
    Check(!CatchTraceException(&controller, point), "subsequent normal trace scope completes");
    Check(updateCalls == 2 && argumentsPreserved, "subsequent original runs exactly once");
    TraceEvent completed;
    Check(PopTraceEvent(completed) && completed.tickComplete && completed.lifecycleStable &&
          completed.outcome == TraceOutcome::Observed && completed.tickAfter.controllerAvailable &&
          completed.tickAfter.flags == 0x18 && completed.tickAfter.cooldown == 8.0f,
          "subsequent scope captures real synthetic post-update state");
    Check(!g_traceTick.active && g_traceTick.count == 0 && g_traceNestedDepth == 0 && !PopTraceEvent(extra),
          "normal completion also clears scope and publishes once");

    // A nested exception may be caught inside the enclosing native update; its
    // cleanup must restore that enclosing depth without discarding its buffer.
    g_original = &NestedOriginal;
    g_traceTick.active = true;
    g_traceTick.count = 1;
    g_traceNestedDepth = 3;
    faultUpdate = true;
    Check(CatchNestedException(&controller, point), "nested native SEH reaches outer handler");
    Check(updateCalls == 3 && depthSeen == 4 && g_traceNestedDepth == 3 &&
          g_traceTick.active && g_traceTick.count == 1,
          "nested unwind restores prior depth and preserves enclosing scope");
    faultUpdate = false;
    Check(!CatchNestedException(&controller, point) && updateCalls == 4 && depthSeen == 4 &&
          g_traceNestedDepth == 3, "subsequent nested normal update runs once and restores depth");
    FinishTraceScope(false, 0);

    Check(CallOriginalWrapper(nullptr, nullptr) == &actorToken && wrapperCalls == 1,
          "wrapper helper returns the genuine synthetic actor after one original call");
    faultWrapper = true;
    Check(CatchWrapperException() && wrapperCalls == 2 && g_traceFaults.load() == 1 &&
          g_traceException.load() == kDeliberateException,
          "wrapper records native fault then continues exception search without retry");

    // Verify abnormal cleanup still resets TLS when its diagnostic queue is full.
    for (std::size_t i = 0; i < TRACE_QUEUE_CAP; ++i) PublishTrace(completed);
    const auto droppedBefore = g_traceDropped.load();
    g_original = &SyntheticOriginal;
    faultUpdate = true;
    Check(CatchTraceException(&controller, point) && updateCalls == 5 &&
          g_traceDropped.load() == droppedBefore + 1 && !g_traceTick.active &&
          g_traceTick.count == 0 && g_traceNestedDepth == 0,
          "queue pressure counts interrupted-event loss while restoring TLS");
    unsigned drained = 0;
    while (PopTraceEvent(extra)) ++drained;
    Check(drained == TRACE_QUEUE_CAP, "queue pressure never overwrites retained events");

    Check(CallOriginalGenerated(expectedRecord, &controller, point) == &actorToken && generatedCalls == 1 &&
          generatedArgumentsPreserved, "generated wrapper preserves record/controller/point arguments and genuine return");
    faultGenerated = true;
    Check(CatchGeneratedException() && generatedCalls == 2 && generatedArgumentsPreserved &&
          g_traceFaults.load() == 2 && g_traceException.load() == kDeliberateException,
          "generated wrapper fault propagates without retry or return substitution");
    faultGenerated = false;
    faultWrapper = false;

    // Complete local synthetic returned actor, descriptor and record. Deliberately
    // use a dynamic alias and generated-position mode excluded by activation scope.
    // Creation diagnostics must still observe the actual returned combat actor.
    std::array<std::uint8_t, 0xA00> combatActor {};
    std::array<std::uint8_t, 0x60> objentry {};
    std::array<std::int32_t, 2> status {100, 150};
    std::array<std::uint8_t, 0x6C> descriptor {};
    auto storeValue = [](std::uint8_t* bytes, std::size_t offset, const auto& value) {
        std::memcpy(bytes + offset, &value, sizeof(value));
    };
    descriptor[0] = 9; // type9 proves the trace does not require activation type2
    storeValue(descriptor.data(), 4, std::uint16_t {1});
    storeValue(descriptor.data(), 0x2C, std::uint32_t {0x236});
    descriptor[0x2C + 0x1C] = 0;
    descriptor[0x2C + 0x1D] = 1;
    storeValue(descriptor.data(), 0x2C + 0x1E, std::uint16_t {44});
    controller.header = reinterpret_cast<uintptr_t>(descriptor.data());
    controller.spawnArray = controller.header + 0x2C;
    controller.regionArray = controller.spawnArray + 0x40;
    expectedRecord = descriptor.data() + 0x2C;
    storeValue(objentry.data(), 0, std::uint32_t {0x309});
    objentry[4] = kh2coop::offsets::objentry::TYPE_MOB;
    objentry[8] = 'M';
    storeValue(combatActor.data(), 0x918, reinterpret_cast<uintptr_t>(objentry.data()));
    storeValue(combatActor.data(), 0x5C0, reinterpret_cast<uintptr_t>(status.data()));
    storeValue(combatActor.data(), 0x9E8, expectedController);
    storeValue(combatActor.data(), 0x9F0, reinterpret_cast<uintptr_t>(expectedRecord));
    generatedResult = fixedResult = combatActor.data();
    const auto beforeGenerated = generatedCalls;
    Check(ObserveWrapper(expectedRecord, &controller, point, TraceWrapper::Generated,
                         g_exeBase + 0x3FE989) == combatActor.data() && generatedCalls == beforeGenerated + 1 &&
          generatedArgumentsPreserved, "all-caller generated observer passes through once with exact original arguments");
    TraceEvent generated;
    Check(PopTraceEvent(generated) && generated.wrapper == TraceWrapper::Generated &&
          generated.wrapperComplete && generated.wrapperOutcome == TraceOutcome::Observed &&
          generated.outcome == TraceOutcome::Observed && generated.roleAvailable &&
          !generated.enclosingTick && !generated.tickComplete &&
          generated.wrapperBefore.controllerAvailable && generated.wrapperAfter.controllerAvailable &&
          !generated.tickBefore.controllerAvailable && !generated.tickAfter.controllerAvailable,
          "successful outside-tick wrapper evidence stays separate from unknown enclosing tick");
    Check(generated.recordAvailable && generated.recordIndexAvailable && generated.recordIndex == 0 &&
          generated.objectId == 0x236 && generated.actorObjectId == 0x309 && !generated.objectIdMatchesRecord &&
          generated.nativeRecordId == 44 && generated.wrapperBefore.nativeType == 9 &&
          generated.generatedPointAvailable && generated.generatedPoint[2] == point[2] && generated.hp == 100 &&
          generated.callerRvaAvailable && generated.callerRva == 0x3FE989,
          "type9 dynamic alias/generated record captures raw identity and actual actor without type2 gate");

    controller.regionArray = 0; // readable record, unproven membership
    const auto beforeFixed = wrapperCalls;
    Check(ObserveWrapper(expectedRecord, &controller, nullptr, TraceWrapper::Fixed, 0) == combatActor.data() &&
          wrapperCalls == beforeFixed + 1 && PopTraceEvent(extra) && extra.recordAvailable &&
          !extra.recordIndexAvailable && !extra.callerRvaAvailable && extra.wrapperOutcome == TraceOutcome::Observed,
          "unknown caller and unproven index remain explicit without hiding fixed combat return");
    controller.regionArray = controller.spawnArray + 0x40;

    int scriptArgs = 7, regionToken = 9;
    expectedScriptArgs = &scriptArgs;
    expectedRegion = &regionToken;
    Check(RunScriptScope(&scriptArgs, g_exeBase + 0x551111) == kDispatcherResult && scriptCalls == 1 &&
          dispatcherCalls == 1 && dispatcherArgumentsPreserved && scriptArgumentsPreserved &&
          !g_scriptScope.active && !g_dispatcherScope.active,
          "script/dispatcher scope preserves native arguments and full 64-bit RAX exactly once");
    Check(PopTraceEvent(extra) && extra.enclosingScript42DC10 && extra.enclosingDispatcher &&
          extra.scriptSequence != 0 && extra.dispatcherSequence != 0 &&
          extra.scriptCallerRvaAvailable && extra.scriptCallerRva == 0x551111 &&
          !extra.dispatcherCallerRvaAvailable && extra.dispatcherCallerRva == 0 && !extra.enclosingTick,
          "explicit script TLS identifies ancestry while real dispatcher hook sees an unknown DLL return address");

    g_dispatcherScope = {true, 111, 222, 333, 444};
    g_scriptScope = {true, 555, 666};
    faultDispatcher = true;
    Check(CatchScriptException(&scriptArgs) && scriptCalls == 2 && dispatcherCalls == 2 &&
          g_dispatcherScope.active && g_dispatcherScope.controller == 111 && g_dispatcherScope.region == 222 &&
          g_dispatcherScope.caller == 333 && g_dispatcherScope.sequence == 444 &&
          g_scriptScope.active && g_scriptScope.caller == 555 && g_scriptScope.sequence == 666,
          "nested dispatcher/script SEH propagates and restores every prior TLS scope field");
    Check(PopTraceEvent(extra) && extra.wrapperComplete && extra.wrapperOutcome == TraceOutcome::Observed &&
          !extra.tickComplete, "later dispatcher failure does not invent completion or erase a genuine wrapper return");
    g_dispatcherScope = {};
    g_scriptScope = {};
    faultDispatcher = false;
    Check(RunScriptScope(&scriptArgs, g_exeBase + 0x551111) == kDispatcherResult &&
          scriptCalls == 3 && dispatcherCalls == 3 && !g_scriptScope.active && !g_dispatcherScope.active &&
          PopTraceEvent(extra), "subsequent normal dispatcher/script scopes work after unwind");

    TraceStamp explicitStamp;
    Check(CaptureDiagnosticStamp(g_exeBase, explicitStamp) && explicitStamp.transition == 17 &&
          CaptureDiagnosticState(g_exeBase, expectedController).nativeType == 9 &&
          !CaptureDiagnosticStamp(0, explicitStamp) && !CaptureDiagnosticState(0, expectedController).controllerAvailable,
          "independent explicit-base diagnostics expose checked state and unavailable invalid base");

    const std::uint32_t limitBits = 0x41200000, usedBits = 0x40C00000;
    storeValue(descriptor.data(), 0x2C, std::uint32_t {0x309});
    std::memcpy(image + ADMISSION_LIMIT_RVA, &limitBits, sizeof(limitBits));
    std::memcpy(image + ADMISSION_USED_RVA, &usedBits, sizeof(usedBits));
    auto observeFactory = [&](FactoryTestMode mode, void* expectedReturn) {
        factoryMode = mode;
        const auto calls = wrapperCalls;
        const auto result = ObserveWrapper(expectedRecord, &controller, nullptr, TraceWrapper::Fixed,
                                           g_exeBase + WRAPPER_RETURN_RVA);
        TraceEvent event;
        const unsigned expectedCalls = mode == FactoryTestMode::Nested ? 2U : 1U;
        Check(result == expectedReturn && wrapperCalls == calls + expectedCalls && factoryArgumentsPreserved &&
              PopTraceEvent(event) && !g_factoryScope && g_factoryDepth == 0,
              "factory wrapper preserves original arguments/result once and restores scope");
        return event;
    };
    admissionOriginalResult = 0;
    const auto allocationsBeforeReject = allocationOriginalCalls;
    storeValue(descriptor.data(), 0x2C, std::uint32_t {0x236});
    auto aliasFactory = observeFactory(FactoryTestMode::Reject, nullptr);
    Check(aliasFactory.factory.complete && aliasFactory.factory.admissionReturned &&
          aliasFactory.factory.admissionResult == 0 && aliasFactory.factory.outcome == FactoryOutcome::Unknown,
          "remapped record retains genuine rejection evidence without claiming an ordinary direct factory cause");
    storeValue(descriptor.data(), 0x2C, std::uint32_t {0x309});
    auto factory = observeFactory(FactoryTestMode::Reject, nullptr);
    Check(factory.factory.complete && factory.factory.outcome == FactoryOutcome::AdmissionRejected &&
          factory.factory.admissionCalls == 1 && factory.factory.admissionReturned &&
          !factory.factory.allocationCalls && allocationOriginalCalls == allocationsBeforeReject &&
          factory.factory.weightBits == inputWeightBits && admissionArgumentBits == inputWeightBits &&
          factory.factory.operandMask == 15 && factory.factory.limitBeforeBits == limitBits &&
          factory.factory.limitAfterBits == limitBits && factory.factory.usedBeforeBits == usedBits &&
          factory.factory.usedAfterBits == usedBits,
          "executed admission rejection preserves raw NaN argument and four boundary operand samples");
    admissionOriginalResult = 0xA5;
    allocationOriginalResult = nullptr;
    const auto admissionsBeforeAllocate = admissionOriginalCalls, allocationsBeforeAllocate = allocationOriginalCalls;
    factory = observeFactory(FactoryTestMode::Allocate, nullptr);
    Check(factory.factory.outcome == FactoryOutcome::Type4AllocationFailed && factory.factory.complete &&
          factory.factory.admissionResult == 0xA5 && factory.factory.allocationReturned &&
          factory.factory.allocationCalls == 1 && factory.factory.allocationSize == 0xD50 &&
          allocationArgument == 0xD50 && !factory.factory.allocationResult &&
          admissionOriginalCalls == admissionsBeforeAllocate + 1 && allocationOriginalCalls == allocationsBeforeAllocate + 1,
          "genuine AL and allocator null distinguish allocation failure after one original call each");
    allocationOriginalResult = combatActor.data();
    factory = observeFactory(FactoryTestMode::Allocate, combatActor.data());
    Check(factory.factory.outcome == FactoryOutcome::AllocationPassed &&
          factory.factory.allocationResult == reinterpret_cast<uintptr_t>(combatActor.data()) &&
          factory.wrapperComplete && factory.actorAvailable,
          "successful allocation retains genuine local pointer separately from actor metadata");

    DWORD oldProtect = 0, ignoredProtect = 0;
    const bool protectedOperands = VirtualProtect(image + 0x2A0F000, 0x1000, PAGE_NOACCESS, &oldProtect) != FALSE;
    Check(protectedOperands, "test-owned admission operand page can become unreadable");
    if (protectedOperands) {
        factory = observeFactory(FactoryTestMode::Allocate, combatActor.data());
        Check(factory.factory.complete && factory.factory.outcome == FactoryOutcome::AllocationPassed &&
              factory.factory.operandMask == 0,
              "unreadable operand snapshots do not erase genuine executed admission/allocation returns");
        Check(VirtualProtect(image + 0x2A0F000, 0x1000, oldProtect, &ignoredProtect) != FALSE,
              "test-owned operand page protection restored");
    }
    factory = observeFactory(FactoryTestMode::WrongCaller, combatActor.data());
    Check(factory.factory.complete && factory.factory.outcome == FactoryOutcome::Unknown &&
          !factory.factory.admissionCalls && !factory.factory.allocationCalls,
          "unmatched native caller executes normally without fabricating observed child coverage");
    factory = observeFactory(FactoryTestMode::WrongSize, combatActor.data());
    Check(factory.factory.outcome == FactoryOutcome::Ambiguous && factory.factory.allocationSize == 0xD48 &&
          allocationArgument == 0xD48, "unexpected allocator size is preserved and explicitly ambiguous");
    factory = observeFactory(FactoryTestMode::Repeated, combatActor.data());
    Check(factory.factory.admissionCalls == 2 && factory.factory.outcome == FactoryOutcome::Ambiguous,
          "multiple factory admission calls cannot silently overwrite one another");
    factory = observeFactory(FactoryTestMode::Overflow, combatActor.data());
    Check(factory.factory.admissionCalls == 0xFFFFU && factory.factory.countOverflow &&
          !factory.factory.complete && factory.factory.outcome == FactoryOutcome::Ambiguous,
          "child counter saturates with explicit incomplete ambiguous coverage");
    g_factoryInstalled = FactoryAdmissionHook;
    factory = observeFactory(FactoryTestMode::Allocate, combatActor.data());
    Check(factory.factory.coverageMask == FactoryAdmissionHook && !factory.factory.complete &&
          factory.factory.admissionCalls == 1 && factory.factory.allocationCalls == 0 &&
          factory.factory.outcome == FactoryOutcome::Unknown, "partial child hook installation cannot classify a null cause");
    g_factoryInstalled = FactoryAllHooks;
    factory = observeFactory(FactoryTestMode::CoverageChange, combatActor.data());
    Check(!factory.factory.complete && factory.factory.outcome == FactoryOutcome::Unknown,
          "coverage serial change during native wrapper invalidates completion");
    factory = observeFactory(FactoryTestMode::Nested, combatActor.data());
    Check(nestedScopeRestored && factory.factory.admissionCalls == 1 && factory.factory.allocationCalls == 1 &&
          nestedFactoryEvent.factory.admissionCalls == 1 && nestedFactoryEvent.factory.depth == 1 &&
          nestedFactoryEvent.factory.outcome == FactoryOutcome::AllocationPassed,
          "nested wrapper gets independent child counts and restores parent collection scope");

    factoryMode = FactoryTestMode::Allocate;
    TraceEvent faultedFactory;
    faultAdmission = true;
    const auto faultAdmissionBefore = admissionOriginalCalls;
    Check(CatchFactoryException(&faultedFactory) && admissionOriginalCalls == faultAdmissionBefore + 1 &&
          !g_factoryScope && g_factoryDepth == 0 && PopTraceEvent(extra) && extra.factory.unwound &&
          extra.factory.admissionFault && !extra.factory.admissionReturned && !extra.wrapperComplete &&
          !extra.factory.complete && !extra.postStampAvailable && !extra.tickComplete,
          "admission SEH propagates once and publishes captured POD without poststate or stale TLS");
    faultAdmission = false;
    faultAllocation = true;
    faultedFactory = {};
    const auto faultAllocationBefore = allocationOriginalCalls;
    Check(CatchFactoryException(&faultedFactory) && allocationOriginalCalls == faultAllocationBefore + 1 &&
          !g_factoryScope && g_factoryDepth == 0 && PopTraceEvent(extra) && extra.factory.unwound &&
          extra.factory.admissionReturned && extra.factory.allocationFault && !extra.factory.allocationReturned &&
          !extra.factory.complete, "allocator SEH propagates once with explicit interrupted child outcome");
    faultAllocation = false;
    factoryMode = FactoryTestMode::NestedFault;
    TraceEvent recoveredOuter;
    recoveredOuter.recordAvailable = true;
    recoveredOuter.objectId = 0x309;
    Check(RunFactoryWrapper(&recoveredOuter, expectedRecord, &controller, nullptr) == combatActor.data() &&
          nestedScopeRestored && !g_factoryScope && g_factoryDepth == 0 &&
          recoveredOuter.factory.outcome == FactoryOutcome::AllocationPassed &&
          recoveredOuter.factory.admissionCalls == 1 && PopTraceEvent(extra) && extra.factory.unwound &&
          extra.factory.depth == 1, "caught nested native fault restores outer scope without mixing child facts");
    factory = observeFactory(FactoryTestMode::Allocate, combatActor.data());
    Check(factory.factory.complete && factory.factory.outcome == FactoryOutcome::AllocationPassed,
          "normal factory observation remains usable after nested native unwind");

    FactoryPredicates parkedParent;
    g_factoryScope = &parkedParent;
    g_factoryDepth = FACTORY_DEPTH_CAP;
    TraceEvent depthLimited;
    Check(RunFactoryWrapper(&depthLimited, expectedRecord, &controller, nullptr) == combatActor.data() &&
          !depthLimited.factory.eligible && depthLimited.factory.countOverflow &&
          depthLimited.factory.outcome == FactoryOutcome::Ambiguous &&
          !depthLimited.factory.admissionCalls && !parkedParent.admissionCalls &&
          g_factoryScope == &parkedParent && g_factoryDepth == FACTORY_DEPTH_CAP,
          "depth cap suppresses child attribution and restores parked parent without overwriting it");
    g_factoryScope = nullptr;
    g_factoryDepth = 0;
    for (std::size_t i = 0; i < TRACE_QUEUE_CAP; ++i) PublishTrace(completed);
    const auto factoryDropsBefore = g_traceDropped.load();
    faultAdmission = true;
    faultedFactory = {};
    Check(CatchFactoryException(&faultedFactory) && g_traceDropped.load() == factoryDropsBefore + 1 &&
          !g_factoryScope && g_factoryDepth == 0,
          "interrupted factory reports queue loss and restores scope even under full queue pressure");
    faultAdmission = false;
    drained = 0;
    while (PopTraceEvent(extra)) ++drained;
    Check(drained == TRACE_QUEUE_CAP, "factory unwind cannot overwrite retained queued events");
    float hookWeight = 0;
    std::memcpy(&hookWeight, &inputWeightBits, sizeof(hookWeight));
    const auto outsideAdmissionCalls = admissionOriginalCalls, outsideAllocationCalls = allocationOriginalCalls;
    constexpr std::size_t wideAllocationSize = 0x100000D50ULL;
    Check(HookedAdmission(hookWeight) == admissionOriginalResult &&
          HookedAllocation(wideAllocationSize) == combatActor.data() &&
          admissionOriginalCalls == outsideAdmissionCalls + 1 &&
          allocationOriginalCalls == outsideAllocationCalls + 1 &&
          allocationArgument == wideAllocationSize && admissionArgumentBits == inputWeightBits &&
          !g_factoryScope, "real child hook entries preserve full arguments/results for unmatched DLL callers");

    // Child hooks execute on the foreign thread, but that thread has no eligible
    // collection scope and must not read budget operands or native serials.
    factoryMode = FactoryTestMode::Allocate;
    const auto admissionBeforeForeign = admissionOriginalCalls, allocationBeforeForeign = allocationOriginalCalls;

    const auto rolesBeforeForeign = roleReads.load();
    const auto transitionsBeforeForeign = transitionReads.load(), loadsBeforeForeign = loadReads.load();
    const auto generatedBeforeForeign = generatedCalls;
    HANDLE thread = CreateThread(nullptr, 0, &ForeignObservation, nullptr, 0, nullptr);
    Check(thread != nullptr, "headless foreign observer thread starts");
    if (!thread) return 1;
    const auto waitResult = WaitForSingleObject(thread, 5000);
    Check(waitResult == WAIT_OBJECT_0, "headless foreign observer thread completes");
    if (waitResult != WAIT_OBJECT_0) { CloseHandle(thread); return 1; }
    CloseHandle(thread);
    Check(!foreignWasGameThread && foreignRegistrationRejected && IsDiagnosticGameThread() &&
          foreignReturnPreserved && generatedCalls == generatedBeforeForeign + 1 && generatedArgumentsPreserved &&
          roleReads.load() == rolesBeforeForeign && transitionReads.load() == transitionsBeforeForeign &&
          loadReads.load() == loadsBeforeForeign,
          "foreign wrapper preserves original once but cannot register or invoke game-owned callbacks");
    Check(PopTraceEvent(extra) && extra.wrapperComplete && extra.actorAvailable &&
          extra.actorObjectId == 0x309 && extra.recordAvailable && !extra.roleAvailable &&
          !extra.stampAvailable && !extra.postStampAvailable && extra.stamp.transition == 0 &&
          extra.stamp.load == 0 && extra.stamp.location[0] == 5 && extra.postStamp.location[1] == 6 &&
          extra.outcome == TraceOutcome::Unavailable,
          "foreign trace keeps raw actor/record/NOW evidence with role and serial availability false");
    Check(!extra.factory.eligible && !extra.factory.complete && !extra.factory.admissionCalls &&
          !extra.factory.allocationCalls && !extra.factory.operandMask &&
          admissionOriginalCalls == admissionBeforeForeign + 1 && allocationOriginalCalls == allocationBeforeForeign + 1,
          "foreign child calls preserve original behavior without operand reads or claimed factory evidence");
    factoryMode = FactoryTestMode::Off;
    RecordCatalogControls(image);
    GeometryControls(image);
    Check(hookCalls == 1, "headless test uses one rejecting MinHook seam and never installs or tears down native hooks");
    KnownMutationControls(image);
    SelectedOccupancyControls(image);
    ConstructionLineageControls(image);

    g_original = nullptr;
    g_originalWrapper = nullptr;
    g_originalGenerated = nullptr;
    g_originalDispatcher = nullptr;
    g_originalScript = nullptr;
    g_originalAdmission = nullptr;
    g_originalAllocation = nullptr;
    g_role = nullptr;
    g_exeBase = 0;
    g_imageSize = 0;
    VirtualFree(image, 0, MEM_RELEASE);
    return errors ? 1 : 0;
}
