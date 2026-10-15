#pragma once
#include "kh2coop/Types.hpp"
#include "kh2coop/EnemyRecordKey.hpp"
#include "kh2coop/CoopDifficulty.hpp"
#include <optional>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kh2coop {

inline constexpr std::uint16_t PROTOCOL_VERSION = 21;

// ===========================================================================
// Protocol v21 adds FieldObjects (51): complete periodic host field-object snapshots (VUH-2276).
// Protocol v20 adds SpawnEmission (50): host controller records and trigger points (VUH-2302).
// Protocol v19 adds ScriptCall (49): the host's mirrored enemies' script calls (VUH-2252).
// Protocol v18 adds host co-op difficulty to ClientHello/SessionState and
// the locked player count to the RoomTransition packet trailer.
// Protocol v17 adds distinct non-death membership retirement in PopulationCut schema2.
// Protocol v16 adds bounded host PopulationCut (48) and occurrence bootstrap.
// v15 adds optional scoped record identity to EnemyManifest entries.
// v14 adds RemoteHit (45) and TargetAuthority (46) (VUH-1515, enemy targeting of remote players)
// and PartyIntent = 47 (VUH-1786, host intended party for a target room).
// v13 added EnemyMotion (VUH-1515 step 2, periodic host enemy pose/motion).
// v12 added versioned host party layout and reapplication events.
// v11 added streamed downed epoch/episode/delivery and ReviveRequest.
// ===========================================================================

// Network-only owner envelope. AvatarBridge v4 is required for v11 avatar state.
// The relay stamps both this connection identity and avatar.ownerSlot.
struct AvatarRelay {
    std::uint64_t ownerConnectionId {0};
    AvatarState avatar {};
};

struct SessionActor {
    std::uint32_t actorId {0};
    SlotType slot {SlotType::Player};
    std::string ownerPeerId;
    std::string archetype;
    std::uint64_t connectionId {0}; // relay-lifetime identity; wire field follows archetype
};

struct SessionState {
    std::string sessionId;
    std::string gameBuild;
    std::string modHash;
    RoomState room {};
    std::vector<SessionActor> actors;
    CoopDifficulty coopDifficulty{CoopDifficulty::Off};
};

struct ActorSnapshot {
    std::uint32_t snapshotId {0};
    ActorState actor {};
};

struct EnemySnapshot {
    std::uint32_t snapshotId {0};
    EnemyState enemy {};
};

enum class EventType : std::uint16_t {
    Unknown = 0,
    SpawnGroup,
    KillEnemy,
    RewardGranted,
    RoomTransitionBegin,
    RoomTransitionComplete,
    CutsceneBegin,
    CutsceneEnd,
    PlayerKo,
    PlayerRevive,
    ForceTeleport,
    SessionResyncRequired
};

struct EventMessage {
    std::uint32_t snapshotId {0};
    EventType type {EventType::Unknown};
    std::string payloadJson;
};

/// Clock sync: the client stamps its send time; the host echoes it with its
/// own clock so the client can estimate server time (offset + RTT).
struct ClockPing {
    std::uint64_t clientSendMs {0};
};

struct ClockPong {
    std::uint64_t clientSendMs {0};
    std::uint64_t serverMs {0};
};

// Existing HelloReject byte and ENet disconnect data carry these values.
// Unknown nonzero values are terminal; zero alone does not prove a timeout.
enum class DisconnectReason : std::uint32_t {
    TransportLost = 0, Incompatible = 1, AdmissionRejected = 2,
    PeerIdleTimeout = 3, HostSessionEnded = 4, RelayStopping = 5,
    HandshakeTimeout = 6, SlotOccupied = 7, LobbyFull = 8,
    SessionChanged = 9 // locally detected resume-pin mismatch
};

// Relay -> client just before a refused connection is closed, so the client
// can say why (VUH-1492). code matches the ENet disconnect data: 1 = protocol,
// mode or version mismatch, 2 = slot or other refusal.
struct HelloReject {
    std::uint8_t code {0};
    std::string reason;
};

// ===========================================================================
// World sync — host-authored messages (plan D5/D7, VUH-1496/1502/1503).
//
// The host is the peer in SlotType::Player. The relay accepts these only
// from the host and forwards them to everyone else; HitClaim goes the other
// way (any client -> host only). `epoch` increments on every host room
// transition so stale messages from a previous room are recognizable.
// ===========================================================================

