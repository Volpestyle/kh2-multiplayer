#pragma once
#include "kh2coop/Protocol.hpp"
#include "kh2coop/WorldContext.hpp"
#include "kh2coop/NativeRecordContentTypes.hpp"
#include <array>
#include <optional>

namespace kh2coop {
inline constexpr std::uint32_t RESYNC_TIMEOUT_MS = 30000;
inline constexpr std::size_t RESYNC_MAX_SNAPSHOT_BYTES = 60000;
inline constexpr std::size_t RESYNC_MAX_PART_BYTES = 16384;
inline constexpr std::size_t RESYNC_MAX_ENEMIES = 1024;
inline constexpr std::size_t RESYNC_MAX_CONTINUATION_RECORDS = 256;
inline constexpr std::size_t RESYNC_MAX_CONTINUATION_BYTES = 512 * 1024;
using ResyncDigest = std::array<std::uint8_t,32>;
struct ResyncKey {
    std::string sessionId;
    std::uint64_t hostConnectionId{0}, requestId{0};
    bool operator==(const ResyncKey&) const = default;
};
struct ResyncTarget {
    std::uint8_t slot{0xff};
    std::uint64_t connectionId{0}, deliverySerial{0};
    bool operator==(const ResyncTarget&) const = default;
};
struct WorldBinding {
    std::string sessionId;
    std::uint64_t hostConnectionId{0}, selfConnectionId{0};
    std::uint8_t selfSlot{0xff};
    std::uint64_t deliverySerial{0};
};
enum class WorldSourceKind : std::uint8_t { Native=0, Simulation=1, Relay=2 };
struct WorldScope {
    std::string sessionId;
    std::uint64_t sourceConnectionId{0}, sourceDeliverySerial{0}, hostSourceSerial{0};
    std::uint64_t targetConnectionId{0}, targetDeliverySerial{0};
    WorldSourceKind kind{WorldSourceKind::Native};
};
struct WorldEnvelope { WorldScope scope; std::vector<std::uint8_t> packet; };
enum class ResyncPhase : std::uint8_t { Bootstrap=0, Checkpoint=1 };
enum class ResyncPlanStage : std::uint8_t { CaptureRequested=0, Fenced=1 };
struct ResyncRequest {
    ResyncKey key;
    RoomTransition room;
    std::uint8_t targetMask{0}; // bits 1 and 2 only
    std::array<std::uint64_t,3> connections{};
};
struct ResyncPlan {
    ResyncRequest request;
    std::array<ResyncTarget,2> targets{};
    std::uint8_t targetCount{0};
    std::uint32_t remainingMs{0}; // original relay deadline, never renewed
    ResyncPhase phase{ResyncPhase::Bootstrap};
    ResyncPlanStage stage{ResyncPlanStage::CaptureRequested};
};
struct ResyncBegin {
    ResyncKey key;
    ResyncPhase phase{ResyncPhase::Bootstrap};
    RoomTransition room;
    std::array<ResyncTarget,2> targets{};
    std::uint8_t targetCount{0};
    std::uint64_t snapshotCut{0};
    std::uint32_t totalBytes{0};
    std::uint16_t partCount{0};
    ResyncDigest sha256{};
};
struct ResyncPart {
    ResyncKey key;
    ResyncPhase phase{ResyncPhase::Bootstrap};
    std::uint64_t snapshotCut{0};
    std::uint32_t offset{0};
    std::vector<std::uint8_t> bytes;
};
struct ResyncEnd {
    ResyncKey key;
    ResyncPhase phase{ResyncPhase::Bootstrap};
    std::uint64_t snapshotCut{0};
    std::uint32_t totalBytes{0};
    ResyncDigest sha256{};
};
enum class ResyncLife : std::uint8_t { Alive=0, ObservedDeadHistory=1 };
enum ResyncCoverage : std::uint32_t {
    ResyncRoom=1, ResyncHold=2, ResyncProgress=4, ResyncManifest=8,
    ResyncHp=16, ResyncDeaths=32, ResyncCompleteCensus=64, ResyncComplete=127,
    ResyncRecordContent=128, ResyncNativeComplete=255
};
struct ResyncRecordReference {
    std::uint16_t definitionIndex{UINT16_MAX}, recordIndex{UINT16_MAX};
    bool operator==(const ResyncRecordReference&) const = default;
};
struct ResyncEnemyState {
    EnemyManifestEntry identity;
    std::uint32_t objectType{0};
    std::int32_t hp{0}, maxHp{0};
    ResyncLife life{ResyncLife::Alive};
    ResyncRecordReference record;
};
// Sampled host-native evidence for the bounded all-alive type-2 policy. No
// native addresses, initialization serials, or creation authority cross wire.
struct ResyncActivationState {
    std::uint32_t flags{}, currentCount{}, initialCount{};
    float cooldown{};
    std::uint8_t stage{}, activation{}, nativeType{};
    std::uint16_t headerId{}, recordCount{};
    std::int32_t cacheRoom{}, cacheAge{};
    std::array<std::uint16_t, 256> cacheIds{};
    bool operator==(const ResyncActivationState&) const = default;
};
struct ResyncActivationReplay {
    std::uint16_t definitionIndex{UINT16_MAX};
    std::array<float, 4> point{}; // actual first-emission update input, never geometry
    ResyncActivationState before, current;
    std::uint64_t firstUpdateSequence{};
    std::uint32_t hostTransition{}, hostLoad{};
    bool operator==(const ResyncActivationReplay&) const = default;
};
struct ResyncSnapshot {
    RoomTransition room;
    EventHold hold;
    ProgressUpdate progress;
    std::uint64_t hpSequence{0};
    std::vector<ResyncEnemyState> enemies;
    std::uint32_t livingCount{0}, deadCount{0}, coverageMask{0};
    std::uint32_t generation{0}, transitionSerial{0}, loadSerial{0};
    std::uint64_t captureFrameBefore{0}, captureFrameAfter{0};
    ResyncDigest nativeFingerprint{};
    // Full sampled content, never native pointers/incarnation/creation authority.
    // Empty at coverage127; coverage255 requires a valid reference for every row.
    std::vector<NativeRecordContentDefinition> recordDefinitions;
    // Optional versioned trailer covered by the immutable snapshot SHA, but
    // excluded from canonical native state. Native admission/convergence must
    // separately compare the recorded before/current state at their boundaries.
    std::optional<ResyncActivationReplay> activationReplay;
};
enum class ResyncAckStatus : std::uint8_t { Received=0, Arrived=1, Converged=2, Unavailable=3, Failed=4 };
enum ResyncChecks : std::uint32_t {
    ResyncCheckContext=1, ResyncCheckLoad=2, ResyncCheckFrames=4,
    ResyncCheckCensus=8, ResyncCheckProgress=16, ResyncCheckFingerprint=32,
    ResyncChecksComplete=63
};
struct ResyncAck {
    ResyncKey key;
    ResyncTarget target;
    ResyncPhase phase{ResyncPhase::Bootstrap};
    std::uint64_t snapshotCut{0};
    ResyncDigest snapshotSha256{}, observedFingerprint{};
    ResyncAckStatus status{ResyncAckStatus::Received};
    RoomTransition observedRoom;
    std::uint32_t enemyCount{0}, deadCount{0}, loadBefore{0}, loadAfter{0};
    std::uint64_t observationFrame1{0}, observationFrame2{0};
    std::uint32_t checksMask{0};
    std::string error; // <=256 bytes
};
enum class ResyncResultReason : std::uint8_t {
    Converged=0, Rejected=1, Busy=2, CaptureUnavailable=3, InvalidSnapshot=4,
    Overflow=5, Deadline=6, HostChanged=7, TargetChanged=8, RoomChanged=9,
    NativeUnavailable=10, NativeFailed=11, Cancelled=12
};
struct ResyncTargetResult {
    ResyncTarget target;
    ResyncAckStatus status{ResyncAckStatus::Unavailable};
    std::uint64_t appliedCut{0};
    ResyncDigest fingerprint{};
    std::string error;
};
struct ResyncResult {
    ResyncKey key;
    ResyncResultReason reason{ResyncResultReason::Rejected};
    std::array<ResyncTargetResult,2> targets{};
    std::uint8_t targetCount{0};
};
// Shared bounded staging; no native writes. End returns only a fully validated
// immutable snapshot. Receipt is not native arrival or convergence.
class ResyncAssembler {
public:
    void Reset();
    bool Begin(const ResyncBegin&);
    bool Part(const ResyncPart&);
    std::optional<ResyncSnapshot> End(const ResyncEnd&);
    const std::optional<ResyncBegin>& Header() const { return begin_; }
private:
    std::optional<ResyncBegin> begin_;
    std::vector<std::uint8_t> bytes_;
    std::uint16_t parts_{0};
};
// Exact canonical state only: excludes transport/request/capture stamps and
// progress version and historical activation metadata. SHA of full encoded
// snapshot additionally covers those.
ResyncDigest resyncNativeFingerprint(const ResyncSnapshot&);
bool sameResyncRoom(const RoomTransition&, const RoomTransition&);
} // namespace kh2coop
