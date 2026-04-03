# Friend Control Handoff — 2026-04-02 (Session 3)

## Current state

Press F5 to toggle solo mode. Left stick moves Donald with **proportional speed** (analog gradient from walk to run). Camera toggles correctly both ways. Facing persists when idle (no snap to Sora). **Animation is still driven by follow-distance-to-Sora** — this is the one remaining major bug.

## What works

- **F5 solo mode toggle** — camera, movement, input all toggle cleanly
- **Camera follows Donald** — retargets `camStruct+0x50` every frame; restores to Sora on F5-off
- **De-tether** — `actor+0xBA8` (follow-steering timer) held at 999.0
- **Left stick moves Donald** — camera-relative, proportional speed (raw analog input)
- **Right stick camera orbit** — preserved
- **Facing persists at rest** — `g_lastFacingAngle[]` cache, written every frame regardless of stick magnitude
- **Sora frozen** — entity-level velocity/accel zeroing in post-physics
- **Proportional speed** — raw stick bytes (0x00–0xFF) give analog gradient; processed entry was digital (0 or ±1)
- **MovementDispatch hook installed** — `FUN_1403d5e50` hooked via MinHook at RVA `0x3D5E50`

## The animation problem (remaining major bug)

### Symptom
Donald's idle/walk/run animation is driven by his distance from Sora, not by the player's stick input. When not moving, he plays whichever animation the vanilla AI selects based on follow-distance (often walk or run), instead of idle.

### Root cause
The friend AI's behavior timer path (`FUN_1403c3bd0` → vtable+0xE8 → `FUN_1403d5e50` → `FUN_1403d2eb0`) computes a speed delta based on follow-distance-to-Sora. This delta drives the motion accumulator which controls the idle↔walk↔run animation transitions. Our movement injection changes position/velocity but not the animation selection.

### What we tried and learned (session 3 deep RE)

1. **`FUN_1403c6dc0(actor+0x158, animId, 0, 0)`** — "setAnimationDirect", the motion controller setter. Both Sora and friends use this path for special animations. **Does NOT work** when called from our hook or CE. The function has an internal `FUN_1403a6420()` call that depends on register state from a prior lookup — may fail outside the normal AI context. Verified: no animation change, no crash.

2. **Writing `actor+0x180` directly** — this field reflects the current animation but is **read-only** from the animation system's perspective. Writing it every frame (via CE timer) does NOT change the visual animation. The field is set by the motion playback chain, not read by it.

3. **Writing the motion accumulator (`motTable[0]`)** — this is the speed index at `*(actor+0x5C0)`. Setting it to 0/max doesn't trigger animation changes. The accumulator is updated by `FUN_1403c0860` inside `FUN_1403d2eb0`, but the animation transition happens through side-effects in that call chain, not from the accumulator value itself.

4. **Calling `FUN_1403d2eb0(actor, -maxVal, 0, 0)` directly from CE** — this DID change the animation, but with delta=-18 it triggered `vtable+0xB0` (the "stopped" callback) which played animation 0x36 = **KO/sleep**, not idle. This proved the function CAN drive animation changes, but only through its internal call chain.

5. **Skipping the friend AI entirely** — animations freeze completely. The AI drives the motion playback chain; without it, no animation ticks forward.

6. **HookedMovementDispatch** (current approach, installed but **unverified**) — hooks `FUN_1403d5e50` and replaces the speed delta for controlled friends. The hook checks if the actor matches `g_friend1Actor`/`g_friend2Actor` and substitutes a stick-magnitude-based delta. **Needs debugging** — the hook fires for all entities, and the friend actor match check may fail if friend pointers aren't refreshed in time, or the function may be called with unexpected parameters.

### Key RE findings — animation call chain

```
Friend AI (FUN_1403c3660, RVA 0x1B0020):
  └─ timer at actor+0xDC0 counts down
     └─ FUN_1403c3bd0 (behavior update):
        reads motTable max (18), multiplies by global speed factor
        └─ vtable+0xE8 (FUN_1401b03d0) → thin wrapper
           └─ FUN_1403d5e50 (movement dispatch) ← OUR HOOK
              ├─ if delta < 0: FUN_1403d3cf0 (deceleration path)
              │   ├─ if motTable+0x180 + delta < 0: FUN_1403c20a0 → idle state reset
              │   └─ else: FUN_1403c0890 → partial decel
              └─ FUN_1403d2eb0 (accumulator update)
                  ├─ FUN_1403c0860 → clamp(current + delta, [min, max])
                  ├─ if delta > 0: FUN_1403dcbb0 (accel side-effect, Sora-only)
                  ├─ if delta ≤ 0: FUN_1403dcc10 (decel side-effect, Sora-only)
                  └─ if accumulator == 0 && channel == 0: vtable+0xB0 → KO anim
```

Sora's equivalent path:
```
Sora AI (FUN_1403a92c0, RVA 0x404A20):
  └─ vtable+0xE8 (FUN_140404EE0) → FUN_1403a85f0
     └─ FUN_1403d5e50 (same movement dispatch) ← SAME FUNCTION
```

