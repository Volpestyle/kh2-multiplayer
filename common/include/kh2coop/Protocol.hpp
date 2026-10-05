#pragma once
#include "kh2coop/Types.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kh2coop {

inline constexpr std::uint16_t PROTOCOL_VERSION = 10;

// ===========================================================================
// Protocol v5 — typed closure/session-incarnation semantics; v4 byte layouts.
// ===========================================================================

// Network-only envelope. AvatarState and its shared-memory layout are unchanged.
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
};

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
};

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