struct RoomTransition {
    std::uint32_t epoch {0};
    std::uint16_t worldId {0};
    std::uint16_t roomId {0};
    std::uint16_t door {0};          // entrance / spawn id
    std::uint16_t mapProgram {0};
    std::uint16_t battleProgram {0};
    std::uint16_t eventProgram {0};
    // VUH-1515 spawn-pick follow-up (shared bit): whether the HOST's own load of this room made every
    // random spawn pick through the shared path, and the low 32 bits of the salt it used. Carried only
    // by the RoomTransition packet (writeRoomTransitionPacket); location comparisons and every embedded
    // RoomTransition (activation, resync, party) ignore both, and the generic codec does not write them.
    std::uint8_t spawnPickShared {0};
    std::uint32_t spawnPickSaltTag {0};
    // Packet-only: host roster count locked for this native room, including late joins.
    std::uint8_t difficultyPlayers {1};
};

// One successful native wrapper return, not permission to run a controller's
// other records. Sequence is host-connection scoped; room binds replay lifetime.
struct SpawnEmission {
    RoomTransition location {};
    std::uint64_t sequence {0};
    std::uint32_t controllerKey {0};
    std::uint16_t headerId {0}, recordIndex {0};
    std::array<float, 4> point {};
};
inline constexpr std::size_t SPAWN_EMISSION_CAP = 4096;

// Ephemeral native activation challenge. The requester owns the deadline on
// its monotonic clock; receipt never renews it. A host response must be captured
// AFTER the host consumes this request, never taken from a cached avatar/point.
inline constexpr std::uint64_t ACTIVATION_LEASE_MS = 500;
inline constexpr std::uint64_t ACTIVATION_REQUEST_INTERVAL_MS = 100;
inline constexpr std::size_t ACTIVATION_MAX_OUTSTANDING = 6;
struct ActivationRequest {
    RoomTransition location {};
    std::array<std::uint64_t, 2> incarnation {}; // random client DLL incarnation
    std::uint64_t requestSeq {0};              // never reused within incarnation
    std::uint8_t requesterSlot {0xFF};         // relay overwrites from verified peer
};

struct HostActivationPoint {
    ActivationRequest request {};
    std::uint64_t sourceSeq {0}; // host-native capture sequence, not bridge writes
    std::array<float, 4> position {}; // exact native argument, including w
};

struct TransitionAck {
    std::uint32_t epoch {0};
    std::uint16_t worldId {0};
    std::uint16_t roomId {0};
    bool arrived {false};            // false = load failed / diverged
};

// Host entered (active) or left a cutscene/event; clients hold meanwhile.
struct EventHold {
    std::uint32_t epoch {0};
    bool active {false};
    std::uint16_t eventProgram {0};
};

// Deterministic enemy identity (plan D5): room + battle program + spawn
// entry + object id. Clients match their natively spawned enemies to it.
struct EnemyManifestEntry {
    std::uint16_t netId {0};
    std::uint16_t battleProgram {0};
    std::uint16_t spawnIndex {0};
    std::uint32_t objectId {0};
    Vec3 spawnPosition {};
    std::optional<EnemyRecordKey> recordKey {};
};

struct EnemyManifest {
    std::uint32_t epoch {0};
    bool replace {true};             // false = append (a later wave)
    std::vector<EnemyManifestEntry> entries;
};

struct EnemyHpEntry {
    std::uint16_t netId {0};
    std::int32_t hp {0};             // absolute, never a delta
    std::int32_t maxHp {0};
};

struct EnemyHp {
    std::uint32_t epoch {0};
    std::vector<EnemyHpEntry> entries;
    // Nonzero, nonwrapping producer order; survives room/manifest changes.
    // Kept last for aggregate source compatibility, encoded after epoch.
    std::uint64_t sequence {0};
};

struct EnemyDeath {
    std::uint32_t epoch {0};
    std::uint16_t netId {0};
    // VUH-2272: 1 = the host's enemy left the list alive (escaped or faded), not
    // a kill. Clients remove their copy without kill credit, so their native
    // spawn controller doesn't advance a wave the host's never advanced. Encoded
    // only when set, so a kill keeps the original 6-byte payload.
    std::uint8_t left {0};
};