Both converge on `FUN_1403d5e50`. The animation system is the same for both — only the speed delta input differs (Sora: from player input, friend: from follow-distance).

### Why vtable+0xB0 gives KO, not idle
When `FUN_1403d2eb0` sees the accumulator hit 0, it calls `vtable+0xB0` on the entity's type handler. For friends, this triggers animation 0x36 (KO/sleep). The IDLE animation path goes through `FUN_1403d3cf0` → `FUN_1403c20a0` (which resets `motTable+0x180` to 0 and writes end-state values). The difference: `FUN_1403d3cf0` is called BEFORE `FUN_1403d2eb0` in `FUN_1403d5e50`, so the deceleration → idle transition happens through the decel path, not the accumulator-hit-zero path.

### Motion table structure (actor+0x5C0 → motTable)
```
+0x000: channel 0 current value (speed index)
+0x004: channel 0 max value (18 for Donald)
+0x008: channel 0 min value (0)
+0x00C..+0x024: channels 1-2 (same layout)
+0x180: movement state value (used by FUN_1403d3cf0 for decel check)
+0x1AF: mode byte (0=friend, 1=player)
+0x1BC: decel end state
+0x1C0: decel end state
+0x22C: speed factor (float, used by friend decel path)
```

### Proven animation trigger: KO via FUN_1403d2eb0

Calling `FUN_1403d2eb0(actor, -maxVal, 0, 0)` from CE (where maxVal=18 for Donald) **did change the animation** — it pushed the accumulator to 0, which triggered `vtable+0xB0` on the friend type handler. This played animation 0x36 (KO/sleep). The entity got stuck in KO and required a game restart to recover.

This proves `FUN_1403d2eb0` CAN drive animation changes through its internal side-effects. The accumulator value alone doesn't change animation — the transition happens through:
- **Accumulator → 0**: fires `vtable+0xB0` → KO animation (0x36)
- **Decel path before accumulator**: `FUN_1403d3cf0` handles the idle transition (checks `motTable+0x180`)
- **Accel path**: drives walk/run through a mechanism not yet fully traced

Key takeaway: to get idle animation, use the deceleration path through `FUN_1403d5e50` (which calls `FUN_1403d3cf0` BEFORE `FUN_1403d2eb0`), NOT `FUN_1403d2eb0` directly. Calling `FUN_1403d2eb0` with a large negative delta bypasses the decel handler and goes straight to KO.

### Valid animation indices for Donald's motion set
```
Index 0: VALID (idle)     Index 6:  VALID (walk variant)
Index 2: VALID (?)        Index 7:  VALID
Index 4: VALID (walk)     Index 8:  VALID (run)
                          Index 10: VALID (run variant)
                          Index 11: VALID
Indices 1, 3, 5, 9, 12: invalid (not in motion set)
```

## Recommended fix for animation (next session)

### Approach: Debug the MovementDispatch hook

The hook at `FUN_1403d5e50` is installed but **not verified firing for friend entities**. Debug steps:

1. **Add logging** in `HookedMovementDispatch` to confirm it's being called for Donald. Log the actor address, original delta, and replacement delta. If it never fires for Donald, the function may be called through a different path than expected.

2. **Verify `g_friend1Actor` is set** when the hook fires. The friend actor pointers are refreshed by `RefreshFriendPointers()` which runs periodically. If the hook fires before the pointers are set, the actor match check fails and the delta passes through unchanged.

3. **If the hook IS firing but animation doesn't change**: the replacement delta may be wrong. The deceleration path in `FUN_1403d5e50` uses `motTable+0x180` (movement state value), not `motTable[0]` (speed index). A negative delta needs to be calibrated against `motTable+0x180` to properly trigger the idle path through `FUN_1403d3cf0` → `FUN_1403c20a0`.

4. **Alternative: hook `FUN_1403c3bd0` instead**. This is the behavior timer callback that computes the speed delta from follow-distance. It's only called for friends (not Sora). Replacing its delta computation would be simpler than hooking the shared `FUN_1403d5e50` and filtering by actor.

### Fallback: Modify follow-distance perception

Instead of hooking the movement dispatch, modify what the friend AI's behavior timer sees:
- Before the AI runs, temporarily write Sora's position to where Donald is (for idle) or far in the stick direction (for run)
- After the AI runs, restore Sora's position
- Hacky but uses the existing animation selection pipeline unchanged

## Stick input notes

### Raw vs processed input
- **Raw input buffer** (`input::RAW_SLOT0`): byte values 0x00–0xFF, gives analog gradient
- **Processed entry** (`input::PROCESSED_ENTRY0`): float values, but DIGITAL (0 or ±1 only)
- Solo mode now uses raw values with axis swap for analog speed control

### Stick axis swap
KH2 swaps stick fields in the processed entry:
- Processed +0x20 ("left") = physical RIGHT stick (camera)
- Processed +0x30 ("right") = physical LEFT stick (movement)

