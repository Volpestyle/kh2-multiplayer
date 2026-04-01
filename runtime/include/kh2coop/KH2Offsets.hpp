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
// Input system  (Ghidra RE Session — 2026-03-31, CONFIRMED)
//
// Architecture overview:
//   Hardware (XInput/Steam/DXInput)
//     → FUN_140105810 (exe+0x105810): main input collector
//       - reads raw gamepads/keyboard/mouse
//       - supports up to 4 XInput + 4 Steam Input controllers
//       - SWAPS active controller data into slot 0 (only slot 0 drives player)
//       - writes to raw input slots at input_struct_base + 0x18 + slot * 0x44
//     → FUN_14039bf00 (exe+0x39BF00): game loop callback
//       - maps raw buttons → game action bitmask via table at exe+0x5C3420
//       - writes processed state to fixed button state array
//     → ~30 game systems read the processed button state
//
// CRITICAL FINDING: Friends do NOT use the input system. They are entirely
// AI-driven (FRIEND@YS → PARTY@YS → BTLOBJ@YS → STDOBJ@YS → OBJ@YS).
// There is no per-party-slot input buffer. For M3 multiplayer input injection,
// the friend AI must be hooked/replaced or entity state written directly.
//
// See docs/INPUT_RE_SESSION.md for full Ghidra trace and injection strategies.
// --------------------------------------------------------------------------
constexpr std::uint64_t INPUT_STRUCT_PTR = 0x079CF00;  // [CONFIRMED] qword ptr to input state struct
constexpr std::uint64_t INPUT          = 0x0BF3120;    // [CONFIRMED] = input_struct + 0x18 = Slot 0 raw input

namespace input {
    // Input state struct offsets (relative to value at INPUT_STRUCT_PTR)
    constexpr std::uint64_t CONTROLLER_COUNT = 0x14;    // [CONFIRMED] int32, active controller count
    constexpr std::uint64_t RAW_SLOT0        = 0x18;    // [CONFIRMED] first raw input slot
    constexpr std::uint64_t RAW_SLOT_STRIDE  = 0x44;    // [CONFIRMED] bytes per raw input slot
    constexpr std::uint64_t ACTIVE_RAW_SLOT_INDEX = 0x129C; // [GHIDRA] original raw slot index swapped into slot 0 this frame

    // Raw input slot layout (0x44 bytes per slot)
    constexpr std::uint64_t BUTTONS          = 0x00;    // [CONFIRMED] ushort, raw button bitmask
    constexpr std::uint64_t LSTICK_X         = 0x02;    // [CONFIRMED] byte, left stick X (0x80=center)
    constexpr std::uint64_t LSTICK_Y         = 0x03;    // [CONFIRMED] byte, left stick Y (0x80=center)
    constexpr std::uint64_t RSTICK_X         = 0x04;    // [CONFIRMED] byte, right stick X (0x80=center)
    constexpr std::uint64_t RSTICK_Y         = 0x05;    // [CONFIRMED] byte, right stick Y (0x80=center)

    // Processed button state array (fixed addresses, NOT relative to struct)
    constexpr std::uint64_t PROCESSED_ENTRY0 = 0x0BF31A0; // [CONFIRMED] processed buttons, slot 0 (0x68 bytes)
    constexpr std::uint64_t PROCESSED_ENTRY1 = 0x0BF3208; // [CONFIRMED] processed buttons, slot 1
    constexpr std::uint64_t PROCESSED_STRIDE = 0x68;       // [CONFIRMED] bytes per processed entry

    // Processed entry layout (0x68 bytes per entry)
    // +0x00: ulonglong — current frame button bitmask (game action bits)
    // +0x08: ulonglong — newly pressed this frame
    // +0x10: ulonglong — released this frame
    // +0x18: ulonglong — auto-repeat trigger
    // +0x20: 16 bytes  — analog stick data (4 floats)
    // +0x30: 16 bytes  — secondary analog data
    // +0x40: pointer   — context/mode pointer
    // +0x48: dword     — flags (bit 0 = disabled, bit 1 = type flag)