// VUH-1515 step 2: host pose/motion for bound, allowlisted, living enemies.
// Periodic and unreliable (stale motion is worthless). Not material world
// state: never cached by the relay, never part of resync or StateHash.
inline constexpr std::size_t ENEMY_MOTION_MAX_ENTRIES = 32;
inline constexpr std::size_t ENEMY_MOTION_ENTRY_BYTES = 31;
inline constexpr std::size_t ENEMY_MOTION_HEADER_BYTES = 18; // epoch, sequence, hostFrame, count
inline constexpr std::uint8_t ENEMY_MOTION_ALIVE = 1;
// Sanity bounds (VUH-1515 review S3/S5): a host value outside these is refused,
// never driven into a native call. Motion ids seen live are < 300; 4096 caps garbage.
inline constexpr std::uint32_t ENEMY_MOTION_MAX_MOTION_ID = 4096;
inline constexpr float ENEMY_MOTION_MAX_TIME = 10000.0f;      // frames, [0, max]
inline constexpr float ENEMY_MOTION_MAX_COORD = 100000.0f;    // |x|, |y|, |z|
inline constexpr float ENEMY_MOTION_MAX_ROTATION = 64.0f;     // |rotationY| radians
struct EnemyMotionEntry {
    std::uint16_t netId {0};          // bound host spawn (spawn index + 1)
    std::uint32_t objectId {0};       // native objentry id, type-checked by clients
    std::uint32_t motionId {0};       // actor+0x180
    float motionTime {0.0f};          // actor+0x19C, frames
    Vec3 position {};                 // entity+0x30
    float rotationY {0.0f};           // entity+0x4C
    std::uint8_t flags {ENEMY_MOTION_ALIVE}; // bit0 alive; other bits must be zero
};
struct EnemyMotion {
    std::uint32_t epoch {0};
    std::uint64_t sequence {0};       // nonzero, strictly increasing per producer lifetime
    std::uint32_t hostFrame {0};      // host DLL frame counter at capture
    std::vector<EnemyMotionEntry> entries;
};

// Complete periodic host field roster, independently named from enemies. Never
// cached/replayed as world state or included in resync, HP or population authority.
inline constexpr std::size_t FIELD_OBJECT_MAX_ENTRIES = 64;
inline constexpr std::size_t FIELD_OBJECT_ENTRY_BYTES = 44;
inline constexpr std::size_t FIELD_OBJECT_HEADER_BYTES = 30; // location16, sequence8, frame4, count2
inline constexpr std::uint32_t FIELD_OBJECT_NO_MOTION = 0xFFFFFFFFu;
inline constexpr bool isFieldObjectNetId(std::uint16_t id) { return id > 0x8000u; }
struct FieldObjectEntry {
    std::uint16_t netId {0};
    std::uint32_t objectId {0};
    std::uint8_t objectType {0};
    Vec3 anchor {};
    Vec3 position {};
    float rotationY {0.0f};
    std::uint32_t motionId {FIELD_OBJECT_NO_MOTION};
    float motionTime {0.0f};
    bool present {true}; // false retains a retired occurrence anchor for late native binding
};
struct FieldObjects {
    RoomTransition location {};
    std::uint64_t sequence {0};
    std::uint32_t hostFrame {0};
    std::vector<FieldObjectEntry> entries; // 0..64, complete known room roster including retired occurrences
};

