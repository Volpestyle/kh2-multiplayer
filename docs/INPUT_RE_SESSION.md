# Input System RE Session — 2026-03-31

Tool: Ghidra (static analysis of KH2FM Steam Global binary)
Target: `KINGDOM HEARTS II FINAL MIX.exe` (Steam Global)
Session goal: Map the input pipeline for M3 (local input injection for friend slots)

---

## Summary

The KH2 input system was fully traced from hardware to game action consumption.
The critical finding is that **friend characters do not use the input system at all** — they are entirely AI-driven. This means M3 input injection cannot simply redirect gamepad input to a friend slot; it requires either direct entity manipulation (Strategy A) or hooking the friend AI (Strategy B).

---

## Current M3 Status (2026-04-01)

- `kh2coop_inject.dll` now hooks `PerEntityUpdate` on a fresh KH2 process and discovers the friend type-handler vtable at runtime.
- The inject path now reads KH2's raw input buffer (`INPUT_STRUCT_PTR` / raw slot layout) instead of calling `XInputGetState()` directly. This fixed the Steam-Input-visible / XInput-invisible controller mismatch on the current machine.
- F5 solo mode works: it suppresses Sora input, toggles control between Sora and Friend1, and can drive Donald locally.
- `SuppressSoraInput()` must preserve the processed-entry metadata tail. Zeroing the full processed entry crashes KH2 because the next button-mapper pass dereferences a live raw-input pointer at `processed_entry+0x48`.
- Left-stick movement and facing alignment were corrected during live smoke tests on 2026-04-01.
- Remaining blocker: Donald still gets pulled back toward Sora by residual vanilla friend behavior after the main AI callback is suppressed. Current hypothesis is that the friend pre-physics callback at vtable `+0x28` is still applying follow/tether steering.
- Operational note: live unload/reload of the injected DLL is still unstable. Use a fresh KH2 launch for retests instead of hot-swapping the loaded module.

---

## Trace Walkthrough

### Step 1: Entry points

Starting from the known `INPUT` address (`exe+0xBF3120` from KH2 Lua Library), searched for named input functions in Ghidra:

| Function | Address | Role |
|---|---|---|
| `Axa.DXInput.init` | `exe+0x1335A0` | Initializes DirectInput8 keyboard + mouse |
| `Axa.DXInput.exit` | `exe+0x133250` | Cleans up DXInput |
| `GetSteamInput` | `exe+0x14D380` | Returns `ISteamInput*` singleton |
| `XInputGetState` | import | Called from gamepad enumeration |

DXInput state struct global: `exe+0x8BB290` (referenced by init, exit, poll, and enum functions).

### Step 2: DXInput struct and per-controller layout

`FUN_140134FF0` (`exe+0x134FF0`) is the per-controller update function:
- Takes controller index as `param_1`
- If `param_1 == 0`, calls `FUN_140132F90` (DXInput keyboard/mouse poll with critical section)
- Checks `GetForegroundWindow()` matches game window before reading
- Uses stride `0x140` per controller in the DXInput struct
- Calls through function pointer at `DXInput_struct + 0xD0 + index * 0x140`

Per-controller struct offsets (within DXInput global at `exe+0x8BB290`):
- `+0x18` (`0x8BB2A8`): input data start
- `+0x28` (`0x8BB2B8`): controller status
- `+0x60` (`0x8BB2F0`): connected flags
- `+0xD0` (`0x8BB360`): vtable/function pointers
- Stride: `0x140` per controller

### Step 3: Gamepad enumeration (`FUN_140133970`)

Address: `exe+0x133970`

This function binds physical controllers to gamepad slots:
1. Iterates XInput devices 0–3, calls `XInputGetState` for each
2. Checks assignment flags at `exe+0x8BB280` (XInput) and `exe+0x8BB284` (Steam Input)
3. Stores controller index at `gamepad_struct + 0x10`
4. Sets type flag at `gamepad_struct + 0x4C` (0=XInput, 1=Steam Input)
5. Sets up lambda callbacks at `+0x80` (pad data), `+0xC0` (button), `+0x100` (disconnect)
6. For Steam Input, calls `FUN_1404485C0` with controller-specific type masks

### Step 4: Main input collector (`FUN_140105810`)

Address: `exe+0x105810`

This is the central per-frame input function. `param_1` is a pointer to the input state struct (stored at `exe+0x79CF00`).