    // Button mapping table (raw pad bits → game action bits)
    constexpr std::uint64_t BUTTON_MAP_TABLE = 0x05C3420; // [CONFIRMED] input mapping table

    // Input context switch function: called ~30 times during mode changes
    // (battle start, menu open, cutscene, minigame, etc.)
    constexpr std::uint64_t CONTEXT_SWITCH_FUNC = 0x039B580; // [CONFIRMED] FUN_14039b580

    // Key code addresses
    constexpr std::uint64_t INPUT_COLLECTOR_FUNC = 0x0105810; // [CONFIRMED] main input collection
    constexpr std::uint64_t INPUT_LOOP_FUNC      = 0x039BF00; // [CONFIRMED] game loop input callback
    constexpr std::uint64_t BUTTON_MAP_FUNC      = 0x039C720; // [CONFIRMED] raw→game button mapper

    // DXInput state struct (keyboard/mouse subsystem)
    constexpr std::uint64_t DXINPUT_STRUCT   = 0x08BB290; // [CONFIRMED] DXInput global state
}

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

    // --------------------------------------------------------------------------
    // Friend actor pointers  (RE Session — 2026-03-26, CONFIRMED)
    //
    // The first friend slot (Slot 1) stores actor object pointers to BOTH
    // friend party members at these offsets. The addresses are dynamic and
    // change on room transitions. Entity struct is at actor + 0x640.
    //
    // Slot 0 and Slot 2 do NOT contain actor pointers at these offsets.
    // --------------------------------------------------------------------------
    constexpr std::uint64_t FRIEND1_ACTOR_PTR = 0x220; // qword [CONFIRMED] Slot 1 only — Friend 1 actor object
    constexpr std::uint64_t FRIEND2_ACTOR_PTR = 0x228; // qword [CONFIRMED] Slot 1 only — Friend 2 actor object
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
// --------------------------------------------------------------------------
// Actor object offsets  (CE Dynamic Session — 2026-03-31, CONFIRMED)
//
// The actor object is the parent struct that contains the entity transform
// at actor+0x640. These offsets are relative to the actor object base.
//
// The animation/motion ID at +0x180 maps directly to OpenKH's MotionSet
// enum: IDLE=0, WALK=1, RUN=2, JUMP=3, FALL=4, LAND=5, EX000=151, etc.
// Verified live via Cheat Engine snapshot diffing across IDLE/RUN/JUMP/
// FALL/LAND/ATTACK transitions.
// --------------------------------------------------------------------------
namespace actor {
    constexpr std::uint64_t ENTITY_TRANSFORM = 0x640; // [CONFIRMED] entity transform struct base
    constexpr std::uint64_t ANIM_ID          = 0x180; // dword [CONFIRMED] current animation/motion ID (MotionSet enum)
    constexpr std::uint64_t ANIM_SUB         = 0x184; // dword [CONFIRMED] animation sub-state / variant
    constexpr std::uint64_t OBJENTRY_PTR     = 0x918; // qword [CONFIRMED] pointer to objentry/descriptor record (id + name + mset)
    constexpr std::uint64_t FLAGS_9B8        = 0x9B8; // dword [CONFIRMED] entity flags (used by calc_motion)
    constexpr std::uint64_t STATE_PTR_9C0    = 0x9C0; // qword [CONFIRMED] state pointer (non-zero = active)
    constexpr std::uint64_t LINKED_NEXT_HANDLE = 0xA90; // dword [CONFIRMED] next active-entity handle used by EntityUpdateLoop