// Reliable, scoped teammate request. Relay stamps requesterSlot and forwards
// once to targetConnectionId only. Episode is minted by the target owner,
// strictly increasing for that game lifetime, never an HP write authority.
struct ReviveRequest {
    RoomTransition location{};
    std::uint64_t seq{0};
    std::uint64_t requesterConnectionId{0};
    std::uint64_t targetConnectionId{0};
    std::uint64_t targetEpisode{0};
    std::uint8_t requesterSlot{0xFF};
    std::uint8_t targetSlot{0xFF};
};
// Reliable, scoped host report (VUH-1515): an enemy on the host hit the
// host-side native clone of targetSlot's player. The relay validates it
// against the admitted room, roster, arrival and current EnemyManifest and
// forwards it once to targetConnectionId only; the target owner applies the
// damage to its own real player. Wire order is field order: 55 payload bytes.
// seq is host-minted, nonzero and strictly increasing within the host connection.
struct RemoteHit {
    RoomTransition location{};          // host's admitted room tuple
    std::uint64_t seq{0};
    std::uint64_t hostConnectionId{0};  // host's roster identity at detection
    std::uint64_t targetConnectionId{0};
    std::uint8_t targetSlot{0xFF};      // 1 or 2
    std::uint16_t netId{0};             // host enemy identity from the current EnemyManifest
    std::uint32_t objectId{0};
    std::uint32_t attackId{0};          // atkp entry id
    std::int32_t damage{0};             // HP to apply, > 0
};
inline constexpr std::uint16_t REMOTE_HIT_PAYLOAD_SIZE = 55;
inline constexpr std::int32_t REMOTE_HIT_MAX_DAMAGE = 9999;
// Reliable, scoped, periodic host statement (VUH-1515): which remote players'
// host clones the host currently targets and covers, per enemy family. The
// relay validates it against the admitted room and forwards it to every
// verified client; it is never cached or replayed to late joiners. Wire order
// is field order: 38 payload bytes. seq is host-minted, nonzero and strictly
// increasing within the host connection.
struct TargetAuthority {
    RoomTransition location{};          // host's admitted room tuple
    std::uint64_t seq{0};
    std::uint64_t hostConnectionId{0};
    std::uint8_t slotMask{0};           // bit k (k = 1, 2): host targets and covers slot k's clone
    std::uint32_t familyMask{0};        // bit 0 = Shadow objectId 302; other bits must be 0 for now
    std::uint8_t mode{0};               // TargetAuthorityMode
};
enum class TargetAuthorityMode : std::uint8_t {
    Forward = 0, // host forwards RemoteHit; owner cancels the local family hit
    Mirror = 1,  // target choice only
};
inline constexpr std::uint16_t TARGET_AUTHORITY_PAYLOAD_SIZE = 38;
inline constexpr std::uint8_t TARGET_AUTHORITY_SLOT_BITS = 0x06;
inline constexpr std::uint32_t TARGET_AUTHORITY_FAMILY_BITS = 0x1u;
inline constexpr std::uint8_t TARGET_AUTHORITY_MAX_MODE = 1;

