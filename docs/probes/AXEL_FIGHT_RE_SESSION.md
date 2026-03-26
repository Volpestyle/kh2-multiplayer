# Axel Fight RE Session (2026-03-26)

Target: Roxas vs Axel Day 6 fight (TT World 0x02, Room 0x14)

> **Superseded findings:** The actor pointer array assumption (index +1 = Axel) was
> corrected in this same doc — index +1 was a weapon object. Entity discovery is now
> done via Slot1+0x220/+0x228 (see `FRIEND_ENEMY_DISCOVERY_SESSION.md`). The weapon
> object layout, position writer, and B_EX110 HP findings below remain valid and are
> referenced by `docs/ROXAS_DUAL_WIELD_FORM.md`.

## Confirmed Findings

### Actor Pointer Array
**Location:** `exe+0x2A171C8`
**Layout:** array of QWORD pointers, 8-byte stride

| Index | Value | Identity |
|-------|-------|----------|
| -4    | varying pointer | unknown (0x7FF69D0BC7D0 in one session) |
| 0     | Roxas actor     | `[CONFIRMED]` camera-followed actor, entity at +0x640 |
| +1    | second actor    | `[CONFIRMED]` moving position data consistent with Axel's arena location |

This array persisted across room reloads at the same exe offset. The Roxas actor pointer matched `camStruct+0x50` -> `actorObj`.

### Second Actor (Likely Axel) — 0x7FF69CAE9DB0

**Position offsets within actor object:**

| Offset | Content | Notes |
|--------|---------|-------|
| +0x70  | X,Y,Z,W (W=1.0) | Primary position — **MOVING** during fight, delta ~180 units/150ms |
| +0x670 | (0,0,0) W=1.0 | Zeroed — NOT the same layout as Roxas (+0x640 entity base) |
| +0x838 | X,Y,Z,W (same as +0x70) | Position copy |
| +0x878 | X,Y,Z,W (slightly different) | Interpolated / previous frame position? |
| +0xEE4 | X,Y,Z,W=1.0 | Another position vector (possibly bone/visual offset) |
| +0xF28 | X,Y,Z,W=1.0 | Yet another position vector |

**Key difference from Roxas:** Roxas's entity struct starts at actorObj+0x640 with vtable at entity+0x00 (RVA +0x2539CA0), airborne at +0x08, moveState at +0x100. The second actor does NOT have a valid entity struct at +0x640 (all zeroed). The entity-like data appears to start at a different offset, and it does NOT share the same vtable range (0x2530000-0x2550000).

**HP:** NOT found within actor+0x0000 to actor+0x2000 as either 4-byte int, 2-byte int, or float. Axel's HP is stored in a separate structure.

### What Didn't Work
- Vtable heuristic scan (exe+0x2530000-0x2550000 range) finds ONLY Roxas — enemies use different vtable ranges or no vtable
- Unit slot system (exe+0x2A23518, stride 0x278) has HP only for slot 0 (Roxas); slots 1-10 are empty/zero for enemies
- `exe+0x2A23718` had value 100/100 but did NOT change during combat — it's a static stat in Roxas's slot (MP or form gauge?), not enemy HP
- Broad Lua memory scans (>1MB) freeze the game — use CE native scanner or aob_scan instead
- Position buffer array (exe+0xAD9100) entries did not reliably update during this fight

### Position Writer
Data watchpoint on Roxas's POS_X confirmed: all entity position writes come through `MEMCPY_4FLOATS` at `exe+0x1A8E60` (RIP at +0x1A8E69). Register layout:
- RCX = destination address (entity position)
- RSI = actor object base
- RBX = entity + 0xA0
- RDX = entity + 0xC0

