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
| World/room identity | Partial | `WORLD_ID`, `ROOM_ID`, `MAP_PROGRAM`, `BATTLE_PROGRAM`, `EVENT_PROGRAM` present. |
| Cutscene/transition state | Partial | `CUTSCENE_TIMER` used as runtime proxy for cutscene state. |
| Unit slot stat block | Partial | `SLOT0_BASE`, `SLOT_STRIDE`, `slot::HP`, `slot::MAX_HP` are mapped. |
| Actor transform path (pos/rot/vel) | Missing | Entity pointer chain and transform offsets are still `[UNKNOWN]`. |
| Enemy list root | Missing | `enemy::LIST_PTR`, `enemy::COUNT`, `enemy::STRIDE` are `[UNKNOWN]`. |
| Camera target override path | Missing | `CAMERA_TYPE` exists, writable follow target path not mapped. |
| Input injection for slot 1/2 | Missing | Global input address exists, per-slot injection path not mapped. |
| Replica writeback path | Missing | Actor/enemy replica writes still TODO in runtime bridge. |

---

## Known Offsets (From Current Codebase)

## World / room

- `NOW = 0x0717008` `[CONFIRMED]`
- `WORLD_ID = 0x0717008` `[CONFIRMED]`
- `ROOM_ID = 0x0717009` `[CONFIRMED]`
- `MAP_PROGRAM = 0x071700C` `[KH2LIB]`
- `BATTLE_PROGRAM = 0x071700E` `[KH2LIB]`
- `EVENT_PROGRAM = 0x0717010` `[KH2LIB]`

## Game state and cutscene related

- `PAUSE_STATUS = 0x0ABB7F8` `[KH2LIB]`
- `BATTLE_STATUS = 0x2A11384` `[KH2LIB]`
- `BATTLE_END = 0x2A0FC60` `[KH2LIB]`
- `CONTROLLABLE = 0x2A17168` `[KH2LIB]`
- `CUTSCENE_TIMER = 0x0B64F18` `[KH2LIB]`
- `CUTSCENE_LEN = 0x0B64F34` `[KH2LIB]`
- `CUTSCENE_SKIP = 0x0B64F1C` `[KH2LIB]`

## Unit slot stat system

- `SLOT0_BASE = 0x2A23518` `[KH2LIB]`
- `SLOT_STRIDE = 0x278` `[KH2LIB]`
- `slot::HP = 0x80` `[CONFIRMED]` (relative within slot)
- `slot::MAX_HP = 0x84` `[CONFIRMED]` (relative within slot)
- `slot0::HP = 0x2A23598` `[CONFIRMED]`
- `slot0::MAX_HP = 0x2A2359C` `[CONFIRMED]`

## Camera / input related

- `CAMERA_TYPE = 0x0718CA8` `[KH2LIB]`
- `INPUT = 0x0BF3120` `[KH2LIB]`
- `SOFT_RESET = 0x0ABABDA` `[KH2LIB]`

## Unknown placeholders that block live co-op runtime

- `entity::POS_X = 0x0` `[UNKNOWN]`
- `entity::POS_Y = 0x0` `[UNKNOWN]`
- `entity::POS_Z = 0x0` `[UNKNOWN]`
- `entity::ROT_Y = 0x0` `[UNKNOWN]`
- `enemy::LIST_PTR = 0x0` `[UNKNOWN]`
- `enemy::COUNT = 0x0` `[UNKNOWN]`
- `enemy::STRIDE = 0x0` `[UNKNOWN]`

---

## Runtime Bridge Coverage (What Code Actually Uses)

File: `runtime/src/GameBridgePC.cpp`

- `ReadRoomState()` reads world/room/program values and cutscene proxy.
- `ReadActorState()` is currently placeholder logic.
- `ReadEnemyStates()` is TODO.
- `WriteCameraTarget()` / `RestoreVanillaCamera()` are TODO.
- `InjectOwnedInput()` is TODO.
- `ApplyReplicaActorState()` / `ApplyReplicaEnemyState()` are TODO.

---

## RE Task Checklist (Session Tracker)

## 1) Actor roots and transforms

- [ ] Find canonical pointer chain for slot 0 actor entity
- [ ] Find canonical pointer chain for slot 1 actor entity
- [ ] Find canonical pointer chain for slot 2 actor entity
- [ ] Map actor position (x, y, z)
- [ ] Map actor rotation (yaw)
- [ ] Map actor velocity if stable
- [ ] Map actor motion/action ids if stable
- [ ] Verify by logging once per frame in a fixed room

## 2) Camera control path

- [ ] Find current camera target pointer/reference
- [ ] Find safe writable follow target path
- [ ] Confirm cinematic/cutscene camera override conditions
- [ ] Verify runtime target swap between slot 0/1/2 without crash

## 3) Input path (slot 1/2 ownership)

- [ ] Identify where vanilla input is consumed
- [ ] Identify per-slot command/application path
- [ ] Identify safe write path for slot 1/2 owned input
- [ ] Verify slot 1/2 movement + attack in offline test

## 4) Enemy list path

- [ ] Find root enemy list pointer
- [ ] Find enemy count field
- [ ] Find enemy stride
- [ ] Map per-enemy position, hp, motion/state
- [ ] Verify fixed encounter can be enumerated every frame

## 5) Transition/progression reliability

- [ ] Identify transition begin/end flags
- [ ] Identify stable room/world/program hashable state
- [ ] Verify boundaries with logs across repeated room transitions

---

## Suggested Log Format (Per Probe Tick)

Use one line per slot/enemy probe:

`[frame=<n>] [time=<ms>] [world=<id>] [room=<id>] [slot=<0|1|2>] [pos=<x,y,z>] [rot=<r>] [motion=<id>] [hp=<n>]`

This makes cross-client comparison and desync triage straightforward.