    // Movement fields consumed by EntityPositionPhysics (exe+0x3B89A0).
    // Live CE execution breakpoints on 2026-03-31 hit:
    //   - exe+0x3B8B85  movups xmm0,[rbx+0xB98]
    //   - exe+0x3B8C02  movups xmm0,[rbx+0xA58]
    // with RBX matching the live Friend1 actor object from Slot1+0x220.
    // The function later clears the accel block (0xA48..0xA60) before return.
    constexpr std::uint64_t VELOCITY_X       = 0xB98; // float [CONFIRMED] movement velocity X
    constexpr std::uint64_t VELOCITY_Y       = 0xB9C; // float [CONFIRMED] movement velocity Y
    constexpr std::uint64_t VELOCITY_Z       = 0xBA0; // float [CONFIRMED] movement velocity Z
    constexpr std::uint64_t ACCEL_X          = 0xA58; // float [CONFIRMED] acceleration X
    constexpr std::uint64_t ACCEL_Y          = 0xA5C; // float [CONFIRMED] acceleration Y
    constexpr std::uint64_t ACCEL_Z          = 0xA60; // float [CONFIRMED] acceleration Z
}

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
// Entity update call chain  (CE + Ghidra Session — 2026-03-31, CONFIRMED)
//
// Full call chain from game loop to entity position write, traced via
// hardware write breakpoint on friend entity posY + Ghidra xref analysis.
//
// Call chain:
//   exe+0x3BF5E0  Entity Update Loop (head = exe+0x2A171C8, next = actor+0xA90)
//     └─► exe+0x3BFD30  Per-Entity Update (dispatches vtable calls + physics)
//           ├─► vtable+0x10  Main AI/action update (class-specific)
//           ├─► vtable+0x18  Post-main update
//           ├─► vtable+0x28  Pre-physics update
//           └─► exe+0x3B89A0  Position Physics (velocity, gravity, collision)
//                 └─► exe+0x3B9090  Position Calculator (final write)
//                       └─► exe+0x1A8E60  MEMCPY_4FLOATS → entity transform
//     └─► exe+0x3BEEC0  calc_motion (batch motion processing)
//
// For Strategy B (AI replacement hook), exe+0x3BFD30 is the ideal hook
// target: intercept friend entities before physics, replace the vtable
// AI dispatch with player input, then let physics run normally.
// --------------------------------------------------------------------------
namespace entity_update {
    constexpr std::uint64_t UPDATE_LOOP        = 0x3BF5E0; // [CONFIRMED] iterates all entities
    constexpr std::uint64_t PER_ENTITY_UPDATE  = 0x3BFD30; // [CONFIRMED] per-entity frame update
    constexpr std::uint64_t POSITION_PHYSICS   = 0x3B89A0; // [CONFIRMED] velocity + gravity + collision
    constexpr std::uint64_t POSITION_CALC      = 0x3B9090; // [CONFIRMED] collision + final position write
    constexpr std::uint64_t CALC_MOTION        = 0x3BEEC0; // [CONFIRMED] batch motion/animation processing
}

// --------------------------------------------------------------------------
// Active entity list / handle resolver  (Ghidra + CE — 2026-03-31, CONFIRMED)
//
// KH2 keeps all active actors in a linked list:
//   - head pointer at exe+0x2A171C8
//   - tail pointer at exe+0x2A171D0
//   - next link stored as a 32-bit handle at actor+0xA90
//
// Handle resolution (FUN_1404ad3f0):
//   ptr = HANDLE_REGION_TABLE[(handle & 0x7fffffff) >> 25] |
//         (handle & 0x01ffffff)
//
// The region table is populated by FUN_1404ad390/FUN_1404ad2c0 and holds up
// to 64 high-address buckets. This is the canonical way to walk active
// enemies externally; there is no dedicated enemy-only array root.
// --------------------------------------------------------------------------
namespace active_entity_list {
    constexpr std::uint64_t HEAD                = 0x2A171C8; // [CONFIRMED] qword head of active actor list
    constexpr std::uint64_t TAIL                = 0x2A171D0; // [CONFIRMED] qword tail of active actor list
    constexpr std::uint64_t FREE_HEAD           = 0x2A171D8; // [CONFIRMED] qword deferred-removal list head
    constexpr std::uint64_t FREE_TAIL           = 0x2A171E0; // [CONFIRMED] qword deferred-removal list tail
    constexpr std::uint64_t HANDLE_REGION_TABLE = 0x2B0D720; // [CONFIRMED] qword[64] high-address table for handle resolution
    constexpr std::uint32_t HANDLE_LOW_MASK     = 0x01FFFFFF; // [CONFIRMED] low 25 bits kept directly in handle
    constexpr std::uint32_t HANDLE_BUCKET_SHIFT = 25;         // [CONFIRMED] bucket index = bits 25..30
    constexpr std::uint32_t HANDLE_BUCKET_COUNT = 64;         // [CONFIRMED] max buckets in HANDLE_REGION_TABLE
    constexpr std::uint32_t MAX_TRAVERSAL       = 256;        // safety cap for external traversal
}

