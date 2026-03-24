#pragma once
#include <cstdint>

// ==========================================================================
// KH2 Final Mix PC — Memory Offset Map
//
// Target build: Steam Global PC
// Verified base: process module "KINGDOM HEARTS II FINAL MIX.exe"
// All offsets are relative to the KH2 executable base address.
//
// Source: KH2-Lua-Library/io_packages/KH2SteamGlobal.lua
//         + live Cheat Engine verification session.
//
// STATUS KEY:
//   [CONFIRMED] — verified on live Steam build via Cheat Engine
//   [KH2LIB]    — from KH2SteamGlobal.lua (trusted, not yet live-verified)
//   [UNKNOWN]   — not yet identified
//
// ARCHITECTURE NOTES:
//   - Unit slot data (HP, MP, equipment, abilities) is at STATIC offsets.
//     Slot 0 starts at 0x2A23518, stride 0x278 per slot.
//     HP = Slot0 + 0x80, MaxHP = Slot0 + 0x84.
//   - Entity/position data is HEAP-ALLOCATED (separate from unit slots).
//     Position requires finding the entity pointer for each slot.
//   - Camera data is in yet another separate region.
//   - The game copies position to many stack/render temps. Only the source
//     position teleports the character when written.
// ==========================================================================

namespace kh2coop {
namespace offsets {

// --------------------------------------------------------------------------
// kh2lib base pointers (Steam Global)
// --------------------------------------------------------------------------
constexpr std::uint64_t NOW            = 0x0717008;   // [CONFIRMED] Current location state
constexpr std::uint64_t SAVE           = 0x09A9830;   // [KH2LIB] Save file base
constexpr std::uint64_t SVE            = 0x2A0C4C0;   // [KH2LIB] Saved location
constexpr std::uint64_t OBJ0_PTR       = 0x2A24FB0;   // [KH2LIB] 00objentry.bin pointer
constexpr std::uint64_t SYS3_PTR       = 0x2AE5DD0;   // [KH2LIB] 03system.bin pointer
constexpr std::uint64_t BTL0_PTR       = 0x2AE5DD8;   // [KH2LIB] 00battle.bin pointer
constexpr std::uint64_t ARD_PTR        = 0x2A0F7A8;   // [KH2LIB] ARD pointer
constexpr std::uint64_t SPAWNS         = 0x2AE5CF8;   // [KH2LIB] Spawn data

// --------------------------------------------------------------------------
// World / room state (at NOW + offset)
// --------------------------------------------------------------------------
constexpr std::uint64_t WORLD_ID       = 0x0717008;   // uint8  [CONFIRMED] NOW+0x00
constexpr std::uint64_t ROOM_ID        = 0x0717009;   // uint8  [CONFIRMED] NOW+0x01
// Map/Battle/Event programs are at NOW+0x04, NOW+0x06, NOW+0x08 (uint16 each)
constexpr std::uint64_t MAP_PROGRAM    = 0x071700C;   // uint16 [KH2LIB] NOW+0x04
constexpr std::uint64_t BATTLE_PROGRAM = 0x071700E;   // uint16 [KH2LIB] NOW+0x06
constexpr std::uint64_t EVENT_PROGRAM  = 0x0717010;   // uint16 [KH2LIB] NOW+0x08

// --------------------------------------------------------------------------
// Game state flags
// --------------------------------------------------------------------------
constexpr std::uint64_t PAUSE_STATUS   = 0x0ABB7F8;   // [KH2LIB] 0=enabled,1=paused,2=disabled,3=menu,4=dying,5=continue
constexpr std::uint64_t BATTLE_STATUS  = 0x2A11384;   // [KH2LIB] Out-of-battle / Regular / Forced
constexpr std::uint64_t BATTLE_END     = 0x2A0FC60;   // [KH2LIB] End-of-battle signal
constexpr std::uint64_t CONTROLLABLE   = 0x2A17168;   // [KH2LIB] Sora controllable flag
constexpr std::uint64_t CUTSCENE_TIMER = 0x0B64F18;   // [KH2LIB] Current cutscene timer
constexpr std::uint64_t CUTSCENE_LEN   = 0x0B64F34;   // [KH2LIB] Cutscene length
constexpr std::uint64_t CUTSCENE_SKIP  = 0x0B64F1C;   // [KH2LIB] Cutscene skip flag
constexpr std::uint64_t GAME_SPEED     = 0x0717424;   // [KH2LIB] Game speed

// --------------------------------------------------------------------------
// Camera
// --------------------------------------------------------------------------
constexpr std::uint64_t CAMERA_TYPE    = 0x0718CA8;   // [KH2LIB] Camera type

// --------------------------------------------------------------------------
// Input
// --------------------------------------------------------------------------
constexpr std::uint64_t INPUT          = 0x0BF3120;   // [KH2LIB] Input state
constexpr std::uint64_t SOFT_RESET     = 0x0ABABDA;   // [KH2LIB] Soft reset trigger

// --------------------------------------------------------------------------
// Music / UI
// --------------------------------------------------------------------------
constexpr std::uint64_t MUSIC          = 0x0ABACC4;   // [KH2LIB] Background music ID
constexpr std::uint64_t REACT_CMD      = 0x2A110E2;   // [KH2LIB] Reaction command
constexpr std::uint64_t TEXTBOX        = 0x074DF20;   // [KH2LIB] Last displayed textbox
constexpr std::uint64_t MENU1          = 0x2A11090;   // [KH2LIB] Main command menu
constexpr std::uint64_t MENU_STRIDE    = 0x8;         // [KH2LIB] Stride between menu slots

// --------------------------------------------------------------------------
// Unit slots — the party/actor stat system
//
// KH2 stores unit data in a static array of slots.
// Slot 0 = PLAYER (Sora/Roxas), Slot 1 = FRIEND_1, Slot 2 = FRIEND_2.
// Each slot is 0x278 bytes. Stats like HP/MP are at fixed offsets within.
//
// NOTE: Position/rotation are NOT in the unit slot. They live in a
// separate heap-allocated entity struct (see entity section below).
// --------------------------------------------------------------------------
constexpr std::uint64_t SLOT0_BASE     = 0x2A23518;   // [KH2LIB] Unit Slot 0 (Sora/Roxas)
constexpr std::uint64_t SLOT_STRIDE    = 0x278;        // [KH2LIB] Bytes per unit slot

// Offsets within a unit slot:
namespace slot {
    constexpr std::uint64_t HP         = 0x80;         // int32 [CONFIRMED] Current HP
    constexpr std::uint64_t MAX_HP     = 0x84;         // int32 [CONFIRMED] Max HP
    // Other fields within the 0x278-byte slot TBD.
    // Known: equipment, abilities, MP, AP, drive gauge are in here.
}

// Convenience: absolute addresses for Slot 0 HP/MaxHP
namespace slot0 {
    constexpr std::uint64_t HP         = 0x2A23598;    // [CONFIRMED] = SLOT0_BASE + 0x80
    constexpr std::uint64_t MAX_HP     = 0x2A2359C;    // [CONFIRMED] = SLOT0_BASE + 0x84
}

constexpr std::uint64_t MAX_DRIVE      = 0x2A236CA;   // [KH2LIB] Max drive gauge

// --------------------------------------------------------------------------
// Point / gauge system (minigames, etc.)
// --------------------------------------------------------------------------
constexpr std::uint64_t POINT1         = 0x2A0F9C8;   // [KH2LIB] Hunny Slider HP / minigame points
constexpr std::uint64_t POINT_STRIDE   = 0x50;         // [KH2LIB]
constexpr std::uint64_t GAUGE1         = 0x2A0FAB8;   // [KH2LIB]
constexpr std::uint64_t GAUGE_STRIDE   = 0x48;         // [KH2LIB]

// --------------------------------------------------------------------------
// Entity / position data
//
// Entity position (X, Y, Z, rotation) is NOT in the unit slot. It lives
// in a heap-allocated entity structure managed by the 3D engine.
// Finding the entity pointer chain is the next RE task.
//
// Approach for next session:
//   1. Do a float scan for position while walking.
//   2. When a heap address is found, use "Pointer scan for this address"
//      in Cheat Engine to find the static pointer chain.
//   3. Or: look for a pointer within/near the unit slot that points to
//      the entity object.
// --------------------------------------------------------------------------
namespace entity {
    constexpr std::uint64_t POS_X      = 0x0;  // float [UNKNOWN] heap entity + ??
    constexpr std::uint64_t POS_Y      = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t POS_Z      = 0x0;  // float [UNKNOWN]
    constexpr std::uint64_t ROT_Y      = 0x0;  // float [UNKNOWN]
}

// --------------------------------------------------------------------------
// Enemy list — TBD
// --------------------------------------------------------------------------
namespace enemy {
    constexpr std::uint64_t LIST_PTR   = 0x0;  // [UNKNOWN]
    constexpr std::uint64_t COUNT      = 0x0;  // [UNKNOWN]
    constexpr std::uint64_t STRIDE     = 0x0;  // [UNKNOWN]
}

// --------------------------------------------------------------------------
// Misc / RNG
// --------------------------------------------------------------------------
constexpr std::uint64_t RNG            = 0x07535C0;    // [KH2LIB] RNG state
constexpr std::uint64_t MSN            = 0x0BF3340;    // [KH2LIB] Mission data
constexpr std::uint64_t DEMYX_CLONE    = 0x2A0F834;    // [KH2LIB] Demyx clone / mission signal

} // namespace offsets
} // namespace kh2coop