Key behavior:
1. **Clears all raw input slots** — buttons=0, sticks=0x80808080 (centered)
2. **Calls `FUN_140134FF0(index)`** for each connected controller to read raw hardware state
3. **Active controller selection** — finds the first controller with any non-zero input
4. **SWAPS active controller to slot 0** — if controller N has input and N≠0, swaps all 0x44 bytes of slot N with slot 0. This means **only slot 0 ever drives the player character**.
5. **Mouse input** — reads cursor position, processes mouse buttons via DXInput
6. **Keyboard state** — double-buffered at `struct+0x12A0` (toggled via `struct+0x14A0`)
7. **Calls `FUN_140102CA0(param_1)`** — keyboard/mouse key binding dispatch

The per-controller swap at step 4 is the definitive proof that **KH2 has no native per-slot input routing**.

### Step 5: Keyboard binding dispatch (`FUN_140102CA0`)

Additional Step 4 note:
- The collector writes the original active raw slot index to `input_struct + 0x129C` before it swaps that controller into raw slot 0.
- That offset lets the inject DLL reconstruct controller indices after the swap, so Friend1/Friend2 can still target controller indices 1/2 even when KH2 has moved the active controller data into raw slot 0.
- This is now the preferred in-process input source because direct `XInputGetState` calls from the injected DLL can miss controllers that KH2 receives through Steam Input or another remapped path.

Address: `exe+0x102CA0`

Reads from a key binding linked list at `struct+0x14B0`. Each binding entry contains:
- `+0x1C`: packed key code (low 16 bits = primary key, high 16 bits = secondary/modifier)
- `+0x22`: target slot index (low nibble) and flags
- `+0x23`: held state flag
- `+0x24/+0x26`: analog delta values (short)

Bindings are processed against the keyboard buffer and/or mouse buttons. Results are written to the raw input slot at `struct+0x18 + slot_index * 0x44`:
- Button bits are OR'd into `slot+0x00` (ushort)
- Analog axes are accumulated at `slot+0x02..0x05`

Supports input layout remapping (3 layouts via `struct+0x14A4`: 0=default, 1=layout1, 2=layout2).

### Step 6: Game loop callback (`FUN_14039BF00`)

Address: `exe+0x39BF00`

Registered as a periodic callback via `FUN_14014F770(task_system, 0, 110000, FUN_14039BF00)` during input init.

Processing:
1. Calls `FUN_140105810(DAT_14079CF00)` — raw input collection
2. Calls `thunk_FUN_140579ADA` — secondary processing
3. Calls `FUN_14039BBE0` — additional state update
4. **Iterates 2 processed button state entries** starting at `exe+0xBF31C8` (stride `0x68`):
   - Calls `FUN_14039C720(entry_base)` — button mapping
   - Computes new-press, release, and auto-repeat masks
   - Handles auto-repeat timer

Loop range: `exe+0xBF31C8` to `exe+0xBF3297` (exactly 2 entries).

### Step 7: Button mapper (`FUN_14039C720`)

Address: `exe+0x39C720`

Maps raw pad button bitmask to game action bitmask:
1. Reads raw buttons via double-indirection: `**(entry_base + 0x48)` — pointer to pointer to raw button data
2. Iterates mapping table at `exe+0x5C3420`: each entry is `{mask, output_bits, next_mask}` (3 × 8 bytes)
3. If `(raw & mask) == mask`, ORs `output_bits` into result
4. Also supports a dynamic overlay mapping table at `exe+0xBF3330`
5. Copies analog stick data via `MEMCPY_4FLOATS` (`exe+0x1A8E60`)

### Step 8: Processed button state array

Fixed addresses in the data section:

| Entry | Base address | Size |
|---|---|---|
| Entry 0 | `exe+0xBF31A0` | 0x68 bytes |
| Entry 1 | `exe+0xBF3208` | 0x68 bytes |

Layout per entry:
| Offset | Type | Description |
|---|---|---|
| `+0x00` | ulonglong | Current frame button bitmask (game actions) |
| `+0x08` | ulonglong | Newly pressed this frame |
| `+0x10` | ulonglong | Released this frame |
| `+0x18` | ulonglong | Auto-repeat trigger |
| `+0x20` | 16 bytes | Left stick analog (4 floats) |
| `+0x30` | 16 bytes | Right stick analog (4 floats) |
| `+0x40` | pointer | Context/mode reference |
| `+0x48` | dword | Flags (bit 0=disabled, bit 1=Steam-vs-XInput) |

### Step 9: Context switch (`FUN_14039B580`)

Address: `exe+0x39B580`

Called from ~30 sites throughout the game (battle start, menu open, cutscene, minigame, room transition, etc.). Sets the active button mapping context:
1. Loads mapping overlay from `(&DAT_140749600)[context_id]`
2. Masks all button entries with `_DAT_1405C3D40` / `_UNK_1405C3D48` (defines which buttons valid in context)
3. Clears analog and repeat state
4. Stores context ID at `exe+0xBF3198`

### Step 10: Input consumers

