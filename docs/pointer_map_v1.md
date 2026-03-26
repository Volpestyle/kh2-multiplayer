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
| Enemy list root | Partial | Stride confirmed (0x6C00/0x72F0 per type), moveState=8/9. Root pointer and count still unknown. |
| Input injection | Missing | Global input address known (`0x0BF3120`), per-slot injection path not mapped. |
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

**Stride varies by enemy type.** A fixed stride cannot be assumed. Enemy list root pointer and count are still unknown.

### Still unknown (blocks further milestones)

| Name | Needed for |
|---|---|
| `enemy::LIST_PTR` | M5 enemy replication |
| `enemy::COUNT` | M5 enemy replication |
| Per-slot input injection path | M3 friend-slot control |
| Animation ID offset in entity struct | M4 animation sync |
| MP offset within unit slot | M1 full stats |

---

## Runtime Bridge Coverage

File: `runtime/src/GameBridgePC.cpp`

| Method | Status | Notes |
|---|---|---|
| `Tick()` | Implemented | Auto-discovers entities (all slots) on room change, re-points camera |
| `DiscoverEntityAddresses()` | Implemented | Camera chain (slot 0) + Slot1+0x220/0x228 (friends). Re-discovers on room transition. |
| `ReadRoomState()` | Implemented | Reads world/room/program/cutscene state |
| `ReadActorState(slot)` | Implemented | All slots: position, rotation, velocity, airborne, HP. Friends via Slot1 actor pointers. |
| `ReadEnemyStates()` | TODO | Needs enemy list root pointer + count |
| `WriteCameraTarget(slot)` | Implemented | Fake actor allocation + pointer redirect |
| `RestoreVanillaCamera()` | Implemented | Restores original pointer, frees memory |
| `InjectOwnedInput(slot, input)` | TODO | Needs per-slot input path RE |
| `ApplyReplicaActorState(state)` | Implemented | All slots: position/rotation/flags to entity struct. Slot 0: dual-write to buffer. All: HP + camera fake actor. |
| `ApplyReplicaEnemyState(state)` | TODO | Needs enemy list root pointer |