In the raw buffer, `LSTICK` = physical right, `RSTICK` = physical left. Solo mode swaps `leftX↔rightX`, `leftY↔rightY` and negates Y to match the processed convention.

## Hook architecture summary

```
HookedPerEntityUpdate(actorObj):
  PRE-CALL:
    - detect frame boundary (entity list head → g_soraActor)
    - check F5 hotkey
    - refresh friend pointers
    - identify friend slot
    - discover & install AI hooks (once)
    - read gamepads (raw analog + axis swap in solo mode)
    - if solo: zero movement stick, retarget camera
  
  CALL ORIGINAL:
    g_origPerEntityUpdate(actorObj)
      → vtable+0x10 (AI): HookedFriendAI
          - PRE: InjectMovementInput (velocity/facing/detether)
          - call g_origFriendAI (for animation playback chain)
          - POST: InjectMovementInput again (re-assert velocity/facing)
      → vtable+0x28 (pre-physics): HookedFriendPrePhysics → calls original
      → EntityPositionPhysics
  
  POST-CALL:
    - if friend: InjectMovementInput (final velocity/facing write)
    - if Sora + solo: SuppressSoraMovement

HookedMovementDispatch(actor, speedDelta, channel, flag):
  - if actor == g_friend1Actor/g_friend2Actor && soloMode && channel == 0:
      replace speedDelta with stick-magnitude-based value
  - call g_origMovementDispatch
```

## Files

- `inject/src/EntityHook.cpp` — all hook logic (~1700 lines)
- `inject/src/PatternScan.hpp` — AOB scan + PE section helpers
- `inject/src/DllMain.cpp` — DLL entry, Panacea exports, init thread
- `runtime/include/kh2coop/KH2Offsets.hpp` — all confirmed memory offsets
- `scripts/restart-kh2.ps1` — kill/rebuild/relaunch helper

## CE/Ghidra quick reference

| Thing | Address/RVA |
|---|---|
| PerEntityUpdate | exe+0x3BFD30 |
| Entity Update Loop | exe+0x3BF5E0 |
| EntityPositionPhysics | exe+0x3B89A0 |
| Sora AI (vtable+0x10) | exe+0x404A20 → FUN_1403a92c0 |
| Friend AI (vtable+0x10) | exe+0x1B0020 → FUN_1403c3660 |
| Friend behavior timer | FUN_1403c3bd0 |
| Movement dispatch | exe+0x3D5E50 (FUN_1403d5e50) — **HOOKED** |
| Movement accumulator | exe+0x3D2EB0 (FUN_1403d2eb0) |
| Accumulator clamp | exe+0x3C0860 (FUN_1403c0860) |
| Decel handler | exe+0x3D3CF0 (FUN_1403d3cf0) |
| Idle state reset | exe+0x3C20A0 (FUN_1403c20a0) |
| SetAnimationDirect | exe+0x3C6DC0 (FUN_1403c6dc0) — doesn't work from hook |
| SetMotion (channel) | exe+0x3B6670 |
| SetMotionSimple | exe+0x3B6630 |
| Motion controller tick | exe+0x3C6740 (FUN_1403c6740) |
| ResolveEntityType | exe+0x4AD270 |
| Camera struct | exe+0x718C60 |
| Camera actor ptr | camStruct+0x50 |
| Processed entry 0 | exe+0xBF31A0 |
| Active entity list head | exe+0x2A171C8 |
| Slot1 base | exe+0x2A23518+0x278 |
| Friend1 actor ptr | Slot1+0x220 |
| Friend2 actor ptr | Slot1+0x228 |
| Follow-steering timer | actor+0xBA8 |
| Motion set pointer | actor+0x80 |
| Motion controller | actor+0x158 |
| Animation ID (read-only) | actor+0x180 |
| Motion table pointer | actor+0x5C0 |
| Behavior timer | actor+0xDC0 |
| Velocity XYZ | actor+0xB98/B9C/BA0 |
| Acceleration XYZ | actor+0xA58/A5C/A60 |
| Entity transform | actor+0x640 |
| Position XYZ | entity+0x30/0x34/0x38 |
| ROT_Y | entity+0x4C |
| sin(ROT_Y) | entity+0x40 (mislabeled COS_FACING) |
| cos(ROT_Y) | entity+0x48 (mislabeled SIN_FACING) |

## DLL injection

There is **no automated injection**. The restart script builds and launches KH2 but does NOT inject the DLL. Current injection method:

```lua
-- In Cheat Engine Lua console (after attaching to KH2):
openProcess("KINGDOM HEARTS II FINAL MIX.exe")
local loadLibA = getAddress("kernel32.LoadLibraryA")
local mem = allocateMemory(512)
writeString(mem, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\\kh2coop_inject.dll", false)
createRemoteThread(loadLibA, mem)
```

The restart script's `-CopyDll` flag copies from `build/inject/Release/` which may be stale. Always manually copy from `build/inject/staging/kh2coop_inject.dll` after building.
