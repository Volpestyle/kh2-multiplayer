#pragma once
#include <cstdint>

// ==========================================================================
// KH2 Final Mix PC — Memory Offset Map
//
// Target build: Epic Games Store (EOSSDK-Win64-Shipping.dll)
// All offsets are relative to the KH2 executable base address unless noted.
//
// STATUS KEY:
//   [KNOWN]    — confirmed by OpenKH / community RE work
//   [PROBE]    — needs live verification with Cheat Engine or similar
//   [UNKNOWN]  — not yet identified, requires RE work
//
// HOW TO FILL THESE IN:
//   1. Launch KH2 FM on PC.
//   2. Attach Cheat Engine to the process.
//   3. For each [PROBE] offset:
//      a. Search for the expected value (e.g. Sora's HP = 60 at start).
//      b. Narrow down by changing the value in-game and re-scanning.
//      c. Record the address relative to the exe base.
//      d. Verify it survives room transitions.
//   4. For pointer chains: follow each level of indirection.
//   5. Update this file and rebuild.
// ==========================================================================

namespace kh2coop {
namespace offsets {

// --------------------------------------------------------------------------
// Global state
// --------------------------------------------------------------------------

// World and room identifiers.
// [KNOWN] — OpenKH references these in save/state reading.
constexpr std::uint64_t WORLD_ID       = 0x0714DB8;  // uint8  [PROBE]
constexpr std::uint64_t ROOM_ID        = 0x0714DB9;  // uint8  [PROBE]
constexpr std::uint64_t MAP_PROGRAM    = 0x0714DBA;  // uint16 [PROBE]
constexpr std::uint64_t BATTLE_PROGRAM = 0x0714DBC;  // uint16 [PROBE]
constexpr std::uint64_t EVENT_PROGRAM  = 0x0714DBE;  // uint16 [PROBE]

// Transition / cutscene flags.
constexpr std::uint64_t IN_TRANSITION  = 0x0;  // bool [UNKNOWN]
constexpr std::uint64_t IN_CUTSCENE    = 0x0;  // bool [UNKNOWN]

// --------------------------------------------------------------------------
// Actor slots — the party system
//
// KH2 maintains a "party" table. Slot 0 = PLAYER, Slot 1 = FRIEND_1,
// Slot 2 = FRIEND_2. Each slot is an entry in an actor/entity array.
//
// The base pointer to the entity table needs to be found first. Then
// each actor is at: BASE + (slot * ACTOR_STRIDE).
// --------------------------------------------------------------------------

// Pointer to the actor/entity table base.
// This is typically a static pointer -> dynamic allocation.
constexpr std::uint64_t ACTOR_TABLE_PTR  = 0x0;  // ptr [UNKNOWN]
constexpr std::uint64_t ACTOR_STRIDE     = 0x0;  // [UNKNOWN] bytes per actor entry

// Offsets within a single actor entry:
namespace actor {
    constexpr std::uint64_t POS_X       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t POS_Y       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t POS_Z       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t ROT_Y       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t VEL_X       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t VEL_Y       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t VEL_Z       = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t MOTION_ID   = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t ACTION      = 0x0;  // uint16 [UNKNOWN]
    constexpr std::uint64_t COMBO_STEP  = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t HP          = 0x0;  // int32 [UNKNOWN]
    constexpr std::uint64_t MAX_HP      = 0x0;  // int32 [UNKNOWN]
    constexpr std::uint64_t MP          = 0x0;  // int32 [UNKNOWN]
    constexpr std::uint64_t MAX_MP      = 0x0;  // int32 [UNKNOWN]
    constexpr std::uint64_t DRIVE       = 0x0;  // int32 [UNKNOWN]
    constexpr std::uint64_t TARGET_ID   = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t FLAGS       = 0x0;  // uint32 bitfield [UNKNOWN]
    // Flag bits within FLAGS (if bitfield):
    constexpr std::uint32_t FLAG_AIRBORNE  = 0x0;  // [UNKNOWN]
    constexpr std::uint32_t FLAG_INVULN    = 0x0;  // [UNKNOWN]
    constexpr std::uint32_t FLAG_STAGGER   = 0x0;  // [UNKNOWN]
    constexpr std::uint32_t FLAG_DOWNED    = 0x0;  // [UNKNOWN]
}

// --------------------------------------------------------------------------
// Camera
// --------------------------------------------------------------------------

// Pointer to camera state structure.
constexpr std::uint64_t CAMERA_PTR         = 0x0;  // ptr [UNKNOWN]

namespace camera {
    constexpr std::uint64_t TARGET_ENTITY  = 0x0;  // ptr to followed actor [UNKNOWN]
    constexpr std::uint64_t MODE           = 0x0;  // uint32 camera mode [UNKNOWN]
    constexpr std::uint64_t FORCED_FLAG    = 0x0;  // bool cinematic override [UNKNOWN]
}

// --------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------

// Raw input buffer address — where KH2 reads pad state each frame.
constexpr std::uint64_t INPUT_BUFFER       = 0x0;  // [UNKNOWN]

namespace input {
    constexpr std::uint64_t BUTTONS        = 0x0;  // uint32 button bitfield [UNKNOWN]
    constexpr std::uint64_t LEFT_STICK_X   = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t LEFT_STICK_Y   = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t RIGHT_STICK_X  = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t RIGHT_STICK_Y  = 0x0;  // float [UNKNOWN]
}

// --------------------------------------------------------------------------
// Enemy list
// --------------------------------------------------------------------------

// Pointer to the enemy/entity list.
constexpr std::uint64_t ENEMY_LIST_PTR     = 0x0;  // ptr [UNKNOWN]
constexpr std::uint64_t ENEMY_COUNT        = 0x0;  // uint32 [UNKNOWN]
constexpr std::uint64_t ENEMY_STRIDE       = 0x0;  // bytes per enemy [UNKNOWN]

namespace enemy {
    constexpr std::uint64_t OBJECT_ID      = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t SPAWN_GROUP_ID = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t POS_X          = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t POS_Y          = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t POS_Z          = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t ROT_Y          = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t MOTION_ID      = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t HP             = 0x0;  // int32 [UNKNOWN]
    constexpr std::uint64_t TARGET_ACTOR   = 0x0;  // uint32 [UNKNOWN]
    constexpr std::uint64_t ALIVE_FLAG     = 0x0;  // bool [UNKNOWN]
}

} // namespace offsets
} // namespace kh2coop
