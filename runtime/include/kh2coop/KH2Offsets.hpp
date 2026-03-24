#pragma once
#include <cstdint>

// ==========================================================================
// KH2 Final Mix PC — Memory Offset Map
//
// Target build: Steam PC (verified)
// Verified base: process module "KINGDOM HEARTS II FINAL MIX.exe"
// All offsets are relative to the KH2 executable base address unless noted.
//
// STATUS KEY:
//   [CONFIRMED] — verified on live Steam build via Cheat Engine
//   [COMMUNITY] — from KH2FM-Plando-Useful-Codes wiki (PS2 addrs, PC unverified)
//   [PROBE]     — needs live verification with Cheat Engine
//   [UNKNOWN]   — not yet identified, requires RE work
//
// ARCHITECTURE NOTES (learned from live probing session):
//   - Stats (HP, MP, etc.) are at STATIC offsets in the exe module.
//   - Entity/position data is HEAP-ALLOCATED (dynamic memory).
//     Position requires a pointer chain: static ptr -> heap entity struct.
//   - Camera data is separate from both stats and entity data.
//   - The game copies position to many temporary locations (stack, render
//     buffers). The "source" position is the one that teleports the character
//     when written. Stack/temp copies have no effect when changed.
//   - The KH2 modding community's kh2lib uses a "Now" pointer for
//     world/room state and a "Save" pointer for persistent data.
//     These pointer bases are version-specific (EGS vs Steam differ).
//
// COMMUNITY REFERENCE (PS2 addresses from the wiki):
//   World ID:  PS2 0x032BAE0  (byte)
//   Room ID:   PS2 0x032BAE1  (byte)
//   Sora stats: PS2 0x032E020, PC 0x09A9560 (Save+0x24F0)
//   Save base:  PC ~0x09A7070 (= 0x09A9560 - 0x24F0)
//   HP in stat block: offset +0x04 (byte), but live HP is 4-byte int elsewhere
//
// HOW TO CONTINUE PROBING:
//   1. Find kh2lib.dll/lua to get the "Now" and "Save" pointer bases.
//   2. For position: do a pointer scan in Cheat Engine from a known heap
//      position address to find the static pointer chain.
//   3. Verify offsets survive room transitions.
// ==========================================================================

namespace kh2coop {
namespace offsets {

// --------------------------------------------------------------------------
// Global state
// --------------------------------------------------------------------------

// World and room identifiers.
// Community wiki: PS2 0x032BAE0 / 0x032BAE1. PC offset TBD for Steam.
// The Lua modding community accesses these via "kh2lib.Now + 0x00/0x01".
constexpr std::uint64_t WORLD_ID       = 0x0;  // uint8  [UNKNOWN] Steam offset TBD
constexpr std::uint64_t ROOM_ID        = 0x0;  // uint8  [UNKNOWN] Steam offset TBD
constexpr std::uint64_t MAP_PROGRAM    = 0x0;  // uint16 [UNKNOWN]
constexpr std::uint64_t BATTLE_PROGRAM = 0x0;  // uint16 [UNKNOWN]
constexpr std::uint64_t EVENT_PROGRAM  = 0x0;  // uint16 [UNKNOWN]

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
    // NOTE: HP and Max HP below are STATIC addresses (not part of the entity
    // struct on the heap). They are the live runtime HP, separate from the
    // save-data stat block at Save+0x24F0. Writing to these changes the
    // actual in-game HP immediately. These are Sora/Roxas slot 0 only;
    // party member HP addresses are at different static offsets (TBD).
    constexpr std::uint64_t HP          = 0x2A23598;  // int32 [CONFIRMED] Sora/Roxas current HP (Steam)
    constexpr std::uint64_t MAX_HP      = 0x2A2359C;  // int32 [CONFIRMED] Sora/Roxas max HP (Steam)
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
