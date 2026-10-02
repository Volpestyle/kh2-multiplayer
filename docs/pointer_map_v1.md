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
| World/room identity | Done | Static `NOW` block + **commit path** mapped (staging at `0x717018`, helper `axaAppMain+0x3A00`, `KH2J` table `0x9A98B0`). See [NOW commit path](#now-commit-path-disassembly). |
| Cutscene/transition state | Partial | `CUTSCENE_TIMER` used as proxy. Transition start/end flags not yet mapped. |
| Unit slot stat block | Partial | `SLOT0_BASE`, `SLOT_STRIDE`, `slot::HP`, `slot::MAX_HP` confirmed. MP offset unknown. |
| Actor transform (slot 0) | Done | Entity struct layout fully mapped: position, rotation, velocity, airborne flags. |
| Actor transform (slot 1/2) | Done | Slot1+0x220/+0x228 actor pointers, entity at actor+0x640. Same layout as slot 0. |
| Entity discovery | Done | Two-strategy scan: camera actor pointer chain (primary), vtable+W+moveState heuristic (fallback). Auto-discovers on room transition via `Tick()`. |
| Position buffer array | Done | Buffer at `exe+0xAD9100`, stride `0x38`. Dual-write for physics-active rooms. |
| Camera struct | Done | Full camera struct at `exe+0x718C60` with look-at, eye position, actor pointer, distance. |
| Camera retarget | Done | Fake actor allocation + pointer redirect at `camStruct+0x50`. Implemented in `WriteCameraTarget()` / `RestoreVanillaCamera()`. |
| Enemy list root | Done | Active-entity list head at `exe+0x2A171C8`; next handle at `actor+0xA90`; handle region table at `exe+0x2B0D720`. Enemy count is derived by traversal + moveState `8/9` plus objentry-name filter (`B_`/`M_`). |
| Input system | Done | Full pipeline mapped: raw collection (`exe+0x105810`), button mapping (`exe+0x39C720`), processed state (`exe+0xBF31A0`). Friends are AI-only; two injection strategies defined. See [Input system](#input-system--ghidra-re-session--2026-03-31-confirmed). |
| Animation ID | Done | `actor+0x180` (DWORD), maps to OpenKH MotionSet enum. Verified IDLE/RUN/JUMP/FALL/LAND/ATTACK. |
| Entity update chain | Done | Full call chain traced: update loop (`0x3BF5E0`) → per-entity update (`0x3BFD30`) → position physics (`0x3B89A0`) → position calc (`0x3B9090`) → MEMCPY_4FLOATS. Strategy B hook target identified; current live M3 work is on suppressing the friend vtable `+0x28` pre-physics callback to remove residual Sora tethering. |
| Replica writeback | Partial | `ApplyReplicaActorState()` works for slot 0 (position, rotation, flags, HP) and camera fake actor (all slots). Enemy replica still TODO. |

---

## Known Offsets

### World / room

| Offset | Name | Source |
|---|---|---|
| `0x0717008` | `WORLD_ID` | `[CONFIRMED]` |
| `0x0717009` | `ROOM_ID` | `[CONFIRMED]` |
| `0x071700A` | `NOW_EXTRA` (byte) | `[CONFIRMED]` third byte in `NOW` block; written by same commit helper as world/room |
| `0x071700C` | `MAP_PROGRAM` | `[KH2LIB]` |
| `0x071700E` | `BATTLE_PROGRAM` | `[KH2LIB]` |
| `0x0717010` | `EVENT_PROGRAM` | `[KH2LIB]` |
| `0x0717018` | `NOW_STAGING_BASE` | `[CONFIRMED]` **8 bytes** (`movsd`); first bytes become world/room after **`axaAppMain+0x3A00`** commit (see below) |
| `0x0717120` | `NOW_STAGING_XMM_B` | `[CONFIRMED]` alternate branch: **`movsd [exe+0x717120], xmm0`** from same **`[rdi]`** template |
| `0x0717128` | `NOW_STAGING_WORD` | `[CONFIRMED]` **`mov [exe+0x717128], ax`** from **`[rdi+08]`** (16-bit field) |
| `0x09A98B0` | `KH2J_PROGRAM_TABLE` | `[CONFIRMED]` table labeled `"KH2J:"` in CE; indexed from packed world/room to fill map/battle/event programs |

### NOW commit path (disassembly)

Live **Steam Global** build (Cheat Engine symbols). The **authoritative** world/room/program values in the `NOW` block are filled by a helper under the export **`KINGDOM HEARTS II FINAL MIX.axaAppMain`**:

| CE label | Role |
|---|---|
| `axaAppMain+0x37B0` | **Staging fill** (prologue **`sub rsp,20`** …). **`rcx`** → **`rdi`** = pointer to a **source** record in RAM. **`r8d`/`edx`/`r9b`** carry packed args (seen as **`esi`/`ebx`/`bpl`** in callee). **Two branches:** (A) **`movsd xmm0,[rdi]`** then **`movsd [exe+0x717120],xmm0`** at **`+0x37EC`**, then **`mov [exe+0x717128], ax`** from **`[rdi+08]`** at **`+0x37F8`**. (B) Other path reaches **`movsd [exe+0x717018], xmm0`** at **`+0x387E`** (same **`xmm0`** from **`[rdi]`**). Next op **`movzx eax,[rdi+08]`** at **`+0x3886`** matches common **hardware-hit `RIP`** when watching **`0x717018`** (fault lines up on the **following** instruction). **Ignore** bogus **`rol byte ptr …`** if your listing starts earlier — align on **`+0x37B0`**. |
| `axaAppMain+0x3A00` | **Commit to `NOW`:** **`rcx`** = **`exe+0x717018`** (typical **`lea rcx,[exe+0x717018]`**). Reads bytes from staging, writes **`exe+0x717008`** … **`0x717010`**, using **`exe+0x9A98B0`** (`KH2J:`). |
| `axaAppMain+0x3A28` | **`mov [exe+0x717009], r11b`** — store to **`ROOM_ID`** (watchpoint may show **`RIP` at `+0x3A2F`**). |
| Callers above `+0x3A00` | **`lea rcx,[exe+0x717018]`** then **`call axaAppMain+0x3A00`** (e.g. **`+0x3994`–`+0x39A2`** region). |

**RE implication:** Trace **callers of `axaAppMain+0x37B0`** (who sets **`rdi`** and the **`r8`/`rdx`/`r9`** args) to find **transition / request** layer. **`rdi`** is the live **location packet**, not the static exe staging slot.

#### Call sites that `call axaAppMain+0x37B0` (staging fill)

Captured **2026-03-25** via Cheat Engine MCP `find_call_references` on the **function entry** at **`axaAppMain+0x37B0`** (same as disassembly prologue at **`+0x37B0`**). **23** distinct **`call`** sites in **`KINGDOM HEARTS II FINAL MIX.exe`**.

**In CE:** go to **`KINGDOM HEARTS II FINAL MIX.axaAppMain+37B0`**, then **Find out what addresses call this address** (e.g. **Ctrl+R**); the list below should match.

RVAs are relative to **`KINGDOM HEARTS II FINAL MIX.exe`** image base (use **`Ctrl+G` → `KINGDOM HEARTS II FINAL MIX.exe+<RVA>`**).

| # | `exe` RVA | Notes |
|---|-----------|--------|
| 1 | `+0x15112C` | Near other `axaAppMain` code — good first stop for arg setup |
| 2 | `+0x154ECC` | Same |
| 3 | `+0x30A605` | Deeper / map-load style band |
| 4 | `+0x30BC89` | |
| 5 | `+0x38F827` | |
| 6 | `+0x3946E6` | |
| 7 | `+0x3A49D4` | |
| 8 | `+0x3F1DD4` | |
| 9 | `+0x3FCC44` | |
| 10 | `+0x3FF303` | |
| 11 | `+0x3FFD55` | |
| 12 | `+0x42E430` | |
| 13 | `+0x42E4A2` | |
| 14 | `+0x434A48` | |
| 15 | `+0x434CD2` | |
| 16 | `+0x434D1D` | |
| 17 | `+0x436E29` | |
| 18 | `+0x5437C6` | High-RVA cluster (often UI / flow helpers) |
| 19 | `+0x543A9A` | |
| 20 | `+0x545CA2` | |
| 21 | `+0x54648A` | |
| 22 | `+0x54806A` | |
| 23 | `+0x549153` | |

Absolute VAs are **ASLR-dependent**; only **RVAs** are stable across runs.

##### Tracing caller #1 (`exe+0x15112C`) — worked example

Disassembly around **`KINGDOM HEARTS II FINAL MIX.exe+0x15112C`** (live MCP capture, Steam Global):

1. **`call` to staging fill** — at **`exe+0x15112C`**: **`call`** → **`axaAppMain+0x37B0`** (VA **`…492990`**).

2. **Register args right before the `call`** (read **upward** a few lines):
   - **`lea rcx,[rsp+30]`** at **`exe+0x151123`** → **`rcx`** = pointer to a **stack-local “location packet”** (8-byte `movsd` shape + 16-bit field at **`[rsp+38]`**, matching **`[rdi]` / `[rdi+8]`** inside the callee).
   - **`xor r8d,r8d`** / **`xor r9d,r9d`** on this merge path → then **`lea edx,[r8+1]`** at **`exe+0x151128`** → **`edx = 1`**.
   - So this path calls staging fill with **packed `edx`/`r8`/`r9`** = **`(1, 0, 0)`** and **`rcx` = packet on stack**.

3. **Where the packet bytes come from** (still **above** the `call`, same function):
   - One branch: **`call exe+0x3A0520`** (helper just above **`exe+0x15108E`** in this capture), then **`movsd xmm0,[rax]`** / **`movzx` from `[rax+8]`** into **`[rsp+30]`** / **`[rsp+38]`** — so **`rax`** points at an **in-memory source struct** after that helper returns.
   - Another branch: **`movzx` / `mov`** from **runtime globals** (CE shows absolute **`0x7FF7…C40719`** range — **heap / mutable**, not a fixed `exe+` offset) into **`[rsp+30]`…`[rsp+38]`**.

4. **Next RE step (go up the chain):**
   - **Who calls *this* function?** Put cursor on **`exe+0x15112C`**, **Find out what addresses call this function** on the **containing routine** (scroll **up** to the **function prologue** — **`push` / `sub rsp`** — first; the first bytes at **`exe+0x151080`** may be **misaligned** in a raw dump).
   - Then repeat: at the **parent** `call`, note **`rcx`/`rdx`/`r8`/`r9`** and any **`rax`** filled by a child **`call`**.

##### Step 1 done: child function + direct parents (`exe+0x151000`)

| Item | `exe` RVA | Notes |
|---|---|---|
| **Child** (builds stack packet → **`call axaAppMain+0x37B0`**) | **`0x151000`–`0x151155`** (~342 B) | Contains **`call`** at **`0x15112C`** into staging fill. |
| **Parent A** (`call` → child) | **`0x150CFE`** | After **`call exe+0x152940`**, tests **`dword [C406B8]`**; **non-zero** → **`call exe+0x151000`**, then **`call exe+0x3DB640`**. Routine prologue ~**`0x150CA2`** (**`push rbp`**, **`sub rsp,20`**). |
| **Parent B** | **`0x150E2A`** | Same **`[C406B8]`** gate; **`call exe+0x151000`** then **`mov bpl,1`**. Same **overall task** as A, different branch (longer path from ~**`0x150D44`**: **`r8d=[C40724]`**, table **`lea rdi,[exe+0x5B1510]`**, calls **`624210`/`624680`**, **`6DD400`**, etc.). |
| **Parent C** | **`0x15150C`** | Same pattern as A: **`call exe+0x152940`**, **`[C406B8]`** gate, **`call exe+0x151000`**, **`call exe+0x3DB640`**. |

**Globals (runtime VAs in CE; names TBD):** **`C406B8`** = “call **`0x151000`**” gate (**`dword`**). **`C406B4`**, **`C40724`**, **`C406B1`**, **`C406B0`**, **`C406C8`/`C406D0`** appear in the same neighborhood.

**Next (step 2 up):** In CE, **Find out what addresses call** the routine at **`exe+0x150CA2`** (or **`0x150D44`** for the wider function). If CE shows **no xrefs**, use **“Find references”** / **AOB** for the **`call`** `E8` sequence or trace **who sets `[C406B8]`**.

##### Step 2 automation (2026-03-25) — MCP + CE freeze

| Attempt | Result |
|---|---|
| **`find_call_references`** on **`exe+0x150CA0`**, **`0x150CA2`**, **`0x150D44`** (absolute VAs) | **0 callers** each (bridge/tool may miss tail calls, **`jmp` entries**, or non-standard prologues). |
| **`evaluate_lua`**: byte-step scan of whole **`KINGDOM HEARTS II FINAL MIX.exe`** for **`E8`** **`rel32`** targeting **`base+0x150CA2`** | First short run: **count = 0** (no **`call`** lands on that **exact** VA — entry may be **`0x150CA0`** only, or **`ff 25` / `jmp`**). |
| **`evaluate_lua`**: extended scan (multiple targets, full module) | **Aborted**; **Cheat Engine froze** — **do not** loop **`readByte`/`readInteger` over ~45 MB** in Lua inside CE. |

**Safe alternatives:** CE **disassembler** → **Find out what addresses call this address** on **`exe+0x150CA2`**. Or **xref `mov [C406B8]`** / **hardware write watch** on **`C406B8`** (sparse). Or **`aob_scan`** MCP with a **narrow** **`+X`** region, not a Lua per-byte walk.

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
| `0x08EC540` | `LOADING_INDICATOR` | `[KH2LIB]` |
| `0x07435D0` | `CURRENT_OPEN_MENU` — `FF` none, `01` save, `03` load, `05` moogle, `07` item popup, `08` pause (cutscene/fight), `0A` pause | `[KH2LIB]` |
| `0x29FB500` | `CONTINUE` | `[KH2LIB]` |
| `0x2A0C4C0` | `SVE` — saved location | `[KH2LIB]` |
| `0x0BF3340` | `MSN` — mission | `[KH2LIB]` |
| `0x07535C0` | `RNG` | `[KH2LIB]` |
| `0x2AE5CF8` | `SPAWNS` | `[KH2LIB]` |
| `0x2A0F7A8` | `ARD_POINTER` — room script data | `[KH2LIB]` |
| `0x2A24FB0` | `OBJENTRY_POINTER` (`00objentry.bin`) | `[KH2LIB]` |
| `0x2AE5DD0` / `0x2AE5DD8` | `SYS3_POINTER` / `BTL0_POINTER` (`03system.bin` / `00battle.bin`) | `[KH2LIB]` |

`CONTROLLABLE` reads `3` during cutscenes and `0` when a minigame starts (GoA ROM
script). Writing `2` to `PAUSE_STATUS` disables pausing (GoA ROM script).

### Save region (Steam Global)

| Offset | Name | Source |
|---|---|---|
| `0x09A98B0` | `SAVE` — live save body; starts with the magic `"KH2J"` (the `KH2J_PROGRAM_TABLE` label above is this block) | Archipelago KH2 client (Steam), matches the live CE label. The KH2 Lua library's Steam table says `0x09A9830` — an older patch; several of its addresses sit `0x80` low (its `Slot1` `0x2A23518` vs Archipelago's `0x2A23598`). Treat other `[KH2LIB]` addresses on this page as unverified until checked live. |
| `SAVE+0x0C` / `+0x0D` / `+0x0E` | saved world / room / door | `[KH2LIB]` (GoA ROM `Warp`) |
| `SAVE+0x10 + 0x180*world + 0x6*room` | per-room map / btl / evt program table (3 shorts) | `[KH2LIB]` (GoA ROM `Warp`) |
| `SAVE+0x3534 + 4*world` | world party table: 4 bytes per world, `[player, friend1, friend2, world ally]`, with `0x00` = playable character, `0x01` Donald, `0x02` Goofy, `0x12` empty. The GoA ROM writes `0x12020100` (full party) / `0x12121200` (Roxas only) at `+0x353C` (world 2, Twilight Town) and `+0x357C` (world `0x12`). This is OpenKH `WorldPartyMembers` `0x3534` and Expert595's "party table at save `+0x3534`" with **no** offset shift. GoA (world 4) friend1 = `SAVE+0x3545` | `[KH2LIB]` (GoA ROM), OpenKH, Expert595 |
| `SAVE+0x3524` | current form (`6` = Anti) | `[KH2LIB]` (GoA ROM) |
| `SAVE+0x1C98` (20 × `0x20`) | story progress per world (OpenKH `StoryProgress` `0x1C90`); GoA ROM progress checks `+0x1CFF`, `+0x1D2E`, `+0x1EDE` fall inside | OpenKH + 8, unverified |
| `SAVE+0x2300` (8 × 19) | room-visited flags (OpenKH `RoomVisitedFlag` `0x22F8`) | OpenKH + 8, unverified |
| `SAVE+0x24F8` (13 × `0x114`) | character stats (OpenKH `Characters` `0x24F0`) | OpenKH + 8, unverified |
| `SAVE+0x3580…` | inventory bytes (OpenKH `InventoryCount` `0x3580`; this one matches without the +8, so recheck live) | `[KH2LIB]` |

**OpenKH vs in-memory offsets:** OpenKH's `SaveDataFinalMix` offsets read 8
lower than the in-memory body the GoA ROM addresses (world id OpenKH `0x04` vs
`SAVE+0x0C`). The party table was once cited as a second example, but it is a per-world array that matches OpenKH exactly, so the shift rests on one data point. The candidate
progress allow list in `common/include/kh2coop/ProgressAllowList.hpp` uses
in-memory offsets; verify the shift live before relying on it.

The GoA ROM's `Warp(W,R,D,M,B,E)` writes the `NOW` block (world, room, door,
programs) and the saved location, but only redirects a transition already in
progress (from the world map). It also swaps party costumes by rewriting
objentry model-name strings before rooms load: with `obj0` = the pointer read
from `OBJENTRY_POINTER`, `obj0+0x16F0` is Donald's model name and
`obj0+0x1750` Goofy's.

