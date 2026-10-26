#include "NativeSpawnController.hpp"
#include "KnownDormantScripts.hpp"
#include "kh2coop/EnemyClass.hpp"
#include "NativeResourceTrace.hpp"
#include "NativePrivateStatus.hpp"
#include "EnemyPopulation.hpp"
#include "PlayerKit.hpp"
#include "PrologueParty.hpp"
#include "kh2coop/PlayerKits.hpp"
#include "NativeTraceFiber.hpp"
#include "kh2coop/RouteSpawnPolicy.hpp"
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
// VUH-2864: the region predicates activationseats::Contains re-evaluates, pinned by vtable slot 1
// and an FNV-1a 64 of the whole body (rel32/RIP-relative only, so relocation leaves it unchanged),
// plus the two constants they read: 1.0f (cylinder bound) and the sign mask (negated extents).
struct RegionPredicatePin {
    uintptr_t vtable, body; std::size_t size; std::uint64_t fnv; activationseats::RegionKind kind;
};
constexpr RegionPredicatePin kRegionPredicates[] = {
    {0x5D4F98, 0x421180, 199, 0xc39fe4c58c9b227fULL, activationseats::RegionKind::Box},
    {0x5D4FB0, 0x421250, 202, 0xe3611fecdc574c0fULL, activationseats::RegionKind::Cylinder},
    {0x5D4FC8, 0x421320, 3, 0xa3703b1a918cfbebULL, activationseats::RegionKind::Infinity},
};
constexpr uintptr_t PREDICATE_ONE_RVA = 0x623A80, PREDICATE_SIGN_RVA = 0x623EB0;
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
    const bool counted = g_knownMutation.Begin();
    HostEmissionMutation(reinterpret_cast<uintptr_t>(controller));
    void* result = nullptr;
    __try { result = g_originalControllerCtor(controller, key, header); }
    __finally { g_knownMutation.End(counted, AbnormalTermination() != FALSE); }
    return result;
}
void __fastcall HookedControllerInit(void* controller) {
    const bool counted = g_knownMutation.Begin();
    HostEmissionMutation(reinterpret_cast<uintptr_t>(controller));
    __try { g_originalControllerInit(controller); }
    __finally { g_knownMutation.End(counted, AbnormalTermination() != FALSE); }
    HostEmissionInitialized(reinterpret_cast<uintptr_t>(controller));
}
void __fastcall HookedControllerTeardown(void* controller) {
    const bool counted = g_knownMutation.Begin();
    HostEmissionMutation(reinterpret_cast<uintptr_t>(controller));
    __try { g_originalControllerTeardown(controller); }
    __finally { g_knownMutation.End(counted, AbnormalTermination() != FALSE); }
}
uintptr_t g_exeBase = 0;
std::uint32_t g_imageSize = 0;
LogFn g_log = nullptr;
RoleFn g_role = nullptr;
CaptureFn g_capture = nullptr;
CopyFn g_copy = nullptr;
SeatsFn g_seats = nullptr;
bool g_seatPredicatesVerified = false, g_seatRequested = true;
std::uint64_t g_seatChoices = 0, g_seatRegionsUnavailable = 0, g_seatHolds = 0;
EmissionFn g_emission = nullptr;
ReplayFn g_replay = nullptr;
struct EmissionUpdate {
    uintptr_t controller = 0;
    std::array<float, 4> point {};
    bool available = false;
};
tracefiber::Store<EmissionUpdate> g_emissionStorage;
std::uint32_t g_emissionUnsupportedLoad = UINT32_MAX;
unsigned g_emissionUnsupportedLogs = 0;
UpdateFn g_original = nullptr;
LookupFn g_lookup = nullptr;
bool g_installed = false;
bool g_moraleGeneratedVerified = false;
std::atomic<bool> g_traceRequested {false}, g_traceInstalled {false};
std::atomic<bool> g_fixedInstalled {false}, g_generatedInstalled {false};
std::atomic<bool> g_dispatcherInstalled {false}, g_scriptInstalled {false};
std::atomic<DWORD> g_diagnosticGameThread {0};
WrapperFn g_originalWrapper = nullptr;
GeneratedFn g_originalGenerated = nullptr;
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
std::array<std::uint64_t, 6> g_lastReport {};

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

template <std::size_t N> bool Matches(uintptr_t address,const std::array<std::uint8_t,N>& expected) {
    std::array<std::uint8_t,N> actual{};
    return CopyNative(address,actual.data(),N) && actual==expected;
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
enum class QualificationUse { CombatActivation, Emission };
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
    bool mixedEmission = false;
    bool generatedEmission = false;
};

Qualification Qualify(uintptr_t address, QualificationUse use = QualificationUse::CombatActivation) {
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
    // Observed HB749/753 helpers are M_EX640_HB/type21, not combat actors.
    // This exception grants emission receipts only, never combat activation.
    const auto& location=result.stamp.location;
    bool observedMixed=use==QualificationUse::Emission && result.key==825778018 &&
        location[0]==4 && location[1]==21 && location[2]==0 &&
        Field<std::uint16_t>(location.data(),4)==1 && Field<std::uint16_t>(location.data(),6)==26 &&
        Field<std::uint16_t>(location.data(),8)==32767;
    constexpr std::uint32_t hb749Ids[]={2406,2406,2407,2407};
    constexpr std::uint32_t hb753Ids[]={2406,2407,2407,2407,2406};
    const std::uint32_t* mixedIds=nullptr;
    if(result.headerId==749 && result.records==4 && Field<std::uint16_t>(header.data(),6)==2)
        mixedIds=hb749Ids;
    else if(result.headerId==753 && result.records==5 && Field<std::uint16_t>(header.data(),6)==1)
        mixedIds=hb753Ids;
    observedMixed=observedMixed && mixedIds;
    if(observedMixed)for(unsigned i=0;i<result.records;++i)
        if(Field<std::uint32_t>(records.data()+i*0x40,0)!=mixedIds[i])observedMixed=false;
    const bool observedGenerated=g_moraleGeneratedVerified && g_generatedInstalled.load() &&
        use==QualificationUse::Emission &&
        routespawn::MoraleGenerated(location,result.key,header,
            std::span<const std::uint8_t>(records.data(),recordBytes));
    ObjectTables tables {};
    if (!ReadObjectTables(tables)) return fail("object tables read/cap");
    struct ObjectIdentity { uintptr_t address = 0; std::array<std::uint8_t, 0x28> bytes {}; };
    std::array<ObjectIdentity, RECORD_CAP> identities {};
    for (std::size_t i = 0; i < result.records; ++i) {
        const auto* record = records.data() + i * 0x40;
        if (record[0x1C] != 2 || (record[0x1D] != 0 && !observedGenerated))
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
        const bool helper=observedMixed && id==2406 && entry[4]==21 &&
            std::memcmp(entry.data()+8,"M_EX640_HB",sizeof("M_EX640_HB"))==0;
        if ((!helper && !enemyclass::Combat(entry[4],id)) ||
            (entry[8] == 'F' && entry[9] == '_')) return fail("noncombat/mixed records", Scope::OutOfScope);
        if(observedMixed && ((id==2406 && !helper) || (id==2407 && entry[4]!=offsets::objentry::TYPE_MOB)))
            return fail("observed mixed class changed",Scope::OutOfScope);
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
        std::array<std::uint8_t, 0x28> identity {};
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
    result.mixedEmission=observedMixed;
    result.generatedEmission=observedGenerated;
    result.reason = observedGenerated ? "observed MU01 generated emission" :
        observedMixed ? "observed HB mixed emission" : "static combat type2";
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
    if (!enemyclass::Combat(event.actorType,event.actorObjectId) ||
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

EmissionBirth CaptureEmissionBirth(void* actor,const Qualification& q,const void* record,
                                    const std::array<std::uint8_t,64>& before) {
    EmissionBirth b;b.actor=reinterpret_cast<uintptr_t>(actor);b.controller=q.controller;b.record=reinterpret_cast<uintptr_t>(record);
    b.header=q.header;b.spawnArray=q.spawnArray;b.key=q.key;b.headerId=q.headerId;b.records=q.records;
    if(q.scope!=Scope::Qualified || b.record<q.spawnArray || (b.record-q.spawnArray)%64 ||
       (b.record-q.spawnArray)/64>=q.records)return b;
    b.recordIndex=static_cast<std::uint16_t>((b.record-q.spawnArray)/64);
    if(!b.actor || !CopyNative(b.record,b.recordBytes.data(),b.recordBytes.size()) || b.recordBytes!=before ||
       !ReadNative(b.actor,b.handle) || !b.handle || !ReadNative(b.actor+offsets::actor::OBJENTRY_PTR,b.objentry) || !b.objentry ||
       !ReadNative(b.objentry,b.objectId))return b;
    std::uint8_t type=0;std::array<char,32> name{};
    if(!ReadNative(b.objentry+4,type) || !CopyNative(b.objentry+8,name.data(),name.size()) ||
       b.objectId!=Field<std::uint32_t>(before.data(),0))return b;
    b.objectType=type;
    if(q.mixedEmission && b.objectId==2406 && type==21 &&
       std::memcmp(name.data(),"M_EX640_HB",sizeof("M_EX640_HB"))==0) {
        b.helper=true;b.available=true;return b;
    }
    uintptr_t controller=0,definition=0;
    if(!enemyclass::Combat(type,b.objectId) ||
       (name[0]=='F' && name[1]=='_') || !ReadNative(b.actor+0x5C0,b.status) ||
       !ReadNative(b.actor+0x9E8,controller) || controller!=b.controller ||
       !ReadNative(b.actor+0x9F0,definition) || definition!=b.record)return b;
    b.available=true;return b;
}

// VUH-2343: read-only receipt for the forced-solo player's actual ingress.
// This observes the original record; it grants no extra actor or party write.
void ObserveBaileyPlayer(const void* record, void* controller, TraceWrapper wrapper, uintptr_t caller) {
    if (!g_log || wrapper != TraceWrapper::Fixed) return;
    std::array<std::uint8_t, 10> now {};
    std::array<std::uint8_t, 64> bytes {};
    constexpr std::array<std::uint8_t, 10> bailey {4,8,0,0,52,0,52,0,0,0};
    if (!CopyNative(g_exeBase + offsets::NOW, now.data(), now.size()) || now != bailey ||
        !CopyNative(reinterpret_cast<uintptr_t>(record), bytes.data(), bytes.size()) ||
        Field<std::uint32_t>(bytes.data(), 0) != 566) return;
    static std::atomic<unsigned> lines {0};
    if (lines.fetch_add(1, std::memory_order_relaxed) >= 8) return;
    Controller native {};
    std::array<std::uint8_t, 0x2C> header {};
    const bool read = ReadNative(reinterpret_cast<uintptr_t>(controller), native) &&
        CopyNative(native.header, header.data(), header.size());
    char recordHex[129] {};
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < bytes.size(); ++i) {
        recordHex[2*i] = hex[bytes[i] >> 4]; recordHex[2*i+1] = hex[bytes[i] & 15];
    }
    g_log("[bailey-player-spawn] observation-only=1 tid=%u ownerThread=%u caller=%llX transition=%u load=%u controller=%llX record=%llX headerRead=%u key=%u headerId=%u type=%u records=%u firstRecord=%u bytes=%s",
        GetCurrentThreadId(), IsDiagnosticGameThread() ? 1u : 0u,
        static_cast<unsigned long long>(caller - g_exeBase), warp::TransitionSerial(), warp::LoadSerial(),
        reinterpret_cast<unsigned long long>(controller), reinterpret_cast<unsigned long long>(record),
        read ? 1u : 0u, native.key, Field<std::uint16_t>(header.data(), 2), header[0],
        Field<std::uint16_t>(header.data(), 4), native.spawnArray == reinterpret_cast<uintptr_t>(record) ? 1u : 0u, recordHex);
}

// VUH-2352: observe only the installed MU01_MS102 late-wave definitions.
// Unsupported map/NPC controllers retain their existing native behavior.
bool ObserveMoraleWave(const void* record, void* controller, const float* point,
                       TraceWrapper wrapper, uintptr_t caller, const Qualification& q,
                       unsigned role, const EmissionUpdate* update, void* actor, bool after) {
    if (!g_log) return false;
    std::array<std::uint8_t,10> now {};
    constexpr std::array<std::uint8_t,10> morale {8,1,0,0,68,0,68,0,0,0};
    Controller native {};
    std::array<std::uint8_t,0x2C> header {};
    std::array<std::uint8_t,64> bytes {};
    if (!CopyNative(g_exeBase+offsets::NOW,now.data(),now.size()) || now!=morale ||
        !ReadNative(reinterpret_cast<uintptr_t>(controller),native) ||
        !CopyNative(native.header,header.data(),header.size()) ||
        !CopyNative(reinterpret_cast<uintptr_t>(record),bytes.data(),bytes.size())) return false;
    const auto id=Field<std::uint16_t>(header.data(),2);
    if (!((native.key==0x70345F62 && (id==978 || id==999)) || // b_4p
          (native.key==0x73345F62 && id==1007)) ||          // b_4s
        bytes[0x1C]!=2 || bytes[0x1D]!=1) return false;
    static std::atomic<unsigned> entries {0};
    // Reserve the return budget for real births; startup resource misses must not exhaust it.
    static std::atomic<unsigned> births {0};
    if (!after && entries.fetch_add(1,std::memory_order_relaxed)>=8) return true;
    if (after && (!actor || births.fetch_add(1,std::memory_order_relaxed)>=64)) return true;
    std::array<float,4> generated {};
    const bool pointRead=wrapper==TraceWrapper::Generated &&
        CopyNative(reinterpret_cast<uintptr_t>(point),generated.data(),sizeof(generated));
    uintptr_t attachedController=0,attachedRecord=0;
    const bool attached=after && actor &&
        ReadNative(reinterpret_cast<uintptr_t>(actor)+0x9E8,attachedController) &&
        ReadNative(reinterpret_cast<uintptr_t>(actor)+0x9F0,attachedRecord);
    const auto address=reinterpret_cast<uintptr_t>(record);
    const bool indexed=address>=native.spawnArray && (address-native.spawnArray)%64==0 &&
        (address-native.spawnArray)/64<Field<std::uint16_t>(header.data(),4);
    g_log("[morale-wave-spawn] observation-only=1 after=%u role=%u ownerThread=%u caller=%llX wrapper=%s transition=%u load=%u key=%u header=%u type=%u records=%u index=%d objectId=%u mode=%u positionMode=%u qualified=%u reason=%s enclosed=%u pointRead=%u point=(%.3f,%.3f,%.3f,%.3f) actor=%llX attached=%u controller=%llX record=%llX",
        after?1u:0u,role,IsDiagnosticGameThread()?1u:0u,static_cast<unsigned long long>(caller-g_exeBase),
        wrapper==TraceWrapper::Generated?"generated":"fixed",warp::TransitionSerial(),warp::LoadSerial(),
        native.key,id,header[0],Field<std::uint16_t>(header.data(),4),
        indexed?static_cast<int>((address-native.spawnArray)/64):-1,Field<std::uint32_t>(bytes.data(),0),
        bytes[0x1C],bytes[0x1D],q.scope==Scope::Qualified?1u:0u,q.reason,
        update && update->available && update->controller==reinterpret_cast<uintptr_t>(controller)?1u:0u,
        pointRead?1u:0u,generated[0],generated[1],generated[2],generated[3],
        reinterpret_cast<unsigned long long>(actor),attached?1u:0u,
        static_cast<unsigned long long>(attachedController),static_cast<unsigned long long>(attachedRecord));
    // VUH-2352: diagnose the exact pin refusal without changing admission or
    // reading more game memory. Entry/return budgets above also bound this log.
    std::span<const std::uint8_t> pinnedHeader, pinnedRecords;
    if (id==978) { pinnedHeader=routespawn::MoraleHeader978; pinnedRecords=routespawn::MoraleRecords978; }
    else if (id==999) { pinnedHeader=routespawn::MoraleHeader999; pinnedRecords=routespawn::MoraleRecords999; }
    else { pinnedHeader=routespawn::MoraleHeader1007; pinnedRecords=routespawn::MoraleRecords1007; }
    unsigned headerDiffs=0, recordDiffs=0;
    std::uint64_t headerDiffMask=0, recordDiffMask=0;
    int firstHeaderDiff=-1, firstRecordDiff=-1;
    unsigned headerExpected=0, headerObserved=0, recordExpected=0, recordObserved=0;
    for (std::size_t offset=0; offset<header.size(); ++offset) {
        if (header[offset]==pinnedHeader[offset]) continue;
        headerDiffMask |= std::uint64_t{1} << offset;
        if (!headerDiffs++) {
            firstHeaderDiff=static_cast<int>(offset);
            headerExpected=pinnedHeader[offset]; headerObserved=header[offset];
        }
    }
    const auto ordinal=indexed ? (address-native.spawnArray)/64 : pinnedRecords.size()/64;
    if (ordinal<pinnedRecords.size()/64) {
        const auto pinnedRecord=pinnedRecords.subspan(ordinal*64,64);
        for (std::size_t offset=0; offset<bytes.size(); ++offset) {
            if (bytes[offset]==pinnedRecord[offset]) continue;
            recordDiffMask |= std::uint64_t{1} << offset;
            if (!recordDiffs++) {
                firstRecordDiff=static_cast<int>(offset);
                recordExpected=pinnedRecord[offset]; recordObserved=bytes[offset];
            }
        }
    } else { recordDiffs=65; firstRecordDiff=-2; }
    g_log("[morale-wave-pin] after=%u role=%u key=%u header=%u index=%d headerDiffs=%u firstHeaderDiff=%d headerExpected=%u headerObserved=%u recordDiffs=%u firstRecordDiff=%d recordExpected=%u recordObserved=%u headerDiffMask=%016llX recordDiffMask=%016llX",
        after?1u:0u,role,native.key,id,indexed?static_cast<int>(ordinal):-1,
        headerDiffs,firstHeaderDiff,headerExpected,headerObserved,
        recordDiffs,firstRecordDiff,recordExpected,recordObserved,
        static_cast<unsigned long long>(headerDiffMask),static_cast<unsigned long long>(recordDiffMask));
    return true;
}

// VUH-2343: exact native load-time ingress only. The original raw566 call is
// delegated once by SpawnMissingParty; no frame-loop creation or party-row write.
// VUH-2464: the same ingress at every checked MissingPartySite (Bailey; the Roxas prologue layout).
bool MissingPartyAtSite(const void* record,void* controller,TraceWrapper wrapper,uintptr_t caller,void*& result) {
    const auto error=GetLastError();
    if(wrapper!=TraceWrapper::Fixed || caller!=g_exeBase+0x3FF727 || !IsDiagnosticGameThread())return false;
    Stamp stamp{};Controller native{};
    std::array<std::uint8_t,44> header{};
    // VUH-2572: up to 32 records (Back Alley evt4 has 18), every one pinned.
    std::array<std::uint8_t,32*64> records{};
    std::array<std::uint8_t,routespawn::BaileyCaller.size()> call{};
    if(!ReadStamp(stamp) || !routespawn::MissingPartySiteAt(stamp.location))return false;
    // VUH-2570: at a checked location, say which shape check refused the group (bounded).
    static std::atomic<unsigned> shapeLogs{0};
    const auto refuse=[&](const char* why) {
        if(g_log && shapeLogs.fetch_add(1)<16)
            g_log("[missing-party] site shape refused reason=%s header=%llX spawn=%llX region=%llX record=%llX",why,
                  static_cast<unsigned long long>(native.header),static_cast<unsigned long long>(native.spawnArray),
                  static_cast<unsigned long long>(native.regionArray),reinterpret_cast<unsigned long long>(record));
        return false;
    };
    if(!ReadNative(reinterpret_cast<uintptr_t>(controller),native))return refuse("controller");
    if(native.spawnArray!=native.header+44)return refuse("spawn-array");
    const auto offset=reinterpret_cast<uintptr_t>(record)-native.spawnArray;
    if(reinterpret_cast<uintptr_t>(record)<native.spawnArray || offset%64)return refuse("record-index");
    if(!CopyNative(native.header,header.data(),header.size()))return refuse("header-read");
    const std::size_t count=Field<std::uint16_t>(header.data(),4);
    if(!count || count>records.size()/64 || offset/64>=count)return refuse("count");
    if(native.regionArray!=native.spawnArray+count*64)return refuse("region-array");
    if(!CopyNative(native.spawnArray,records.data(),count*64))return refuse("records-read");
    const auto groupBytes=std::span<const std::uint8_t>(records.data(),count*64);
    const auto* site=routespawn::MissingPartySiteFor(stamp.location,native.key,caller-g_exeBase,header,groupBytes);
    if(!site)return refuse("bytes");
    if(!routespawn::SitePlayerRecordAt(*site,offset/64))return refuse("record-index");
    if(!CopyNative(g_exeBase+0x3FF71B,call.data(),call.size()) || call!=routespawn::BaileyCaller)return refuse("caller");
    // The clone's direct object: Bailey keeps normal Sora; a prologue site takes the one remote seat's
    // latched, prologue-qualified profile. Without one the authored local load stays solo and admission holds.
    std::uint16_t cloneObject=84;
    const char* profileReason="sora";
    if(site->prologue && !prologueparty::CloneProfile(cloneObject,profileReason)) {
        if(g_log)g_log("[missing-party] site=%s refused profile reason=%s",site->name,profileReason);
        prologueparty::NoteConstruction(site->location,false,profileReason);
        return false;
    }
    // VUH-1512: this seat's own local at a prologue site is its agreed body (ResolveLocal made it member 0),
    // not necessarily the authored Roxas; the binding expects that body's object and status key.
    routespawn::MissingPartySite seatSite=*site;
    if(site->prologue && !prologueparty::LocalBody(seatSite.localObject,seatSite.localKey)) {
        if(g_log)g_log("[missing-party] site=%s refused local body (no agreed profile)",site->name);
        prologueparty::NoteConstruction(site->location,false,"local-body");
        return false;
    }
    const int cloneKey=kh2coop::kitStatusKey(cloneObject);
    const auto* kit=kh2coop::qualifiedKit(cloneObject);
    // The direct factory resolves the same normal player-class body as an authored member.
    // Keep headroom for BOTH native births; each still performs native admission.
    uintptr_t object=0,event=0;std::int32_t cutscene=0;
    std::uint16_t member0=0,statusKey=0;std::uint8_t type=255,cost=0,form=255,drive=255;
    float limit=0,used=0;std::array<char,8> name{};
    const bool ready=kit && cloneKey && privatestatus::Ready() && playerkit::MissingPartyReady() &&
        ReadNative(g_exeBase+0x2A11478,event) && !event && ReadNative(g_exeBase+0xB65210,cutscene) && !cutscene &&
        ReadNative(g_exeBase+playerkit::RVA_RESOLVED_MEMBERS,member0) && member0==seatSite.localObject &&
        ReadNative(g_exeBase+0x9ACDD4,drive) && drive==0 && LookupNative(cloneObject,object) && object &&
        ReadNative(object+4,type) && type==0 && ReadNative(object+0x4C,statusKey) && statusKey==cloneKey &&
        ReadNative(object+0x57,form) && form==kit->form && form==0 &&
        CopyNative(object+8,name.data(),name.size()) && kh2coop::kitNameIs(*kit,name.data(),name.size()) &&
        ReadNative(object+0x54,cost) &&
        ReadNative(g_exeBase+ADMISSION_LIMIT_RVA,limit) && ReadNative(g_exeBase+ADMISSION_USED_RVA,used) &&
        std::isfinite(limit) && std::isfinite(used) && limit-used>=2.0f*cost && SameStamp(stamp);
    if(!ready) {
        if(g_log)g_log("[missing-party] site=%s refused prerequisites event=%llX cutscene=%d member0=%u local=%u clone=%u privateReady=%u",
            site->name,event,cutscene,member0,seatSite.localObject,cloneObject,privatestatus::Ready()?1u:0u);
        if(site->prologue)prologueparty::NoteConstruction(site->location,false,"prerequisites");
        return false;
    }
    Controller finalController{};std::array<std::uint8_t,44> finalHeader{};std::array<std::uint8_t,32*64> finalRecords{};
    if(!ReadNative(reinterpret_cast<uintptr_t>(controller),finalController) || finalController.key!=native.key ||
       finalController.header!=native.header || finalController.spawnArray!=native.spawnArray || finalController.regionArray!=native.regionArray ||
       !CopyNative(native.header,finalHeader.data(),finalHeader.size()) || finalHeader!=header ||
       !CopyNative(native.spawnArray,finalRecords.data(),count*64) || finalRecords!=records || !SameStamp(stamp))return false;
    SetLastError(error);
    bool bound=false;uintptr_t clone=0;
    result=privatestatus::SpawnMissingParty(record,controller,g_originalWrapper,g_log,seatSite,cloneObject,&bound,&clone);
    if(site->prologue) {
        const auto nativeError=GetLastError();
        if(bound)prologueparty::ColorDuplicate(clone,cloneObject,seatSite.localObject);
        prologueparty::NoteConstruction(site->location,bound,bound?"bound":"unbound",clone);
        SetLastError(nativeError);
    }
    return true;
}

// VUH-2464: read-only receipt for every TT (world 2) raw566 player ingress: the
// caller, its call bytes, NOW and the whole spawn group. It grants nothing.
void ObserveProloguePlayer(const void* record, void* controller, TraceWrapper wrapper, uintptr_t caller) {
    if (!g_log || wrapper != TraceWrapper::Fixed) return;
    std::array<std::uint8_t, 10> now {};
    std::array<std::uint8_t, 64> bytes {};
    if (!CopyNative(g_exeBase + offsets::NOW, now.data(), now.size()) || now[0] != 2 ||
        !CopyNative(reinterpret_cast<uintptr_t>(record), bytes.data(), bytes.size()) ||
        Field<std::uint32_t>(bytes.data(), 0) != 566) return;
    static std::atomic<unsigned> lines {0};
    if (lines.fetch_add(1, std::memory_order_relaxed) >= 48) return;
    Controller native {};
    std::array<std::uint8_t, 0x2C> header {};
    std::array<std::uint8_t, 16> call {};
    std::array<std::uint8_t, 32 * 64> group {};
    const bool read = ReadNative(reinterpret_cast<uintptr_t>(controller), native) &&
        CopyNative(native.header, header.data(), header.size());
    const unsigned count = read ? Field<std::uint16_t>(header.data(), 4) : 0;
    const unsigned shown = count < 32 ? count : 32;  // the whole group a site would pin (VUH-2572)
    const bool groupRead = read && shown && CopyNative(native.spawnArray, group.data(), shown * 64u);
    const bool callRead = CopyNative(caller - 0xC, call.data(), call.size());
    constexpr char hex[] = "0123456789ABCDEF";
    const auto toHex = [&](const std::uint8_t* p, std::size_t n, char* out) {
        for (std::size_t i = 0; i < n; ++i) { out[2*i] = hex[p[i] >> 4]; out[2*i+1] = hex[p[i] & 15]; }
        out[2*n] = 0;
    };
    char nowHex[21] {}, headerHex[89] {}, callHex[33] {}, groupHex[32 * 64 * 2 + 1] {};
    toHex(now.data(), now.size(), nowHex);
    toHex(header.data(), read ? header.size() : 0, headerHex);
    toHex(call.data(), callRead ? call.size() : 0, callHex);
    toHex(group.data(), groupRead ? shown * 64u : 0, groupHex);
    uintptr_t canonical = 0; std::uint16_t members[4] {};
    ReadNative(g_exeBase + 0x2A105D0, canonical);
    CopyNative(g_exeBase + 0x2A25300, members, sizeof(members));
    g_log("[prologue-player-spawn] observation-only=1 tid=%u ownerThread=%u caller=%llX callBytes=%s now=%s transition=%u load=%u controller=%llX record=%llX key=%u headerId=%u type=%u records=%u index=%lld members=%X/%X/%X/%X canonical=%llX header=%s group=%s",
        GetCurrentThreadId(), IsDiagnosticGameThread() ? 1u : 0u,
        static_cast<unsigned long long>(caller - g_exeBase), callHex, nowHex, warp::TransitionSerial(), warp::LoadSerial(),
        reinterpret_cast<unsigned long long>(controller), reinterpret_cast<unsigned long long>(record),
        native.key, Field<std::uint16_t>(header.data(), 2), header[0], count,
        read ? static_cast<long long>(reinterpret_cast<uintptr_t>(record) - native.spawnArray) / 64 : -1LL,
        members[0], members[1], members[2], members[3], static_cast<unsigned long long>(canonical), headerHex, groupHex);
}

void* PolicyWrapper(const void* record, void* controller, const float* point, TraceWrapper wrapper, uintptr_t caller) {
    const auto entryError = GetLastError();
    ObserveBaileyPlayer(record, controller, wrapper, caller);
    ObserveProloguePlayer(record, controller, wrapper, caller);
    void* missingResult=nullptr;
    // Preserve the caller's error before either native call or a refused qualification.
    SetLastError(entryError);
    if(MissingPartyAtSite(record,controller,wrapper,caller,missingResult))return missingResult;
    const auto role = g_emission && IsDiagnosticGameThread() && g_role ? g_role() : 0;
    Qualification q;
    bool qualified = false;
    std::uint16_t index = 0;
    if (role == 1 || role == 2) {
        q = Qualify(reinterpret_cast<uintptr_t>(controller),QualificationUse::Emission);
        const auto address = reinterpret_cast<uintptr_t>(record);
        qualified = q.scope == Scope::Qualified && address >= q.spawnArray &&
            address - q.spawnArray < static_cast<uintptr_t>(q.records) * 0x40 &&
            (address - q.spawnArray) % 0x40 == 0;
        if (qualified) index = static_cast<std::uint16_t>((address - q.spawnArray) / 0x40);
    }
    const auto* update = g_emission ? g_emissionStorage.Current() : nullptr;
    const bool observeMorale = ObserveMoraleWave(record,controller,point,wrapper,caller,q,role,update,nullptr,false);
    if (qualified && role == 2 && !update) { SetLastError(entryError); return nullptr; }
    const bool enclosed = qualified && update && update->available && update->controller == reinterpret_cast<uintptr_t>(controller) &&
        (!q.generatedEmission || (wrapper==TraceWrapper::Generated && caller==g_exeBase+0x3FE989));
    if(qualified && q.generatedEmission && role==2 && !enclosed) {SetLastError(entryError);return nullptr;}
    if (qualified && !enclosed && g_log) {
        if (g_emissionUnsupportedLoad != q.stamp.load) { g_emissionUnsupportedLoad = q.stamp.load; g_emissionUnsupportedLogs = 0; }
        if (g_emissionUnsupportedLogs++ < 32)
            g_log("[spawn-emission] unsupported producer=outside-update role=%u key=%u header=%u record=%u caller=%llX",
                role, q.key, q.headerId, index, static_cast<unsigned long long>(caller - g_exeBase));
    }
    EmissionPermit permit{};
    if(enclosed && role==2 && !g_emission(q.key,q.headerId,index,nullptr,EmissionPhase::Reserve,permit,nullptr)) {
        SetLastError(entryError);return nullptr;
    }
    std::array<std::uint8_t,64> before{};
    const bool definitionRead=enclosed && CopyNative(reinterpret_cast<uintptr_t>(record),before.data(),before.size());
    float moraleLimit=0,moraleUsed=0;std::uint8_t moraleWeight=0;
    uintptr_t moraleObject=0;std::uint32_t moraleObjectId=0;
    const bool moraleBudgetRead=qualified && q.generatedEmission && role==2 &&
        ReadNative(reinterpret_cast<uintptr_t>(record),moraleObjectId) && LookupNative(moraleObjectId,moraleObject) && moraleObject &&
        ReadNative(g_exeBase+ADMISSION_LIMIT_RVA,moraleLimit) && ReadNative(g_exeBase+ADMISSION_USED_RVA,moraleUsed) &&
        ReadNative(moraleObject+0x54,moraleWeight);
    void* actor=nullptr;bool committed=false,returned=false;
    __try {
        SetLastError(entryError);
        actor=g_rawDiagnosticEnabled ? ObserveWrapper(record,controller,point,wrapper,caller) :
            wrapper==TraceWrapper::Fixed ? CallOriginalWrapper(record,controller) : CallOriginalGenerated(record,controller,point);
        returned=true;
        const auto nativeError=GetLastError();
        if (observeMorale) ObserveMoraleWave(record,controller,point,wrapper,caller,q,role,update,actor,true);
        static unsigned moraleBudgetLogs=0;
        if(actor && qualified && q.generatedEmission && role==2 && g_log && moraleBudgetLogs++<64) {
            float usedAfter=0;const bool afterRead=ReadNative(g_exeBase+ADMISSION_USED_RVA,usedAfter);
            const bool fits=moraleBudgetRead && std::isfinite(moraleLimit) && std::isfinite(moraleUsed) &&
                moraleUsed>=0 && moraleUsed<=moraleLimit && static_cast<float>(moraleWeight)<=moraleLimit-moraleUsed;
            g_log("[morale-birth-budget] header=%u record=%u netId=%u beforeRead=%u limit=%.2f used=%.2f weight=%u fits=%u afterRead=%u usedAfter=%.2f",
                q.headerId,index,permit.netId,moraleBudgetRead?1U:0U,moraleLimit,moraleUsed,
                static_cast<unsigned>(moraleWeight),fits?1U:0U,afterRead?1U:0U,usedAfter);
        }
        if(enclosed && actor && g_role()==role && g_emissionStorage.Same(update) && SameStamp(q.stamp)) {
            auto birth=definitionRead ? CaptureEmissionBirth(actor,q,record,before) : EmissionBirth{};
            // Even refused capture identifies the successful return and its
            // qualified group. It retires recycled IDs and fences point fallback.
            birth.actor=reinterpret_cast<uintptr_t>(actor);birth.key=q.key;birth.headerId=q.headerId;
            birth.recordIndex=index;birth.records=q.records;
            static unsigned unavailableBirthLogs=0;
            if(!birth.available && g_log && unavailableBirthLogs++<32) {
                uintptr_t linkedController=0,linkedRecord=0;
                ReadNative(reinterpret_cast<uintptr_t>(actor)+0x9E8,linkedController);
                ReadNative(reinterpret_cast<uintptr_t>(actor)+0x9F0,linkedRecord);
                g_log("[spawn-birth] capture-refused header=%u record=%u actor=%llX handle=%u objentry=%llX status=%llX objectId=%u type=%u controller=%llX expectedController=%llX definition=%llX expectedDefinition=%llX",
                    q.headerId,index,reinterpret_cast<unsigned long long>(actor),birth.handle,
                    static_cast<unsigned long long>(birth.objentry),static_cast<unsigned long long>(birth.status),birth.objectId,birth.objectType,
                    static_cast<unsigned long long>(linkedController),static_cast<unsigned long long>(q.controller),
                    static_cast<unsigned long long>(linkedRecord),reinterpret_cast<unsigned long long>(record));
            }
            // A nonnull return spends its exact grant even if metadata refused.
            committed=g_emission(q.key,q.headerId,index,update->point.data(),EmissionPhase::Birth,permit,&birth);
        }
        SetLastError(nativeError);
    } __finally {
        if(enclosed && role==2 && !committed) {
            const auto nativeError=GetLastError();
            // Only an observed normal null return is safe to retry. An unwind
            // or unqualified nonnull outcome may already have allocated a body.
            EmissionBirth abandoned{};
            if(returned && actor) {
                abandoned.actor=reinterpret_cast<uintptr_t>(actor);abandoned.key=q.key;abandoned.headerId=q.headerId;
                abandoned.recordIndex=index;abandoned.records=q.records;
            }
            g_emission(q.key,q.headerId,index,nullptr,
                returned && !actor ? EmissionPhase::Cancel : EmissionPhase::Abandon,permit,abandoned.actor?&abandoned:nullptr);
            SetLastError(nativeError);
        }
    }
    return actor;
}

void* __fastcall HookedWrapper(const void* record, void* controller) {
    return PolicyWrapper(record, controller, nullptr, TraceWrapper::Fixed, reinterpret_cast<uintptr_t>(_ReturnAddress()));
}

void* __fastcall HookedGenerated(const void* record, void* controller, const float* point) {
    return PolicyWrapper(record, controller, point, TraceWrapper::Generated,
                          reinterpret_cast<uintptr_t>(_ReturnAddress()));
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

// Enter only after the caller and full controller qualifier have passed.
// FLS keeps parked native fibers from overwriting another update's receipt.
void CallEmissionUpdate(void* controller, const float* point) {
    auto* scope = g_emission ? g_emissionStorage.Current() : nullptr;
    // Storage refusal must never turn a historical-point client tick into an
    // unguarded native update. A true script producer enters no helper at all.
    if (g_emission && !scope && g_role && g_role() == 2) return;
    EmissionUpdate previous;
    if (scope) {
        previous = *scope;
        *scope = {};
        scope->controller = reinterpret_cast<uintptr_t>(controller);
        scope->available = CopyNative(reinterpret_cast<uintptr_t>(point), scope->point.data(), sizeof(scope->point)) && FinitePoint(scope->point.data());
    }
    __try { g_original(controller, point); }
    __finally { if (scope && g_emissionStorage.Same(scope)) *scope = previous; }
}

std::uint64_t Fnv1a(const std::uint8_t* bytes, std::size_t size) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 0x100000001b3ULL; }
    return hash;
}

bool VerifyRegionPredicates(uintptr_t exeBase) {
    for (const auto& pin : kRegionPredicates) {
        uintptr_t slot = 0;
        std::array<std::uint8_t, 256> body {};
        if (!ReadNative(exeBase + pin.vtable + 8, slot) || slot != exeBase + pin.body ||
            pin.size > body.size() || !CopyNative(exeBase + pin.body, body.data(), pin.size) ||
            Fnv1a(body.data(), pin.size) != pin.fnv) return false;
    }
    std::uint32_t one = 0, sign = 0;
    return ReadNative(exeBase + PREDICATE_ONE_RVA, one) && one == 0x3F800000U &&
           ReadNative(exeBase + PREDICATE_SIGN_RVA, sign) && sign == 0x80000000U;
}

// VUH-2864. One qualified controller's runtime region list (0x3FF000 walks the same nodes):
// head +0x10, next encoded handle +0x58, tail +0x18. Each node must be a pinned predicate
// class whose descriptor lies in the controller's own descriptor array. Anything else is
// unavailable, and the tick keeps the host's point.
using RegionList = std::array<activationseats::Region, activationseats::kMaxRegions>;
bool ReadActivationRegions(uintptr_t address, RegionList& out, std::size_t& count) {
    count = 0;
    Controller native {};
    std::array<std::uint8_t, 0x2C> header {};
    if (!g_seatPredicatesVerified || !ReadNative(address, native) ||
        !CopyNative(native.header, header.data(), header.size())) return false;
    const auto records = Field<std::uint16_t>(header.data(), 4);
    const auto descriptors = static_cast<uintptr_t>(Field<std::uint16_t>(header.data(), 6)) * 0x40;
    if (native.spawnArray != native.header + header.size() ||
        native.regionArray != native.spawnArray + static_cast<uintptr_t>(records) * 0x40) return false;
    std::array<uintptr_t, activationseats::kMaxRegions> seen {};
    uintptr_t node = native.regionHead, last = 0;
    while (node) {
        if (count == out.size()) return false;
        for (std::size_t i = 0; i < count; ++i) if (seen[i] == node) return false;
        std::array<std::uint8_t, 0x70> bytes {};
        if (!CopyNative(node, bytes.data(), bytes.size())) return false;
        const auto vtable = Field<uintptr_t>(bytes.data(), 0);
        const auto* pin = std::find_if(std::begin(kRegionPredicates), std::end(kRegionPredicates),
            [&](const RegionPredicatePin& p) { return g_exeBase + p.vtable == vtable; });
        const auto descriptor = Field<uintptr_t>(bytes.data(), 0x68);
        if (pin == std::end(kRegionPredicates) || descriptor < native.regionArray ||
            descriptor - native.regionArray >= descriptors || (descriptor - native.regionArray) % 0x40) return false;
        auto& region = out[count];
        region.kind = pin->kind;
        std::memcpy(region.inverse.data(), bytes.data() + 8, sizeof(region.inverse));
        std::memcpy(region.extent.data(), bytes.data() + 0x48, sizeof(region.extent));
        if (!std::all_of(region.inverse.begin(), region.inverse.end(), [](float v) { return std::isfinite(v); }) ||
            !std::all_of(region.extent.begin(), region.extent.end(), [](float v) { return std::isfinite(v); }))
            return false;
        seen[count++] = node;
        last = node;
        const auto rawHandle = Field<std::uint32_t>(bytes.data(), 0x58);
        const auto handle = rawHandle & 0x7FFFFFFFU;
        if (!rawHandle) { node = 0; continue; }
        uintptr_t table = 0;
        if (!ReadNative(g_exeBase + HANDLE_REGIONS_RVA + (handle >> 25) * 8, table) ||
            !table || table == UINTPTR_MAX || (table & 0x1FFFFFFU)) return false;
        node = table | (handle & 0x1FFFFFFU);
    }
    return count ? native.regionTail == last : native.regionTail == 0;
}

// Its own line: the [spawnctl] action= format is parsed by rig tools and stays unchanged.
void ReportSeat(const char* side, std::uint8_t slot, const Qualification& q, const float* point) {
    if (!g_log) return;
    const auto now = GetTickCount64();
    if (g_lastReport[5] && now - g_lastReport[5] < 1000) return;
    g_lastReport[5] = now;
    g_log("[spawnctl] seat-activation side=%s slot=%u controller=%llX key=%u headerId=%u room=%02X/%02X choices=%llu regionsUnavailable=%llu holds=%llu point=(%.3f,%.3f,%.3f)",
          side, slot, static_cast<unsigned long long>(q.controller), q.key, q.headerId,
          q.stamp.location[0], q.stamp.location[1], static_cast<unsigned long long>(g_seatChoices),
          static_cast<unsigned long long>(g_seatRegionsUnavailable), static_cast<unsigned long long>(g_seatHolds),
          point[0], point[1], point[2]);
}

// Host only. Writes the chosen point to out4 and returns its seat index (0 = the host's own
// point, also when the regions are unreadable). Clients apply the host's recorded row instead.
std::size_t ChooseActivationPoint(uintptr_t controller, const float* host4,
                                  const activationseats::SeatPoints& seats, float* out4) {
    std::copy_n(host4, 4, out4);
    if (!seats.count) return 0;
    RegionList regions {};
    std::size_t count = 0;
    const bool available = ReadActivationRegions(controller, regions, count);
    if (!available) ++g_seatRegionsUnavailable;
    const activationseats::Point host {host4[0], host4[1], host4[2], host4[3]};
    const auto seat = activationseats::Select(std::span(regions.data(), count), available, host, seats);
    if (seat) {
        std::copy_n(seats.points[seat - 1].data(), 4, out4);
        ++g_seatChoices;
    }
    return seat;
}

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
    if(q.scope!=Scope::Qualified && g_emission)
        q=Qualify(reinterpret_cast<uintptr_t>(controller),QualificationUse::Emission);
    if (q.scope != Scope::Qualified) {
        if (q.scope == Scope::Unavailable) { ++g_unavailable; Report(3, "unavailable", q); }
        else { ++g_unsupported; Report(4, "unsupported", q); }
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::UnqualifiedControllerPassThrough, nativePoint, g_original(controller, nativePoint));
        return;
    }
    alignas(16) float point[4] {};
    if (role == 1) {
        // VUH-2864: the remote seats are tested only when the host's own point is in no region.
        // The host's actual choice, its fallback included, is the row clients apply.
        const bool seatActive = g_seatRequested && g_seatPredicatesVerified && g_seats;
        activationseats::SeatPoints seats {};
        if (!seatActive || !g_seats(seats) || !activationseats::WellFormed(seats)) seats = {};
        alignas(16) float chosen[4] {};
        const float* tick = nativePoint;
        if (CopyNative(reinterpret_cast<uintptr_t>(nativePoint), point, sizeof(point)) &&
            FinitePoint(point) && SameStamp(q.stamp) && g_role() == 1) {
            // Combat activation only: an emission-only controller (the HB749/753 mixed helpers,
            // the LoD generated waves) keeps the native point, so a friend's point never activates it.
            const bool combatActivation = !q.mixedEmission && !q.generatedEmission;
            std::size_t seat = 0;
            if (combatActivation)
                seat = ChooseActivationPoint(reinterpret_cast<uintptr_t>(controller), point, seats, chosen);
            if (seat) tick = chosen;
            if(combatActivation) {
                const activationseats::Choice choice {q.key, q.headerId,
                    static_cast<std::uint8_t>(seat ? seats.slots[seat - 1] : 0),
                    {chosen[0], chosen[1], chosen[2], chosen[3]}};
                g_capture(point, seatActive ? &choice : nullptr, reinterpret_cast<uintptr_t>(controller));
                ++g_captures;Report(0,"capture",q,point);
            } else Report(0,"emission-host",q,point);
            if (seat) ReportSeat("host", seats.slots[seat - 1], q, chosen);
        } else {
            q.reason = "host point/lifecycle unavailable";
            ++g_unavailable;
            Report(3, "unavailable", q);
        }
        BeginHostEmission(reinterpret_cast<uintptr_t>(controller), tick);
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::HostPassThrough, tick, CallEmissionUpdate(controller, tick));
        FinishHostEmission();
        return;
    }
    if (g_role() != 2) {
        OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::RoleChangedBeforeSelection, nativePoint, g_original(controller, nativePoint));
        return;
    }
    if (g_emission && !g_emissionStorage.Current()) {
        q.reason = "emission scope storage unavailable; native tick paused";
        ++g_holds; Report(1, "hold", q); return;
    }
    BeginGeometry(q, role); // All diagnostic native reads precede both lease copies.
    const auto clientReturnMutation = AcquireKnownMutationTicket();
    const bool replay = g_replay && g_replay(q.key, q.headerId, point);
    activationseats::Choices choices {};
    const bool havePoint = replay || (!q.mixedEmission && !q.generatedEmission && g_copy(point, reinterpret_cast<uintptr_t>(controller), g_traceTick.sequence, &choices));
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
    if (!(replay ? g_replay(q.key, q.headerId, point) : g_copy(point, reinterpret_cast<uintptr_t>(controller), g_traceTick.sequence, &choices)) || !FinitePoint(point) || !SameStamp(q.stamp)) {
        q.reason = "final host lease unavailable; native tick paused";
        ++g_holds;
        Report(1, "hold", q);
        if (g_geometryScope && g_geometryScope->geometry.originalPhase.available)
            g_geometryScope->geometry.originalPhase.hold = OriginalHold::FinalLeaseOrLifecycle;
        return;
    }
    // VUH-2864: the host's actual decision for this controller, never a second local choice.
    // A replayed emission already carries the host's chosen trigger point.
    std::uint8_t seat = 0;
    if (!replay) {
        const auto resolved = activationseats::Resolve(choices, q.key, q.headerId);
        if (resolved.kind == activationseats::Resolution::Hold) {
            q.reason = "host seat decision unavailable; native tick paused";
            ++g_holds; ++g_seatHolds;
            Report(1, "hold", q);
            if (g_geometryScope && g_geometryScope->geometry.originalPhase.available)
                g_geometryScope->geometry.originalPhase.hold = OriginalHold::FinalLeaseOrLifecycle;
            return;
        }
        if (resolved.kind == activationseats::Resolution::Row) {
            std::copy(resolved.point.begin(), resolved.point.end(), point);
            seat = resolved.seat;
        }
    }
    OBSERVE_ORIGINAL_UPDATE(OriginalCallSource::ClientLeaseApply, point, CallEmissionUpdate(controller, point));
    if (seat) ReportSeat("client", seat, q, point);
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
        status = MH_EnableHook(target);
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
    if (status == MH_OK) {
        status = MH_EnableHook(target);
        if (status != MH_OK) MH_RemoveHook(target);
    }
    if (status != MH_OK) { original = nullptr; return false; }
    return true;
}

