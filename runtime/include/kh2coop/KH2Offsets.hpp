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
constexpr std::uint64_t NOW_EXTRA      = 0x071700A;   // uint8  [CONFIRMED] NOW+0x02, third byte in NOW block
// Map/Battle/Event programs are at NOW+0x04, NOW+0x06, NOW+0x08 (uint16 each)
constexpr std::uint64_t MAP_PROGRAM    = 0x071700C;   // uint16 [KH2LIB] NOW+0x04
constexpr std::uint64_t BATTLE_PROGRAM = 0x071700E;   // uint16 [KH2LIB] NOW+0x06
constexpr std::uint64_t EVENT_PROGRAM  = 0x0717010;   // uint16 [KH2LIB] NOW+0x08
// Staging: filled by axaAppMain+0x37B0 path (movsd [NOW_STAGING_*], xmm0 from [rdi]), then committed by axaAppMain+0x3A00.
constexpr std::uint64_t NOW_STAGING_BASE = 0x0717018;   // [CONFIRMED] 8 bytes (movsd); → NOW via +0x3A00
constexpr std::uint64_t NOW_STAGING_XMM_B = 0x0717120;  // [CONFIRMED] alternate branch movsd target (same [rdi] template)
constexpr std::uint64_t NOW_STAGING_WORD = 0x0717128;   // [CONFIRMED] uint16 from [rdi+08] (mov [exe+717128], ax)
// "KH2J:" program lookup table (indexed from packed world/room in commit helper).
constexpr std::uint64_t KH2J_PROGRAM_TABLE = 0x09A98B0; // [CONFIRMED] fills MAP/BATTLE/EVENT programs

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
// Camera  (RE Session — CONFIRMED)
//
// The camera struct base is at exe+0x718C60. It contains:
//   - Configuration (distances, angles, speeds)
//   - A pointer to the followed actor object at +0x50
//   - Smoothed look-at, eye position, and raw look-at targets
//
// The camera copies the followed actor's position via MEMCPY_4FLOATS
// (exe+0x1A8E60) each frame. The source is:
//   [camStruct+0x50] + 0x640 + 0x30  (actor object -> entity transform -> position)
//
// RETARGETING: To make the camera follow a different entity, allocate a
// fake actor object (copy 0x700 bytes from the original), update the
// entity transform position at +0x640+0x30, and write the fake actor
// pointer to camStruct+0x50. Must continuously update each frame.
// --------------------------------------------------------------------------
constexpr std::uint64_t CAMERA_STRUCT  = 0x0718C60;   // [CONFIRMED] Camera struct base

namespace camera {
    constexpr std::uint64_t SMOOTH_LOOKAT  = 0x08;    // Vec4 [CONFIRMED] smoothed/interpolated look-at (X,Y,Z,W)
    constexpr std::uint64_t EYE_POS        = 0x18;    // Vec4 [CONFIRMED] camera eye world position (interpolated)
    constexpr std::uint64_t CAMERA_TYPE    = 0x48;    // dword [KH2LIB] camera type / mode
    constexpr std::uint64_t ACTOR_PTR      = 0x50;    // qword [CONFIRMED] pointer to followed actor object
    constexpr std::uint64_t DISTANCE       = 0x58;    // float [CONFIRMED] camera follow distance (~500)
    constexpr std::uint64_t EYE_POS_RAW    = 0x64;    // Vec4 [CONFIRMED] camera eye position (raw)
    constexpr std::uint64_t EYE_POS_COPY   = 0x74;    // Vec4 [CONFIRMED] camera eye position (copy)
    constexpr std::uint64_t LOOKAT_RAW     = 0x84;    // Vec4 [CONFIRMED] raw look-at target (X,Y,Z,W)
    constexpr std::uint64_t LOOKAT_COPY    = 0x94;    // Vec4 [CONFIRMED] look-at target copy
    constexpr std::uint64_t HEIGHT_OFFSET  = 0xA4;    // float [CONFIRMED] look-at Y offset (~1.5)

    // Actor object layout: the pointer at ACTOR_PTR points to an actor
    // object. The entity transform struct is at actor_object + 0x640.
    // Position within entity transform is at +0x30 (same as entity::POS_X).
    constexpr std::uint64_t ACTOR_TO_ENTITY = 0x640;  // [CONFIRMED] offset from actor object to entity transform
}