## Next Steps
1. **Try HP scan on next enemy encounter** (Dusks or Twilight Thorn) — use CE native `createMemScan` with `soUnknownValue` + `soDecreasedValue` constrained to exe+0x2400000-0x2C00000
2. **Investigate the actor pointer array further** — find the array root pointer and length field (currently we only know exe+0x2A171C8 contains slot 0)
3. **Multiple enemies** (Dusks) would be better for finding enemy list stride than a 1v1 boss
4. **The 100/100 value at exe+0x2A23718** should be identified — likely MP or a form gauge within Roxas's unit slot at SLOT0+0x200

## Follow-up Findings (same fight, later live session)

### Actor Array Re-interpretation
- The pointer at `exe+0x2A171D0` (`0x7FF69CAE9DB0` in one live run) is **NOT Axel**. It is a live `w_ex010_20` weapon object (Roxas dual-wield keyblade attachment).
- Evidence: the object has descriptor-table pointers at `+0x80/+0x88/+0x90` to `exe+0xF2AC00`, `exe+0xF2AC10`, and `exe+0xF2AE20`, and the referenced descriptor block contains the string `w_ex010_20`.
- A second live weapon object was found at `0x7FF69CAEBEF0`. It is `w_ex010_10` (the other Roxas dual-wield keyblade attachment).
- Evidence: the second object has descriptor-table pointers at `+0x80/+0x88/+0x90` to `exe+0xF69400`, `exe+0xF69410`, and `exe+0xF69620`, and the referenced descriptor block contains the string `w_ex010_10`.
- Implication: the array at `exe+0x2A171C8` is **not guaranteed to be combatants only**. In this fight it includes weapon/attachment-style live objects, so `index +1 = Axel` was a bad assumption.

### Roxas Dual-Wield Keyblade Object Layout
- Both `w_ex010_20` and `w_ex010_10` share the same live transform pattern as the earlier "second actor" object.
- Confirmed live transform offsets within both weapon objects:
  - `+0x70`  = primary position `X,Y,Z,W`
  - `+0x838` = position copy (same values)
  - `+0x878` = nearby/interpolated position copy
  - `+0xF28` = additional position copy
- This gives us two reproducible live weapon attachment roots for future RE: one object per dual-wield keyblade.

### Axel HP Follow-up
- A `B_EX110`-related object at `0x7FF69CB2BB30` showed a clean damage delta at `+0x154`: `9913 -> 9846` after one hit.
- Nearby field `+0x15C` held `12434` and looked like a max/current-template value, but writing the mirror alone did **not** stick.
- Write breakpoint on the mirror at `0x7FF69CB2BC84` always hit the copy helper at `exe+0x41C73E` (`mov eax,[rdx+04]` / `mov [rcx+r9],eax`). This is a **copy path**, not necessarily the authoritative HP owner.
- Exact-value scans after damage produced these notable HP candidates:
  - heap: `0x1540C98985A`
  - exe/module mirrors: `0x7FF69CB0919C`, `0x7FF69CB091A0`, `0x7FF69CB2BC84`, `0x7FF69CB2BC8C`
- Freezing the mirrors alone did not hold. Adding the unaligned heap candidate to a fast timer made all mirrors jump to the target value, but **likely caused a crash**. Treat `0x1540C98985A` as a dangerous candidate until the real owner is proven.

### Updated Warnings
- Do **not** fast-freeze unaligned heap HP candidates during this fight until the authoritative Axel HP source is confirmed.
- Do **not** assume weapon-like live objects in the actor array are enemies. Check descriptor-table pointers first.

### Updated Next Steps
1. **Re-find the real Axel root object** — pivot from `B_EX110` descriptor references instead of the actor pointer array.
2. **Trace backward from the `B_EX110` HP mirror copy path** — keep `exe+0x41C73E` as a breadcrumb, but find the true source field that feeds it.
3. **Map both Roxas keyblade attachment objects fully** — confirm whether they expose bone/socket data beyond the world transform copies.
4. **Document actor-array semantics** — identify which indices are combatants vs. attachments/weapons in this room.
