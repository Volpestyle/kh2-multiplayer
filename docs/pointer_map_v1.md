# Pointer Map v1

Target: `KH2 Final Mix PC (Steam Global)`  
Module base: `KINGDOM HEARTS II FINAL MIX.exe`  
Source of truth for current constants: `runtime/include/kh2coop/KH2Offsets.hpp`

## Status Legend

- `[CONFIRMED]` = Verified live in Cheat Engine on target build
- `[KH2LIB]` = Pulled from KH2 Lua library, not fully live-verified in this project
- `[UNKNOWN]` = Not mapped yet

---

## Mapped vs Missing (Current Snapshot)

| Area | Status | Notes |
|---|---|---|
| World/room identity | Done | `WORLD_ID`, `ROOM_ID`, `MAP_PROGRAM`, `BATTLE_PROGRAM`, `EVENT_PROGRAM` confirmed. |
| Cutscene/transition state | Partial | `CUTSCENE_TIMER` used as proxy. Transition start/end flags not yet mapped. |
| Unit slot stat block | Partial | `SLOT0_BASE`, `SLOT_STRIDE`, `slot::HP`, `slot::MAX_HP` confirmed. MP offset unknown. |
| Actor transform (slot 0) | Done | Entity struct layout fully mapped: position, rotation, velocity, airborne flags. |
| Actor transform (slot 1/2) | Missing | Friend entity struct discovery not yet implemented. |
| Entity discovery | Done | Two-strategy scan: camera actor pointer chain (primary), vtable+W+moveState heuristic (fallback). Auto-discovers on room transition via `Tick()`. |
| Position buffer array | Done | Buffer at `exe+0xAD9100`, stride `0x38`. Dual-write for physics-active rooms. |
| Camera struct | Done | Full camera struct at `exe+0x718C60` with look-at, eye position, actor pointer, distance. |
| Camera retarget | Done | Fake actor allocation + pointer redirect at `camStruct+0x50`. Implemented in `WriteCameraTarget()` / `RestoreVanillaCamera()`. |
| Enemy list root | Missing | `enemy::LIST_PTR`, `COUNT`, `STRIDE` still `0x0`. |
| Input injection | Missing | Global input address known (`0x0BF3120`), per-slot injection path not mapped. |
| Replica writeback | Partial | `ApplyReplicaActorState()` works for slot 0 (position, rotation, flags, HP) and camera fake actor (all slots). Enemy replica still TODO. |

---

## Known Offsets

### World / room

| Offset | Name | Source |
|---|---|---|
| `0x0717008` | `WORLD_ID` | `[CONFIRMED]` |
| `0x0717009` | `ROOM_ID` | `[CONFIRMED]` |
| `0x071700C` | `MAP_PROGRAM` | `[KH2LIB]` |
| `0x071700E` | `BATTLE_PROGRAM` | `[KH2LIB]` |
| `0x0717010` | `EVENT_PROGRAM` | `[KH2LIB]` |

### Game state

| Offset | Name | Source |
|---|---|---|
| `0x0ABB7F8` | `PAUSE_STATUS` | `[KH2LIB]` |
| `0x2A11384` | `BATTLE_STATUS` | `[KH2LIB]` |
| `0x2A0FC60` | `BATTLE_END` | `[KH2LIB]` |
| `0x2A17168` | `CONTROLLABLE` | `[KH2LIB]` |
| `0x0B64F18` | `CUTSCENE_TIMER` | `[KH2LIB]` |
| `0x0B64F34` | `CUTSCENE_LEN` | `[KH2LIB]` |
| `0x0B64F1C` | `CUTSCENE_SKIP` | `[KH2LIB]` |
| `0x0717424` | `GAME_SPEED` | `[KH2LIB]` |

### Unit slot stat system

| Offset | Name | Source |
|---|---|---|
| `0x2A23518` | `SLOT0_BASE` | `[KH2LIB]` |
| `+0x278` | `SLOT_STRIDE` | `[KH2LIB]` |
| `+0x80` | `slot::HP` (within slot) | `[CONFIRMED]` |
| `+0x84` | `slot::MAX_HP` (within slot) | `[CONFIRMED]` |

### Entity transform struct (relative to dynamic struct base)

| Offset | Name | Source | Notes |
|---|---|---|---|
| `+0x00` | `VTABLE_PTR` | `[CONFIRMED]` | QWORD, exe 0x253xxxx range |
| `+0x08` | `AIRBORNE_FLAG` | `[CONFIRMED]` | DWORD, 0=ground, 1=air |
| `+0x30` | `POS_X` | `[CONFIRMED]` | float |
| `+0x34` | `POS_Y` | `[CONFIRMED]` | float (negative = up) |
| `+0x38` | `POS_Z` | `[CONFIRMED]` | float |
| `+0x3C` | `POS_W` | `[CONFIRMED]` | float (always 1.0) |
| `+0x40` | `COS_FACING` | `[CONFIRMED]` | float |
| `+0x48` | `SIN_FACING` | `[CONFIRMED]` | float |
| `+0x4C` | `ROT_Y` | `[CONFIRMED]` | float, radians |
| `+0xA4` | `VEL_Y` | `[CONFIRMED]` | float (airborne Y velocity) |
| `+0x100` | `MOVE_STATE` | `[CONFIRMED]` | DWORD, 2=ground, 3=air |
| `+0x104` | `AIRBORNE_SUB` | `[CONFIRMED]` | DWORD, 0=ground, 1=air |

