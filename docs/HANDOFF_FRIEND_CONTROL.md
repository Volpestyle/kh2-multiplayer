# Friend Control Handoff — 2026-04-03 (Session 5)

## Current state

Press F5 to toggle solo mode. Left stick moves Donald with **proportional speed** (analog gradient from walk to run). Camera toggles correctly both ways. Facing persists when idle (no snap to Sora). **Animation now matches stick input** — idle/walk/run play through their full loops regardless of distance from Sora. The animation bug from Sessions 3-4 is fully resolved.

## What works

- **F5 solo mode toggle** — camera, movement, input all toggle cleanly
- **Camera follows Donald** — retargets `camStruct+0x50` every frame; restores to Sora on F5-off
- **De-tether** — `actor+0xBA8` (follow-steering timer) held at 999.0
- **Left stick moves Donald** — camera-relative, proportional speed (raw analog input)
- **Right stick camera orbit** — preserved
- **Facing persists at rest** — `g_lastFacingAngle[]` cache, written every frame
- **Sora frozen** — entity-level velocity/accel zeroing in post-physics
- **Animation matches stick input** — idle (0), walk (1), run (2) correctly loop
- **All hooks installed** — PerEntityUpdate, FriendAI, PrePhysics, MovementDispatch, MotionChainSetAnim, InputCollector

## Session 5 — the animation fix

### Root cause (discovered via CE data breakpoints)

The animation system has **three layers** that all fight for control of `motCtrl+0x44` (animation current time):

1. **The vanilla friend AI** calls `FUN_1403c86a0` every frame (through the behavior timer). Each call creates a NEW motion object at `motCtrl+0x18`, resetting animation time to 0. This is why overriding animation ID alone doesn't work — the animation restarts every frame.

2. **A per-frame "motion playback" system** (outside the AI) calls `FUN_1403c88c0` once per frame for EVERY entity. This goes through `FUN_1403c8cd0` → `FUN_1403c8a40`. In `FUN_1403c8a40`, when blend params are non-zero (`param_3 > 0` AND `motCtrl+0x168 bit 0` set), the function writes `0x40000000` (float 2.0) to `motCtrl+0x44`, resetting the animation time. CE data breakpoint on `motCtrl+0x44` showed this instruction at RVA `0x3C8BC9` firing 60 times/second in strict alternation with the tick's time advancement.

3. **The motCtrl tick** (`FUN_1403c6740`) advances time normally and handles loop/chain boundaries. It's the only part that works correctly — it just loops whatever animation is in the motion object.

### The two-part fix

**Part 1: Skip the vanilla AI** (`HookedFriendAI`)

For controlled friends, DON'T call `g_origFriendAI`. Instead:
- Call `InjectMovementInput` for velocity/facing
- Compute target animation from stick magnitude (idle < 0.25, walk < 0.7, run >= 0.7)
- Call `g_setAnimationUnderlying` (FUN_1403c86a0) **only when the target changes**
- Track last-set animation in `g_lastOverrideAnim[]` to avoid per-frame calls

This prevents the AI from resetting the animation every frame. The tick loops whatever we set.

**Part 2: Block per-frame FUN_1403c88c0 calls** (`HookedMotionChainSetAnim`)

Even with the AI skipped, the game's motion playback system calls `FUN_1403c88c0` once per frame for all entities. This goes through `FUN_1403c8cd0` → `FUN_1403c8a40` and resets `motCtrl+0x44` to 2.0 via the blend path.

Solution: use a guard flag `g_inOurAnimSet`:
- Set `true` before our `g_setAnimationUnderlying` call, `false` after
- In `HookedMotionChainSetAnim`: if controlled friend AND `!g_inOurAnimSet`, return 1 (pretend success) without calling the original
- Our own calls pass through normally

This blocks the per-frame reset while allowing our intentional animation changes.

### Key RE findings from Session 5

#### FUN_1403c86a0 (SetAnimationUnderlying) — decompiled
```c
void FUN_1403c86a0(motCtrl, animId, startTime, blendParam) {
    *(uint64_t*)(motCtrl + 0x50) = 0;  // QWORD write: clears BOTH queueSize AND queueIndex
    // Walk secondary motion chain and reset each controller
    while (secondary chain) { FUN_1403c70d0(secondary); }
    FUN_1403c88c0(motCtrl, animId, startTime, blendParam);  // Set the animation
}
```
The QWORD write at +0x50 is critical — it atomically zeros both queue fields, guaranteeing the tick takes the LOOP path (`queueIndex == queueSize == 0`).