### Room transition request (cold warp) — verified live 2026-10-02

| RVA | Role | Source |
|---|---|---|
| `0x152990` | **`RequestTransition(const LocationPacket*, u32 fadeFlags, int mode, u8 flag, int extra)`**. Starts the room-load task (callback `0x152A90`) and copies the packet into `NOW` staging `0x717018`/`0x717020` (or `0x717120`/`0x717128` when byte `0x9BA8D1` is set). Every room change goes through it: 23 callers, including the room-script Jump handler `0x3A46E0` and the world-map flow `0x154E20` → `0x151000`. | `[GHIDRA]` + `[CONFIRMED]` |
| `0x3A46E0` | Room-script Jump handler: builds a packet from the script operand and calls `0x152990(&packet, flags, 0 or 2, 0, op[6])`. | `[GHIDRA]` |
| `0x154E20` | World-map exit: packet with programs `0xFFFF`, then `0x152990(&packet, flags \| 1, 0, 0, 0)`. | `[GHIDRA]` |

`LocationPacket` is the `NOW` layout: `u8 world, u8 room, u16 door, u16 map,
u16 btl` (8 bytes), then `u16 evt` at `+8`. Programs of `0xFFFF` take the
save's per-room values. The inject DLL calls `0x152990(&packet, 1, 0, 0, 0)` from
the PerEntityUpdate hook at the start of a frame (`inject/src/Warp.cpp`,
`kh2ctl warp`), guarded by the function's first 24 bytes. 50 consecutive
warps across 13 rooms in three worlds loaded cleanly.

