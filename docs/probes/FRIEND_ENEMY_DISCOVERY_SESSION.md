# Friend & Enemy Entity Discovery RE Session (2026-03-26)

Target: Sora's story — Twilight Town + Mysterious Tower with Donald & Goofy

## Confirmed Findings

### Friend Entity Discovery via Unit Slot Pointers

**Location:** Unit Slot 1 (SLOT0_BASE + SLOT_STRIDE), offsets +0x220 and +0x228

| Offset in Slot 1 | Content | Notes |
|---|---|---|
| +0x220 | QWORD pointer to Friend 1 actor object | `[CONFIRMED]` dynamic, changes per room |
| +0x228 | QWORD pointer to Friend 2 actor object | `[CONFIRMED]` dynamic, changes per room |

Entity struct is at `actor + 0x640` (same ACTOR_TO_ENTITY offset as Sora/camera chain).

**Important:** Only Slot 1 stores these pointers. Slot 0 and Slot 2 have non-pointer data at +0x220/+0x228.

**Entity struct layout is IDENTICAL to Sora's:**
- Vtable at +0x00 (QWORD, in exe entity data range)
- Airborne flag at +0x08
- Position XYZW at +0x30..+0x3C
- Rotation (cos/sin/radians) at +0x40, +0x48, +0x4C
- Velocity Y at +0xA4
- Move state at +0x100, airborne sub at +0x104

**Key difference:** Friends have moveState=0 (AI-controlled) vs Sora's moveState=2 (ground) / 3 (air).

**Verified in:**
- TT Room 7 (exploration, no battle)
- TT Room 8 (Dusk fight, battle active)
- Mysterious Tower Room 25 (enemy fights)

Actor addresses were different in every room, but the Slot1+0x220/+0x228 path worked consistently.

### Entity Allocation Order

Entities are allocated in the exe data section in a consistent order:

| Order | Type | Stride to next | moveState |
|---|---|---|---|
| 1 | Sora (player) | ~0x18300 to Friend1 | 2 (ground) / 3 (air) |
| 2 | Friend 1 | ~0x11D70 to Friend2 | 0 |
| 3 | Friend 2 | ~0x12A80 to first enemy | 0 |
| 4+ | Enemies | type-dependent stride | 8 or 9 |

The Sora→Friend1→Friend2 stride was consistent across TT and Mysterious Tower rooms.

### Enemy Entity Layout

Enemy entities use the **same entity struct** at actor+0x640 with the same field layout.

**Active enemy indicators:**
- moveState=8: active enemy (ground/fighting)
- moveState=9: active enemy (alternate state — stagger, attack, or dying)
- Garbage data at entity struct: dead/freed enemy slot

**Stride varies by enemy type:**

| Enemy Type | Stride | Room |
|---|---|---|
| Dusk (B_EX_X) | 0x6C00 | TT Room 8 |
| Nobody (unknown subtype) | 0x72F0 | Mysterious Tower Room 25 |

Enemies of the same type within a fight have consistent stride. Different types have different strides. The stride likely depends on the actor/model size.

**TT Room 8 Dusk fight details:**
- 5 enemy entity slots found at RVAs: 0x2532CA0, 0x25398A0, 0x25404A0, 0x25470A0, 0x254DCA0
- Stride between consecutive: 0x6C00
- After killing enemies, their entity struct data became garbage (vtable = random, position = NaN)
- HP was NOT found in the entity struct (consistent with Axel fight session)

**Mysterious Tower Room 25 details:**
- 8 enemy entities in 2 groups of 4
- Group A stride: 0x72F0 (4 entities)
- Gap between groups: ~0x4DA20
- Group B stride: 0x72F0 (4 entities)
- All active enemies had moveState=8

### Actor Object Layout (confirmed across all entity types)

| Offset | Content | Notes |
|---|---|---|
| +0x70 | Position X,Y,Z,W | Primary world transform (actor-level copy) |
| +0x80 | Descriptor table ptr 1 | Points to PAX/model descriptor data |
| +0x88 | Descriptor table ptr 2 | |
| +0x90 | Descriptor table ptr 3 | |
| +0x640 | Entity struct start | Position/rotation/movement data |

### Unit Slot HP Anomaly

Donald (Slot 1) consistently showed HP=0/1 and Goofy (Slot 2) showed HP=0/0 across all rooms tested. This occurred even when both characters were visually present and fighting alongside Sora. Possible explanations:
- Early TT/Tower story segment uses script-controlled companions without full party stats
- HP is tracked elsewhere for these specific story sections
- The MEMT system assigns them as NPCs rather than FRIEND type in these rooms

The friend actor pointers at Slot1+0x220/+0x228 were valid regardless of HP status.

### What Didn't Work
- Vtable scan with moveState=2/3 filter: only finds Sora. Friends have moveState=0, enemies have moveState=8/9.
- Actor pointer array at exe+0x2A171C8: only contains 1-2 entries (Sora + 1 unknown), NOT a comprehensive actor list.
- Scanning unit slots 0 and 2 for actor pointers at +0x220/+0x228: only Slot 1 has them.
- Broad Lua per-byte scans over >1MB: risk freezing CE. Use chunk-based scans instead.

## Code Changes

- `KH2Offsets.hpp`: Added `slot::FRIEND1_ACTOR_PTR` (0x220) and `slot::FRIEND2_ACTOR_PTR` (0x228). Updated `enemy::STRIDE` to 0x6C00 with note about type-dependent variation. Added enemy moveState constants.
- `GameBridgePC.hpp`: Added `friend1EntityAddr_` and `friend2EntityAddr_` member variables.
- `GameBridgePC.cpp`:
  - `DiscoverEntityAddresses()`: Now reads Slot1+0x220/+0x228 to discover friend entity structs, with W=1.0 validation.
  - `ReadActorState()`: Now reads position/rotation/velocity/airborne for all 3 slots (was slot 0 only).
  - `ApplyReplicaActorState()`: Now writes position/rotation/flags to all 3 slot entity structs (was slot 0 only).
  - `Tick()`: Resets friend entity addresses on room change.

## Next Steps
1. **Find enemy list root pointer + count** — needed to iterate enemies without scanning the data section. Try data breakpoints on enemy entity allocation or look for a global that changes when enemies spawn.
2. **Identify Donald vs Goofy mapping** — determine which +0x220 is Donald and which is Goofy. Check MEMT table or character ID within actor.
3. **Find enemy HP** — not in entity struct; may be in a separate damage/stat structure. Reference Axel fight session's B_EX110 findings.
4. **Verify friend entity writes** — test ApplyReplicaActorState() on Friend1/Friend2 with a live KH2 process to confirm position teleportation works.
5. **Find buffer slot indices for friends** — needed for dual-write in physics-active rooms.
