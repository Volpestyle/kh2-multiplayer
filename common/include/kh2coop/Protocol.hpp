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