// Reliable, scoped host report (VUH-2252, D14 item 3): script calls a host
// mirrored enemy's script made on that enemy, sent only when the value changed.
// A client runs no brain for a mirrored enemy, so without these a tilt, a
// texture animation or a shadow set by its script never shows there. The
// relay checks host, room and sequence and forwards it to every verified
// client; never cached, never replayed to late joiners.
// args are the call's values after the actor, as bdscript tags them: high 32
// bits '@INT' or '@FLT', low 32 bits the value. A vector argument travels as
// its four floats. Only the traps scriptCallShape names are accepted, and
// unused args must be zero. effect is the host-minted id of an effect a
// mirrored enemy's script started: nonzero for exactly the effect calls, zero
// for every other call. Wire order is field order: 37 + 45 * n bytes.
inline constexpr std::uint32_t SCRIPT_TAG_INT = 0x544E4940;   // '@INT'
inline constexpr std::uint32_t SCRIPT_TAG_FLOAT = 0x544C4640; // '@FLT'
inline constexpr float SCRIPT_CALL_MAX_FLOAT = 100000.0f;
enum class ScriptCallKind : std::uint8_t { None, Scalars, Vector4 };
struct ScriptCallShape {
    ScriptCallKind kind{ScriptCallKind::None};
    std::uint8_t argc{0};
    bool effect{false};            // the entry names an effect id
};
// bdscript trap table/index (OpenKH BdxTraps.cs names; tables at exe+0x753490).
// obj_effect_start_pos (1/22) travels as a start (1/21) and a set_pos (0/38);
// effect_set_dir (0/41) as the set_rot (0/40) it results in.
inline constexpr ScriptCallShape scriptCallShape(std::uint8_t table, std::uint16_t index) {
    using K = ScriptCallKind;
    return table == 1 && index == 1 ? ScriptCallShape{K::Scalars, 1}     // trap_obj_set_rot (float)
         : table == 8 && index == 2 ? ScriptCallShape{K::Vector4, 4}     // trap_obj_set_xyzrot
         : table == 1 && (index == 85 || index == 86) ? ScriptCallShape{K::Scalars, 1} // texanm start/stop
         : table == 6 && (index == 45 || index == 64) ? ScriptCallShape{K::Scalars, 0} // hide/show shadow
         : table == 6 && index == 65 ? ScriptCallShape{K::Scalars, 1}    // trap_obj_set_scissoring
         : table == 1 && (index == 21 || index == 87) ? ScriptCallShape{K::Scalars, 3, true} // obj_effect_start / _bind
         : table == 0 && (index == 38 || index == 39 || index == 40 || index == 67) ? ScriptCallShape{K::Vector4, 4, true} // effect pos/scale/rot/color
         : table == 0 && index == 66 ? ScriptCallShape{K::Scalars, 1, true} // trap_effect_pause
         : table == 0 && (index == 44 || index == 51 || index == 85) ? ScriptCallShape{K::Scalars, 0, true} // loop_end / kill / loop_end_kill
         : table == 0 && index == 52 ? ScriptCallShape{K::Scalars, 2, true} // trap_effect_fadeout (time, mode)
         : table == 1 && index == 349 ? ScriptCallShape{K::Scalars, 0}   // trap_obj_effect_kill_all
         : ScriptCallShape{};
}
struct ScriptCallEntry {
    std::uint16_t netId{0};        // host enemy occurrence or high-bit field occurrence the call acted on
    std::uint32_t objectId{0};     // its objentry id, checked by clients
    std::uint8_t table{0};
    std::uint16_t index{0};
    std::uint32_t effect{0};       // host effect id for the effect calls; 0 otherwise
    std::array<std::uint64_t, 4> args{};
};
struct ScriptCall {
    RoomTransition location{};     // host's admitted room tuple
    std::uint64_t seq{0};          // nonzero, strictly increasing within the host connection
    std::uint64_t hostConnectionId{0};
    std::uint32_t hostFrame{0};
    std::vector<ScriptCallEntry> entries; // 1..SCRIPT_CALL_MAX_ENTRIES, in call order
};
inline constexpr std::size_t SCRIPT_CALL_MAX_ENTRIES = 32;
inline constexpr std::uint16_t SCRIPT_CALL_HEADER_BYTES = 37;
inline constexpr std::uint16_t SCRIPT_CALL_ENTRY_BYTES = 45;
// Every arg the shape uses is tagged and finite (floats within the bound); unused args are zero.
bool validScriptCallEntry(const ScriptCallEntry& e);
inline constexpr float REVIVE_RANGE = 200.0f; // native world units; initial policy
inline constexpr std::uint64_t REVIVE_AVATAR_MAX_AGE_MS = 1000;

// A client's hit on a replica enemy (plan D4). attackerSlot is stamped by
// the relay. Wire order: epoch, seq, netId, objectId, requesterConnectionId,
// attackId, damage, attackerPosition (x/y/z), attackerSlot: 43 payload bytes.
// seq is nonzero and strictly increases across rooms within one connection;
// it cannot wrap. A new connection ID permits restarting the sequence.
struct HitClaim {
    std::uint32_t epoch {0};
    std::uint32_t seq {0};
    std::uint16_t netId {0};
    std::uint32_t objectId {0};
    std::uint64_t requesterConnectionId {0}; // echo the roster identity at detection
    std::uint32_t attackId {0};      // atkp entry
    std::int32_t damage {0};
    Vec3 attackerPosition {};
    SlotType attackerSlot {SlotType::Player};
};

// Host story/world flags (plan D6, VUH-1495/1497): bytes of the save body
// inside an allow list. `full` replaces everything the receiver holds.
struct ProgressSpan {
    std::uint32_t offset {0};
    std::vector<std::uint8_t> bytes;
};

struct ProgressUpdate {
    std::uint32_t version {0};
    bool full {false};
    std::vector<ProgressSpan> spans;
};

// Desync detection and resync (VUH-1508). Every machine (host included)
// sends StateHash periodically; the relay compares each client with the
// host for the same epoch and, when a mismatch persists, tells everyone.
enum DesyncField : std::uint8_t {
    DesyncRoom = 1 << 0,
    DesyncEnemies = 1 << 1,
    DesyncProgress = 1 << 2,
    DesyncMissingEnemies = 1 << 3,  // complete same-room host living > 0, client living == 0
};