### Entity position buffer array

| Offset | Name | Source | Notes |
|---|---|---|---|
| `0xAD9100` | `buffer::ARRAY_BASE` | `[CONFIRMED]` | static array |
| `+0x38` | `buffer::ENTRY_STRIDE` | `[CONFIRMED]` | per entry |
| `+0x00` | `ENTRY_POS_X` (within entry) | `[CONFIRMED]` | float |
| `+0x04` | `ENTRY_POS_Y` | `[CONFIRMED]` | float |
| `+0x08` | `ENTRY_POS_Z` | `[CONFIRMED]` | float |
| `+0x0C` | `ENTRY_POS_W` | `[CONFIRMED]` | float (1.0) |

### Entity discovery constants

| Value | Name | Notes |
|---|---|---|
| `0x2500000..0x2600000` | Scan range | Entity data region in exe |
| `0x2530000..0x2540000` | Vtable range | Entity vtable pointers fall here |
| `0x1354E0` | `POS_UPDATE_FUNC` | Code address (stable) |
| `0x1A8E60` | `MEMCPY_4FLOATS` | Code address (stable) |
| `0x456696` | `ENTITY_POS_WRITER` | Code address (stable) |

### Camera struct (relative to `exe+0x718C60`)

| Offset | Name | Source | Notes |
|---|---|---|---|
| `+0x08` | `SMOOTH_LOOKAT` | `[CONFIRMED]` | Vec4 (X,Y,Z,W) |
| `+0x18` | `EYE_POS` | `[CONFIRMED]` | Vec4 interpolated |
| `+0x48` | `CAMERA_TYPE` | `[KH2LIB]` | DWORD camera mode |
| `+0x50` | `ACTOR_PTR` | `[CONFIRMED]` | QWORD ptr to followed actor |
| `+0x58` | `DISTANCE` | `[CONFIRMED]` | float (~500) |
| `+0x64` | `EYE_POS_RAW` | `[CONFIRMED]` | Vec4 |
| `+0x74` | `EYE_POS_COPY` | `[CONFIRMED]` | Vec4 |
| `+0x84` | `LOOKAT_RAW` | `[CONFIRMED]` | Vec4 |
| `+0x94` | `LOOKAT_COPY` | `[CONFIRMED]` | Vec4 |
| `+0xA4` | `HEIGHT_OFFSET` | `[CONFIRMED]` | float (~1.5) |

Camera actor pointer chain: `camStruct+0x50 -> actorObj+0x640 -> entity+0x30 = position`

### Input / other

| Offset | Name | Source |
|---|---|---|
| `0x0718CA8` | `CAMERA_TYPE` (legacy alias) | `[KH2LIB]` |
| `0x0BF3120` | `INPUT` | `[KH2LIB]` |
| `0x0ABABDA` | `SOFT_RESET` | `[KH2LIB]` |

### Still unknown (blocks further milestones)

| Name | Needed for |
|---|---|
| `enemy::LIST_PTR` | M5 enemy replication |
| `enemy::COUNT` | M5 enemy replication |
| `enemy::STRIDE` | M5 enemy replication |
| Per-slot input injection path | M3 friend-slot control |
| Friend1/Friend2 entity struct addresses | M1 completion, M3 |
| Animation ID offset in entity struct | M4 animation sync |
| MP offset within unit slot | M1 full stats |

---

## Runtime Bridge Coverage

File: `runtime/src/GameBridgePC.cpp`

| Method | Status | Notes |
|---|---|---|
| `Tick()` | Implemented | Auto-discovers entities on room change, re-points camera |
| `DiscoverEntityAddresses()` | Implemented | Camera pointer chain (primary) + vtable scan (fallback) |
| `ReadRoomState()` | Implemented | Reads world/room/program/cutscene state |
| `ReadActorState(slot)` | Partial | Slot 0: position, rotation, velocity, airborne, HP. Slot 1/2: HP only |
| `ReadEnemyStates()` | TODO | Needs enemy list offsets |
| `WriteCameraTarget(slot)` | Implemented | Fake actor allocation + pointer redirect |
| `RestoreVanillaCamera()` | Implemented | Restores original pointer, frees memory |
| `InjectOwnedInput(slot, input)` | TODO | Needs per-slot input path RE |
| `ApplyReplicaActorState(state)` | Partial | Slot 0: dual-write position/rotation/flags. All slots: HP + camera fake actor |
| `ApplyReplicaEnemyState(state)` | TODO | Needs enemy entity struct discovery |
