# Pointer Map v1

Target: `KH2 Final Mix PC (Steam Global)`  
Module base: `KINGDOM HEARTS II FINAL MIX.exe`  
Source of truth for current constants: `runtime/include/kh2coop/KH2Offsets.hpp`

## Status Legend

- `[CONFIRMED]` = Verified live in Cheat Engine on target build
- `[KH2LIB]` = Pulled from KH2 Lua library, not fully live-verified in this project
- `[GHIDRA]` = Verified by static analysis of the target executable
- `[LIVE]` = Observed through the injected DLL/scenario rig; evidence linked alongside the claim
- `[UNKNOWN]` = Not mapped yet

---

## Mapped vs Missing (Current Snapshot)

| Area | Status | Notes |
|---|---|---|
| World/room identity | Done | Static `NOW` block + **commit path** mapped (staging at `0x717018`, helper `axaAppMain+0x3A00`, `KH2J` table `0x9A98B0`). See [NOW commit path](#now-commit-path-disassembly). |
| Cutscene/transition state | Partial | Native transition request and load-completion callbacks mapped and hooked; arrival requires a completed load plus the full target location. Cutscene hold remains separate work. See [room transition lifecycle](#room-transition-request-cold-warp--verified-live-2026-10-02). |
| Unit slot stat block | Partial | `SLOT0_BASE`, `SLOT_STRIDE`, `slot::HP`, `slot::MAX_HP` confirmed. MP offset unknown. |
| Actor transform (slot 0) | Done | Entity struct layout fully mapped: position, rotation, velocity, airborne flags. |
| Actor transform (slot 1/2) | Done | Slot1+0x220/+0x228 actor pointers, entity at actor+0x640. Same layout as slot 0. |
| Entity discovery | Done | Two-strategy scan: camera actor pointer chain (primary), vtable+W+moveState heuristic (fallback). Auto-discovers on room transition via `Tick()`. |
| Position buffer array | Done | Buffer at `exe+0xAD9100`, stride `0x38`. Dual-write for physics-active rooms. |
| Camera struct | Done | Full camera struct at `exe+0x718C60` with look-at, eye position, actor pointer, distance. |
| Camera retarget | Done | Fake actor allocation + pointer redirect at `camStruct+0x50`. Implemented in `WriteCameraTarget()` / `RestoreVanillaCamera()`. |
| Enemy list root | Done | Active-entity list head at `exe+0x2A171C8`; next handle at `actor+0xA90`; handle region table at `exe+0x2B0D720`. Enemy class is objentry type `3/4`; the old moveState/name filter missed some mobs. Checked traversal completion is required to establish absence. |
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
| `0x071700A` | `NOW_EXTRA` / entrance door (byte) | `[CONFIRMED]` third byte in `NOW`; commit helper `0x152BE0` writes only this byte, not a `u16` |
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
| `SAVE+0x1C90` (19 × `0x20`) | world story flags, ending at `+0x1EF0`; the next bytes belong to separate auxiliary structures | Native readers/setter `0x39A980` / `0x39AFC0`, table initialization `0x39ACD0` |
| `SAVE+0x22F8` (8 × 19) | room-visited flags, ending at `+0x2390` | Native reader/setter `0x3BAEA0` / `0x3BAF20`, clear `0x3BAF00` |
| `SAVE+0x23AC` (known chest indices 1…411) | chest flags; GoA map chest uses `+0x23DF` mask `0x02` (overall flag 409). Unknown bits 0 and 412…415 are excluded from mirroring | Native setter/reader `0x3A1B60` / `0x3A1EA0`, chest initialization `0x402200`; local treasure table corroboration |
| `SAVE+0x24F0` (13 × `0x114`) | personal character stats, excluded from progress mirroring | OpenKH layout; native personal preservation remains a live acceptance check |
| `SAVE+0x3580` (length `0x140`) | personal inventory, excluded from progress mirroring | OpenKH and local KH2 Lua inventory table |

**OpenKH vs in-memory offsets:** There is no universal eight-byte shift.
OpenKH places world ID at `+0x0C`, after magic, version and checksum. Native
readers/writers independently establish the story and visited ranges above.
OpenKH's proposed twentieth story block overlaps native auxiliary state;
that state is excluded until its schema is understood. The progress allow
list in `common/include/kh2coop/ProgressAllowList.hpp` also masks unverified
chest bits. The known chest extent comes from a local mod treasure table,
corroborated by native addressing and the GoA chest entry; it is not proof
of the complete installed archive or all story-setter side effects.

The GoA ROM's `Warp(W,R,D,M,B,E)` writes the `NOW` block (world, room, door,
programs) and the saved location, but only redirects a transition already in
progress (from the world map). It also swaps party costumes by rewriting
objentry model-name strings before rooms load: with `obj0` = the pointer read
from `OBJENTRY_POINTER`, `obj0+0x16F0` is Donald's model name and
`obj0+0x1750` Goofy's.

### Room transition request (cold warp) — verified live 2026-10-02

| RVA | Role | Source |
|---|---|---|
| `0x152990` | **`void __fastcall RequestTransition(const LocationPacket*, u32 fadeFlags, int mode, u8 flag, int extra)`**. Starts the room-load task (callback `0x152A90`) and copies the packet into `NOW` staging `0x717018`/`0x717020` (or `0x717120`/`0x717128` when byte `0x9BA8D1` is set). 23 traced callers include the room-script Jump handler `0x3A46E0` and the world-map flow `0x154E20` → `0x151000`; this does not prove coverage of every initial-load or scripted path. | `[GHIDRA]` + `[CONFIRMED]` + `[LIVE]` |
| `0x152BE0` | **`void __fastcall CommitLocation(const LocationPacket*)`**. Copies world/room/door bytes to `NOW`; resolves map/btl/evt from the save's room table unless the corresponding packet field is not `0xFFFF`. | `[GHIDRA]` |
| `0x152680` | **`void __fastcall LoadRoom(void* task)`**. Runs room loading, including script redirects, then schedules `0x152CD0` through `0x1506B0`. | `[GHIDRA]` |
| `0x152CD0` | **`void __fastcall LoadComplete(void* task)`**. Sets `IN_FIELD` (`0x9BA8D0`) to 1, runs native room finalizers, then calls `0x1500B0(task)` (zeros the task callback and returns). The detour records completion **after** the original callback returns. | `[GHIDRA]`; hook bytes verified and installed in the live run below |
| `0x152F40` | **`void __fastcall LoadCompleteDirect()`**. Equivalent field-live/finalizer path without the task argument or task-clear tail. The detour also records completion after return. | `[GHIDRA]`; hook bytes verified and installed, but the shared live completion log does not identify which callback fired |
| `0x3A46E0` | Room-script Jump handler: builds a packet from the script operand and calls `0x152990(&packet, flags, 0 or 2, 0, op[6])`. | `[GHIDRA]` |
| `0x154E20` | World-map exit: packet with programs `0xFFFF`, then `0x152990(&packet, flags \| 1, 0, 0, 0)`. | `[GHIDRA]` |

`LocationPacket` uses `u8 world, u8 room, u16 door, u16 map,
u16 btl` (8 bytes), then `u16 evt` at `+8`; the DLL pads the buffer to 16 bytes.
The native commit consumes only the low door byte, so host-follow requests
reject doors above `0xFF` and read current door as `u8`. Programs of `0xFFFF` take the
save's per-room values. The inject DLL calls `0x152990(&packet, 1, 0, 0, 0)` from
the PerEntityUpdate hook at the start of a frame ([Warp.cpp](../inject/src/Warp.cpp),
`kh2ctl warp`). All three request/completion hook targets must match their
first 24 executable bytes before installation. The earlier cold-warp test's 50 consecutive
warps across 13 rooms in three worlds loaded cleanly.

**Load signals.** The request stages a target; the commit changes `NOW` before
the new room has finished loading. `0x9BA928` (the
load task pointer written by `0x152990`), byte `0x9BA8D1` and
`LOADING_INDICATOR` `0x8EC540` didn't change when sampled every 50 ms through a
load. Earlier code inferred loading from a ~0.55 s entity-update stall. That
heuristic is superseded: a pause can stall updates, and a same-room reload can
leave all location fields unchanged. Native completion callbacks now provide
the load generation; `NOW` equality or elapsed time alone never proves arrival.

**Lifecycle and authority (VUH-1496).** [Warp.cpp](../inject/src/Warp.cpp)
increments a transition serial when accepting a request and a load serial after
a native completion callback returns. Client-native calls to `0x152990` are
blocked; only the host-command path bypasses that detour through its trampoline.
The current session role is checked at the native call, so disconnect releases
the client lock. [EnemySync.cpp](../inject/src/EnemySync.cpp) and
[EntityHook.cpp](../inject/src/EntityHook.cpp) discard old enemy/puppet/clone and
hit-target caches without restoring fields through stale actor pointers. They
rebind from subsequent entity updates, including after same-room reloads. If
the lifecycle hooks cannot be verified, these actor writers remain suspended.

Every accepted host epoch, including a join to an already matching location,
queues a real reload. `TransitionAck(arrived=true)` is queued only after a later
load generation, the safe gameplay gate, and exact equality of **world, room,
door, map, battle and event**. The gate requires no frozen entity groups,
`IN_FIELD != 0`, no open menu, idle native timeline state and no active event
context. The elapsed cutscene timer is diagnostic only. The host advances its
epoch when the new room instance becomes live; request acceptance alone is not
client arrival. This establishes completion behavior, not proof that every
native exit path is intercepted.

**First live host-follow evidence, 2026-10-02.** The
[smoke report](../build/scenarios/20261002-142449_net_host_transitions_smoke_1/report.json)
records a passing initial `04/1A` checkpoint with zero puppet position error on
both connected instances. The
[host log](../build/scenarios/20261002-142449_net_host_transitions_smoke_1/kh2coop_inject_55600.log)
then records load serial 3 and host epoch 2 at `02/00` with door/map/btl/evt all 0.
The [client log](../build/scenarios/20261002-142449_net_host_transitions_smoke_1/kh2coop_inject_66492.log)
orders epoch-2 queued → issued (transition 3) → load complete (serial 4) → arrived
at the same full target, and the
[relay log](../build/scenarios/20261002-142449_net_host_transitions_smoke_1/relay.log)
records `TransitionAck slot=1 epoch=2 room=02/00 arrived=1`.

That run **failed** the `before_late_join` checkpoint:
[captured state](../build/scenarios/20261002-142449_net_host_transitions_smoke_1/before_late_join_failed.json)
reports both native friend positions at zero and a 500-unit puppet error in
Twilight Town. It is evidence for request/load/ACK ordering, not a passing
multiplayer acceptance run or proof of late join, same-room reload, or native
exit blocking. The missing bindings are consistent with the documented R12
party-slot limitation; the capture lacks an actor roster/raw friend pointers
needed to conclusively identify that cause. Both completion hooks use the same
log line, so this run cannot separately establish which completion RVA fired.

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
| `0x9BA8D0` | "in field" byte; the load task `0x152A90` clears it at load start; `0x152CD0` / `0x152F40` set it before running finalizers, so the byte alone is not completion proof |
| `0xB64F98` (`CUTSCENE_TIMER`+0x80) | elapsed timeline position, retained after completion; **not** an active-event predicate |
| `0xB65210` (`CUTSCENE_STATE`) | int32 timeline lifecycle; updater `0x2CC8A0` runs only in state 3, teardown `0x2C85B0` and transition reset `0x2C87C0` clear this state without clearing the timer |
| `0x2A11478` (`EVENT_CONTEXT`) | active event-context pointer; native predicate `0x3AC220` tests nonzero, completion `0x3AC000` and cleanup `0x3AC0E0` restore event policies then clear it |
| `0x8EC5C0` (`LOADING_INDICATOR`+0x80) | loading indicator |
| `0x2AE5D78` (`SPAWNS`+0x80) | **not** an enemy toggle: a rotating 0–7 index for an effect's random spread (`0x3F3FA0`) |

`CURRENT_OPEN_MENU` `0x7435D0` needs no shift. Current warp/arrival safe-state gate:
`0x2A171E8 == 0 && 0x9BA8D0 != 0 && menu == 0xFF &&
0xB65210 == 0 && 0x2A11478 == 0`.

The native GoA map chest exposed the stale-timer gate on 2026-10-02: after
the popup disappeared, frozen groups were 0, in-field 1 and menu 255, while
the timer remained 90. Independent decompilation confirmed that the timer is
retained. Runtime gate and event flags now use both lifecycle and context. The
[native chest calibration](../build/scenarios/20261002-191026_progress_chest_goa_native_open_1/report.json)
passed: an active popup had state 3 and a non-null context; completion left
timer 90 with both predicates zero. Native warps then succeeded and
`ReadRoomState()` reported no cutscene. Chest flag 409 initialized motion 152
after reload; the immediate native opening motion was 153. The separate
[three-instance chest run](../build/scenarios/20261002-191345_net_progress_chest_goa_1/report.json)
also passed client next-load mirroring, late join and a subsequent reload.
All six client applies preserved personal bytes immediately around writes;
full character/inventory/munny/EXP snapshots and four disk save hashes stayed
unchanged. This establishes chest behavior, not all native story side effects.

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

The March labels for the two sticks were reversed. Native movement/menu
calibration on2026-10-02 corrected the layout below; it matches
`KH2Offsets.hpp` and the current `kh2ctl player-input` path. See
[raw slot calibration](KH2_CONTROL_CLI.md#raw-slot-0-pulse).

| Offset | Type | Name | Notes |
|---|---|---|---|
| `+0x00` | ushort | BUTTONS | Raw button bitmask |
| `+0x02` | byte | RSTICK_X | Right stick X (0x80=center) |
| `+0x03` | byte | RSTICK_Y | Right stick Y (0x80=center) |
| `+0x04` | byte | LSTICK_X | Left stick X (0x80=center) |
| `+0x05` | byte | LSTICK_Y | Left stick Y (0x80=center) |

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

`EntityPositionPhysics` consumes several independent movement terms. Its native epilogue clears actor+0xA48 through +0xA67, including +0xA64, with four qword stores at RVAs 0x3B9042/0x3B905F/0x3B9066/0x3B906D. Post-physics zero values therefore do not establish that those terms contributed nothing during the update.

### Static movement terms (checked 2026-10-05)

Existing-binary decompilation and instruction bytes distinguish the following
terms; these are capabilities, not attribution of a live position change.

| Actor offset | Meaning on the normal physics path |
|---|---|
| `+0x690` xyz | Direct additive carried/transformed-frame delta; added at `0x3B8C8A` to `0x3B8C96` independently of velocity. |
| `+0xA48` xyz | Separate supplied displacement, passed to position calculation without the `+0xA64` scale. |
| `+0xA58` xyz / `+0xA64` | Term multiplied by the global float at `exe+0x717480` and `+0xA64` before addition; physical units remain unassigned. |
| `+0xA18` / `+0xA28` / `+0xA38` xyz | Saved entry position, requested aggregate times `exe+0x717488`, and actual position difference times the same factor. |
| `+0x6D0` xyz | Solver start: copied from current `+0x670` at `0x3B94CE`; `0x16FEC0` receives the structure at this address. |
| `+0x6E0` xyz | Selected movement supplied to that solver. `0x16FEC0` / `0x1638F0` can rewrite it during solver passes; its post-call value need not equal the original input. |
| `+0x700` xyz | Solved position output, copied to `+0x670` at `0x3B955C`. |
| `+0xA78` xyz | `(solved700 − old670 − S0) * exe[0x717488]`, written at `0x3B9535` to `0x3B9549`. `S0` is the **pre-solver stack copy** of `+0x6E0` taken at `0x3B94DA`, not sampled post-call `+0x6E0`. |

`0x3B81D0` retrieves polygon geometry and transforms a global vector into a
surface basis; its old actor-pair separation label was incorrect. The explicit
other-actor loop is in `0x3B9090`, calling `0x40AD70` with the two actors' `+0xA00`
objects. The final solved position is copied from `+0x700` to `+0x670` at
`0x3B955C`. Alternate or skipped branches can leave summary fields stale.
Sparse sequential reads cannot reconstruct all within-frame contributors.
See [exact static output and field references](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-physics-terms/field-references.md).
The [upstream calculator audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-calculator-upstream-audit/result.md) resolves the call-site pointer roles and the `S0` distinction; these static capabilities do not identify a live originating force.

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
| `ReadEnemyStates()` | Partial | Traverses active-entity list head `exe+0x2A171C8`, resolves `actor+0xA90` handles via `exe+0x2B0D720`, reads `objectId` from `actor+0x918`, filters objentry type `3/4`. External traversal currently has silent read-failure/truncation paths; an empty result alone does not prove native absence. |
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

### Actor behaviour flags `actor+0x18C` (`[GHIDRA]`, bit 6 corrected 2026-10-05; bit 6 effects not yet checked live)

| Bit | Effect |
|---|---|
| 6 (`0x40`) | Skips the `0x3B81D0` surface-derived addition in `0x3B89A0`. In `0x3B9090`, selects unfiltered integrated + local supplied + linked movement for `+0x6E0`, then still reaches `0x16FEC0`. Does not itself exclude the earlier `0x3BA970` / `0x40AD70` actor-pair path. Also gates later contact/support handling; not a general collision or ground-snap disable. The handler resolved from `actor+0x0C` has a `+0x0C & 2` test in these surface/selection guards; this is not a proven universal equivalent. |
| 14 (`0x4000`) | TakeDamage (`0x3D5E50`) passes reactFlag 0, so no hit reaction |
| `0x1000020` | Participates in `0x3BA9A0`'s unfiltered-movement selection and later contact/support guards in `0x3B9090`; does not bypass the earlier actor-pair loop or the `0x16FEC0` call. |

The post-solver contact/notification branch requires handler `+0x0C` bits 0/1 clear, `actor+0x18C & 0x1000020 == 0`, and bit 6 clear; failure can enter alternative notification handling. Later support-handle handling requires `0x3BA9A0` false, handler bit 1 clear, actor bit 6 clear and nonnull `actor+0x748`. These are selective guards after the solved-position copy, not proof that all terrain correction or ground snap is suppressed. See the [byte-check note and retained provenance](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-calculator-upstream-audit/pointer-map-note.md).

### Native enemy provenance and appearance cache (`[GHIDRA]`, 2026-10-02)

Independently corroborated static reads, additionally observed in the
[checked native census](../build/scenarios/20261002-200856_net_enemy_census_transition08_1/native_enemy_census_transition08.json).
They are diagnostic fields, not a validated suppression or replication
boundary. Equal SAVE progress hashes do not cover this cache.

| Address / offset | Meaning |
|---|---|
| `actor+0x9E8` | Controller pointer attached by `0x3B4BD0`; null is legal |
| `actor+0x9F0` | Pointer to a `0x40`-byte native spawn record, not inline bytes; record ID is u16 at `+0x1E` |
| `exe+0x2AE5E60` | Four inline appearance-cache buckets, each `0x208` bytes: room tag i32, age i32, then 256 u16 record IDs |
| `exe+0x2AE6680` | Active bucket pointer; must equal one of root `+0`, `+0x208`, `+0x410`, `+0x618` before reading |
| `exe+0x2AE6688` | u32 selection counter; IDs can change without this counter changing |
| `0x3F5980` | Selects a bucket using the NOW room byte and age; tag is not a packed world/room identity |
| `0x3F5900`, `0x3F5BE0`, `0x3F5CD0` | Test, insert and clear native record IDs; membership scans all 256 slots, including after zero gaps |
| `0x3A0580`, `0x3A0660` | Restore/back up `0x830` cache bytes from/to `exe+0x2A0C550` during native load lifecycle |

Native emission wrappers `0x3FE590`/`0x3FE650` call factory `0x3DF930` and
attach this provenance. Some callers dereference the result without a null
check, so returning null from a broad spawn hook is not safe. Compare a checked
native linked-list census, controller/record state and cache contents before
choosing an authoritative emission strategy.

### Ordinary spawn-controller hook and lookup ABI (2026-10-02)

Static disassembly and checked geometry establish the following module-relative
RVAs. They define call mechanics and bounded scope, not authoritative spawn
convergence.

| Address / offset | Meaning |
|---|---|
| `0x2A10010`, `0x2A10418` | Controller table: 16-byte `{u32 key, u32 flags, pointer}` entries and i32 count. Entry flags bit0 denotes an alternate script pointer, not an ordinary controller |
| Controller `+0/+4/+8` | u32 group key / u32 flags / header pointer |
| Controller `+0x10/+0x18/+0x20` | Runtime region head / tail / float cooldown |
| Controller `+0x24/+0x28/+0x2C` | Current/initial eligible record counts, then u8 stage; eligible counts are not living actor counts |
| Controller `+0x30/+0x38` | Spawn array / region descriptor array. Require `spawnArray == header+0x2C`, `regionArray == spawnArray + spawnCount*0x40` |
| Header `+0/+1/+2/+4/+6` | **u8 type**, u8 flags, u16 ID, u16 spawn count, u16 region count. `0x3FE334: 0F B6 01` reads the type byte; do not combine type/flags into u16 |
| Spawn record (`0x40` bytes) | u32 object ID +0; float XYZ +4/+8/+`0xC`; u8 mode +`0x1C`, position mode +`0x1D`; u16 record ID +`0x1E`, delay +`0x2A`; u8 stage +`0x30` |
| `0x2A10420`, `0x2A105D0` | Tracked activation actor / player actor; these need not match |

`0x3FF000` ABI: `void (__fastcall*)(void* controller, const float* point4)`.
RCX is the controller; RDX points to **16 readable bytes**. The verified caller
consumes no return value and returns to `0x3A5063`. Use aligned local float[4]
and call synchronously; native copies the vector and does not retain its pointer.

Exact installation fingerprints (verification spans, not manually selected
trampoline overwrite lengths):

| RVA / length | Bytes |
|---|---|
| `0x3FF000` / 15-byte entry | `48 89 5C 24 10 48 89 6C 24 18 57 48 83 EC 30` |
| `0x3A5056` / 13-byte setup/call | `48 8B 0B 48 8D 54 24 20 E8 9D 9F 05 00` |
| `0x3E0F10` / 16-byte lookup entry | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57` |
| `0x1E2300` / 5-byte comparator | `2B 0A 8B C1 C3` |

The update clears transient flags `0x18`, respects disabled bit0/event gating,
decrements cooldown, tests linked regions and performs emission/cache bookkeeping.
Bit3 means a region accepted; type2 sets header+`0xE` and dispatches delayed
records. Qualified-client Hold skips the whole tick, including cooldown; it is
a scoped pause. Unsupported/unavailable scope cannot be claimed controlled.

Direct read-only lookup `0x3E0F10` has ABI
`const void* (__fastcall*)(int32_t directObjectId)` (ECX ID, RAX pointer/null).
It tries three table pointers at `0x2A25030`, `+8`, `+0x10` in order. Each
nonnull table has i32 count +4 and entries +8, stride `0x60`. Its comparator
subtracts entry ID from the low32 bits of a pointer-sized numeric key, not a
pointer to ID storage. Do not replace this with OBJ0-only or `base+ID*stride`.
Verify bytes, checked nonnegative table counts/spans, then use a POD-only SEH
leaf. Validate returned span/alignment, ID, type and name; recheck roots/counts
and lifecycle. Faults/changed tables are unavailable; null is unresolved, not
combat. Caps64 controllers, 256 records and 65536 object entries are safety
bounds, not discovered native maxima.

General resolver `0x3DFEB0` also maps party/drive/save aliases. The static direct-ID
boundary excludes zero, high-four-bit modifiers and hexadecimal aliases
`236,237,238,23B,23C,23D,23F,240,2C0,319,31A,3EE,62A,62B`.
Qualify an entire ordinary type2 controller only when **every** record, including
later-stage/cached records, has mode2, positionMode0, finite fixed XYZ and a
direct objentry type3/4 with name prefix other than `F_`. Mixed/unsupported
records exclude the whole controller; header30 is observed coverage, not the
scope definition. Do not rewrite records or invoke a subset of emitters.

Runtime region nodes are `0x70` bytes: vtable +0, inverse float4x4 +8, extents XYZ
+`0x48/0x4C/0x50`, next encoded handle u32 +`0x58`, descriptor pointer +`0x68`.
Each source descriptor is `0x40` bytes: u16 kind/category +0/+2, float position
XYZ +4/+8/+`0xC`, extents +`0x10/0x14/0x18`, Y rotation +`0x20`. Walk the actual
list, since native setup excludes some categories; require aligned source
pointers inside the checked descriptor array/count.

| Kind | Vtable / predicate RVA | Native predicate |
|---|---|---|
| BOX (0) | `0x5D4F98` / `0x421180` | Inclusive `-extent <= transformed XYZ <= extent` |
| CYLINDER (1) | `0x5D4FB0` / `0x421250` | Inclusive Y bounds, then `(x/extentX)^2+(z/extentZ)^2 <= 1` |
| INFINITY (2) | `0x5D4FC8` / `0x421320` | Unconditional true (`B0 01 C3`), independent of point/matrix values |

Predicates copy float4 and force w=1. Captured inverse matrix M transforms via
`q[j]=M[j]*x+M[j+4]*y+M[j+8]*z+M[j+12]`, j=0..2. Native accessor `0x3B5B40`
selects actor+`0x70` if parent handle u32 +`0x6A0` resolves nonnull through
`0x3DA520`, otherwise +`0x670`. Generic resolution uses the 64-entry table at
`0x2B0D720`: `table[(handle & 0x7FFFFFFF)>>25] | (handle & 0x01FFFFFF)`.
Bit31 is ignored, raw handle0 is null, and invalid/failed reads cannot imply null.

The [geometry03 artifact](../build/scenarios/20261002-204742_net_enemy_census_transition03_1/native_enemy_census_transition03.json)
has eight valid native/causal captures out of nine; the overall run **failed**
because snapshot0/client1 failed actor-identity stability. Later snapshots1/2
captured all seven header30 BOX regions on every peer. Source descriptor and
inverse-matrix/extent bytes matched across valid captures. At snapshots1/2, each
peer's geometry placed the host point outside all seven and both clients inside
indices5/6, without edge uncertainty. This is offline float32 containment, not
instrumented predicate returns or an authority-fix pass. See the
[extraction receipt](../build/rig/native_geometry_transition03_summary.json) and
[scenario limits](SCENARIOS.md#native-enemy-census-diagnostic).

### Fixed type2 emission observation and replay limits (2026-10-02, static verified)

Successful fixed-wrapper return `0x3FE83F` inside delayed type2 emitter
`0x3FE6F0` is a verified creation boundary, not a complete replay API.
The opt-in `KH2COOP_SPAWN_TRACE=1` observer now covers all callers of fixed
`0x3FE590` and generated `0x3FE650`, plus explicit dispatcher/script scopes.
It passes each original through once, preserves genuine returns and distinguishes
checked nonnull, null and unavailable observations. Wrapper evidence outside
an activation tick is retained separately; it cannot claim completed tick
bookkeeping. Source and byte checks are offline evidence; native execution and
diagnostic coverage still need a live run.

| RVA | Windows x64 ABI / role |
|---|---|
| `0x3FE6F0` | `void __fastcall(void* controller)`; native type2 occupancy/cache/stage/cooldown checks and record selection |
| `0x3FE590` | `void* __fastcall(const void* record, void* controller)`; RCX record, RDX controller, RAX actor/null; fixed native position/yaw, constructor, provenance and eligible cache insertion |
| `0x3FE650` | `void* __fastcall(const void* record, void* controller, const float* point4)`; R8 explicit point; separately traced generated-position path |
| `0x3FE320` | `uint64_t __fastcall(void* controller, const void* region)`; RCX/RDX; observer preserves full RAX, not just a decompiler's narrow return type |
| `0x42DC10` | One RCX pointer to native uint32 handle storage; advances stage then tail-jumps to the dispatcher |
| `0x3DF930` | `void* __fastcall(uint32_t objectId, const float* point4, float yaw)`; ECX, RDX, XMM2; generic factory alone omits wrapper metadata |
| `0x3B4BD0` | `void __fastcall(void* actor, void* controller, const void* record)`; RCX/RDX/R8; provenance and actor parameters, already called by the wrapper's virtual path |

Verified fixed-wrapper entry (15 bytes):
`48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30`.
At `0x3FE83A`, `E8 51 FD FF FF` calls that wrapper; return `0x3FE83F`
is before caller-owned cooldown assignment at `0x3FE99E` and stage advancement.
Generated wrapper call `0x3FE984` instead returns `0x3FE989`; do not merge scopes.
Observe enclosing controller tick completion to obtain truthful post-state.

Wrapper cache insertion uses objentry-type mask `0xA02010`: it includes mob
type4 (also 13/21/23), **not boss type3**. A successful boss creation need not
insert its record ID. The wrapper does not perform the caller's region,
occupancy, cache, stage, cooldown or count/event reconciliation. `0x3FF780`
is unsafe as a replay convenience: it increments initialCount and resets
currentCount to initialCount, destroying accumulated death bookkeeping.
Calling the whole emitter or stage helpers lets divergent local state make
new population decisions. No safe single-call bookkeeping reconciliation API
has been verified.

Controller task `0x3A4F90` is scheduled at priority 12000 by `0x3A4E10`;
actor updating uses priority 20000. Executing creation from a head-actor callback
is not proven equivalent to the native controller lane. No runtime/worker thread
may call native creation. Native lethal/count/event behavior must remain on its
verified path; do not compensate with HP/count/cache writes.

Cross-peer addressing requires epoch/full location/session plus unique ordinary
controller groupKey/headerId, recordIndex/nativeRecordId/objectId and validated
static descriptor content. GroupKey alone repeats (headers 30..33 share `b_00`);
pointers/table indices are local, and controller reinitialization can occur
within a room. Record ID is not a creation generation: lookup `0x3B41E0` maps
ID0 to `0x70`, searches without a controller discriminator, and filters actors.
Cache routines compare signed int16 slots while wrappers load IDs unsigned;
an initial replay proof must reject zero/high-bit IDs and room-wide collisions,
using IDs 1..`0x7FFF` until broader semantics are proved. These are replay limits,
not changes to existing activation qualification.

Successful wrapper order need not equal the next census's manifest netId order.
Correlate the returned actor under checked native identity before publishing a
creation event; repeated emissions need distinct host event identities and
idempotent delivery. Matched initial cache/count enrollment, alive removal versus
death, stage/refill progression, mid-room reinitialization and late-join history
remain blockers. Creating the visible actor alone cannot establish that state.

### Alive removal, native death and script bypasses (2026-10-02, static verified)

These Windows x64 ABIs were checked by decompilation and register-bearing
bytes in the installed executable. They are observation/API candidates, not
permission to invoke a repair on an arbitrary actor.

| RVA | ABI / native effect |
|---|---|
| `0x3FFD90` | `void(controller, actor)` in RCX/RDX; removal bookkeeping only, no disposal |
| `0x411800` | `void(actor)` in RCX; controller from `actor+0x9E8`, calls `3FFD90`, then disposes through `3B45C0` |
| `0x411470` | `void(typeHandler, actor)` in RCX/RDX; common type-handler removal path with bookkeeping/disposal |
| `0x3B45C0` | `void(actor)` in RCX; resource teardown and deferred deletion marker |
| `0x3D4A40` | `void(actor)` in RCX; guarded lethal notification before setting `actor+0x9B8` bit 2 |
| `0x3FED10` | `void(controller, actor)` in RCX/RDX; conditional death-count dispatch |
| `0x3FED40` | `void(controller)` in RCX; native count decrement and events |

For a living delayed-mode-2 record, `3FFD90` clears its appearance-cache ID
and resets controller cooldown without decrementing the battle count. Dead
removal recognizes bit 2 and normally retains the record's cache ID. Native
count events include `3AABE0(3, headerId)` and aggregate-zero `3AABE0(2, 0)`.
Other death wrappers reach count bookkeeping independently; neither HP alone
nor a later census disappearance proves which lifecycle side effects ran.
Never call bookkeeping alone, represent alive removal as HP zero, or repeat
native count/cache changes after a disposal path already performed them.

Generic actor update `3BFD30` can dispatch type-handler virtual slot `+0x38`
at `3BFF58` from normalized fade state (`actor+0xA08`, `+0xAAC`) and flags.
Those values are not HP. A removed actor can cease blocking occupancy before
allocation is freed. Exact subtype/caller proof is needed before replay or
selective suppression; broad suppression would also affect death and teardown.

**Selected actor retirement, 2026-10-05 (saved PE; not live lifetime proof).**
The deletion marker is `actor+120` bit `0x80000`. `3BF300` first processes the
previous deferred list, then unlinks marked actors from the active list using
encoded `+A90` links and queues them for a later sweep. Active-list absence is
therefore earlier than allocator release. The selected ordinary Shadow handler
is `7528E8 -> vtable 5D2D68`, slot0 `419AB0`.

| RVA | Checked selected release boundary |
|---|---|
| `0x3B4E80` | RCX=actor; invokes handler slot `+30` unless `actor+9B8` bit `0x40` is already set, then marks it after normal return. This callback is separate from the later slot0 dispatch. |
| `0x419AB0` | RCX=handler, RDX=actor; nonnull actor passes through subtype/base cleanup `3F8E00 -> 3B3D60`, then original actor base goes to `152570` at `419AC8`, return `419ACD`. No actor read is safe after that release. |
| `0x152570` | RCX=pointer becomes RDX; loads RCX from `[9BA920]` and tail-dispatches its vtable slot `+10`. Base-cleanup release at return `3B3DBC` frees a loaded member, not the actor base. |
| `0x19C470` | RCX=allocator domain, RDX=pointer; normal nonnull path synchronously unlinks the block header, subtracts its recorded size, poisons the block with `0xEFACCAFE`, and clears its previous link. Arena release, not OS deallocation or demonstrated reuse. |

Binding writers `152450` and `152680` use constructor `19C3A0`, which installs
the `kn::MemoryAllocator` primary vtable `5B2BB0`; slot `+10` resolves to
`19C470`. Release header is `pointer-20`, size is read at `pointer-10`; do not
infer that size from the factory's `0xD50` request. Helper `3A08F0` and the
paired synchronization helpers remain uncharacterized. Null, changed live
binding, unwind or partial execution cannot establish successful retirement.

The current lifecycle probes do not observe these completion boundaries.
Encoded handles identify an address/region, with no creation generation;
same-address reuse remains ambiguous. These static facts do not authorize
absent-dead reconstruction. Exact bodies, callsites and limits are in the
[release audit](../build/rig/vuh1508-dead-pack-prep-20261005-01/dead-pack-final-release-audit/result.md)
and [allocator binding audit](../build/rig/vuh1508-dead-pack-prep-20261005-01/dead-pack-allocator-binding-audit/result.md).

The full 68-byte body of script callback `42DC10` verifies stage advancement
`3FFE40`, followed by tail jump `42DC49 -> 3FE320(controller, null)`, bypassing
`3FF000` even for type 2. Its original trampoline can leave the dispatcher with
a DLL return address: an unavailable executable caller RVA is expected in
that case. Explicit script TLS records dynamic enclosure, not immediate
dispatcher ancestry. Return `3FE83F` identifies the fixed emitter branch, not
the full producer path. All-caller fixed/generated observations outside the
activation tick retain wrapper evidence without claiming tick bookkeeping.

The opt-in lifecycle module independently byte-gates `3FFD90`, `3B45C0`,
`3D4A40`, `3FED10` and `3FED40`. Its installed mask exposes partial coverage;
the other subtype wrappers and `3AABE0` events are not hooked. Pre/post actor,
controller/cache and full native stamps retain nested entry/parent sequence.
Disposal can invalidate poststate, and count decrement has no actor argument.
Only the first registered game thread samples role/Warp callbacks; unknown
threads retain raw NOW and unavailable serials. These probes are observations,
not a replay or reconciliation API. Generic factory bypasses, actual allocation
release and same-address/controller reincarnation remain unproven; current
census/netId correlation is diagnostic. See `SCENARIOS.md` for saved-log auditing
and `ENEMY_PARITY.md` for the retained wave failure and offline test scope.

**Scoped predicate extension, 2026-10-03 (saved-PE verification; no live install).**
Admission `3A1F00(float XMM0) -> AL` reads float32 limit/used at `2A0F7DC` /
`2A0F830`; the factory passes unsigned-byte objentry weight `+54`, returning
to `3DFA0B`. Type-4 allocation calls `152430(size_t RCX) -> RAX` with `0xD50`,
returning to `3DFA7D`. That allocator entry tail-forwards through a vtable;
its decompiler `void` return is incorrect. Scoped observers preserve actual
arguments/results and never dereference returned allocation storage.

Removal predicate `3DAC30(actor RCX) -> AL` is reached by the type-4 tail thunk
`419B90`, retaining ordinary caller `3BFD6F`. Its script child `3B4420` returns
to `3DAC3E`; auxiliary child `3CE550` returns to `3DAC9C` and tests dword `+14`
on its genuine argument. Actor boundary operands are qword `+5B0`, dword
`+5B8`, qwords `+80` / `+98` and encoded dword handle `+BB4`. They are sampled
operands, not extra native resolutions or immutable identity. New predicate
events end before the later virtual removal/disposal call. Full byte guards
and caller-site checks gate the observers; partial masks, foreign calls,
unwinds, repeated children and loss cannot establish a branch conclusion.
The [static capture contract](../build/rig/wave_predicate_capture_plan_20261003.md)
records exact bytes against PE SHA256 `9002B2DE6A1F91A790BD0673DE125D1CF833F7942BFEC827CDCF6BA64D5849ED`.
