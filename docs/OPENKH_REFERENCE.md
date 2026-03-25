# OpenKH Reference Guide

OpenKH (`../openkh`) is an open-source KH2 modding toolkit. It is primarily a
**file-format parser and asset editor**, not a runtime memory manipulation
project. However, several components are directly useful for our co-op mod.

This document maps what OpenKH has to what we need and when to consult it.

---

## High-Value Resources

### Panacea — DLL Injection Framework

**Path:** `openkh/OpenKh.Research.Panacea/`

A C++ DLL that injects into the running KH2 process and hooks game functions.
Provides per-frame callbacks and a DLL loader system.

| File | What it has |
|------|-------------|
| `OpenKH.cpp` | Pattern scanning to find game functions via byte signatures (version-resilient) |
| `Panacea.cpp` | `Hook<TFunc>` template for function hooking; `MyAppVtbl` with `onFrame`, `onInit`, `onSoftReset` vtable; DLL loader that calls `OnInit`/`OnFrame` exports |
| `KingdomApi.h` | C++ struct types for game file I/O internals; `VarPtr<T>` / `ArrayPtr<T,N>` for resolving game variable pointers |
| `dllmain.cpp` | DLL entry point and initialization |

**When to consult:** If we decide to move from external `ReadProcessMemory` to
in-process DLL injection for better performance (eliminates camera flicker,
avoids cross-process overhead). Our multiplayer DLL could be loaded as a
Panacea plugin with `OnInit`/`OnFrame` exports.

### Motion / Animation IDs

**Path:** `openkh/OpenKh.Kh2/MotionSet.cs`

Complete enum of every KH2 animation index:

```
IDLE=0, WALK=1, RUN=2, JUMP=3, FALL=4, LAND=5, LAND2=6,
DAMAGE_S_FRONT=10, DAMAGE_S_BACK=11, DAMAGE_AIR=14,
BLOW_FRONT_HIT=20, BLOW_BACK_HIT=21,
REFLECT=41, AUTOGUARD=42, APPEAR=44, LEAVE=45,
TURN_L=48, TURN_R=49, TALK=50, CHANGEFORM=52,
MAGIC_FIRE1-3=56-58, MAGIC_BLIZZARD1-3=59-61,
MAGIC_THUNDER1-3=62-64, MAGIC_CURE1-3=65-67,
DEAD_LAND=131, DEAD_AIR=132,
EX000-EX100=151-251 (extended/custom),
RTN_00-RTN_09=283-292 (return animations)
```

**When to consult:** When implementing animation sync between clients (M4+).
These IDs map to the animation state value in the entity struct, which we'll
need to read/replicate.

### Object Type Taxonomy

**Path:** `openkh/OpenKh.Kh2/Objentry.cs`

Defines object types used by the game engine:

| Type | Value | Description |
|------|-------|-------------|
| PLAYER | 0 | Sora / Roxas / player-controlled |
| FRIEND | 1 | Donald, Goofy, world partners |
| NPC | 2 | Non-combat NPCs |
| BOSS | 3 | Boss entities |
| ZAKO | 4 | Regular enemies |
| WEAPON | 5 | Weapon models |
| E_WEAPON | 6 | Enemy weapons |
| SP | 7 | Special objects |
| F_OBJ | 8 | Field objects |
| BTLNPC | 9 | Battle NPCs |
| CHEST | 10 | Treasure chests |
| MAPTOOL | 11 | Map interaction objects |
| WORLD_MAP | 12 | World map |
| PRIZEBOX | 13 | Prize boxes / drops |
| SUBMEMBER | 14 | Sub-members |
| SHOP | 15 | Shop keepers |

Also defines `ObjectForm`: Sora=0, Valor=1, Wisdom=2, Limit=3, Master=4,
Final=5, Anti=6.

**When to consult:** During entity discovery RE — to identify entity type bytes
in memory and distinguish player from friend/NPC/enemy entities.

### Member Table (MEMT)

**Path:** `openkh/OpenKh.Kh2/SystemData/Memt.cs`

Defines how party slots are mapped per world/story-flag:

- `MemberIndices`: Player, Friend1, Friend2, FriendWorld (byte indices)
- Each entry maps WorldId + StoryFlag to a Members[] array
- Members array positions: Sora, Donald, Goofy, WorldChar, Valor, Wisdom,
  Limit, Master, Final, Anti, Mickey, SoraHigh, etc.

**When to consult:** When implementing Friend1/Friend2 entity discovery — this
tells us which character object ID occupies each party slot in each world.

### Character Slot Indices

**Path:** `openkh/OpenKh.Kh2/Battle/Lvup.cs`

Character index mapping (used in battle tables and unit slots):

