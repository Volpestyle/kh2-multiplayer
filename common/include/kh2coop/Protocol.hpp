#pragma once
#include "kh2coop/Types.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace kh2coop {

// ===========================================================================
// Protocol v1 — CampaignCoop session messages (current, wired into codec)
// ===========================================================================

struct SessionActor {
    std::uint32_t actorId {0};
    SlotType slot {SlotType::Player};
    std::string ownerPeerId;
    std::string archetype;
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
};

struct EnemyDeath {
    std::uint32_t epoch {0};
    std::uint16_t netId {0};
};

// A client's hit on a replica enemy (plan D4). attackerSlot is stamped by
// the relay; the host applies the damage natively and broadcasts EnemyHp.
struct HitClaim {
    std::uint32_t epoch {0};
    std::uint32_t seq {0};
    std::uint16_t netId {0};
    std::uint32_t attackId {0};      // atkp entry
    std::int32_t damage {0};
    Vec3 attackerPosition {};
    SlotType attackerSlot {SlotType::Player};
};

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
    std::uint16_t protocolVersion {2};
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