Xrefs to `exe+0xBF31A0` (processed entry 0) show reads from:
- `FUN_14039B580` — context switch (read + mask + write)
- `FUN_14039B640` — write (post-processing)
- `FUN_14039BDE0` — write (additional processing)
- `FUN_14039B6D0` — read/write
- `FUN_14039B780` — data reference
- `FUN_14039B7F0` — init/reset
- `FUN_140023870` — write (early game init)

`FUN_14039B580` (context switch) is called from 30 unique sites spanning addresses `0x150770` through `0x39BBB0`, covering battle, menu, event, and transition systems.

---

## Friend AI Architecture (RTTI Discovery)

RTTI type info strings reveal the friend class hierarchy:

```
Friend@kn
  └→ FRIEND@YS
       └→ PARTY@YS
            └→ BTLOBJ@YS
                 └→ STDOBJ@YS
                      └→ OBJ@YS
```

Parallel hierarchy in `gb` (battle?) namespace:
```
FRIEND@gb → OBJ@gb
FRIEND_FORMATION@gb
```

Additional RTTI classes:
- `FriendPersonality@kn` — AI behavior/personality config
- `ACTION_FRIEND_FLY@kn` — friend fly action
- `ACTION_FRIEND_FLY_DASH@kn` — friend fly dash
- `ACTION_FRIEND_FLY_BLOW@kn` — friend fly blow
- `GAUGE_FRIEND@dk` — friend gauge display

Debug strings:
- `"dbg/friend_param.bin"` — debug friend parameter file
- `"friend attack:"` (at `exe+0x5C3F50`) — debug output
- `"gentle friend"` — behavior mode string
- `" friend1:"`, `" friend2:"`, `" friend3:"` — debug slot labels
- `"FRIEND RECOV    %3d"` — friend recovery formatting
- `"W_FRIEND"` — world friend flag
- `"FRIEND"` — entity type label

---

## M3 Injection Strategies

### ~~Strategy A — Puppet Mode~~ (skipped)

Direct entity manipulation without hooking. Build on existing `ApplyReplicaActorState()`:

1. **Position/rotation** — already working via dual-write to entity struct + buffer array
2. **Animation ID** — find offset in actor struct (CE session needed), write to display correct animation on remote clients
3. **Action triggers** — find attack/guard/dodge state fields in actor struct, write to trigger combat actions
4. **AI suppression** — may need to NOP or freeze the friend AI to prevent it from overwriting our writes

Pros: no DLL injection needed, works with external `ReadProcessMemory`/`WriteProcessMemory`.
Cons: no physics/collision integration, animations may not play properly, combat interactions may be incomplete.

Current status: rejected for M3. Live testing showed this path would stay cosmetic-only and would fight the friend AI every frame.

### Strategy B — AI Replacement Hook (current implementation path)

In-process DLL injection to hook the friend AI decision function:

1. **Hook `PerEntityUpdate`** — done. This is the stable entry point for identifying live Friend1/Friend2 actor objects each frame.
2. **Hook the friend AI dispatch** — done for vtable `+0x10`. This suppresses the main AI callback and injects movement input.
3. **Read KH2-owned raw input memory** — done. This avoids the machine-specific problem where KH2 sees a controller through Steam Input while `XInputGetState()` returns no pads.
4. **Re-apply movement before physics** — partially done. `EntityPositionPhysics` correctly consumes our writes at `actor+0xB98/+0xA58`, but a remaining friend follow/tether path still appears to run later in the update.
5. **Current next step** — suppress or replace the friend pre-physics callback at vtable `+0x28` and re-test on a fresh process.

Pros: full game integration, proper animations, combat works correctly.
Cons: requires DLL injection, more complex implementation, potential stability issues.

### Recommended sequence

1. Keep Strategy B as the active path; Strategy A is no longer worth pursuing for M3.
2. Validate the vtable `+0x28` pre-physics suppression path to remove Donald's residual tether to Sora.
3. Once local solo movement is fully clean, wire network-received input frames into the same hook path for M4.
4. Only after movement is stable, expand into attack/guard/dodge injection and combat verification.

---

## Next Steps (CE dynamic sessions)

1. **Residual tether / follow path** — confirm whether friend vtable `+0x28` (`0x1401B0050` → `0x1403D6870` on the current build) is the remaining follow-steering callback that pulls Donald back toward Sora after main AI suppression.
2. **Combat action path** — once tethering is removed, identify which friend callbacks or state fields need to be driven for attack/guard/dodge so combat lands natively.
3. **Optional tuning reload** — if iteration speed becomes the bottleneck, add a reloadable config layer for stick source, inversion, deadzone, and movement speed. Do not prioritize full DLL hot-reload yet.