```
Sora=0, Donald=1, Goofy=2, Mickey=3, Auron=4,
Ping/Mulan=5, Aladdin=6, Sparrow=7, Beast=8,
Jack=9, Simba=10, Tron=11, Riku=12
```

These 13 indices correspond to the character slots in the battle system.

**When to consult:** When mapping our `SlotType` (Player/Friend1/Friend2) to
the game's internal character index system.

### Attack Parameters

**Path:** `openkh/OpenKh.Kh2/Battle/Atkp.cs`

Complete attack parameter struct: Power, Element, KnockbackStrength, Team,
HitSfx, various flags (ComboFinisher, AirCombo, Reaction, etc.)

**When to consult:** When implementing damage synchronization (M5 — enemy
replication). Needed to understand how attacks interact with the HP system.

### World ID Constants

**Path:** `openkh/OpenKh.Kh2/Constants.cs`

All 19 world IDs with names and short codes:

```
0=WorldZz, 1=EndOfSea, 2=TwilightTown ("tt"), 3=DestinyIsland ("di"),
4=HollowBastion ("hb"), 5=BeastCastle ("bb"), 6=TheUnderworld ("he"),
7=Agrabah ("al"), 8=LandOfDragons ("mu"), 9=HundredAcreWood ("po"),
10=PrideLands ("lk"), 11=Atlantica ("lm"), 12=DisneyCastle ("dc"),
13=TimelessRiver ("wi"), 14=HalloweenTown ("nm"), 15=WorldMap ("wm"),
16=PortRoyal ("ca"), 17=SpaceParanoids ("tr"), 18=WorldThatNeverWas ("eh")
```

**When to consult:** For debugging room transitions and world-specific behavior.

---

## Reference-Only Resources

These are useful as background knowledge but not directly used at runtime.

### Hypervisor (C# Memory Reader)

**Path:** `openkh/Hypervisor/Hypervisor.cs`

C# equivalent of our `GameBridgePC` — uses `ReadProcessMemory` /
`WriteProcessMemory` with base offset. Confirms our approach is correct.
Uses `EOSSDK-Win64-Shipping.dll` module for Epic Games Store version detection.

### Binarysharp.MSharp (Process Memory Library)

**Path:** `openkh/Binarysharp.MSharp/`

Full .NET memory editing library. Has pointer chain resolution
(`RemotePointer`), remote allocation (`RemoteAllocation`), and memory
protection handling. Useful reference for our C++ pointer chain code.

### Save Data Layout

**Path:** `openkh/OpenKh.Kh2/SaveData/SaveDataFinalMix.cs`

Complete FM save file offsets:
- 0x0C: WorldId, 0x0D: RoomId, 0x0E: SpawnId
- 0x24F0: Characters[13] (0x114 bytes each)
- 0x32F4: DriveForms[10]
- 0x36E0: Experience

**When to consult:** If we ever implement save-state synchronization.

### File Format Documentation

**Path:** `openkh/docs/kh2/`

Extensive markdown docs for on-disk file formats:
- `file/type/00objentry.md` — Object entry table
- `file/type/00battle.md` — Battle system tables
- `file/type/03system.md` — System tables (CMD, ARIF, MEMT)
- `file/type/areadata.md` — Area data scripts
- `dictionary/` — Character, object, and enemy dictionaries

These describe **file formats**, not runtime memory layouts. The in-memory
representation may differ from the on-disk format.

---

## What OpenKH Does NOT Have

| What we need | OpenKH has? | Notes |
|---|---|---|
| Runtime entity position offsets | No | We found these via CE (Session 3) |
| Camera struct layout | No | We found this via CE (Camera RE Session) |
| Input injection addresses | No | IInput is an abstract interface |
| Enemy list pointer chain | No | Only file-format enemy tables |
| Entity struct vtable layout | No | Only file-level object definitions |
| Buffer array layout | No | We found this via CE (Session 3) |

Our `KH2Offsets.hpp` and Cheat Engine sessions are the authoritative source for
runtime memory layouts. OpenKH complements this with game-design-level
knowledge (what types of objects exist, how party slots work, animation IDs).

---

## Architecture Consideration: Panacea vs External Process

Current approach: **External process** (`ReadProcessMemory` / `WriteProcessMemory`)
- Pros: Simple, no injection risk, easy to develop/debug
- Cons: Cross-process overhead, camera writes flicker, can't hook functions

Potential future approach: **Panacea DLL plugin**
- Pros: In-process access (zero overhead), per-frame hooks, can intercept
  game functions directly (e.g., redirect camera copy at instruction level)
- Cons: More complex development, crash risk, version coupling

Decision deferred — external process works for M1-M2. Consider Panacea for
M3+ if input injection or camera stability requires in-process hooks.