struct StateHash {
    std::uint32_t epoch {0};
    std::uint16_t worldId {0};
    std::uint16_t roomId {0};
    std::uint32_t enemiesHash {0};   // actual live (netId, objectId, HP), including unmatched actors
    std::uint32_t progressHash {0};  // actual local SAVE bytes/bits inside the progress allow list
    // Protocol 10: summary of the same checked native living rows as enemiesHash.
    // Defaults mean unavailable, never evidence of a complete empty census.
    bool nativeCensusComplete {false};
    std::uint32_t nativeLivingCount {0};
    std::uint32_t nativeCombatCount {0}; // includes dead/dying native combat rows
};

struct DesyncNotice {
    SlotType slot {SlotType::Player}; // the diverged client
    std::uint32_t epoch {0};
    std::uint8_t fields {0};          // DesyncField bits
};

// Host-only protocol8 immutable forced native resync request (see ResyncProtocol).
struct ResyncRequest; // protocol8 definition in ResyncProtocol.hpp

// ===========================================================================
// Protocol v2 forward-looking records (declared, not yet wired into codec)
//
// These types support both CampaignCoop and PublicRealm modes.
// They will be integrated into the codec and wire format as Track B/C work
// progresses. Defined now so that refactors can reference stable types.
//
// See docs/kh2_realm_protocol_sketch.jsonc for the full schema sketch.
// ===========================================================================

/// Extended handshake replacing the overloaded SessionState-as-hello pattern.
struct ClientHello {
    std::uint16_t protocolVersion {PROTOCOL_VERSION};
    std::string gameBuild;
    std::string contentHash;
    std::string modHash;
    PeerId peerId;
    std::string peerName;
    RuntimeMode requestedMode {RuntimeMode::CampaignCoop};
    /// CampaignCoop: which party slot this client requests (0=Player, 1=Friend1, 2=Friend2).
    /// 0xFF = no preference (server assigns next free slot).
    std::uint8_t requestedSlot {0xFF};
    CoopDifficulty coopDifficulty{CoopDifficulty::Off}; // only the admitted host's request is used
};

/// Realm seed — derived from a KH2 save file import.
/// Defines what content is accessible in a public realm.
struct RealmSeed {
    RealmId realmId;
    std::string sourceSaveChecksum;
    std::string buildHash;
    std::string contentHash;
    std::vector<std::uint16_t> unlockedWorlds;
    std::vector<std::string> unlockedWarpPoints;
    std::vector<std::uint32_t> storyFlags;
    std::vector<std::uint32_t> clearedBossFlags;
};

/// Persistent character record — owned by a peer, persisted by the realm.
struct CharacterRecord {
    CharacterId characterId;
    PeerId ownerPeerId;
    std::string displayName;
    std::string archetypeId;
    std::uint16_t level {1};
    std::vector<std::string> equipment;
    std::vector<std::string> abilities;
    std::int32_t hp {0};
    std::int32_t mp {0};
    std::int32_t drive {0};
};

/// Describes a running instance (room/encounter) in the realm.
struct InstanceDescriptor {
    InstanceId instanceId;
    InstanceType instanceType {InstanceType::CampaignPartyInstance};
    AuthorityType authorityType {AuthorityType::HostClient};
    PeerId authorityPeerId;
    RealmId realmId;
    std::uint16_t worldId {0};
    std::uint16_t roomId {0};
    std::uint16_t mapProgram {0};
    std::uint16_t battleProgram {0};
    std::uint16_t eventProgram {0};
    std::uint16_t maxPlayers {3};
    bool pvpEnabled {false};
};

/// Binds a network actor to a character, role, and optional KH2-native slot.
struct ActorBinding {
    ActorNetId actorNetId {0};
    CharacterId characterId;
    PeerId ownerPeerId;
    NativeRole nativeRole {NativeRole::RemoteReplica};
    /// SlotType mapping — only meaningful inside CampaignCoop instances.
    std::uint8_t slotType {0xFF}; // 0xFF = unassigned
    std::string archetypeId;
    bool localOnly {false};
};

/// Party record for public-realm party management.
struct PartyRecord {
    PartyId partyId;
    CharacterId leaderCharacterId;
    std::vector<CharacterId> memberCharacterIds;
    RuntimeMode partyMode {RuntimeMode::CampaignCoop};
    InstanceId activeInstanceId; // empty = not in an instance
};

} // namespace kh2coop
