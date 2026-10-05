#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace kh2coop::inject::resourcetrace {
inline constexpr std::size_t ChildCap = 64, DepthCap = 8, QueueCap = 256;
enum class Caller : std::uint8_t { Unknown, LookupZeroRoot, Recursive, AlternateRoot };
enum class InstallStatus : std::uint8_t {
    Disabled, Ready, IdentityUnavailable, IdentityMismatch, CreateFailed,
    TrampolineMismatch, AllocationFailed, RegistrationFailed, PinFailed,
    EnableFailedRetained, Retired, ReinitializationRejected, RollbackRetained,
    DiagnosticProfileRejected, FiberStorageUnavailable
};
struct Parent {
    std::uint64_t serial=0, coverage=0, wrapperSequence=0;
    uintptr_t controller=0, record=0;
    std::uint32_t threadId=0;
    std::uint16_t recordIndex=0;
    std::array<std::uint8_t,64> recordBytes{};
    bool available=false, recordIndexAvailable=false, recordBytesAvailable=false;
};
struct StringSample {
    std::array<char,256> bytes{};
    std::uint16_t length=0;
    bool pointerNonNull=false, readable=false, terminated=false, truncated=false;
};
struct ResourceObservation {
    std::uint64_t invocation=0, parentCallback=0, generation=0, outerBoundary=0;
    Parent parent{};
    uintptr_t actualEnteredTarget=0, caller=0, package=0, filename=0, optionalRoot=0;
    uintptr_t sampledVtable=0, sampledSlot0=0, sampledCurrentPackage=0;
    std::int32_t sampledCount=0, sampledIndex=0;
    std::uint32_t sampledMode=0, threadId=0, depth=0;
    StringSample filenameSample{}, rootSample{};
    std::uint64_t rawRax=0, boundaryDropped=0;
    std::uint8_t al=0;
    Caller callerKind=Caller::Unknown;
    bool vtableRead=false, slotRead=false, countRead=false, indexRead=false;
    bool currentPackageRead=false, modeRead=false, normalReturn=false, unwound=false;
    bool outerNormalReturn=false, outerUnwound=false, installationIdentityVerified=false;
    bool dispatchOperandObserved=false, completeRouting=false, fiberContinuityProven=false;
    bool continuousModeProven=false, globalPendingExcluded=false, lifetimeProven=false;
    bool creatorExclusive=false, atomic=false, creationAuthority=false;
};
struct ConstructionToken {
    std::uint64_t generation=0, outerBoundary=0;
    std::uint32_t previousDepth=0;
    bool entered=false;
    uintptr_t context=0; // retained FLS-owned Local, never a stack address
};
struct Statistics {
    InstallStatus status=InstallStatus::Disabled;
    bool recording=false, resourcesMayBeReferenced=false, modulePinned=false;
    bool installationIdentityVerified=false;
    std::uint64_t entered=0, returned=0, unwound=0, dropped=0, unparented=0, foreign=0;
    std::uint64_t published=0, generation=0;
};

// Root owns MinHook initialization and all global uninitialization guards.
// No native calls or activation unless explicitly requested. Once any enable
// was attempted, the module/code/table/trampoline live until process exit.
bool Initialize(uintptr_t exeBase, bool requested=false);
void StopRecording();
bool RetainsMinHookResources();
bool RejectReinitialization();
// Call after the spawn lineage scope is installed. End in the wrapper finally,
// outside its original native call; child publication happens only at outer End.
ConstructionToken BeginConstruction();
void EndConstruction(ConstructionToken token, bool normalReturn);
bool Pop(ResourceObservation& out);
Statistics GetStatistics();
} // namespace kh2coop::inject::resourcetrace