void InstallKnownMutationHooks() {
    if (!g_knownMutationRequested) return;
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

void SetActivationSeats(SeatsFn seats) { g_seats = seats; }

bool EmissionBirthCurrent(const EmissionBirth& birth) noexcept {
    if(!birth.available || !IsDiagnosticGameThread())return false;
    const auto q=Qualify(birth.controller,QualificationUse::Emission);
    if(q.scope!=Scope::Qualified || q.header!=birth.header || q.spawnArray!=birth.spawnArray ||
       q.key!=birth.key || q.headerId!=birth.headerId || q.records!=birth.records ||
       q.spawnArray+static_cast<uintptr_t>(birth.recordIndex)*64!=birth.record)return false;
    return CaptureEmissionBirth(reinterpret_cast<void*>(birth.actor),q,reinterpret_cast<void*>(birth.record),birth.recordBytes)==birth;
}

bool CaptureBaileyCreeperBirth(uintptr_t actor,EmissionBirth& birth,BaileyCreeperDiagnostic* diagnostic) noexcept {
    if(diagnostic)*diagnostic={};
    const auto check=[diagnostic](bool value,const char* reason){if(!value && diagnostic)diagnostic->reason=reason;return value;};
    birth={};uintptr_t controller=0,record=0;
    if(!check(actor!=0,"actor-null") || !check(IsDiagnosticGameThread(),"owner-thread") ||
       !check(ReadNative(actor+0x9E8,controller),"controller-read") || !check(ReadNative(actor+0x9F0,record),"record-read"))return false;
    const auto q=Qualify(controller,QualificationUse::Emission);
    if(!check(q.scope==Scope::Qualified,q.reason) || !check(q.records==10,"record-count") ||
       !check(record>=q.spawnArray,"record-below") || !check(record-q.spawnArray<640,"record-above") ||
       !check((record-q.spawnArray)%64==0,"record-alignment"))return false;
    std::array<std::uint8_t,44> header{};std::array<std::uint8_t,640> records{};
    const auto ordinal=static_cast<unsigned>((record-q.spawnArray)/64);
    if(!check(CopyNative(q.header,header.data(),header.size()),"header-read") ||
       !check(CopyNative(q.spawnArray,records.data(),records.size()),"records-read"))return false;
    if(!routespawn::BaileyCreeperGroup(q.stamp.location,q.key,header,records,ordinal)) {
        if(diagnostic)*diagnostic=DescribeBaileyCreeperGroup(q.stamp.location,q.key,header,records,ordinal);
        return false;
    }
    std::array<std::uint8_t,64> bytes{};std::memcpy(bytes.data(),records.data()+ordinal*64,64);
    birth=CaptureEmissionBirth(reinterpret_cast<void*>(actor),q,reinterpret_cast<void*>(record),bytes);
    Controller after{};std::array<std::uint8_t,44> headerAfter{};std::array<std::uint8_t,640> recordsAfter{};
    if(!check(birth.available,"birth-recapture") || !check(birth.objectId==317,"birth-object") ||
       !check(birth.objectType==offsets::objentry::TYPE_MOB,"birth-type") || !check(ReadNative(controller,after),"controller-bookend-read") ||
       !check(after.key==q.key,"key-bookend") || !check(after.header==q.header,"header-root-bookend") ||
       !check(after.spawnArray==q.spawnArray,"records-root-bookend") || !check(after.regionArray==q.spawnArray+640,"region-root-bookend") ||
       !check(CopyNative(q.header,headerAfter.data(),headerAfter.size()),"header-bookend-read") || !check(headerAfter==header,"header-bookend") ||
       !check(CopyNative(q.spawnArray,recordsAfter.data(),recordsAfter.size()),"records-bookend-read") || !check(recordsAfter==records,"records-bookend") ||
       !check(SameStamp(q.stamp),"stamp-bookend"))return false;
    if(diagnostic)diagnostic->reason="ok";
    return true;
}
bool BaileyCreeperBirthCurrent(const EmissionBirth& birth,BaileyCreeperDiagnostic* diagnostic) noexcept {
    if(diagnostic)*diagnostic={};
    EmissionBirth current{};
    if(!birth.available){if(diagnostic)diagnostic->reason="original-unavailable";return false;}
    if(!CaptureBaileyCreeperBirth(birth.actor,current,diagnostic))return false;
    if(current==birth)return true;
    if(diagnostic) {
        diagnostic->reason="birth-changed";
        diagnostic->changed=(current.actor!=birth.actor?1U:0U)|(current.objentry!=birth.objentry?2U:0U)|
            (current.status!=birth.status?4U:0U)|(current.controller!=birth.controller?8U:0U)|
            (current.record!=birth.record?16U:0U)|(current.header!=birth.header?32U:0U)|
            (current.spawnArray!=birth.spawnArray?64U:0U)|(current.recordBytes!=birth.recordBytes?128U:0U)|
            (current.objectId!=birth.objectId || current.objectType!=birth.objectType || current.handle!=birth.handle ||
             current.key!=birth.key || current.headerId!=birth.headerId || current.recordIndex!=birth.recordIndex ||
             current.records!=birth.records || current.available!=birth.available || current.helper!=birth.helper?256U:0U);
    }
    return false;
}
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
    NativeRecordMembership& out, RecordCatalogRead read, RecordCatalogStamp stamp,
    const NativeOrdinaryResyncView* view = nullptr) {
    out = {};
    out.actor = actor;
    if (!IsDiagnosticGameThread()) { out.issues = Issue(RecordCatalogIssue::Thread); return; }
    const bool association = view ? view->qualified && view->raw == &catalog :
        catalog.associationComplete && catalog.contentBytesComplete && catalog.status == NativeRecordContentStatus::Complete;
    if (base != catalog.exeBase || !association || !catalog.tableInventoryComplete ||
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
                if (view && !view->OrdinaryIndex(i)) continue;
                if (e.controller != out.controller[0]) continue;
                ++matches; selected = &e; out.tableIndex = i;
            }
            if (matches != 1 || (!view && !selected->associationUnique) || !selected->bytesComplete ||
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

// This resync-only adapter never changes the raw collector's classification.
// The default membership path above still requires globally complete ordinary data.
std::array<std::uint16_t, 6> CatalogLocation(const TraceStamp& s) {
    return {s.location[0], s.location[1], s.location[2], Field<std::uint16_t>(s.location.data(),4),
        Field<std::uint16_t>(s.location.data(),6), Field<std::uint16_t>(s.location.data(),8)};
}
bool ResyncCatalogBookend(uintptr_t base, const NativeRecordCatalog& raw,
    RecordCatalogRead read, RecordCatalogStamp stamp) {
    TraceStamp before{}, after{}; std::int32_t count=0;
    if (!stamp(base,before) || !CatalogSameStamp(before,raw.before) ||
        !read(base+CONTROLLER_COUNT,&count,4) || count!=raw.countBefore) return false;
    for (std::uint32_t i=0;i<raw.entryCount;++i) {
        const auto& e=raw.entries[i]; std::array<std::uint8_t,16> table{};
        if (!read(base+CONTROLLER_TABLE+i*16,table.data(),table.size()) || table!=e.tableBefore) return false;
        if (Field<std::uint32_t>(table.data(),4)&1) continue;
        std::array<std::uint8_t,64> controller{}, record{}; std::array<std::uint8_t,44> header{};
        if (!read(e.controller,controller.data(),controller.size()) || controller!=e.controllerBefore ||
            !read(e.header,header.data(),header.size()) || header!=e.content.header) return false;
        for (std::size_t j=0;j<e.content.records.size();++j)
            if (!read(e.spawnArray+j*64,record.data(),record.size()) || record!=e.content.records[j]) return false;
    }
    return stamp(base,after) && CatalogSameStamp(after,raw.before);
}
bool ResyncOrdinaryShape(const NativeRecordCatalog& raw, NativeOrdinaryResyncView& view) {
    const auto loc=CatalogLocation(raw.before);
    const NativeRecordLocation location{loc[0],loc[1],loc[2],loc[3],loc[4],loc[5]};
    std::uint32_t records=0;
    for (std::uint32_t i=0;i<raw.entryCount;++i) {
        const auto& e=raw.entries[i];
        if (!e.tableBeforeAvailable || !e.tableAfterAvailable || e.tableBefore!=e.tableAfter ||
            e.controller!=Field<uintptr_t>(e.tableBefore.data(),8)) return false;
        const auto flags=Field<std::uint32_t>(e.tableBefore.data(),4);
        if (flags&1) {
            if (flags!=1 || e.issues!=Issue(RecordCatalogIssue::Script) ||
                Field<std::uint32_t>(e.tableBefore.data(),0)!=0 || view.scriptRawIndex!=0xFF) return false;
            view.scriptRawIndex=static_cast<std::uint8_t>(i); continue;
        }
        if (flags&~2U || e.issues || !e.bytesComplete || !e.controllerBeforeAvailable || !e.controllerAfterAvailable ||
            !e.headerBeforeAvailable || !e.headerAfterAvailable || e.controllerBefore!=e.controllerAfter ||
            e.content.header!=e.headerAfter || e.content.layoutSha256!=raw.layoutSha256 ||
            e.content.location!=location ||
            e.content.groupKey!=Field<std::uint32_t>(e.tableBefore.data(),0) ||
            e.content.groupKey!=Field<std::uint32_t>(e.controllerBefore.data(),0) ||
            e.declaredRecords>RECORD_CAP || e.content.records.size()!=e.declaredRecords ||
            e.recordsAfter!=e.content.records || e.recordReadMask.size()!=e.declaredRecords ||
            !std::all_of(e.recordReadMask.begin(),e.recordReadMask.end(),[](auto m){return m==3;}) ||
            (e.content.header[0]!=1 && e.content.header[0]!=2) ||
            Field<std::uint16_t>(e.content.header.data(),4)!=e.declaredRecords ||
            !CatalogSpan(e.controller,64) || e.controller%8 ||
            !CatalogSpan(e.header,44+static_cast<std::size_t>(e.declaredRecords)*64) ||
            e.header!=Field<uintptr_t>(e.controllerBefore.data(),8) ||
            e.spawnArray!=Field<uintptr_t>(e.controllerBefore.data(),0x30) ||
            e.regionArray!=Field<uintptr_t>(e.controllerBefore.data(),0x38) ||
            e.spawnArray!=e.header+44 || e.regionArray!=e.spawnArray+e.declaredRecords*64 ||
            !CatalogSpan(e.regionArray,1)) return false;
        records+=e.declaredRecords;
        if (records>1024 || CatalogOverlap(e.controller,64,e.header,44+e.declaredRecords*64)) return false;
        const auto ordinal=view.ordinaryCount++;
        view.ordinaryToRaw[ordinal]=static_cast<std::uint8_t>(i);
        view.rawToOrdinary[i]=static_cast<std::uint8_t>(ordinal);
        // Recompute associations across the ordinary subset. Raw associationUnique
        // remains false for a mixed catalog and is never overwritten or borrowed.
        for (std::uint32_t j=0;j<=i;++j) {
            const auto& other=raw.entries[j];
            if (Field<std::uint32_t>(other.tableBefore.data(),4)&1) continue;
            if (j<i && native_record_detail::descriptorEqual(e.content,other.content)) return false;
            if (j<i) {
                const std::array<std::pair<uintptr_t,std::size_t>,2> a {{{e.controller,64},
                    {e.header,44+static_cast<std::size_t>(e.declaredRecords)*64}}};
                const std::array<std::pair<uintptr_t,std::size_t>,2> b {{{other.controller,64},
                    {other.header,44+static_cast<std::size_t>(other.declaredRecords)*64}}};
                for (const auto& x:a) for (const auto& y:b)
                    if (CatalogOverlap(x.first,x.second,y.first,y.second)) return false;
            }
            for (std::size_t a=0;a<e.content.records.size();++a) {
                const auto end=j==i?a:other.content.records.size();
                for (std::size_t b=0;b<end;++b)
                    if (Field<std::uint16_t>(e.content.records[a].data(),0x1E)==
                        Field<std::uint16_t>(other.content.records[b].data(),0x1E)) return false;
            }
        }
    }
    return records==raw.declaredLogicalRecords && records==raw.capturedLogicalRecords;
}
struct ResyncDormantAsset {
    uintptr_t root=0, scriptRegion=0, anchorRegion=0;
    std::uint32_t scriptRegionIndex=0, anchorRegionIndex=0;
    std::array<std::uint8_t,16> header{}, scriptEntry{}, anchorEntry{};
    std::array<std::uint8_t,knowndormantscripts::ProgramByteCap> program{};
    std::array<std::uint8_t,knowndormantscripts::AnchorPrefixByteCap> anchorPrefix{};
    bool operator==(const ResyncDormantAsset&) const = default;
};
bool ReadResyncDormantAsset(uintptr_t base, const NativeRecordCatalog& raw,
    const knowndormantscripts::Rule& rule, const NativeOrdinaryResyncView& view,
    ResyncDormantAsset& out, RecordCatalogRead read, const ResyncDormantAsset* expected=nullptr) {
    out={};
    if (rule.programBytes.empty() || rule.programBytes.size()>out.program.size() ||
        rule.anchorPrefix.empty() || rule.anchorPrefix.size()>out.anchorPrefix.size() ||
        rule.anchorHeaderOffset!=rule.anchorPrefix.size() || rule.barCount>64 ||
        rule.scriptBarIndex>=rule.barCount || rule.anchorBarIndex>=rule.barCount ||
        !CatalogSpan(base,knowndormantscripts::ArdRootRva+8) ||
        !read(base+knowndormantscripts::ArdRootRva,&out.root,8) ||
        (expected && out.root!=expected->root) || out.root%16 ||
        !CatalogSpan(out.root,16+rule.barCount*16) || !CatalogSpan(out.root,rule.scriptOffset+rule.programBytes.size()) ||
        !CatalogSpan(out.root,rule.anchorOffset+rule.anchorSize) ||
        !read(out.root,out.header.data(),out.header.size()) ||
        (expected && out.header!=expected->header) || Field<std::uint32_t>(out.header.data(),0)!=0x01524142 ||
        Field<std::uint32_t>(out.header.data(),4)!=rule.barCount) return false;
    const auto scriptSlot=out.root+16+rule.scriptBarIndex*16, anchorSlot=out.root+16+rule.anchorBarIndex*16;
    if (!read(scriptSlot,out.scriptEntry.data(),16) || !read(anchorSlot,out.anchorEntry.data(),16) ||
        (expected && (out.scriptEntry!=expected->scriptEntry || out.anchorEntry!=expected->anchorEntry)) ||
        Field<std::uint32_t>(out.scriptEntry.data(),0)!=rule.scriptType ||
        Field<std::uint32_t>(out.scriptEntry.data(),4)!=rule.scriptName ||
        Field<std::uint32_t>(out.scriptEntry.data(),12)!=rule.programBytes.size() ||
        Field<std::uint32_t>(out.anchorEntry.data(),0)!=rule.anchorType ||
        Field<std::uint32_t>(out.anchorEntry.data(),4)!=rule.anchorKey ||
        Field<std::uint32_t>(out.anchorEntry.data(),12)!=rule.anchorSize) return false;
    const auto resolve=[&](const auto& entry, uintptr_t& region, std::uint32_t& index, uintptr_t target) {
        const auto rawHandle=Field<std::uint32_t>(entry.data(),8), handle=rawHandle&0x7FFFFFFFU;
        index=handle>>25;
        if (!rawHandle || !CatalogSpan(base,HANDLE_REGIONS_RVA+64*8) ||
            !read(base+HANDLE_REGIONS_RVA+index*8,&region,8) || !region || region==UINTPTR_MAX ||
            (region&0x1FFFFFFU)) return false;
        return (region|(handle&0x1FFFFFFU))==target;
    };
    const auto script=out.root+rule.scriptOffset, anchor=out.root+rule.anchorOffset;
    if (script!=raw.entries[view.scriptRawIndex].controller ||
        !resolve(out.scriptEntry,out.scriptRegion,out.scriptRegionIndex,script) ||
        !resolve(out.anchorEntry,out.anchorRegion,out.anchorRegionIndex,anchor) ||
        (expected && (out.scriptRegion!=expected->scriptRegion || out.anchorRegion!=expected->anchorRegion))) return false;
    unsigned anchorMatches=0;
    for (std::uint32_t i=0;i<view.ordinaryCount;++i) {
        const auto& e=raw.entries[view.ordinaryToRaw[i]];
        if (e.content.groupKey==rule.anchorKey && e.header==anchor+rule.anchorHeaderOffset) ++anchorMatches;
    }
    if (anchorMatches!=1 || !CatalogSpan(anchor,rule.anchorPrefix.size())) return false;
    // All new witnesses are disjoint from ordinary mutable spans and the native
    // table/count/NOW/flag metadata. Exact duplicate region slots are harmless.
    const std::array<std::pair<uintptr_t,std::size_t>,5> payloads {{{out.root,16+rule.barCount*16},
        {script,rule.programBytes.size()},{anchor,rule.anchorPrefix.size()},
        {base+HANDLE_REGIONS_RVA+out.scriptRegionIndex*8,8},{base+HANDLE_REGIONS_RVA+out.anchorRegionIndex*8,8}}};
    const std::array<std::pair<uintptr_t,std::size_t>,5> metadata {{{base+CONTROLLER_TABLE,raw.entryCount*16},
        {base+CONTROLLER_COUNT,4},{base+offsets::NOW,10},{base+rule.flagRva,1},
        {base+knowndormantscripts::ArdRootRva,8}}};
    for (const auto& a:payloads) {
        for (const auto& b:metadata) if (CatalogOverlap(a.first,a.second,b.first,b.second)) return false;
        for (std::uint32_t i=0;i<view.ordinaryCount;++i) {
            const auto& e=raw.entries[view.ordinaryToRaw[i]];
            if (CatalogOverlap(a.first,a.second,e.controller,64) ||
                CatalogOverlap(a.first,a.second,e.header,44+static_cast<std::size_t>(e.declaredRecords)*64)) return false;
        }
    }
    for (std::size_t i=0;i<payloads.size();++i) for (std::size_t j=0;j<i;++j)
        if (payloads[i]!=payloads[j] && CatalogOverlap(payloads[i].first,payloads[i].second,payloads[j].first,payloads[j].second)) return false;
    for (const auto& a:metadata) for (std::uint32_t i=0;i<view.ordinaryCount;++i) {
        const auto& e=raw.entries[view.ordinaryToRaw[i]];
        if (CatalogOverlap(a.first,a.second,e.controller,64) ||
            CatalogOverlap(a.first,a.second,e.header,44+static_cast<std::size_t>(e.declaredRecords)*64)) return false;
    }
    return read(script,out.program.data(),rule.programBytes.size()) &&
        std::equal(rule.programBytes.begin(),rule.programBytes.end(),out.program.begin()) &&
        read(anchor,out.anchorPrefix.data(),rule.anchorPrefix.size()) &&
        std::equal(rule.anchorPrefix.begin(),rule.anchorPrefix.end(),out.anchorPrefix.begin());
}
bool NativeResyncDormancyPreloadReadyImpl(uintptr_t base, const std::array<std::uint16_t,6>& location,
    RecordCatalogRead read, RecordCatalogStamp stamp) {
    const auto* rule=knowndormantscripts::Find(location);
    if (!rule) return true;
    if (rule->predicate!=knowndormantscripts::DormancyPredicate::AllBitsSet || !rule->flagMask) return false;
    TraceStamp before{}, after{}; std::uint8_t first=0, last=0;
    return IsDiagnosticGameThread() && CatalogSpan(base,rule->flagRva+1) &&
        stamp(base,before) && before.load && read(base+rule->flagRva,&first,1) && (first&rule->flagMask)==rule->flagMask &&
        read(base+rule->flagRva,&last,1) && (last&rule->flagMask)==rule->flagMask && first==last &&
        stamp(base,after) && CatalogSameStamp(before,after);
}
bool QualifyNativeOrdinaryResyncViewImpl(uintptr_t base, const NativeRecordCatalog& raw,
    NativeOrdinaryResyncView& out, RecordCatalogRead read, RecordCatalogStamp stamp) {
    out={}; out.raw=&raw;
    if (!IsDiagnosticGameThread() || base!=raw.exeBase || raw.entryCount>64 ||
        !raw.beforeAvailable || !raw.afterAvailable || !raw.lifecycleStable || !raw.tableInventoryComplete ||
        !CatalogSameStamp(raw.before,raw.after) || !raw.countBeforeAvailable || !raw.countAfterAvailable ||
        raw.countBefore!=static_cast<std::int32_t>(raw.entryCount) || raw.countBefore!=raw.countAfter) return false;
    const auto* rule=knowndormantscripts::Find(CatalogLocation(raw.before));
    // A known mixed target cannot evade its required Script by clearing bit0 or
    // omitting that slot and presenting a generic all-ordinary capture instead.
    if (!rule && !raw.issues && raw.status==NativeRecordContentStatus::Complete &&
        raw.associationComplete && raw.contentBytesComplete) {
        out.ordinaryCount=raw.entryCount;
        for (std::uint32_t i=0;i<raw.entryCount;++i) out.rawToOrdinary[i]=out.ordinaryToRaw[i]=static_cast<std::uint8_t>(i);
        out.qualified=true; return true;
    }
    if (raw.issues!=Issue(RecordCatalogIssue::Script) || raw.status!=NativeRecordContentStatus::Unsupported ||
        raw.layoutSha256!=knowndormantscripts::ImageLayout || !raw.before.load ||
        !CatalogSpan(base,HANDLE_REGIONS_RVA+64*8)) return false;
    if (!rule || rule->predicate!=knowndormantscripts::DormancyPredicate::AllBitsSet || !rule->flagMask ||
        raw.entryCount!=rule->rawCount || !ResyncOrdinaryShape(raw,out) ||
        out.scriptRawIndex==0xFF || out.ordinaryCount!=rule->ordinaryCount || !out.ordinaryCount) return false;
    out.mixed=true; out.ruleIndex=static_cast<std::uint8_t>(rule-knowndormantscripts::Rules.data());
    ResyncDormantAsset before{}, after{};
    if (!ResyncCatalogBookend(base,raw,read,stamp)) return false;
    out.flagBeforeAvailable=read(base+rule->flagRva,&out.flagBefore,1);
    if (!out.flagBeforeAvailable || (out.flagBefore&rule->flagMask)!=rule->flagMask ||
        !ReadResyncDormantAsset(base,raw,*rule,out,before,read) ||
        !ResyncCatalogBookend(base,raw,read,stamp) ||
        !ReadResyncDormantAsset(base,raw,*rule,out,after,read,&before) || before!=after) return false;
    out.flagAfterAvailable=read(base+rule->flagRva,&out.flagAfter,1);
    if (!out.flagAfterAvailable || out.flagBefore!=out.flagAfter || (out.flagAfter&rule->flagMask)!=rule->flagMask ||
        !ResyncCatalogBookend(base,raw,read,stamp)) return false;
    out.nativeAssetBound=true; out.nativeAssetRoot=before.root; out.qualified=true; return true;
}
void ResolveNativeOrdinaryResyncMembershipImpl(uintptr_t base, const NativeRecordCatalog& raw,
    const NativeOrdinaryResyncView& view, uintptr_t actor, NativeRecordMembership& out,
    RecordCatalogRead read, RecordCatalogStamp stamp) {
    out={}; out.actor=actor;
    if (!view.qualified || view.raw!=&raw) {out.issues=Issue(RecordCatalogIssue::NoAssociation); return;}
    if (!view.mixed) {ResolveRecordMembershipImpl(base,raw,actor,out,read,stamp); return;}
    NativeOrdinaryResyncView current;
    if (!QualifyNativeOrdinaryResyncViewImpl(base,raw,current,read,stamp) || !current.mixed ||
        current.ordinaryToRaw!=view.ordinaryToRaw || current.rawToOrdinary!=view.rawToOrdinary ||
        current.ordinaryCount!=view.ordinaryCount || current.scriptRawIndex!=view.scriptRawIndex ||
        current.ruleIndex!=view.ruleIndex || current.nativeAssetRoot!=view.nativeAssetRoot) {
        out.issues=Issue(RecordCatalogIssue::NoAssociation); return;
    }
    const auto& rule=knowndormantscripts::Rules[current.ruleIndex];
    if (CatalogOverlap(actor,0x9F8,current.nativeAssetRoot,16+rule.barCount*16) ||
        CatalogOverlap(actor,0x9F8,current.nativeAssetRoot+rule.scriptOffset,rule.programBytes.size()) ||
        CatalogOverlap(actor,0x9F8,base+knowndormantscripts::ArdRootRva,8) ||
        CatalogOverlap(actor,0x9F8,base+HANDLE_REGIONS_RVA,64*8) || CatalogOverlap(actor,0x9F8,base+rule.flagRva,1)) {
        out.issues=Issue(RecordCatalogIssue::Alias); return;
    }
    ResolveRecordMembershipImpl(base,raw,actor,out,read,stamp,&current);
    NativeOrdinaryResyncView after;
    if (!QualifyNativeOrdinaryResyncViewImpl(base,raw,after,read,stamp) ||
        after.nativeAssetRoot!=current.nativeAssetRoot || after.rawToOrdinary!=current.rawToOrdinary) {
        out.issues|=Issue(RecordCatalogIssue::Changed); out.contentComparisonAvailable=false;
        out.status=NativeRecordContentStatus::Partial;
    }
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
            (!enemyclass::Combat(f.objectType,f.objectId) || (f.objectName[0] == 'F' && f.objectName[1] == '_'));
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
bool QualifyNativeOrdinaryResyncView(uintptr_t exeBase, const NativeRecordCatalog& raw,
    NativeOrdinaryResyncView& out) {
    return QualifyNativeOrdinaryResyncViewImpl(exeBase,raw,out,&CopyNative,&CaptureDiagnosticStamp);
}
bool NativeResyncDormancyPreloadReady(uintptr_t exeBase, const std::array<std::uint16_t,6>& location) {
    return NativeResyncDormancyPreloadReadyImpl(exeBase,location,&CopyNative,&CaptureDiagnosticStamp);
}
void ResolveNativeOrdinaryResyncMembership(uintptr_t exeBase, const NativeRecordCatalog& raw,
    const NativeOrdinaryResyncView& view, uintptr_t actor, NativeRecordMembership& out) {
    ResolveNativeOrdinaryResyncMembershipImpl(exeBase,raw,view,actor,out,&CopyNative,&CaptureDiagnosticStamp);
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

bool DiagnosticGameThreadKnown() { return g_diagnosticGameThread.load() != 0; }

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

bool Install(uintptr_t exeBase, LogFn log, RoleFn role, CaptureFn capture, CopyFn copy, bool trace, EmissionFn emission, ReplayFn replay) {
    if (g_installed) return true;
    g_exeBase = exeBase;
    g_moraleGeneratedVerified=Matches(exeBase+GENERATED_RVA,routespawn::MoraleWrapperBody) &&
        Matches(exeBase+0x3FE96A,routespawn::MoraleCaller);
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
    {
        char setting[4] {};
        g_seatRequested = !(GetEnvironmentVariableA("KH2COOP_SEAT_ACTIVATION", setting, sizeof(setting)) == 1 &&
                            setting[0] == '0');
        g_seatPredicatesVerified = VerifyRegionPredicates(exeBase);
        if (g_log) g_log("[spawnctl] seat-activation requested=%u predicates=%u (VUH-2864)",
                         g_seatRequested ? 1U : 0U, g_seatPredicatesVerified ? 1U : 0U);
    }
    g_emission = nullptr; g_replay = nullptr; // publish only after both wrapper gates are live
    if (emission && !g_emissionStorage.Init(reinterpret_cast<const void*>(&Install))) {
        if (g_log) g_log("[spawn-emission] unavailable: FLS storage");
        return false;
    }
    g_lookup = reinterpret_cast<LookupFn>(exeBase + LOOKUP_RVA);
    auto* address = reinterpret_cast<void*>(exeBase + UPDATE_RVA);
    auto status = MH_CreateHook(address, reinterpret_cast<void*>(&HookedUpdate),
                                reinterpret_cast<void**>(&g_original));
    if (status == MH_OK) {
        status = MH_EnableHook(address);
        if (status != MH_OK) MH_RemoveHook(address);
    }
    if (status != MH_OK) {
        if (g_log) g_log("[spawnctl] unavailable: hook installation failed status=%d", status);
        g_original = nullptr;
        if (g_knownMutationRequested) g_knownMutation.Poison();
        return false;
    }
    g_installed = true;
    InstallKnownMutationHooks(); // Independent of every diagnostic trace flag.
    if (emission && !trace) {
        g_fixedInstalled = Matches(exeBase + WRAPPER_CALL_RVA, kWrapperCallBytes) &&
            InstallDiagnosticHook(WRAPPER_RVA, kWrapperBytes, reinterpret_cast<void*>(&HookedWrapper), g_originalWrapper, true);
        g_generatedInstalled = InstallDiagnosticHook(GENERATED_RVA, kGeneratedBytes,
            reinterpret_cast<void*>(&HookedGenerated), g_originalGenerated, true);
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
    if (emission) {
        const bool ready = g_fixedInstalled.load() && g_generatedInstalled.load();
        if (ready) { g_emission = emission; g_replay = replay; }
        if (g_log) g_log("[spawn-emission] %s fixed=%u generated=%u", ready ? "ready" : "unavailable",
            g_fixedInstalled ? 1U : 0U, g_generatedInstalled ? 1U : 0U);
    }
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
    g_seats = nullptr;
    g_seatPredicatesVerified = false;
    g_seatChoices = g_seatRegionsUnavailable = g_seatHolds = 0;
    g_emission = nullptr; g_replay = nullptr;
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


namespace {
// Exact owned Steam PE9002b2de ranges; manifest records bytes/SHA256.
// Split constructor effect setup pins cover its complete exact317 no-type18 path.
constexpr std::uint8_t kChildPin0[]={
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x40,0x0F,0x29,0x74,0x24,0x30,0x0F,0x28,0xF2,0x48,0x8B,0xFA,0x8B,0xD9,0x0F,
    0xBA,0xE1,0x1E,0x73,0x63,0xE8,0x5E,0x05,0x00,0x00,0x48,0x85,0xC0,0x74,0x59,0x0F,0xB6,0x40,0x04,0x3C,0x18,0x77,0x0A,0xB9,
    0x80,0x00,0x40,0x01,0x0F,0xA3,0xC1,0x72,0x47,0x8B,0xCB,0xE8,0x40,0x05,0x00,0x00,0xB9,0xB0,0x0B,0x00,0x00,0x48,0x8B,0xD8,
    0xE8,0xB3,0x2A,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xA4,0x04,0x00,0x00,0x4C,0x8B,0xCF,0xF3,0x0F,0x11,0x74,0x24,0x20,0x41,
    0xB8,0x06,0x00,0x00,0x00,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0xE8,0x90,0x98,0x03,0x00,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,
    0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xC3,0x8D,0x83,0xE7,0xFC,0xFF,0xFF,0x83,0xF8,0x01,0x0F,0x86,0x6B,0x04,0x00,0x00,0x81,
    0xFB,0xEE,0x03,0x00,0x00,0x0F,0x84,0x5F,0x04,0x00,0x00,0x8B,0xC3,0xC1,0xE8,0x1D,0x24,0x01,0x0F,0x85,0x52,0x04,0x00,0x00,
    0x8B,0xC3,0xC1,0xE8,0x1C,0xA8,0x01,0x0F,0x85,0x45,0x04,0x00,0x00,0x81,0xFB,0x36,0x02,0x00,0x00,0x0F,0x84,0xEF,0x03,0x00,
    0x00,0x8B,0xCB,0xE8,0xB8,0x04,0x00,0x00,0x48,0x8B,0xD8,0x0F,0xB6,0x48,0x54,0x66,0x0F,0x6E,0xC1,0x0F,0x5B,0xC0,0xE8,0xF5,
    0x24,0xFC,0xFF,0x84,0xC0,0x0F,0x84,0x17,0x04,0x00,0x00,0x0F,0xB6,0x43,0x04,0x83,0xF8,0x18,0x0F,0x87,0x52,0x01,0x00,0x00,
    0x8B,0xC8,0x48,0x8D,0x05,0xD7,0x05,0xC2,0xFF,0x8B,0x94,0x88,0x3C,0xFE,0x3D,0x00,0x48,0x03,0xD0,0xFF,0xE2,0x0F,0xBE,0x53,
    0x57,0x48,0x8B,0xCB,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,
    0x5F,0xE9,0x5A,0x82,0xFC,0xFF,0x0F,0x28,0xD6,0x48,0x8B,0xD7,0x48,0x8B,0xCB,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,
    0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x8D,0x34,0xFE,0xFF,0xB9,0x50,0x0D,0x00,0x00,0xE8,0xB3,0x29,0xD7,0xFF,0x48,0x85,0xC0,
    0x0F,0x84,0xA4,0x03,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,
    0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x8A,0xA3,0x03,0x00,0xB9,0x50,0x0D,0x00,0x00,0xE8,0x80,0x29,0xD7,0xFF,
    0x48,0x85,0xC0,0x0F,0x84,0x71,0x03,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,
    0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x77,0xB0,0x03,0x00,0xB9,0x48,0x0D,0x00,0x00,0xE8,0x4D,
    0x29,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0x3E,0x03,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,
    0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x34,0x6E,0x02,0x00,0xB9,0x50,0x0D,0x00,
    0x00,0xE8,0x1A,0x29,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0x0B,0x03,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,
    0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x61,0x92,0x01,0x00,0xB9,
    0x50,0x0D,0x00,0x00,0xE8,0xE7,0x28,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xD8,0x02,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,
    0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x7E,0x9E,
    0x03,0x00,0xB9,0x48,0x0D,0x00,0x00,0xE8,0xB4,0x28,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xA5,0x02,0x00,0x00,0x0F,0x28,0xDE,
    0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,
    0xE9,0x9B,0x9A,0x03,0x00,0xB9,0x48,0x0D,0x00,0x00,0xE8,0x81,0x28,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0x72,0x02,0x00,0x00,
    0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,
    0xC4,0x40,0x5F,0xE9,0x38,0xA6,0x03,0x00,0xB9,0x48,0x0D,0x00,0x00,0xE8,0x4E,0x28,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0x3F,
    0x02,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,
    0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0xF5,0xAB,0x03,0x00,0xB9,0x00,0x0D,0x00,0x00,0xE8,0x1B,0x28,0xD7,0xFF,0x48,0x85,0xC0,
    0x0F,0x84,0x0C,0x02,0x00,0x00,0x4C,0x8B,0xCF,0xF3,0x0F,0x11,0x74,0x24,0x20,0x41,0xB8,0x05,0x00,0x00,0x00,0x48,0x8B,0xD3,
    0x48,0x8B,0xC8,0xE8,0xA8,0x69,0x03,0x00,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xC3,
    0xE8,0x83,0xEE,0xD6,0xFF,0x85,0xC0,0x0F,0x85,0xD5,0x01,0x00,0x00,0x0F,0x28,0xD6,0x48,0x8B,0xD7,0x48,0x8B,0xCB,0x48,0x8B,
    0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0xBE,0x91,0x03,0x00,0xE8,0x59,0xEE,0xD6,0xFF,0x85,
    0xC0,0x0F,0x85,0xAB,0x01,0x00,0x00,0xB9,0xF8,0x0B,0x00,0x00,0xE8,0xA7,0x27,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0x98,0x01,
    0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,
    0x48,0x83,0xC4,0x40,0x5F,0xE9,0xDE,0xA7,0x03,0x00,0xB9,0x08,0x0C,0x00,0x00,0xE8,0x74,0x27,0xD7,0xFF,0x48,0x85,0xC0,0x0F,
    0x84,0x65,0x01,0x00,0x00,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,
    0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x7B,0x27,0x02,0x00,0x0F,0x28,0xD6,0x48,0x8B,0xD7,0x48,0x8B,0xCB,0x48,0x8B,
    0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0xEE,0x2B,0x17,0x00,0x0F,0x28,0xD6,0x48,0x8B,0xD7,
    0x48,0x8B,0xCB,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x31,0x42,0xFB,0xFF,0x0F,
    0x28,0xD6,0x48,0x8B,0xD7,0x48,0x8B,0xCB,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,
    0x94,0x2C,0x03,0x00,0xB9,0xA8,0x0F,0x00,0x00,0xE8,0xEA,0x26,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xDB,0x00,0x00,0x00,0x0F,
    0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,
    0x40,0x5F,0xE9,0x91,0xDF,0xFF,0xFF,0xB9,0xE0,0x0B,0x00,0x00,0xE8,0xB7,0x26,0xD7,0xFF,0x48,0x85,0xC0,0x0F,0x84,0xA8,0x00,
    0x00,0x00,0xF3,0x0F,0x10,0x05,0xF6,0x3C,0x24,0x00,0x45,0x33,0xC9,0x4C,0x8B,0xC7,0xF3,0x0F,0x11,0x44,0x24,0x20,0x48,0x8B,
    0xD3,0x48,0x8B,0xC8,0xE8,0x6F,0x82,0x01,0x00,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,
    0xC3,0xB9,0x08,0x0C,0x00,0x00,0xE8,0x75,0x26,0xD7,0xFF,0x48,0x85,0xC0,0x74,0x6A,0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,
    0xD3,0x48,0x8B,0xC8,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x30,0xB2,0x03,0x00,
    0x33,0xC9,0xE8,0xA9,0x53,0x02,0x00,0x8B,0xC8,0xE8,0xC2,0x00,0x00,0x00,0x0F,0xBE,0x58,0x57,0x85,0xDB,0x75,0x15,0x0F,0xB6,
    0x1D,0xD7,0xCF,0x5C,0x00,0x8B,0xCB,0xE8,0x8C,0x53,0x02,0x00,0x8B,0xC8,0xE8,0xA5,0x00,0x00,0x00,0x8B,0xD3,0x48,0x8B,0xC8,
    0x0F,0x28,0xDE,0x4C,0x8B,0xC7,0x48,0x8B,0x5C,0x24,0x58,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xE9,0x86,0x7E,
    0xFC,0xFF,0x48,0x8B,0x5C,0x24,0x58,0x33,0xC0,0x0F,0x28,0x74,0x24,0x30,0x48,0x83,0xC4,0x40,0x5F,0xC3,0x35,0xFA,0x3D,0x00,
    0x56,0xFA,0x3D,0x00,0x72,0xFB,0x3D,0x00,0x0C,0xFB,0x3D,0x00,0x73,0xFA,0x3D,0x00,0x72,0xFB,0x3D,0x00,0x72,0xFB,0x3D,0x00,
    0x48,0xFC,0x3D,0x00,0x72,0xFB,0x3D,0x00,0xD9,0xFA,0x3D,0x00,0xB2,0xFC,0x3D,0x00,0x72,0xFB,0x3D,0x00,0x3F,0xFB,0x3D,0x00,
    0xA5,0xFB,0x3D,0x00,0x72,0xFB,0x3D,0x00,0x0B,0xFC,0x3D,0x00,0xE5,0xFC,0x3D,0x00,0x02,0xFD,0x3D,0x00,0x6F,0xFD,0x3D,0x00,
    0x3C,0xFD,0x3D,0x00,0x72,0xFC,0x3D,0x00,0xA6,0xFA,0x3D,0x00,0x1F,0xFD,0x3D,0x00,0xD8,0xFB,0x3D,0x00,0xB1,0xFD,0x3D,0x00,
};
constexpr std::uint8_t kChildPin1[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xC1,0x8B,0xD1,0x25,0xFF,0xFF,0xFF,0x0F,0x3D,0xEE,0x03,0x00,0x00,0x0F,0x87,0x26,0x01,
    0x00,0x00,0x0F,0x84,0x12,0x01,0x00,0x00,0x05,0xCA,0xFD,0xFF,0xFF,0x3D,0xE4,0x00,0x00,0x00,0x0F,0x87,0x42,0x01,0x00,0x00,
    0x4C,0x8D,0x05,0x19,0x01,0xC2,0xFF,0x41,0x0F,0xB6,0x84,0x00,0x70,0x00,0x3E,0x00,0x41,0x8B,0x8C,0x80,0x40,0x00,0x3E,0x00,
    0x49,0x03,0xC8,0xFF,0xE1,0x8B,0x15,0x9D,0x50,0x64,0x02,0x85,0xD2,0x0F,0x85,0x23,0x01,0x00,0x00,0x0F,0xB6,0x0D,0xC2,0xCE,
    0x5C,0x00,0xE8,0x79,0x52,0x02,0x00,0x8B,0xD0,0x85,0xC0,0x0F,0x85,0x0D,0x01,0x00,0x00,0x33,0xC9,0xE8,0x68,0x52,0x02,0x00,
    0x8B,0xD0,0xE9,0xF3,0x00,0x00,0x00,0xB9,0x01,0x00,0x00,0x00,0xE8,0xA7,0x33,0xFE,0xFF,0x8B,0xD0,0xE9,0xE2,0x00,0x00,0x00,
    0xB9,0x02,0x00,0x00,0x00,0xE8,0x96,0x33,0xFE,0xFF,0x8B,0xD0,0xE9,0xD1,0x00,0x00,0x00,0xB9,0x01,0x00,0x00,0x00,0xE8,0xC5,
    0x33,0xFE,0xFF,0x8B,0xD0,0xE9,0xC0,0x00,0x00,0x00,0xB9,0x02,0x00,0x00,0x00,0xE8,0xB4,0x33,0xFE,0xFF,0x8B,0xD0,0xE9,0xAF,
    0x00,0x00,0x00,0xB9,0x01,0x00,0x00,0x00,0xE8,0x13,0x93,0x03,0x00,0x8B,0xD0,0xE9,0x9E,0x00,0x00,0x00,0xB9,0x36,0x02,0x00,
    0x00,0xE8,0x22,0xFF,0xFF,0xFF,0x48,0x8B,0xD8,0x66,0x83,0x78,0x4E,0x00,0x74,0x29,0x0F,0xB7,0x50,0x4C,0x48,0x8D,0x0D,0xFD,
    0xBD,0x5C,0x00,0xE8,0x38,0x15,0xFC,0xFF,0x48,0x85,0xC0,0x74,0x14,0x0F,0xB7,0x10,0x85,0xD2,0x74,0x0D,0x0F,0xB7,0x4B,0x4E,
    0xE8,0xF3,0xF8,0x00,0x00,0x8B,0xD0,0xEB,0x61,0x33,0xD2,0xEB,0x5D,0xE8,0x76,0x2E,0x00,0x00,0x0F,0xB7,0x48,0x12,0xEB,0xB9,
    0xE8,0x6B,0x2E,0x00,0x00,0x0F,0xB7,0x48,0x14,0xEB,0xAE,0xB9,0x65,0x00,0x00,0x00,0xEB,0xA7,0xB9,0x03,0x00,0x00,0x00,0xE8,
    0x34,0x33,0xFE,0xFF,0x8B,0xD0,0xEB,0x32,0x2D,0x2A,0x06,0x00,0x00,0x74,0x16,0x83,0xF8,0x01,0x75,0x26,0x33,0xD2,0x81,0x3D,
    0x38,0x7D,0x70,0x02,0x0B,0x04,0x00,0x00,0x0F,0x95,0xC2,0xEB,0x0F,0x33,0xD2,0x81,0x3D,0x27,0x7D,0x70,0x02,0x0B,0x04,0x00,
    0x00,0x0F,0x94,0xC2,0x81,0xC2,0x0B,0x04,0x00,0x00,0x85,0xD2,0x75,0x08,0x33,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3,0x81,0xE2,
    0xFF,0xFF,0xFF,0x0F,0x8B,0xCA,0x48,0x83,0xC4,0x20,0x5B,0xE9,0xD0,0x0E,0x00,0x00,0xFD,0xFE,0x3D,0x00,0x2F,0xFF,0x3D,0x00,
    0x40,0xFF,0x3D,0x00,0x0B,0xFF,0x3D,0x00,0x73,0xFF,0x3D,0x00,0x84,0xFF,0x3D,0x00,0xC5,0xFF,0x3D,0x00,0xD0,0xFF,0x3D,0x00,
    0xDB,0xFF,0x3D,0x00,0x51,0xFF,0x3D,0x00,0x62,0xFF,0x3D,0x00,0x22,0x00,0x3E,0x00,0x00,0x01,0x02,0x0B,0x0B,0x03,0x04,0x05,
    0x0B,0x06,0x07,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x08,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,0x0B,
    0x0B,0x0B,0x0B,0x09,0x0A,
};
constexpr std::uint8_t kChildPin2[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xEC,0x30,0x48,0x63,0xF1,0x48,
    0x8D,0x3D,0x02,0x41,0x64,0x02,0x33,0xDB,0x48,0x8D,0x2D,0xC9,0x13,0xE0,0xFF,0x48,0x8B,0x17,0x48,0x85,0xD2,0x74,0x21,0x4C,
    0x63,0x42,0x04,0x48,0x8B,0xCE,0x48,0x83,0xC2,0x08,0x48,0x89,0x6C,0x24,0x20,0x41,0xB9,0x60,0x00,0x00,0x00,0xFF,0x15,0x25,
    0xAC,0x19,0x00,0x48,0x85,0xC0,0x75,0x0D,0xFF,0xC3,0x48,0x83,0xC7,0x08,0x83,0xFB,0x03,0x7C,0xCC,0x33,0xC0,0x48,0x8B,0x5C,
    0x24,0x40,0x48,0x8B,0x6C,0x24,0x48,0x48,0x8B,0x74,0x24,0x50,0x48,0x83,0xC4,0x30,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin3[]={
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,
    0xF2,0x8B,0xF9,0x48,0x85,0xD2,0x75,0x08,0xE8,0x2B,0xFD,0xFF,0xFF,0x48,0x8B,0xF0,0x4C,0x8D,0x46,0x08,0x48,0x8D,0x15,0xD5,
    0x83,0x1E,0x00,0x48,0x8D,0x0D,0x16,0x4E,0x64,0x02,0xE8,0xA1,0xC3,0xC9,0xFF,0xE8,0x2C,0x00,0xD7,0xFF,0x85,0xC0,0x74,0x36,
    0xE8,0x23,0x00,0xD7,0xFF,0x83,0xF8,0x07,0x74,0x2C,0x48,0x8D,0x15,0xBF,0x83,0x1E,0x00,0x48,0x8D,0x4E,0x08,0xE8,0x8C,0x18,
    0x09,0x00,0x48,0x85,0xC0,0x74,0x17,0x4C,0x8D,0x46,0x08,0x48,0x8D,0x15,0xAE,0x83,0x1E,0x00,0x48,0x8D,0x0D,0xD7,0x4D,0x64,
    0x02,0xE8,0x62,0xC3,0xC9,0xFF,0x48,0x8D,0x0D,0xCB,0x4D,0x64,0x02,0xE8,0xC6,0xD9,0xFB,0xFF,0x41,0xBE,0x01,0x00,0x00,0x00,
    0x8B,0xC8,0x41,0x8B,0xDE,0x44,0x8B,0xFF,0xD3,0xE3,0x41,0xBC,0x80,0x00,0x40,0x01,0x41,0x81,0xE7,0x00,0x00,0x00,0x40,0x74,
    0x25,0x8B,0xCF,0xE8,0xA0,0xFC,0xFF,0xFF,0x48,0x85,0xC0,0x74,0x19,0x0F,0xB6,0x40,0x04,0x3C,0x18,0x0F,0x87,0xB8,0x00,0x00,
    0x00,0x0F,0xB6,0xC0,0x41,0x0F,0xA3,0xC4,0x0F,0x83,0xAB,0x00,0x00,0x00,0x8D,0x87,0xE7,0xFC,0xFF,0xFF,0x41,0x3B,0xC6,0x0F,
    0x86,0x9C,0x00,0x00,0x00,0x81,0xFF,0xEE,0x03,0x00,0x00,0x0F,0x84,0x90,0x00,0x00,0x00,0x8B,0xC7,0xC1,0xE8,0x1D,0x41,0x22,
    0xC6,0x0F,0x85,0x82,0x00,0x00,0x00,0x0F,0xB6,0x46,0x48,0xF6,0xD0,0x41,0x84,0xC6,0x74,0x77,0xE8,0x69,0xFF,0xD6,0xFF,0x85,
    0xC0,0x74,0x42,0xE8,0x60,0xFF,0xD6,0xFF,0x83,0xF8,0x07,0x74,0x38,0x4C,0x8B,0x0D,0x54,0xC3,0x50,0x00,0x4C,0x8D,0x46,0x08,
    0x48,0x8D,0x15,0x19,0x83,0x1E,0x00,0x48,0x8D,0x0D,0x4A,0x4D,0x64,0x02,0xE8,0xAD,0xC2,0xC9,0xFF,0x48,0x8D,0x0D,0x3E,0x4D,
    0x64,0x02,0xE8,0x11,0x07,0xFC,0xFF,0x48,0x85,0xC0,0x75,0x20,0x48,0x8D,0x15,0x05,0x83,0x1E,0x00,0xEB,0x07,0x48,0x8D,0x15,
    0xDC,0x82,0x1E,0x00,0x4C,0x8D,0x46,0x08,0x48,0x8D,0x0D,0x19,0x4D,0x64,0x02,0xE8,0x7C,0xC2,0xC9,0xFF,0x48,0x8D,0x0D,0x0D,
    0x4D,0x64,0x02,0xE8,0xE0,0xD8,0xFB,0xFF,0x8B,0xC8,0x41,0x8B,0xC6,0xD3,0xE0,0x0B,0xD8,0x45,0x85,0xFF,0x74,0x1D,0x8B,0xCF,
    0xE8,0xCB,0xFB,0xFF,0xFF,0x48,0x85,0xC0,0x74,0x11,0x0F,0xB6,0x40,0x04,0x3C,0x18,0x77,0x2C,0x0F,0xB6,0xC8,0x41,0x0F,0xA3,
    0xCC,0x73,0x23,0x80,0x7E,0x28,0x00,0x74,0x1D,0x45,0x33,0xC0,0x8B,0xD7,0x48,0x8B,0xCE,0xE8,0xC2,0x08,0x00,0x00,0x48,0x8B,
    0xC8,0xE8,0x9A,0xD8,0xFB,0xFF,0x8B,0xC8,0x41,0xD3,0xE6,0x41,0x0B,0xDE,0x48,0x8B,0x6C,0x24,0x60,0x8B,0xC3,0x48,0x8B,0x5C,
    0x24,0x58,0x48,0x83,0xC4,0x20,0x41,0x5F,0x41,0x5E,0x41,0x5C,0x5F,0x5E,0xC3,
};
constexpr std::uint8_t kChildPin4[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0x45,0x33,0xC0,0x4D,0x85,0xC0,0x75,0x09,0x4C,
    0x8B,0x05,0xCA,0x51,0x65,0x02,0xEB,0x0C,0x41,0x8B,0x48,0x70,0xE8,0x67,0xF0,0x10,0x00,0x4C,0x8B,0xC0,0x4D,0x85,0xC0,0x74,
    0x3F,0x49,0x8D,0x40,0x04,0x4C,0x8B,0xCF,0x4C,0x2B,0xC8,0x0F,0x1F,0x44,0x00,0x00,0x0F,0xB6,0x10,0x42,0x0F,0xB6,0x0C,0x08,
    0x2B,0xD1,0x75,0x07,0x48,0xFF,0xC0,0x85,0xC9,0x75,0xED,0x85,0xD2,0x75,0xBB,0x83,0xFB,0xFF,0x74,0x06,0x41,0x3B,0x58,0x48,
    0x75,0xB0,0x49,0x8B,0xC0,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x33,0xC0,0x48,
    0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin5[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0xE8,0x95,0x15,0x00,0x00,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x74,0x09,0x81,0x78,0x48,0x10,0x27,
    0x00,0x00,0x7C,0x1D,0x33,0xC0,0x48,0x85,0xC0,0x75,0x09,0x48,0x8B,0x05,0x5E,0x67,0x65,0x02,0xEB,0x08,0x8B,0x48,0x70,0xE8,
    0xFC,0x05,0x11,0x00,0x48,0x85,0xC0,0x75,0xF3,0x48,0x8D,0x4B,0x04,0x48,0x8D,0x15,0x1C,0x71,0x22,0x00,0xE8,0xC5,0x4D,0x0D,
    0x00,0x66,0xFF,0x43,0x02,0x48,0x8B,0x43,0x58,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin6[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x33,0xC0,0x0F,0x1F,0x44,0x00,0x00,0x48,0x85,0xC0,0x75,0x09,0x48,0x8B,0x05,
    0x9C,0x59,0x65,0x02,0xEB,0x08,0x8B,0x48,0x70,0xE8,0x3A,0xF8,0x10,0x00,0x48,0x85,0xC0,0x74,0x15,0x48,0x39,0x58,0x58,0x75,
    0xDF,0xB9,0xFF,0xFF,0x00,0x00,0x66,0x01,0x48,0x02,0x48,0x83,0xC4,0x20,0x5B,0xC3,0xB9,0xFF,0xFF,0x00,0x00,0x66,0x01,0x48,
    0x02,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin7[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x49,0x8B,0xF8,0x48,0x8B,0x49,
    0x10,0x8B,0xF2,0x48,0x85,0xC9,0x74,0x05,0xE8,0x1B,0x76,0xFB,0xFF,0x48,0x8B,0x4B,0x18,0x48,0x85,0xC9,0x74,0x05,0xE8,0x0D,
    0x76,0xFB,0xFF,0x48,0x8B,0x4B,0x20,0x48,0x85,0xC9,0x74,0x05,0xE8,0xFF,0x75,0xFB,0xFF,0x33,0xC0,0xC7,0x03,0xFF,0xFF,0xFF,
    0xFF,0x89,0x43,0x04,0x48,0x89,0x43,0x10,0x48,0x89,0x43,0x18,0x48,0x89,0x43,0x20,0x48,0x85,0xFF,0x75,0x0A,0x8B,0xCE,0xE8,
    0x7C,0x9A,0xFF,0xFF,0x48,0x8B,0xF8,0x48,0x8B,0xD7,0x8B,0xCE,0xE8,0x1F,0x9D,0xFF,0xFF,0xA9,0xF3,0xFF,0xFF,0xFF,0x74,0x05,
    0xE8,0x03,0x6E,0xFB,0xFF,0x48,0x8B,0xCB,0x89,0x73,0x04,0x48,0x89,0x7B,0x08,0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0x74,0x24,
    0x38,0x48,0x83,0xC4,0x20,0x5F,0xE9,0xB5,0xFC,0xFF,0xFF,
};
constexpr std::uint8_t kChildPin8[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x33,0xD2,0x48,0x8B,0x49,0x08,0xE8,0x1C,0xAA,0xFF,0xFF,0x8B,0x13,0x48,0x8B,
    0xC8,0xE8,0x02,0x6B,0xFB,0xFF,0x8B,0x4B,0x04,0x48,0x89,0x43,0x10,0xE8,0x86,0xA2,0xFF,0xFF,0x84,0xC0,0x75,0x2F,0x8B,0x4B,
    0x04,0xE8,0xCA,0xA2,0xFF,0xFF,0x84,0xC0,0x75,0x23,0x48,0x8B,0x4B,0x08,0x0F,0xB6,0x41,0x48,0xF6,0xD0,0xA8,0x01,0x74,0x15,
    0x33,0xD2,0xE8,0x41,0xA9,0xFF,0xFF,0x8B,0x13,0x48,0x8B,0xC8,0xE8,0xC7,0x6A,0xFB,0xFF,0x48,0x89,0x43,0x18,0x8B,0x4B,0x04,
    0xE8,0x4B,0xA2,0xFF,0xFF,0x84,0xC0,0x75,0x25,0x8B,0x53,0x04,0x45,0x33,0xC0,0x48,0x8B,0x4B,0x08,0xE8,0x38,0xAA,0xFF,0xFF,
    0x48,0x85,0xC0,0x74,0x11,0xBA,0xFF,0xFF,0xFF,0xFF,0x48,0x8B,0xC8,0xE8,0x96,0x6A,0xFB,0xFF,0x48,0x89,0x43,0x20,0x48,0x83,
    0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin9[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x85,0xD2,0x48,0x8D,0x79,0x08,0x48,0x8D,0x1D,0x48,0x44,0x64,0x02,
    0x4C,0x8B,0xC7,0x48,0x0F,0x45,0xDA,0x48,0x8D,0x15,0xF2,0x79,0x1E,0x00,0x48,0x8B,0xCB,0xE8,0xC2,0xB9,0xC9,0xFF,0xE8,0x4D,
    0xF6,0xD6,0xFF,0x85,0xC0,0x74,0x30,0xE8,0x44,0xF6,0xD6,0xFF,0x83,0xF8,0x07,0x74,0x26,0x48,0x8D,0x15,0xE0,0x79,0x1E,0x00,
    0x48,0x8B,0xCF,0xE8,0xAE,0x0E,0x09,0x00,0x48,0x85,0xC0,0x74,0x12,0x4C,0x8B,0xC7,0x48,0x8D,0x15,0xD1,0x79,0x1E,0x00,0x48,
    0x8B,0xCB,0xE8,0x89,0xB9,0xC9,0xFF,0x48,0x8B,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin10[]={
    0x40,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB6,0x41,0x48,0x48,0x8B,0xF9,0xF6,0xD0,0xA8,0x01,0x75,0x08,0x33,0xC0,0x48,0x83,0xC4,
    0x20,0x5F,0xC3,0x48,0x89,0x5C,0x24,0x30,0x48,0x85,0xD2,0x48,0x8D,0x1D,0xFE,0x44,0x64,0x02,0x48,0x0F,0x45,0xDA,0xE8,0xED,
    0xF6,0xD6,0xFF,0x85,0xC0,0x74,0x3A,0xE8,0xE4,0xF6,0xD6,0xFF,0x83,0xF8,0x07,0x74,0x30,0x4C,0x8B,0x0D,0xD8,0xBA,0x50,0x00,
    0x4C,0x8D,0x47,0x08,0x48,0x8D,0x15,0x9D,0x7A,0x1E,0x00,0x48,0x8B,0xCB,0xE8,0x35,0xBA,0xC9,0xFF,0x48,0x8B,0xCB,0xE8,0x9D,
    0xFE,0xFB,0xFF,0x48,0x85,0xC0,0x75,0x1C,0x48,0x8D,0x15,0x91,0x7A,0x1E,0x00,0xEB,0x07,0x48,0x8D,0x15,0x68,0x7A,0x1E,0x00,
    0x4C,0x8D,0x47,0x08,0x48,0x8B,0xCB,0xE8,0x0C,0xBA,0xC9,0xFF,0x48,0x8B,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,
    0x5F,0xC3,
};
constexpr std::uint8_t kChildPin11[]={
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x4D,0x85,0xC0,0x48,
    0x8D,0x3D,0x12,0x44,0x64,0x02,0x8B,0xF2,0x8B,0xDA,0x49,0x0F,0x45,0xF8,0x48,0x8B,0xE9,0xC1,0xEE,0x1C,0x4C,0x8D,0x41,0x28,
    0x40,0x80,0xE6,0x01,0x41,0x80,0x38,0x00,0x74,0x75,0x48,0x8D,0x15,0xAB,0x79,0x1E,0x00,0x48,0x8B,0xCF,0xE8,0x27,0xB9,0xC9,
    0xFF,0x40,0x84,0xF6,0x75,0x2E,0x8D,0x83,0xE7,0xFC,0xFF,0xFF,0x83,0xF8,0x01,0x76,0x10,0x81,0xFB,0xEE,0x03,0x00,0x00,0x74,
    0x08,0xC1,0xEB,0x1D,0x80,0xE3,0x01,0x74,0x41,0x80,0x7D,0x04,0x00,0x74,0x3B,0x48,0x8B,0xCF,0xE8,0x69,0xCF,0xFB,0xFF,0x83,
    0xF8,0x02,0x7D,0x2E,0xBA,0x2E,0x00,0x00,0x00,0x48,0x8B,0xCF,0xE8,0xEF,0x0D,0x09,0x00,0xF2,0x0F,0x10,0x05,0x67,0x79,0x1E,
    0x00,0xF2,0x0F,0x11,0x00,0x0F,0xB7,0x0D,0x64,0x79,0x1E,0x00,0x66,0x89,0x48,0x08,0x0F,0xB6,0x0D,0x5B,0x79,0x1E,0x00,0x88,
    0x48,0x0A,0x48,0x8B,0xC7,0xEB,0x1F,0x40,0x84,0xF6,0x74,0x18,0x4C,0x8D,0x41,0x08,0x48,0x8B,0xCF,0x48,0x8D,0x15,0x46,0x79,
    0x1E,0x00,0xE8,0xA9,0xB8,0xC9,0xFF,0x48,0x8B,0xC7,0xEB,0x02,0x33,0xC0,0x48,0x8B,0x5C,0x24,0x38,0x48,0x8B,0x6C,0x24,0x40,
    0x48,0x8B,0x74,0x24,0x48,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin12[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x02,0x1A,0xFC,0xFF,0x48,0x8D,0x0D,0xA3,0x8A,0x33,0x00,0xE8,0xF6,0x33,
    0x09,0x00,0x89,0x03,0x48,0x8B,0xC3,0x81,0x8B,0xC8,0x06,0x00,0x00,0x00,0x08,0x00,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin13[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0xB2,0x5E,0x03,0x00,0x48,0x8D,0x0D,0x53,0x10,0x37,0x00,0xC7,0x83,0x48,
    0x0D,0x00,0x00,0x00,0x00,0x00,0x00,0xC7,0x83,0x4C,0x0D,0x00,0x00,0x00,0x00,0xC8,0x42,0xE8,0xD2,0x19,0x0D,0x00,0x89,0x03,
    0x48,0x8B,0xCB,0x83,0x8B,0xC8,0x06,0x00,0x00,0x10,0xE8,0xA1,0xA4,0xFD,0xFF,0x83,0xF8,0x02,0x48,0x8B,0xC3,0x75,0x0A,0x81,
    0x8B,0x10,0x0D,0x00,0x00,0x00,0x80,0x00,0x00,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin14[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x30,0x4D,0x8B,0xC8,0xF3,0x0F,0x11,0x5C,0x24,0x20,0x41,0xB8,0x03,0x00,0x00,
    0x00,0x48,0x8B,0xFA,0x48,0x8B,0xD9,0xE8,0x1C,0xF2,0xFF,0xFF,0x48,0x8D,0x0D,0x6D,0x06,0x34,0x00,0xE8,0x10,0xBB,0x09,0x00,
    0x89,0x03,0x48,0x8B,0xCB,0x81,0x8B,0xC8,0x06,0x00,0x00,0x00,0x00,0x02,0x00,0xE8,0x1C,0x93,0xFA,0xFF,0x84,0xC0,0x75,0x0A,
    0xC7,0x83,0xDC,0x04,0x00,0x00,0x02,0x00,0x00,0x00,0x0F,0xB7,0x47,0x48,0xC1,0xE8,0x03,0x83,0xE0,0x01,0x8D,0x0C,0xC5,0x08,
    0x00,0x00,0x00,0x8B,0x83,0x40,0x07,0x00,0x00,0x83,0xE0,0x87,0x0B,0xC8,0x48,0x8B,0xC3,0x89,0x8B,0x40,0x07,0x00,0x00,0x48,
    0x8B,0x5C,0x24,0x40,0x48,0x83,0xC4,0x30,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin15[]={
    0x40,0x53,0x48,0x83,0xEC,0x30,0xF3,0x0F,0x10,0x44,0x24,0x60,0x48,0x8B,0xD9,0xF3,0x0F,0x11,0x44,0x24,0x20,0xE8,0x46,0x24,
    0xFC,0xFF,0x33,0xC9,0xE8,0xDF,0xC8,0x09,0x00,0x33,0xC9,0x89,0x83,0x38,0x0D,0x00,0x00,0xE8,0xD2,0xC8,0x09,0x00,0x48,0x8D,
    0x0D,0x43,0x13,0x34,0x00,0x89,0x83,0x40,0x0D,0x00,0x00,0xE8,0xC0,0xC8,0x09,0x00,0x89,0x03,0x48,0x8B,0xC3,0x83,0x8B,0xCC,
    0x06,0x00,0x00,0x02,0x48,0x83,0xC4,0x30,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin16[]={
    0x40,0x53,0x48,0x83,0xEC,0x30,0xF3,0x0F,0x10,0x44,0x24,0x60,0x48,0x8B,0xD9,0xF3,0x0F,0x11,0x44,0x24,0x20,0xE8,0x86,0x7B,
    0x00,0x00,0x48,0x8D,0x8B,0xF8,0x0B,0x00,0x00,0xE8,0x8A,0xAC,0xFE,0xFF,0x48,0x8D,0x8B,0x10,0x0C,0x00,0x00,0x48,0x8B,0xD3,
    0xE8,0x6B,0xF6,0xFF,0xFF,0x33,0xC0,0xC7,0x83,0xF4,0x0C,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x89,0x83,0xF0,0x0C,0x00,0x00,0x33,
    0xC9,0x48,0x89,0x83,0xF8,0x0C,0x00,0x00,0x48,0x89,0x83,0x10,0x0D,0x00,0x00,0x48,0x89,0x83,0x18,0x0D,0x00,0x00,0x48,0x89,
    0x83,0x20,0x0D,0x00,0x00,0x48,0x89,0x83,0x28,0x0D,0x00,0x00,0x89,0x83,0x30,0x0D,0x00,0x00,0xE8,0x29,0xA4,0x0D,0x00,0x48,
    0x8D,0x0D,0x62,0x95,0x37,0x00,0x89,0x83,0x34,0x0D,0x00,0x00,0xE8,0x17,0xA4,0x0D,0x00,0x89,0x03,0x48,0x8B,0xC3,0x83,0x8B,
    0xC8,0x06,0x00,0x00,0x02,0x48,0x83,0xC4,0x30,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin17[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x54,0x41,0x56,
    0x41,0x57,0x48,0x83,0xEC,0x30,0xF3,0x0F,0x10,0x44,0x24,0x70,0x4C,0x8B,0xFA,0xF3,0x0F,0x11,0x44,0x24,0x28,0x48,0x8B,0xF9,
    0x4C,0x89,0x4C,0x24,0x20,0x45,0x8B,0xC8,0x4C,0x8B,0xC2,0x8B,0x12,0xE8,0x7E,0x88,0xFD,0xFF,0x33,0xC9,0xE8,0xB7,0x28,0x0D,
    0x00,0x33,0xC9,0x89,0x87,0xB0,0x0B,0x00,0x00,0xE8,0xAA,0x28,0x0D,0x00,0x4C,0x8D,0xB7,0xB8,0x0B,0x00,0x00,0x89,0x87,0xB4,
    0x0B,0x00,0x00,0x33,0xED,0x41,0xC7,0x46,0x0C,0xFF,0xFF,0xFF,0xFF,0x4C,0x8D,0x25,0x2C,0xC1,0x1E,0x00,0x41,0x89,0x6E,0x08,
    0x49,0x89,0x6E,0x10,0x48,0x8D,0x9F,0xE0,0x0B,0x00,0x00,0x41,0xC7,0x46,0x18,0x00,0x18,0x00,0x00,0x8D,0x75,0x02,0x4D,0x89,
    0x26,0x48,0x8B,0xCB,0xE8,0x17,0x3C,0xFF,0xFF,0x48,0x83,0xC3,0x0C,0x48,0x83,0xEE,0x01,0x75,0xEE,0x48,0x8D,0x0D,0xDE,0x1D,
    0x37,0x00,0xE8,0x51,0x28,0x0D,0x00,0x89,0x07,0x83,0x8F,0xC8,0x06,0x00,0x00,0x01,0x48,0x8B,0x8F,0x28,0x09,0x00,0x00,0x48,
    0x85,0xC9,0x0F,0x84,0x47,0x01,0x00,0x00,0x45,0x33,0xC0,0x8D,0x56,0x12,0xE8,0x0D,0xBA,0xFC,0xFF,0x48,0x8B,0xF0,0x48,0x85,
    0xC0,0x74,0x5F,0x8B,0x48,0x08,0xE8,0x4D,0x28,0x0D,0x00,0x48,0x85,0xC0,0x74,0x52,0x48,0x8B,0xCD,0x4C,0x8D,0x05,0x36,0xC4,
    0x1E,0x00,0x0F,0x1F,0x40,0x00,0x66,0x66,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x41,0x0F,0xB6,0x44,0x0F,0x08,0x48,0xFF,
    0xC1,0x41,0x3A,0x44,0x08,0xFF,0x75,0x0B,0x48,0x83,0xF9,0x08,0x75,0xEA,0x41,0x3A,0x44,0x08,0xFF,0x8B,0x4E,0x08,0x8B,0xDD,
    0x0F,0x94,0xC3,0xE8,0x08,0x28,0x0D,0x00,0x48,0x8B,0xD0,0x48,0x8D,0x8F,0x80,0x00,0x00,0x00,0x44,0x8B,0xC3,0xE8,0xA6,0xB8,
    0xEE,0xFF,0x48,0x8B,0x8F,0x28,0x09,0x00,0x00,0x45,0x33,0xC0,0x41,0x8D,0x50,0x22,0xE8,0x93,0xB9,0xFC,0xFF,0x48,0x8B,0xD8,
    0x48,0x85,0xC0,0x74,0x46,0x8B,0x48,0x08,0xE8,0xD3,0x27,0x0D,0x00,0x48,0x85,0xC0,0x74,0x39,0x8B,0x4B,0x08,0xE8,0xC6,0x27,
    0x0D,0x00,0x49,0x8B,0xCE,0x41,0xC7,0x46,0x0C,0xFF,0xFF,0xFF,0xFF,0x49,0x89,0x6E,0x10,0x41,0xC7,0x46,0x18,0x00,0x18,0x00,
    0x00,0x4D,0x89,0x26,0x49,0x89,0x46,0x20,0x41,0xC7,0x46,0x08,0x02,0x00,0x00,0x00,0xE8,0x6B,0x27,0x0D,0x00,0x89,0x87,0xB4,
    0x0B,0x00,0x00,0x48,0x8B,0x8F,0x28,0x09,0x00,0x00,0xE8,0x79,0x2F,0xFC,0xFF,0x48,0x8B,0xC8,0xE8,0x51,0x27,0x0D,0x00,0x48,
    0x8B,0x8F,0x28,0x09,0x00,0x00,0x45,0x33,0xC0,0x89,0x87,0xB0,0x0B,0x00,0x00,0x41,0x8D,0x50,0x26,0xE8,0x18,0xB9,0xFC,0xFF,
    0x48,0x8B,0xF0,0x48,0x85,0xC0,0x74,0x3F,0x8B,0x48,0x08,0xE8,0x58,0x27,0x0D,0x00,0x48,0x85,0xC0,0x74,0x32,0x41,0x0F,0xB6,
    0x5F,0x48,0x8B,0x4E,0x08,0xC0,0xEB,0x06,0x80,0xE3,0x01,0xE8,0x40,0x27,0x0D,0x00,0x48,0x8B,0xD0,0x48,0x8D,0x8F,0xC0,0x09,
    0x00,0x00,0x44,0x0F,0xB6,0xC3,0xE8,0x6D,0xF7,0x02,0x00,0x8B,0x87,0xD4,0x09,0x00,0x00,0x89,0x87,0x50,0x07,0x00,0x00,0x48,
    0x8B,0x97,0x30,0x09,0x00,0x00,0x48,0x85,0xD2,0x74,0x0C,0x48,0x8D,0x8F,0x58,0x01,0x00,0x00,0xE8,0xF9,0xD8,0xFE,0xFF,0x8B,
    0x4F,0x08,0xE8,0x01,0x27,0x0D,0x00,0x66,0x39,0x68,0x4E,0x74,0x18,0x8B,0x4F,0x08,0xE8,0xF3,0x26,0x0D,0x00,0x48,0x8D,0x8F,
    0x28,0x01,0x00,0x00,0x0F,0xB7,0x50,0x4E,0xE8,0xF3,0xF8,0x02,0x00,0x48,0x8B,0x5C,0x24,0x50,0x48,0x8B,0xC7,0x48,0x8B,0x7C,
    0x24,0x68,0x48,0x8B,0x6C,0x24,0x58,0x48,0x8B,0x74,0x24,0x60,0x48,0x83,0xC4,0x30,0x41,0x5F,0x41,0x5E,0x41,0x5C,0xC3,
};
constexpr std::uint8_t kChildPin18[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x89,0x54,0x24,0x10,0x57,0x41,0x54,0x41,0x55,
    0x41,0x56,0x41,0x57,0x48,0x83,0xEC,0x40,0x48,0x8B,0xF1,0x0F,0x29,0x74,0x24,0x30,0x33,0xC9,0x45,0x8B,0xE1,0x49,0x8B,0xE8,
    0xE8,0x0B,0xA0,0x0F,0x00,0x48,0x8B,0xCE,0x89,0x06,0xE8,0x01,0xA0,0x0F,0x00,0x48,0x8B,0xCD,0x89,0x46,0x04,0xE8,0xF6,0x9F,
    0x0F,0x00,0x89,0x46,0x08,0x48,0x8D,0x4E,0x40,0x45,0x33,0xF6,0x4C,0x89,0x76,0x10,0x4C,0x89,0x76,0x18,0x4C,0x89,0x76,0x20,
    0x4C,0x89,0x76,0x28,0xC7,0x46,0x30,0x00,0x00,0x80,0x3F,0xC7,0x46,0x34,0x00,0x00,0x80,0x3F,0xC7,0x46,0x38,0x00,0x00,0x80,
    0x3F,0xC7,0x46,0x3C,0x00,0x00,0x80,0x3F,0xE8,0xFB,0xEF,0xD8,0xFF,0x48,0x8D,0x8E,0x80,0x00,0x00,0x00,0xE8,0xBF,0x0D,0xF1,
    0xFF,0x48,0x8D,0x8E,0xB0,0x00,0x00,0x00,0xE8,0xB3,0x0D,0xF1,0xFF,0x4C,0x89,0xB6,0xE0,0x00,0x00,0x00,0x48,0x8D,0x8E,0x28,
    0x01,0x00,0x00,0x4C,0x89,0xB6,0xE8,0x00,0x00,0x00,0x4C,0x89,0xB6,0xF0,0x00,0x00,0x00,0x4C,0x89,0xB6,0xF8,0x00,0x00,0x00,
    0x44,0x89,0xB6,0x00,0x01,0x00,0x00,0xC7,0x86,0x18,0x01,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x48,0xC7,0x86,0x1C,0x01,0x00,0x00,
    0x05,0x00,0x00,0x00,0x44,0x89,0xB6,0x24,0x01,0x00,0x00,0xE8,0x58,0x71,0x05,0x00,0x48,0x8D,0x8E,0x58,0x01,0x00,0x00,0x48,
    0x8B,0xD6,0xE8,0xB9,0x2E,0x01,0x00,0x41,0x8D,0x4E,0x10,0x48,0x8D,0x86,0x98,0x03,0x00,0x00,0x0F,0x1F,0x40,0x00,0x66,0x66,
    0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x4C,0x89,0x30,0x44,0x89,0x70,0x08,0x48,0x8D,0x40,0x10,0x48,0x83,0xE9,0x01,0x75,
    0xEF,0x4C,0x89,0xB6,0x90,0x03,0x00,0x00,0x48,0x8D,0x8E,0xE0,0x04,0x00,0x00,0x4C,0x89,0xB6,0x98,0x04,0x00,0x00,0x4C,0x89,
    0xB6,0xA0,0x04,0x00,0x00,0x4C,0x89,0xB6,0xA8,0x04,0x00,0x00,0x4C,0x89,0xB6,0xB0,0x04,0x00,0x00,0x4C,0x89,0xB6,0xB8,0x04,
    0x00,0x00,0x4C,0x89,0xB6,0xC0,0x04,0x00,0x00,0xC7,0x86,0xD8,0x04,0x00,0x00,0x80,0x80,0x80,0x80,0x44,0x89,0xB6,0xDC,0x04,
    0x00,0x00,0xE8,0x71,0x71,0x05,0x00,0x48,0x8D,0x8E,0x40,0x05,0x00,0x00,0xE8,0x65,0x71,0x05,0x00,0x4C,0x89,0xB6,0xA0,0x05,
    0x00,0x00,0x4C,0x8D,0xAE,0xD0,0x05,0x00,0x00,0x4C,0x89,0xB6,0xA8,0x05,0x00,0x00,0x33,0xC9,0x4C,0x89,0xB6,0xB0,0x05,0x00,
    0x00,0x44,0x89,0xB6,0xB8,0x05,0x00,0x00,0x4C,0x89,0xB6,0xC0,0x05,0x00,0x00,0x4D,0x89,0x75,0x00,0x4D,0x89,0x75,0x08,0x41,
    0xC7,0x45,0x10,0xFF,0xFF,0xFF,0xFF,0x4D,0x89,0x75,0x14,0x4D,0x89,0x75,0x20,0x45,0x89,0x75,0x1C,0x41,0xC7,0x45,0x28,0x00,
    0x00,0x80,0x3F,0x4D,0x89,0x75,0x3C,0x45,0x89,0x75,0x38,0x41,0xC7,0x45,0x44,0x00,0x00,0x80,0x3F,0x44,0x89,0xB6,0x20,0x06,
    0x00,0x00,0x4C,0x89,0xB6,0x28,0x06,0x00,0x00,0x4C,0x89,0xB6,0x30,0x06,0x00,0x00,0x44,0x89,0xB6,0x38,0x06,0x00,0x00,0x4C,
    0x89,0xB6,0x40,0x06,0x00,0x00,0x44,0x88,0xB6,0x48,0x06,0x00,0x00,0x66,0x44,0x89,0xB6,0x4C,0x06,0x00,0x00,0x4C,0x89,0xB6,
    0x50,0x06,0x00,0x00,0x4C,0x89,0xB6,0x58,0x06,0x00,0x00,0x4C,0x89,0xB6,0x60,0x06,0x00,0x00,0x4C,0x89,0xB6,0x68,0x06,0x00,
    0x00,0x4C,0x89,0xB6,0x74,0x06,0x00,0x00,0x44,0x89,0xB6,0x70,0x06,0x00,0x00,0x48,0xC7,0x86,0x7C,0x06,0x00,0x00,0x00,0x00,
    0x80,0x3F,0x4C,0x89,0xB6,0x84,0x06,0x00,0x00,0x48,0xC7,0x86,0x8C,0x06,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0x94,
    0x06,0x00,0x00,0x44,0x89,0xB6,0x9C,0x06,0x00,0x00,0xE8,0xD9,0x9D,0x0F,0x00,0x89,0x86,0xA0,0x06,0x00,0x00,0x41,0xB8,0xFF,
    0xFF,0x00,0x00,0x66,0x44,0x89,0xB6,0xA6,0x06,0x00,0x00,0xC7,0x86,0xA8,0x06,0x00,0x00,0x03,0x00,0x00,0x00,0x4C,0x89,0xB6,
    0xB0,0x06,0x00,0x00,0x4C,0x89,0xB6,0xC8,0x06,0x00,0x00,0x4C,0x89,0xB6,0xD4,0x06,0x00,0x00,0x4C,0x89,0xB6,0xD0,0x06,0x00,
    0x00,0x48,0xC7,0x86,0xDC,0x06,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0xE4,0x06,0x00,0x00,0x48,0xC7,0x86,0xEC,0x06,
    0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0xF4,0x06,0x00,0x00,0x48,0xC7,0x86,0xFC,0x06,0x00,0x00,0x00,0x00,0x80,0x3F,
    0x4C,0x89,0xB6,0x04,0x07,0x00,0x00,0x48,0xC7,0x86,0x0C,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0x14,0x07,0x00,
    0x00,0x48,0xC7,0x86,0x1C,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0x24,0x07,0x00,0x00,0xC7,0x86,0x2C,0x07,0x00,
    0x00,0x00,0x00,0x80,0x3F,0xC7,0x86,0x30,0x07,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x66,0x44,0x89,0x86,0x34,0x07,0x00,0x00,0x4C,
    0x89,0xB6,0x38,0x07,0x00,0x00,0x83,0xA6,0x40,0x07,0x00,0x00,0xB4,0x83,0x8E,0x40,0x07,0x00,0x00,0x34,0x8B,0x86,0x44,0x07,
    0x00,0x00,0x83,0xE0,0xFA,0x83,0xC8,0x02,0x89,0x86,0x44,0x07,0x00,0x00,0x4C,0x89,0xB6,0x48,0x07,0x00,0x00,0x4C,0x89,0xB6,
    0x50,0x07,0x00,0x00,0x44,0x89,0xB6,0xD8,0x06,0x00,0x00,0x44,0x89,0xB6,0xD0,0x06,0x00,0x00,0x48,0xC7,0x86,0xDC,0x06,0x00,
    0x00,0x00,0x00,0x80,0x3F,0x48,0x8D,0x96,0x58,0x07,0x00,0x00,0x4C,0x89,0xB6,0xE4,0x06,0x00,0x00,0x48,0x8D,0x4A,0x10,0x48,
    0xC7,0x86,0xEC,0x06,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0xF4,0x06,0x00,0x00,0x48,0xC7,0x86,0xFC,0x06,0x00,0x00,
    0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0x04,0x07,0x00,0x00,0x48,0xC7,0x86,0x0C,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,
    0xB6,0x14,0x07,0x00,0x00,0x48,0xC7,0x86,0x1C,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0xB6,0x24,0x07,0x00,0x00,0xC7,
    0x86,0x2C,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0x4C,0x89,0x71,0x04,0x44,0x89,0x31,0xC7,0x41,0x0C,0x00,0x00,0x80,0x3F,0x4C,
    0x89,0x42,0x24,0x4C,0x89,0x72,0x2C,0x44,0x88,0x72,0x34,0x4C,0x89,0x72,0x04,0x44,0x89,0x32,0xC7,0x42,0x0C,0x00,0x00,0x80,
    0x3F,0xE8,0x6A,0x59,0xDF,0xFF,0x8B,0x86,0x90,0x07,0x00,0x00,0x48,0x8D,0x8E,0xB8,0x07,0x00,0x00,0x83,0xE0,0xFA,0x83,0xC8,
    0x02,0x89,0x86,0x90,0x07,0x00,0x00,0x4C,0x89,0xB6,0xB0,0x07,0x00,0x00,0xE8,0x45,0xAA,0xDE,0xFF,0x48,0x8D,0x8E,0x08,0x08,
    0x00,0x00,0xE8,0x59,0xEC,0xD8,0xFF,0x48,0x8D,0x8E,0x48,0x08,0x00,0x00,0xE8,0x4D,0xEC,0xD8,0xFF,0x48,0x8D,0x8E,0x98,0x08,
    0x00,0x00,0x4C,0x89,0xB6,0x8C,0x08,0x00,0x00,0x44,0x89,0xB6,0x88,0x08,0x00,0x00,0xC7,0x86,0x94,0x08,0x00,0x00,0x00,0x00,
    0x80,0x3F,0xE8,0x19,0x0C,0xDD,0xFF,0x4C,0x89,0xB6,0xC0,0x08,0x00,0x00,0x48,0x8D,0x8E,0x08,0x08,0x00,0x00,0x4C,0x89,0xB6,
    0xB8,0x08,0x00,0x00,0x44,0x89,0xB6,0xC8,0x08,0x00,0x00,0x4C,0x89,0xB6,0xD0,0x08,0x00,0x00,0x4C,0x89,0xB6,0xD8,0x08,0x00,
    0x00,0xF3,0x0F,0x10,0x05,0xF3,0xEF,0x1F,0x00,0xF3,0x0F,0x11,0x86,0xE0,0x08,0x00,0x00,0xF3,0x0F,0x10,0x0D,0xDF,0xEF,0x1F,
    0x00,0xF3,0x0F,0x11,0x8E,0xE4,0x08,0x00,0x00,0x66,0x44,0x89,0xB6,0xE9,0x08,0x00,0x00,0x4C,0x89,0xB6,0xF0,0x08,0x00,0x00,
    0x4C,0x89,0xB6,0xF8,0x08,0x00,0x00,0x44,0x89,0xB6,0x00,0x09,0x00,0x00,0x66,0x44,0x89,0xB6,0x06,0x09,0x00,0x00,0x4C,0x89,
    0xB6,0x08,0x09,0x00,0x00,0x44,0x88,0xB6,0xEB,0x08,0x00,0x00,0xC7,0x86,0xB0,0x08,0x00,0x00,0x80,0x80,0x80,0x80,0xE8,0x9D,
    0xEB,0xD8,0xFF,0x48,0x8D,0x8E,0x48,0x08,0x00,0x00,0xE8,0x91,0xEB,0xD8,0xFF,0x48,0x8D,0x8E,0x10,0x09,0x00,0x00,0xC7,0x86,
    0xC8,0x08,0x00,0x00,0x0C,0x08,0x00,0x00,0xE8,0xBB,0x2A,0x03,0x00,0x8B,0x54,0x24,0x78,0x48,0x8D,0x8E,0x10,0x09,0x00,0x00,
    0x4C,0x8B,0xC5,0xE8,0xB8,0x2C,0x03,0x00,0x33,0xFF,0x33,0xC9,0x89,0xBE,0xB8,0x09,0x00,0x00,0x40,0x88,0xBE,0xBC,0x09,0x00,
    0x00,0x48,0x89,0xBE,0xC0,0x09,0x00,0x00,0x48,0x89,0xBE,0xC8,0x09,0x00,0x00,0x48,0x89,0xBE,0xD0,0x09,0x00,0x00,0xE8,0xFD,
    0x9A,0x0F,0x00,0x89,0x86,0xD8,0x09,0x00,0x00,0x48,0x89,0xBE,0xE8,0x09,0x00,0x00,0x48,0x89,0xBE,0xF0,0x09,0x00,0x00,0x48,
    0x89,0xBE,0x00,0x0A,0x00,0x00,0x48,0xC7,0x86,0x08,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x48,0xC7,0x86,0x10,0x0A,0x00,0x00,
    0x00,0x00,0x80,0x3F,0x48,0x89,0xBE,0x1C,0x0A,0x00,0x00,0x89,0xBE,0x18,0x0A,0x00,0x00,0x48,0xC7,0x86,0x24,0x0A,0x00,0x00,
    0x00,0x00,0x80,0x3F,0x48,0x89,0xBE,0x2C,0x0A,0x00,0x00,0x48,0xC7,0x86,0x34,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x48,0x89,
    0xBE,0x3C,0x0A,0x00,0x00,0x48,0x89,0xBE,0x44,0x0A,0x00,0x00,0x48,0x89,0xBE,0x4C,0x0A,0x00,0x00,0x48,0x89,0xBE,0x54,0x0A,
    0x00,0x00,0x48,0x89,0xBE,0x5C,0x0A,0x00,0x00,0x48,0x89,0xBE,0x64,0x0A,0x00,0x00,0x48,0x89,0xBE,0x6C,0x0A,0x00,0x00,0x48,
    0xC7,0x86,0x74,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x48,0x89,0xBE,0x7C,0x0A,0x00,0x00,0x89,0xBE,0x84,0x0A,0x00,0x00,0xC7,
    0x86,0x98,0x0A,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0x9C,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x48,0x89,0xBE,0xA4,
    0x0A,0x00,0x00,0x48,0xC7,0x86,0xAC,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x48,0x89,0xBE,0xB8,0x0A,0x00,0x00,0x66,0xC7,0x86,
    0xC0,0x0A,0x00,0x00,0x01,0x00,0x89,0xBE,0xC4,0x0A,0x00,0x00,0xC7,0x86,0xC8,0x0A,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,
    0x86,0xCC,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x89,0xBE,0xD4,0x0A,0x00,0x00,0xC7,0x86,0xD8,0x0A,0x00,0x00,0x80,0x80,0x80,
    0x80,0x48,0xC7,0x86,0xDC,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x89,0xBE,0xE4,0x0A,0x00,0x00,0xC7,0x86,0xE8,0x0A,0x00,0x00,
    0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0xEC,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x89,0xBE,0xF4,0x0A,0x00,0x00,0xC7,0x86,0xF8,
    0x0A,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0xFC,0x0A,0x00,0x00,0x00,0x00,0x80,0x3F,0x89,0xBE,0x04,0x0B,0x00,0x00,
    0xC7,0x86,0x08,0x0B,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0x0C,0x0B,0x00,0x00,0x00,0x00,0x80,0x3F,0x89,0xBE,0x14,
    0x0B,0x00,0x00,0xC7,0x86,0x18,0x0B,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0x1C,0x0B,0x00,0x00,0x00,0x00,0x80,0x3F,
    0x89,0xBE,0x24,0x0B,0x00,0x00,0xC7,0x86,0x28,0x0B,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0x2C,0x0B,0x00,0x00,0x00,
    0x00,0x80,0x3F,0x89,0xBE,0x34,0x0B,0x00,0x00,0xC7,0x86,0x38,0x0B,0x00,0x00,0x80,0x80,0x80,0x80,0x48,0xC7,0x86,0x3C,0x0B,
    0x00,0x00,0x00,0x00,0x80,0x3F,0xC7,0x86,0x8C,0x0B,0x00,0x00,0x00,0x00,0x80,0x42,0xE8,0xDB,0x22,0xFF,0xFF,0x84,0xC0,0x75,
    0x13,0x8B,0x05,0xF1,0xDA,0x65,0x02,0xC1,0xE8,0x04,0xA8,0x01,0x75,0x06,0x0F,0xB6,0x45,0x56,0xEB,0x02,0x32,0xC0,0x88,0x86,
    0x90,0x0B,0x00,0x00,0x48,0x8D,0x0D,0x4D,0x76,0x39,0x00,0x44,0x88,0xA6,0x91,0x0B,0x00,0x00,0x48,0x89,0xBE,0x94,0x0B,0x00,
    0x00,0x48,0x89,0xBE,0x9C,0x0B,0x00,0x00,0x48,0x89,0xBE,0xA4,0x0B,0x00,0x00,0xE8,0xF4,0x98,0x0F,0x00,0x48,0x8B,0xCE,0x89,
    0x06,0xE8,0x6A,0xC2,0x00,0x00,0x33,0xC9,0xE8,0xC3,0x63,0x05,0x00,0x48,0x8B,0xD6,0x48,0x8B,0xC8,0x48,0x8B,0xD8,0x4C,0x8B,
    0x00,0x41,0xFF,0x50,0x08,0x48,0x8B,0xCB,0xE8,0xCB,0x98,0x0F,0x00,0x48,0x8D,0x8E,0x10,0x09,0x00,0x00,0x89,0x46,0x0C,0x89,
    0xBE,0xA8,0x0B,0x00,0x00,0xE8,0x56,0x29,0x03,0x00,0x48,0x89,0x86,0x88,0x0A,0x00,0x00,0x48,0x8B,0xF8,0x48,0x85,0xC0,0x0F,
    0x84,0x94,0x00,0x00,0x00,0x48,0x8B,0x18,0xE8,0xEB,0xEB,0xD9,0xFF,0x48,0x8B,0xD0,0x48,0x8B,0xCF,0xFF,0x53,0x40,0x48,0x8D,
    0x8E,0x10,0x09,0x00,0x00,0x48,0x89,0x86,0xB0,0x07,0x00,0x00,0xE8,0x4F,0x29,0x03,0x00,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x74,
    0x1D,0x48,0x8B,0x8E,0xB0,0x07,0x00,0x00,0x48,0x8B,0xD0,0xE8,0xD8,0x11,0xDF,0xFF,0x48,0x89,0x9E,0xE0,0x07,0x00,0x00,0x83,
    0x8E,0xB8,0x09,0x00,0x00,0x02,0x48,0x8B,0x8E,0xB0,0x07,0x00,0x00,0xE8,0x3E,0x14,0xDF,0xFF,0x48,0x8B,0xCE,0xC7,0x86,0xE0,
    0x08,0x00,0x00,0x00,0x00,0x96,0x43,0xC7,0x86,0xE4,0x08,0x00,0x00,0x00,0x00,0xFA,0x44,0xE8,0x12,0x9E,0x01,0x00,0xF6,0x45,
    0x48,0x10,0x74,0x07,0x83,0x8E,0xC8,0x08,0x00,0x00,0x20,0x8B,0x4E,0x08,0xE8,0x4D,0x98,0x0F,0x00,0x33,0xFF,0x0F,0xB6,0x48,
    0x06,0x88,0x8E,0x07,0x09,0x00,0x00,0xEB,0x10,0x33,0xFF,0x48,0x89,0xBE,0xB0,0x07,0x00,0x00,0x83,0xA6,0xC8,0x08,0x00,0x00,
    0xF3,0x45,0x33,0xC0,0x48,0x89,0xBE,0xA0,0x07,0x00,0x00,0x48,0x89,0xBE,0x98,0x07,0x00,0x00,0x48,0x8B,0x8E,0x20,0x09,0x00,
    0x00,0x41,0x8D,0x50,0x17,0xE8,0xBE,0x29,0xFF,0xFF,0x48,0x8B,0xD8,0x48,0x85,0xC0,0x0F,0x84,0x0F,0x01,0x00,0x00,0x8B,0x48,
    0x08,0xE8,0xFA,0x97,0x0F,0x00,0x48,0x85,0xC0,0x0F,0x84,0xFE,0x00,0x00,0x00,0x8B,0x4B,0x08,0xE8,0xE9,0x97,0x0F,0x00,0x45,
    0x33,0xC0,0x48,0x89,0x86,0xB8,0x0A,0x00,0x00,0x48,0x8B,0xC8,0x41,0x8D,0x50,0x11,0xE8,0x23,0x61,0x01,0x00,0x4C,0x8B,0xC0,
    0x48,0x85,0xC0,0x0F,0x84,0x9C,0x00,0x00,0x00,0x0F,0xBF,0x48,0x10,0xBA,0x11,0x00,0x00,0x00,0x66,0x0F,0x6E,0xC1,0x0F,0x5B,
    0xC0,0xF3,0x0F,0x11,0x86,0xF8,0x06,0x00,0x00,0xF3,0x0F,0x11,0x86,0xF0,0x06,0x00,0x00,0x0F,0xBF,0x48,0x12,0x66,0x0F,0x6E,
    0xC1,0x0F,0x5B,0xC0,0xF3,0x0F,0x11,0x86,0xF4,0x06,0x00,0x00,0x0F,0xBF,0x48,0x0E,0x66,0x0F,0x6E,0xC9,0x0F,0x5B,0xC9,0xF3,
    0x0F,0x11,0x8E,0x3C,0x07,0x00,0x00,0x0F,0xB6,0x40,0x04,0xF6,0xD8,0x8B,0x86,0x40,0x07,0x00,0x00,0x1B,0xC9,0x83,0xE0,0xF9,
    0x83,0xE1,0x02,0x0B,0xC8,0x89,0x8E,0x40,0x07,0x00,0x00,0x83,0x8E,0xB8,0x09,0x00,0x00,0x01,0x48,0x8B,0x8E,0xB8,0x0A,0x00,
    0x00,0xE8,0xA2,0x60,0x01,0x00,0x48,0x85,0xC0,0x74,0x22,0x48,0x89,0x86,0x98,0x07,0x00,0x00,0x4C,0x8B,0xC0,0x48,0x8B,0x8E,
    0xB8,0x0A,0x00,0x00,0xBA,0x11,0x00,0x00,0x00,0xE8,0x82,0x60,0x01,0x00,0x48,0x89,0x86,0xA0,0x07,0x00,0x00,0x48,0x8B,0x96,
    0xB8,0x0A,0x00,0x00,0x41,0xB8,0x01,0x00,0x00,0x00,0x48,0x8B,0xCE,0xE8,0x86,0x6E,0x05,0x00,0x48,0x8B,0x96,0xB8,0x0A,0x00,
    0x00,0x41,0xB8,0x02,0x00,0x00,0x00,0x48,0x8B,0xCE,0x48,0x89,0x86,0x00,0x0A,0x00,0x00,0xE8,0x6A,0x6E,0x05,0x00,0x48,0x89,
    0x86,0x40,0x06,0x00,0x00,0x89,0xBE,0x68,0x07,0x00,0x00,0x48,0xC7,0x86,0x6C,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0xC7,0x86,
    0x74,0x07,0x00,0x00,0x00,0x00,0x80,0x3F,0x0F,0xB6,0x45,0x54,0x66,0x0F,0x6E,0xC0,0x0F,0x5B,0xC0,0xE8,0x38,0xE3,0xFE,0xFF,
    0x48,0x8B,0xD6,0x49,0x8B,0xCD,0xE8,0xDD,0x13,0x03,0x00,0x48,0x8B,0x94,0x24,0x90,0x00,0x00,0x00,0x48,0x8D,0x8E,0x68,0x0A,
    0x00,0x00,0x4C,0x89,0xB6,0xF8,0x09,0x00,0x00,0xE8,0x92,0x52,0xDF,0xFF,0xF3,0x0F,0x10,0xB4,0x24,0x98,0x00,0x00,0x00,0x48,
    0x8B,0xCE,0xF3,0x0F,0x11,0xB6,0x74,0x0A,0x00,0x00,0xE8,0x29,0x70,0x00,0x00,0x0F,0x28,0xCE,0x48,0x8D,0x4C,0x24,0x20,0xE8,
    0x7C,0x38,0x00,0x00,0x48,0x8B,0xD0,0x48,0x8D,0x8E,0x80,0x06,0x00,0x00,0xE8,0x5D,0x52,0xDF,0xFF,0x8B,0x86,0x8C,0x06,0x00,
    0x00,0x48,0x8B,0xCE,0x89,0x86,0xA8,0x07,0x00,0x00,0x83,0xA6,0xB8,0x09,0x00,0x00,0xF7,0x81,0x8E,0x20,0x01,0x00,0x00,0x00,
    0x00,0x20,0x00,0xE8,0x48,0xBF,0x00,0x00,0x48,0x8B,0x94,0x24,0x90,0x00,0x00,0x00,0x48,0x8D,0x8E,0x70,0x06,0x00,0x00,0xE8,
    0x24,0x52,0xDF,0xFF,0x48,0x8B,0xCE,0xC7,0x86,0x7C,0x06,0x00,0x00,0x00,0x00,0x80,0x3F,0xE8,0x42,0x8F,0x01,0x00,0x8B,0x86,
    0xC8,0x06,0x00,0x00,0xD1,0xE8,0xA8,0x01,0x74,0x0E,0x4C,0x89,0xB6,0x2C,0x0D,0x00,0x00,0x4C,0x89,0xB6,0x24,0x0D,0x00,0x00,
    0x48,0x8B,0xCE,0xE8,0x20,0x8F,0x01,0x00,0x4C,0x8D,0x8E,0x38,0x09,0x00,0x00,0x48,0x8B,0xD6,0x49,0x89,0x39,0x4C,0x8D,0x86,
    0xB0,0x00,0x00,0x00,0x48,0x89,0xBE,0x40,0x09,0x00,0x00,0x48,0x8D,0x8E,0x10,0x09,0x00,0x00,0x48,0x89,0xBE,0x48,0x09,0x00,
    0x00,0x48,0x89,0xBE,0x50,0x09,0x00,0x00,0x48,0x89,0xBE,0x58,0x09,0x00,0x00,0x48,0x89,0xBE,0x60,0x09,0x00,0x00,0x48,0x89,
    0xBE,0x68,0x09,0x00,0x00,0x48,0x89,0xBE,0x70,0x09,0x00,0x00,0x48,0x89,0xBE,0x78,0x09,0x00,0x00,0x48,0x89,0xBE,0x80,0x09,
    0x00,0x00,0x48,0x89,0xBE,0x88,0x09,0x00,0x00,0x48,0x89,0xBE,0x90,0x09,0x00,0x00,0x48,0x89,0xBE,0x98,0x09,0x00,0x00,0x48,
    0x89,0xBE,0xA0,0x09,0x00,0x00,0x48,0x89,0xBE,0xA8,0x09,0x00,0x00,0x48,0x89,0xBE,0xB0,0x09,0x00,0x00,0xE8,0x77,0x27,0x03,
    0x00,0x0F,0x28,0x74,0x24,0x30,0x4C,0x8D,0x5C,0x24,0x40,0x49,0x8B,0x5B,0x30,0x48,0x8B,0xC6,0x49,0x8B,0x6B,0x40,0x49,0x8B,
    0x73,0x48,0x49,0x8B,0xE3,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin19[]={
    0x48,0x63,0xC1,0x48,0x8D,0x0D,0x06,0x70,0x34,0x00,0x48,0x8B,0x04,0xC1,0xC3,
};
constexpr std::uint8_t kChildPin20[]={
    0xC2,0x00,0x00,
};
constexpr std::uint8_t kChildPin21[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xFA,0x48,0x8B,0xD9,0xBA,0x0C,0x00,0x00,0x00,0x48,0x8B,0xCF,
    0x44,0x8D,0x42,0xF3,0xE8,0x9F,0x06,0xFD,0xFF,0x48,0x85,0xC0,0x74,0x0A,0x48,0x89,0x3B,0x0F,0xB7,0x40,0x06,0x89,0x43,0x10,
    0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin22[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x15,0xF7,0x75,0x65,0x02,0x33,0xFF,0x8B,0xC7,0x48,0x8B,0xD9,
    0x48,0x85,0xC0,0x75,0x05,0x48,0x8B,0xC2,0xEB,0x12,0x8B,0x88,0x90,0x0A,0x00,0x00,0xE8,0x83,0xD6,0x0E,0x00,0x48,0x8B,0x15,
    0xD4,0x75,0x65,0x02,0x48,0x85,0xC0,0x74,0x14,0x0F,0xB6,0x88,0x91,0x0B,0x00,0x00,0x38,0x8B,0x91,0x0B,0x00,0x00,0x76,0x05,
    0x48,0x8B,0xF8,0xEB,0xCB,0x48,0x85,0xFF,0x75,0x4B,0x48,0x85,0xD2,0x75,0x26,0x33,0xC9,0x48,0x89,0x1D,0xB0,0x75,0x65,0x02,
    0x48,0x89,0x1D,0xA1,0x75,0x65,0x02,0xE8,0x14,0xD6,0x0E,0x00,0x89,0x83,0x90,0x0A,0x00,0x00,0x48,0x8B,0x5C,0x24,0x30,0x48,
    0x83,0xC4,0x20,0x5F,0xC3,0x48,0x8B,0xCA,0xE8,0xFB,0xD5,0x0E,0x00,0x89,0x83,0x90,0x0A,0x00,0x00,0x48,0x89,0x1D,0x76,0x75,
    0x65,0x02,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,0x48,0x8B,0x05,0x6C,0x75,0x65,0x02,0x48,0x3B,0xF8,0x75,
    0x39,0x48,0x85,0xC0,0x74,0xA9,0x48,0x8B,0xCB,0xE8,0xCA,0xD5,0x0E,0x00,0x48,0x8B,0x0D,0x53,0x75,0x65,0x02,0x89,0x81,0x90,
    0x0A,0x00,0x00,0x33,0xC9,0xE8,0xB6,0xD5,0x0E,0x00,0x89,0x83,0x90,0x0A,0x00,0x00,0x48,0x89,0x1D,0x39,0x75,0x65,0x02,0x48,
    0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,0x8B,0x8F,0x90,0x0A,0x00,0x00,0xE8,0xC3,0xD5,0x0E,0x00,0x48,0x8B,0xC8,
    0xE8,0x8B,0xD5,0x0E,0x00,0x48,0x8B,0xCB,0x89,0x83,0x90,0x0A,0x00,0x00,0xE8,0x7D,0xD5,0x0E,0x00,0x48,0x8B,0x5C,0x24,0x30,
    0x89,0x87,0x90,0x0A,0x00,0x00,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin23[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0x59,0x08,0x8B,0x09,0xE8,0xF0,0xB7,0x07,0x00,0x8B,0x48,0x04,0xE8,0xE8,0xB7,0x07,0x00,
    0x8B,0xD3,0x48,0x8B,0xC8,0x48,0x83,0xC4,0x20,0x5B,0xE9,0xB9,0xF8,0xFD,0xFF,
};
constexpr std::uint8_t kChildPin24[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x8B,0xDA,0x48,0x8B,0x89,0xC0,0x05,0x00,0x00,0x48,0x85,
    0xC9,0x74,0x05,0xE8,0x70,0xF4,0xFA,0xFF,0x8B,0xCB,0xE8,0x09,0xEF,0xFA,0xFF,0x48,0x89,0x87,0xC0,0x05,0x00,0x00,0x48,0x8B,
    0xD8,0x48,0x85,0xC0,0x74,0x0E,0x48,0x8B,0xCF,0xE8,0xB2,0xBE,0x09,0x00,0x89,0x83,0x68,0x02,0x00,0x00,0x48,0x8B,0x5C,0x24,
    0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin25[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x05,0xC0,0x36,0x66,0x02,0x4C,0x8D,0x05,0xF9,0x6F,0x65,0x02,0xFF,
    0xC8,0x8B,0xF9,0x48,0x63,0xD0,0x89,0x05,0xAC,0x36,0x66,0x02,0x49,0x63,0x84,0x90,0x80,0xC5,0x00,0x00,0x48,0x69,0xC8,0x78,
    0x02,0x00,0x00,0x49,0x03,0xC8,0xE8,0x55,0xFD,0xFF,0xFF,0x8B,0xCF,0x48,0x8B,0xD8,0xE8,0x6B,0xBC,0x02,0x00,0x48,0x8B,0xD0,
    0x48,0x8B,0xCB,0xE8,0x70,0x1F,0x00,0x00,0x48,0x8B,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin26[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x32,0x25,0xFE,0xFF,0x4C,0x8B,0x43,0x08,0x48,0x8D,0x05,0xF7,0x1D,0x3F,
    0x00,0x33,0xC9,0x48,0x89,0x03,0x41,0x39,0x48,0x1C,0x7E,0x27,0x49,0x8D,0x40,0x20,0x8B,0x10,0xF6,0xC2,0x01,0x74,0x06,0x83,
    0x4B,0x18,0x01,0x8B,0x10,0x0F,0xBA,0xE2,0x1D,0x73,0x04,0x83,0x4B,0x18,0x02,0xFF,0xC1,0x48,0x83,0xC0,0x20,0x41,0x3B,0x48,
    0x1C,0x7C,0xDD,0x48,0x8B,0xC3,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin27[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
    0x48,0x83,0xEC,0x30,0x48,0x8B,0x01,0x4C,0x8B,0xEA,0x4C,0x8B,0x79,0x08,0x4C,0x8B,0xF1,0xFF,0x50,0x58,0x4C,0x63,0xE0,0x49,
    0x8B,0xCE,0x49,0x8B,0x06,0x49,0x83,0xC4,0x0F,0x49,0x83,0xE4,0xF0,0xFF,0x50,0x08,0x45,0x33,0xC0,0x8D,0x0C,0x00,0x41,0x0F,
    0xB7,0x47,0x10,0x48,0x83,0xC0,0x13,0x48,0x8D,0x14,0x40,0x49,0x8B,0x45,0x00,0x48,0xC1,0xE2,0x04,0x48,0x03,0xD1,0x49,0x8B,
    0xCD,0x49,0x03,0xD4,0xFF,0x50,0x08,0x48,0x8B,0xC8,0xE8,0x21,0x23,0xFE,0xFF,0x48,0x8B,0xD8,0x33,0xFF,0x48,0x05,0x90,0x03,
    0x00,0x00,0x49,0x8B,0xCE,0x8B,0xF7,0x48,0x89,0x83,0x40,0x03,0x00,0x00,0x49,0x8B,0x06,0xFF,0x50,0x58,0x85,0xC0,0x7E,0x21,
    0x48,0x8B,0x83,0x40,0x03,0x00,0x00,0x48,0x8D,0x7F,0x01,0x49,0x8B,0xCE,0xFF,0xC6,0xC6,0x44,0x07,0xFF,0x01,0x49,0x8B,0x06,
    0xFF,0x50,0x58,0x3B,0xF0,0x7C,0xE1,0x33,0xFF,0x48,0x8B,0x8B,0x40,0x03,0x00,0x00,0x8B,0xEF,0x49,0x03,0xCC,0x48,0x89,0x4B,
    0x30,0x41,0x0F,0xB7,0x57,0x10,0x48,0xC1,0xE2,0x04,0x48,0x03,0xD1,0x48,0x89,0x53,0x38,0x41,0x0F,0xB7,0x47,0x10,0x48,0xC1,
    0xE0,0x04,0x48,0x03,0xC2,0x48,0x89,0x43,0x40,0x41,0x0F,0xB7,0x47,0x10,0x45,0x8B,0x67,0x14,0x66,0x3B,0xF8,0x73,0x5E,0x90,
    0x48,0x8B,0x4B,0x30,0x49,0x8D,0x57,0x10,0x48,0x63,0xF5,0x49,0x03,0xD4,0x48,0x8B,0xFE,0x48,0xC1,0xE6,0x04,0x48,0xC1,0xE7,
    0x06,0x48,0x03,0xCE,0x48,0x03,0xD7,0xE8,0x6C,0x6B,0xFE,0xFF,0x48,0x8B,0x4B,0x38,0x49,0x8D,0x57,0x20,0x49,0x03,0xD4,0x48,
    0x03,0xCE,0x48,0x03,0xD7,0xE8,0x56,0x6B,0xFE,0xFF,0x48,0x8B,0x4B,0x40,0x49,0x8D,0x57,0x30,0x49,0x03,0xD4,0x48,0x03,0xCE,
    0x48,0x03,0xD7,0xE8,0x40,0x6B,0xFE,0xFF,0x41,0x0F,0xB7,0x47,0x10,0xFF,0xC5,0x3B,0xE8,0x7C,0xA5,0x33,0xFF,0x4D,0x8B,0x4D,
    0x00,0x45,0x33,0xC0,0x0F,0xB7,0xC0,0x49,0x8B,0xCD,0x48,0x8D,0x14,0x40,0x48,0xC1,0xE2,0x06,0x41,0xFF,0x51,0x08,0x48,0x89,
    0x43,0x48,0x49,0x8B,0xCE,0x41,0x0F,0xB7,0x57,0x10,0x48,0xC1,0xE2,0x06,0x48,0x03,0xD0,0x48,0x89,0x53,0x50,0x41,0x0F,0xB7,
    0x47,0x10,0x48,0xC1,0xE0,0x06,0x48,0x03,0xC2,0x48,0x89,0x43,0x58,0x4C,0x89,0x6B,0x18,0x41,0x0F,0xB7,0x47,0x10,0x48,0xC1,
    0xE0,0x04,0x48,0x03,0x43,0x40,0x48,0x89,0x83,0x48,0x03,0x00,0x00,0x49,0x8B,0x06,0xFF,0x50,0x58,0x48,0x63,0xC8,0x48,0x8B,
    0x83,0x48,0x03,0x00,0x00,0x48,0x8D,0x0C,0xC8,0x48,0x89,0x8B,0x50,0x03,0x00,0x00,0x49,0x8B,0xCE,0x49,0x8B,0x06,0xFF,0x50,
    0x58,0x4C,0x8B,0x83,0x48,0x03,0x00,0x00,0x48,0x8B,0xD3,0x48,0x63,0xC8,0x89,0x7C,0x24,0x20,0x4C,0x8D,0x0C,0xCD,0x0F,0x00,
    0x00,0x00,0x49,0x8B,0xCE,0x4C,0x03,0x8B,0x50,0x03,0x00,0x00,0x49,0x83,0xE1,0xF0,0xE8,0x7B,0xF7,0x00,0x00,0x4C,0x8B,0x83,
    0x50,0x03,0x00,0x00,0x4C,0x8B,0xC8,0x48,0x8B,0xD3,0xC7,0x44,0x24,0x20,0x01,0x00,0x00,0x00,0x49,0x8B,0xCE,0xE8,0x5E,0xF7,
    0x00,0x00,0x48,0x8B,0xD3,0x49,0x8B,0xCE,0xE8,0x13,0x59,0xFE,0xFF,0x48,0x8B,0x6C,0x24,0x68,0x48,0x8B,0xC3,0x48,0x8B,0x5C,
    0x24,0x60,0x48,0x8B,0x74,0x24,0x70,0x48,0x83,0xC4,0x30,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin28[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x83,0xC1,0x70,0xE8,0xFA,0xDC,0xF9,0xFF,0x48,0x8D,
    0x8F,0xB0,0x00,0x00,0x00,0xE8,0xAE,0x81,0xFF,0xFF,0x48,0x8D,0x8F,0x34,0x02,0x00,0x00,0xE8,0xE2,0xDC,0xF9,0xFF,0x48,0x8D,
    0x8F,0x74,0x02,0x00,0x00,0xE8,0xD6,0xDC,0xF9,0xFF,0x48,0x8D,0x8F,0xB8,0x02,0x00,0x00,0xE8,0xCA,0xDC,0xF9,0xFF,0x48,0x8D,
    0x8F,0xF8,0x02,0x00,0x00,0xE8,0xBE,0xDC,0xF9,0xFF,0x48,0x8B,0x5C,0x24,0x30,0x33,0xC9,0x48,0x89,0x4F,0x18,0x48,0x89,0x4F,
    0x20,0x48,0x89,0x4F,0x28,0x48,0x89,0x4F,0x30,0x48,0x89,0x4F,0x38,0x48,0x89,0x4F,0x40,0x48,0x89,0x4F,0x48,0x48,0x89,0x4F,
    0x50,0x48,0x89,0x4F,0x58,0x48,0x89,0x4F,0x60,0x48,0x89,0x4F,0x68,0x89,0x8F,0x38,0x03,0x00,0x00,0x48,0x89,0x8F,0x40,0x03,
    0x00,0x00,0x48,0x89,0x4F,0x08,0x48,0x89,0x8F,0x80,0x03,0x00,0x00,0x48,0x8B,0x05,0x44,0x32,0x92,0x00,0x48,0x89,0x07,0x48,
    0x8B,0xC7,0x48,0x89,0x3D,0x37,0x32,0x92,0x00,0x48,0x89,0x8F,0x48,0x03,0x00,0x00,0x48,0x89,0x8F,0x50,0x03,0x00,0x00,0x48,
    0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin29[]={
    0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x83,
    0xEC,0x58,0x48,0x8B,0x79,0x08,0x49,0x8B,0xD9,0x48,0x89,0x7C,0x24,0x30,0x49,0x8B,0xF0,0xE8,0x02,0x0B,0xFF,0xFF,0x45,0x33,
    0xE4,0x48,0x89,0x9C,0x24,0xB8,0x00,0x00,0x00,0x4C,0x8B,0xE8,0x48,0x89,0x44,0x24,0x40,0x44,0x89,0xA4,0x24,0xA0,0x00,0x00,
    0x00,0x44,0x39,0x67,0x1C,0x0F,0x8E,0x83,0x01,0x00,0x00,0x4C,0x8B,0xEE,0x48,0x89,0x74,0x24,0x38,0x4C,0x8D,0x7F,0x30,0x90,
    0x49,0x89,0x5D,0x00,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0x41,0x8B,0x1F,0xB9,0x2C,0x03,0x00,0x00,0x45,0x8B,0x77,0x04,
    0x48,0x03,0xDF,0x4C,0x03,0xF7,0x8B,0x43,0x0C,0x44,0x8B,0x43,0x04,0x44,0x0F,0xB7,0x0B,0x4C,0x03,0xC7,0x89,0x44,0x24,0x28,
    0x8B,0x43,0x08,0x89,0x44,0x24,0x20,0xE8,0xD4,0xBC,0xFD,0xFF,0x48,0x83,0xC3,0x10,0x33,0xED,0x41,0x39,0x2E,0x0F,0x8E,0xD1,
    0x00,0x00,0x00,0x4C,0x8B,0x64,0x24,0x30,0x49,0x8D,0x7E,0x04,0x4C,0x8B,0xAC,0x24,0xA8,0x00,0x00,0x00,0x0F,0x1F,0x40,0x00,
    0x48,0x63,0x0F,0x85,0xC9,0x79,0x4A,0x41,0xB9,0x00,0x00,0x00,0x17,0xC7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0x45,0x33,0xC0,
    0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0xB9,0x2D,0x03,0x00,0x00,0xE8,0x76,0xB8,0xFD,0xFF,0x8B,0x4B,0x10,0x48,0x8D,0x43,
    0x14,0x44,0x8B,0x18,0x4C,0x8D,0x43,0x1C,0x48,0x8D,0x53,0x18,0x44,0x0F,0xB7,0xC9,0x41,0x8B,0x08,0x48,0x83,0xC3,0x20,0x4D,
    0x03,0xDC,0xBE,0x2E,0x03,0x00,0x00,0xEB,0x2E,0x48,0x63,0x84,0x24,0xC0,0x00,0x00,0x00,0x4C,0x8D,0x53,0x0C,0x4C,0x8B,0xD9,
    0x48,0x8D,0x53,0x08,0x41,0x8B,0x0A,0x48,0x83,0xC3,0x10,0x49,0xC1,0xE3,0x06,0xBE,0x2F,0x03,0x00,0x00,0x4D,0x03,0x5C,0xC5,
    0x50,0x41,0xB9,0x04,0x00,0x00,0x00,0x8B,0x02,0x4D,0x8B,0xC3,0x89,0x4C,0x24,0x28,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,
    0x8B,0xCE,0x89,0x44,0x24,0x20,0xE8,0x15,0xBC,0xFD,0xFF,0xFF,0xC5,0x48,0x83,0xC7,0x04,0x41,0x3B,0x2E,0x0F,0x8C,0x56,0xFF,
    0xFF,0xFF,0x44,0x8B,0xA4,0x24,0xA0,0x00,0x00,0x00,0x4C,0x8B,0x6C,0x24,0x38,0x48,0x8B,0x7C,0x24,0x30,0x41,0xB9,0x00,0x00,
    0x00,0x17,0xC7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,0x45,0x33,0xC0,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0xB9,0x2D,0x03,
    0x00,0x00,0xE8,0xA1,0xBC,0xFD,0xFF,0x48,0x8B,0x9C,0x24,0xB8,0x00,0x00,0x00,0x41,0xFF,0xC4,0x49,0x83,0xC5,0x08,0x44,0x89,
    0xA4,0x24,0xA0,0x00,0x00,0x00,0x49,0x83,0xC7,0x20,0x4C,0x89,0x6C,0x24,0x38,0x44,0x3B,0x67,0x1C,0x0F,0x8C,0x97,0xFE,0xFF,
    0xFF,0x4C,0x8B,0x6C,0x24,0x40,0x48,0x8B,0xB4,0x24,0xB0,0x00,0x00,0x00,0x4D,0x85,0xED,0x0F,0x84,0x92,0x01,0x00,0x00,0x45,
    0x33,0xFF,0x44,0x89,0xBC,0x24,0xA0,0x00,0x00,0x00,0x45,0x39,0x7D,0x1C,0x0F,0x8E,0x7D,0x01,0x00,0x00,0x4D,0x8D,0x65,0x30,
    0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x8B,0x47,0x1C,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0x41,0x03,0xC7,0xB9,0x30,
    0x03,0x00,0x00,0x48,0x98,0x48,0x89,0x1C,0xC6,0x41,0x8B,0x1C,0x24,0x45,0x8B,0x74,0x24,0x04,0x49,0x03,0xDD,0x8B,0x43,0x0C,
    0x44,0x8B,0x43,0x04,0x44,0x0F,0xB7,0x0B,0x4D,0x03,0xC5,0x89,0x44,0x24,0x28,0x8B,0x43,0x08,0x89,0x44,0x24,0x20,0xE8,0x2D,
    0xBB,0xFD,0xFF,0x48,0x83,0xC3,0x10,0x33,0xED,0x43,0x39,0x2C,0x2E,0x0F,0x8E,0xCF,0x00,0x00,0x00,0x4C,0x8B,0xBC,0x24,0xA8,
    0x00,0x00,0x00,0x49,0x8D,0x7D,0x04,0x49,0x03,0xFE,0x48,0x63,0x0F,0x85,0xC9,0x79,0x4A,0x41,0xB9,0x00,0x00,0x00,0x17,0xC7,
    0x44,0x24,0x20,0x00,0x00,0x00,0x00,0x45,0x33,0xC0,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0xB9,0x2D,0x03,0x00,0x00,0xE8,
    0xD4,0xB6,0xFD,0xFF,0x8B,0x4B,0x10,0x48,0x8D,0x43,0x14,0x44,0x8B,0x18,0x4C,0x8D,0x43,0x1C,0x48,0x8D,0x53,0x18,0x44,0x0F,
    0xB7,0xC9,0x41,0x8B,0x08,0x48,0x83,0xC3,0x20,0x4D,0x03,0xDD,0xBE,0x31,0x03,0x00,0x00,0xEB,0x2E,0x48,0x63,0x84,0x24,0xC0,
    0x00,0x00,0x00,0x4C,0x8D,0x53,0x0C,0x4C,0x8B,0xD9,0x48,0x8D,0x53,0x08,0x41,0x8B,0x0A,0x48,0x83,0xC3,0x10,0x49,0xC1,0xE3,
    0x06,0xBE,0x32,0x03,0x00,0x00,0x4D,0x03,0x5C,0xC7,0x50,0x41,0xB9,0x04,0x00,0x00,0x00,0x8B,0x02,0x4D,0x8B,0xC3,0x89,0x4C,
    0x24,0x28,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0x8B,0xCE,0x89,0x44,0x24,0x20,0xE8,0x73,0xBA,0xFD,0xFF,0xFF,0xC5,0x48,
    0x83,0xC7,0x04,0x43,0x3B,0x2C,0x2E,0x0F,0x8C,0x55,0xFF,0xFF,0xFF,0x44,0x8B,0xBC,0x24,0xA0,0x00,0x00,0x00,0x48,0x8B,0x7C,
    0x24,0x30,0x48,0x8B,0xB4,0x24,0xB0,0x00,0x00,0x00,0x41,0xB9,0x00,0x00,0x00,0x17,0xC7,0x44,0x24,0x20,0x00,0x00,0x00,0x00,
    0x45,0x33,0xC0,0x48,0x8D,0x94,0x24,0xB8,0x00,0x00,0x00,0xB9,0x2D,0x03,0x00,0x00,0xE8,0xFB,0xBA,0xFD,0xFF,0x41,0xFF,0xC7,
    0x49,0x83,0xC4,0x20,0x44,0x89,0xBC,0x24,0xA0,0x00,0x00,0x00,0x45,0x3B,0x7D,0x1C,0x7D,0x0D,0x48,0x8B,0x9C,0x24,0xB8,0x00,
    0x00,0x00,0xE9,0x99,0xFE,0xFF,0xFF,0x48,0x8B,0x84,0x24,0xB8,0x00,0x00,0x00,0xEB,0x03,0x48,0x8B,0xC3,0x48,0x83,0xC4,0x58,
    0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin30[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0xDA,0x48,0x8B,0x0D,0x11,0x4D,0x92,0x00,0xE8,
    0xC4,0x1A,0xF6,0xFF,0x48,0x85,0xDB,0x74,0x55,0x48,0x8B,0x4F,0x58,0x4C,0x8D,0x53,0x0F,0x49,0x83,0xE2,0xF0,0x4D,0x8D,0x42,
    0x30,0x48,0x85,0xC9,0x74,0x18,0x8B,0x01,0x8B,0x51,0x10,0x2B,0xC2,0x2B,0xC1,0x49,0x3B,0xC0,0x72,0x0A,0x48,0x8D,0x04,0x11,
    0x48,0x3B,0x47,0x48,0x72,0x44,0x4C,0x8B,0x4F,0x48,0x48,0x8D,0x4F,0x60,0x49,0x3B,0xC9,0x74,0x1B,0x0F,0x1F,0x44,0x00,0x00,
    0x8B,0x11,0x2B,0x51,0x10,0x2B,0xD1,0x49,0x3B,0xD0,0x74,0x26,0x77,0x24,0x48,0x8B,0x09,0x49,0x3B,0xC9,0x75,0xEA,0x33,0xDB,
    0x48,0x8B,0x0D,0xA9,0x4C,0x92,0x00,0xE8,0x5C,0x19,0xF6,0xFF,0x48,0x8B,0xC3,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,
    0x5F,0xC3,0x48,0x8B,0x01,0x8B,0x59,0x10,0x48,0x03,0xD9,0x48,0x89,0x03,0x48,0x89,0x4B,0x08,0x44,0x89,0x43,0x10,0x48,0x89,
    0x7B,0x18,0x48,0x89,0x19,0x48,0x8B,0x03,0x48,0x3B,0x47,0x48,0x74,0x04,0x48,0x89,0x58,0x08,0x4C,0x01,0x47,0x50,0x48,0xB8,
    0xEF,0xBE,0xAD,0xDE,0xEF,0xBE,0xAD,0xDE,0x48,0x89,0x5F,0x58,0x48,0x83,0xC3,0x20,0x49,0x89,0x04,0x1A,0x48,0xB8,0x21,0x43,
    0x65,0x87,0x67,0x45,0x23,0x01,0x49,0x89,0x44,0x1A,0x08,0xEB,0x93,
};
constexpr std::uint8_t kChildPin31[]={
    0x48,0x8B,0xD1,0x45,0x33,0xC0,0x48,0x8B,0x0D,0xE3,0x84,0x86,0x00,0x48,0x8B,0x01,0x48,0xFF,0x60,
};
constexpr std::uint8_t kChildPin32[]={
    0x85,0xC9,0x75,0x03,0x33,0xC0,0xC3,0xE9,0x74,0x01,0x00,0x00,
};
constexpr std::uint8_t kChildPin33[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x8B,0xD9,0xE8,0xC3,0xFE,0xFF,0xFF,0x0F,0xBA,0xF3,0x1F,0x48,0x8D,0x15,0x18,0x03,0x66,0x02,
    0x8B,0xCB,0x8B,0xC3,0x48,0xC1,0xE8,0x19,0x81,0xE1,0xFF,0xFF,0xFF,0x01,0x48,0x8B,0x04,0xC2,0x48,0x0B,0xC1,0x48,0x83,0xC4,
    0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin34[]={
    0x48,0x83,0xEC,0x38,0x48,0xC7,0x44,0x24,0x20,0xFE,0xFF,0xFF,0xFF,0x8B,0x0D,0x95,0xC8,0x64,0x02,0x65,0x48,0x8B,0x04,0x25,
    0x58,0x00,0x00,0x00,0xBA,0x10,0x00,0x00,0x00,0x48,0x8B,0x0C,0xC8,0x8B,0x04,0x0A,0x39,0x05,0x32,0x06,0x66,0x02,0x7F,0x5B,
    0x48,0x8D,0x05,0x2D,0x06,0x66,0x02,0x48,0x83,0xC4,0x38,0xC3,0x0F,0x1F,0x40,0x00,0x48,0xC7,0x00,0xFF,0xFF,0xFF,0xFF,0x48,
    0xC7,0x40,0x08,0xFF,0xFF,0xFF,0xFF,0x48,0xC7,0x40,0x10,0xFF,0xFF,0xFF,0xFF,0x48,0x8D,0x40,0x40,0x48,0xC7,0x40,0xD8,0xFF,
    0xFF,0xFF,0xFF,0x48,0xC7,0x40,0xE0,0xFF,0xFF,0xFF,0xFF,0x48,0xC7,0x40,0xE8,0xFF,0xFF,0xFF,0xFF,0x48,0xC7,0x40,0xF0,0xFF,
    0xFF,0xFF,0xFF,0x48,0xC7,0x40,0xF8,0xFF,0xFF,0xFF,0xFF,0x48,0x83,0xEA,0x01,0x74,0x25,0xEB,0xB5,0x48,0x8D,0x0D,0xCE,0x05,
    0x66,0x02,0xE8,0xF9,0xCB,0xF8,0xFF,0x83,0x3D,0xC2,0x05,0x66,0x02,0xFF,0x75,0x90,0x48,0x8D,0x05,0xB9,0x03,0x66,0x02,0xBA,
    0x08,0x00,0x00,0x00,0xEB,0x92,0x48,0x8D,0x0D,0xAB,0x05,0x66,0x02,0xE8,0x76,0xCB,0xF8,0xFF,0x48,0x8D,0x05,0xA3,0x05,0x66,
    0x02,0x48,0x83,0xC4,0x38,0xC3,
};
constexpr std::uint8_t kChildPin35[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x59,0x08,0xE8,0x3D,0x0C,0xFF,0xFF,0x4C,0x63,0x43,0x1C,0x33,
    0xC9,0x48,0x8B,0xF8,0x8B,0xD1,0x4D,0x8B,0xD8,0x45,0x85,0xC0,0x7E,0x52,0x4C,0x8D,0x53,0x34,0x66,0x0F,0x1F,0x44,0x00,0x00,
    0x45,0x8B,0x02,0xFF,0xC2,0x4C,0x03,0xC3,0x4D,0x63,0x08,0x4D,0x85,0xC9,0x7E,0x27,0x49,0x83,0xC0,0x04,0x0F,0x1F,0x40,0x00,
    0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,0x41,0x8B,0x00,0x4D,0x8D,0x40,0x04,0xC1,0xF8,0x1F,0xFF,0xC2,0x83,0xE0,0x01,0x03,
    0xD0,0x49,0x83,0xE9,0x01,0x75,0xE9,0x83,0xC2,0x02,0x49,0x83,0xC2,0x20,0x49,0x83,0xEB,0x01,0x75,0xBC,0x44,0x8B,0x43,0x1C,
    0xC1,0xE2,0x04,0x46,0x8D,0x1C,0xC5,0x0F,0x00,0x00,0x00,0x41,0x83,0xE3,0xF0,0x44,0x03,0xDA,0x48,0x85,0xFF,0x74,0x74,0x48,
    0x63,0x47,0x1C,0x4C,0x8B,0xD0,0x85,0xC0,0x7E,0x4C,0x4C,0x8D,0x4F,0x34,0x66,0x90,0x41,0x8B,0x11,0xFF,0xC1,0x48,0x03,0xD7,
    0x4C,0x63,0x02,0x4D,0x85,0xC0,0x7E,0x26,0x48,0x83,0xC2,0x04,0x0F,0x1F,0x40,0x00,0x0F,0x1F,0x84,0x00,0x00,0x00,0x00,0x00,
    0x8B,0x02,0x48,0x8D,0x52,0x04,0xC1,0xF8,0x1F,0xFF,0xC1,0x83,0xE0,0x01,0x03,0xC8,0x49,0x83,0xE8,0x01,0x75,0xEA,0x83,0xC1,
    0x02,0x49,0x83,0xC1,0x20,0x49,0x83,0xEA,0x01,0x75,0xBD,0x8B,0x47,0x1C,0x8D,0x04,0xC5,0x0F,0x00,0x00,0x00,0xC1,0xE1,0x04,
    0x83,0xE0,0xF0,0x41,0x03,0xCB,0x03,0xC1,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,0x48,0x8B,0x5C,0x24,0x30,
    0x41,0x8B,0xC3,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin36[]={
    0x48,0x8B,0x41,0x08,0x8B,0x48,0x0C,0x85,0xC9,0x74,0x05,0x48,0x03,0xC8,0xEB,0x02,0x33,0xC9,0x8B,0x40,0x1C,0x48,0x85,0xC9,
    0x74,0x03,0x03,0x41,0x1C,0xC3,
};
constexpr std::uint8_t kChildPin37[]={
    0x48,0x8B,0x51,0x08,0x8B,0x42,0x0C,0x85,0xC0,0x74,0x04,0x48,0x03,0xC2,0xC3,0x33,0xC0,0xC3,
};
constexpr std::uint8_t kChildPin38[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x02,0x41,0x8B,0xD8,0xC1,0xE1,0x10,0x48,0x8B,0xFA,0x81,0x20,
    0xFF,0xFF,0xFF,0x7F,0x4C,0x8B,0x0A,0x41,0x8B,0x01,0x25,0xFF,0xFF,0xFF,0x9F,0x0F,0xBA,0xE8,0x1C,0x41,0x89,0x01,0x48,0x8B,
    0x02,0x81,0x20,0xFF,0xFF,0xFF,0xF3,0x48,0x8B,0x02,0x33,0x08,0x81,0xE1,0x00,0x00,0xFF,0x03,0x31,0x08,0x33,0xC9,0xE8,0xC5,
    0x00,0x30,0x00,0x48,0x8B,0x0F,0x89,0x41,0x04,0x48,0x8B,0x07,0x66,0x89,0x18,0x8D,0x43,0x01,0x48,0x8B,0x17,0x48,0x8B,0x5C,
    0x24,0x30,0x48,0x63,0xC8,0x48,0xC1,0xE1,0x04,0x48,0x03,0xCA,0x48,0x8D,0x42,0x10,0x48,0x89,0x0F,0x48,0x83,0xC4,0x20,0x5F,
    0xC3,
};
constexpr std::uint8_t kChildPin39[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x02,0x49,0x8B,0xF1,0xC1,0xE1,0x10,
    0x41,0x8B,0xD8,0x48,0x8B,0xFA,0x81,0x20,0xFF,0xFF,0xFF,0x7F,0x4C,0x8B,0x12,0x41,0x8B,0x02,0x0F,0xBA,0xF0,0x1E,0x0D,0x00,
    0x00,0x00,0x30,0x41,0x89,0x02,0x48,0x8B,0x02,0x81,0x20,0xFF,0xFF,0xFF,0xF3,0x48,0x8B,0x02,0x33,0x08,0x81,0xE1,0x00,0x00,
    0xFF,0x03,0x31,0x08,0x49,0x8B,0xC9,0xE8,0x3C,0x00,0x30,0x00,0x48,0x8B,0x0F,0x89,0x41,0x04,0x48,0x8B,0x07,0x66,0x89,0x18,
    0x48,0x8B,0xC6,0x48,0x8B,0x74,0x24,0x38,0x48,0x83,0x07,0x10,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin40[]={
    0x48,0x8B,0x02,0x4D,0x8B,0xD9,0x4D,0x8B,0xC8,0x45,0x8B,0xC3,0x81,0x20,0xFF,0xFF,0x00,0xFC,0x4C,0x8B,0x12,0x8B,0x44,0x24,
    0x28,0x41,0x89,0x42,0x08,0x4C,0x8B,0x12,0x8B,0x44,0x24,0x30,0x41,0x89,0x42,0x0C,0xE9,0xC3,0xF8,0xFF,0xFF,
};
constexpr std::uint8_t kChildPin41[]={
    0x48,0x8B,0x02,0x81,0x20,0xFF,0xFF,0x00,0xFC,0x48,0x8B,0x02,0x44,0x89,0x48,0x08,0x4C,0x8B,0x0A,0x8B,0x44,0x24,0x28,0x41,
    0x89,0x41,0x0C,0xE9,0x60,0xFC,0xFF,0xFF,
};
constexpr std::uint8_t kChildPin42[]={
    0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x02,0x49,0x8B,0xD8,0xC1,0xE1,0x10,0x48,0x8B,0xFA,0x81,0x20,
    0xFF,0xFF,0x00,0xFC,0x48,0x8B,0x02,0x44,0x89,0x48,0x08,0x4C,0x8B,0x0A,0x8B,0x44,0x24,0x50,0x41,0x89,0x41,0x0C,0x48,0x8B,
    0x02,0x81,0x20,0xFF,0xFF,0xFF,0x7F,0x4C,0x8B,0x0A,0x41,0x8B,0x01,0x0F,0xBA,0xF0,0x1C,0x0D,0x00,0x00,0x00,0x60,0x41,0x89,
    0x01,0x48,0x8B,0x02,0x81,0x20,0xFF,0xFF,0xFF,0xF3,0x48,0x8B,0x02,0x33,0x08,0x81,0xE1,0x00,0x00,0xFF,0x03,0x31,0x08,0x33,
    0xC9,0xE8,0x4A,0xF8,0x2F,0x00,0x48,0x8B,0x0F,0x89,0x41,0x04,0x48,0x8B,0x07,0x66,0x89,0x18,0x8D,0x43,0x01,0x48,0x8B,0x17,
    0x48,0x8B,0x5C,0x24,0x30,0x48,0x63,0xC8,0x48,0xC1,0xE1,0x04,0x48,0x03,0xCA,0x48,0x8D,0x42,0x10,0x48,0x89,0x0F,0x48,0x83,
    0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin43[]={
    0x48,0x3B,0xCA,0x74,0x16,0x8B,0x02,0x89,0x01,0x8B,0x42,0x04,0x89,0x41,0x04,0x8B,0x42,0x08,0x89,0x41,0x08,0x8B,0x42,0x0C,
    0x89,0x41,0x0C,0x48,0x8B,0xC1,0xC3,
};
constexpr std::uint8_t kChildPin44[]={
    0x33,0xC0,0x48,0x89,0x4A,0x20,0x48,0x89,0x42,0x10,0x48,0x8B,0x41,0x10,0x48,0x89,0x42,0x08,0x48,0x89,0x51,0x10,0xC3,
};
constexpr std::uint8_t kChildPin45[]={
    0x0F,0x28,0x05,0x29,0x1A,0x4E,0x00,0x0F,0x28,0x0D,0x32,0x1A,0x4E,0x00,0x0F,0x11,0x01,0x0F,0x28,0x05,0x48,0x1A,0x4E,0x00,
    0x0F,0x11,0x49,0x10,0x0F,0x28,0x0D,0x4D,0x1B,0x4E,0x00,0x0F,0x11,0x41,0x20,0x0F,0x11,0x49,0x30,0xC3,
};
constexpr std::uint8_t kChildPin46[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8,0x32,0x5B,0xFA,0xFF,0x48,0x8D,0x8B,0x84,0x00,0x00,0x00,0xE8,0x26,0x5B,
    0xFA,0xFF,0x48,0x8D,0x8B,0xC4,0x00,0x00,0x00,0xE8,0x1A,0x5B,0xFA,0xFF,0x48,0x8D,0x8B,0x04,0x01,0x00,0x00,0xE8,0x0E,0x5B,
    0xFA,0xFF,0x48,0x8D,0x8B,0x44,0x01,0x00,0x00,0xE8,0x02,0x5B,0xFA,0xFF,0x48,0x8B,0xC3,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin47[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x85,0xC9,0x75,0x08,0x33,0xC0,0x48,0x83,0xC4,0x20,0x5B,0xC3,0xE8,0x65,
    0x00,0x00,0x00,0x48,0x8B,0xC8,0x48,0x8B,0xD3,0x48,0x83,0xC4,0x20,0x5B,0xE9,0x25,0x01,0x00,0x00,
};
constexpr std::uint8_t kChildPin48[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x33,0xF6,0x48,0x8D,0x99,0xD0,0x01,0x00,0x00,
    0x48,0x89,0x31,0x48,0x8B,0xF9,0x48,0x89,0x71,0x08,0x48,0x89,0x71,0x10,0x48,0x89,0x71,0x18,0x48,0x89,0x71,0x20,0x48,0x89,
    0x71,0x28,0x48,0x89,0x71,0x30,0x48,0x89,0x71,0x38,0x48,0x89,0x71,0x40,0x48,0x89,0x71,0x48,0x48,0x89,0x71,0x50,0x48,0x89,
    0x71,0x58,0x48,0x89,0x71,0x60,0x48,0x89,0x71,0x68,0x48,0x89,0x71,0x70,0x48,0x89,0x71,0x78,0x48,0x89,0xB1,0x80,0x00,0x00,
    0x00,0x48,0x89,0xB1,0x88,0x00,0x00,0x00,0x48,0x89,0xB1,0x90,0x00,0x00,0x00,0x48,0x89,0xB1,0x98,0x00,0x00,0x00,0x48,0x89,
    0xB1,0xA0,0x00,0x00,0x00,0x48,0x89,0xB1,0xA8,0x00,0x00,0x00,0x48,0x89,0xB1,0xB0,0x00,0x00,0x00,0x48,0x89,0xB1,0xB8,0x00,
    0x00,0x00,0x48,0x89,0xB1,0xC0,0x00,0x00,0x00,0x48,0x89,0xB1,0xC8,0x00,0x00,0x00,0x48,0x89,0xB1,0xD0,0x00,0x00,0x00,0x48,
    0x89,0xB1,0xD8,0x00,0x00,0x00,0x48,0x89,0xB1,0xE0,0x00,0x00,0x00,0x48,0x89,0xB1,0xE8,0x00,0x00,0x00,0x48,0x89,0xB1,0xF0,
    0x00,0x00,0x00,0x48,0x89,0xB1,0xF8,0x00,0x00,0x00,0x48,0x89,0xB1,0x00,0x01,0x00,0x00,0x48,0x89,0xB1,0x08,0x01,0x00,0x00,
    0x48,0x89,0xB1,0x10,0x01,0x00,0x00,0x48,0x89,0xB1,0x18,0x01,0x00,0x00,0x48,0x89,0xB1,0x20,0x01,0x00,0x00,0x48,0x89,0xB1,
    0x28,0x01,0x00,0x00,0x48,0x89,0xB1,0x30,0x01,0x00,0x00,0x48,0x89,0xB1,0x38,0x01,0x00,0x00,0x48,0x89,0xB1,0x40,0x01,0x00,
    0x00,0x48,0x89,0xB1,0x48,0x01,0x00,0x00,0x48,0x89,0xB1,0x50,0x01,0x00,0x00,0x48,0x89,0xB1,0x58,0x01,0x00,0x00,0x48,0x89,
    0xB1,0x60,0x01,0x00,0x00,0x48,0x89,0xB1,0x68,0x01,0x00,0x00,0x48,0x89,0xB1,0x70,0x01,0x00,0x00,0x48,0x89,0xB1,0x78,0x01,
    0x00,0x00,0x48,0x89,0xB1,0x80,0x01,0x00,0x00,0x48,0x89,0xB1,0x88,0x01,0x00,0x00,0x48,0x89,0xB1,0x90,0x01,0x00,0x00,0xC7,
    0x81,0x98,0x01,0x00,0x00,0x9F,0x86,0x01,0x00,0xC7,0x81,0x9C,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0xC7,0x81,0xA0,0x01,0x00,
    0x00,0x9F,0x86,0x01,0x00,0xC7,0x81,0xA4,0x01,0x00,0x00,0x01,0x00,0x00,0x00,0x89,0xB1,0xAF,0x01,0x00,0x00,0x48,0x89,0xB1,
    0xB4,0x01,0x00,0x00,0x48,0x89,0xB1,0xBC,0x01,0x00,0x00,0x48,0x89,0xB1,0xC4,0x01,0x00,0x00,0x33,0xC9,0x48,0x89,0x33,0x48,
    0x89,0x73,0x08,0x48,0x89,0x73,0x10,0x48,0x89,0x73,0x18,0xE8,0x88,0xD0,0x0E,0x00,0x33,0xC9,0x89,0x87,0x50,0x02,0x00,0x00,
    0xE8,0x7B,0xD0,0x0E,0x00,0x89,0x87,0x54,0x02,0x00,0x00,0x33,0xC9,0x40,0x88,0xB7,0x58,0x02,0x00,0x00,0x89,0xB7,0x5C,0x02,
    0x00,0x00,0xC7,0x87,0x60,0x02,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xC7,0x87,0x64,0x02,0x00,0x00,0x01,0x00,0x00,0x00,0xE8,0x4D,
    0xD0,0x0E,0x00,0x33,0xC9,0x89,0x87,0x68,0x02,0x00,0x00,0xE8,0x40,0xD0,0x0E,0x00,0x33,0xC9,0x89,0x87,0x6C,0x02,0x00,0x00,
    0xE8,0x33,0xD0,0x0E,0x00,0x89,0x87,0x70,0x02,0x00,0x00,0x48,0x8B,0xD7,0xC7,0x87,0xA8,0x01,0x00,0x00,0x64,0x64,0x64,0x64,
    0x48,0x8B,0xCB,0x66,0xC7,0x87,0xAC,0x01,0x00,0x00,0x64,0x64,0xC6,0x87,0xAE,0x01,0x00,0x00,0x64,0xE8,0x78,0x14,0x04,0x00,
    0x48,0x8B,0x5C,0x24,0x30,0x48,0x8B,0xC7,0x48,0x8B,0x74,0x24,0x38,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin49[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB7,0x42,0x02,0x48,0x8B,0xFA,0x48,0x8B,
    0xD9,0x66,0x85,0xC0,0x74,0x0A,0x0F,0xB7,0xC8,0xE8,0x3A,0xC6,0x02,0x00,0xEB,0x05,0xE8,0x53,0xC6,0x02,0x00,0x33,0xF6,0x4C,
    0x8D,0x57,0x06,0x4C,0x8B,0xC0,0x4C,0x8D,0x4B,0x14,0x44,0x8D,0x5E,0x02,0x66,0x90,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,
    0xFE,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x41,0x89,0x71,0xF4,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,
    0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0xEC,0x41,0x89,0x51,0xF0,0x41,0x0F,0xB7,0x02,0x41,0x0F,0xB7,0x08,0x0F,0xAF,0xC8,0xB8,
    0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0xF8,0x41,
    0x89,0x51,0xFC,0x41,0x89,0x31,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x02,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,
    0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x04,0x41,0x89,0x51,0x08,0x41,0x89,
    0x71,0x0C,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x04,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,
    0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x10,0x41,0x89,0x51,0x14,0x41,0x89,0x71,0x18,0x41,0x0F,
    0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x06,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,
    0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x1C,0x41,0x89,0x51,0x20,0x41,0x89,0x71,0x24,0x41,0x0F,0xB7,0x00,0x41,0x0F,
    0xB7,0x4A,0x08,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,
    0x03,0xD0,0x41,0x89,0x51,0x28,0x41,0x89,0x51,0x2C,0x41,0x89,0x71,0x30,0x41,0x0F,0xB7,0x4A,0x0A,0x41,0x0F,0xB7,0x00,0x0F,
    0xAF,0xC8,0x83,0xC1,0x63,0xB8,0x1F,0x85,0xEB,0x51,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,
    0x51,0x34,0x41,0x89,0x51,0x38,0x41,0x89,0x71,0x3C,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x0C,0x0F,0xAF,0xC8,0xB8,0x1F,
    0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x40,0x41,0x89,
    0x51,0x44,0x41,0x89,0x71,0x48,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x0E,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,
    0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x4C,0x41,0x89,0x51,0x50,0x41,0x89,
    0x71,0x54,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x10,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,
    0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x58,0x41,0x89,0x51,0x5C,0x41,0x89,0x71,0x60,0x41,0x0F,
    0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x12,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,
    0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0x64,0x41,0x89,0x51,0x68,0x41,0x89,0x71,0x6C,0x41,0x0F,0xB7,0x00,0x41,0x0F,
    0xB7,0x4A,0x14,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,
    0x03,0xD0,0x41,0x89,0x51,0x70,0x41,0x89,0x51,0x74,0x41,0x89,0x71,0x78,0x41,0x0F,0xB7,0x00,0x41,0x0F,0xB7,0x4A,0x16,0x0F,
    0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x4D,0x8D,0x89,0xC0,0x00,0x00,0x00,0x8B,0xC2,
    0x4D,0x8D,0x52,0x20,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0xBC,0x41,0x89,0x51,0xC0,0x41,0x89,0x71,0xC4,0x41,0x0F,0xB7,
    0x4A,0xF8,0x41,0x0F,0xB7,0x00,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,
    0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,0xC8,0x41,0x89,0x51,0xCC,0x41,0x89,0x71,0xD0,0x41,0x0F,0xB7,0x4A,0xFA,0x41,0x0F,
    0xB7,0x00,0x0F,0xAF,0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,
    0xD0,0x41,0x89,0x51,0xD4,0x41,0x89,0x51,0xD8,0x41,0x89,0x71,0xDC,0x41,0x0F,0xB7,0x4A,0xFC,0x41,0x0F,0xB7,0x00,0x0F,0xAF,
    0xC8,0xB8,0x1F,0x85,0xEB,0x51,0x83,0xC1,0x63,0xF7,0xE9,0xC1,0xFA,0x05,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x41,0x89,0x51,
    0xE0,0x41,0x89,0x51,0xE4,0x41,0x89,0x71,0xE8,0x49,0x83,0xEB,0x01,0x0F,0x85,0x2D,0xFD,0xFF,0xFF,0x41,0x0F,0xB7,0x40,0x02,
    0x66,0x89,0x83,0x88,0x01,0x00,0x00,0x41,0x0F,0xB7,0x40,0x04,0x66,0x89,0x83,0x8C,0x01,0x00,0x00,0x41,0x0F,0xB7,0x40,0x06,
    0x89,0x83,0x98,0x01,0x00,0x00,0x41,0x0F,0xB7,0x40,0x08,0x89,0x83,0x9C,0x01,0x00,0x00,0xB8,0xAD,0x8B,0xDB,0x68,0x0F,0xB7,
    0x4F,0x44,0x0F,0xAF,0x4B,0x04,0x81,0xC1,0x0F,0x27,0x00,0x00,0xF7,0xE9,0xB8,0xAD,0x8B,0xDB,0x68,0xC1,0xFA,0x0C,0x8B,0xCA,
    0xC1,0xE9,0x1F,0x03,0xD1,0x89,0x93,0xA0,0x01,0x00,0x00,0x0F,0xB7,0x4F,0x46,0x0F,0xAF,0x4B,0x04,0x81,0xC1,0x0F,0x27,0x00,
    0x00,0xF7,0xE9,0xC1,0xFA,0x0C,0x8B,0xC2,0xC1,0xE8,0x1F,0x03,0xD0,0x89,0x93,0xA4,0x01,0x00,0x00,0x0F,0xB6,0x47,0x48,0x88,
    0x83,0xA8,0x01,0x00,0x00,0x0F,0xB6,0x47,0x4A,0x88,0x83,0xA9,0x01,0x00,0x00,0x0F,0xB6,0x47,0x4C,0x88,0x83,0xAA,0x01,0x00,
    0x00,0x0F,0xB6,0x47,0x4E,0x88,0x83,0xAB,0x01,0x00,0x00,0x0F,0xB6,0x47,0x50,0x88,0x83,0xAC,0x01,0x00,0x00,0x0F,0xB6,0x47,
    0x52,0x88,0x83,0xAD,0x01,0x00,0x00,0x0F,0xB6,0x47,0x54,0x88,0x83,0xAE,0x01,0x00,0x00,0x48,0x8B,0x05,0x48,0x31,0x72,0x02,
    0x8B,0x88,0x2C,0x01,0x00,0x00,0x89,0x8B,0xC4,0x01,0x00,0x00,0x48,0x8B,0xCF,0xE8,0x14,0xAC,0x0E,0x00,0x48,0x8B,0x74,0x24,
    0x38,0x89,0x83,0x70,0x02,0x00,0x00,0x48,0x8B,0x5C,0x24,0x30,0x48,0x83,0xC4,0x20,0x5F,0xC3,
};
constexpr std::uint8_t kChildPin50[]={
    0x48,0x89,0x51,0x28,0x33,0xD2,0x48,0x89,0x11,0x48,0x89,0x51,0x08,0x48,0x89,0x51,0x10,0x48,0x89,0x51,0x18,0x89,0x51,0x20,
    0x88,0x51,0x24,0xC7,0x41,0x30,0x03,0x03,0x01,0x01,0x89,0x51,0x38,0xC7,0x41,0x3C,0x00,0x00,0x80,0x3F,0xC7,0x41,0x40,0x00,
    0x00,0x80,0x3F,0xC7,0x41,0x44,0x00,0x00,0x80,0x3F,0x88,0x51,0x34,0xC7,0x41,0x48,0x64,0x00,0x64,0x00,0x48,0xC7,0x41,0x4C,
    0x64,0x00,0x00,0x00,0x48,0x89,0x51,0x58,0xC7,0x41,0x60,0x00,0x00,0x80,0x3F,0x48,0xC7,0x41,0x64,0x00,0x00,0x80,0x3F,0x89,
    0x51,0x6C,0x89,0x51,0x54,0xC7,0x41,0x70,0x00,0x00,0x80,0x3F,0x48,0xC7,0x41,0x74,0x00,0x00,0x80,0x3F,0xC3,
};
constexpr std::uint8_t kChildPin51[]={
    0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x30,0x49,0x8B,0xF0,
    0x4C,0x8B,0xF2,0x45,0x33,0xC0,0x48,0x8B,0xF9,0x48,0x8B,0x49,0x10,0x49,0x8B,0xD9,0x41,0x8D,0x50,0x12,0xE8,0x7F,0xFF,0xFB,
    0xFF,0x48,0x85,0xC0,0x0F,0x84,0xA8,0x00,0x00,0x00,0x8B,0x48,0x08,0xE8,0xBE,0x6D,0x0C,0x00,0x48,0x85,0xC0,0x0F,0x84,0x97,
    0x00,0x00,0x00,0x45,0x33,0xC0,0x48,0x8B,0xD0,0x48,0x8B,0xCE,0xE8,0x57,0xFE,0xED,0xFF,0x48,0x8B,0x57,0x08,0x0F,0xB7,0x4A,
    0x4C,0x81,0xE9,0xF8,0x03,0x00,0x00,0x0F,0x84,0x62,0x01,0x00,0x00,0x81,0xE9,0x92,0x0F,0x00,0x00,0x0F,0x84,0x56,0x01,0x00,
    0x00,0x48,0x89,0x6C,0x24,0x40,0x83,0xE9,0x0B,0x0F,0x84,0x08,0x01,0x00,0x00,0x83,0xE9,0x0D,0x0F,0x84,0xCC,0x00,0x00,0x00,
    0x83,0xF9,0x28,0x0F,0x84,0x93,0x00,0x00,0x00,0x8B,0x0A,0x81,0xF9,0x88,0x08,0x00,0x00,0x74,0x1B,0x81,0xF9,0x89,0x08,0x00,
    0x00,0x74,0x4D,0x8D,0x81,0x76,0xF7,0xFF,0xFF,0x83,0xF8,0x02,0x76,0x08,0x81,0xF9,0x7C,0x09,0x00,0x00,0x75,0x1F,0x33,0xFF,
    0x4C,0x89,0x74,0x24,0x28,0x45,0x33,0xC0,0x89,0x7C,0x24,0x20,0x33,0xD2,0x48,0x8B,0xCE,0x44,0x8D,0x4F,0x01,0xE8,0xE6,0x05,
    0xEE,0xFF,0x48,0x89,0x03,0x48,0x8B,0x6C,0x24,0x40,0x48,0x8B,0x5C,0x24,0x48,0x48,0x8B,0x74,0x24,0x50,0x48,0x8B,0x7C,0x24,
    0x58,0x48,0x83,0xC4,0x30,0x41,0x5E,0xC3,
};
constexpr std::uint8_t kChildPin52[]={
    0x48,0x89,0x5C,0x24,0x08,0x48,0x63,0x59,0x04,0x45,0x33,0xDB,0x48,0x85,0xDB,0x7E,0x2D,0x45,0x8B,0xD3,0x4C,0x8D,0x49,0x10,
    0x83,0xFA,0xFF,0x74,0x08,0x41,0x0F,0xB7,0x01,0x3B,0xC2,0x75,0x0A,0x41,0x8B,0xC0,0x41,0xFF,0xC8,0x85,0xC0,0x74,0x17,0x41,
    0xFF,0xC3,0x49,0xFF,0xC2,0x49,0x83,0xC1,0x10,0x4C,0x3B,0xD3,0x7C,0xDA,0x33,0xC0,0x48,0x8B,0x5C,0x24,0x08,0xC3,0x48,0x8B,
    0x5C,0x24,0x08,0x49,0x63,0xC3,0x48,0xFF,0xC0,0x48,0xC1,0xE0,0x04,0x48,0x03,0xC1,0xC3,
};
constexpr std::uint8_t kChildPin53[]={
    0x4C,0x8B,0xCA,0x4C,0x8D,0x15,0x86,0x03,0x66,0x02,0x4C,0x8B,0xC2,0x41,0x81,0xE1,0xFF,0xFF,0xFF,0x01,0x49,0x81,0xE0,0x00,
    0x00,0x00,0xFE,0x49,0x8B,0xCA,0x33,0xC0,0x48,0x8B,0x11,0x48,0x83,0xFA,0xFF,0x74,0x1B,0x49,0x3B,0xD0,0x74,0x1D,0xFF,0xC0,
    0x48,0x83,0xC1,0x08,0x83,0xF8,0x40,0x72,0xE7,0xC1,0xE0,0x19,0x41,0x0B,0xC1,0x0F,0xBA,0xE8,0x1F,0xC3,0x48,0x63,0xC8,0x4D,
    0x89,0x04,0xCA,0xC1,0xE0,0x19,0x41,0x0B,0xC1,0x0F,0xBA,0xE8,0x1F,0xC3,
};
constexpr std::uint8_t kChildPin54[]={
    0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0x48,0x8B,0xC2,0x48,0x8D,0x0D,0xFD,0xE7,0x40,0x00,0x45,0x33,0xC0,0x48,0x89,
    0x0B,0x33,0xC9,0x48,0x89,0x53,0x08,0x48,0x89,0x4B,0x10,0x89,0x4B,0x18,0x8D,0x51,0xFF,0x48,0x8B,0xC8,0xE8,0x5F,0x9C,0x01,
    0x00,0x48,0x8B,0xC3,0x48,0x83,0xC4,0x20,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin55[]={
    0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x15,0x83,0x98,0x6F,0x02,0x48,0x8D,0x1D,0xDC,0xFF,0xFF,0xFF,0x48,0x63,0xC9,0x41,
    0xB9,0x5C,0x00,0x00,0x00,0x48,0x89,0x5C,0x24,0x20,0x4C,0x63,0x42,0x04,0x48,0x83,0xC2,0x08,0xFF,0x15,0x20,0xFC,0x18,0x00,
    0x48,0x85,0xC0,0x75,0x21,0x48,0x8B,0x15,0x54,0x98,0x6F,0x02,0x8D,0x48,0x45,0x44,0x8D,0x48,0x5C,0x48,0x89,0x5C,0x24,0x20,
    0x4C,0x63,0x42,0x04,0x48,0x83,0xC2,0x08,0xFF,0x15,0xFA,0xFB,0x18,0x00,0x48,0x83,0xC4,0x30,0x5B,0xC3,
};
constexpr std::uint8_t kChildPin56[]={
    0x0F,0xB7,0x02,0x2B,0xC8,0x8B,0xC1,0xC3,
};
constexpr std::uint8_t kChildPin57[]={
    0x8D,0x41,0xFF,0x48,0x63,0xC8,0x48,0x8B,0x05,0xCB,0x70,0x6F,0x02,0x48,0x8D,0x14,0x49,0x48,0x8D,0x04,0x90,0xC3,
};
constexpr std::uint8_t kChildPin58[]={
    0x48,0x8B,0x05,0xB9,0x70,0x6F,0x02,0xC3,
};
constexpr std::uint8_t kChildPin59[]={
    0x8B,0x05,0x7A,0x65,0x5C,0x00,0xC3,
};
struct ChildPin {uintptr_t rva;const std::uint8_t* bytes;std::size_t size;};
constexpr ChildPin kChildPins[]={
    {0x3DF930,kChildPin0,sizeof(kChildPin0)}, // factory
    {0x3DFEB0,kChildPin1,sizeof(kChildPin1)}, // object_resolver_including_tables
    {0x3E0F10,kChildPin2,sizeof(kChildPin2)}, // object_lookup
    {0x3E0160,kChildPin3,sizeof(kChildPin3)}, // resource_mask
    {0x39E1E0,kChildPin4,sizeof(kChildPin4)}, // resource_find
    {0x39CC40,kChildPin5,sizeof(kChildPin5)}, // resource_acquire
    {0x39DA10,kChildPin6,sizeof(kChildPin6)}, // resource_release
    {0x3E63D0,kChildPin7,sizeof(kChildPin7)}, // resource_bind
    {0x3E6120,kChildPin8,sizeof(kChildPin8)}, // resource_acquire_three
    {0x3E0B50,kChildPin9,sizeof(kChildPin9)}, // model_filename
    {0x3E0AB0,kChildPin10,sizeof(kChildPin10)}, // ai_filename
    {0x3E0BD0,kChildPin11,sizeof(kChildPin11)}, // mset_filename
    {0x419E30,kChildPin12,sizeof(kChildPin12)}, // constructor_type4
    {0x3DB840,kChildPin13,sizeof(kChildPin13)}, // constructor_3db840
    {0x411700,kChildPin14,sizeof(kChildPin14)}, // constructor_411700
    {0x410940,kChildPin15,sizeof(kChildPin15)}, // constructor_410940
    {0x3D2DA0,kChildPin16,sizeof(kChildPin16)}, // constructor_3d2da0
    {0x3DA940,kChildPin17,sizeof(kChildPin17)}, // constructor_3da940
    {0x3B3200,kChildPin18,sizeof(kChildPin18)}, // constructor_actor
    {0x409D20,kChildPin19,sizeof(kChildPin19)}, // handler_lookup
    {0x409AB0,kChildPin20,sizeof(kChildPin20)}, // handler0_init
    {0x3E4F90,kChildPin21,sizeof(kChildPin21)}, // initialize_script_descriptor
    {0x3BFBC0,kChildPin22,sizeof(kChildPin22)}, // register_actor_list
    {0x431a70,kChildPin23,sizeof(kChildPin23)}, // set_sheet_trap
    {0x411350,kChildPin24,sizeof(kChildPin24)}, // set_sheet_native
    {0x3c0280,kChildPin25,sizeof(kChildPin25)}, // status_allocate
    {0x1c1ff0,kChildPin26,sizeof(kChildPin26)}, // model_kind3_ctor
    {0x1c21e0,kChildPin27,sizeof(kChildPin27)}, // model_instance_allocate
    {0x1a4570,kChildPin28,sizeof(kChildPin28)}, // model_instance_init
    {0x1d1b50,kChildPin29,sizeof(kChildPin29)}, // model_mesh_prepare
    {0x19c2b0,kChildPin30,sizeof(kChildPin30)}, // default_heap_allocate
    {0x152430,kChildPin31,sizeof(kChildPin31)}, // actor_allocate_thunk
    {0x4ad270,kChildPin32,sizeof(kChildPin32)}, // handle_decode_thunk
    {0x4ad3f0,kChildPin33,sizeof(kChildPin33)}, // handle_decode
    {0x4ad2c0,kChildPin34,sizeof(kChildPin34)}, // handle_lazy_init
    {0x1d1a30,kChildPin35,sizeof(kChildPin35)}, // model_count_getter
    {0x1c25c0,kChildPin36,sizeof(kChildPin36)}, // model_count_getter2
    {0x1c2680,kChildPin37,sizeof(kChildPin37)}, // model_data_getter
    {0x1ad130,kChildPin38,sizeof(kChildPin38)}, // model_geometry_tail1
    {0x1ad1b0,kChildPin39,sizeof(kChildPin39)}, // model_geometry_tail2
    {0x1ad8c0,kChildPin40,sizeof(kChildPin40)}, // model_geometry_thunk1
    {0x1ad4b0,kChildPin41,sizeof(kChildPin41)}, // model_geometry_thunk2
    {0x1ad990,kChildPin42,sizeof(kChildPin42)}, // model_geometry_command
    {0x1a8e60,kChildPin43,sizeof(kChildPin43)}, // model_copy16
    {0x1a7d10,kChildPin44,sizeof(kChildPin44)}, // model_list_link
    {0x142280,kChildPin45,sizeof(kChildPin45)}, // matrix_identity
    {0x19c740,kChildPin46,sizeof(kChildPin46)}, // model_data_init
    {0x4ad240,kChildPin47,sizeof(kChildPin47)}, // handle_encode
    {0x3c0010,kChildPin48,sizeof(kChildPin48)}, // status_reset
    {0x3c2240,kChildPin49,sizeof(kChildPin49)}, // status_sheet_init
    {0x4016b0,kChildPin50,sizeof(kChildPin50)}, // status_aux_reset
    {0x3e6470,kChildPin51,sizeof(kChildPin51)}, // constructor_no_effect_path
    {0x3a6420,kChildPin52,sizeof(kChildPin52)}, // bar_type_lookup
    {0x4ad390,kChildPin53,sizeof(kChildPin53)}, // handle_encode_region_complete
    {0x1a4530,kChildPin54,sizeof(kChildPin54)}, // model_wrapper_init
    {0x3ebf30,kChildPin55,sizeof(kChildPin55)}, // sheet_lookup
    {0x3ebf20,kChildPin56,sizeof(kChildPin56)}, // sheet_compare
    {0x3ee8a0,kChildPin57,sizeof(kChildPin57)}, // battle_level_lookup
    {0x3ee8c0,kChildPin58,sizeof(kChildPin58)}, // battle_level_default
    {0x1501d0,kChildPin59,sizeof(kChildPin59)}, // runtime_mode
};
constexpr std::uint8_t kChildObject317[]={0x3D,0x01,0x00,0x00,0x04,0x00,0x00,0x00,0x4D,0x5F,0x45,0x58,0x38,0x38,0x30,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x4D,0x5F,0x45,0x58,0x38,0x38,0x30,0x2E,0x6D,0x73,0x65,0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xC3,0x0B,0x00,0x00,0x00,0x00,0x20,0x41,0x09,0x00,0x01,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
} // namespace

namespace {
// No per-child state: these flags describe only installed native compatibility.
std::atomic<bool> g_childNativeAttempted{},g_childNativePinned{};
uintptr_t g_childNativeBase{};
struct ChildLastError {DWORD value=GetLastError();~ChildLastError(){SetLastError(value);}};
using ChildOutcome=ScriptChildNativeOutcome;
constexpr uintptr_t ChildResourceRoot=0x29F33C8,ChildHeapRoot=0x9BA920;
constexpr uintptr_t ChildPool=0x2A17290,ChildFree=0x2A23810,ChildCount=0x2A23950;
constexpr std::size_t ChildResourceCap=512;
static_assert(std::is_trivially_copyable_v<ScriptChildResourceLease>);

bool ChildMatchPins(uintptr_t base,bool original) noexcept {
    std::array<std::uint8_t,256> bytes{};
    for(const auto& p:kChildPins) {
        // Existing private-status owns these fully pinned original gateways.
        if(!original && (p.rva==0x3DF930 || p.rva==0x3C0010))continue;
        for(std::size_t n=0;n<p.size;) {
            const auto count=std::min(bytes.size(),p.size-n);
            if(!CopyNative(base+p.rva+n,bytes.data(),count) ||
               std::memcmp(bytes.data(),p.bytes+n,count))return false;
            n+=count;
        }
    }
    return true;
}
// Match the established (non-initializing) branch of pinned4AD2C0.
// A manual handle-table read alone does not prove this thread's CRT guard.
bool ChildRegistryInitialized() noexcept {
    std::uint32_t index=0;std::int32_t guard=0,epoch=0;uintptr_t slots=0,block=0;
    if(!ReadNative(g_exeBase+0x2AF9B68,index) || index>=1024 ||
       !ReadNative(g_exeBase+0x2B0D920,guard) || !guard || guard==-1)return false;
    slots=static_cast<uintptr_t>(__readgsqword(0x58));
    return slots && ReadNative(slots+static_cast<uintptr_t>(index)*8,block) && block &&
        ReadNative(block+0x10,epoch) && guard<=epoch;
}
bool ChildThread() noexcept {
    return g_childNativePinned.load(std::memory_order_acquire) &&
        g_childNativeBase==g_exeBase && IsDiagnosticGameThread() && ChildRegistryInitialized();
}
bool ChildStampCurrent(const TraceStamp& stamp) noexcept {
    std::uint8_t field=0,menu=0;std::uint32_t frozen=1;std::int32_t timeline=-1;uintptr_t event=1;
    return ChildThread() && !warp::TransitionPending() && !warp::LoadPending() && SameStamp(stamp) &&
        ReadNative(g_exeBase+offsets::IN_FIELD,field) && field &&
        ReadNative(g_exeBase+offsets::OPEN_MENU,menu) && menu==0xFF &&
        ReadNative(g_exeBase+offsets::CONTROLLABLE,frozen) && !frozen &&
        ReadNative(g_exeBase+offsets::CUTSCENE_STATE,timeline) && !timeline &&
        ReadNative(g_exeBase+offsets::EVENT_CONTEXT,event) && !event;
}
bool ChildDecode(std::uint32_t raw,uintptr_t& out) noexcept {
    out=0;if(!raw)return false;const auto h=raw&0x7FFFFFFFu;uintptr_t region=0;
    if(!ReadNative(g_exeBase+HANDLE_REGIONS_RVA+(h>>25)*8,region) || !region ||
       region==UINTPTR_MAX || (region&0x1FFFFFFu))return false;
    out=region|(h&0x1FFFFFFu);return ReadableRange(out,1);
}
bool ChildObject(uintptr_t entry) noexcept {
    return entry && Matches(entry,kChildObject317);
}
bool ChildNames(uintptr_t entry,std::array<std::array<char,68>,3>& names) noexcept {
    __try {
        using Name=char*(__fastcall*)(uintptr_t,char*);
        using MotionName=char*(__fastcall*)(uintptr_t,std::uint32_t,char*);
        return reinterpret_cast<Name>(g_exeBase+0x3E0B50)(entry,names[0].data())==names[0].data() &&
            reinterpret_cast<Name>(g_exeBase+0x3E0AB0)(entry,names[1].data())==names[1].data() &&
            reinterpret_cast<MotionName>(g_exeBase+0x3E0BD0)(entry,317,names[2].data())==names[2].data();
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ChildNamesValid(const std::array<std::array<char,68>,3>& names) noexcept {
    for(const auto& n:names) {
        if(!std::memchr(n.data(),0,n.size()))return false;
        for(const char* c=n.data();*c;++c)if(*c<0x20 || *c>0x7E)return false;
    }
    if(std::strcmp(names[0].data(),"obj/M_EX880.mdlx") ||
       std::strcmp(names[2].data(),"obj/M_EX880.mset"))return false;
    constexpr char prefix[]="obj/M_EX880.a.";
    if(std::strncmp(names[1].data(),prefix,sizeof(prefix)-1))return false;
    const auto n=std::strlen(names[1].data()+sizeof(prefix)-1);
    if(!n || n>8)return false;
    for(const char* c=names[1].data()+sizeof(prefix)-1;*c;++c)
        if(!((*c>='a' && *c<='z') || (*c>='A' && *c<='Z') || (*c>='0' && *c<='9')))return false;
    return true;
}
struct ChildResourceView {
    uintptr_t record{},payload{};std::int32_t resourceClass{};std::uint16_t refs{};
    std::array<std::uint8_t,120> raw{};
};
struct ChildResources {uintptr_t head{};std::array<ChildResourceView,3> files{};};
bool ChildScan(const std::array<std::array<char,68>,3>& names,ChildResources& out) noexcept {
    out={};std::array<uintptr_t,64> regions{},checkRegions{};
    std::array<uintptr_t,ChildResourceCap> seen{},payloads{};std::array<unsigned,3> matches{};
    if(!ReadNative(g_exeBase+ChildResourceRoot,out.head) || !out.head ||
       !ReadNative(g_exeBase+HANDLE_REGIONS_RVA,regions))return false;
    auto current=out.head;std::size_t count=0;
    while(current) {
        if(count==seen.size() || std::find(seen.begin(),seen.begin()+count,current)!=seen.begin()+count)return false;
        seen[count]=current;std::array<std::uint8_t,120> raw{};
        if(!CopyNative(current,raw.data(),raw.size()) || !std::memchr(raw.data()+4,0,68))return false;
        payloads[count++]=Field<uintptr_t>(raw.data(),0x58);
        for(unsigned i=0;i<3;++i)if(!std::strcmp(reinterpret_cast<const char*>(raw.data()+4),names[i].data())) {
            if(++matches[i]!=1)return false;
            auto& r=out.files[i];r.record=current;r.payload=payloads[count-1];r.raw=raw;
            r.refs=Field<std::uint16_t>(raw.data(),2);r.resourceClass=Field<std::int32_t>(raw.data(),0x48);
            std::uint8_t probe=0;
            if(raw[1]!=3 || r.resourceClass>=10000 || !r.payload || !ReadNative(r.payload,probe))return false;
        }
        const auto next=Field<std::uint32_t>(raw.data(),0x70);
        if(!next){current=0;continue;}
        const auto h=next&0x7FFFFFFFu;
        const auto region=regions[h>>25];
        if(!region || region==UINTPTR_MAX || (region&0x1FFFFFFu))return false;
        current=region|(h&0x1FFFFFFu);
    }
    uintptr_t checkHead=0;
    if(matches!=std::array<unsigned,3>{1,1,1} ||
       !ReadNative(g_exeBase+ChildResourceRoot,checkHead) || checkHead!=out.head ||
       !ReadNative(g_exeBase+HANDLE_REGIONS_RVA,checkRegions) || checkRegions!=regions)return false;
    for(const auto& r:out.files) {
        std::array<std::uint8_t,120> check{};
        if(std::count(payloads.begin(),payloads.begin()+count,r.payload)!=1 ||
           !CopyNative(r.record,check.data(),check.size()) || check!=r.raw)return false;
    }
    return true;
}
bool ChildModelHeap(const ChildResources& resources,uintptr_t& model,uintptr_t& heap) noexcept {
    model=0;heap=0;std::uint32_t header[4]{};
    const auto bar=resources.files[0].payload;
    if(!CopyNative(bar,header,sizeof(header)) || header[0]!=0x01524142 || header[1]!=4)return false;
    constexpr std::uint64_t tags[]{0x78655F6D00000004ull,0x5F6D697400000007ull,
        0x78655F6D00000003ull,0x78655F6D00000017ull};
    constexpr std::uint32_t sizes[]{35472,135492,10086,584};
    for(unsigned i=0;i<4;++i) {
        std::uint64_t tag=0;std::uint32_t handle=0,size=0;uintptr_t payload=0;std::uint8_t probe=0;
        const auto entry=bar+16+16*i;
        if(!ReadNative(entry,tag) || tag!=tags[i] || !ReadNative(entry+8,handle) ||
           !ReadNative(entry+12,size) || size!=sizes[i] || !ChildDecode(handle,payload) ||
           !ReadNative(payload,probe) || !ReadNative(payload+size-1,probe))return false;
        if(!i)model=payload;
    }
    uintptr_t vtable=0,data=0,fn=0;std::uint32_t kind=0;
    if(!ReadNative(model,vtable) || vtable!=g_exeBase+0x5B3E00 ||
       !ReadNative(model+8,data) || data!=model+0x90 || !ReadNative(data,kind) || kind!=3 ||
       !ReadNative(vtable+8,fn) || fn!=g_exeBase+0x1D1A30 ||
       !ReadNative(vtable+0x40,fn) || fn!=g_exeBase+0x1C21E0 ||
       !ReadNative(vtable+0x58,fn) || fn!=g_exeBase+0x1C25C0 ||
       !ReadNative(g_exeBase+ChildHeapRoot,heap) || !heap ||
       !ReadNative(heap,vtable) || vtable!=g_exeBase+0x5B2BB0 ||
       !ReadNative(vtable+8,fn) || fn!=g_exeBase+0x19C2B0)return false;
    return true;
}
bool ChildLeaseMatches(const ScriptChildResourceLease& lease,const ChildResources& now,bool exactRefs) noexcept {
    if(now.head!=lease.resourceHead)return false;
    for(unsigned i=0;i<3;++i) {
        const auto& r=now.files[i];
        if(r.record!=lease.records[i] || r.payload!=lease.payloads[i] || r.resourceClass!=lease.resourceClasses[i])return false;
        if(exactRefs && (lease.observedRefs[i]>UINT16_MAX-2 ||
            r.refs!=static_cast<unsigned>(lease.observedRefs[i])+((lease.acquiredMask>>i)&1)))return false;
    }
    return true;
}
bool ChildAcquire(const char* name,uintptr_t& payload) noexcept {
    __try {using Fn=uintptr_t(__fastcall*)(const char*,int);
        payload=reinterpret_cast<Fn>(g_exeBase+0x39CC40)(name,-1);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ChildRelease(uintptr_t payload) noexcept {
    __try {using Fn=void(__fastcall*)(uintptr_t);
        reinterpret_cast<Fn>(g_exeBase+0x39DA10)(payload);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ChildSetSheet(uintptr_t actor) noexcept {
    __try {using Fn=void(__fastcall*)(uintptr_t,int);
        reinterpret_cast<Fn>(g_exeBase+0x411350)(actor,52);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ChildActorRoots(uintptr_t actor,std::uint32_t handle,uintptr_t entry,uintptr_t expectedStatus) noexcept {
    uintptr_t decoded=0,object=0,status=1,controller=1,record=1;
    return actor && ChildDecode(handle,decoded) && decoded==actor && ChildObject(entry) &&
        ReadNative(actor+8,object) && object==entry && ReadNative(actor+0x5C0,status) && status==expectedStatus &&
        ReadNative(actor+0x9E8,controller) && !controller && ReadNative(actor+0x9F0,record) && !record;
}
// Read-only native sheet52 presence: native3EBF30 silently falls back to69.
// Require a sorted table and exact52 before allocating a status; no fallback.
bool ChildSheet52Ready() noexcept {
    uintptr_t table=0,level=0;std::int32_t count=0;std::uint16_t previous=0;
    if(!ReadNative(g_exeBase+0x2AE57C0,table) || !table ||
       !ReadNative(table+4,count) || count<=0 || count>4096)return false;
    unsigned matches=0;
    for(int i=0;i<count;++i) {
        std::array<std::uint8_t,0x5C> row{};
        if(!CopyNative(table+8+static_cast<uintptr_t>(i)*row.size(),row.data(),row.size()))return false;
        const auto id=Field<std::uint16_t>(row.data(),0);
        if(i && id<=previous)return false;previous=id;
        if(id!=52)continue;
        ++matches;const auto levelIndex=Field<std::uint16_t>(row.data(),2);
        if(levelIndex) {
            if(!ReadNative(g_exeBase+0x2AE5978,level) || !level)return false;
            level+=static_cast<uintptr_t>(levelIndex-1)*12;
        } else if(!ReadNative(g_exeBase+0x2AE5980,level) || !level)return false;
        std::array<std::uint8_t,12> probe{};
        if(!CopyNative(level,probe.data(),probe.size()))return false;
    }
    uintptr_t check=0;std::int32_t checkCount=0;
    return matches==1 && ReadNative(g_exeBase+0x2AE57C0,check) && check==table &&
        ReadNative(table+4,checkCount) && checkCount==count;
}
} // namespace

bool InitializeScriptChildNative(uintptr_t exeBase) noexcept {
    ChildLastError error;
    if(g_childNativeAttempted.exchange(true))return g_childNativeBase==exeBase && g_childNativePinned.load();
    g_childNativeBase=exeBase;IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    const bool valid=exeBase && ReadNative(exeBase,dos) && dos.e_magic==IMAGE_DOS_SIGNATURE &&
        dos.e_lfanew>0 && dos.e_lfanew<0x100000 && ReadNative(exeBase+dos.e_lfanew,nt) &&
        nt.Signature==IMAGE_NT_SIGNATURE && nt.FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64 &&
        nt.OptionalHeader.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.FileHeader.TimeDateStamp==0x669E384A &&
        nt.OptionalHeader.SizeOfImage==0x2C2B000 && ChildMatchPins(exeBase,true);
    g_childNativePinned.store(valid,std::memory_order_release);return valid;
}
bool ScriptChildNativeReady() noexcept {
    return ChildThread() && g_installed && g_role && g_role()==2 && privatestatus::Ready();
}
ChildOutcome AcquireScriptChildResources(const TraceStamp& stamp,ScriptChildResourceLease& lease) noexcept {
    ChildLastError error;
    // Never erase an existing transaction or turn a failed release into retry.
    if(lease.exeBase || lease.attemptedMask || lease.acquiredMask || lease.ambiguous)return ChildOutcome::Refused;
    if(!ScriptChildNativeReady() || !ChildStampCurrent(stamp) || !ChildMatchPins(g_exeBase,false))return ChildOutcome::Refused;
    uintptr_t entry=0;
    if(!LookupNative(317,entry) || !ChildObject(entry))return ChildOutcome::Refused;
    std::array<std::array<char,68>,3> names{};
    if(!ChildNames(entry,names) || !ChildNamesValid(names))return ChildOutcome::Refused;
    ChildResources resources{};uintptr_t model=0,heap=0;
    if(!ChildStampCurrent(stamp) || !ChildScan(names,resources) || !ChildModelHeap(resources,model,heap))return ChildOutcome::Refused;
    // Reserve headroom for BOTH our temporary retain and the constructor retain.
    for(const auto& r:resources.files)if(r.refs>UINT16_MAX-2)return ChildOutcome::Refused;
    lease.exeBase=g_exeBase;lease.stamp=stamp;lease.objentry=entry;lease.names=names;
    lease.resourceHead=resources.head;lease.modelObject=model;lease.heap=heap;lease.ownerThread=GetCurrentThreadId();
    for(unsigned i=0;i<3;++i) {
        lease.records[i]=resources.files[i].record;lease.payloads[i]=resources.files[i].payload;
        lease.observedRefs[i]=resources.files[i].refs;lease.resourceClasses[i]=resources.files[i].resourceClass;
    }
    ChildOutcome failure=ChildOutcome::Refused;
    for(unsigned i=0;i<3;++i) {
        ChildResources before{},after{};uintptr_t payload=0;
        if(!ChildStampCurrent(stamp) || !ChildScan(names,before) || !ChildLeaseMatches(lease,before,true))break;
        lease.attemptedMask|=static_cast<std::uint8_t>(1u<<i);
        if(!ChildAcquire(names[i].data(),payload)) {
            lease.ambiguous=true;lease.uncertainMask|=static_cast<std::uint8_t>(1u<<i);failure=ChildOutcome::Fault;break;
        }
        if(payload!=lease.payloads[i] || !ChildStampCurrent(stamp) || !ChildScan(names,after)) {
            lease.ambiguous=true;lease.uncertainMask|=static_cast<std::uint8_t>(1u<<i);failure=ChildOutcome::Ambiguous;break;
        }
        lease.acquiredMask|=static_cast<std::uint8_t>(1u<<i);
        if(!ChildLeaseMatches(lease,after,true)) {
            // Its increment was not established: do not guess a decrement.
            lease.acquiredMask&=static_cast<std::uint8_t>(~(1u<<i));
            lease.uncertainMask|=static_cast<std::uint8_t>(1u<<i);lease.ambiguous=true;failure=ChildOutcome::Ambiguous;break;
        }
        if(i==2) {
            lease.prepared=true;
            if(ScriptChildResourcesCurrent(lease))return ChildOutcome::Ready;
        }
    }
    const auto cleanup=ReleaseScriptChildResources(lease);
    if(failure==ChildOutcome::Fault || cleanup==ChildOutcome::Fault)return ChildOutcome::Fault;
    if(lease.ambiguous || cleanup==ChildOutcome::Ambiguous)return ChildOutcome::Ambiguous;
    return ChildOutcome::Refused;
}
bool ScriptChildResourcesCurrent(const ScriptChildResourceLease& lease) noexcept {
    ChildLastError error;ChildResources resources{};uintptr_t model=0,heap=0;float limit=0,used=0;std::uint8_t weight=0;
    return lease.prepared && !lease.ambiguous && lease.acquiredMask==7 && !lease.releaseAttemptedMask &&
        lease.exeBase==g_exeBase && lease.ownerThread==GetCurrentThreadId() && ScriptChildNativeReady() &&
        ChildStampCurrent(lease.stamp) && ChildObject(lease.objentry) && ChildMatchPins(g_exeBase,false) &&
        ChildScan(lease.names,resources) && ChildLeaseMatches(lease,resources,true) &&
        ChildModelHeap(resources,model,heap) && model==lease.modelObject && heap==lease.heap &&
        ReadNative(lease.objentry+0x54,weight) && weight==9 &&
        ReadNative(g_exeBase+ADMISSION_LIMIT_RVA,limit) && ReadNative(g_exeBase+ADMISSION_USED_RVA,used) &&
        enemypop::BudgetAllows(limit,used,weight) && ChildStampCurrent(lease.stamp);
}
ChildOutcome ReleaseScriptChildResources(ScriptChildResourceLease& lease) noexcept {
    ChildLastError error;bool fault=false;
    // Cleanup does not need current session authority, only our exact native
    // reservations. An authority drop must not itself leak all three refs.
    for(unsigned i=0;i<3;++i)if(lease.acquiredMask&(1u<<i)) {
        const auto bit=static_cast<std::uint8_t>(1u<<i);ChildResources before{},after{};
        // Spend before native entry, including ambiguous/refused cleanup. Never
        // let a retry decrement a replacement or the constructor's own refs.
        lease.acquiredMask&=static_cast<std::uint8_t>(~bit);lease.releaseAttemptedMask|=bit;
        if(lease.exeBase!=g_exeBase || lease.ownerThread!=GetCurrentThreadId() || !ChildStampCurrent(lease.stamp) ||
           !ChildMatchPins(g_exeBase,false) || !ChildScan(lease.names,before) ||
           !ChildLeaseMatches(lease,before,false) || !before.files[i].refs) {
            lease.ambiguous=true;lease.uncertainMask|=bit;continue;
        }
        if(!ChildRelease(lease.payloads[i])) {fault=true;lease.ambiguous=true;lease.uncertainMask|=bit;continue;}
        if(!ChildStampCurrent(lease.stamp) || !ChildScan(lease.names,after) || !ChildLeaseMatches(lease,after,false) ||
           static_cast<unsigned>(after.files[i].refs)+1!=before.files[i].refs) {
            lease.ambiguous=true;lease.uncertainMask|=bit;
        }
    }
    lease.prepared=false;
    return fault?ChildOutcome::Fault:lease.ambiguous?ChildOutcome::Ambiguous:ChildOutcome::Completed;
}
ChildOutcome InitializeScriptChildStatus52(uintptr_t actor,std::uint32_t actorHandle,uintptr_t entry,
    const TraceStamp& stamp,uintptr_t& status) noexcept {
    ChildLastError error;status=0;
    if(!ScriptChildNativeReady() || !ChildStampCurrent(stamp) || !ChildMatchPins(g_exeBase,false) ||
       !ChildActorRoots(actor,actorHandle,entry,0) || !ChildSheet52Ready() || !ChildStampCurrent(stamp) ||
       !ChildActorRoots(actor,actorHandle,entry,0))return ChildOutcome::Refused;
    // No native call between the final pool reads and the exact native leaf.
    std::int32_t count=0,index=-1;
    if(!ReadNative(g_exeBase+ChildCount,count) || count<1 || count>80 ||
       !ReadNative(g_exeBase+ChildFree+static_cast<uintptr_t>(count-1)*4,index) || index<0 || index>=80)
        return ChildOutcome::Refused;
    const auto expected=g_exeBase+ChildPool+static_cast<uintptr_t>(index)*0x278;
    if(!ChildSetSheet(actor)){(void)ReadNative(actor+0x5C0,status);return ChildOutcome::Fault;}
    std::uint32_t backref=0;std::int32_t hp=0,maxHp=0,afterCount=-1;
    if(!ReadNative(actor+0x5C0,status) || status!=expected ||
       !ChildActorRoots(actor,actorHandle,entry,status) || !ReadNative(status+0x268,backref) ||
       (backref&0x7FFFFFFFu)!=(actorHandle&0x7FFFFFFFu) || !ReadNative(status,hp) ||
       !ReadNative(status+4,maxHp) || maxHp<=0 || hp<=0 || hp>maxHp ||
       !ReadNative(g_exeBase+ChildCount,afterCount) || afterCount!=count-1 || !ChildStampCurrent(stamp))return ChildOutcome::Ambiguous;
    return ChildOutcome::Completed;
}

} // namespace kh2coop::inject::spawncontroller
