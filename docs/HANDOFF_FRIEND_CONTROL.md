# Friend Control Handoff — 2026-04-02 (Session 2)

## Current state

Press F5 to toggle solo mode. Left stick moves Donald in correct camera-relative directions. Right stick orbits camera. No crashes. Sora stands still. But: Donald has no walk/run animation, his facing snaps back to Sora at rest, and the camera doesn't toggle back to Sora on F5-off.

## What works

- **F5 solo mode toggle** — `CheckTestModeHotkey` toggles `g_soloTestMode`, logged
- **Camera follows Donald** — in-process retarget: writes `g_friend1Actor` to `camStruct+0x50`
- **De-tether** — `actor+0xBA8` (follow-steering timer) held at 999.0 blocks vanilla follow callback
- **Left stick moves Donald** — camera-relative, correct in all directions (Y inversion fixed)
- **Right stick camera orbit** — preserved by only zeroing movement stick (+0x30) in processed entry, leaving camera stick (+0x20) untouched
- **Sora frozen** — entity-level velocity/accel zeroing in post-physics path (`SuppressSoraMovement`)
- **No crashes** — original friend AI runs through to handle animation system, no bad function calls

## Remaining bugs (3 issues, all caused by the same root: original AI still running)

### 1. Animation stuck — Donald doesn't walk/run

**Root cause:** The original friend AI runs every frame and calls the motion set functions (`FUN_1403b6670(actor, 2, 1, 0, 0)` and `FUN_1403b6630(actor, 1, 1, 0)`). These drive the animation system. But the AI's motion calls are based on the AI's own state (following Sora, idle), not our injected velocity. So Donald always plays the AI's chosen animation (usually idle or follow-walk toward Sora).

**What we know about the animation system:**
- Writing to `actor+0x180` does NOT trigger animations. It's a one-shot field: the game reads it, starts that animation, and clears it to 0 within one frame. Continuously writing it just re-triggers the same start.
- The real animation trigger is `FUN_1403b6670` (exe+0x3B6670) and `FUN_1403b6630` (exe+0x3B6630). These are motion channel setters.
- The friend AI function (exe+0x390010) is tiny — just two calls:
  ```c
  FUN_1403b6670(actor, 2, 1, 0, 0);  // channel 2, flags (1,0,0)
  FUN_1403b6630(actor, 1, 1, 0);     // channel 1, flags (1,1,0)
  ```
- The second arg is a **motion channel index**, NOT an animation ID. The animation played depends on the entity's internal state (velocity, movement flags, etc.).
- Calling `g_setMotion(actor, animId, 1, 0, 0)` directly from our code caused explosions (channel 0 = effects?) and crashes. The function reads `actor+0x80` as a motion set pointer and looks up the channel — passing wrong channel indices triggers wrong animation data.

**Likely fix strategy:** The motion set functions select animation based on entity state. If we write correct velocity to the entity BEFORE the AI runs, the AI's own motion calls should pick up walk/run from the velocity. Current problem: our velocity writes happen in post-physics (AFTER the AI), so the AI sees zero velocity and picks idle. **Try writing velocity in the pre-AI path (before `g_origFriendAI` call) instead of or in addition to post-physics.** Alternatively, find what field the motion system reads to determine idle vs walk vs run and write that directly.