// --------------------------------------------------------------------------
// Enemy entities  (RE Session — 2026-03-26 / 2026-03-31, PARTIAL)
//
// During a Dusk fight in TT Room 8, enemy entities were found in a
// contiguous array with stride 0x6C00 per actor slot. Each slot contains
// an actor object with the entity struct at actor + 0x640 (same as Sora
// and friends). Active enemies have moveState=8 or 9. Dead/freed slots
// have garbage data at the entity struct.
//
// Canonical traversal is now known: start at active_entity_list::HEAD and
// follow actor::LINKED_NEXT_HANDLE through active_entity_list::HANDLE_REGION_TABLE.
// Enemy count is derived by traversal + filtering moveState 8/9; no separate
// dedicated enemy count global has been identified yet.
// --------------------------------------------------------------------------
namespace enemy {
    constexpr std::uint64_t STRIDE     = 0x6C00;  // [CONFIRMED] bytes per enemy actor slot
    // moveState values observed for enemies:
    constexpr std::uint32_t MS_ACTIVE_GROUND = 8;  // [CONFIRMED] active enemy, grounded
    constexpr std::uint32_t MS_ACTIVE_ALT    = 9;  // [CONFIRMED] active enemy, alt state (stagger/attack?)
}

// --------------------------------------------------------------------------
// Objentry / descriptor records  (CE Session — 2026-03-31, PARTIAL)
//
// actor+0x918 points into the exe objentry table. Live reads from the current
// process resolved:
//   - `P_EX100` -> id 84
//   - `P_EX030` -> id 93
//   - `F_EX030_BB` -> id 321
//   - `N_BB080_TSURU1` -> id 397
//
// This gives a stable object ID for actors outside the unit-slot stat system.
// The name prefix is also useful for filtering active-list false positives:
// moveState 8/9 alone can match non-combat `N_...` actors in non-battle rooms.
// --------------------------------------------------------------------------
namespace objentry {
    constexpr std::uint64_t OBJECT_ID   = 0x00; // dword [CONFIRMED] ObjEntry/object ID
    constexpr std::uint64_t TYPE_FLAGS  = 0x04; // dword [PARTIAL] type/flags field; layout still under RE
    constexpr std::uint64_t NAME        = 0x08; // char[32] [CONFIRMED] object name, e.g. "P_EX100"
    constexpr std::uint64_t MSET_NAME   = 0x28; // char[32] [CONFIRMED] motion-set name, e.g. "P_EX100.mset"
}

// --------------------------------------------------------------------------
// Misc / RNG
// --------------------------------------------------------------------------
constexpr std::uint64_t RNG            = 0x07535C0;    // [KH2LIB] RNG state
constexpr std::uint64_t MSN            = 0x0BF3340;    // [KH2LIB] Mission data
constexpr std::uint64_t DEMYX_CLONE    = 0x2A0F834;    // [KH2LIB] Demyx clone / mission signal

} // namespace offsets
} // namespace kh2coop
