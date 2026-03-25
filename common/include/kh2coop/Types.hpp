#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace kh2coop {

// ---------------------------------------------------------------------------
// Stable identity types (used across both runtime modes)
// ---------------------------------------------------------------------------

using PeerId = std::string;
using CharacterId = std::string;
using PartyId = std::string;
using RealmId = std::string;
using InstanceId = std::string;
using ActorNetId = std::uint32_t;

// ---------------------------------------------------------------------------
// Runtime mode — determines which actor model and session shape to use.
// CampaignCoop: 3-slot canonical party, host KH2 save is authority.
// PublicRealm:  persistent characters, realm service, instanced rooms.
// ---------------------------------------------------------------------------

enum class RuntimeMode : std::uint8_t {
    CampaignCoop = 0,
    PublicRealm = 1
};

// ---------------------------------------------------------------------------
// Instance types — what kind of gameplay room is active.
// ---------------------------------------------------------------------------

enum class InstanceType : std::uint8_t {
    CampaignPartyInstance = 0,
    PublicHubInstance = 1,
    AdventureInstance = 2,
    PvpArenaInstance = 3
};

// ---------------------------------------------------------------------------
// Authority model for an instance.
// ---------------------------------------------------------------------------

enum class AuthorityType : std::uint8_t {
    HostClient = 0,
    DedicatedWorker = 1
};

// ---------------------------------------------------------------------------
// Native role — how an actor maps to the local KH2 engine.
// CampaignCoop uses LocalPrimary / LocalCompanion1 / LocalCompanion2.
// PublicRealm uses LocalPrimary + RemoteReplica for other humans.
// ---------------------------------------------------------------------------

enum class NativeRole : std::uint8_t {
    LocalPrimary = 0,
    LocalCompanion1 = 1,
    LocalCompanion2 = 2,
    RemoteReplica = 3,
    RemoteEnemy = 4,
    RemoteNpc = 5
};

// ---------------------------------------------------------------------------
// Canonical party slots (CampaignCoop actor model)
// ---------------------------------------------------------------------------

enum class SlotType : std::uint8_t {
    Player = 0,
    Friend1 = 1,
    Friend2 = 2
};

enum class ActionState : std::uint16_t {
    Unknown = 0,
    Idle,
    Move,
    Jump,
    Attack,
    Guard,
    Dodge,
    Stagger,
    Downed
};

struct Vec3 {
    float x {0.0f};
    float y {0.0f};
    float z {0.0f};
};

struct ActorState {
    std::uint32_t actorId {0};
    SlotType slot {SlotType::Player};
    Vec3 position {};
    float rotationY {0.0f};
    Vec3 velocity {};
    std::uint32_t motionId {0};
    ActionState action {ActionState::Unknown};
    std::uint32_t comboStep {0};
    std::int32_t hp {0};
    std::int32_t mp {0};
    std::int32_t drive {0};
    std::uint32_t targetId {0};
    bool airborne {false};
    bool invuln {false};
    bool staggered {false};
    bool downed {false};
};

struct EnemyState {
    std::uint32_t netId {0};
    std::uint32_t objectId {0};
    std::uint32_t spawnGroupId {0};
    Vec3 position {};
    float rotationY {0.0f};
    std::uint32_t motionId {0};
    std::int32_t hp {0};
    std::uint32_t targetActorId {0};
    bool alive {true};
};

struct RoomState {
    std::uint32_t worldId {0};
    std::uint32_t roomId {0};
    std::uint32_t mapProgram {0};
    std::uint32_t battleProgram {0};
    std::uint32_t eventProgram {0};
    bool inTransition {false};
    bool inCutscene {false};
};

struct InputButtons {
    bool attack {false};
    bool jump {false};
    bool guard {false};
    bool dodge {false};
    bool lockOn {false};
    bool magic1 {false};
    bool magic2 {false};
    bool special1 {false};
    bool special2 {false};
};

struct InputFrame {
    std::uint32_t seq {0};
    std::uint64_t clientTimeMs {0};
    std::uint32_t ownedActorId {0};
    float leftStickX {0.0f};
    float leftStickY {0.0f};
    float rightStickX {0.0f};
    float rightStickY {0.0f};
    std::uint32_t requestedTargetId {0};
    InputButtons buttons {};
};

} // namespace kh2coop