**Alternative:** Suppress the AI again (don't call `g_origFriendAI`) but call the motion set functions ourselves with the correct channels (2 and 1) — the explosions were caused by passing ANIM_IDLE(0) as a channel index. Always use channels 2 and 1 like the original AI does.

### 2. Facing snaps to Sora at rest

**Root cause:** The original AI runs and its follow-steering logic sets facing toward Sora. Our facing write happens in `InjectMovementInput` but only when `magnitude > 0.01f` (moving). At rest, we don't write facing, so the AI's facing-toward-Sora wins.

**Fix:** Write facing in the post-physics path even when idle (keep the last known facing angle). Store `g_lastFacingAngle` and write it every frame regardless of magnitude.

### 3. Camera doesn't toggle back to Sora on F5-off

**Root cause:** `RestoreCameraToSora()` runs once on F5 toggle-off, writing Sora's actor to `camStruct+0x50`. But the very next frame, `HookedPerEntityUpdate` checks `g_soloTestMode` — if the bool already flipped to false, `RetargetCameraToFriend()` shouldn't fire. Yet the camera stays on Donald.

**Likely cause:** `OnFrame()` (Panacea path) also calls `RetargetCameraToFriend()` when `g_soloTestMode` is true. If `OnFrame()` runs AFTER the F5 toggle but in the same frame, it re-retargets. Also, the game's own camera system may be slow to respond — it might re-read the actor pointer and our restore gets overwritten by the game's own camera logic.

**Fix:** Add a cooldown frame counter: after F5-off, skip `RetargetCameraToFriend` for N frames to let the game's camera system stabilize on Sora. Or set a `g_cameraRestorePending` flag that blocks retarget for a few frames after toggle-off.

## Key architecture findings (updated)

### Motion set functions are channel-based, not animation-ID-based
```
FUN_1403b6670(actor, channelIdx, flag1, flag2, param5)
FUN_1403b6630(actor, channelIdx, flag1, flag2)
```
The friend AI always uses channels 2 and 1. Channel 0 triggers effects/explosions. The animation played on a channel depends on the entity's movement state, NOT the channel index. Both functions read `actor+0x80` (motion set pointer), search for the matching channel, and create a motion playback object via `FUN_1402c6b30`.

### Entity facing fields (corrected)
- `entity+0x40` (labeled `COS_FACING` in KH2Offsets.hpp) = actually `sin(ROT_Y)`
- `entity+0x48` (labeled `SIN_FACING` in KH2Offsets.hpp) = actually `cos(ROT_Y)`
- `entity+0x4C` = `ROT_Y` = `atan2(velX, velZ)` (verified via CE live read)

The labels in KH2Offsets.hpp are historically swapped. The code in EntityHook.cpp writes the correct values to the correct offsets with comments explaining the swap.

### Sora = entity list head
`g_soraActor` is set from the entity list head (exe+0x2A171C8) on each frame boundary. This is reliable — Sora is always the first entity processed.

### Camera retarget architecture
- `RetargetCameraToFriend()` writes `g_friend1Actor` to `camStruct+0x50` every frame
- `RestoreCameraToSora()` writes `g_soraActor` (entity list head) to `camStruct+0x50`
- The game's own camera system also writes to `camStruct+0x50` during cutscenes, room transitions, etc.

### Processed entry layout (confirmed)
```
+0x00..+0x1F: button bitmasks (current, pressed, released, auto-repeat)
+0x20..+0x2F: physical RIGHT stick (camera) — 4 floats
+0x30..+0x3F: physical LEFT stick (movement) — 4 floats  
+0x40: context pointer
+0x48: flags
```
Only +0x30 is zeroed during solo mode. Everything else left intact.

## Hook architecture summary

```
HookedPerEntityUpdate(actorObj):
  PRE-CALL:
    - detect frame boundary (entity list head → g_soraActor)
    - check F5 hotkey
    - snapshot processed stick (once per frame)
    - refresh friend pointers
    - identify friend slot
    - discover & install AI hooks (once)
    - read gamepads
    - if solo: zero movement stick, retarget camera
  
  CALL ORIGINAL:
    g_origPerEntityUpdate(actorObj)
      → vtable+0x10 (AI): HookedFriendAI → calls g_origFriendAI (passthrough)
      → vtable+0x28 (pre-physics): HookedFriendPrePhysics → calls original (passthrough)
      → EntityPositionPhysics
        → vtable+0x40 (follow-steering): HookedFollowSteering → returns zeros for friends
  
  POST-CALL:
    - if friend: InjectMovementInput (velocity, facing, de-tether)
    - if Sora + solo: SuppressSoraMovement (zero velocity/accel, force idle)
```

## Files

- `inject/src/EntityHook.cpp` — all hook logic, ~1360 lines
- `inject/src/PatternScan.hpp` — AOB scan + PE section helpers
- `inject/src/DllMain.cpp` — DLL entry, Panacea exports, init thread
- `runtime/include/kh2coop/KH2Offsets.hpp` — all confirmed memory offsets
- `scripts/restart-kh2.ps1` — kill/rebuild/relaunch helper

## CE/Ghidra quick reference

| Thing | Address/RVA |
|---|---|
| PerEntityUpdate | exe+0x3BFD30 |
| Entity Update Loop | exe+0x3BF5E0 |
| calc_motion | exe+0x3BEEC0 |
| EntityPositionPhysics | exe+0x3B89A0 |
| Friend AI function | exe+0x390010 (tiny: 2 motion set calls) |
| SetMotion (channel setter) | exe+0x3B6670 |
| SetMotionSimple | exe+0x3B6630 |
| Motion playback core | exe+0x2C6B30 |
| Handle resolver | exe+0x4AD270 |
| Camera struct | exe+0x718C60 |
| Camera actor ptr | camStruct+0x50 |
| Processed entry 0 | exe+0xBF31A0 |
| Active entity list head | exe+0x2A171C8 |
| Slot1 base | exe+0x2A23518+0x278 |
| Friend1 actor ptr | Slot1+0x220 |
| Friend2 actor ptr | Slot1+0x228 |
| Follow-steering timer | actor+0xBA8 |
| Motion set pointer | actor+0x80 |
| Animation motion ID | actor+0x180 (one-shot, cleared by calc_motion) |
| Velocity XYZ | actor+0xB98/B9C/BA0 |
| Acceleration XYZ | actor+0xA58/A5C/A60 |
| Entity transform | actor+0x640 |
| Position XYZ | entity+0x30/0x34/0x38 |
| ROT_Y | entity+0x4C |
| sin(ROT_Y) | entity+0x40 (mislabeled COS_FACING) |
| cos(ROT_Y) | entity+0x48 (mislabeled SIN_FACING) |

## Recommended next steps (priority order)

1. **Fix animation** — Most impactful. Try: suppress AI again in `HookedFriendAI`, call `g_setMotion(actor, 2, 1, 0, 0)` and `g_setMotionSimple(actor, 1, 1, 0)` (correct channel indices 2 and 1), but write velocity to the entity BEFORE the motion calls so the motion system sees our movement state. If that doesn't work, use CE to find what field the motion system reads to decide idle vs walk vs run.

2. **Fix facing at rest** — Store last facing angle, write it every frame in post-physics regardless of magnitude.

3. **Fix camera toggle** — Add frame cooldown after F5-off, or check that both OnFrame and HookedPerEntityUpdate paths agree on g_soloTestMode before calling RetargetCameraToFriend.