#### FUN_1403c8a40 (animation reset) — the time resetter
```c
if ((param_3 <= 0.0) || ((motCtrl+0x168 & 1) == 0)) {
    motCtrl+0x44 = param_4;      // Normal: time = startTime (0.0)
    motCtrl+0x48 = 0;            // No blend
} else {
    motCtrl+0x48 = param_3;      // Blend target
    motCtrl+0x4C = param_4;      // Blend position
    motCtrl+0x44 = 2.0f;         // ← THE STUCK VALUE (0x40000000)
    vtable+0x20 call...           // Initialize blend
}
```
The `else` branch fires when the caller passes non-zero blend params AND `motCtrl+0x168` bit 0 is set. The per-frame motion playback system triggers this path, writing 2.0 to curTime every frame.

#### FUN_1403c6740 (motCtrl tick) — loop vs chain
```c
time += deltaTime * speedMult;
if (time >= maxTime) {
    if (queueIndex == queueSize) {
        // LOOP: vtable+0x30 for duration, FUN_1403c8520 for bone update
        // Does NOT call FUN_1403c88c0 — animation just loops
    } else {
        // CHAIN: FUN_1403c88c0 with next queue entry (our hook catches this)
    }
}
```

### Approaches tried in Sessions 4-5

| Approach | Why it fails |
|---|---|
| Hook `FUN_1403d5e50` + replace delta | Both internal paths gated by bit 2 — NOP for friends |
| Call `FUN_1403c86a0` from HookedFriendAI | AI's per-frame FUN_1403c86a0 overwrites immediately |
| Call `FUN_1403c86a0` POST (after tick) | Restarts animation from frame 0 every frame |
| Hook `FUN_1403c88c0` (replace animId only) | Per-frame calls still reset time via blend path in FUN_1403c8a40 |
| Skip friend AI only | Per-frame motion playback system still calls FUN_1403c88c0 and resets time |
| Fake Sora position | Follow-distance isn't read from entity position for animation |
| **Skip AI + block per-frame FUN_1403c88c0** | **WORKS — animation loops correctly** |

## Motion controller structure (actor+0x158 = motCtrl)
```
+0x18: motion object pointer (drives animation playback)
+0x20: secondary motion object pointer
+0x28: animation ID (= actor+0x180) — written by FUN_1403c88c0
+0x30: animation sub-state (written by FUN_1403c8a40)
+0x34: flags (bit 0 = paused, bit 1 = updated this frame, bit 12 = 0x1000)
+0x38: secondary flags (bit 2 checked by FUN_1403c8a40)
+0x40: animation max time
+0x44: animation current time (advanced by tick each frame) ← THE CRITICAL FIELD
+0x48: blend target time (>0 means blend in progress)
+0x4C: blend position
+0x50: queue size (QWORD write from FUN_1403c86a0 clears +0x50 AND +0x54)
+0x54: current queue index
+0x58..+0x70: queue entry 0 (animId at +0x58, startTime at +0x5C, blend at +0x60, callback at +0x68, flags at +0x70)
+0x78...: queue entries 1-3 (stride 0x20)
+0xD8: secondary motion object pointer
+0x108: state field (updated by FUN_1403b6f20)
+0x130..0x144: bone transform bounds
+0x150: playback speed multiplier
+0x155: byte flag (cleared to 0 at tick start)
+0x158: additional motion data pointer
+0x160: motion data reference
+0x168: flags (bit 0 = enable blend path in FUN_1403c8a40)
+0x1B0: reference counter pointer
+0x208: state field (cleared by FUN_1403c8a40)
```

## Motion table structure (actor+0x5C0 → motTable)
```
+0x000: channel 0 current value (accumulator, stuck at 18 for Donald)
+0x004: channel 0 max value (18)
+0x008: channel 0 min value (0)
+0x00C..+0x024: channels 1-2 (same layout)
+0x180: movement state value
+0x1AF: mode byte (0=friend, 1=player)
+0x1BC: decel end state
+0x1C0: decel end state (gate check by FUN_1403c1d10)
+0x22C: speed factor (float)
```