**Load signals.** `NOW` world/room change at request time. `0x9BA928` (the
load task pointer written by `0x152990`), byte `0x9BA8D1` and
`LOADING_INDICATOR` `0x8EC540` didn't change when sampled every 50 ms through a
load. The reliable signal is that entity updates stop for ~0.55 s during a
load and resume in the new room (the DLL's frame counter).

**`[KH2LIB]` state flags on this build.** `PAUSE_STATUS` `0xABB7F8` reads
garbage (`0xE5E04CA0`). `+0x80` (`0xABB878`) reads 0. `CONTROLLABLE`
`0x2A17168` reads 142 in normal play, and `CURRENT_OPEN_MENU` `0x7435D0`
reads `FF`. None of them changed when tested (no menu could be opened by
script), so they stay unverified.

**Static `+0x80` corrections (`[GHIDRA]`, not yet checked live).** The game's
code addresses these at the library value `+0x80`:

| RVA | Meaning |
|---|---|
| `0x2A11404` (`BATTLE_STATUS`+0x80) | battle type: `0x3AB690` sets `2` (forced battle), `0x3ABAB0` clears it, `0x3ABB20` tests `!= 0` |
| `0x2A171E8` (`CONTROLLABLE`+0x80) | frozen-entity-group bitset; `0x3BFA40` skips an entity whose group bit is set. `0` = nothing frozen |
| `0xABB878` (`PAUSE_STATUS`+0x80) | pause-blocker bitmask; the pause watcher `0x1572B0` opens pause only when it is `0`, `0x9BA8D0 != 0` and Start is pressed |
| `0x9BA8D0` | "in field" byte; the load task `0x152A90` clears it at load start |
| `0xB64F98` (`CUTSCENE_TIMER`+0x80) | cutscene timer |
| `0x8EC5C0` (`LOADING_INDICATOR`+0x80) | loading indicator |
| `0x2AE5D78` (`SPAWNS`+0x80) | **not** an enemy toggle: a rotating 0–7 index for an effect's random spread (`0x3F3FA0`) |

`CURRENT_OPEN_MENU` `0x7435D0` needs no shift. Suggested safe-state gate:
`0x2A171E8 == 0 && 0x9BA8D0 != 0 && menu == 0xFF && cutscene idle`.

### Unit slot stat system

| Offset | Name | Source |
|---|---|---|
| `0x2A23518` | `SLOT0_BASE` | `[KH2LIB]` |
| `+0x278` | `SLOT_STRIDE` | `[KH2LIB]` |
| `+0x80` | `slot::HP` (within slot) | `[CONFIRMED]` |
| `+0x84` | `slot::MAX_HP` (within slot) | `[CONFIRMED]` |

### Actor object offsets  (CE Dynamic Session — 2026-03-31, CONFIRMED)

| Offset | Name | Source | Notes |
|---|---|---|---|
| `+0x180` | `actor::ANIM_ID` | `[CONFIRMED]` | DWORD, current animation/motion ID. Maps to OpenKH MotionSet: IDLE=0, WALK=1, RUN=2, JUMP=3, FALL=4, LAND=5, EX000=151. Verified via CE snapshot diff across all states. |
| `+0x184` | `actor::ANIM_SUB` | `[CONFIRMED]` | DWORD, animation sub-state / variant index |
| `+0x640` | `actor::ENTITY_TRANSFORM` | `[CONFIRMED]` | Entity transform struct base (position, rotation, etc.) |
| `+0x9B8` | `actor::FLAGS` | `[CONFIRMED]` | DWORD, entity flags (OR'd with 0x4000, 0x500 by calc_motion) |
| `+0x9C0` | `actor::STATE_PTR` | `[CONFIRMED]` | QWORD, state pointer (non-zero = active) |
| `+0xA90` | `actor::LINKED_NEXT_HANDLE` | `[CONFIRMED]` | DWORD, next active-entity handle used by `EntityUpdateLoop` |
| `+0xA58` | `actor::ACCEL_X` | `[CONFIRMED]` | Float, read by `EntityPositionPhysics` at `exe+0x3B8C02` (`movups xmm0,[rbx+0xA58]`). |
| `+0xA5C` | `actor::ACCEL_Y` | `[CONFIRMED]` | Float, part of the acceleration vector consumed from `actor+0xA58`. |
| `+0xA60` | `actor::ACCEL_Z` | `[CONFIRMED]` | Float, part of the acceleration vector consumed from `actor+0xA58`. |
| `+0xB98` | `actor::VELOCITY_X` | `[CONFIRMED]` | Float, read by `EntityPositionPhysics` at `exe+0x3B8B85` (`movups xmm0,[rbx+0xB98]`). |
| `+0xB9C` | `actor::VELOCITY_Y` | `[CONFIRMED]` | Float, part of the velocity vector consumed from `actor+0xB98`. |
| `+0xBA0` | `actor::VELOCITY_Z` | `[CONFIRMED]` | Float, part of the velocity vector consumed from `actor+0xB98`. |

### Entity transform struct (relative to dynamic struct base)

| Offset | Name | Source | Notes |
|---|---|---|---|
| `+0x00` | `VTABLE_PTR` | `[CONFIRMED]` | QWORD, exe 0x253xxxx range |
| `+0x08` | `AIRBORNE_FLAG` | `[CONFIRMED]` | DWORD, **sticky**: set at a jump, stays 1 after landing. Use `+0x104` for airborne (VUH-1490) |
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
| `0x0BF3120` | `INPUT` | `[CONFIRMED]` |
| `0x0ABABDA` | `SOFT_RESET` | `[KH2LIB]` |

### Input system  (Ghidra RE Session — 2026-03-31, CONFIRMED)

Full input pipeline traced via Ghidra static analysis. See `docs/INPUT_RE_SESSION.md` for the complete disassembly walkthrough.

**Architecture:**
```
Hardware (XInput / Steam Input / DXInput)
  │
  ▼
exe+0x105810  — main input collector (FUN_140105810)
  │  reads raw gamepads/keyboard/mouse
  │  supports up to 4 XInput + 4 Steam Input controllers
  │  SWAPS active controller data into slot 0
  │  writes to raw input slots at struct_base+0x18 + slot*0x44
  │
  ▼
exe+0x39BF00  — game loop input callback (FUN_14039bf00)
  │  calls exe+0x39C720 per entry — maps raw buttons→game actions
  │  via mapping table at exe+0x5C3420
  │  writes processed button bitmask to fixed-address array
  │
  ▼
Processed button state at exe+0xBF31A0 (2 entries, stride 0x68)
  │  Entry 0: exe+0xBF31A0  (current buttons, new-press, release, repeat, analog)
  │  Entry 1: exe+0xBF3208
  │
  ▼
~30 game systems read processed state via exe+0x39B580 context switch
```

#### Input struct pointers

| Offset | Name | Source | Notes |
|---|---|---|---|
| `0x079CF00` | `INPUT_STRUCT_PTR` | `[CONFIRMED]` | QWORD ptr to input state struct |
| `0x0BF3120` | `INPUT` (raw slot 0) | `[CONFIRMED]` | = struct_base + 0x18 |
| `0x0BF3164` | Raw slot 1 | `[CONFIRMED]` | = struct_base + 0x18 + 0x44 |
| `0x0BF31A0` | Processed entry 0 | `[CONFIRMED]` | 0x68-byte processed button state |
| `0x0BF3208` | Processed entry 1 | `[CONFIRMED]` | same layout |
| `0x05C3420` | Button mapping table | `[CONFIRMED]` | raw pad → game action bits |
| `0x08BB290` | DXInput struct | `[CONFIRMED]` | keyboard/mouse subsystem state |

#### Raw input slot layout (0x44 bytes per slot, at struct_base + 0x18 + slot * 0x44)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `+0x00` | ushort | BUTTONS | Raw button bitmask |
| `+0x02` | byte | LSTICK_X | Left stick X (0x80=center) |
| `+0x03` | byte | LSTICK_Y | Left stick Y (0x80=center) |
| `+0x04` | byte | RSTICK_X | Right stick X (0x80=center) |
| `+0x05` | byte | RSTICK_Y | Right stick Y (0x80=center) |

#### Processed entry layout (0x68 bytes per entry)

| Offset | Type | Name | Notes |
|---|---|---|---|
| `+0x00` | ulonglong | Current buttons | Game action bitmask |
| `+0x08` | ulonglong | New press | Newly pressed this frame |
| `+0x10` | ulonglong | Release | Released this frame |
| `+0x18` | ulonglong | Auto-repeat | Repeat trigger |
| `+0x20` | 4 floats | Analog data | Left stick |
| `+0x30` | 4 floats | Analog data 2 | Right stick |
| `+0x40` | pointer | Context | Mode pointer |
| `+0x48` | dword | Flags | bit 0=disabled, bit 1=type |

#### Key code addresses (stable)

| `exe` RVA | Name | Notes |
|---|---|---|
| `0x105810` | InputCollector | Main per-frame input collection |
| `0x39BF00` | InputLoopCallback | Registered game loop callback for button processing |
| `0x39C720` | ButtonMapper | Maps raw pad → game action bitmask |
| `0x39B580` | ContextSwitch | Input context switch (called ~30 sites) |
| `0x134FF0` | PerControllerUpdate | Per-controller raw read + dispatch |
| `0x133970` | GamepadEnumBind | XInput/Steam enumeration and binding |
| `0x132F90` | DXInputPoll | DXInput keyboard/mouse poll with critical section |

#### Critical finding: friends do not use input

KH2 consolidates all active controller input into **slot 0 only**. Friend characters (Donald/Goofy) are entirely AI-driven. The game swaps whichever controller has activity into slot 0; there is no per-party-slot input buffer.

RTTI class hierarchy for friends:
```
Friend@kn → FRIEND@YS → PARTY@YS → BTLOBJ@YS → STDOBJ@YS → OBJ@YS
```

Related RTTI classes: `FriendPersonality@kn`, `FRIEND_FORMATION@gb`, `ACTION_FRIEND_FLY@kn`.

Debug strings found: `"friend attack:"`, `"gentle friend"`, `" friend1:"`, `" friend2:"`, `" friend3:"`, `"FRIEND RECOV    %3d"`.

#### M3 injection strategies

**Strategy A — Puppet Mode (implement first):**
Write directly to entity position/rotation/action fields. Already partially working via `ApplyReplicaActorState()`. Needs animation ID offset for visual sync.

**Strategy B — AI Replacement Hook (future):**
Hook the friend AI tick function via DLL injection (Panacea-style). Replace AI output with player input. Requires finding the `FRIEND@YS` vtable dispatch and the per-frame AI decision function. Use CE hardware write breakpoint on friend entity position to find the callstack.

### Friend entity discovery  (RE Session — 2026-03-26, CONFIRMED)

The first friend unit slot (Slot 1, at `SLOT0_BASE + SLOT_STRIDE`) stores actor object pointers to **both** friend party members:

| Offset within Slot 1 | Name | Source | Notes |
|---|---|---|---|
| `+0x220` | `slot::FRIEND1_ACTOR_PTR` | `[CONFIRMED]` | QWORD, pointer to Friend 1 actor object |
| `+0x228` | `slot::FRIEND2_ACTOR_PTR` | `[CONFIRMED]` | QWORD, pointer to Friend 2 actor object |

Entity struct is at `actor + 0x640` (same as Sora). Friends share the **exact same entity struct layout** as the player — position, rotation, velocity, airborne flags all at the same offsets.

**Key differences from player entity:**
- Friends have `moveState=0` (AI-controlled) vs player `moveState=2` (ground) / `3` (air)
- Slot 0 and Slot 2 do **not** have actor pointers at `+0x220`/`+0x228` — only Slot 1 stores both
- Actor addresses are dynamic and change per room transition (re-discover after each transition)

Verified in: TT Room 7, TT Room 8 (Dusk fight), Mysterious Tower Room 25. Friend entities were at different addresses in each room, but the Slot1+0x220/0x228 discovery path worked consistently.

### Enemy entity layout  (RE Session — 2026-03-26, PARTIAL)

Enemy entities use the **same entity struct layout** at `actor + 0x640`. Active enemies have `moveState=8` (or `9` for alt state). Dead/freed enemy slots have garbage data.

Enemies of the same type are allocated in contiguous actor slots:

| Enemy type | Stride | World/Room |
|---|---|---|
| Dusk (TT Room 8) | `0x6C00` | World 2 Room 8 |
| Nobodies (Mysterious Tower Room 25) | `0x72F0` | World 2 Room 25 |

**Stride varies by enemy type.** A fixed stride cannot be assumed. The canonical runtime container is not a dedicated enemy array; it is the active-entity linked list headed by `exe+0x2A171C8`.

### Actor objentry descriptor  (CE Session — 2026-03-31, PARTIAL)

`actor+0x918` points into the exe objentry/descriptor table:

| Offset | Meaning | Status | Notes |
|---|---|---|---|
| `+0x00` | `objectId` | `[CONFIRMED]` | dword ObjEntry ID |
| `+0x04` | type/flags | `[PARTIAL]` | dword present, exact bit layout still under RE |
| `+0x08` | object name | `[CONFIRMED]` | char[32], e.g. `P_EX100`, `P_EX030`, `F_EX030_BB`, `N_BB080_TSURU1` |
| `+0x28` | mset name | `[CONFIRMED]` | char[32], e.g. `P_EX100.mset` |

Live validation on 2026-03-31 from the current process:

- `P_EX100` -> id `84`
- `P_EX030` -> id `93`
- `F_EX030_BB` -> id `321`
- `N_BB080_TSURU1` -> id `397`

Important caveat: a live non-combat `N_...` actor in the current room had `moveState=8`, so **moveState `8/9` alone is not a safe enemy filter**. Runtime enemy traversal now uses the objentry name prefix and only accepts `B_...` / `M_...` actors after the moveState check.

### Active entity list / handle resolver  (Ghidra + CE Session — 2026-03-31, CONFIRMED)

`EntityUpdateLoop` walks a global linked list of all active actors:

- `exe+0x2A171C8` — head pointer (`DAT_142a171c8`)
- `exe+0x2A171D0` — tail pointer (`DAT_142a171d0`)
- `actor+0xA90` — next-link handle for each actor node
- `exe+0x2B0D720` — 64-entry qword region table used to resolve handles to pointers

Handle resolution from `FUN_1404ad3f0`:

```c
masked = handle & 0x7fffffff;
bucket = masked >> 25;
ptr = HANDLE_REGION_TABLE[bucket] | (masked & 0x01ffffff);
```

Live CE validation on 2026-03-31:

- Current room: world `5`, room `8`
- Active-list head read from `exe+0x2A171C8`: `0x7FF6EE0C2F60`
- First live links resolved cleanly through `actor+0xA90`:
  - `0x7FF6EE0C2F60` -> handle `0x8403A1B0` -> `0x7FF6EE03A1B0`
  - `0x7FF6EE03A1B0` -> handle `0x84056FB0` -> `0x7FF6EE056FB0`
  - `0x7FF6EE056FB0` -> handle `0x8407BA40` -> `0x7FF6EE07BA40`
- The current room contained `18` active list nodes total and `1` moveState `8/9` false positive (`N_BB080_TSURU1`) in a non-combat room
- This established that active-list traversal needs an objentry-based class filter in addition to moveState

**Result (superseded 2026-10-02, VUH-1486):** an enemy is an active-list actor whose objentry type byte (`actor+0x918` → record `+0x04`, OpenKH `ObjectType`) is `3` (BOSS) or `4` (ZAKO). Verified live in the Parlor Ambush: 8 `M_EX020_RAW` Shadows read type `4`. Their team `actor+0x4DC` read `2`; the party reads `1`, props `0`. The earlier moveState `8`/`9` + `B_`/`M_` filter missed them because some enemies don't use the player's entity layout, so `entity+0x100` reads garbage. `kh2ctl entities` lists every active actor with these fields. No enemy-only count global has been identified.

### Entity update call chain  (CE + Ghidra Session — 2026-03-31, CONFIRMED)

Traced via hardware write breakpoint on Friend1 entity position Y (`actor+0x640+0x34`). The breakpoint fired at `exe+0x1A8E6F` (inside `MEMCPY_4FLOATS`). Return address analysis + Ghidra xref tracing revealed the full call chain:

```
exe+0x3BF5E0  Entity Update Loop
  │  Iterates linked list at DAT_142a171c8 (all active entities)
  │
  ├─► exe+0x3BFD30  Per-Entity Update
  │     │  Dispatches to vtable virtual functions:
  │     │    vtable+0x08  — early update
  │     │    vtable+0x10  — main AI / action update (friend AI decision here)
  │     │    vtable+0x18  — post-main update
  │     │    vtable+0x20  — conditional update
  │     │    vtable+0x28  — pre-physics update
  │     │
  │     └─► exe+0x3B89A0  Position Physics
  │           │  Calculates velocity, gravity, applies movement delta
  │           │  Reads velocity from actor+0xB98, acceleration from actor+0xA58
  │           │  Live CE execution breakpoints confirmed both reads on Friend1:
  │           │    exe+0x3B8B85 (velocity) and exe+0x3B8C02 (acceleration)
  │           │
  │           └─► exe+0x3B9090  Position Calculator
  │                 │  Collision detection, final position computation
  │                 │  Writes to entity transform via MEMCPY_4FLOATS:
  │                 │    lea rcx,[rsi+0x670]  (entity+0x30 = position)
  │                 │    lea rdx,[rsi+0x700]  (computed position source)
  │                 │    call exe+0x1A8E60
  │                 │
  │                 └─► exe+0x1A8E60  MEMCPY_4FLOATS
  │
  └─► exe+0x3BEEC0  calc_motion (batch animation processing)
```

| `exe` RVA | Name | Role |
|---|---|---|
| `0x3BF5E0` | Entity Update Loop | Iterates all entities, calls per-entity update |
| `0x3BFD30` | Per-Entity Update | Dispatches vtable calls + calls position physics. **Strategy B hook target.** |
| `0x3B89A0` | Position Physics | Velocity / gravity / movement delta calculation |
| `0x3B9090` | Position Calculator | Collision + writes final position to entity transform |
| `0x3BEEC0` | calc_motion | Batch animation/motion processing for all entities |

`EntityPositionPhysics` consumes the injected velocity/acceleration vectors and then clears the accel block (`actor+0xA48..0xA60`) before returning. That matches the intended "write every frame" model for the inject hook.

**Strategy B hook target:** `exe+0x3BFD30` — intercept for friend entities, replace the vtable AI dispatch (vtable+0x10) with player input processing, then let `exe+0x3B89A0` (physics) run normally for full game integration.

**Live M3 note (2026-04-01):** vtable `+0x10` suppression is enough to hand local movement to Friend1, but not enough to remove all vanilla friend behavior. Left-stick solo control now works in practice, and the remaining Donald→Sora pull is currently attributed to the friend vtable `+0x28` pre-physics callback (`0x1401B0050` on the current build, tail-calling `0x1403D6870`). That callback is now the main tether-removal candidate for the next live retest.

### Still unknown (blocks further milestones)

| Name | Needed for | Notes |
|---|---|---|
| MP offset within unit slot | M1 full stats | KH2LIB GoA example suggests Slot+0x180/0x184 |

---

## Runtime Bridge Coverage

File: `runtime/src/GameBridgePC.cpp`

| Method | Status | Notes |
|---|---|---|
| `Tick()` | Implemented | Auto-discovers entities (all slots) on room change, re-points camera |
| `DiscoverEntityAddresses()` | Implemented | Camera chain (slot 0) + Slot1+0x220/0x228 (friends). Re-discovers on room transition. |
| `ReadRoomState()` | Implemented | Reads world/room/program/cutscene state |
| `ReadActorState(slot)` | Implemented | All slots: position, rotation, velocity, airborne, HP. Friends via Slot1 actor pointers. |
| `ReadEnemyStates()` | Partial | Traverses active-entity list head `exe+0x2A171C8`, resolves `actor+0xA90` handles via `exe+0x2B0D720`, reads `objectId` from `actor+0x918`, filters moveState `8/9` plus objentry prefix `B_`/`M_` |
| `WriteCameraTarget(slot)` | Implemented | Fake actor allocation + pointer redirect |
| `RestoreVanillaCamera()` | Implemented | Restores original pointer, frees memory |
| `InjectOwnedInput(slot, input)` | TODO | Input pipeline fully mapped (see Input System section). Friends are AI-only; injection requires Strategy A (direct entity write) or Strategy B (AI hook). |
| `ApplyReplicaActorState(state)` | Implemented | All slots: position/rotation/flags to entity struct. Slot 0: dual-write to buffer. All: HP + camera fake actor. |
| `ApplyReplicaEnemyState(state)` | TODO | Enemy traversal + objectId now exist; HP/spawn-group/damage writeback still needs mapping |

### Limit command gate (`[GHIDRA]`, 2026-10-02, VUH-1491; not yet checked live)

| RVA | Role |
|---|---|
| `0x2AE5690` | `limt` table pointer (`03system.bin`, loaded by `0x3F4C30`). `0x40`-byte entries: `+0x09..+0x0B` required partner character ids, `+0x30` u16 command id |
| `0x2A161E8` | `cmd` table pointer |
| `0x3E7C30(cmdId)` | plain `limt` lookup by command id |
| `0x3E7800(cmdId)` | `limt` entry only if usable now (partners present and active via `0x3D5C70` / `0x3BA720`, player-actor bit `+0x6CB`&1, a room flag) |
| `0x3D88E0(actor, cmd*, state)` | per-command menu state for limits: `5` while a limit runs (`0x2A24CC0`) or MP recharges, `2` when MP is short; `5` read as greyed out |
| `0x3D8B40` | executes a limit; re-checks `0x3E7800` |

To block limits while puppets are active, hook `0x3D88E0` and return `5` for any command `0x3E7C30` recognises.

### Actor behaviour flags `actor+0x18C` (`[GHIDRA]`, 2026-10-02; bit 6 not yet checked live)

| Bit | Effect |
|---|---|
| 6 (`0x40`) | Skips collision: actor-vs-actor separation in `EntityPositionPhysics` (`0x3B89A0` → `0x3B81D0`), and terrain collision plus ground snap in the position calculator (`0x3B9090`). Per actor; the objentry-wide equivalent is `objentry+0x0C & 2` (shared by every actor of that type). Use for non-colliding puppets |
| 14 (`0x4000`) | TakeDamage (`0x3D5E50`) passes reactFlag 0, so no hit reaction |
| `0x1000020` | Also skips the ground-snap block in `0x3B9090` |