// Legacy alias (CAMERA_TYPE was previously at the absolute address)
constexpr std::uint64_t CAMERA_TYPE    = 0x0718CA8;   // = CAMERA_STRUCT + camera::CAMERA_TYPE

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
// Entity / position data  (Session 3 — CONFIRMED)
//
// Entity position is NOT in the unit slot. It lives in a struct within the
// exe data section. The struct base address CHANGES per room transition,
// but the internal layout is consistent.
//
// DISCOVERY: Session 3 confirmed that writing to entity struct + 0x30
// teleports the player. The copy chain flows:
//   entity struct (+0x30)  →  buffer array (exe+0xAD9100)  →  render
//
// In physics-active rooms, the game recomputes position each frame, so
// writes to the entity struct alone are overwritten within ~500ms.
// Reliable writes require continuous dual-write to BOTH the entity struct
// AND the buffer array entry.
//
// DYNAMIC DISCOVERY: The entity struct can be found by scanning the exe
// data section (0x2500000+) for a QWORD vtable pointer that also lands in
// the same 0x2500000..0x2600000 module region, with position W=1.0 at +0x3C
// from that base. Live validation found player vtables in the 0x251xxxx range,
// so discovery must not assume only 0x253xxxx values.
// --------------------------------------------------------------------------
namespace entity {
    // Offsets within the entity transform struct (relative to struct base)
    constexpr std::uint64_t VTABLE_PTR     = 0x00;  // qword [CONFIRMED] points to exe 0x253xxxx range
    constexpr std::uint64_t AIRBORNE_FLAG  = 0x08;  // dword [CONFIRMED] 0=grounded, 1=airborne
    constexpr std::uint64_t POS_X          = 0x30;  // float [CONFIRMED] horizontal position
    constexpr std::uint64_t POS_Y          = 0x34;  // float [CONFIRMED] vertical (negative = up)
    constexpr std::uint64_t POS_Z          = 0x38;  // float [CONFIRMED] depth position
    constexpr std::uint64_t POS_W          = 0x3C;  // float [CONFIRMED] always 1.0 (homogeneous)
    constexpr std::uint64_t COS_FACING     = 0x40;  // float [CONFIRMED] cos(facing angle)
    constexpr std::uint64_t SIN_FACING     = 0x48;  // float [CONFIRMED] sin(facing angle)
    constexpr std::uint64_t ROT_Y          = 0x4C;  // float [CONFIRMED] facing angle in radians
    constexpr std::uint64_t VEL_Y          = 0xA4;  // float [CONFIRMED] Y velocity (when airborne)
    constexpr std::uint64_t MOVE_STATE     = 0x100; // dword [CONFIRMED] 2=grounded, 3=airborne
    constexpr std::uint64_t AIRBORNE_SUB   = 0x104; // dword [CONFIRMED] 0=grounded, 1=airborne
}

// --------------------------------------------------------------------------
// Entity position buffer array  (Session 3 — CONFIRMED)
//
// A static array of entity position entries at a fixed exe offset.
// Each entry is 0x38 bytes: 16 bytes position (X,Y,Z,W) + 40 bytes meta.
// The player's slot index within this array changes per room.
//
// For dual-write teleportation, write to both the entity struct position
// AND the corresponding buffer array entry.
// --------------------------------------------------------------------------
namespace buffer {
    constexpr std::uint64_t ARRAY_BASE     = 0xAD9100;  // [CONFIRMED] entity position buffer array
    constexpr std::uint64_t ENTRY_STRIDE   = 0x38;      // [CONFIRMED] bytes per buffer entry
    // Within each buffer entry, position is at offset 0 (X,Y,Z,W as 4 floats)
    constexpr std::uint64_t ENTRY_POS_X    = 0x00;      // float
    constexpr std::uint64_t ENTRY_POS_Y    = 0x04;      // float
    constexpr std::uint64_t ENTRY_POS_Z    = 0x08;      // float
    constexpr std::uint64_t ENTRY_POS_W    = 0x0C;      // float (1.0)
}

// --------------------------------------------------------------------------
// Entity struct discovery
//
// The entity struct base lives in the exe data section and changes per
// room transition. Known examples:
//   Room 1 (early Twilight Town): exe+0x251F260, pos at exe+0x251F290
//   Room 2 (Twilight Town populated): exe+0x25224E0, pos at exe+0x2522510
//   Roxas Twilight Town live check: actor ptr exe+0x24FE460, entity ptr at
//                                    exe+0x24FEAA0, vtable at exe+0x2514E00
//
// To find dynamically: scan exe range 0x2500000..0x2600000 for a QWORD that
// also points back into that same module region (vtable pointer), then verify
// +0x3C == 1.0f (W).
//
// Key code addresses (stable across rooms):
//   exe+0x1354E0  — position update function (R9 = buffer entry)
//   exe+0x1A8E60  — memcpy_4floats (copies between buffers)
//   exe+0x456696  — entity sub-struct position writer
// --------------------------------------------------------------------------
namespace entity_discovery {
    constexpr std::uint64_t SCAN_START     = 0x2500000;  // start of entity data region
    constexpr std::uint64_t SCAN_END       = 0x2600000;  // end of entity data region
    constexpr std::uint64_t VTABLE_RANGE_LO = 0x2500000; // vtable pointers observed in this module region
    constexpr std::uint64_t VTABLE_RANGE_HI = 0x2600000; // (relative to exe base)
    constexpr float         POS_W_EXPECTED  = 1.0f;      // W component for validation

    // Code addresses (stable, for hooking)
    constexpr std::uint64_t POS_UPDATE_FUNC  = 0x1354E0; // R9 = buffer entry for current entity
    constexpr std::uint64_t MEMCPY_4FLOATS   = 0x1A8E60; // copies position between buffers
    constexpr std::uint64_t ENTITY_POS_WRITER = 0x456696; // validates + copies XYZW to sub-struct
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
