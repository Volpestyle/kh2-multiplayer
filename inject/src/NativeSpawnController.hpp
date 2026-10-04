#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include "kh2coop/NativeRecordContentTypes.hpp"
#include "kh2coop/KnownControllerMutation.hpp"

namespace kh2coop::inject::spawncontroller {

using LogFn = void (*)(const char* fmt, ...);
// Game-thread callbacks. Role is current session authority: 0 off, 1 host, 2 client.
using RoleFn = std::uint8_t (*)();
using CaptureFn = void (*)(const float* nativePoint4);
// False holds a qualified client controller, including its native cooldown.
// The owner validates epoch, full location, lifecycle, session and lease expiry.
using CopyFn = bool (*)(float* output4, uintptr_t controller, std::uint64_t updateSequence);

// Default-off negative fence: current means only no observed known mutation
// since this ticket. No incarnation, pending exclusion or creation permission.
KnownControllerMutationTicket AcquireKnownMutationTicket();
bool KnownMutationTicketCurrent(const KnownControllerMutationTicket& ticket);

// Diagnostic only. Addresses are local correlation hints, NEVER portable IDs.
// Explicit validity bits distinguish unknown reads from native zero values.
struct TraceStamp {
    std::uint32_t transition = 0, load = 0;
    std::array<std::uint8_t, 10> location {};
};

// Read-only ordinary content inventory. Local pointers/raw NOW are sidecar
// evidence only; portable descriptors contain the six named location fields.
// Complete means equal sampled endpoints, never atomicity, lifetime or action
// eligibility. Storage is bounded, but this API may allocate; never call it in
// the final lease-to-original-call interval. Caller owns the returned snapshot.
enum class RecordCatalogIssue : std::uint32_t {
    None = 0, Thread = 1, Lifecycle = 2, Count = 4, TableRead = 8,
    Changed = 16, Script = 32, UnknownFlags = 64, Pointer = 128,
    ControllerRead = 256, Key = 512, HeaderRead = 1024, UnsupportedType = 2048,
    RecordCap = 4096, Layout = 8192, RecordRead = 16384, Alias = 32768,
    DuplicateId = 65536, DuplicateContent = 131072, LayoutIdentity = 262144,
    ActorRead = 524288, NoAssociation = 1048576, RecordIndex = 2097152
};
struct NativeRecordCatalogEntry {
    std::array<std::uint8_t, 16> tableBefore {}, tableAfter {};
    std::array<std::uint8_t, 64> controllerBefore {}, controllerAfter {};
    std::array<std::uint8_t, 44> headerAfter {};
    NativeRecordContentDefinition content {};
    std::vector<std::array<std::uint8_t, 64>> recordsAfter;
    // Bit 0: complete before read; bit 1: complete after read, per record.
    std::vector<std::uint8_t> recordReadMask;
    uintptr_t controller = 0, header = 0, spawnArray = 0, regionArray = 0;
    std::uint32_t issues = 0;
    std::uint16_t declaredRecords = 0;
    bool tableBeforeAvailable = false, tableAfterAvailable = false;
    bool controllerBeforeAvailable = false, controllerAfterAvailable = false;
    bool headerBeforeAvailable = false, headerAfterAvailable = false;
    bool bytesComplete = false, associationUnique = false;
};
struct NativeRecordCatalog {
    std::array<NativeRecordCatalogEntry, 64> entries {};
    TraceStamp before {}, after {};
    std::array<std::uint8_t, 32> layoutSha256 {};
    uintptr_t exeBase = 0;
    std::int32_t countBefore = 0, countAfter = 0;
    std::uint32_t entryCount = 0, declaredLogicalRecords = 0, capturedLogicalRecords = 0, issues = 0;
    NativeRecordContentStatus status = NativeRecordContentStatus::Unavailable;
    bool countBeforeAvailable = false, countAfterAvailable = false;
    bool beforeAvailable = false, afterAvailable = false, lifecycleStable = false;
    bool tableInventoryComplete = false, contentBytesComplete = false, associationComplete = false;
    bool behaviorEligibility = false, lifetimeProven = false, globalPendingExcluded = false, atomic = false;
};
struct NativeRecordMembership {
    // Indices address the supplied catalog only; they are not actor identities.
    std::uint32_t tableIndex = 0, issues = 0;
    std::uint16_t recordIndex = 0;
    uintptr_t actor = 0;
    std::array<uintptr_t, 2> controller {}, record {};
    std::array<std::array<std::uint8_t, 64>, 2> recordBytes {}, controllerBytes {};
    std::array<std::array<std::uint8_t, 44>, 2> headerBytes {};
    std::array<std::vector<std::array<std::uint8_t, 64>>, 2> definitionRecords;
    std::array<std::vector<bool>, 2> definitionRecordReads;
    std::array<std::array<std::uint8_t, 16>, 64> tableBefore {}, tableAfter {};
    std::array<std::uint8_t, 64> tableReadMask {};
    std::array<std::int32_t, 2> counts {};
    // Actual individual read receipts, not inferred from nonzero values.
    std::array<bool, 2> pointerReads {}, recordReads {}, controllerReads {}, headerReads {}, tableReads {}, countReads {}, stampReads {};
    std::array<bool, 2> actorControllerReads {}, actorRecordReads {};
    std::array<TraceStamp, 2> stamps {};
    NativeRecordContentStatus status = NativeRecordContentStatus::Unavailable;
    bool contentComparisonAvailable = false;
    bool behaviorEligibility = false, lifetimeProven = false, globalPendingExcluded = false, atomic = false;
};

// layoutSha256 is caller-supplied reviewed compatibility provenance, not a
// digest of running memory. Unknown (all-zero) provenance cannot be complete.
void CaptureNativeRecordCatalog(uintptr_t exeBase,
    const std::array<std::uint8_t, 32>& layoutSha256, NativeRecordCatalog& out);
void ResolveNativeRecordMembership(uintptr_t exeBase, const NativeRecordCatalog& catalog,
    uintptr_t actor, NativeRecordMembership& out);
struct TraceState {
    uintptr_t header = 0, spawnArray = 0, cacheBucket = 0;
    std::uint32_t key = 0, flags = 0, currentCount = 0, initialCount = 0;
    float cooldown = 0;
    std::uint16_t headerId = 0, recordCount = 0;
    std::uint8_t stage = 0, activation = 0, nativeType = 0;
    std::int32_t cacheRoom = 0, cacheAge = 0;
    std::array<std::uint16_t, 256> cacheIds {};
    bool controllerAvailable = false, cacheAvailable = false;
};

// Read-only, fixed-size historical observation, never activation authority.
// Requires PREPARE=1 and SPAWN_TRACE=1 before the native initialization. Counts
// are native enrollment/death state, NOT numbers of emitted actors. A missed
// first return cannot be replaced by a later tick. Raw header+E is retained.
enum class HostFirstEmissionStatus : std::uint8_t { Disabled, Unavailable, Waiting, Recorded, Refused };
struct HostFirstEmission {
    HostFirstEmissionStatus status = HostFirstEmissionStatus::Disabled;
    const char* reason = "disabled"; // static literal, local diagnostic only
    TraceStamp stamp {};
    std::uint64_t initSerial = 0, updateSequence = 0;
    uintptr_t controller = 0, header = 0, spawnArray = 0, firstActor = 0;
    std::array<std::uint8_t, 32> layoutSha256 {};
    NativeRecordLocation location {};
    std::uint32_t groupKey = 0;
    std::array<std::uint8_t, 44> rawHeader {};
    std::array<std::array<std::uint8_t, 64>, 5> records {};
    std::array<float, 4> point {};
    TraceState before {}, after {};
    std::uint16_t firstRecordIndex = 0;
    bool firstActorMetadataAvailable = false;
    bool battleAvailable = false, barrierAvailable = false, eventAvailable = false;
};
// Game-thread only; no allocation. Binds retained raw content to the caller's
// complete fresh native catalog and live local stamp/pointers. False has no
// usable historical point; the returned status/reason explains refusal.
bool CopyHostFirstEmission(const NativeRecordCatalog& catalog, std::uint32_t entryIndex,
                          HostFirstEmission& out);
// Completed qualified client original call, retained for up to64 controllers
// without eviction. Requires PREPARE+SPAWN_TRACE, current local lifecycle and
// no known mutation. Overflow refuses the load rather than dropping a receipt.
bool CopyLastClientOriginalReturn(uintptr_t controller, std::uint64_t& sequence);

enum class SelectedOccupancyIssue : std::uint64_t {
    Thread = 1, Catalog = 2, Selection = 4, Pointer = 8, Roots = 16,
    Bucket = 32, Next = 64, Cycle = 128, CrossList = 256, Cap = 512,
    Tail = 1024, NodeRead = 2048, RecordRead = 4096, ObjectRead = 8192,
    Changed = 16384, Lifecycle = 32768, Mutation = 65536, Alias = 131072,
    Cache = 262144, CatalogChanged = 524288, Controller = 1048576
};
struct NativeOccupancyFields {
    uintptr_t object = 0, status = 0, controller = 0, record = 0;
    std::uint32_t nextHandle = 0, flags120 = 0, objectId = 0;
    std::uint16_t recordId = 0, readMask = 0;
    std::uint8_t objectType = 0;
    std::array<char, 2> objectName {};
    // Mask: next=1, flags=2, object=4, status=8, controller=16, record=32,
    // recordId=64, objectId=128, objectType=256, objectName=512. Null pointers
    // are successful pointer reads, never synthetic successful pointed reads.
    bool operator==(const NativeOccupancyFields&) const = default;
};
struct NativeOccupancyNode {
    uintptr_t actor = 0;
    std::array<NativeOccupancyFields, 2> fields {};
    std::uint16_t recordIndex = 0;
    bool deferred = false, stable = false, exactSelectedReference = false;
    bool selectedIdConflict = false, pending = false, unclassifiable = false;
    // Exclusion is stable sampled object-ID/type/name classification, not
    // independently rooted ObjEntry membership or an incarnation guarantee.
    bool excludedKnownNoncombat = false, nativeLookupSkipped = false;
};
struct NativeOccupancyCache {
    uintptr_t bucket = 0;
    std::array<std::uint8_t, 0x208> bytes {};
    std::array<std::uint16_t, 256> ids {};
    std::int32_t room = 0, age = 0;
    bool pointerRead = false, bucketInRange = false, bytesRead = false;
};
struct NativeSelectedOccupancy {
    static constexpr std::size_t NodeCap = 256; // aggregate active + deferred
    std::uint32_t selectedDefinition = 0;
    std::uint64_t issues = 0;
    // Fresh whole-room definition/header/table/controller/record samples,
    // retained on drift/failure and compared with the supplied complete catalog.
    std::array<NativeRecordCatalog, 2> catalogs;
    std::array<KnownControllerMutationTicket, 2> mutation;
    std::array<std::array<uintptr_t, 4>, 2> roots {}; // active H/T, deferred H/T
    std::array<std::uint8_t, 2> rootReadMask {};
    std::array<std::array<uintptr_t, 64>, 2> buckets {};
    std::array<std::array<bool, 64>, 2> bucketReads {};
    std::array<std::array<std::uint8_t, 0x58>, 2> controllerBytes {};
    std::array<bool, 2> controllerReads {};
    std::array<TraceState, 2> state {};
    std::array<NativeOccupancyCache, 2> cache {};
    std::array<std::uint32_t, 2> context716750 {};
    std::array<bool, 2> context716750Reads {};
    std::vector<NativeOccupancyNode> nodes;
    std::array<bool, 2> listTerminated {}, tailMatched {};
    std::array<std::uint16_t, 256> activeReferenceCounts {}, deferredReferenceCounts {}, cacheSelectedCounts {};
    std::uint32_t selectedReferenceCount = 0, pendingNodeCount = 0, unclassifiableNodeCount = 0;
    std::uint32_t conflictCount = 0, excludedNoncombatCount = 0, cacheEntryCount = 0;
    bool catalogStable = false, lifecycleStable = false, mutationStable = false;
    bool listedComplete = false, cacheAvailable = false, controllerStateAvailable = false;
    bool noSelectedReferencesAtSamples = false, cacheDuplicateIds = false;
    bool globalPendingExcluded = false, atomic = false, lifetimeProven = false, creationAuthority = false;
};

// Read-only, bounded sampled association. Selection is an index in the supplied
// complete catalog, never a hard-coded group or manifest spawn index. May
// allocate; must not run in the final lease-to-native-call interval. Exact
// reference counts do not establish ready HP/type membership: caller joins
// node actor + controller + record + index to its checked ready census.
void CaptureNativeSelectedOccupancy(uintptr_t exeBase, const NativeRecordCatalog& catalog,
    std::uint32_t selectedDefinition, NativeSelectedOccupancy& out);
enum class TraceOutcome : std::uint8_t {
    Unavailable, OutOfScope, NullReturn, ActorReadUnavailable, IdentityMismatch, Observed
};
enum class TraceWrapper : std::uint8_t { Fixed, Generated };

// Default-off sidecar for naturally entered wrapper calls. The whole five-row
// definition and ordinary table are sampled at the wrapper boundaries, never
// substituted from the render census. These are local diagnostic bytes only.
struct NativeConstructionSample {
    std::array<std::uint8_t, 64> controllerBytes {}, recordBytes {};
    std::array<std::uint8_t, 44> headerBytes {};
    std::array<std::array<std::uint8_t, 64>, 5> records {};
    std::array<std::array<std::uint8_t, 16>, 64> table {};
    TraceStamp stamp {};
    KnownControllerMutationTicket mutation {};
    uintptr_t header = 0, spawnArray = 0, regionArray = 0;
    std::uint64_t tableReadMask = 0;
    std::int32_t countBefore = 0, countAfter = 0;
    std::uint32_t group = 0, tableMatches = 0;
    std::uint16_t headerId = 0, declaredRecords = 0, recordIndex = 0;
    std::uint8_t nativeType = 0, recordReadMask = 0;
    bool controllerRead = false, recordRead = false, headerRead = false;
    bool countBeforeRead = false, countAfterRead = false, stampRead = false;
    bool tableComplete = false, fiveRecordLayout = false;
    bool recordIndexAvailable = false, recordMatchesDefinition = false;
    bool ordinaryAssociationSampled = false;
};
struct NativeConstructionLineage {
    std::uint64_t serial = 0, coverage = 0, coverageAfter = 0, wrapperSequence = 0;
    std::uint64_t enclosingThreadSerial = 0, tickSequence = 0, dispatcherSequence = 0, scriptSequence = 0;
    std::uint64_t droppedBefore = 0, droppedAfter = 0;
    uintptr_t controller = 0, record = 0, callerRva = 0, dispatcherCallerRva = 0, scriptCallerRva = 0;
    std::uint32_t threadId = 0, depth = 0;
    TraceWrapper wrapper = TraceWrapper::Fixed;
    std::array<NativeConstructionSample, 2> samples {};
    bool captured = false, callerRvaAvailable = false;
    bool enclosingTick = false, enclosingDispatcher = false, enclosingScript = false;
    bool dispatcherCallerRvaAvailable = false, scriptCallerRvaAvailable = false;
    bool candidateThreadParent = false, ambiguous = false, overflow = false;
    bool normalReturn = false, unwound = false, sampledIdentityStable = false;
    bool sampledDefinitionStable = false, sampledTableStable = false, sampledLifecycleStable = false;
    bool knownMutationStable = false;
    bool coverageStable = false, traceQueueStable = false;
    // Thread-local nesting is deliberately not promoted to fiber ancestry.
    bool parentFiberAncestryUnproven = true, fiberContinuityProven = false;
    bool continuousModeProven = false, globalPendingExcluded = false;
    bool creatorExclusive = false, controllerIncarnationProven = false;
    bool atomic = false, creationAuthority = false;
};

// Nonallocating copy of the actual current wrapper frame for a child observer.
// Capture this once at child entry. Do not reparent from TLS on child return.
// The copy has before samples only until the wrapper has actually returned;
// terminal PopNativeConstructionLineage joins by exact serial/coverage/sequence.
bool CopyNativeConstructionLineage(NativeConstructionLineage& out);
bool PopNativeConstructionLineage(NativeConstructionLineage& out);
// These classify executed child returns, never inferred budget snapshots.
enum class FactoryOutcome : std::uint8_t {
    Unknown, AdmissionRejected, Type4AllocationFailed, AllocationPassed, Ambiguous
};
constexpr std::uint32_t FactoryAdmissionHook = 1, FactoryAllocationHook = 2;
constexpr std::uint32_t FactoryAllHooks = FactoryAdmissionHook | FactoryAllocationHook;
struct FactoryPredicates {
    std::uint64_t coverageSerial = 0, allocationSize = 0;
    uintptr_t allocationResult = 0;
    std::uint32_t coverageMask = 0, depth = 0;
    std::uint32_t weightBits = 0, limitBeforeBits = 0, usedBeforeBits = 0;
    std::uint32_t limitAfterBits = 0, usedAfterBits = 0;
    std::uint16_t admissionCalls = 0, allocationCalls = 0;
    // Bits0..3: limitBefore, usedBefore, limitAfter, usedAfter readable.
    std::uint8_t operandMask = 0, admissionResult = 0;
    FactoryOutcome outcome = FactoryOutcome::Unknown;
    bool eligible = false, complete = false, unwound = false, countOverflow = false;
    bool admissionReturned = false, allocationReturned = false;
    bool admissionFault = false, allocationFault = false;
};
// Separate opt-in diagnostic coverage; never part of FactoryAllHooks/available.
enum class GeometryTerminal : std::uint8_t { Disabled, Waiting, Armed, TickLimit, Deadline, LifecycleChanged, InvalidConfig };
struct GeometryPredicate {
    uintptr_t region = 0;
    std::array<std::uint8_t, 16> inputBytes {};
    std::uint8_t result = 0;
    bool inputAvailable = false, returned = false, unwound = false, member = false;
};
// Independent callwise native gate witness, never enrollment authority or BOX
// completeness. result is the first actual AL (including non-Boolean values).
// available describes captured hook coverage; complete additionally requires
// exactly one returned call and an unambiguous, stable enclosing invocation.
// Selected TLS/caller attribution assumes the production lease callback does
// not bypass HookedUpdate to invoke the native update/predicate directly.
struct EventGateObservation {
    std::uint64_t coverageSerial = 0;
    std::uint32_t calls = 0;
    std::uint8_t result = 0;
    bool requested = false, available = false, returned = false, unwound = false;
    bool nested = false, overflow = false, complete = false;
};
// Additive thread-bracket observation only. TLS does not establish fiber/task
// continuity, and none of these fields grants original-phase eligibility.
enum class UpdatePhase : std::uint8_t { None, RunUpdateWork, InsideOriginalCall };
enum class OriginalCallSource : std::uint8_t {
    None, RoleOffPassThrough, UnverifiedCallerPassThrough, UnqualifiedControllerPassThrough,
    HostPassThrough, RoleChangedBeforeSelection, RoleChangedAfterFirstCopy, ClientLeaseApply
};
enum class OriginalHold : std::uint8_t { None, FirstLeaseOrLifecycle, FinalLeaseOrLifecycle };
struct OriginalPhaseSample {
    std::uint64_t invocation = 0, updateSequence = 0;
    uintptr_t controller = 0, point = 0, caller = 0, callerRva = 0;
    UpdatePhase phase = UpdatePhase::None;
    OriginalCallSource source = OriginalCallSource::None;
    bool available = false, callerRvaAvailable = false;
};
struct OriginalPhaseObservation {
    std::uint64_t coverageSerial = 0, invocation = 0, updateSequence = 0;
    uintptr_t controller = 0, updateCaller = 0, updateCallerRva = 0, originalPoint = 0;
    OriginalPhaseSample gateEntry {}, gateReturn {};
    std::uint32_t originalEntries = 0, originalReturns = 0, originalUnwinds = 0;
    OriginalCallSource source = OriginalCallSource::None;
    OriginalHold hold = OriginalHold::None;
    bool requested = false, available = false, updateCallerRvaAvailable = false;
    bool overflow = false, mismatch = false, unwound = false, threadPhaseConsistent = false;
    bool fiberContinuityProven = false, originalPhaseEligibility = false;
};
struct GeometryObservation {
    EventGateObservation eventGate {};
    OriginalPhaseObservation originalPhase {};
    std::uint64_t coverageSerial = 0, startedMs = 0;
    std::uint32_t ordinal = 0;
    std::uint32_t tableCount = 0, tableIndex = 0;
    std::array<std::uint8_t, 16> tableEntry {};
    std::array<std::uint8_t, 0x40> controllerBytes {};
    std::array<std::uint8_t, 0x2C> header {};
    std::array<std::uint8_t, 5 * 0x40> records {};
    std::array<std::uint8_t, 7 * 0x40> descriptors {};
    std::array<uintptr_t, 7> regions {};
    std::array<std::array<std::uint8_t, 0x70>, 7> regionBytes {};
    std::array<GeometryPredicate, 7> predicates {};
    std::uint32_t calls = 0;
    bool definitionAvailable = false, definitionStable = false, originalReturned = false;
    bool overflow = false, nested = false, unwound = false, complete = false;
};
enum class TraceKind : std::uint8_t { Wrapper, Geometry };
struct TraceEvent {
    TraceKind kind = TraceKind::Wrapper;
    GeometryObservation geometry {};
    std::uint64_t sequence = 0, tickSequence = 0, dispatcherSequence = 0, scriptSequence = 0;
    TraceWrapper wrapper = TraceWrapper::Fixed;
    uintptr_t callerRva = 0, dispatcherCallerRva = 0, dispatcherRegion = 0, scriptCallerRva = 0;
    uintptr_t controller = 0, record = 0, actor = 0, objentry = 0, status = 0;
    uintptr_t actorController = 0, actorRecord = 0;
    std::uint32_t objectId = 0, actorObjectId = 0;
    std::int32_t hp = 0, maxHp = 0;
    std::uint16_t recordIndex = 0, nativeRecordId = 0;
    std::uint8_t actorType = 0, role = 0;
    std::array<std::uint8_t, 64> recordBytes {};
    std::array<float, 4> generatedPoint {};
    TraceStamp stamp {}, postStamp {};
    TraceState tickBefore {}, tickAfter {};
    // These surround the wrapper itself, BEFORE caller-owned cooldown/stage
    // processing. They never substitute for an unavailable enclosing tick.
    TraceState wrapperBefore {}, wrapperAfter {};
    FactoryPredicates factory {};
    TraceOutcome outcome = TraceOutcome::Unavailable;
    TraceOutcome wrapperOutcome = TraceOutcome::Unavailable;
    const char* reason = "not sampled"; // static literals only; never native pointers
    bool stampAvailable = false, postStampAvailable = false;
    bool enclosingTick = false, tickComplete = false, lifecycleStable = false;
    bool recordAvailable = false, actorAvailable = false;
    bool recordIndexAvailable = false, objectIdMatchesRecord = false;
    bool callerRvaAvailable = false, dispatcherCallerRvaAvailable = false;
    bool enclosingDispatcher = false, generatedPointAvailable = false;
    bool wrapperComplete = false;
    bool enclosingScript42DC10 = false, scriptCallerRvaAvailable = false;
    bool roleAvailable = false;
};
struct TraceStats {
    std::uint64_t started = 0, published = 0, dropped = 0;
    std::uint64_t unsupportedCaller = 0, unavailable = 0, nativeFaults = 0;
    std::uint32_t lastNativeException = 0;
    bool requested = false, available = false;
    bool fixedAvailable = false, generatedAvailable = false, dispatcherAvailable = false;
    bool scriptAvailable = false;
    bool constructionRequested = false, constructionConfigured = false;
    std::uint64_t constructionCoverage = 0, constructionSerial = 0;
    std::uint64_t constructionPublished = 0, constructionDropped = 0;
    std::uint32_t factoryVerifiedMask = 0, factoryInstalledMask = 0, factoryFailedMask = 0;
    std::uint64_t factoryForeignScopes = 0, factoryUnwoundScopes = 0, factoryAmbiguousScopes = 0;
    bool geometryRequested = false, geometryConfigured = false, geometryVerified = false, geometryInstalled = false;
    bool geometryPriorObserved = false;
    GeometryTerminal geometryTerminal = GeometryTerminal::Disabled;
    std::uint32_t geometryLoad = 0, geometryTransition = 0, geometryTicks = 0;
    std::array<std::uint8_t, 10> geometryLocation {};
    std::uint64_t geometryCoverageSerial = 0, geometryStartedMs = 0;
    std::uint64_t geometryDropped = 0, geometryUnwound = 0, geometryForeign = 0;
    bool enrollmentRequested = false, enrollmentConfigured = false;
    bool eventGateVerified = false, eventGateInstalled = false, eventGateFailed = false;
    std::uint64_t eventGateCoverageSerial = 0, eventGateForeign = 0;
    std::uint64_t eventGateUnwound = 0, eventGateDropped = 0;
};

// Checked read-only snapshots, usable independently of hook installation.
// No pointers or results are retained. Availability remains explicit in POD.
// The known head-actor frame owner registers once. Foreign callers cannot
// replace that thread, and diagnostic callbacks must first check affinity.
void RegisterDiagnosticGameThread();
bool IsDiagnosticGameThread();
bool CaptureDiagnosticStamp(uintptr_t exeBase, TraceStamp& out);
TraceState CaptureDiagnosticState(uintptr_t exeBase, uintptr_t controller);

// Destructive local diagnostic drain, called by the existing game-thread census
// owner. It neither reads native memory nor performs any world/network mutation.
bool PopTraceEvent(TraceEvent& event);
TraceStats GetTraceStats();

// MinHook must already be initialized. No network polling or actor writes here.
bool Install(uintptr_t exeBase, LogFn log, RoleFn role, CaptureFn capture, CopyFn copy,
             bool trace = false);
void Shutdown();

} // namespace kh2coop::inject::spawncontroller
