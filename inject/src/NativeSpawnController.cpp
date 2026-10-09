#include "NativeSpawnController.hpp"
#include "NativePopulationAuthority.hpp"
#include "NativeResourceTrace.hpp"
#include "NativeTraceFiber.hpp"
#include "kh2coop/NativeRecordContent.hpp"
#include "Warp.hpp"
#include "kh2coop/KH2Offsets.hpp"

#include <Windows.h>
#include <MinHook.h>
#include <intrin.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

namespace kh2coop::inject::spawncontroller {
namespace {

constexpr uintptr_t UPDATE_RVA = 0x3FF000;
constexpr uintptr_t CALL_RVA = 0x3A5056;
constexpr uintptr_t RETURN_RVA = 0x3A5063;
constexpr uintptr_t LOOKUP_RVA = 0x3E0F10;
constexpr uintptr_t COMPARE_RVA = 0x1E2300;
constexpr uintptr_t WRAPPER_RVA = 0x3FE590;
constexpr uintptr_t WRAPPER_CALL_RVA = 0x3FE829;
constexpr uintptr_t WRAPPER_RETURN_RVA = 0x3FE83F;
constexpr uintptr_t GENERATED_RVA = 0x3FE650;
constexpr uintptr_t DISPATCHER_RVA = 0x3FE320;
constexpr uintptr_t SCRIPT_RVA = 0x42DC10;
constexpr uintptr_t ADMISSION_RVA = 0x3A1F00, ADMISSION_RETURN_RVA = 0x3DFA0B;
constexpr uintptr_t ALLOCATION_RVA = 0x152430, ALLOCATION_RETURN_RVA = 0x3DFA7D;
constexpr uintptr_t ADMISSION_LIMIT_RVA = 0x2A0F7DC, ADMISSION_USED_RVA = 0x2A0F830;
constexpr std::uint8_t kAdmissionBytes[] = {
    0xF3,0x0F,0x10,0x0D,0xD4,0xD8,0x66,0x02,0xF3,0x0F,0x5C,0x0D,0x20,0xD9,0x66,0x02,
    0x0F,0x2F,0xC8,0x0F,0x93,0xC0,0xC3};
constexpr std::uint8_t kAdmissionCallBytes[] = {
    0x0F,0xB6,0x48,0x54,0x66,0x0F,0x6E,0xC1,0x0F,0x5B,0xC0,0xE8,0xF5,0x24,0xFC,0xFF,
    0x84,0xC0,0x0F,0x84,0x17,0x04,0x00,0x00};
constexpr std::uint8_t kAllocationBytes[] = {
    0x48,0x8B,0xD1,0x45,0x33,0xC0,0x48,0x8B,0x0D,0xE3,0x84,0x86,0x00,0x48,0x8B,0x01,
    0x48,0xFF,0x60,0x08};
constexpr std::uint8_t kAllocationCallBytes[] = {
    0xB9,0x50,0x0D,0x00,0x00,0xE8,0xB3,0x29,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xA4,0x03,0x00,0x00};
constexpr uintptr_t CACHE_ROOT = 0x2AE5E60;
constexpr uintptr_t BOX_RVA = 0x421180, BOX_RETURN_RVA = 0x3FF140;
constexpr uintptr_t EVENT_GATE_RVA = 0x3ABC80, EVENT_GATE_CALL_RVA = 0x3FF0E0, EVENT_GATE_RETURN_RVA = 0x3FF0E5;
// Complete 65-byte no-argument predicate. MinHook's stolen prefix includes the
// RIP-relative mov at +4; byte verification does not prove native relocation.
constexpr std::uint8_t kEventGateBytes[] = {
    0x48,0x83,0xEC,0x28,0x8B,0x05,0x76,0x57,0x66,0x02,0x8B,0xC8,0xC1,0xE9,0x08,
    0xF6,0xC1,0x01,0x75,0x26,0xD1,0xE8,0xA8,0x01,0x75,0x20,0xE8,0x81,0x05,0,0,
    0x84,0xC0,0x75,0x17,0x33,0xD2,0x48,0x8D,0x0D,0x54,0x48,0x66,0x02,
    0xE8,0x3F,0x9B,0xFF,0xFF,0x84,0xC0,0x75,0x05,0x48,0x83,0xC4,0x28,0xC3,
    0xB0,0x01,0x48,0x83,0xC4,0x28,0xC3};
constexpr std::uint8_t kEventGateCallBytes[] = {0xE8,0x9B,0xCB,0xFA,0xFF,0x84,0xC0,0x75,0x6E};
static_assert(sizeof(kEventGateBytes) == 65);
constexpr uintptr_t BOX_VTABLE_RVA = 0x5D4F98, HANDLE_REGIONS_RVA = 0x2B0D720;
// Complete instructions through the 18-byte prefix; no RIP-relative operand.
constexpr std::uint8_t kBoxBytes[] = {
    0x40,0x53,0x48,0x83,0xEC,0x50,0x48,0x8B,0xD9,0x48,0xC7,0x44,0x24,0x24,0,0,0,0};
constexpr std::uint8_t kBoxCallBytes[] = {
    0x48,0x8B,0x06,0x48,0x8B,0xD3,0x48,0x8B,0xCE,0xFF,0x50,0x08,0x84,0xC0,0x74,0xC1};
constexpr std::uint8_t kWrapperBytes[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30};
constexpr std::uint8_t kWrapperCallBytes[] = {
    0x48,0x83,0xC1,0x2C,0x49,0x63,0xC6,0x48,0xC1,0xE0,0x06,
    0x48,0x8B,0xD7,0x48,0x03,0xC8,0xE8,0x51,0xFD,0xFF,0xFF};
constexpr std::uint8_t kGeneratedBytes[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t kDispatcherBytes[] = {
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xD9,0x48,0x8B,0xFA};
// Entire one-argument script callback. Its tail jump to 3FE320 preserves the
// script caller's return address; dispatcher _ReturnAddress alone cannot name it.
constexpr std::uint8_t kScriptBytes[] = {
    0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x09,0xE8,0x53,0xF6,0x07,0x00,
    0x8B,0x48,0x04,0xE8,0x4B,0xF6,0x07,0x00,0x48,0x8B,0x98,0xE8,0x09,0x00,0x00,
    0x48,0x85,0xDB,0x74,0x1D,0xF6,0x43,0x04,0x01,0x75,0x17,0x48,0x8B,0xCB,
    0xE8,0x01,0x22,0xFD,0xFF,0x33,0xD2,0x48,0x8B,0xCB,0x48,0x83,0xC4,0x20,
    0x5B,0xE9,0xD2,0x06,0xFD,0xFF,0x48,0x83,0xC4,0x20,0x5B,0xC3};
constexpr uintptr_t CONTROLLER_TABLE = 0x2A10010;
constexpr uintptr_t CONTROLLER_COUNT = 0x2A10418;
constexpr uintptr_t OBJECT_TABLES = 0x2A25030;
// Explicit safety bounds, not discovered native maxima. Never truncate a scope.
constexpr std::size_t CONTROLLER_CAP = 64;
constexpr std::size_t RECORD_CAP = 256;
constexpr std::int32_t OBJECT_CAP = 65536;
constexpr uintptr_t USER_END = 0x7FFFFFFFFFFFULL;
constexpr std::uint8_t kUpdateBytes[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x57,0x48,0x83,0xEC,0x30};
constexpr std::uint8_t kCallBytes[] = {
    0x48,0x8B,0x0B,0x48,0x8D,0x54,0x24,0x20,0xE8,0x9D,0x9F,0x05,0x00};
constexpr std::uint8_t kLookupBytes[] = {
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
constexpr std::uint8_t kCompareBytes[] = {0x2B,0x0A,0x8B,0xC1,0xC3};

using UpdateFn = void (__fastcall*)(void*, const float*);
using LookupFn = const void* (__fastcall*)(std::int32_t);
using WrapperFn = void* (__fastcall*)(const void*, void*);
using GeneratedFn = void* (__fastcall*)(const void*, void*, const float*);
using NaturalFactoryFn = void* (__fastcall*)(std::uint32_t,const float*,float);
using DispatcherFn = std::uint64_t (__fastcall*)(void*, void*);
// No XMM inputs/return in either verified path. Capturing RAX preserves even
// the unspecified scalar result of the script's early return as well as its tail call.
using ScriptFn = std::uint64_t (__fastcall*)(const void*);
using AdmissionFn = std::uint8_t (__fastcall*)(float);
using AllocationFn = void* (__fastcall*)(std::size_t);
using BoxFn = std::uint8_t (__fastcall*)(void*, const float*);
using EventGateFn = std::uint8_t (__fastcall*)();
using ControllerConstructorFn = void* (__fastcall*)(void*, std::uint32_t, const void*);
using ControllerMutationFn = void (__fastcall*)(void*);
constexpr uintptr_t CONTROLLER_CTOR_RVA = 0x3FDCE0, CONTROLLER_INIT_RVA = 0x3FF550,
    CONTROLLER_TEARDOWN_RVA = 0x3FDE60;
// Whole instruction prefixes checked against the saved PE; the initial five
// bytes in all three are one non-relative mov [rsp+10h],rbx instruction.
constexpr std::uint8_t kControllerCtorBytes[] = {
    0x48,0x89,0x5C,0x24,0x10,0x56,0x48,0x83,0xEC,0x20,0x89,0x11,0x33,0xF6,0x89,0x71,0x04};
constexpr std::uint8_t kControllerInitBytes[] = {
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20};
constexpr std::uint8_t kControllerTeardownBytes[] = {
    0x48,0x89,0x5C,0x24,0x10,0x56,0x48,0x83,0xEC,0x20,0x83,0x49,0x04,0x01};
KnownControllerMutationFence g_knownMutation;
static_assert(std::is_trivially_copyable_v<KnownControllerMutationTicket> &&
              std::is_trivially_destructible_v<KnownControllerMutationTicket>);
bool g_knownMutationRequested = false;
std::uint32_t g_knownMutationInstalled = 0;
ControllerConstructorFn g_originalControllerCtor = nullptr;
ControllerMutationFn g_originalControllerInit = nullptr, g_originalControllerTeardown = nullptr;
void HostEmissionMutation(uintptr_t controller);
void HostEmissionInitialized(uintptr_t controller);

void* __fastcall HookedControllerCtor(void* controller, std::uint32_t key, const void* header) {
    const auto savedError=GetLastError();
    const auto observation=populationauthority::Enter(populationauthority::Kind::Constructor,
        reinterpret_cast<uintptr_t>(controller),0,0,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool counted = g_knownMutation.Begin();
    HostEmissionMutation(reinterpret_cast<uintptr_t>(controller));
    void* result = nullptr;
    bool returned=false;
    __try { SetLastError(savedError);result = g_originalControllerCtor(controller, key, header);returned=true; }
    __finally { const auto error=GetLastError();g_knownMutation.End(counted, AbnormalTermination() != FALSE);
        populationauthority::Exit(observation,populationauthority::Kind::Constructor,
            reinterpret_cast<uintptr_t>(controller),0,0,reinterpret_cast<uintptr_t>(result),returned);SetLastError(error); }
    return result;
}
void __fastcall HookedControllerInit(void* controller) {
    const auto savedError=GetLastError();
    const auto observation=populationauthority::Enter(populationauthority::Kind::Initialize,
        reinterpret_cast<uintptr_t>(controller),0,0,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool counted = g_knownMutation.Begin();
    HostEmissionMutation(reinterpret_cast<uintptr_t>(controller));
    bool returned=false;
    __try { SetLastError(savedError);g_originalControllerInit(controller);returned=true; }
    __finally { const auto error=GetLastError();g_knownMutation.End(counted, AbnormalTermination() != FALSE);
        populationauthority::Exit(observation,populationauthority::Kind::Initialize,
            reinterpret_cast<uintptr_t>(controller),0,0,0,returned);SetLastError(error); }
    HostEmissionInitialized(reinterpret_cast<uintptr_t>(controller));
}
void __fastcall HookedControllerTeardown(void* controller) {
    const auto savedError=GetLastError();
    const auto observation=populationauthority::Enter(populationauthority::Kind::Teardown,
        reinterpret_cast<uintptr_t>(controller),0,0,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    const bool counted = g_knownMutation.Begin();
    HostEmissionMutation(reinterpret_cast<uintptr_t>(controller));
    bool returned=false;
    __try { SetLastError(savedError);g_originalControllerTeardown(controller);returned=true; }
    __finally { const auto error=GetLastError();g_knownMutation.End(counted, AbnormalTermination() != FALSE);
        populationauthority::Exit(observation,populationauthority::Kind::Teardown,0,0,0,0,returned);SetLastError(error); }
}
uintptr_t g_exeBase = 0;
std::uint32_t g_imageSize = 0;
LogFn g_log = nullptr;
RoleFn g_role = nullptr;
CaptureFn g_capture = nullptr;
CopyFn g_copy = nullptr;
UpdateFn g_original = nullptr;
LookupFn g_lookup = nullptr;
bool g_installed = false;
std::atomic<bool> g_traceRequested {false}, g_traceInstalled {false};
std::atomic<bool> g_fixedInstalled {false}, g_generatedInstalled {false};
std::atomic<bool> g_dispatcherInstalled {false}, g_scriptInstalled {false};
std::atomic<DWORD> g_diagnosticGameThread {0};
WrapperFn g_originalWrapper = nullptr;
GeneratedFn g_originalGenerated = nullptr;
NaturalFactoryFn g_originalNaturalFactory = nullptr;
DispatcherFn g_originalDispatcher = nullptr;
ScriptFn g_originalScript = nullptr;
AdmissionFn g_originalAdmission = nullptr;
AllocationFn g_originalAllocation = nullptr;
BoxFn g_originalBox = nullptr;
EventGateFn g_originalEventGate = nullptr;
std::atomic<bool> g_enrollmentRequested {false}, g_enrollmentConfigured {false};
std::atomic<bool> g_eventGateVerified {false}, g_eventGateInstalled {false}, g_eventGateFailed {false};
std::atomic<std::uint64_t> g_eventGateCoverage {0}, g_eventGateForeign {0}, g_eventGateUnwound {0}, g_eventGateDropped {0};
thread_local unsigned g_eventGateDepth = 0;
std::atomic<bool> g_originalPhaseRequested {false}, g_originalPhaseConfigured {false};
std::atomic<std::uint64_t> g_originalPhaseCoverage {0};
constexpr unsigned ORIGINAL_PHASE_DEPTH_CAP = 32;
struct OriginalPhaseFrame {
    std::uint64_t invocation = 0, updateSequence = 0;
    uintptr_t controller = 0, caller = 0, point = 0;
    unsigned depth = 0;
    UpdatePhase phase = UpdatePhase::None;
    OriginalCallSource source = OriginalCallSource::None;
    bool available = false, overflow = false;
};
thread_local OriginalPhaseFrame g_originalPhaseFrame;
thread_local std::uint64_t g_originalPhaseSerial = 0;
static_assert(std::is_trivially_copyable_v<OriginalPhaseFrame> && std::is_trivially_destructible_v<OriginalPhaseFrame>);
static_assert(std::is_trivially_copyable_v<OriginalPhaseObservation> && std::is_trivially_destructible_v<OriginalPhaseObservation>);
struct GeometryConfig {
    bool requested = false, configured = false;
    std::uint32_t load = 0, transition = 0;
    std::array<std::uint8_t, 10> location {};
};
GeometryConfig g_geometryConfig;
std::atomic<bool> g_geometryVerified {false}, g_geometryInstalled {false}, g_geometryPrior {false};
std::atomic<GeometryTerminal> g_geometryTerminal {GeometryTerminal::Disabled};
std::atomic<std::uint32_t> g_geometryTicks {0};
std::atomic<std::uint64_t> g_geometryStarted {0}, g_geometryCoverage {0};
std::atomic<std::uint64_t> g_geometryDropped {0}, g_geometryUnwound {0}, g_geometryForeign {0};
thread_local TraceEvent* g_geometryScope = nullptr;
thread_local unsigned g_geometryPredicateDepth = 0;
std::atomic<std::uint32_t> g_factoryVerified {0}, g_factoryInstalled {0}, g_factoryFailed {0};
std::atomic<std::uint64_t> g_factoryCoverageSerial {0};
std::atomic<std::uint64_t> g_factoryForeign {0}, g_factoryUnwound {0}, g_factoryAmbiguous {0};

constexpr unsigned FACTORY_DEPTH_CAP = 32;
constexpr unsigned CONSTRUCTION_DEPTH_CAP = 8;
std::atomic<bool> g_constructionRequested {false}, g_constructionConfigured {false};
std::atomic<std::uint64_t> g_constructionSerial {0}, g_constructionCoverage {0};
struct FactoryFrame {
    FactoryPredicates factory{};NativeConstructionLineage construction{};
    uintptr_t anchor{};std::uint64_t serial{};bool constructionVisible{};
};
struct FactoryLocal {unsigned depth{};std::array<FactoryFrame,FACTORY_DEPTH_CAP> frames{};};
tracefiber::Store<FactoryLocal> g_factoryStorage;
std::atomic<std::uint64_t> g_factoryScopeSerial{};
std::atomic<bool> g_rawDiagnosticEnabled{};
FactoryFrame* FactoryParent(FactoryLocal*& local) {
    local=g_factoryStorage.Current();
    if(!g_rawDiagnosticEnabled || !local || !local->depth)return nullptr;
    if(local->depth>FACTORY_DEPTH_CAP){g_factoryStorage.Reject(local);return nullptr;}
    auto& frame=local->frames[local->depth-1];
    if(!g_factoryStorage.Anchor(frame.anchor)){g_factoryStorage.Reject(local);return nullptr;}
    return &frame;
}
bool FactoryCurrent(FactoryLocal* local,unsigned depth,std::uint64_t serial) {
    const bool valid=g_factoryStorage.Same(local) && local->depth==depth && depth>0 && depth<=FACTORY_DEPTH_CAP &&
        local->frames[depth-1].serial==serial && g_factoryStorage.Anchor(local->frames[depth-1].anchor);
    if(!valid)g_factoryStorage.Abandon(local);
    return valid;
}
constexpr std::size_t CONSTRUCTION_QUEUE_CAP = 32;
SRWLOCK g_constructionLock = SRWLOCK_INIT;
std::array<NativeConstructionLineage, CONSTRUCTION_QUEUE_CAP> g_constructionQueue {};
std::size_t g_constructionRead = 0, g_constructionCount = 0;
std::atomic<std::uint64_t> g_constructionPublished {0}, g_constructionDropped {0};
static_assert(std::is_trivially_copyable_v<NativeConstructionLineage> &&
              std::is_trivially_destructible_v<NativeConstructionLineage>);
constexpr std::size_t TRACE_QUEUE_CAP = 128, TRACE_TICK_CAP = 64;
SRWLOCK g_traceLock = SRWLOCK_INIT;
std::array<TraceEvent, TRACE_QUEUE_CAP> g_traceQueue {};
std::size_t g_traceRead = 0, g_traceCount = 0;
std::atomic<std::uint64_t> g_traceStarted {0}, g_tracePublished {0}, g_traceDropped {0};
std::atomic<std::uint64_t> g_traceUnsupported {0}, g_traceUnavailable {0}, g_traceFaults {0};
std::atomic<std::uint32_t> g_traceException {0};
std::atomic<std::uint64_t> g_traceTicks {0};
std::atomic<std::uint64_t> g_dispatcherSequences {0}, g_scriptSequences {0};
struct DispatcherScope {
    bool active = false;
    uintptr_t controller = 0, region = 0, caller = 0;
    std::uint64_t sequence = 0;
};
struct ScriptScope {
    bool active = false;
    uintptr_t caller = 0;
    std::uint64_t sequence = 0;
};
thread_local DispatcherScope g_dispatcherScope;
thread_local ScriptScope g_scriptScope;
struct TraceTick {
    bool active = false;
    bool stampAvailable = false;
    uintptr_t controller = 0;
    std::uint64_t sequence = 0;
    TraceStamp stamp {};
    TraceState before {};
    std::size_t count = 0;
    std::array<TraceEvent, TRACE_TICK_CAP> events {};
    TraceEvent geometryEvent {};
    bool geometrySelected = false;
};
thread_local TraceTick g_traceTick;
thread_local unsigned g_traceNestedDepth = 0;
static_assert(std::is_trivially_copyable_v<TraceEvent>);
// One supported candidate per load. Overflow/foreign access closes recording;
// never wait in a native update and never allocate a catalog inside that update.
SRWLOCK g_hostEmissionLock = SRWLOCK_INIT;
HostFirstEmission g_hostEmission;
KnownControllerMutationTicket g_hostEmissionTicket;
std::atomic<bool> g_hostEmissionPoison {false};
std::atomic<bool> g_hostEmissionConfigured {false};
std::uint64_t g_hostEmissionSerial = 0;
bool g_hostEmissionAmbiguous = false;
bool g_hostEmissionFirstUpdateBound = false;
struct HostEmissionTick {
    HostFirstEmission value {};
    KnownControllerMutationTicket ticket {};
    void* fiber = nullptr;
    DWORD thread = 0;
    unsigned successfulReturns = 0, wrapperAttempts = 0;
    bool active = false, invalid = false;
};
thread_local HostEmissionTick g_hostEmissionTick;
struct ClientOriginalReturn {
    uintptr_t controller = 0;
    std::uint64_t sequence = 0;
    TraceStamp stamp {};
    KnownControllerMutationTicket mutation {};
};
struct ClientOriginalReturns {
    std::array<ClientOriginalReturn, CONTROLLER_CAP> entries {};
    TraceStamp stamp {};
    std::size_t count = 0;
    bool stampAvailable = false, overflow = false;
};
thread_local ClientOriginalReturns g_clientOriginalReturn;
static_assert(std::is_trivially_copyable_v<ClientOriginalReturns>);
static_assert(std::is_trivially_copyable_v<HostFirstEmission>);
std::uint64_t g_captures = 0, g_applies = 0, g_holds = 0, g_unsupported = 0, g_unavailable = 0;
std::array<std::uint64_t, 5> g_lastReport {};

void SaturatingIncrement(std::atomic<std::uint64_t>& counter) {
    auto value = counter.load(std::memory_order_relaxed);
    while (value != UINT64_MAX && !counter.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {}
}

bool ReadableRange(uintptr_t address, std::size_t size) {
    return address > 0x10000 && address < USER_END && size <= USER_END - address;
}

// SEH leaves contain no objects requiring C++ unwinding.
bool CopyNative(uintptr_t address, void* out, std::size_t size) {
    if (!ReadableRange(address, size)) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool LookupNative(std::uint32_t id, uintptr_t& out) {
    __try {
        out = reinterpret_cast<uintptr_t>(g_lookup(static_cast<std::int32_t>(id)));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template <typename T> bool ReadNative(uintptr_t address, T& out) {
    return CopyNative(address, &out, sizeof(out));
}

template <typename T> T Field(const std::uint8_t* bytes, std::size_t offset) {
    T out {};
    std::memcpy(&out, bytes + offset, sizeof(out));
    return out;
}

template <std::size_t N> bool Matches(uintptr_t address, const std::uint8_t (&expected)[N]) {
    std::array<std::uint8_t, N> actual {};
    return CopyNative(address, actual.data(), N) && std::memcmp(actual.data(), expected, N) == 0;
}

struct TableEntry {
    std::uint32_t key = 0, flags = 0;
    uintptr_t controller = 0;
};
static_assert(sizeof(TableEntry) == 16);

struct Controller {
    std::uint32_t key = 0, flags = 0;
    uintptr_t header = 0, regionHead = 0, regionTail = 0;
    float cooldown = 0;
    std::uint32_t currentCount = 0, initialCount = 0;
    std::uint8_t stage = 0, padding[3] {};
    uintptr_t spawnArray = 0, regionArray = 0;
};
static_assert(offsetof(Controller, spawnArray) == 0x30 && sizeof(Controller) == 0x40);

using Stamp = TraceStamp;

bool ReadStamp(Stamp& stamp) {
    // Existing activation authority executes on its verified native task. Keep
    // that policy independent from diagnostic thread enrollment.
    stamp.transition = warp::TransitionSerial();
    stamp.load = warp::LoadSerial();
    return CopyNative(g_exeBase + offsets::NOW, stamp.location.data(), stamp.location.size());
}

bool SameStamp(const Stamp& stamp) {
    Stamp current;
    return ReadStamp(current) && current.transition == stamp.transition && current.load == stamp.load &&
           current.location == stamp.location;
}

void BindClientOriginalReturnStamp(const TraceStamp& stamp) {
    auto& returns = g_clientOriginalReturn;
    if (!returns.stampAvailable || returns.stamp.transition != stamp.transition ||
        returns.stamp.load != stamp.load || returns.stamp.location != stamp.location) {
        returns = {};
        returns.stamp = stamp;
        returns.stampAvailable = true;
    }
}

void RecordClientOriginalReturn(uintptr_t controller, std::uint64_t sequence,
                               const TraceStamp& stamp, const KnownControllerMutationTicket& mutation) {
    BindClientOriginalReturnStamp(stamp);
    auto& returns = g_clientOriginalReturn;
    if (returns.overflow) return;
    for (std::size_t i = 0; i < returns.count; ++i) {
        if (returns.entries[i].controller == controller) {
            returns.entries[i] = {controller, sequence, stamp, mutation};
            return;
        }
    }
    if (returns.count == returns.entries.size()) {
        returns.overflow = true; // No eviction or silently selected subset.
        return;
    }
    returns.entries[returns.count++] = {controller, sequence, stamp, mutation};
}

bool DirectObjectId(std::uint32_t id) {
    if (id == 0 || (id & 0xF0000000U) != 0) return false;
    // General resolver 3DFEB0 remaps these through native party/save state.
    switch (id) {
    case 0x236: case 0x237: case 0x238: case 0x23B: case 0x23C: case 0x23D:
    case 0x23F: case 0x240: case 0x2C0: case 0x319: case 0x31A: case 0x3EE:
    case 0x62A: case 0x62B: return false;
    default: return true;
    }
}

struct ObjectTable { uintptr_t root = 0; std::int32_t count = 0; };
using ObjectTables = std::array<ObjectTable, 3>;

bool ReadObjectTables(ObjectTables& tables) {
    for (std::size_t i = 0; i < tables.size(); ++i) {
        auto& table = tables[i];
        if (!ReadNative(g_exeBase + OBJECT_TABLES + i * 8, table.root)) return false;
        if (!table.root) continue;
        if (!ReadableRange(table.root, 8) || !ReadNative(table.root + 4, table.count) ||
            table.count < 0 || table.count > OBJECT_CAP ||
            !ReadableRange(table.root, 8 + static_cast<std::size_t>(table.count) * 0x60)) return false;
    }
    return true;
}

bool SameObjectTables(const ObjectTables& tables) {
    ObjectTables current {};
    if (!ReadObjectTables(current)) return false;
    for (std::size_t i = 0; i < tables.size(); ++i) {
        if (current[i].root != tables[i].root || current[i].count != tables[i].count) return false;
    }
    return true;
}

enum class Scope { Unavailable, OutOfScope, Qualified };
struct Qualification {
    Scope scope = Scope::Unavailable;
    const char* reason = "not sampled";
    uintptr_t controller = 0;
    uintptr_t header = 0, spawnArray = 0;
    std::uint32_t key = 0;
    std::uint16_t headerId = 0, records = 0;
    Stamp stamp {};
    std::array<std::uint8_t, 5 * 0x40> geometryRecords {};
    bool geometryRecordsVerified = false;
};

Qualification Qualify(uintptr_t address) {
    Qualification result;
    result.controller = address;
    auto fail = [&](const char* reason, Scope scope = Scope::Unavailable) {
        result.reason = reason;
        result.scope = scope;
        return result;
    };
    if (!ReadStamp(result.stamp)) return fail("lifecycle read");
    std::int32_t count = 0;
    std::array<TableEntry, CONTROLLER_CAP> entries {};
    if (!ReadNative(g_exeBase + CONTROLLER_COUNT, count) || count < 0 ||
        count > static_cast<std::int32_t>(entries.size())) return fail("controller count/read cap");
    if (count && !CopyNative(g_exeBase + CONTROLLER_TABLE, entries.data(),
                            static_cast<std::size_t>(count) * sizeof(TableEntry))) return fail("controller table read");
    int index = -1;
    for (int i = 0; i < count; ++i) {
        if (entries[i].controller == address && (entries[i].flags & 1) == 0) { index = i; break; }
    }
    if (index < 0) return fail("not ordinary controller", Scope::OutOfScope);
    Controller controller;
    if (!ReadNative(address, controller) || controller.key != entries[index].key)
        return fail("controller identity read");
    result.key = controller.key;
    result.header = controller.header;
    result.spawnArray = controller.spawnArray;
    std::array<std::uint8_t, 0x2C> header {};
    if (!CopyNative(controller.header, header.data(), header.size())) return fail("header read");
    result.headerId = Field<std::uint16_t>(header.data(), 2);
    result.records = Field<std::uint16_t>(header.data(), 4);
    // 3FE334: 0F B6 01 (movzx eax, byte ptr [rcx]); +1 is separate flags.
    if (header[0] != 2) return fail("native type", Scope::OutOfScope);
    if (result.records == 0) return fail("no spawn records", Scope::OutOfScope);
    if (result.records > RECORD_CAP) return fail("spawn record cap");
    const auto recordBytes = static_cast<std::size_t>(result.records) * 0x40;
    if (!ReadableRange(controller.header, header.size() + recordBytes) ||
        controller.spawnArray != controller.header + header.size() ||
        controller.regionArray != controller.spawnArray + recordBytes) return fail("record layout");
    std::array<std::uint8_t, RECORD_CAP * 0x40> records {};
    if (!CopyNative(controller.spawnArray, records.data(), recordBytes)) return fail("record read");
    ObjectTables tables {};
    if (!ReadObjectTables(tables)) return fail("object tables read/cap");
    struct ObjectIdentity { uintptr_t address = 0; std::array<std::uint8_t, 10> bytes {}; };
    std::array<ObjectIdentity, RECORD_CAP> identities {};
    for (std::size_t i = 0; i < result.records; ++i) {
        const auto* record = records.data() + i * 0x40;
        if (record[0x1C] != 2 || record[0x1D] != 0)
            return fail("nonstatic/mixed record mode", Scope::OutOfScope);
        for (std::size_t offset = 4; offset <= 12; offset += 4) {
            if (!std::isfinite(Field<float>(record, offset))) return fail("nonfinite spawn point");
        }
        const auto id = Field<std::uint32_t>(record, 0);
        if (!DirectObjectId(id)) return fail("dynamic object ID", Scope::OutOfScope);
        uintptr_t object = 0;
        if (!LookupNative(id, object)) return fail("object lookup fault");
        if (!object) return fail("unresolved object ID", Scope::OutOfScope);
        bool inTable = false;
        for (const auto& table : tables) {
            if (table.root && object >= table.root + 8 &&
                object < table.root + 8 + static_cast<std::size_t>(table.count) * 0x60 &&
                (object - table.root - 8) % 0x60 == 0) { inTable = true; break; }
        }
        std::array<std::uint8_t, 0x60> entry {};
        if (!inTable || !CopyNative(object, entry.data(), entry.size()) ||
            Field<std::uint32_t>(entry.data(), 0) != id) return fail("objentry identity/read");
        if ((entry[4] != offsets::objentry::TYPE_BOSS && entry[4] != offsets::objentry::TYPE_MOB) ||
            (entry[8] == 'F' && entry[9] == '_')) return fail("noncombat/mixed records", Scope::OutOfScope);
        identities[i].address = object;
        std::memcpy(identities[i].bytes.data(), entry.data(), identities[i].bytes.size());
    }
    // No native pointers are retained between calls. Commit only stable scope.
    std::int32_t finalCount = 0;
    TableEntry finalEntry;
    Controller finalController;
    std::array<std::uint8_t, 0x2C> finalHeader {};
    std::array<std::uint8_t, RECORD_CAP * 0x40> finalRecords {};
    if (!ReadNative(g_exeBase + CONTROLLER_COUNT, finalCount) || finalCount != count ||
        !ReadNative(g_exeBase + CONTROLLER_TABLE + static_cast<std::size_t>(index) * 16, finalEntry) ||
        finalEntry.key != entries[index].key || finalEntry.flags != entries[index].flags ||
        finalEntry.controller != address || !ReadNative(address, finalController) ||
        finalController.key != controller.key || finalController.header != controller.header ||
        finalController.spawnArray != controller.spawnArray || finalController.regionArray != controller.regionArray ||
        !CopyNative(controller.header, finalHeader.data(), finalHeader.size()) || finalHeader != header ||
        !CopyNative(controller.spawnArray, finalRecords.data(), recordBytes) ||
        std::memcmp(records.data(), finalRecords.data(), recordBytes) != 0 ||
        !SameObjectTables(tables) || !SameStamp(result.stamp)) return fail("scope changed");
    for (std::size_t i = 0; i < result.records; ++i) {
        std::array<std::uint8_t, 10> identity {};
        if (!CopyNative(identities[i].address, identity.data(), identity.size()) || identity != identities[i].bytes)
            return fail("objentry changed");
    }
    if (!SameStamp(result.stamp)) return fail("lifecycle changed");
    if (g_geometryConfig.configured && g_geometryInstalled.load() &&
        result.key == 808476514 && result.headerId == 30 && result.records == 5) {
        std::memcpy(result.geometryRecords.data(), finalRecords.data(), result.geometryRecords.size());
        result.geometryRecordsVerified = true;
    }
    result.scope = Scope::Qualified;
    result.reason = "static combat type2";
    return result;
}

void Report(std::size_t category, const char* action, const Qualification& q, const float* point = nullptr) {
    if (!g_log) return;
    const auto now = GetTickCount64();
    if (g_lastReport[category] && now - g_lastReport[category] < 1000) return;
    g_lastReport[category] = now;
    g_log("[spawnctl] action=%s reason=%s controller=%llX key=%u headerId=%u records=%u transition=%u load=%u room=%02X/%02X capture=%llu apply=%llu hold=%llu unsupported=%llu unavailable=%llu pointAvailable=%u point=(%.3f,%.3f,%.3f,%.3f)",
          action, q.reason, static_cast<unsigned long long>(q.controller), q.key, q.headerId, q.records,
          q.stamp.transition, q.stamp.load, q.stamp.location[0], q.stamp.location[1],
          static_cast<unsigned long long>(g_captures), static_cast<unsigned long long>(g_applies),
          static_cast<unsigned long long>(g_holds), static_cast<unsigned long long>(g_unsupported),
          static_cast<unsigned long long>(g_unavailable), point ? 1U : 0U, point ? point[0] : 0.0f,
          point ? point[1] : 0.0f, point ? point[2] : 0.0f, point ? point[3] : 0.0f);
}

bool FinitePoint(const float* point) {
    return std::isfinite(point[0]) && std::isfinite(point[1]) &&
           std::isfinite(point[2]) && std::isfinite(point[3]);
}

TraceState ReadDiagnosticState(uintptr_t exeBase, uintptr_t address) {
    TraceState state;
    Controller controller;
    std::array<std::uint8_t, 0x2C> header {};
    if (ReadNative(address, controller) && CopyNative(controller.header, header.data(), header.size())) {
        state.header = controller.header;
        state.spawnArray = controller.spawnArray;
        state.key = controller.key;
        state.flags = controller.flags;
        state.currentCount = controller.currentCount;
        state.initialCount = controller.initialCount;
        state.cooldown = controller.cooldown;
        state.stage = controller.stage;
        state.headerId = Field<std::uint16_t>(header.data(), 2);
        state.recordCount = Field<std::uint16_t>(header.data(), 4);
        state.activation = header[0xE];
        state.nativeType = header[0];
        Controller check;
        std::array<std::uint8_t, 0x2C> checkHeader {};
        state.controllerAvailable = ReadNative(address, check) &&
            std::memcmp(&controller, &check, sizeof(controller)) == 0 &&
            CopyNative(controller.header, checkHeader.data(), checkHeader.size()) && checkHeader == header;
    }
    // Only the four verified native buckets are accepted. Null/current pointer
    // outside that region is unknown, never an invented empty appearance cache.
    const uintptr_t root = exeBase + CACHE_ROOT;
    uintptr_t bucket = 0, checkBucket = 0;
    std::array<std::uint8_t, 0x208> bytes {}, checkBytes {};
    if (ReadNative(root + 0x820, bucket) && bucket >= root && bucket < root + 0x820 &&
        (bucket - root) % 0x208 == 0 && CopyNative(bucket, bytes.data(), bytes.size()) &&
        ReadNative(root + 0x820, checkBucket) && checkBucket == bucket &&
        CopyNative(bucket, checkBytes.data(), checkBytes.size()) && bytes == checkBytes) {
        state.cacheBucket = bucket;
        state.cacheRoom = Field<std::int32_t>(bytes.data(), 0);
        state.cacheAge = Field<std::int32_t>(bytes.data(), 4);
        std::memcpy(state.cacheIds.data(), bytes.data() + 8, sizeof(state.cacheIds));
        state.cacheAvailable = true;
    }
    return state;
}

TraceState CaptureTraceState(uintptr_t address) {
    return CaptureDiagnosticState(g_exeBase, address);
}

bool HostEmissionLock() {
    if (TryAcquireSRWLockExclusive(&g_hostEmissionLock)) return true;
    g_hostEmissionPoison = true;
    return false;
}

bool HostEmissionSameStamp(const TraceStamp& a, const TraceStamp& b) {
    return a.load == b.load && a.transition == b.transition && a.location == b.location;
}

bool HostEmissionSameDefinition(const HostFirstEmission& a, const HostFirstEmission& b) {
    if (a.controller != b.controller || a.header != b.header || a.spawnArray != b.spawnArray ||
        a.groupKey != b.groupKey || !HostEmissionSameStamp(a.stamp, b.stamp) || a.records != b.records ||
        a.rawHeader[0xE] > 1 || b.rawHeader[0xE] > 1) return false;
    for (std::size_t i = 0; i < a.rawHeader.size(); ++i)
        if (i != 0xE && a.rawHeader[i] != b.rawHeader[i]) return false;
    return true;
}

bool ReadHostEmissionDefinition(uintptr_t address, HostFirstEmission& out) {
    Controller c {}, check {};
    if (!ReadStamp(out.stamp) || !ReadNative(address, c) || c.key != 808476514 ||
        !CopyNative(c.header, out.rawHeader.data(), out.rawHeader.size()) ||
        out.rawHeader[0] != 2 || Field<std::uint16_t>(out.rawHeader.data(), 2) != 30 ||
        Field<std::uint16_t>(out.rawHeader.data(), 4) != 5 || out.rawHeader[0xE] > 1 ||
        c.spawnArray != c.header + 44 || c.regionArray != c.spawnArray + 320 ||
        !CopyNative(c.spawnArray, out.records.data(), sizeof(out.records))) return false;
    constexpr std::array<std::uint16_t, 5> ids {11, 12, 13, 14, 18};
    for (std::size_t i = 0; i < out.records.size(); ++i) {
        const auto* r = out.records[i].data();
        if (Field<std::uint32_t>(r, 0) != 302 || r[0x1C] != 2 || r[0x1D] != 0 ||
            Field<std::uint16_t>(r, 0x1E) != ids[i] || Field<std::uint16_t>(r, 0x2A) != 8 ||
            r[0x30] != 0) return false;
    }
    std::array<std::uint8_t, 44> header {};
    decltype(out.records) records {};
    if (!ReadNative(address, check) || check.key != c.key || check.header != c.header ||
        check.spawnArray != c.spawnArray || check.regionArray != c.regionArray ||
        !CopyNative(c.header, header.data(), header.size()) || header != out.rawHeader ||
        !CopyNative(c.spawnArray, records.data(), sizeof(records)) || records != out.records ||
        !SameStamp(out.stamp)) return false;
    out.controller = address; out.header = c.header; out.spawnArray = c.spawnArray; out.groupKey = c.key;
    const auto* loc = out.stamp.location.data();
    out.location = {loc[0], loc[1], loc[2], Field<std::uint16_t>(loc, 4),
        Field<std::uint16_t>(loc, 6), Field<std::uint16_t>(loc, 8)};
    return true;
}

bool HostEmissionInitialState(const TraceState& s) {
    return s.controllerAvailable && s.cacheAvailable && s.flags == 2 && s.stage == 0 &&
        s.currentCount == 5 && s.initialCount == 5 && s.cooldown == 0 && s.activation <= 1 &&
        std::all_of(s.cacheIds.begin(), s.cacheIds.end(), [](auto id) { return id == 0; });
}

void RefuseHostEmission(const char* reason) {
    if (!g_hostEmissionConfigured.load()) return;
    g_hostEmissionTick.invalid = true;
    if (!HostEmissionLock()) return;
    g_hostEmission.status = HostFirstEmissionStatus::Refused;
    g_hostEmission.reason = reason;
    ReleaseSRWLockExclusive(&g_hostEmissionLock);
}

void HostEmissionMutation(uintptr_t controller) {
    // The known mutation ticket already invalidates every receipt, including
    // mutations on another thread. Clear this owner's bounded storage as well.
    g_clientOriginalReturn = {};
    if (!g_hostEmissionConfigured.load()) return;
    if (g_hostEmissionTick.active) g_hostEmissionTick.invalid = true;
    if (!HostEmissionLock()) return;
    if (g_hostEmission.controller == controller) {
        g_hostEmission.status = HostFirstEmissionStatus::Unavailable;
        g_hostEmission.reason = "known controller mutation; initialization required";
    }
    ReleaseSRWLockExclusive(&g_hostEmissionLock);
}

void HostEmissionInitialized(uintptr_t controller) {
    if (!g_hostEmissionConfigured.load() || g_hostEmissionPoison.load()) return;
    HostFirstEmission value;
    // Unknown/foreign initialization cannot arm a later historical observation.
    // Boot/warp can precede network host authority. Observe that native init
    // now; only BeginHostEmission/actual wrapper completion require role1.
    // Any earlier wrapper outside that host scope permanently retires Waiting.
    if (!IsDiagnosticGameThread() || !ReadHostEmissionDefinition(controller, value)) return;
    value.before = CaptureTraceState(controller);
    const auto ticket = AcquireKnownMutationTicket();
    const bool initial = HostEmissionInitialState(value.before) && ticket.available && SameStamp(value.stamp);
    if (!HostEmissionLock()) return;
    if (!HostEmissionSameStamp(g_hostEmission.stamp, value.stamp)) g_hostEmissionAmbiguous = false;
    if (g_hostEmission.controller && g_hostEmission.controller != controller &&
        HostEmissionSameStamp(g_hostEmission.stamp, value.stamp)) {
        g_hostEmissionAmbiguous = true;
        g_hostEmission.status = HostFirstEmissionStatus::Refused;
        g_hostEmission.reason = "multiple supported controllers in one load";
    } else if (g_hostEmissionAmbiguous) {
        g_hostEmission.status = HostFirstEmissionStatus::Refused;
        g_hostEmission.reason = "ambiguous load cannot rearm";
    } else if (g_hostEmissionSerial == UINT64_MAX) {
        g_hostEmissionPoison = true;
    } else {
        value.initSerial = ++g_hostEmissionSerial;
        value.status = initial ? HostFirstEmissionStatus::Waiting : HostFirstEmissionStatus::Refused;
        value.reason = initial ? "observed initialization; waiting for first native return" : "initial state unavailable";
        g_hostEmission = value;
        g_hostEmissionTicket = ticket;
        g_hostEmissionFirstUpdateBound = false;
    }
    ReleaseSRWLockExclusive(&g_hostEmissionLock);
}

void BeginHostEmission(uintptr_t controller, const float* point) {
    if (!g_hostEmissionConfigured.load() || g_hostEmissionPoison.load()) return;
    if (g_hostEmissionTick.active) { RefuseHostEmission("nested original update"); return; }
    HostFirstEmission saved;
    bool firstUpdateBound = false;
    if (!HostEmissionLock()) return;
    saved = g_hostEmission;
    firstUpdateBound = g_hostEmissionFirstUpdateBound;
    ReleaseSRWLockExclusive(&g_hostEmissionLock);
    if (saved.controller != controller || saved.status != HostFirstEmissionStatus::Waiting) return;
    HostFirstEmission value;
    if (!IsDiagnosticGameThread() || !g_traceTick.active || g_traceNestedDepth ||
        !g_traceTick.sequence || g_traceTick.sequence == UINT64_MAX ||
        g_traceTick.controller != controller || !ReadHostEmissionDefinition(controller, value)) {
        RefuseHostEmission("first-update identity/coverage unavailable"); return;
    }
    // LoadSerial is published after native finalizers. Exactly the first host
    // update can bind a witnessed birth to that one completion, retaining the
    // same transition/NOW/definition/pointers. Never restamp a later update.
    if (!firstUpdateBound && saved.stamp.transition == value.stamp.transition &&
        saved.stamp.location == value.stamp.location && saved.stamp.load != UINT32_MAX &&
        value.stamp.load == saved.stamp.load + 1) saved.stamp.load = value.stamp.load;
    if (!HostEmissionSameDefinition(saved, value)) {
        RefuseHostEmission("first-update birth/load definition mismatch"); return;
    }
    if (!firstUpdateBound) {
        if (!HostEmissionLock()) return;
        const bool current = g_hostEmission.status == HostFirstEmissionStatus::Waiting &&
            g_hostEmission.initSerial == saved.initSerial;
        if (current) {
            g_hostEmission.stamp = value.stamp;
            g_hostEmissionFirstUpdateBound = true;
        }
        ReleaseSRWLockExclusive(&g_hostEmissionLock);
        if (!current) return;
    }
    value.before = CaptureTraceState(controller);
    const auto ticket = AcquireKnownMutationTicket();
    // A preceding null attempt may have set transient accepted-region bit3.
    // Preserve that actual prestate; never manufacture initial flags2 here.
    auto initial = value.before;
    initial.flags &= ~8U;
    if (!HostEmissionInitialState(initial) || !ticket.available ||
        !CopyNative(reinterpret_cast<uintptr_t>(point), value.point.data(), sizeof(value.point)) ||
        !FinitePoint(value.point.data()) || !SameStamp(value.stamp) || g_role() != 1) {
        RefuseHostEmission("preactivation point/state unavailable"); return;
    }
    value.initSerial = saved.initSerial;
    value.updateSequence = g_traceTick.sequence;
    g_hostEmissionTick = {};
    g_hostEmissionTick.value = value;
    g_hostEmissionTick.ticket = ticket;
    g_hostEmissionTick.thread = GetCurrentThreadId();
    g_hostEmissionTick.fiber = GetCurrentFiber();
    g_hostEmissionTick.active = true;
}

void ObserveHostEmissionWrapper(const TraceEvent& event) {
    if (!g_hostEmissionConfigured.load()) return;
    auto& tick = g_hostEmissionTick;
    if (!tick.active) {
        if (!HostEmissionLock()) return;
        if (g_hostEmission.controller == event.controller &&
            g_hostEmission.status == HostFirstEmissionStatus::Waiting) {
            g_hostEmission.status = HostFirstEmissionStatus::Refused;
            g_hostEmission.reason = "wrapper outside recorded original update";
        }
        ReleaseSRWLockExclusive(&g_hostEmissionLock);
        return;
    }
    if (tick.wrapperAttempts == TRACE_TICK_CAP) { tick.invalid = true; return; }
    ++tick.wrapperAttempts;
    if (event.controller != tick.value.controller || GetCurrentThreadId() != tick.thread ||
        GetCurrentFiber() != tick.fiber || g_traceNestedDepth || event.factory.depth != 0 ||
        !event.roleAvailable || event.role != 1 || !event.enclosingTick ||
        event.tickSequence != tick.value.updateSequence || !event.enclosingDispatcher ||
        !event.dispatcherCallerRvaAvailable || event.dispatcherCallerRva != 0x3FF153 ||
        event.enclosingScript42DC10 || event.wrapper != TraceWrapper::Fixed ||
        !event.callerRvaAvailable || event.callerRva != WRAPPER_RETURN_RVA ||
        !event.wrapperComplete || !event.lifecycleStable || !event.recordIndexAvailable ||
        event.recordIndex >= 5 || !event.recordAvailable ||
        event.recordBytes != tick.value.records[event.recordIndex] ||
        event.record != tick.value.spawnArray + event.recordIndex * 64 ||
        !HostEmissionSameStamp(event.stamp, tick.value.stamp)) {
        tick.invalid = true; return;
    }
    if (event.wrapperOutcome == TraceOutcome::NullReturn && event.actor == 0) return;
    // The actual first native return can still be pending: ready actor metadata
    // is a later census fact. Nonnull is the native emitter's success branch;
    // require its exact selected record and post-return cache attachment below.
    if (!event.actor || (event.wrapperOutcome != TraceOutcome::Observed &&
        event.wrapperOutcome != TraceOutcome::ActorReadUnavailable) ||
        (event.actorAvailable && (!event.objectIdMatchesRecord || event.actorObjectId != 302))) {
        tick.invalid = true; return;
    }
    if (++tick.successfulReturns != 1) { tick.invalid = true; return; }
    tick.value.firstActor = event.actor;
    tick.value.firstRecordIndex = event.recordIndex;
    tick.value.firstActorMetadataAvailable = event.actorAvailable;
}

void FinishHostEmission() {
    auto& tick = g_hostEmissionTick;
    if (!tick.active) return;
    tick.active = false;
    tick.value.after = CaptureTraceState(tick.value.controller);
    HostFirstEmission current;
    if (tick.invalid || g_hostEmissionPoison.load() || !IsDiagnosticGameThread() ||
        GetCurrentThreadId() != tick.thread || GetCurrentFiber() != tick.fiber || g_role() != 1 ||
        !KnownMutationTicketCurrent(tick.ticket) ||
        !ReadHostEmissionDefinition(tick.value.controller, current) ||
        !HostEmissionSameDefinition(tick.value, current) || !tick.value.after.controllerAvailable ||
        !tick.value.after.cacheAvailable) {
        RefuseHostEmission("first original return incomplete/changed"); return;
    }
    if (!tick.successfulReturns) return; // No emission. Never substitute AL1 or counts.
    const auto firstId = Field<std::uint16_t>(tick.value.records[tick.value.firstRecordIndex].data(), 0x1E);
    unsigned attached = 0;
    for (const auto id : tick.value.after.cacheIds) {
        if (id == firstId) ++attached;
        else if (id != 0) { RefuseHostEmission("unexpected first-return cache content"); return; }
    }
    if (attached != 1) { RefuseHostEmission("first-return cache attachment unavailable"); return; }
    if (!HostEmissionLock()) return;
    if (g_hostEmission.status == HostFirstEmissionStatus::Waiting &&
        g_hostEmission.initSerial == tick.value.initSerial) {
        tick.value.status = HostFirstEmissionStatus::Recorded;
        tick.value.reason = "first successful scheduled native wrapper and normal original return";
        g_hostEmission = tick.value;
        g_hostEmissionTicket = tick.ticket;
    }
    ReleaseSRWLockExclusive(&g_hostEmissionLock);
}

void PublishTrace(const TraceEvent& event) {
    // The native task must never wait behind a diagnostics consumer.
    if (!TryAcquireSRWLockExclusive(&g_traceLock)) {
        ++g_traceDropped;
        if (event.kind == TraceKind::Geometry) ++g_geometryDropped;
        if (event.geometry.eventGate.requested) SaturatingIncrement(g_eventGateDropped);
        return;
    }
    if (g_traceCount == TRACE_QUEUE_CAP) {
        ++g_traceDropped;
        if (event.kind == TraceKind::Geometry) ++g_geometryDropped;
        if (event.geometry.eventGate.requested) SaturatingIncrement(g_eventGateDropped);
    } else {
        g_traceQueue[(g_traceRead + g_traceCount) % TRACE_QUEUE_CAP] = event;
        ++g_traceCount;
        ++g_tracePublished;
    }
    ReleaseSRWLockExclusive(&g_traceLock);
}

bool ParseDecimal(const char*& cursor, std::uint32_t maximum, std::uint32_t& value) {
    value = 0;
    if (*cursor < '0' || *cursor > '9') return false;
    do {
        const auto digit = static_cast<std::uint32_t>(*cursor++ - '0');
        if (value > maximum / 10 || (value == maximum / 10 && digit > maximum % 10)) return false;
        value = value * 10 + digit;
    } while (*cursor >= '0' && *cursor <= '9');
    return true;
}

GeometryConfig ReadGeometryConfig(bool trace) {
    GeometryConfig config;
    char enabled[8] {};
    if (!trace || GetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return config;
    config.requested = true;
    char load[16] {}, transition[16] {}, location[80] {};
    const auto a = GetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOAD", load, sizeof(load));
    const auto b = GetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_TRANSITION", transition, sizeof(transition));
    const auto c = GetEnvironmentVariableA("KH2COOP_SPAWN_GEOMETRY_LOCATION", location, sizeof(location));
    if (!a || a >= sizeof(load) || !b || b >= sizeof(transition) || !c || c >= sizeof(location)) return config;
    const char* cursor = load;
    if (!ParseDecimal(cursor, UINT32_MAX, config.load) || *cursor || config.load < 2) return config;
    cursor = transition;
    if (!ParseDecimal(cursor, UINT32_MAX, config.transition) || *cursor || !config.transition) return config;
    cursor = location;
    constexpr unsigned offsets[] = {0, 1, 2, 4, 6, 8};
    for (unsigned i = 0; i < 6; ++i) {
        std::uint32_t value = 0;
        if (!ParseDecimal(cursor, i < 3 ? 255 : 65535, value)) return config;
        config.location[offsets[i]] = static_cast<std::uint8_t>(value);
        if (i >= 3) config.location[offsets[i] + 1] = static_cast<std::uint8_t>(value >> 8);
        if (i != 5) { if (*cursor++ != ',') return config; }
        else if (*cursor) return config;
    }
    config.configured = true;
    return config;
}

bool EnrollmentRequested() {
    char enabled[8] {};
    return GetEnvironmentVariableA("KH2COOP_SPAWN_ENROLL_OBSERVE", enabled, sizeof(enabled)) == 1 && enabled[0] == '1';
}

bool OriginalPhaseRequested() {
    char enabled[8] {};
    return GetEnvironmentVariableA("KH2COOP_SPAWN_ORIGINAL_PHASE_OBSERVE", enabled, sizeof(enabled)) == 1 && enabled[0] == '1';
}

bool ConstructionLineageRequested() {
    char enabled[8] {};
    return GetEnvironmentVariableA("KH2COOP_NATURAL_RESOURCE_TRACE", enabled, sizeof(enabled)) == 1 &&
        enabled[0] == '1';
}

void ConfigureConstructionLineage(bool trace) {
    g_constructionRequested = ConstructionLineageRequested();
    g_constructionConfigured = g_constructionRequested.load() && trace &&
        g_fixedInstalled.load() && g_generatedInstalled.load();
    SaturatingIncrement(g_constructionCoverage);
}

void ConfigureOriginalPhase(bool trace) {
    g_originalPhaseRequested = OriginalPhaseRequested();
    g_originalPhaseConfigured = g_originalPhaseRequested.load() && trace && g_geometryConfig.configured &&
        g_geometryInstalled.load() && g_enrollmentConfigured.load() && g_eventGateInstalled.load();
    SaturatingIncrement(g_originalPhaseCoverage);
}

OriginalPhaseSample CaptureOriginalPhase(uintptr_t caller) {
    OriginalPhaseSample sample;
    const auto& frame = g_originalPhaseFrame;
    sample.invocation = frame.invocation; sample.updateSequence = frame.updateSequence;
    sample.controller = frame.controller; sample.point = frame.point;
    sample.phase = frame.phase; sample.source = frame.source; sample.available = frame.available;
    sample.caller = caller;
    sample.callerRvaAvailable = caller >= g_exeBase && caller - g_exeBase < g_imageSize;
    if (sample.callerRvaAvailable) sample.callerRva = caller - g_exeBase;
    return sample;
}

bool GeometryLocationMatches(const Stamp& stamp) {
    // NOW+3 is not part of the six-field location contract.
    for (unsigned i = 0; i < stamp.location.size(); ++i)
        if (i != 3 && stamp.location[i] != g_geometryConfig.location[i]) return false;
    return true;
}

void GeometryDeadline(std::uint64_t now) {
    if (g_geometryTerminal.load() != GeometryTerminal::Armed) return;
    const auto started = g_geometryStarted.load();
    if (now < started || now - started >= 2000) g_geometryTerminal = GeometryTerminal::Deadline;
}

void ObserveGeometryLifecycle(const Stamp& stamp, std::uint64_t now, std::uint8_t role) {
    if (!g_geometryConfig.configured || !g_geometryInstalled.load()) return;
    if (role == 2 && stamp.load < g_geometryConfig.load && stamp.transition < g_geometryConfig.transition)
        g_geometryPrior = true;
    const bool target = role == 2 && stamp.load == g_geometryConfig.load && stamp.transition == g_geometryConfig.transition && GeometryLocationMatches(stamp);
    if (g_geometryTerminal.load() == GeometryTerminal::Waiting && g_geometryPrior.load() && target) {
        g_geometryStarted = now;
        g_geometryTerminal = GeometryTerminal::Armed;
    } else if (g_geometryTerminal.load() == GeometryTerminal::Armed && !target) {
        g_geometryTerminal = GeometryTerminal::LifecycleChanged;
    }
    GeometryDeadline(now);
}

bool ReadGeometryDefinition(const Qualification& q, GeometryObservation& out) {
    Controller native {};
    std::int32_t count = 0;
    std::array<TableEntry, CONTROLLER_CAP> entries {};
    if (!ReadNative(g_exeBase + CONTROLLER_COUNT, count) || count < 1 || count > static_cast<std::int32_t>(entries.size()) ||
        !CopyNative(g_exeBase + CONTROLLER_TABLE, entries.data(), static_cast<std::size_t>(count) * sizeof(TableEntry))) return false;
    unsigned matches = 0;
    for (int i = 0; i < count; ++i) if (entries[i].controller == q.controller && !(entries[i].flags & 1)) {
        if (entries[i].key != q.key) return false;
        ++matches;
        out.tableIndex = static_cast<std::uint32_t>(i);
        std::memcpy(out.tableEntry.data(), &entries[i], sizeof(TableEntry));
    }
    if (matches != 1) return false; // diagnostic uniqueness; production qualification unchanged
    out.tableCount = static_cast<std::uint32_t>(count);
    if (!CopyNative(q.controller, out.controllerBytes.data(), out.controllerBytes.size())) return false;
    std::memcpy(&native, out.controllerBytes.data(), sizeof(native));
    if (native.key != q.key || native.header != q.header ||
        native.spawnArray != q.spawnArray || !CopyNative(native.header, out.header.data(), out.header.size()) ||
        out.header[0] != 2 || Field<std::uint16_t>(out.header.data(), 2) != 30 ||
        Field<std::uint16_t>(out.header.data(), 4) != 5 || Field<std::uint16_t>(out.header.data(), 6) != 7 ||
        native.spawnArray != native.header + 0x2C || native.regionArray != native.spawnArray + out.records.size() ||
        !CopyNative(native.spawnArray, out.records.data(), out.records.size()) ||
        !CopyNative(native.regionArray, out.descriptors.data(), out.descriptors.size())) return false;
    if (q.geometryRecordsVerified && out.records != q.geometryRecords) return false;
    for (unsigned i = 0; i < 5; ++i)
        if (out.records[i * 64 + 0x1C] != 2 || out.records[i * 64 + 0x1D] != 0) return false;
    uintptr_t node = native.regionHead;
    unsigned descriptorMask = 0;
    for (unsigned i = 0; i < 7; ++i) {
        if (!node || !CopyNative(node, out.regionBytes[i].data(), out.regionBytes[i].size())) return false;
        for (unsigned previous = 0; previous < i; ++previous) if (out.regions[previous] == node) return false;
        out.regions[i] = node;
        const auto* bytes = out.regionBytes[i].data();
        const auto descriptor = Field<uintptr_t>(bytes, 0x68);
        if (Field<uintptr_t>(bytes, 0) != g_exeBase + BOX_VTABLE_RVA ||
            descriptor < native.regionArray || descriptor - native.regionArray >= out.descriptors.size() ||
            (descriptor - native.regionArray) % 64) return false;
        const auto index = static_cast<unsigned>((descriptor - native.regionArray) / 64);
        if ((descriptorMask & (1U << index)) || Field<std::uint16_t>(out.descriptors.data() + index * 64, 0) != 0) return false;
        descriptorMask |= 1U << index;
        const auto rawHandle = Field<std::uint32_t>(bytes, 0x58);
        const auto handle = rawHandle & 0x7FFFFFFFU;
        if (!rawHandle) node = 0;
        else {
            uintptr_t region = 0;
            if (!ReadNative(g_exeBase + HANDLE_REGIONS_RVA + (handle >> 25) * 8, region) ||
                !region || region == UINTPTR_MAX || (region & 0x1FFFFFFU)) return false;
            node = region | (handle & 0x1FFFFFFU);
        }
    }
    return !node && native.regionTail == out.regions.back() && descriptorMask == 0x7F && SameStamp(q.stamp);
}

bool SameGeometryDefinition(const GeometryObservation& before, const GeometryObservation& after) {
    if (before.tableCount != after.tableCount || before.tableIndex != after.tableIndex || before.tableEntry != after.tableEntry ||
        before.records != after.records || before.descriptors != after.descriptors ||
        before.regions != after.regions || before.regionBytes != after.regionBytes) return false;
    for (unsigned i = 0; i < before.header.size(); ++i)
        if (i != 0xE && before.header[i] != after.header[i]) return false; // native activation marker may change
    return true;
}

void BeginGeometry(const Qualification& q, std::uint8_t role) {
    if (g_geometryConfig.configured) GeometryDeadline(GetTickCount64());
    if (!g_traceTick.active || g_traceNestedDepth || !IsDiagnosticGameThread() || role != 2 ||
        !g_geometryInstalled.load() || g_geometryTerminal.load() != GeometryTerminal::Armed ||
        q.key != 808476514 || q.headerId != 30 || q.records != 5 ||
        q.stamp.load != g_geometryConfig.load || q.stamp.transition != g_geometryConfig.transition || !GeometryLocationMatches(q.stamp)) return;
    auto& event = g_traceTick.geometryEvent;
    event = {};
    event.kind = TraceKind::Geometry;
    event.controller = q.controller;
    event.tickSequence = g_traceTick.sequence;
    event.stamp = q.stamp;
    event.stampAvailable = true;
    event.role = role;
    event.roleAvailable = true;
    event.tickBefore = CaptureTraceState(q.controller);
    event.geometry.coverageSerial = g_geometryCoverage.load();
    event.geometry.eventGate.coverageSerial = g_eventGateCoverage.load();
    event.geometry.eventGate.requested = g_enrollmentRequested.load();
    event.geometry.eventGate.available = g_enrollmentConfigured.load() && g_eventGateInstalled.load();
    auto& phase = event.geometry.originalPhase;
    phase.requested = g_originalPhaseRequested.load();
    if (phase.requested) {
        phase.coverageSerial = g_originalPhaseCoverage.load();
        phase.invocation = g_originalPhaseFrame.invocation;
        phase.updateSequence = event.tickSequence;
        phase.controller = q.controller;
        phase.updateCaller = g_originalPhaseFrame.caller;
        phase.updateCallerRvaAvailable = phase.updateCaller >= g_exeBase && phase.updateCaller - g_exeBase < g_imageSize;
        if (phase.updateCallerRvaAvailable) phase.updateCallerRva = phase.updateCaller - g_exeBase;
        phase.overflow = g_originalPhaseFrame.overflow || phase.coverageSerial == UINT64_MAX;
        phase.available = g_originalPhaseConfigured.load() && event.geometry.eventGate.available &&
            g_originalPhaseFrame.available && g_originalPhaseFrame.phase == UpdatePhase::RunUpdateWork &&
            g_originalPhaseFrame.controller == q.controller && !phase.overflow;
        if (phase.available) g_originalPhaseFrame.updateSequence = event.tickSequence;
    }
    event.geometry.startedMs = g_geometryStarted.load();
    event.geometry.ordinal = ++g_geometryTicks;
    event.geometry.definitionAvailable = ReadGeometryDefinition(q, event.geometry);
    GeometryObservation check {};
    event.geometry.definitionAvailable = event.geometry.definitionAvailable && ReadGeometryDefinition(q, check) && SameGeometryDefinition(event.geometry, check);
    g_traceTick.geometrySelected = true;
    g_geometryScope = &event;
    if (event.geometry.ordinal >= 64) g_geometryTerminal = GeometryTerminal::TickLimit;
}

std::uint8_t ObserveBox(void* region, const float* point, uintptr_t caller) {
    auto* const event = g_geometryScope;
    if (!event || !IsDiagnosticGameThread()) {
        if (event) ++g_geometryForeign;
        return g_originalBox(region, point);
    }
    auto& geometry = event->geometry;
    if (caller != g_exeBase + BOX_RETURN_RVA || g_geometryPredicateDepth || g_traceNestedDepth) {
        geometry.nested = true;
        return g_originalBox(region, point);
    }
    GeometryPredicate* child = nullptr;
    if (geometry.calls < geometry.predicates.size()) {
        child = &geometry.predicates[geometry.calls];
        child->region = reinterpret_cast<uintptr_t>(region);
        for (const auto member : geometry.regions) if (member && member == child->region) child->member = true;
        child->inputAvailable = CopyNative(reinterpret_cast<uintptr_t>(point), child->inputBytes.data(), child->inputBytes.size());
    } else geometry.overflow = true;
    if (geometry.calls != UINT32_MAX) ++geometry.calls;
    else geometry.overflow = true;
    const unsigned previousDepth = g_geometryPredicateDepth;
    ++g_geometryPredicateDepth;
    std::uint8_t result = 0;
    __try {
        result = g_originalBox(region, point);
        if (child) { child->result = result; child->returned = true; }
    } __finally {
        g_geometryPredicateDepth = previousDepth;
        if (AbnormalTermination() && child) child->unwound = true;
    }
    return result;
}

std::uint8_t __fastcall HookedBox(void* region, const float* point) {
    return ObserveBox(region, point, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

// Only POD/TLS and saturating counters here: no native reads, role queries,
// logs, allocations or second predicate evaluation. Every entry calls its
// original once. This is selected-scope/caller attribution, not an original-
// phase token: production CopyHostActivation does not enter native update
// or predicate code. HookedUpdate reentrancy invalidates the selected parent;
// arbitrary callbacks bypassing that hook are outside this observer contract.
std::uint8_t ObserveEventGate(uintptr_t caller) {
    auto* const event = g_geometryScope;
    if (!event || !IsDiagnosticGameThread() ||
        caller != g_exeBase + EVENT_GATE_RETURN_RVA || !event->geometry.eventGate.available) {
        SaturatingIncrement(g_eventGateForeign);
        return g_originalEventGate();
    }
    auto& gate = event->geometry.eventGate;
    if (g_eventGateDepth || g_traceNestedDepth) {
        gate.nested = true;
        return g_originalEventGate();
    }
    if (gate.calls == UINT32_MAX) gate.overflow = true;
    else ++gate.calls;
    const bool first = gate.calls == 1 && !gate.overflow;
    auto& phase = event->geometry.originalPhase;
    if (first && phase.requested) phase.gateEntry = CaptureOriginalPhase(caller);
    const unsigned previousDepth = g_eventGateDepth;
    ++g_eventGateDepth;
    std::uint8_t result = 0;
    __try {
        result = g_originalEventGate();
        if (first) { gate.result = result; gate.returned = true; }
        if (first && phase.requested) phase.gateReturn = CaptureOriginalPhase(caller);
    } __finally {
        g_eventGateDepth = previousDepth;
        if (AbnormalTermination()) {
            gate.unwound = true;
            if (phase.requested) phase.unwound = true;
            SaturatingIncrement(g_eventGateUnwound);
        }
    }
    return result;
}

std::uint8_t __fastcall HookedEventGate() {
    return ObserveEventGate(reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

// Native faults keep their original exception and control flow. Only atomic
// diagnostic counters run in this filter; no logger/callback or C++ unwinding.
int NoteNativeFault(unsigned long code) {
    g_traceException.store(static_cast<std::uint32_t>(code), std::memory_order_relaxed);
    ++g_traceFaults;
    return EXCEPTION_CONTINUE_SEARCH;
}
void* CallOriginalWrapper(const void* record, void* controller) {
    __try {
        return g_originalWrapper(record, controller);
    } __except (NoteNativeFault(GetExceptionCode())) {
        return nullptr; // unreachable: filter always continues native exception search
    }
}

void* CallOriginalGenerated(const void* record, void* controller, const float* point) {
    __try {
        return g_originalGenerated(record, controller, point);
    } __except (NoteNativeFault(GetExceptionCode())) {
        return nullptr; // unreachable: preserve the original native exception
    }
}

bool CountFactoryCall(std::uint16_t& count, FactoryPredicates& observation) {
    if (count == 0xFFFFU) { observation.countOverflow = true; return false; }
    ++count;
    return count == 1;
}

std::uint8_t ObserveAdmission(float weight, uintptr_t caller) {
    const auto error=GetLastError();FactoryLocal* local=nullptr;auto* frame=FactoryParent(local);
    if(!frame || !frame->factory.eligible || !(frame->factory.coverageMask&FactoryAdmissionHook) ||
       caller!=g_exeBase+ADMISSION_RETURN_RVA){SetLastError(error);return g_originalAdmission(weight);}
    const auto depth=local->depth;const auto serial=frame->serial;auto& scope=frame->factory;
    const bool first=CountFactoryCall(scope.admissionCalls,scope);std::uint8_t result=0;bool returned=false;
    if(first){std::memcpy(&scope.weightBits,&weight,sizeof(weight));
        if(ReadNative(g_exeBase+ADMISSION_LIMIT_RVA,scope.limitBeforeBits))scope.operandMask|=1;
        if(ReadNative(g_exeBase+ADMISSION_USED_RVA,scope.usedBeforeBits))scope.operandMask|=2;}
    __try {SetLastError(error);result=g_originalAdmission(weight);returned=true;}
    __finally {const auto nativeError=GetLastError();
        if(FactoryCurrent(local,depth,serial)) {
            if(!returned)scope.admissionFault=true;
            else if(first){scope.admissionResult=result;scope.admissionReturned=true;
                if(ReadNative(g_exeBase+ADMISSION_LIMIT_RVA,scope.limitAfterBits))scope.operandMask|=4;
                if(ReadNative(g_exeBase+ADMISSION_USED_RVA,scope.usedAfterBits))scope.operandMask|=8;}
        }else ++g_traceUnavailable;
        SetLastError(nativeError);
    }return result;
}
std::uint8_t __fastcall HookedAdmission(float weight) {return ObserveAdmission(weight,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
void* ObserveAllocation(std::size_t size, uintptr_t caller) {
    const auto error=GetLastError();FactoryLocal* local=nullptr;auto* frame=FactoryParent(local);
    if(!frame || !frame->factory.eligible || !(frame->factory.coverageMask&FactoryAllocationHook) ||
       caller!=g_exeBase+ALLOCATION_RETURN_RVA){SetLastError(error);return g_originalAllocation(size);}
    const auto depth=local->depth;const auto serial=frame->serial;auto& scope=frame->factory;
    const bool first=CountFactoryCall(scope.allocationCalls,scope);void* result=nullptr;bool returned=false;
    if(first)scope.allocationSize=size;
    __try {SetLastError(error);result=g_originalAllocation(size);returned=true;}
    __finally {const auto nativeError=GetLastError();
        if(FactoryCurrent(local,depth,serial)) {
            if(!returned)scope.allocationFault=true;
            else if(first){scope.allocationResult=reinterpret_cast<uintptr_t>(result);scope.allocationReturned=true;}
        }else ++g_traceUnavailable;
        SetLastError(nativeError);
    }return result;
}
void* __fastcall HookedAllocation(std::size_t size) {return ObserveAllocation(size,reinterpret_cast<uintptr_t>(_ReturnAddress()));}

void CompleteFactoryObservation(TraceEvent& event) {
    auto& f = event.factory;
    f.complete = event.wrapperComplete && f.eligible && f.coverageMask == FactoryAllHooks &&
        f.coverageSerial == g_factoryCoverageSerial.load() &&
        f.coverageMask == g_factoryInstalled.load() && !f.unwound &&
        !f.admissionFault && !f.allocationFault && !f.countOverflow &&
        (!f.admissionCalls || f.admissionReturned) && (!f.allocationCalls || f.allocationReturned);
    if (f.countOverflow || f.admissionCalls > 1 || f.allocationCalls > 1) {
        f.outcome = FactoryOutcome::Ambiguous;
    } else if (f.complete && f.admissionCalls == 1 && event.recordAvailable && DirectObjectId(event.objectId)) {
        if (!f.admissionResult && !f.allocationCalls && !event.actor)
            f.outcome = FactoryOutcome::AdmissionRejected;
        else if (f.admissionResult && f.allocationCalls == 1 && f.allocationSize == 0xD50) {
            if (!f.allocationResult && !event.actor) f.outcome = FactoryOutcome::Type4AllocationFailed;
            else if (f.allocationResult && event.actor == f.allocationResult)
                f.outcome = FactoryOutcome::AllocationPassed;
            else f.outcome = FactoryOutcome::Ambiguous;
        } else if (f.allocationCalls || !f.admissionResult) f.outcome = FactoryOutcome::Ambiguous;
    }
    if (f.outcome == FactoryOutcome::Ambiguous) ++g_factoryAmbiguous;
}

void CaptureConstructionSample(uintptr_t controllerAddress, uintptr_t recordAddress,
                               NativeConstructionSample& out) {
    out = {};
    out.mutation = g_knownMutation.Snapshot();
    out.stampRead = CaptureDiagnosticStamp(g_exeBase, out.stamp);
    out.controllerRead = CopyNative(controllerAddress, out.controllerBytes.data(), out.controllerBytes.size());
    out.recordRead = CopyNative(recordAddress, out.recordBytes.data(), out.recordBytes.size());
    if (out.controllerRead) {
        out.group = Field<std::uint32_t>(out.controllerBytes.data(), 0);
        out.header = Field<uintptr_t>(out.controllerBytes.data(), 8);
        out.spawnArray = Field<uintptr_t>(out.controllerBytes.data(), 0x30);
        out.regionArray = Field<uintptr_t>(out.controllerBytes.data(), 0x38);
        out.headerRead = CopyNative(out.header, out.headerBytes.data(), out.headerBytes.size());
    }
    if (out.headerRead) {
        out.nativeType = out.headerBytes[0];
        out.headerId = Field<std::uint16_t>(out.headerBytes.data(), 2);
        out.declaredRecords = Field<std::uint16_t>(out.headerBytes.data(), 4);
        out.fiveRecordLayout = out.declaredRecords == out.records.size() &&
            ReadableRange(out.header, out.headerBytes.size() + sizeof(out.records)) &&
            out.spawnArray == out.header + out.headerBytes.size() &&
            out.regionArray == out.spawnArray + sizeof(out.records);
        if (out.fiveRecordLayout) {
            for (std::size_t i = 0; i < out.records.size(); ++i) {
                if (CopyNative(out.spawnArray + i * 64, out.records[i].data(), out.records[i].size()))
                    out.recordReadMask |= static_cast<std::uint8_t>(1U << i);
            }
            out.recordIndexAvailable = recordAddress >= out.spawnArray &&
                recordAddress - out.spawnArray < sizeof(out.records) &&
                (recordAddress - out.spawnArray) % 64 == 0;
            if (out.recordIndexAvailable) {
                out.recordIndex = static_cast<std::uint16_t>((recordAddress - out.spawnArray) / 64);
                out.recordMatchesDefinition = out.recordRead &&
                    (out.recordReadMask & (1U << out.recordIndex)) &&
                    out.recordBytes == out.records[out.recordIndex];
            }
        }
    }
    out.countBeforeRead = ReadNative(g_exeBase + CONTROLLER_COUNT, out.countBefore);
    const bool boundedCount = out.countBeforeRead && out.countBefore >= 0 &&
        out.countBefore <= static_cast<std::int32_t>(out.table.size());
    bool matchingKey = false;
    if (boundedCount) {
        for (std::int32_t i = 0; i < out.countBefore; ++i) {
            const auto index = static_cast<std::size_t>(i);
            if (!CopyNative(g_exeBase + CONTROLLER_TABLE + index * 16,
                            out.table[index].data(), out.table[index].size())) continue;
            out.tableReadMask |= std::uint64_t{1} << index;
            const auto* bytes = out.table[index].data();
            if (Field<uintptr_t>(bytes, 8) == controllerAddress &&
                (Field<std::uint32_t>(bytes, 4) & 1U) == 0) {
                ++out.tableMatches;
                matchingKey = out.controllerRead && Field<std::uint32_t>(bytes, 0) == out.group &&
                    Field<std::uint32_t>(bytes, 4) == 0;
            }
        }
    }
    out.countAfterRead = ReadNative(g_exeBase + CONTROLLER_COUNT, out.countAfter);
    const auto expectedMask = boundedCount ? out.countBefore == 64 ? UINT64_MAX :
        (std::uint64_t{1} << out.countBefore) - 1 : 0;
    out.tableComplete = boundedCount && out.countAfterRead && out.countBefore == out.countAfter &&
        out.tableReadMask == expectedMask;
    out.ordinaryAssociationSampled = out.tableComplete && out.tableMatches == 1 && matchingKey;
}

bool NextConstructionSerial(std::uint64_t& serial) {
    auto value = g_constructionSerial.load(std::memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (g_constructionSerial.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) {
            serial = value + 1;
            return true;
        }
    }
    return false;
}

void BeginConstructionLineage(NativeConstructionLineage& out, const TraceEvent& event,
                              uintptr_t record, uintptr_t controller,
                              const NativeConstructionLineage* previous, unsigned depth) {
    out = {};
    out.captured = true;
    out.coverage = g_constructionCoverage.load();
    out.wrapperSequence = event.sequence;
    out.enclosingThreadSerial = previous ? previous->serial : 0;
    out.threadId = GetCurrentThreadId();
    out.depth = depth;
    out.wrapper = event.wrapper;
    out.controller = controller;
    out.record = record;
    out.callerRva = event.callerRva; out.callerRvaAvailable = event.callerRvaAvailable;
    out.tickSequence = event.tickSequence; out.enclosingTick = event.enclosingTick;
    out.dispatcherSequence = event.dispatcherSequence; out.enclosingDispatcher = event.enclosingDispatcher;
    out.dispatcherCallerRva = event.dispatcherCallerRva;
    out.dispatcherCallerRvaAvailable = event.dispatcherCallerRvaAvailable;
    out.scriptSequence = event.scriptSequence; out.enclosingScript = event.enclosingScript42DC10;
    out.scriptCallerRva = event.scriptCallerRva; out.scriptCallerRvaAvailable = event.scriptCallerRvaAvailable;
    out.droppedBefore = g_constructionDropped.load();
    out.ambiguous = depth != 0;
    out.overflow = depth >= CONSTRUCTION_DEPTH_CAP || !out.coverage || out.coverage == UINT64_MAX ||
        !NextConstructionSerial(out.serial);
    out.candidateThreadParent = IsDiagnosticGameThread() && !out.overflow;
    if (out.candidateThreadParent) CaptureConstructionSample(controller, record, out.samples[0]);
}

void CompleteConstructionLineage(NativeConstructionLineage& out) {
    out.normalReturn = true;
    out.droppedAfter = g_constructionDropped.load();
    out.traceQueueStable = out.droppedBefore == out.droppedAfter;
    if (!out.candidateThreadParent) return;
    CaptureConstructionSample(out.controller, out.record, out.samples[1]);
    out.coverageAfter = g_constructionCoverage.load();
    out.coverageStable = g_constructionConfigured.load() && out.coverage == out.coverageAfter &&
        out.coverage != 0 && out.coverage != UINT64_MAX;
    const auto& first = out.samples[0];
    const auto& last = out.samples[1];
    out.sampledIdentityStable = first.controllerRead && last.controllerRead &&
        first.group == last.group && first.header == last.header &&
        first.spawnArray == last.spawnArray && first.regionArray == last.regionArray;
    out.sampledDefinitionStable = out.sampledIdentityStable && first.headerRead && last.headerRead &&
        first.headerBytes == last.headerBytes && first.fiveRecordLayout && last.fiveRecordLayout &&
        first.recordReadMask == 31 && last.recordReadMask == 31 && first.records == last.records &&
        first.recordMatchesDefinition && last.recordMatchesDefinition && first.recordIndex == last.recordIndex;
    out.sampledTableStable = first.tableComplete && last.tableComplete &&
        first.countBefore == last.countBefore && first.table == last.table &&
        first.ordinaryAssociationSampled && last.ordinaryAssociationSampled;
    out.sampledLifecycleStable = first.stampRead && last.stampRead &&
        first.stamp.transition == last.stamp.transition && first.stamp.load == last.stamp.load &&
        first.stamp.location == last.stamp.location;
    out.knownMutationStable = g_knownMutation.Current(first.mutation) &&
        last.mutation.available && first.mutation.revision == last.mutation.revision;
}

void PublishConstructionLineage(const NativeConstructionLineage& out) {
    if (!TryAcquireSRWLockExclusive(&g_constructionLock)) {
        SaturatingIncrement(g_constructionDropped);
        return;
    }
    if (g_constructionCount < CONSTRUCTION_QUEUE_CAP) {
        g_constructionQueue[(g_constructionRead + g_constructionCount) % CONSTRUCTION_QUEUE_CAP] = out;
        ++g_constructionCount;
        SaturatingIncrement(g_constructionPublished);
    } else SaturatingIncrement(g_constructionDropped);
    ReleaseSRWLockExclusive(&g_constructionLock);
}

void* RunFactoryWrapper(TraceEvent* event,const void* record,void* controller,const float* point) {
    const auto error=GetLastError();const auto anchor=reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
    auto* local=g_factoryStorage.Current();
    if(!local || !g_rawDiagnosticEnabled || local->depth>=FACTORY_DEPTH_CAP) {
        if(local && local->depth>=FACTORY_DEPTH_CAP)g_factoryStorage.Reject(local);
        ++g_traceUnavailable;event->factory.countOverflow=true;
        SetLastError(error);
        auto* result=event->wrapper==TraceWrapper::Fixed?CallOriginalWrapper(record,controller):CallOriginalGenerated(record,controller,point);
        event->actor=reinterpret_cast<uintptr_t>(result);event->wrapperComplete=true;return result;
    }
    const unsigned index=local->depth;
    if(index && local->frames[index-1].anchor<=anchor){g_factoryStorage.Reject(local);
        SetLastError(error);return event->wrapper==TraceWrapper::Fixed?CallOriginalWrapper(record,controller):CallOriginalGenerated(record,controller,point);}
    auto& frame=local->frames[index];frame={};frame.anchor=anchor;frame.serial=++g_factoryScopeSerial;
    frame.factory.depth=index;frame.factory.coverageSerial=g_factoryCoverageSerial.load();
    frame.factory.coverageMask=g_factoryInstalled.load();frame.factory.eligible=IsDiagnosticGameThread();
    if(!frame.factory.eligible)++g_factoryForeign;
    event->factory=frame.factory;
    const bool constructionEnabled=g_constructionConfigured.load();
    if(constructionEnabled) {
        const auto* previous=index && local->frames[index-1].constructionVisible?&local->frames[index-1].construction:nullptr;
        BeginConstructionLineage(frame.construction,*event,reinterpret_cast<uintptr_t>(record),reinterpret_cast<uintptr_t>(controller),previous,index);
        frame.constructionVisible=frame.construction.candidateThreadParent;
    }
    const auto serial=frame.serial;++local->depth;
    resourcetrace::ConstructionToken resourceToken{};
    if(constructionEnabled)resourceToken=resourcetrace::BeginConstruction();
    void* result=nullptr;bool returned=false;
    __try {SetLastError(error);
        result=event->wrapper==TraceWrapper::Fixed?CallOriginalWrapper(record,controller):CallOriginalGenerated(record,controller,point);
        returned=true;event->actor=reinterpret_cast<uintptr_t>(result);event->wrapperComplete=true;
    } __finally {const auto nativeError=GetLastError();
        const bool current=FactoryCurrent(local,index+1,serial);
        if(current) {
            event->factory=frame.factory;
            if(constructionEnabled) {
                if(returned)CompleteConstructionLineage(frame.construction);
                else {frame.construction.unwound=true;frame.construction.normalReturn=false;
                    frame.construction.droppedAfter=g_constructionDropped.load();frame.construction.coverageAfter=g_constructionCoverage.load();}
                PublishConstructionLineage(frame.construction);
            }
        }else {event->factory.complete=false;event->factory.countOverflow=true;++g_traceUnavailable;}
        if(constructionEnabled)resourcetrace::EndConstruction(resourceToken,returned && current);
        if(current){frame={};local->depth=index;}
        if(!returned){event->factory.unwound=true;event->factory.complete=false;
            event->factory.outcome=FactoryOutcome::Unknown;event->outcome=event->wrapperOutcome=TraceOutcome::Unavailable;
            event->reason="native wrapper interrupted";++g_factoryUnwound;++g_traceUnavailable;PublishTrace(*event);}
        SetLastError(nativeError);
    }
    const auto nativeError=GetLastError();CompleteFactoryObservation(*event);SetLastError(nativeError);return result;
}

void ReadTraceActor(TraceEvent& event) {
    if (!event.actor) { event.outcome = TraceOutcome::NullReturn; event.reason = "native null return"; return; }
    char name[2] {};
    if (!ReadableRange(event.actor, 0xA00) ||
        !ReadNative(event.actor + offsets::actor::OBJENTRY_PTR, event.objentry) ||
        !ReadNative(event.actor + 0x5C0, event.status) || !event.objentry || !event.status ||
        !ReadableRange(event.objentry, 0x60) || !ReadableRange(event.status, 8) ||
        !ReadNative(event.actor + 0x9E8, event.actorController) ||
        !ReadNative(event.actor + 0x9F0, event.actorRecord) ||
        !ReadNative(event.objentry + offsets::objentry::OBJECT_ID, event.actorObjectId) ||
        !ReadNative(event.objentry + offsets::objentry::TYPE_FLAGS, event.actorType) ||
        !CopyNative(event.objentry + offsets::objentry::NAME, name, sizeof(name)) ||
        !ReadNative(event.status, event.hp) || !ReadNative(event.status + 4, event.maxHp)) {
        event.outcome = TraceOutcome::ActorReadUnavailable;
        event.reason = "returned actor metadata read unavailable";
        return;
    }
    uintptr_t objentry = 0, status = 0, controller = 0, record = 0;
    if (!ReadNative(event.actor + offsets::actor::OBJENTRY_PTR, objentry) ||
        !ReadNative(event.actor + 0x5C0, status) ||
        !ReadNative(event.actor + 0x9E8, controller) || !ReadNative(event.actor + 0x9F0, record) ||
        objentry != event.objentry || status != event.status ||
        controller != event.actorController || record != event.actorRecord) {
        event.outcome = TraceOutcome::ActorReadUnavailable;
        event.reason = "returned actor identity changed";
        return;
    }
    event.actorAvailable = true;
    event.objectIdMatchesRecord = event.recordAvailable && event.actorObjectId == event.objectId;
    if ((event.actorType != offsets::objentry::TYPE_BOSS && event.actorType != offsets::objentry::TYPE_MOB) ||
        (name[0] == 'F' && name[1] == '_')) {
        event.outcome = TraceOutcome::OutOfScope;
        event.reason = "returned actor is not combat";
        return;
    }
    if (event.actorController != event.controller || event.actorRecord != event.record) {
        event.outcome = TraceOutcome::IdentityMismatch;
        event.reason = "returned actor provenance mismatch";
        return;
    }
    event.outcome = TraceOutcome::Observed;
    event.reason = "native return observed; census/binding unconfirmed";
}

bool ModuleRva(uintptr_t address, uintptr_t& rva) {
    if (g_imageSize == 0 || address < g_exeBase || address - g_exeBase >= g_imageSize) return false;
    rva = address - g_exeBase;
    return true;
}

void* ObserveWrapper(const void* record, void* controller, const float* point,
                     TraceWrapper wrapper, uintptr_t caller) {
    const auto entryError=GetLastError();
    TraceEvent event;
    event.sequence = ++g_traceStarted;
    event.wrapper = wrapper;
    event.callerRvaAvailable = ModuleRva(caller, event.callerRva);
    if (!event.callerRvaAvailable) ++g_traceUnsupported; // observed, but module ancestry unknown
    event.roleAvailable = g_role && IsDiagnosticGameThread();
    event.role = event.roleAvailable ? g_role() : 0;
    event.controller = reinterpret_cast<uintptr_t>(controller);
    event.record = reinterpret_cast<uintptr_t>(record);
    event.stampAvailable = CaptureDiagnosticStamp(g_exeBase, event.stamp);
    // Diagnostic profile: TLS tick/dispatcher/script correlation is unsupported.
    // Policy-facing g_traceTick storage and RunUpdate sequencing remain untouched.
    event.enclosingTick=event.enclosingDispatcher=event.enclosingScript42DC10=false;
    event.wrapperBefore = CaptureTraceState(event.controller);
    event.recordAvailable = CopyNative(event.record, event.recordBytes.data(), event.recordBytes.size());
    if (event.recordAvailable) {
        event.nativeRecordId = Field<std::uint16_t>(event.recordBytes.data(), 0x1E);
        event.objectId = Field<std::uint32_t>(event.recordBytes.data(), 0);
        Controller native;
        // This proves an index only; it is NOT the activation qualifier and does
        // not exclude initializer, dynamic-position, boss, or other wrapper paths.
        if (event.wrapperBefore.controllerAvailable && ReadNative(event.controller, native) &&
            native.header == event.wrapperBefore.header && native.spawnArray == event.wrapperBefore.spawnArray &&
            event.wrapperBefore.recordCount != 0 && event.wrapperBefore.recordCount <= RECORD_CAP &&
            ReadableRange(native.header, 0x2C + static_cast<std::size_t>(event.wrapperBefore.recordCount) * 0x40) &&
            native.spawnArray == native.header + 0x2C &&
            native.regionArray == native.spawnArray + static_cast<uintptr_t>(event.wrapperBefore.recordCount) * 0x40 &&
            event.record >= native.spawnArray &&
            event.record - native.spawnArray < static_cast<uintptr_t>(event.wrapperBefore.recordCount) * 0x40 &&
            (event.record - native.spawnArray) % 0x40 == 0) {
            event.recordIndex = static_cast<std::uint16_t>((event.record - native.spawnArray) / 0x40);
            event.recordIndexAvailable = true;
        }
    }
    if (wrapper == TraceWrapper::Generated)
        event.generatedPointAvailable = CopyNative(reinterpret_cast<uintptr_t>(point),
                                                  event.generatedPoint.data(), sizeof(event.generatedPoint));
    // This is the only original call on this path. No native retry or suppression.
    SetLastError(entryError);
    void* const actor = RunFactoryWrapper(&event, record, controller, point);
    const auto nativeError=GetLastError();
    ReadTraceActor(event);
    event.wrapperAfter = CaptureTraceState(event.controller);
    event.postStampAvailable = CaptureDiagnosticStamp(g_exeBase, event.postStamp);
    event.lifecycleStable = event.stampAvailable && event.postStampAvailable &&
        event.stamp.transition == event.postStamp.transition && event.stamp.load == event.postStamp.load &&
        event.stamp.location == event.postStamp.location;
    if (event.recordAvailable) {
        std::array<std::uint8_t, 64> finalRecord {};
        if (!CopyNative(event.record, finalRecord.data(), finalRecord.size()) ||
            finalRecord != event.recordBytes) {
            event.outcome = TraceOutcome::Unavailable;
            event.reason = "record changed during native creation";
            event.recordIndexAvailable = false;
            event.factory.complete = false;
            event.factory.outcome = FactoryOutcome::Unknown;
        }
    }
    if (!event.lifecycleStable) {
        event.outcome = TraceOutcome::Unavailable;
        event.reason = "wrapper lifecycle unavailable";
    }
    event.wrapperOutcome = event.outcome;
    ObserveHostEmissionWrapper(event);
    if (event.enclosingTick) {
        if (g_traceTick.count < TRACE_TICK_CAP) g_traceTick.events[g_traceTick.count++] = event;
        else ++g_traceDropped;
    } else {
        if (event.outcome != TraceOutcome::Observed && event.outcome != TraceOutcome::NullReturn &&
            event.outcome != TraceOutcome::OutOfScope) ++g_traceUnavailable;
        PublishTrace(event);
    }
    SetLastError(nativeError);return actor;
}

void* __fastcall HookedWrapper(const void* record, void* controller) {
    if (populationauthority::Requested()) {
        const auto error=GetLastError();void* actor=nullptr;bool returned=false;
        const auto token=populationauthority::Enter(populationauthority::Kind::Fixed,
            reinterpret_cast<uintptr_t>(controller),reinterpret_cast<uintptr_t>(record),0,reinterpret_cast<uintptr_t>(_ReturnAddress()));
        __try {SetLastError(error);actor=g_originalWrapper(record,controller);returned=true;}
        __finally {const auto nativeError=GetLastError();populationauthority::Exit(token,populationauthority::Kind::Fixed,
            reinterpret_cast<uintptr_t>(controller),reinterpret_cast<uintptr_t>(record),0,reinterpret_cast<uintptr_t>(actor),returned);SetLastError(nativeError);}
        return actor;
    }
    return ObserveWrapper(record, controller, nullptr, TraceWrapper::Fixed,
                          reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

void* __fastcall HookedGenerated(const void* record, void* controller, const float* point) {
    if (populationauthority::Requested()) {
        const auto error=GetLastError();void* actor=nullptr;bool returned=false;
        const auto token=populationauthority::Enter(populationauthority::Kind::Generated,
            reinterpret_cast<uintptr_t>(controller),reinterpret_cast<uintptr_t>(record),0,reinterpret_cast<uintptr_t>(_ReturnAddress()));
        __try {SetLastError(error);actor=g_originalGenerated(record,controller,point);returned=true;}
        __finally {const auto nativeError=GetLastError();populationauthority::Exit(token,populationauthority::Kind::Generated,
            reinterpret_cast<uintptr_t>(controller),reinterpret_cast<uintptr_t>(record),0,reinterpret_cast<uintptr_t>(actor),returned);SetLastError(nativeError);}
        return actor;
    }
    return ObserveWrapper(record, controller, point, TraceWrapper::Generated,
                          reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

void* __fastcall HookedNaturalFactory(std::uint32_t rawId,const float* point,float yaw) {
    const auto error=GetLastError();void* result=nullptr;bool returned=false;
    const auto token=populationauthority::EnterFactory(rawId,point,yaw,reinterpret_cast<uintptr_t>(_ReturnAddress()));
    __try {SetLastError(error);result=g_originalNaturalFactory(rawId,point,yaw);returned=true;}
    __finally {const auto nativeError=GetLastError();populationauthority::Exit(token,populationauthority::Kind::Factory,
        0,0,0,reinterpret_cast<uintptr_t>(result),returned);SetLastError(nativeError);}
    return result;
}

std::uint64_t RunDispatcherScope(void* controller, void* region, uintptr_t caller) {
    const bool previousActive = g_dispatcherScope.active;
    if (previousActive && g_hostEmissionTick.active) RefuseHostEmission("nested native dispatcher");
    const uintptr_t previousController = g_dispatcherScope.controller;
    const uintptr_t previousRegion = g_dispatcherScope.region;
    const uintptr_t previousCaller = g_dispatcherScope.caller;
    const std::uint64_t previousSequence = g_dispatcherScope.sequence;
    std::uint64_t result = 0;
    g_dispatcherScope.active = true;
    g_dispatcherScope.controller = reinterpret_cast<uintptr_t>(controller);
    g_dispatcherScope.region = reinterpret_cast<uintptr_t>(region);
    g_dispatcherScope.caller = caller;
    g_dispatcherScope.sequence = ++g_dispatcherSequences;
    __try {
        result = g_originalDispatcher(controller, region);
    } __finally {
        if (AbnormalTermination()) ++g_traceUnavailable;
        g_dispatcherScope.active = previousActive;
        g_dispatcherScope.controller = previousController;
        g_dispatcherScope.region = previousRegion;
        g_dispatcherScope.caller = previousCaller;
        g_dispatcherScope.sequence = previousSequence;
    }
    return result;
}

std::uint64_t __fastcall HookedDispatcher(void* controller, void* region) {
    return RunDispatcherScope(controller, region, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

std::uint64_t RunScriptScope(const void* args, uintptr_t caller) {
    const bool previousActive = g_scriptScope.active;
    const uintptr_t previousCaller = g_scriptScope.caller;
    const std::uint64_t previousSequence = g_scriptScope.sequence;
    std::uint64_t result = 0;
    g_scriptScope.active = true;
    g_scriptScope.caller = caller;
    g_scriptScope.sequence = ++g_scriptSequences;
    __try {
        result = g_originalScript(args);
    } __finally {
        if (AbnormalTermination()) ++g_traceUnavailable;
        g_scriptScope.active = previousActive;
        g_scriptScope.caller = previousCaller;
        g_scriptScope.sequence = previousSequence;
    }
    return result;
}

std::uint64_t __fastcall HookedScript(const void* args) {
    return RunScriptScope(args, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

// Expands at each existing call site; never moves/consolidates the native call
// into a trampoline wrapper. Only scalar/POD accesses surround the original.
#define OBSERVE_ORIGINAL_UPDATE(sourceValue, pointValue, nativeCall) do { \
    const bool phaseEnabled = g_originalPhaseFrame.available; \
    const OriginalPhaseFrame previousPhase = g_originalPhaseFrame; \
    auto* const phaseEvent = phaseEnabled && g_geometryScope && \
        g_geometryScope->geometry.originalPhase.available && \
        g_geometryScope->geometry.originalPhase.invocation == previousPhase.invocation && \
        g_geometryScope->controller == reinterpret_cast<uintptr_t>(controller) \
        ? &g_geometryScope->geometry.originalPhase : nullptr; \
    if (phaseEnabled) { \
        g_originalPhaseFrame.phase = UpdatePhase::InsideOriginalCall; \
        g_originalPhaseFrame.source = sourceValue; \
        g_originalPhaseFrame.point = reinterpret_cast<uintptr_t>(pointValue); \
        if (phaseEvent) { \
            if (phaseEvent->originalEntries == UINT32_MAX) phaseEvent->overflow = true; \
            else ++phaseEvent->originalEntries; \
            phaseEvent->source = sourceValue; \
            phaseEvent->originalPoint = reinterpret_cast<uintptr_t>(pointValue); \
        } \
    } \
    bool phaseReturned = false; \
    __try { \
        nativeCall; \
        phaseReturned = true; \
    } __finally { \
        if (phaseEnabled) { \
            if (phaseEvent) { \
                phaseEvent->mismatch = phaseEvent->mismatch || \
                    g_originalPhaseFrame.invocation != previousPhase.invocation || \
                    g_originalPhaseFrame.updateSequence != previousPhase.updateSequence || \
                    g_originalPhaseFrame.controller != previousPhase.controller || \
                    g_originalPhaseFrame.phase != UpdatePhase::InsideOriginalCall || \
                    g_originalPhaseFrame.source != sourceValue; \
                if (phaseReturned) { \
                    if (phaseEvent->originalReturns == UINT32_MAX) phaseEvent->overflow = true; \
                    else ++phaseEvent->originalReturns; \
                } else if (AbnormalTermination()) { \
                    phaseEvent->unwound = true; \
                    if (phaseEvent->originalUnwinds == UINT32_MAX) phaseEvent->overflow = true; \
                    else ++phaseEvent->originalUnwinds; \
                } \
            } \
            g_originalPhaseFrame = previousPhase; \
        } \
    } \
} while (false)

void RunUpdate(void* controller, const float* nativePoint, uintptr_t returnAddress) {
    const auto role = g_role();
    if (g_traceTick.active && g_traceTick.stampAvailable && !g_traceNestedDepth && IsDiagnosticGameThread())
        ObserveGeometryLifecycle(g_traceTick.stamp, GetTickCount64(), role);
    if (role != 1 && role != 2) {
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::RoleOffPassThrough, nativePoint, g_original(controller, nativePoint));
        return;
    }
    if (returnAddress != g_exeBase + RETURN_RVA) {
        Qualification q;
        q.controller = reinterpret_cast<uintptr_t>(controller);
        q.reason = "unverified caller";
        ++g_unavailable;
        Report(3, "unavailable", q);
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::UnverifiedCallerPassThrough, nativePoint, g_original(controller, nativePoint));
        return;
    }
    auto q = Qualify(reinterpret_cast<uintptr_t>(controller));
    if (q.scope != Scope::Qualified) {
        if (q.scope == Scope::Unavailable) { ++g_unavailable; Report(3, "unavailable", q); }
        else { ++g_unsupported; Report(4, "unsupported", q); }
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::UnqualifiedControllerPassThrough, nativePoint, g_original(controller, nativePoint));
        return;
    }
    alignas(16) float point[4] {};
    if (role == 1) {
        if (CopyNative(reinterpret_cast<uintptr_t>(nativePoint), point, sizeof(point)) &&
            FinitePoint(point) && SameStamp(q.stamp) && g_role() == 1) {
            g_capture(point);
            ++g_captures;
            Report(0, "capture", q, point);
        } else {
            q.reason = "host point/lifecycle unavailable";
            ++g_unavailable;
            Report(3, "unavailable", q);
        }
        BeginHostEmission(reinterpret_cast<uintptr_t>(controller), nativePoint);
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::HostPassThrough, nativePoint, g_original(controller, nativePoint));
        FinishHostEmission();
        return;
    }
    if (g_role() != 2) {
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::RoleChangedBeforeSelection, nativePoint, g_original(controller, nativePoint));
        return;
    }
    BeginGeometry(q, role); // All diagnostic native reads precede both lease copies.
    const auto clientReturnMutation = AcquireKnownMutationTicket();
    const bool havePoint = g_copy(point, reinterpret_cast<uintptr_t>(controller), g_traceTick.sequence);
    if (g_role() != 2) {
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::RoleChangedAfterFirstCopy, nativePoint, g_original(controller, nativePoint));
        return;
    }
    if (!havePoint || !FinitePoint(point) || !SameStamp(q.stamp)) {
        // Explicit scoped pause: no flag reset, cooldown, emission or catch-up.
        q.reason = "host lease/lifecycle unavailable; native tick paused";
        ++g_holds;
        Report(1, "hold", q);
        if (g_geometryScope && g_geometryScope->geometry.originalPhase.available)
            g_geometryScope->geometry.originalPhase.hold = OriginalHold::FirstLeaseOrLifecycle;
        return;
    }
    // The role check above can observe a new session generation and invalidate
    // the first copy while still returning Client. Refresh after that check;
    // no role query or telemetry may invalidate/delay this final copied lease.
    if (!g_copy(point, reinterpret_cast<uintptr_t>(controller), g_traceTick.sequence) || !FinitePoint(point) || !SameStamp(q.stamp)) {
        q.reason = "final host lease unavailable; native tick paused";
        ++g_holds;
        Report(1, "hold", q);
        if (g_geometryScope && g_geometryScope->geometry.originalPhase.available)
            g_geometryScope->geometry.originalPhase.hold = OriginalHold::FinalLeaseOrLifecycle;
        return;
    }
    OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::ClientLeaseApply, point, g_original(controller, point));
    if (g_traceInstalled.load() && g_traceTick.active && !g_traceNestedDepth &&
        g_traceTick.controller == reinterpret_cast<uintptr_t>(controller) &&
        g_traceTick.sequence && g_traceTick.sequence != UINT64_MAX && IsDiagnosticGameThread() &&
        KnownMutationTicketCurrent(clientReturnMutation)) {
        RecordClientOriginalReturn(reinterpret_cast<uintptr_t>(controller), g_traceTick.sequence,
            q.stamp, clientReturnMutation);
    }
    if (g_geometryScope) g_geometryScope->geometry.originalReturned = true;
    ++g_applies;
    Report(2, "apply", q, point);
}
#undef OBSERVE_ORIGINAL_UPDATE

void RunTracedUpdate(void* controller, const float* nativePoint, uintptr_t caller) {
    auto& tick = g_traceTick;
    tick.active = true;
    tick.controller = reinterpret_cast<uintptr_t>(controller);
    tick.sequence = ++g_traceTicks;
    tick.count = 0;
    tick.geometrySelected = false;
    tick.stampAvailable = CaptureDiagnosticStamp(g_exeBase, tick.stamp);
    tick.before = CaptureTraceState(tick.controller);
    RunUpdate(controller, nativePoint, caller);
    const auto after = CaptureTraceState(tick.controller);
    Stamp postStamp;
    const bool postAvailable = CaptureDiagnosticStamp(g_exeBase, postStamp);
    if (tick.geometrySelected) {
        auto& event = tick.geometryEvent;
        g_geometryScope = nullptr;
        event.sequence = ++g_traceStarted;
        event.tickAfter = after;
        event.tickComplete = true;
        event.postStamp = postStamp;
        event.postStampAvailable = postAvailable;
        event.lifecycleStable = postAvailable && event.stamp.transition == postStamp.transition &&
            event.stamp.load == postStamp.load && event.stamp.location == postStamp.location;
        Qualification q;
        q.controller = event.controller; q.key = event.tickBefore.key;
        q.header = event.tickBefore.header; q.spawnArray = event.tickBefore.spawnArray; q.stamp = event.stamp;
        GeometryObservation finalDefinition {};
        event.geometry.definitionStable = event.lifecycleStable && ReadGeometryDefinition(q, finalDefinition) &&
            SameGeometryDefinition(event.geometry, finalDefinition);
        bool childrenComplete = true;
        for (std::size_t i = 0; i < event.geometry.calls && i < event.geometry.predicates.size(); ++i) {
            const auto& child = event.geometry.predicates[i];
            childrenComplete = childrenComplete && child.inputAvailable && child.member && child.returned && !child.unwound;
        }
        event.geometry.complete = event.geometry.definitionAvailable && event.geometry.definitionStable &&
            event.tickBefore.controllerAvailable && after.controllerAvailable && event.geometry.originalReturned &&
            !event.geometry.overflow && !event.geometry.nested && childrenComplete &&
            event.geometry.coverageSerial == g_geometryCoverage.load() && g_geometryInstalled.load();
        auto& gate = event.geometry.eventGate;
        gate.complete = gate.available && gate.calls == 1 && gate.returned && !gate.unwound &&
            !gate.nested && !gate.overflow && event.geometry.originalReturned &&
            event.geometry.definitionAvailable && event.geometry.definitionStable &&
            event.tickBefore.controllerAvailable && after.controllerAvailable && !event.geometry.nested &&
            gate.coverageSerial != UINT64_MAX && gate.coverageSerial == g_eventGateCoverage.load() && g_eventGateInstalled.load();
        auto& phase = event.geometry.originalPhase;
        const auto& entry = phase.gateEntry;
        const auto& returned = phase.gateReturn;
        phase.threadPhaseConsistent = phase.available && gate.complete &&
            phase.coverageSerial != UINT64_MAX && phase.coverageSerial == g_originalPhaseCoverage.load() &&
            g_originalPhaseConfigured.load() && !phase.overflow && !phase.mismatch && !phase.unwound &&
            phase.originalEntries == 1 && phase.originalReturns == 1 && phase.originalUnwinds == 0 &&
            phase.source == OriginalCallSource::ClientLeaseApply && phase.hold == OriginalHold::None &&
            phase.updateCallerRvaAvailable && phase.updateCallerRva == RETURN_RVA &&
            entry.available && returned.available &&
            entry.invocation == phase.invocation && returned.invocation == phase.invocation &&
            entry.updateSequence == event.tickSequence && returned.updateSequence == event.tickSequence &&
            entry.controller == event.controller && returned.controller == event.controller &&
            entry.point == phase.originalPoint && returned.point == phase.originalPoint &&
            entry.phase == UpdatePhase::InsideOriginalCall && returned.phase == UpdatePhase::InsideOriginalCall &&
            entry.source == phase.source && returned.source == phase.source &&
            entry.callerRvaAvailable && returned.callerRvaAvailable &&
            entry.callerRva == EVENT_GATE_RETURN_RVA && returned.callerRva == EVENT_GATE_RETURN_RVA;
        event.reason = event.geometry.complete ? "native geometry observation; emission outcome unclassified" : "incomplete geometry observation";
        PublishTrace(event);
        tick.geometrySelected = false;
    }
    for (std::size_t i = 0; i < tick.count; ++i) {
        auto& event = tick.events[i];
        event.tickAfter = after;
        event.tickComplete = true;
        event.postStamp = postStamp;
        event.postStampAvailable = postAvailable;
        event.lifecycleStable = event.stampAvailable && postAvailable &&
            event.stamp.transition == postStamp.transition && event.stamp.load == postStamp.load &&
            event.stamp.location == postStamp.location;
        if (!event.lifecycleStable || !event.tickBefore.controllerAvailable ||
            !after.controllerAvailable || event.tickBefore.header != after.header ||
            event.tickBefore.spawnArray != after.spawnArray || event.tickBefore.key != after.key ||
            event.tickBefore.headerId != after.headerId || event.tickBefore.recordCount != after.recordCount) {
            event.outcome = TraceOutcome::Unavailable;
            event.reason = "enclosing update lifecycle/controller identity unavailable";
        }
        if (event.outcome != TraceOutcome::Observed && event.outcome != TraceOutcome::NullReturn &&
            event.outcome != TraceOutcome::OutOfScope) ++g_traceUnavailable;
        PublishTrace(event);
    }
}

// Unwind cleanup consumes only previously captured POD. Never inspect native
// pointers, query role/lifecycle, or invoke a logging/network callback here.
void FinishTraceScope(bool interrupted, unsigned previousDepth) {
    auto& tick = g_traceTick;
    if (interrupted && g_hostEmissionTick.active) {
        g_hostEmissionTick.active = false;
        RefuseHostEmission("enclosing original update unwound");
    }
    const auto count = tick.count;
    g_geometryScope = nullptr;
    if (interrupted && tick.geometrySelected) {
        auto& event = tick.geometryEvent;
        event.sequence = ++g_traceStarted;
        event.geometry.unwound = true;
        event.geometry.complete = false;
        event.geometry.eventGate.complete = false;
        event.geometry.originalPhase.threadPhaseConsistent = false;
        if (event.geometry.originalPhase.requested) event.geometry.originalPhase.unwound = true;
        event.reason = "native geometry tick interrupted";
        ++g_geometryUnwound;
        PublishTrace(event);
    }
    tick.geometrySelected = false;
    tick.active = false;
    tick.count = 0;
    tick.stampAvailable = false;
    tick.controller = 0;
    g_traceNestedDepth = previousDepth;
    if (!interrupted) return;
    ++g_traceUnavailable; // Also exposes interrupted ticks with no buffered return.
    for (std::size_t i = 0; i < count && i < TRACE_TICK_CAP; ++i) {
        auto& event = tick.events[i];
        event.outcome = TraceOutcome::Unavailable;
        event.reason = "enclosing native update interrupted; poststate unavailable";
        event.tickComplete = false;
        event.postStampAvailable = false;
        event.lifecycleStable = false;
        event.postStamp = {};
        event.tickAfter = {};
        PublishTrace(event); // Nonblocking bounded queue; pressure counts drops.
    }
}

// These SEH boundaries contain only scalar locals, never C++ objects requiring
// unwinding. __finally runs on native SEH as well as ordinary completion; it
// neither catches nor retries the original update. Poststate sampling remains
// exclusively on the normal path in RunTracedUpdate.
void RunTraceScope(void* controller, const float* nativePoint, uintptr_t caller) {
    const unsigned previousDepth = g_traceNestedDepth;
    __try {
        RunTracedUpdate(controller, nativePoint, caller);
    } __finally {
        FinishTraceScope(AbnormalTermination() != FALSE, previousDepth);
    }
}

void RunNestedUpdate(void* controller, const float* nativePoint, uintptr_t caller) {
    if (g_hostEmissionTick.active) RefuseHostEmission("nested native update");
    const unsigned previousDepth = g_traceNestedDepth;
    auto* const previousGeometry = g_geometryScope;
    if (previousGeometry) {
        previousGeometry->geometry.nested = true;
        if (previousGeometry->geometry.eventGate.available) previousGeometry->geometry.eventGate.nested = true;
    }
    g_geometryScope = nullptr;
    ++g_traceNestedDepth;
    ++g_traceUnavailable;
    __try {
        RunUpdate(controller, nativePoint, caller);
    } __finally {
        g_geometryScope = previousGeometry;
        g_traceNestedDepth = previousDepth;
    }
}

// Opt-in owner boundary only. The disabled hook retains its original dispatch
// and adds no out-of-line entry frame. This is not a native-call wrapper.
void RunObservedUpdateEntry(void* controller, const float* nativePoint, uintptr_t caller) {
    const OriginalPhaseFrame previous = g_originalPhaseFrame;
    g_originalPhaseFrame = {};
    auto& frame = g_originalPhaseFrame;
    frame.controller = reinterpret_cast<uintptr_t>(controller);
    frame.caller = caller;
    frame.phase = UpdatePhase::RunUpdateWork;
    frame.overflow = previous.depth >= ORIGINAL_PHASE_DEPTH_CAP || g_originalPhaseSerial == UINT64_MAX;
    frame.depth = previous.depth >= ORIGINAL_PHASE_DEPTH_CAP ? ORIGINAL_PHASE_DEPTH_CAP : previous.depth + 1;
    if (g_originalPhaseSerial != UINT64_MAX) frame.invocation = ++g_originalPhaseSerial;
    frame.available = g_originalPhaseConfigured.load() && !frame.overflow;
    __try {
        if ((g_traceInstalled || g_geometryInstalled) && g_traceTick.active) {
            RunNestedUpdate(controller, nativePoint, caller);
        } else if ((g_traceInstalled || g_geometryInstalled) && caller == g_exeBase + RETURN_RVA) {
            RunTraceScope(controller, nativePoint, caller);
        } else {
            RunUpdate(controller, nativePoint, caller);
        }
    } __finally {
        g_originalPhaseFrame = previous;
    }
}

void __fastcall HookedUpdate(void* controller, const float* nativePoint) {
    if (populationauthority::Requested()) {
        const auto error=GetLastError();bool returned=false;
        const auto token=populationauthority::Enter(populationauthority::Kind::Update,
            reinterpret_cast<uintptr_t>(controller),0,0,reinterpret_cast<uintptr_t>(_ReturnAddress()));
        __try {SetLastError(error);g_original(controller,nativePoint);returned=true;}
        __finally {const auto nativeError=GetLastError();populationauthority::Exit(token,populationauthority::Kind::Update,
            reinterpret_cast<uintptr_t>(controller),0,0,0,returned);SetLastError(nativeError);}
        return;
    }
    const auto caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (g_originalPhaseConfigured.load()) {
        RunObservedUpdateEntry(controller, nativePoint, caller);
        return;
    }
    if ((g_traceInstalled || g_geometryInstalled) && g_traceTick.active) {
        RunNestedUpdate(controller, nativePoint, caller);
    } else if ((g_traceInstalled || g_geometryInstalled) && caller == g_exeBase + RETURN_RVA) {
        RunTraceScope(controller, nativePoint, caller);
    } else {
        RunUpdate(controller, nativePoint, caller);
    }
}

template <std::size_t N, typename Fn>
bool InstallDiagnosticHook(uintptr_t rva, const std::uint8_t (&bytes)[N], void* detour, Fn& original, bool retainOnExposure=false) {
    if (!Matches(g_exeBase + rva, bytes)) {
        if (g_log) g_log("[spawntrace] component-unavailable rva=%llX reason=byte-gate", static_cast<unsigned long long>(rva));
        return false;
    }
    auto* target = reinterpret_cast<void*>(g_exeBase + rva);
    auto status = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
    const bool created = status == MH_OK;
    if (created) {
        status = populationauthority::Requested() && !populationauthority::PrepareTrampoline(rva,reinterpret_cast<void*>(original))
            ? MH_ERROR_UNSUPPORTED_FUNCTION : MH_EnableHook(target);
        if (status != MH_OK) {
            if (retainOnExposure) MH_DisableHook(target);
            else MH_RemoveHook(target);
        }
    }
    if (status != MH_OK) {
        // A reported enable failure is not proof that no fiber entered.
        // Only this raw profile opts in; known-mutation installation is unchanged.
        if (!created || !retainOnExposure) original = nullptr;
        if (g_log) g_log("[spawntrace] component-unavailable rva=%llX status=%d", static_cast<unsigned long long>(rva), status);
        return false;
    }
    return true;
}

bool KnownMutationRequested() {
    char enabled[8] {};
    return GetEnvironmentVariableA("KH2COOP_SURVIVING_PACK_PREPARE", enabled, sizeof(enabled)) == 1 &&
        enabled[0] == '1';
}

template <std::size_t N, typename Fn>
bool InstallKnownMutationHook(uintptr_t rva, const std::uint8_t (&bytes)[N], void* detour, Fn& original) {
    if (!Matches(g_exeBase + rva, bytes)) return false;
    auto* target = reinterpret_cast<void*>(g_exeBase + rva);
    auto status = MH_CreateHook(target, detour, reinterpret_cast<void**>(&original));
    const bool created=status==MH_OK;
    if (created) {
        status = populationauthority::Requested() && !populationauthority::PrepareTrampoline(rva,reinterpret_cast<void*>(original))
            ? MH_ERROR_UNSUPPORTED_FUNCTION : MH_EnableHook(target);
        if (status != MH_OK) {
            if (populationauthority::Requested()) MH_DisableHook(target);
            else MH_RemoveHook(target);
        }
    }
    if (status != MH_OK) {
        if (!created || !populationauthority::Requested()) original = nullptr;
        return false;
    }
    return true;
}

void InstallKnownMutationHooks() {
    if (!g_knownMutationRequested && !populationauthority::Requested()) return;
    if (InstallKnownMutationHook(CONTROLLER_CTOR_RVA, kControllerCtorBytes,
            reinterpret_cast<void*>(&HookedControllerCtor), g_originalControllerCtor))
        g_knownMutationInstalled |= 1;
    if (InstallKnownMutationHook(CONTROLLER_INIT_RVA, kControllerInitBytes,
            reinterpret_cast<void*>(&HookedControllerInit), g_originalControllerInit))
        g_knownMutationInstalled |= 2;
    if (InstallKnownMutationHook(CONTROLLER_TEARDOWN_RVA, kControllerTeardownBytes,
            reinterpret_cast<void*>(&HookedControllerTeardown), g_originalControllerTeardown))
        g_knownMutationInstalled |= 4;
    if (g_knownMutationInstalled == 7) g_knownMutation.CompleteCoverage();
    else g_knownMutation.Poison();
    if (g_log) g_log("[knownmutation] requested=1 installedMask=%u poisoned=%u negative-fence-only=1 creationAuthority=0",
        g_knownMutationInstalled, g_knownMutation.Snapshot().poisoned ? 1U : 0U);
    populationauthority::Coverage(g_knownMutationInstalled);
}

template <typename Fn>
void RemoveKnownMutationHook(std::uint32_t bit, uintptr_t rva, Fn& original) {
    if (!(g_knownMutationInstalled & bit)) return;
    auto* target = reinterpret_cast<void*>(g_exeBase + rva);
    // Retain the trampoline if removal fails; a still-live detour must not see
    // a null original. The permanently poisoned namespace cannot issue tickets.
    if (MH_DisableHook(target) == MH_OK && MH_RemoveHook(target) == MH_OK) {
        g_knownMutationInstalled &= ~bit;
        original = nullptr;
    }
}

void ShutdownKnownMutationHooks() {
    if (!g_knownMutationRequested && !g_knownMutationInstalled) return;
    g_knownMutation.CloseCoverage(); // Before any teardown, even partial setup.
    // Same quiesced-owner requirement as the existing update hook shutdown.
    RemoveKnownMutationHook(1, CONTROLLER_CTOR_RVA, g_originalControllerCtor);
    RemoveKnownMutationHook(2, CONTROLLER_INIT_RVA, g_originalControllerInit);
    RemoveKnownMutationHook(4, CONTROLLER_TEARDOWN_RVA, g_originalControllerTeardown);
}

void InstallEventGateObserver(bool trace) {
    g_enrollmentRequested = EnrollmentRequested();
    g_enrollmentConfigured = g_enrollmentRequested.load() && trace && g_geometryConfig.configured;
    if (!g_enrollmentConfigured.load()) return;
    g_eventGateVerified = Matches(g_exeBase + EVENT_GATE_RVA, kEventGateBytes) &&
        Matches(g_exeBase + EVENT_GATE_CALL_RVA, kEventGateCallBytes);
    if (g_eventGateVerified.load()) g_eventGateInstalled = InstallDiagnosticHook(EVENT_GATE_RVA, kEventGateBytes,
        reinterpret_cast<void*>(&HookedEventGate), g_originalEventGate);
    g_eventGateFailed = !g_eventGateInstalled.load();
    SaturatingIncrement(g_eventGateCoverage);
}

} // namespace

namespace {
// One implementation is exercised over test-owned buffers in the same-TU
// harness. Production always supplies the existing checked native read leaves.
using RecordCatalogRead = bool (*)(uintptr_t, void*, std::size_t);
using RecordCatalogStamp = bool (*)(uintptr_t, TraceStamp&);
constexpr std::uint32_t Issue(RecordCatalogIssue value) { return static_cast<std::uint32_t>(value); }
bool CatalogSameStamp(const TraceStamp& a, const TraceStamp& b) {
    return a.load == b.load && a.transition == b.transition && a.location == b.location;
}
bool CatalogSpan(uintptr_t p, std::size_t n) { return n && ReadableRange(p, n); }
bool CatalogOverlap(uintptr_t a, std::size_t an, uintptr_t b, std::size_t bn) {
    return CatalogSpan(a, an) && CatalogSpan(b, bn) && a < b + bn && b < a + an;
}

void CaptureRecordCatalogImpl(uintptr_t base, const std::array<std::uint8_t, 32>& layout,
    NativeRecordCatalog& out, RecordCatalogRead read, RecordCatalogStamp stamp) {
    out = {};
    out.exeBase = base;
    out.layoutSha256 = layout;
    if (!IsDiagnosticGameThread()) { out.issues = Issue(RecordCatalogIssue::Thread); return; }
    if (!ReadableRange(base, CONTROLLER_COUNT + sizeof(std::int32_t)) ||
        !ReadableRange(base, offsets::NOW + 10)) {
        out.issues = Issue(RecordCatalogIssue::Pointer); return;
    }
    if (std::all_of(layout.begin(), layout.end(), [](auto byte) { return byte == 0; }))
        out.issues |= Issue(RecordCatalogIssue::LayoutIdentity);
    out.beforeAvailable = stamp(base, out.before);
    out.countBeforeAvailable = read(base + CONTROLLER_COUNT, &out.countBefore, sizeof(out.countBefore));
    if (!out.countBeforeAvailable || out.countBefore < 0 || out.countBefore > 64)
        out.issues |= Issue(RecordCatalogIssue::Count);
    else out.entryCount = static_cast<std::uint32_t>(out.countBefore);
    // Keep each declared table slot, including unavailable/alternate entries.
    for (std::uint32_t i = 0; i < out.entryCount; ++i) {
        auto& e = out.entries[i];
        e.content.layoutSha256 = layout;
        const auto* loc = out.before.location.data();
        e.content.location = {loc[0], loc[1], loc[2], Field<std::uint16_t>(loc, 4),
            Field<std::uint16_t>(loc, 6), Field<std::uint16_t>(loc, 8)};
        e.tableBeforeAvailable = read(base + CONTROLLER_TABLE + i * 16, e.tableBefore.data(), 16);
        if (!e.tableBeforeAvailable) { e.issues |= Issue(RecordCatalogIssue::TableRead); continue; }
        const auto flags = Field<std::uint32_t>(e.tableBefore.data(), 4);
        e.content.groupKey = Field<std::uint32_t>(e.tableBefore.data(), 0);
        e.controller = Field<uintptr_t>(e.tableBefore.data(), 8);
        if (flags & 1) e.issues |= Issue(RecordCatalogIssue::Script);
        // Bit 1 is the known ordinary table state flag; retain it, do not use
        // it as content/behavior authority. Other bits have no reviewed layout.
        if (flags & ~3U) e.issues |= Issue(RecordCatalogIssue::UnknownFlags);
        if (flags & 1) continue; // Alternate pointers are not Controllers.
        if (!CatalogSpan(e.controller, 64) || e.controller % 8) {
            e.issues |= Issue(RecordCatalogIssue::Pointer); continue;
        }
        e.controllerBeforeAvailable = read(e.controller, e.controllerBefore.data(), 64);
        if (!e.controllerBeforeAvailable) { e.issues |= Issue(RecordCatalogIssue::ControllerRead); continue; }
        if (Field<std::uint32_t>(e.controllerBefore.data(), 0) != e.content.groupKey)
            e.issues |= Issue(RecordCatalogIssue::Key);
        e.header = Field<uintptr_t>(e.controllerBefore.data(), 8);
        e.spawnArray = Field<uintptr_t>(e.controllerBefore.data(), 0x30);
        e.regionArray = Field<uintptr_t>(e.controllerBefore.data(), 0x38);
        if (!CatalogSpan(e.header, 44)) { e.issues |= Issue(RecordCatalogIssue::Pointer); continue; }
        e.headerBeforeAvailable = read(e.header, e.content.header.data(), 44);
        if (!e.headerBeforeAvailable) { e.issues |= Issue(RecordCatalogIssue::HeaderRead); continue; }
        e.declaredRecords = Field<std::uint16_t>(e.content.header.data(), 4);
        out.declaredLogicalRecords += e.declaredRecords;
        if (e.content.header[0] != 1 && e.content.header[0] != 2)
            e.issues |= Issue(RecordCatalogIssue::UnsupportedType);
        if (e.declaredRecords > RECORD_CAP || out.declaredLogicalRecords > 1024) {
            e.issues |= Issue(RecordCatalogIssue::RecordCap); continue;
        }
        const std::size_t bytes = static_cast<std::size_t>(e.declaredRecords) * 64;
        if (!CatalogSpan(e.header, 44 + bytes) || e.spawnArray != e.header + 44 ||
            e.regionArray != e.spawnArray + bytes || !CatalogSpan(e.regionArray, 1)) {
            e.issues |= Issue(RecordCatalogIssue::Layout); continue;
        }
        e.content.records.resize(e.declaredRecords);
        e.recordsAfter.resize(e.declaredRecords);
        e.recordReadMask.resize(e.declaredRecords);
        out.capturedLogicalRecords += e.declaredRecords;
        for (std::size_t r = 0; r < e.content.records.size(); ++r) {
            if (read(e.spawnArray + r * 64, e.content.records[r].data(), 64)) e.recordReadMask[r] |= 1;
            else e.issues |= Issue(RecordCatalogIssue::RecordRead);
        }
    }
    // Read exactly the captured addresses: never chase changed after pointers.
    for (std::uint32_t i = 0; i < out.entryCount; ++i) {
        auto& e = out.entries[i];
        e.tableAfterAvailable = read(base + CONTROLLER_TABLE + i * 16, e.tableAfter.data(), 16);
        if (!e.tableAfterAvailable) e.issues |= Issue(RecordCatalogIssue::TableRead);
        if (e.tableBeforeAvailable && e.tableAfterAvailable && e.tableBefore != e.tableAfter)
            e.issues |= Issue(RecordCatalogIssue::Changed);
        if (e.controllerBeforeAvailable) {
            e.controllerAfterAvailable = read(e.controller, e.controllerAfter.data(), 64);
            if (!e.controllerAfterAvailable) e.issues |= Issue(RecordCatalogIssue::ControllerRead);
            else if (e.controllerBefore != e.controllerAfter) e.issues |= Issue(RecordCatalogIssue::Changed);
        }
        if (e.headerBeforeAvailable) {
            e.headerAfterAvailable = read(e.header, e.headerAfter.data(), 44);
            if (!e.headerAfterAvailable) e.issues |= Issue(RecordCatalogIssue::HeaderRead);
            else if (e.content.header != e.headerAfter) e.issues |= Issue(RecordCatalogIssue::Changed);
        }
        for (std::size_t r = 0; r < e.content.records.size(); ++r) {
            if (read(e.spawnArray + r * 64, e.recordsAfter[r].data(), 64)) e.recordReadMask[r] |= 2;
            else e.issues |= Issue(RecordCatalogIssue::RecordRead);
            if (e.recordReadMask[r] == 3 && e.content.records[r] != e.recordsAfter[r])
                e.issues |= Issue(RecordCatalogIssue::Changed);
        }
        e.bytesComplete = e.tableBeforeAvailable && e.tableAfterAvailable &&
            e.controllerBeforeAvailable && e.controllerAfterAvailable &&
            e.headerBeforeAvailable && e.headerAfterAvailable &&
            e.content.records.size() == e.declaredRecords &&
            !(e.issues & (Issue(RecordCatalogIssue::Changed) | Issue(RecordCatalogIssue::RecordRead) |
                Issue(RecordCatalogIssue::Layout) | Issue(RecordCatalogIssue::RecordCap)));
    }
    out.countAfterAvailable = read(base + CONTROLLER_COUNT, &out.countAfter, sizeof(out.countAfter));
    out.afterAvailable = stamp(base, out.after);
    out.lifecycleStable = out.beforeAvailable && out.afterAvailable && CatalogSameStamp(out.before, out.after);
    if (!out.lifecycleStable) out.issues |= Issue(RecordCatalogIssue::Lifecycle);
    if (!out.countAfterAvailable || out.countBefore != out.countAfter) out.issues |= Issue(RecordCatalogIssue::Count);
    out.tableInventoryComplete = out.countBeforeAvailable && out.countAfterAvailable &&
        out.countBefore >= 0 && out.countBefore <= 64 && out.countBefore == out.countAfter && out.lifecycleStable;
    out.contentBytesComplete = out.tableInventoryComplete;
    for (std::uint32_t i = 0; i < out.entryCount; ++i) {
        auto& a = out.entries[i];
        out.tableInventoryComplete = out.tableInventoryComplete && a.tableBeforeAvailable &&
            a.tableAfterAvailable && a.tableBefore == a.tableAfter;
        out.contentBytesComplete = out.contentBytesComplete && a.bytesComplete;
        // Reject aliases to table/count/NOW metadata, within a definition, and
        // across definitions, including empty headers. Endpoints alone cannot
        // establish that aliased mutable bytes were sampled coherently.
        const auto dataSize = a.headerBeforeAvailable ? 44 + static_cast<std::size_t>(a.declaredRecords) * 64 : 0;
        const std::array<std::pair<uintptr_t, std::size_t>, 2> aSpans {{{a.controller, 64}, {a.header, dataSize}}};
        for (const auto& s : aSpans) {
            if (CatalogOverlap(s.first, s.second, base + CONTROLLER_TABLE, out.entryCount * 16) ||
                CatalogOverlap(s.first, s.second, base + CONTROLLER_COUNT, 4) ||
                CatalogOverlap(s.first, s.second, base + offsets::NOW, 10)) a.issues |= Issue(RecordCatalogIssue::Alias);
        }
        if (CatalogOverlap(a.controller, 64, a.header, dataSize)) a.issues |= Issue(RecordCatalogIssue::Alias);
        for (std::uint32_t j = 0; j < i; ++j) {
            auto& b = out.entries[j];
            const auto bSize = b.headerBeforeAvailable ? 44 + static_cast<std::size_t>(b.declaredRecords) * 64 : 0;
            const std::array<std::pair<uintptr_t, std::size_t>, 2> bSpans {{{b.controller, 64}, {b.header, bSize}}};
            for (const auto& s : aSpans) for (const auto& t : bSpans)
                if (CatalogOverlap(s.first, s.second, t.first, t.second)) {
                    a.issues |= Issue(RecordCatalogIssue::Alias); b.issues |= Issue(RecordCatalogIssue::Alias);
                }
            if (a.bytesComplete && b.bytesComplete && native_record_detail::descriptorEqual(a.content, b.content)) {
                a.issues |= Issue(RecordCatalogIssue::DuplicateContent);
                b.issues |= Issue(RecordCatalogIssue::DuplicateContent);
            }
        }
    }
    // IDs are raw u16 content, including zero/high-bit values. No remapping or
    // living-actor filter may remove a collision from the sampled catalog.
    for (std::uint32_t i = 0; i < out.entryCount; ++i) for (std::size_t r = 0; r < out.entries[i].content.records.size(); ++r) {
        auto& a = out.entries[i];
        if (!(a.recordReadMask[r] & 1)) continue;
        for (std::uint32_t j = 0; j <= i; ++j) {
            auto& b = out.entries[j];
            const auto end = j == i ? r : b.content.records.size();
            for (std::size_t s = 0; s < end; ++s)
                if ((b.recordReadMask[s] & 1) && Field<std::uint16_t>(a.content.records[r].data(), 0x1E) ==
                    Field<std::uint16_t>(b.content.records[s].data(), 0x1E)) {
                    a.issues |= Issue(RecordCatalogIssue::DuplicateId); b.issues |= Issue(RecordCatalogIssue::DuplicateId);
                }
        }
    }
    out.contentBytesComplete = out.contentBytesComplete && out.tableInventoryComplete;
    for (std::uint32_t i = 0; i < out.entryCount; ++i) {
        auto& e = out.entries[i];
        out.issues |= e.issues;
    }
    out.associationComplete = out.contentBytesComplete && !out.issues;
    for (std::uint32_t i = 0; i < out.entryCount; ++i)
        out.entries[i].associationUnique = out.associationComplete;
    if (out.associationComplete) out.status = NativeRecordContentStatus::Complete;
    else if (out.issues & (Issue(RecordCatalogIssue::Alias) | Issue(RecordCatalogIssue::DuplicateId) |
        Issue(RecordCatalogIssue::DuplicateContent))) out.status = NativeRecordContentStatus::Ambiguous;
    else if (out.issues & (Issue(RecordCatalogIssue::Script) | Issue(RecordCatalogIssue::UnknownFlags) |
        Issue(RecordCatalogIssue::UnsupportedType) | Issue(RecordCatalogIssue::RecordCap))) out.status = NativeRecordContentStatus::Unsupported;
    else if (out.countBeforeAvailable) out.status = NativeRecordContentStatus::Partial;
}

void ResolveRecordMembershipImpl(uintptr_t base, const NativeRecordCatalog& catalog, uintptr_t actor,
    NativeRecordMembership& out, RecordCatalogRead read, RecordCatalogStamp stamp) {
    out = {};
    out.actor = actor;
    if (!IsDiagnosticGameThread()) { out.issues = Issue(RecordCatalogIssue::Thread); return; }
    if (base != catalog.exeBase || !catalog.associationComplete || !catalog.contentBytesComplete ||
        !catalog.tableInventoryComplete || catalog.status != NativeRecordContentStatus::Complete ||
        !catalog.beforeAvailable || !catalog.afterAvailable || catalog.entryCount > 64 ||
        catalog.countBefore != static_cast<std::int32_t>(catalog.entryCount) ||
        !ReadableRange(base, CONTROLLER_COUNT + 4) || !CatalogSpan(actor, 0x9F8) || actor % 8) {
        out.issues = Issue(RecordCatalogIssue::NoAssociation); return;
    }
    const NativeRecordCatalogEntry* selected = nullptr;
    for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
        const auto& e = catalog.entries[i];
        if (CatalogOverlap(actor, 0x9F8, e.controller, 64) ||
            CatalogOverlap(actor, 0x9F8, e.header, 44 + static_cast<std::size_t>(e.declaredRecords) * 64))
            out.issues |= Issue(RecordCatalogIssue::Alias);
    }
    if (CatalogOverlap(actor, 0x9F8, base + CONTROLLER_TABLE, catalog.entryCount * 16) ||
        CatalogOverlap(actor, 0x9F8, base + CONTROLLER_COUNT, 4) ||
        CatalogOverlap(actor, 0x9F8, base + offsets::NOW, 10)) out.issues |= Issue(RecordCatalogIssue::Alias);
    auto readAssociation = [&](unsigned phase) {
        out.countReads[phase] = read(base + CONTROLLER_COUNT, &out.counts[phase], 4);
        auto& table = phase == 0 ? out.tableBefore : out.tableAfter;
        out.tableReads[phase] = true;
        for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
            const bool got = read(base + CONTROLLER_TABLE + i * 16, table[i].data(), 16);
            if (got) out.tableReadMask[i] |= static_cast<std::uint8_t>(1U << phase);
            out.tableReads[phase] = out.tableReads[phase] && got;
            if (!got || table[i] != catalog.entries[i].tableBefore) out.issues |= Issue(RecordCatalogIssue::Changed);
        }
        // Two real pointer reads in each pass; never compare cached locals to
        // themselves or resolve a new target if the second pass changes.
        const bool c = read(actor + 0x9E8, &out.controller[phase], sizeof(uintptr_t));
        const bool r = read(actor + 0x9F0, &out.record[phase], sizeof(uintptr_t));
        out.actorControllerReads[phase] = c;
        out.actorRecordReads[phase] = r;
        out.pointerReads[phase] = c && r;
        if (!c || !r) out.issues |= Issue(RecordCatalogIssue::ActorRead);
    };
    for (unsigned phase = 0; phase < 2; ++phase) {
        if (phase == 0) {
            out.stampReads[phase] = stamp(base, out.stamps[phase]);
            readAssociation(phase);
        }
        if (phase == 0 && out.pointerReads[phase]) {
            unsigned matches = 0;
            for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
                const auto& e = catalog.entries[i];
                if (e.controller != out.controller[0]) continue;
                ++matches; selected = &e; out.tableIndex = i;
            }
            if (matches != 1 || !selected->associationUnique || !selected->bytesComplete ||
                selected->content.records.size() != selected->declaredRecords ||
                selected->declaredRecords > 256 || !selected->declaredRecords ||
                !CatalogSpan(selected->spawnArray, selected->declaredRecords * 64) ||
                selected->spawnArray != selected->header + 44 ||
                selected->regionArray != selected->spawnArray + selected->declaredRecords * 64) {
                selected = nullptr; out.issues |= Issue(RecordCatalogIssue::NoAssociation);
            } else if (out.record[0] < selected->spawnArray ||
                out.record[0] >= selected->regionArray || (out.record[0] - selected->spawnArray) % 64) {
                selected = nullptr; out.issues |= Issue(RecordCatalogIssue::RecordIndex);
            } else out.recordIndex = static_cast<std::uint16_t>((out.record[0] - selected->spawnArray) / 64);
        }
        if (selected) {
            out.controllerReads[phase] = read(selected->controller, out.controllerBytes[phase].data(), 64);
            out.headerReads[phase] = read(selected->header, out.headerBytes[phase].data(), 44);
            out.recordReads[phase] = read(out.record[0], out.recordBytes[phase].data(), 64);
            if (!out.controllerReads[phase] || !out.headerReads[phase] || !out.recordReads[phase])
                out.issues |= Issue(RecordCatalogIssue::RecordRead);
            if ((out.controllerReads[phase] && out.controllerBytes[phase] != selected->controllerBefore) ||
                (out.headerReads[phase] && out.headerBytes[phase] != selected->content.header) ||
                (out.recordReads[phase] && out.recordBytes[phase] != selected->content.records[out.recordIndex]))
                out.issues |= Issue(RecordCatalogIssue::Changed);
            out.definitionRecords[phase].resize(selected->declaredRecords);
            out.definitionRecordReads[phase].resize(selected->declaredRecords);
            for (std::size_t r = 0; r < selected->declaredRecords; ++r) {
                const bool got = read(selected->spawnArray + r * 64, out.definitionRecords[phase][r].data(), 64);
                out.definitionRecordReads[phase][r] = got;
                if (!got) out.issues |= Issue(RecordCatalogIssue::RecordRead);
                else if (out.definitionRecords[phase][r] != selected->content.records[r])
                    out.issues |= Issue(RecordCatalogIssue::Changed);
            }
        }
        if (phase == 1) {
            readAssociation(phase);
            out.stampReads[phase] = stamp(base, out.stamps[phase]);
        }
        if (!out.countReads[phase] || out.counts[phase] != catalog.countBefore) out.issues |= Issue(RecordCatalogIssue::Count);
        if (!out.stampReads[phase] || !CatalogSameStamp(out.stamps[phase], catalog.before)) out.issues |= Issue(RecordCatalogIssue::Lifecycle);
    }
    if (out.controller[0] != out.controller[1] || out.record[0] != out.record[1]) out.issues |= Issue(RecordCatalogIssue::Changed);
    out.contentComparisonAvailable = selected && !out.issues;
    out.status = out.contentComparisonAvailable ? NativeRecordContentStatus::Complete : NativeRecordContentStatus::Partial;
}
constexpr std::uint64_t OccIssue(SelectedOccupancyIssue issue) { return static_cast<std::uint64_t>(issue); }

bool SameOccupancyCatalog(const NativeRecordCatalog& a, const NativeRecordCatalog& b) {
    if (a.status != NativeRecordContentStatus::Complete || !a.associationComplete || !a.contentBytesComplete ||
        !a.tableInventoryComplete || !a.lifecycleStable || a.exeBase != b.exeBase || a.entryCount != b.entryCount ||
        a.countBefore != b.countBefore || a.countAfter != b.countAfter || a.layoutSha256 != b.layoutSha256) return false;
    for (std::uint32_t i = 0; i < b.entryCount; ++i) {
        const auto& x = a.entries[i]; const auto& y = b.entries[i];
        if (x.tableBefore != y.tableBefore || x.controllerBefore != y.controllerBefore ||
            x.content.header != y.content.header || x.content.records != y.content.records) return false;
    }
    return true;
}

void CaptureSelectedOccupancyImpl(uintptr_t base, const NativeRecordCatalog& catalog, std::uint32_t selection,
    NativeSelectedOccupancy& out, RecordCatalogRead read, RecordCatalogStamp stamp) {
    out = {};
    out.selectedDefinition = selection;
    if (!IsDiagnosticGameThread()) { out.issues = OccIssue(SelectedOccupancyIssue::Thread); return; }
    if (catalog.status != NativeRecordContentStatus::Complete || !catalog.associationComplete ||
        !catalog.contentBytesComplete || !catalog.tableInventoryComplete || !catalog.lifecycleStable ||
        catalog.exeBase != base || catalog.entryCount > 64 || catalog.countBefore != static_cast<std::int32_t>(catalog.entryCount) ||
        catalog.countBefore != catalog.countAfter) { out.issues = OccIssue(SelectedOccupancyIssue::Catalog); return; }
    if (selection >= catalog.entryCount || catalog.entries[selection].declaredRecords > 256 ||
        catalog.entries[selection].content.records.size() != catalog.entries[selection].declaredRecords) {
        out.issues = OccIssue(SelectedOccupancyIssue::Selection); return;
    }
    if (!ReadableRange(base, offsets::active_entity_list::HANDLE_REGION_TABLE + 64 * 8)) {
        out.issues = OccIssue(SelectedOccupancyIssue::Pointer); return;
    }
    const auto& selected = catalog.entries[selection];
    std::size_t totalRecords = 0;
    for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
        const auto& e = catalog.entries[i];
        if (!e.bytesComplete || !e.associationUnique || e.declaredRecords > 256 ||
            e.content.records.size() != e.declaredRecords || !CatalogSpan(e.header, 44 + e.declaredRecords * 64) ||
            !CatalogSpan(e.controller, 0x58) || e.spawnArray != e.header + 44 ||
            e.regionArray != e.spawnArray + e.declaredRecords * 64 ||
            (totalRecords += e.declaredRecords) > 1024) {
            out.issues = OccIssue(SelectedOccupancyIssue::Catalog); return;
        }
    }
    out.mutation[0] = AcquireKnownMutationTicket();
    CaptureRecordCatalogImpl(base, catalog.layoutSha256, out.catalogs[0], read, stamp);
    auto readRoots = [&](unsigned phase) {
        constexpr std::array<uintptr_t, 4> rvas {offsets::active_entity_list::HEAD, offsets::active_entity_list::TAIL,
            offsets::active_entity_list::FREE_HEAD, offsets::active_entity_list::FREE_TAIL};
        for (unsigned i = 0; i < 4; ++i) {
            if (read(base + rvas[i], &out.roots[phase][i], 8)) out.rootReadMask[phase] |= static_cast<std::uint8_t>(1U << i);
            else out.issues |= OccIssue(SelectedOccupancyIssue::Roots);
        }
        for (unsigned i = 0; i < 64; ++i) {
            out.bucketReads[phase][i] = read(base + offsets::active_entity_list::HANDLE_REGION_TABLE + i * 8, &out.buckets[phase][i], 8);
            if (!out.bucketReads[phase][i]) out.issues |= OccIssue(SelectedOccupancyIssue::Bucket);
        }
        // Raw diagnostic only; no interpretation as an interval or eligibility.
        out.context716750Reads[phase] = read(base + 0x716750, &out.context716750[phase], 4);
    };
    auto readState = [&](unsigned phase) {
        out.controllerReads[phase] = read(selected.controller, out.controllerBytes[phase].data(), 0x58);
        if (!out.controllerReads[phase]) out.issues |= OccIssue(SelectedOccupancyIssue::Controller);
        else if (std::memcmp(out.controllerBytes[phase].data(), selected.controllerBefore.data(), 64) != 0)
            out.issues |= OccIssue(SelectedOccupancyIssue::Changed);
        auto& cache = out.cache[phase];
        const uintptr_t root = base + CACHE_ROOT;
        cache.pointerRead = read(root + 0x820, &cache.bucket, 8);
        cache.bucketInRange = cache.pointerRead && cache.bucket >= root && cache.bucket < root + 0x820 &&
            (cache.bucket - root) % 0x208 == 0;
        if (cache.bucketInRange) cache.bytesRead = read(cache.bucket, cache.bytes.data(), cache.bytes.size());
        if (cache.bytesRead) {
            cache.room = Field<std::int32_t>(cache.bytes.data(), 0);
            cache.age = Field<std::int32_t>(cache.bytes.data(), 4);
            std::memcpy(cache.ids.data(), cache.bytes.data() + 8, sizeof(cache.ids));
        } else out.issues |= OccIssue(SelectedOccupancyIssue::Cache);
    };
    auto readFields = [&](uintptr_t actor, NativeOccupancyFields& fields) {
        auto field = [&](std::size_t offset, auto& value, unsigned bit) {
            if (read(actor + offset, &value, sizeof(value))) fields.readMask |= static_cast<std::uint16_t>(bit);
            else out.issues |= OccIssue(SelectedOccupancyIssue::NodeRead);
        };
        field(offsets::actor::LINKED_NEXT_HANDLE, fields.nextHandle, 1);
        field(0x120, fields.flags120, 2);
        field(offsets::actor::OBJENTRY_PTR, fields.object, 4);
        field(0x5C0, fields.status, 8);
        field(0x9E8, fields.controller, 16);
        field(0x9F0, fields.record, 32);
        if ((fields.readMask & 32) && fields.record) {
            if (CatalogSpan(fields.record, 0x20) && read(fields.record + 0x1E, &fields.recordId, 2)) fields.readMask |= 64;
            else out.issues |= OccIssue(SelectedOccupancyIssue::RecordRead);
        }
        if ((fields.readMask & 4) && fields.object) {
            auto objectField = [&](std::size_t offset, auto& value, unsigned bit) {
                if (CatalogSpan(fields.object, 0x60) && read(fields.object + offset, &value, sizeof(value)))
                    fields.readMask |= static_cast<std::uint16_t>(bit);
                else out.issues |= OccIssue(SelectedOccupancyIssue::ObjectRead);
            };
            objectField(offsets::objentry::OBJECT_ID, fields.objectId, 128);
            objectField(offsets::objentry::TYPE_FLAGS, fields.objectType, 256);
            objectField(offsets::objentry::NAME, fields.objectName, 512);
        }
    };
    readRoots(0);
    readState(0);
    out.nodes.reserve(NativeSelectedOccupancy::NodeCap);
    for (unsigned list = 0; list < 2; ++list) {
        const unsigned rootBits = 3U << (list * 2);
        if ((out.rootReadMask[0] & rootBits) != rootBits) continue;
        uintptr_t actor = out.roots[0][list * 2], last = 0;
        while (actor) {
            const auto visited = std::find_if(out.nodes.begin(), out.nodes.end(), [&](const auto& n) { return n.actor == actor; });
            if (visited != out.nodes.end()) {
                out.issues |= OccIssue(visited->deferred == (list != 0) ? SelectedOccupancyIssue::Cycle : SelectedOccupancyIssue::CrossList);
                break;
            }
            if (out.nodes.size() == NativeSelectedOccupancy::NodeCap) { out.issues |= OccIssue(SelectedOccupancyIssue::Cap); break; }
            if (!CatalogSpan(actor, 0xA94) || actor % 8) { out.issues |= OccIssue(SelectedOccupancyIssue::Pointer); break; }
            // Reject actor extents overlapping any checked definition/controller,
            // roots/handles/cache metadata, or another actor. Object entries can
            // legitimately be shared; they are read and compared independently.
            bool alias = CatalogOverlap(actor, 0xA94, base + CONTROLLER_TABLE, catalog.entryCount * 16) ||
                CatalogOverlap(actor, 0xA94, base + CONTROLLER_COUNT, 4) ||
                CatalogOverlap(actor, 0xA94, base + offsets::NOW, 10) ||
                CatalogOverlap(actor, 0xA94, base + offsets::active_entity_list::HEAD, 32) ||
                CatalogOverlap(actor, 0xA94, base + HANDLE_REGIONS_RVA, 512) ||
                CatalogOverlap(actor, 0xA94, base + CACHE_ROOT, 0x828);
            for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
                const auto& e = catalog.entries[i];
                alias = alias || CatalogOverlap(actor, 0xA94, e.controller, 0x58) ||
                    CatalogOverlap(actor, 0xA94, e.header, 44 + e.content.records.size() * 64);
            }
            for (const auto& n : out.nodes) alias = alias || CatalogOverlap(actor, 0xA94, n.actor, 0xA94);
            if (alias) out.issues |= OccIssue(SelectedOccupancyIssue::Alias);
            NativeOccupancyNode row;
            row.actor = actor; row.deferred = list != 0;
            readFields(actor, row.fields[0]);
            out.nodes.push_back(row);
            last = actor;
            const auto& fields = out.nodes.back().fields[0];
            if (!(fields.readMask & 1)) { out.issues |= OccIssue(SelectedOccupancyIssue::Next); break; }
            if (!fields.nextHandle) { actor = 0; break; }
            const auto handle = fields.nextHandle & 0x7FFFFFFFU;
            const auto bucket = handle >> offsets::active_entity_list::HANDLE_BUCKET_SHIFT;
            const auto region = out.buckets[0][bucket];
            if (!out.bucketReads[0][bucket] || !region || region == UINTPTR_MAX ||
                (region & offsets::active_entity_list::HANDLE_LOW_MASK)) {
                out.issues |= OccIssue(SelectedOccupancyIssue::Bucket); break;
            }
            actor = region | (handle & offsets::active_entity_list::HANDLE_LOW_MASK);
        }
        out.listTerminated[list] = actor == 0;
        out.tailMatched[list] = out.listTerminated[list] && last == out.roots[0][list * 2 + 1];
        if (!out.tailMatched[list]) out.issues |= OccIssue(SelectedOccupancyIssue::Tail);
    }
    // Re-read every retained node even if traversal or an earlier field failed.
    for (auto& row : out.nodes) {
        readFields(row.actor, row.fields[1]);
        row.stable = row.fields[0] == row.fields[1];
        if (!row.stable) out.issues |= OccIssue(SelectedOccupancyIssue::Changed);
    }
    readState(1);
    readRoots(1);
    CaptureRecordCatalogImpl(base, catalog.layoutSha256, out.catalogs[1], read, stamp);
    out.mutation[1] = AcquireKnownMutationTicket();
    out.mutationStable = out.mutation[0].available && out.mutation[1].available &&
        out.mutation[0].revision == out.mutation[1].revision;
    if (!out.mutationStable) out.issues |= OccIssue(SelectedOccupancyIssue::Mutation);
    out.catalogStable = SameOccupancyCatalog(out.catalogs[0], catalog) && SameOccupancyCatalog(out.catalogs[1], catalog);
    if (!out.catalogStable) out.issues |= OccIssue(SelectedOccupancyIssue::CatalogChanged);
    out.lifecycleStable = true;
    for (const auto& c : out.catalogs) {
        out.lifecycleStable = out.lifecycleStable && c.beforeAvailable && c.afterAvailable &&
            CatalogSameStamp(c.before, catalog.before) && CatalogSameStamp(c.after, catalog.before);
    }
    if (!out.lifecycleStable) out.issues |= OccIssue(SelectedOccupancyIssue::Lifecycle);
    if (out.roots[0] != out.roots[1] || out.rootReadMask[0] != out.rootReadMask[1] || out.buckets[0] != out.buckets[1])
        out.issues |= OccIssue(SelectedOccupancyIssue::Changed);
    const bool controllerSame = out.controllerReads[0] && out.controllerReads[1] && out.controllerBytes[0] == out.controllerBytes[1];
    if (!controllerSame) out.issues |= OccIssue(SelectedOccupancyIssue::Controller);
    const bool cacheSame = out.cache[0].bytesRead && out.cache[1].bytesRead && out.cache[0].bucket == out.cache[1].bucket &&
        out.cache[0].bytes == out.cache[1].bytes;
    if (!cacheSame) out.issues |= OccIssue(SelectedOccupancyIssue::Cache);

    for (auto& row : out.nodes) {
        const auto& f = row.fields[0];
        const bool pointerFields = (f.readMask & 63) == 63;
        const bool objectFields = f.object && (f.readMask & 896) == 896;
        bool selectedObject = false;
        for (const auto& record : selected.content.records)
            selectedObject = selectedObject || (objectFields && f.objectId == Field<std::uint32_t>(record.data(), 0));
        row.nativeLookupSkipped = (f.readMask & 2) && (f.flags120 & 0x10080000U) != 0;
        if (pointerFields && f.controller == selected.controller && f.record >= selected.spawnArray &&
            f.record < selected.regionArray && (f.record - selected.spawnArray) % 64 == 0) {
            row.recordIndex = static_cast<std::uint16_t>((f.record - selected.spawnArray) / 64);
            row.exactSelectedReference = (f.readMask & 64) &&
                f.recordId == Field<std::uint16_t>(selected.content.records[row.recordIndex].data(), 0x1E);
        }
        if ((f.readMask & 64) && f.recordId) {
            for (const auto& record : selected.content.records) {
                if (f.recordId == Field<std::uint16_t>(record.data(), 0x1E) && !row.exactSelectedReference)
                    row.selectedIdConflict = true;
            }
        }
        // Explicitly exclude only known unrelated noncombat rows. Unknown or
        // selected-object null provenance remains pending, even when flags make
        // the native filtered occupancy lookup skip the node.
        // Conservative reviewed type envelope (saved 3DF930 cases through 0x18).
        // Out-of-envelope metadata is unknown, not proof of a noncombat actor.
        const bool noncombat = objectFields && f.objectId != 0 && f.objectType <= 0x18 &&
            ((f.objectType != offsets::objentry::TYPE_BOSS &&
            f.objectType != offsets::objentry::TYPE_MOB) || (f.objectName[0] == 'F' && f.objectName[1] == '_'));
        row.excludedKnownNoncombat = row.stable && pointerFields && noncombat && !selectedObject &&
            !f.controller && !f.record && !row.selectedIdConflict;
        if (row.exactSelectedReference) {
            ++out.selectedReferenceCount;
            auto& counts = row.deferred ? out.deferredReferenceCounts : out.activeReferenceCounts;
            ++counts[row.recordIndex];
            row.pending = !f.status || !objectFields ||
                f.objectId != Field<std::uint32_t>(selected.content.records[row.recordIndex].data(), 0) ||
                !CatalogSpan(f.status, 8) || row.nativeLookupSkipped;
        } else if (!row.excludedKnownNoncombat && (!pointerFields || !f.controller || !f.record || !objectFields)) {
            row.pending = selectedObject;
            row.unclassifiable = true;
        } else if (!row.excludedKnownNoncombat) {
            unsigned matches = 0;
            for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
                const auto& e = catalog.entries[i];
                if (e.controller == f.controller && f.record >= e.spawnArray && f.record < e.regionArray &&
                    (f.record - e.spawnArray) % 64 == 0 && (f.readMask & 64) &&
                    f.recordId == Field<std::uint16_t>(e.content.records[(f.record - e.spawnArray) / 64].data(), 0x1E)) ++matches;
            }
            if (matches != 1) row.unclassifiable = true;
        }
        if (!row.stable) row.unclassifiable = true;
        if (row.selectedIdConflict || (row.exactSelectedReference && row.deferred)) ++out.conflictCount;
        if (row.pending) ++out.pendingNodeCount;
        if (row.unclassifiable) ++out.unclassifiableNodeCount;
        if (row.excludedKnownNoncombat) ++out.excludedNoncombatCount;
    }
    for (std::size_t r = 0; r < selected.content.records.size(); ++r) {
        if (out.activeReferenceCounts[r] + out.deferredReferenceCounts[r] > 1) ++out.conflictCount;
    }
    out.listedComplete = out.issues == 0 || out.issues == OccIssue(SelectedOccupancyIssue::Cache);
    out.controllerStateAvailable = controllerSame && out.catalogStable && out.lifecycleStable && out.mutationStable;
    out.cacheAvailable = cacheSame && out.catalogStable && out.lifecycleStable && out.mutationStable;
    if (out.cache[0].bytesRead) {
        for (std::size_t i = 0; i < out.cache[0].ids.size(); ++i) {
            const auto id = out.cache[0].ids[i];
            if (!id) continue; // Native empty cache slot, not cached record-ID zero.
            ++out.cacheEntryCount;
            if (std::find(out.cache[0].ids.begin(), out.cache[0].ids.begin() + i, id) != out.cache[0].ids.begin() + i)
                out.cacheDuplicateIds = true;
            for (std::size_t r = 0; r < selected.content.records.size(); ++r)
                if (id == Field<std::uint16_t>(selected.content.records[r].data(), 0x1E)) ++out.cacheSelectedCounts[r];
        }
    }
    for (unsigned phase = 0; phase < 2; ++phase) {
        auto& state = out.state[phase];
        if (out.controllerReads[phase]) {
            Controller c; std::memcpy(&c, out.controllerBytes[phase].data(), sizeof(c));
            state.header = c.header; state.spawnArray = c.spawnArray; state.key = c.key; state.flags = c.flags;
            state.cooldown = c.cooldown; state.stage = c.stage; state.currentCount = c.currentCount; state.initialCount = c.initialCount;
            const auto& header = out.catalogs[phase].entries[selection];
            if (header.headerBeforeAvailable) {
                state.headerId = Field<std::uint16_t>(header.content.header.data(), 2);
                state.recordCount = Field<std::uint16_t>(header.content.header.data(), 4);
                state.activation = header.content.header[0xE]; state.nativeType = header.content.header[0];
            }
        }
        state.controllerAvailable = out.controllerStateAvailable;
        state.cacheBucket = out.cache[phase].bucket; state.cacheRoom = out.cache[phase].room;
        state.cacheAge = out.cache[phase].age; state.cacheIds = out.cache[phase].ids;
        state.cacheAvailable = out.cacheAvailable;
    }
    out.noSelectedReferencesAtSamples = out.listedComplete && !out.selectedReferenceCount && !out.conflictCount &&
        !out.pendingNodeCount && !out.unclassifiableNodeCount;
}
} // namespace

void CaptureNativeSelectedOccupancy(uintptr_t exeBase, const NativeRecordCatalog& catalog,
    std::uint32_t selectedDefinition, NativeSelectedOccupancy& out) {
    CaptureSelectedOccupancyImpl(exeBase, catalog, selectedDefinition, out, &CopyNative, &CaptureDiagnosticStamp);
}

void CaptureNativeRecordCatalog(uintptr_t exeBase,
    const std::array<std::uint8_t, 32>& layoutSha256, NativeRecordCatalog& out) {
    CaptureRecordCatalogImpl(exeBase, layoutSha256, out, &CopyNative, &CaptureDiagnosticStamp);
}
void ResolveNativeRecordMembership(uintptr_t exeBase, const NativeRecordCatalog& catalog,
    uintptr_t actor, NativeRecordMembership& out) {
    ResolveRecordMembershipImpl(exeBase, catalog, actor, out, &CopyNative, &CaptureDiagnosticStamp);
}

void RegisterDiagnosticGameThread() {
    DWORD expected = 0;
    g_diagnosticGameThread.compare_exchange_strong(expected, GetCurrentThreadId());
}

KnownControllerMutationTicket AcquireKnownMutationTicket() {
    return g_knownMutation.Snapshot();
}

bool KnownMutationTicketCurrent(const KnownControllerMutationTicket& ticket) {
    return g_knownMutation.Current(ticket);
}

bool CopyLastClientOriginalReturn(uintptr_t controller, std::uint64_t& sequence) {
    sequence = 0;
    if (!IsDiagnosticGameThread() || !g_traceInstalled.load() || !controller) return false;
    TraceStamp current;
    if (!ReadStamp(current)) { g_clientOriginalReturn = {}; return false; }
    BindClientOriginalReturnStamp(current);
    const auto& returns = g_clientOriginalReturn;
    if (returns.overflow) return false;
    for (std::size_t i = 0; i < returns.count; ++i) {
        const auto& last = returns.entries[i];
        if (last.controller != controller) continue;
        if (!last.sequence || last.sequence == UINT64_MAX ||
            !KnownMutationTicketCurrent(last.mutation)) return false;
        sequence = last.sequence;
        return true;
    }
    return false;
}

bool CopyHostFirstEmission(const NativeRecordCatalog& catalog, std::uint32_t entryIndex,
                          HostFirstEmission& out) {
    out = {};
    auto fail = [&](HostFirstEmissionStatus status, const char* reason) {
        out = {}; out.status = status; out.reason = reason; return false;
    };
    if (!g_knownMutationRequested) return false;
    if (!g_hostEmissionConfigured.load())
        return fail(HostFirstEmissionStatus::Unavailable, "PREPARE and complete SPAWN_TRACE hooks required");
    if (g_hostEmissionPoison.load() || !IsDiagnosticGameThread() || !g_role || g_role() != 1)
        return fail(HostFirstEmissionStatus::Refused, "recorder ownership/coverage unavailable");
    HostFirstEmission saved;
    KnownControllerMutationTicket ticket;
    if (!HostEmissionLock()) return fail(HostFirstEmissionStatus::Refused, "recorder contention");
    saved = g_hostEmission; ticket = g_hostEmissionTicket;
    ReleaseSRWLockExclusive(&g_hostEmissionLock);
    if (saved.status != HostFirstEmissionStatus::Recorded)
        return fail(saved.status == HostFirstEmissionStatus::Disabled ? HostFirstEmissionStatus::Unavailable : saved.status,
                    saved.controller ? saved.reason : "no covered initialization/first return");
    if (catalog.exeBase != g_exeBase || catalog.status != NativeRecordContentStatus::Complete ||
        !catalog.tableInventoryComplete || !catalog.contentBytesComplete || !catalog.associationComplete ||
        !catalog.lifecycleStable || !catalog.beforeAvailable || !catalog.afterAvailable ||
        catalog.entryCount > catalog.entries.size() || entryIndex >= catalog.entryCount ||
        !HostEmissionSameStamp(catalog.before, saved.stamp) || !HostEmissionSameStamp(catalog.after, saved.stamp) ||
        !SameStamp(saved.stamp) || !KnownMutationTicketCurrent(ticket))
        return fail(HostFirstEmissionStatus::Refused, "fresh catalog/lifecycle/mutation mismatch");
    const auto& e = catalog.entries[entryIndex];
    if (e.issues || !e.bytesComplete || !e.associationUnique || e.controller != saved.controller ||
        e.header != saved.header || e.spawnArray != saved.spawnArray || e.content.groupKey != saved.groupKey ||
        e.content.location != saved.location || e.content.layoutSha256 != catalog.layoutSha256 ||
        std::all_of(catalog.layoutSha256.begin(), catalog.layoutSha256.end(), [](auto byte) { return byte == 0; }) ||
        e.declaredRecords != 5 || e.content.records.size() != 5 || e.recordsAfter.size() != 5 ||
        e.recordReadMask.size() != 5 || e.headerAfter != e.content.header)
        return fail(HostFirstEmissionStatus::Refused, "catalog selected definition mismatch");
    HostFirstEmission fresh;
    if (!ReadHostEmissionDefinition(saved.controller, fresh) || !HostEmissionSameDefinition(saved, fresh) ||
        fresh.rawHeader != e.content.header) {
        RefuseHostEmission("observed live definition changed");
        return fail(HostFirstEmissionStatus::Refused, "live definition changed");
    }
    for (std::size_t i = 0; i < saved.records.size(); ++i)
        if (e.recordReadMask[i] != 3 || e.content.records[i] != saved.records[i] || e.recordsAfter[i] != saved.records[i])
            return fail(HostFirstEmissionStatus::Refused, "fresh ordered records mismatch");
    unsigned candidates = 0;
    for (std::uint32_t i = 0; i < catalog.entryCount; ++i) {
        const auto& c = catalog.entries[i].content;
        if (c.groupKey == saved.groupKey && c.header[0] == 2 &&
            Field<std::uint16_t>(c.header.data(), 2) == 30) ++candidates;
    }
    if (candidates != 1 || !SameStamp(saved.stamp) || !KnownMutationTicketCurrent(ticket) || g_hostEmissionPoison.load())
        return fail(HostFirstEmissionStatus::Refused, "candidate/endpoint changed");
    saved.layoutSha256 = catalog.layoutSha256;
    out = saved;
    return true;
}

bool IsDiagnosticGameThread() {
    const DWORD registered = g_diagnosticGameThread.load();
    return registered != 0 && registered == GetCurrentThreadId();
}

bool CaptureDiagnosticStamp(uintptr_t exeBase, TraceStamp& out) {
    out = {};
    if (!ReadableRange(exeBase, offsets::NOW + out.location.size())) return false;
    if (!IsDiagnosticGameThread()) {
        CopyNative(exeBase + offsets::NOW, out.location.data(), out.location.size());
        return false; // Raw NOW is retained, but native serials are unavailable.
    }
    out.transition = warp::TransitionSerial();
    out.load = warp::LoadSerial();
    std::array<std::uint8_t, 10> check {};
    return CopyNative(exeBase + offsets::NOW, out.location.data(), out.location.size()) &&
        CopyNative(exeBase + offsets::NOW, check.data(), check.size()) && check == out.location &&
        out.transition == warp::TransitionSerial() && out.load == warp::LoadSerial();
}

TraceState CaptureDiagnosticState(uintptr_t exeBase, uintptr_t controller) {
    if (!ReadableRange(exeBase, CACHE_ROOT + 0x1000)) return {};
    return ReadDiagnosticState(exeBase, controller);
}

bool Install(uintptr_t exeBase, LogFn log, RoleFn role, CaptureFn capture, CopyFn copy, bool trace) {
    if (g_installed) return true;
    g_exeBase = exeBase;
    g_log = log;
    g_knownMutationRequested = KnownMutationRequested();
    g_traceRequested = trace;
    if(trace && g_knownMutationRequested){if(g_log)g_log("[legacytrace] unavailable reason=PREPARE-enabled diagnostic-profile requires=PREPARE0");trace=false;}
    if(trace && !g_factoryStorage.Init(reinterpret_cast<const void*>(&Install))){if(g_log)g_log("[legacytrace] unavailable reason=FLS-storage");trace=false;}
    g_rawDiagnosticEnabled=trace;
    g_geometryConfig = ReadGeometryConfig(trace);
    g_geometryConfig.configured=false; // explicitly unsupported diagnostic eligibility
    if(g_log && g_traceRequested)g_log("[legacytrace] profile=FLS-raw-only geometry=unsupported enrollment=unsupported originalPhase=unsupported dispatcher=unsupported script=unsupported tick=unsupported firstEmission=unsupported");
    g_enrollmentRequested = EnrollmentRequested();
    g_enrollmentConfigured = g_enrollmentRequested.load() && trace && g_geometryConfig.configured;
    g_geometryTerminal = !g_geometryConfig.requested ? GeometryTerminal::Disabled :
        g_geometryConfig.configured ? GeometryTerminal::Waiting : GeometryTerminal::InvalidConfig;
    if (!role || !capture || !copy || !Matches(exeBase + UPDATE_RVA, kUpdateBytes) ||
        !Matches(exeBase + CALL_RVA, kCallBytes) || !Matches(exeBase + LOOKUP_RVA, kLookupBytes) ||
        !Matches(exeBase + COMPARE_RVA, kCompareBytes)) {
        if (g_log) g_log("[spawnctl] unavailable: callback or native bytes verification failed");
        if (g_knownMutationRequested) g_knownMutation.Poison();
        return false;
    }
    g_role = role;
    g_capture = capture;
    g_copy = copy;
    g_lookup = reinterpret_cast<LookupFn>(exeBase + LOOKUP_RVA);
    auto* address = reinterpret_cast<void*>(exeBase + UPDATE_RVA);
    auto status = MH_CreateHook(address, reinterpret_cast<void*>(&HookedUpdate),
                                reinterpret_cast<void**>(&g_original));
    if (status == MH_OK) {
        status = populationauthority::Requested() && !populationauthority::PrepareTrampoline(UPDATE_RVA,reinterpret_cast<void*>(g_original))
            ? MH_ERROR_UNSUPPORTED_FUNCTION : MH_EnableHook(address);
        if (status != MH_OK) {
            if (populationauthority::Requested()) MH_DisableHook(address);
            else MH_RemoveHook(address);
        }
    }
    if (status != MH_OK) {
        if (g_log) g_log("[spawnctl] unavailable: hook installation failed status=%d", status);
        if (!populationauthority::Requested()) g_original = nullptr;
        if (g_knownMutationRequested) g_knownMutation.Poison();
        return false;
    }
    g_installed = true;
    populationauthority::Coverage(8);
    InstallKnownMutationHooks(); // Independent of every diagnostic trace flag.
    if (populationauthority::Requested()) {
        g_fixedInstalled=InstallDiagnosticHook(WRAPPER_RVA,kWrapperBytes,reinterpret_cast<void*>(&HookedWrapper),g_originalWrapper,true);
        g_generatedInstalled=InstallDiagnosticHook(GENERATED_RVA,kGeneratedBytes,reinterpret_cast<void*>(&HookedGenerated),g_originalGenerated,true);
        populationauthority::Coverage((g_fixedInstalled?16u:0u)|(g_generatedInstalled?32u:0u));
        constexpr std::uint8_t factoryBytes[]{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30};
        if (InstallDiagnosticHook(0x3DF930,factoryBytes,reinterpret_cast<void*>(&HookedNaturalFactory),g_originalNaturalFactory,true))
            populationauthority::Coverage(2048);
    }
    if (trace) {
        std::uint32_t peOffset = 0, signature = 0, imageSize = 0;
        std::uint16_t magic = 0;
        if (ReadNative(exeBase + 0x3C, peOffset) && peOffset < 0x100000 &&
            ReadNative(exeBase + peOffset, signature) && signature == IMAGE_NT_SIGNATURE &&
            ReadNative(exeBase + peOffset + 24, magic) && magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
            ReadNative(exeBase + peOffset + 0x50, imageSize)) {
            g_imageSize = imageSize;
        }
        // Children are enabled before wrappers can claim complete coverage.
        // Both entire leaf entries and their exact factory call sites are gated.
        const bool admissionVerified = Matches(exeBase + ADMISSION_RVA, kAdmissionBytes) &&
            Matches(exeBase + 0x3DF9FB, kAdmissionCallBytes);
        std::uint32_t type4Branch = 0;
        const bool allocationVerified = Matches(exeBase + ALLOCATION_RVA, kAllocationBytes) &&
            Matches(exeBase + 0x3DFA73, kAllocationCallBytes) &&
            ReadNative(exeBase + 0x3DFE4C, type4Branch) && type4Branch == 0x3DFA73;
        g_factoryVerified = (admissionVerified ? FactoryAdmissionHook : 0U) |
            (allocationVerified ? FactoryAllocationHook : 0U);
        std::uint32_t factoryInstalled = 0;
        if (admissionVerified && InstallDiagnosticHook(ADMISSION_RVA, kAdmissionBytes,
                reinterpret_cast<void*>(&HookedAdmission), g_originalAdmission, true))
            factoryInstalled |= FactoryAdmissionHook;
        if (allocationVerified && InstallDiagnosticHook(ALLOCATION_RVA, kAllocationBytes,
                reinterpret_cast<void*>(&HookedAllocation), g_originalAllocation, true))
            factoryInstalled |= FactoryAllocationHook;
        g_factoryFailed = FactoryAllHooks & ~factoryInstalled;
        g_factoryInstalled = factoryInstalled;
        ++g_factoryCoverageSerial;
        g_fixedInstalled = Matches(exeBase + WRAPPER_CALL_RVA, kWrapperCallBytes) &&
            InstallDiagnosticHook(WRAPPER_RVA, kWrapperBytes, reinterpret_cast<void*>(&HookedWrapper), g_originalWrapper, true);
        g_generatedInstalled = InstallDiagnosticHook(GENERATED_RVA, kGeneratedBytes,
                                                     reinterpret_cast<void*>(&HookedGenerated), g_originalGenerated, true);
        g_dispatcherInstalled=false;g_scriptInstalled=false;
        (void)&HookedDispatcher;(void)&HookedScript; // unsupported, never installed
        g_traceInstalled = g_fixedInstalled || g_generatedInstalled;
        if (g_geometryConfig.configured) {
            g_geometryVerified = Matches(exeBase + BOX_RVA, kBoxBytes) &&
                Matches(exeBase + 0x3FF134, kBoxCallBytes);
            if (g_geometryVerified) g_geometryInstalled = InstallDiagnosticHook(BOX_RVA, kBoxBytes,
                reinterpret_cast<void*>(&HookedBox), g_originalBox);
            ++g_geometryCoverage;
        }
        InstallEventGateObserver(trace);
        if (g_log) g_log("[spawntrace] %s fixed=%u generated=%u dispatcher=%u script42DC10=%u factoryVerifiedMask=%u factoryInstalledMask=%u factoryFailedMask=%u callers=all queueCap=%zu tickCap=%zu diagnostic-only=1",
                         g_fixedInstalled && g_generatedInstalled && g_dispatcherInstalled && g_scriptInstalled &&
                         factoryInstalled == FactoryAllHooks ? "ready" : "unavailable",
                         g_fixedInstalled ? 1U : 0U, g_generatedInstalled ? 1U : 0U,
                         g_dispatcherInstalled ? 1U : 0U, g_scriptInstalled ? 1U : 0U,
                         g_factoryVerified.load(), factoryInstalled, g_factoryFailed.load(), TRACE_QUEUE_CAP, TRACE_TICK_CAP);
    }
    ConfigureOriginalPhase(false);
    ConfigureConstructionLineage(trace);
    // Recording uses the already installed diagnostic return hooks. PREPARE
    // alone never quietly expands the installed trace profile.
    g_hostEmissionConfigured = false; // this profile supplies no policy-qualified first emission
    if (g_knownMutationRequested && g_log)
        g_log("[hostfirstemission] configured=%u requires=PREPARE+SPAWN_TRACE boundedCandidates=1 creationAuthority=0",
            g_hostEmissionConfigured.load() ? 1U : 0U);
    if (g_constructionRequested.load() && g_log) g_log("[spawnconstruction] requested=1 configured=%u coverage=%llu sampled-only=1 parentFiberAncestryUnproven=1 creationAuthority=0",
        g_constructionConfigured.load() ? 1U : 0U, static_cast<unsigned long long>(g_constructionCoverage.load()));
    if (g_originalPhaseRequested.load() && g_log) g_log("[spawnoriginalphase] requested=1 configured=%u coverage=%llu thread-bracket-only=1 fiberContinuityProven=0 originalPhaseEligibility=0",
        g_originalPhaseConfigured.load() ? 1U : 0U, static_cast<unsigned long long>(g_originalPhaseCoverage.load()));
    if (g_geometryConfig.requested && g_log) g_log("[spawngeometry] config configured=%u verified=%u installed=%u load=%u transition=%u tuple=%u,%u,%u,%u,%u,%u maxTicks=64 maxMs=2000 coverage=%llu",
        g_geometryConfig.configured ? 1U : 0U, g_geometryVerified.load() ? 1U : 0U, g_geometryInstalled.load() ? 1U : 0U,
        g_geometryConfig.load, g_geometryConfig.transition, g_geometryConfig.location[0], g_geometryConfig.location[1], g_geometryConfig.location[2],
        Field<std::uint16_t>(g_geometryConfig.location.data(), 4), Field<std::uint16_t>(g_geometryConfig.location.data(), 6), Field<std::uint16_t>(g_geometryConfig.location.data(), 8),
        static_cast<unsigned long long>(g_geometryCoverage.load()));
    if (g_enrollmentRequested.load() && g_log) g_log("[spawnenrollobserve] configured=%u verified=%u installed=%u failed=%u coverage=%llu diagnostic-only=1",
        g_enrollmentConfigured.load() ? 1U : 0U, g_eventGateVerified.load() ? 1U : 0U,
        g_eventGateInstalled.load() ? 1U : 0U, g_eventGateFailed.load() ? 1U : 0U,
        static_cast<unsigned long long>(g_eventGateCoverage.load()));
    if (g_log) g_log("[spawnctl] ready update=3FF000 caller=3A5063 lookup=3E0F10 limits controllers=%zu records=%zu objects=%d policy=qualified-client-hold",
                     CONTROLLER_CAP, RECORD_CAP, OBJECT_CAP);
    return true;
}

bool CopyNativeConstructionLineage(NativeConstructionLineage& out) {
    out = {};
    const auto error=GetLastError();FactoryLocal* local=nullptr;auto* frame=FactoryParent(local);
    const auto* scope=frame && frame->constructionVisible?&frame->construction:nullptr;
    if(!g_constructionConfigured.load() || !scope || !scope->captured || !scope->candidateThreadParent ||
       scope->overflow || scope->threadId!=GetCurrentThreadId() || !IsDiagnosticGameThread() ||
       scope->coverage!=g_constructionCoverage.load()){SetLastError(error);return false;}
    out = *scope;
    SetLastError(error);return true;
}

bool PopNativeConstructionLineage(NativeConstructionLineage& out) {
    out = {};
    AcquireSRWLockExclusive(&g_constructionLock);
    const bool haveEvent = g_constructionCount != 0;
    if (haveEvent) {
        out = g_constructionQueue[g_constructionRead];
        g_constructionRead = (g_constructionRead + 1) % CONSTRUCTION_QUEUE_CAP;
        --g_constructionCount;
    }
    ReleaseSRWLockExclusive(&g_constructionLock);
    return haveEvent;
}

bool PopTraceEvent(TraceEvent& event) {
    AcquireSRWLockExclusive(&g_traceLock);
    const bool haveEvent = g_traceCount != 0;
    if (haveEvent) {
        event = g_traceQueue[g_traceRead];
        g_traceRead = (g_traceRead + 1) % TRACE_QUEUE_CAP;
        --g_traceCount;
    }
    ReleaseSRWLockExclusive(&g_traceLock);
    return haveEvent;
}

TraceStats GetTraceStats() {
    if (IsDiagnosticGameThread()) GeometryDeadline(GetTickCount64());
    TraceStats stats;
    stats.started = g_traceStarted.load();
    stats.published = g_tracePublished.load();
    stats.dropped = g_traceDropped.load();
    stats.unsupportedCaller = g_traceUnsupported.load();
    stats.unavailable = g_traceUnavailable.load();
    stats.nativeFaults = g_traceFaults.load();
    stats.lastNativeException = g_traceException.load();
    stats.requested = g_traceRequested;
    stats.fixedAvailable = g_fixedInstalled;
    stats.generatedAvailable = g_generatedInstalled;
    stats.dispatcherAvailable = g_dispatcherInstalled;
    stats.scriptAvailable = g_scriptInstalled;
    stats.constructionRequested = g_constructionRequested.load();
    stats.constructionConfigured = g_constructionConfigured.load();
    stats.constructionCoverage = g_constructionCoverage.load();
    stats.constructionSerial = g_constructionSerial.load();
    stats.constructionPublished = g_constructionPublished.load();
    stats.constructionDropped = g_constructionDropped.load();
    stats.factoryVerifiedMask = g_factoryVerified.load();
    stats.factoryInstalledMask = g_factoryInstalled.load();
    stats.factoryFailedMask = g_factoryFailed.load();
    stats.factoryForeignScopes = g_factoryForeign.load();
    stats.factoryUnwoundScopes = g_factoryUnwound.load();
    stats.factoryAmbiguousScopes = g_factoryAmbiguous.load();
    stats.flsRefused = g_factoryStorage.refused.load();
    stats.flsFirstReason = g_factoryStorage.firstReason.load();
    stats.available = g_fixedInstalled && g_generatedInstalled && g_dispatcherInstalled && g_scriptInstalled &&
        stats.factoryInstalledMask == FactoryAllHooks;
    stats.geometryRequested = g_geometryConfig.requested;
    stats.geometryConfigured = g_geometryConfig.configured;
    stats.geometryLoad = g_geometryConfig.load; stats.geometryTransition = g_geometryConfig.transition;
    stats.geometryLocation = g_geometryConfig.location;
    stats.geometryVerified = g_geometryVerified.load(); stats.geometryInstalled = g_geometryInstalled.load();
    stats.geometryPriorObserved = g_geometryPrior.load(); stats.geometryTerminal = g_geometryTerminal.load();
    stats.geometryTicks = g_geometryTicks.load(); stats.geometryStartedMs = g_geometryStarted.load();
    stats.geometryCoverageSerial = g_geometryCoverage.load(); stats.geometryDropped = g_geometryDropped.load();
    stats.geometryUnwound = g_geometryUnwound.load(); stats.geometryForeign = g_geometryForeign.load();
    stats.enrollmentRequested = g_enrollmentRequested.load(); stats.enrollmentConfigured = g_enrollmentConfigured.load();
    stats.eventGateVerified = g_eventGateVerified.load(); stats.eventGateInstalled = g_eventGateInstalled.load();
    stats.eventGateFailed = g_eventGateFailed.load(); stats.eventGateCoverageSerial = g_eventGateCoverage.load();
    stats.eventGateForeign = g_eventGateForeign.load(); stats.eventGateUnwound = g_eventGateUnwound.load();
    stats.eventGateDropped = g_eventGateDropped.load();
    return stats;
}

void Shutdown() {
    if (populationauthority::Retained()) {
        populationauthority::Stop();
        const uintptr_t targets[]{UPDATE_RVA,WRAPPER_RVA,GENERATED_RVA,CONTROLLER_CTOR_RVA,CONTROLLER_INIT_RVA,CONTROLLER_TEARDOWN_RVA,0x3DF930};
        for (const auto rva:targets) MH_DisableHook(reinterpret_cast<void*>(g_exeBase+rva));
        return; // originals/code/storage stay valid for in-flight native calls
    }
    if(g_factoryStorage.Ready()) {
        g_rawDiagnosticEnabled=false;g_constructionConfigured=false;++g_constructionCoverage;
        ++g_factoryCoverageSerial;
        // Caller still owns quiescence. Disable entry, but do not remove trampolines
        // or clear originals potentially retained by suspended native fibers.
        const uintptr_t targets[]{WRAPPER_RVA,GENERATED_RVA,ADMISSION_RVA,ALLOCATION_RVA,UPDATE_RVA};
        for(const auto rva:targets)MH_DisableHook(reinterpret_cast<void*>(g_exeBase+rva));
        return;
    }
    g_hostEmissionConfigured = false;
    g_hostEmissionPoison = true;
    g_hostEmissionTick = {};
    g_clientOriginalReturn = {};
    g_constructionConfigured = false;
    SaturatingIncrement(g_constructionCoverage);
    ShutdownKnownMutationHooks();
    if (!g_installed) return;
    g_originalPhaseConfigured = false;
    SaturatingIncrement(g_originalPhaseCoverage);
    // As for the other native observers, caller must quiesce the native owner
    // before teardown. Clearing installed is not an in-flight trampoline fence.
    if (g_eventGateInstalled.exchange(false)) {
        SaturatingIncrement(g_eventGateCoverage);
        MH_DisableHook(reinterpret_cast<void*>(g_exeBase + EVENT_GATE_RVA));
        MH_RemoveHook(reinterpret_cast<void*>(g_exeBase + EVENT_GATE_RVA));
        g_originalEventGate = nullptr;
    }
    if (g_geometryInstalled.exchange(false)) {
        ++g_geometryCoverage;
        MH_DisableHook(reinterpret_cast<void*>(g_exeBase + BOX_RVA));
        MH_RemoveHook(reinterpret_cast<void*>(g_exeBase + BOX_RVA));
        g_originalBox = nullptr;
    }
    const auto factoryInstalled = g_factoryInstalled.exchange(0);
    ++g_factoryCoverageSerial;
    if (g_fixedInstalled) {
        auto* wrapper = reinterpret_cast<void*>(g_exeBase + WRAPPER_RVA);
        MH_DisableHook(wrapper);
        MH_RemoveHook(wrapper);
        g_fixedInstalled = false;
        g_originalWrapper = nullptr;
    }
    if (g_generatedInstalled) {
        auto* target = reinterpret_cast<void*>(g_exeBase + GENERATED_RVA);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        g_generatedInstalled = false;
        g_originalGenerated = nullptr;
    }
    if (g_dispatcherInstalled) {
        auto* target = reinterpret_cast<void*>(g_exeBase + DISPATCHER_RVA);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        g_dispatcherInstalled = false;
        g_originalDispatcher = nullptr;
    }
    if (g_scriptInstalled) {
        auto* target = reinterpret_cast<void*>(g_exeBase + SCRIPT_RVA);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        g_scriptInstalled = false;
        g_originalScript = nullptr;
    }
    if (factoryInstalled & FactoryAdmissionHook) {
        auto* target = reinterpret_cast<void*>(g_exeBase + ADMISSION_RVA);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        g_originalAdmission = nullptr;
    }
    if (factoryInstalled & FactoryAllocationHook) {
        auto* target = reinterpret_cast<void*>(g_exeBase + ALLOCATION_RVA);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        g_originalAllocation = nullptr;
    }
    g_traceInstalled = false;
    g_imageSize = 0;
    auto* address = reinterpret_cast<void*>(g_exeBase + UPDATE_RVA);
    MH_DisableHook(address);
    MH_RemoveHook(address);
    g_installed = false;
    g_original = nullptr;
    g_lookup = nullptr;
    g_role = nullptr;
    g_capture = nullptr;
    g_copy = nullptr;
    g_lastReport = {};
    g_captures = g_applies = g_holds = g_unsupported = g_unavailable = 0;
    AcquireSRWLockExclusive(&g_traceLock);
    g_traceRead = g_traceCount = 0;
    ReleaseSRWLockExclusive(&g_traceLock);
    g_traceTick.active = false;
    g_traceTick.count = 0;
    g_traceNestedDepth = 0;
    g_dispatcherScope = {};
    g_scriptScope = {};
    // FLS-owned scopes are permanently retained; no stack-pointer restoration.
    AcquireSRWLockExclusive(&g_constructionLock);
    g_constructionRead = g_constructionCount = 0;
    ReleaseSRWLockExclusive(&g_constructionLock);
    g_eventGateDepth = 0;
    g_originalPhaseFrame = {};
    g_diagnosticGameThread.store(0);
}

} // namespace kh2coop::inject::spawncontroller