## Hook architecture summary (current)

```
HookedPerEntityUpdate(actorObj):
  PRE-CALL:
    - detect frame boundary
    - check F5 hotkey (reset g_lastOverrideAnim on toggle-on)
    - refresh friend pointers
    - identify friend slot
    - discover & install AI hooks (once)
    - read gamepads
    - if solo: zero movement stick, retarget camera
  
  CALL ORIGINAL:
    g_origPerEntityUpdate(actorObj)
      → vtable+0x10 (AI): HookedFriendAI
          - if controlled: skip AI, inject movement, set animation on change
          - if not controlled: call original AI normally
      → FUN_1403c6740 (motCtrl tick) — advances animation time, handles loops
      → vtable+0x18 (HookedFriendPrePhysics — passes through)
      → vtable+0x78
      → EntityPositionPhysics
  
  POST-CALL:
    - InjectMovementInput (final velocity/facing write)
    - if Sora + solo: SuppressSoraMovement

HookedMotionChainSetAnim(motCtrl, animId, start, blend):
  - if controlled friend AND !g_inOurAnimSet: BLOCK (return 1)
  - if controlled friend AND g_inOurAnimSet: replace animId with target, pass through
  - all other entities: pass through unchanged

HookedMovementDispatch(actor, delta, channel, flag):
  - diagnostic logging only (NOP for friends, delta replacement removed)
```

## Key addresses

| Thing | Address/RVA |
|---|---|
| MotCtrl tick | exe+0x3C6740 (FUN_1403c6740) |
| Loop handler (frame update) | exe+0x3C8520 (FUN_1403c8520) |
| Animation reset (time writer) | exe+0x3C8A40 (FUN_1403c8a40) |
| Time-reset instruction | exe+0x3C8BC9 (`mov [rbx+44], 40000000`) |
| Motion chain setter | exe+0x3C88C0 (FUN_1403c88c0) — hooked |
| Motion object creator | exe+0x3C8CD0 (FUN_1403c8cd0) |
| Underlying anim setter | exe+0x3C86A0 (FUN_1403c86a0) |
| Animation resolver | exe+0x3C7C50 (FUN_1403c7c50) |
| PerEntityUpdate | exe+0x3BFD30 |
| Friend AI (vtable+0x10) | exe+0x1B0020 |
| Behavior timer | FUN_1403c3bd0 |
| Movement dispatch | exe+0x3D5E50 — hooked but NOP for friends |
| MotCtrl tick call site | exe+0x3BFE8A (inside PerEntityUpdate) |

## Known limitations / future work

- **Combat AI disabled** — skipping the vanilla AI means no combat reactions, ability triggers, or battle targeting. These need to be re-enabled selectively (skip only the animation/movement parts of the AI, not combat).
- **No animation blending** — transitions between idle/walk/run are instant (no smooth blend). Could be improved by passing non-zero blend params to FUN_1403c86a0 and whitelisting those calls through the FUN_1403c88c0 guard.
- **Friend2 (Goofy)** — also has AI skipped in solo mode. Gets forced to IDLE. Needs separate gamepad or network input.

## Files

- `inject/src/EntityHook.cpp` — all hook logic (~1920 lines)
- `inject/src/PatternScan.hpp` — AOB scan + PE section helpers
- `inject/src/DllMain.cpp` — DLL entry, Panacea exports, init thread
- `runtime/include/kh2coop/KH2Offsets.hpp` — all confirmed memory offsets
- `scripts/restart-kh2.ps1` — kill/rebuild/relaunch helper

## DLL injection

```lua
-- In Cheat Engine Lua console (after attaching to KH2):
openProcess("KINGDOM HEARTS II FINAL MIX.exe")
local loadLibA = getAddress("kernel32.LoadLibraryA")
local mem = allocateMemory(512)
writeString(mem, "C:\\Program Files (x86)\\Steam\\steamapps\\common\\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\\kh2coop_inject.dll", false)
createRemoteThread(loadLibA, mem)
```

**IMPORTANT:** After `restart-kh2.ps1`, the process PID may change (Steam relaunches). Always verify CE is attached to the CURRENT PID before injecting. Use `Get-Process "KINGDOM HEARTS II FINAL MIX"` to check.
