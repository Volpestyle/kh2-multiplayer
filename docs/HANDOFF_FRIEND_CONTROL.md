# Friend Control Handoff — 2026-04-02

## What works

- **F5 solo mode toggle** — `EntityHook.cpp` CheckTestModeHotkey toggles `g_soloTestMode`
- **Camera follows Donald** — in-process retarget: writes `g_friend1Actor` to `camStruct+0x50` (exe+0x718CB0). Works reliably.
- **De-tether** — holding `actor+0xBA8` (follow-steering timer) at 999.0 prevents `EntityPositionPhysics` from calling the vanilla follow callback. Confirmed via CE live testing.
- **Left stick moves Donald** — after discovering the processed-entry stick swap (+0x20 = physical right/camera, +0x30 = physical left/movement). Movement is camera-relative.
- **Facing partially correct** — `atan2(-velX, -velZ)` fixes left/right facing but forward/back is still wrong.
- **AI suppression** — `HookedFriendAI` (vtable+0x10) and `HookedFriendPrePhysics` (vtable+0x28) correctly suppress friend AI when `g_soloTestMode && g_currentFriendSlot != 0`.

## Known remaining bugs

### 1. Camera control lost in solo mode (right stick doesn't orbit)
`SuppressSoraInput()` zeros the processed entry then restores +0x20 (physical right stick / camera). But the camera may read from a different source or a different processed entry (Entry 1 at exe+0xBF3208). **Next step:** Use CE to set a read breakpoint on the camera orbit code to find exactly where it reads stick input. Also check if the game reads raw input for camera instead of the processed entry.

### 2. Facing direction wrong for forward/backward
`atan2(-velX, -velZ)` fixes left/right but not forward/back. The sign convention may need per-axis adjustment rather than a blanket negate. **Next step:** Read Sora's ROT_Y while walking in each cardinal direction to establish the exact KH2 angle convention, then match it.

### 3. Camera retargets on inject without F5
The camera snaps to Donald immediately on DLL injection, before F5 is pressed. This is a regression — `RetargetCameraToFriend()` is gated on `g_soloTestMode` which starts `false`. **Likely cause:** The frame-boundary detection (entity list head check) may fire `g_frameCounter` increments that interact with stale `g_processedStickFrame` initialization, or `g_soloTestMode` is briefly true due to a startup race. Check `Initialize()` and the first few frames of `HookedPerEntityUpdate` for any path that calls `RetargetCameraToFriend` unconditionally.

### 4. Animation stuck on RUN
The DLL writes to `actor+0x180` (confirmed correct offset via CE), but the game's `calc_motion` system (exe+0x3BEEC0) likely overwrites it after our write. **Two approaches:**
- **Timer approach (proven):** A CE Lua timer writing animation based on velocity every 8ms worked perfectly in live testing. The DLL could spawn a lightweight thread doing the same.
- **Hook approach:** Hook or NOP the `calc_motion` write for controlled friends. Need to decompile `calc_motion` to find the exact instruction that writes to +0x180.

## Key architecture findings

### Entity update dispatch uses handle resolution, not direct pointers
`PerEntityUpdate` resolves a 32-bit handle at `actor+0x00` via `FUN_1404ad270` (handle resolver at exe+0x4AD270). The resolver uses `HANDLE_REGION_TABLE` at exe+0x2B0D720. The resolved pointer is the type handler whose vtable drives AI (+0x10), post-main (+0x18), pre-physics (+0x28).

**Critical:** `actor+0x9C0` (previously assumed to be the type handler) is NULL for friend entities. The correct path is handle resolution through `actor+0x00`.

### Two different type handlers per entity
- `actor+0x00` → handler A (vtable used by PerEntityUpdate for AI/pre-physics)
- `actor+0x0C` → handler B (vtable used by EntityPositionPhysics for follow-steering)

These are **different handlers with different vtables**. The vtable+0x40 on handler A has signature `(handler, actor) → char` (a pre-update callback). The vtable+0x40 on handler B has signature `(handler, outVec4, entity, dt) → void*` (the actual follow-steering). **Do NOT hook handler A's vtable+0x40 with handler B's signature — this corrupts entity state.**

### Processed entry stick swap
KH2's input mapper swaps stick fields in the processed button state entry:
- `PROCESSED_ENTRY0 + 0x20` ("left" label) = **physical RIGHT stick** (camera)
- `PROCESSED_ENTRY0 + 0x30` ("right" label) = **physical LEFT stick** (movement)

Verified via live CE probe. `TryReadSoloProcessedStick()` in EntityHook.cpp accounts for this.

### SuppressSoraInput must preserve camera stick
Zeroing the full 0x40 bytes kills camera orbit. Current approach: zero all 0x40, then restore the 16 bytes at +0x20 (camera stick). This may not be sufficient if the camera reads from elsewhere.

### Follow-steering timer (actor+0xBA8)
`EntityPositionPhysics` decrements this timer by frame time each frame. When it goes negative, it calls vtable+0x40 on handler B (the tether). Holding it at 999.0 disables the tether. Reset to 0.0 on solo-mode exit to restore vanilla follow.

### Frame counter without Panacea
`OnFrame()` is only called when loaded via Panacea. When injected via CE, `g_frameCounter` is incremented by detecting the entity list head in `HookedPerEntityUpdate` (compares `actorObj` against `active_entity_list::HEAD`).

## Files

- `inject/src/EntityHook.cpp` — all hook logic, input injection, camera retarget
- `inject/src/PatternScan.hpp` — AOB scan + PE section helpers
- `inject/src/DllMain.cpp` — DLL entry point, Panacea exports
- `runtime/include/kh2coop/KH2Offsets.hpp` — all confirmed memory offsets
- `docs/INPUT_RE_SESSION.md` — full input pipeline RE trace

## CE/Ghidra quick reference

| Thing | Address/RVA |
|---|---|
| PerEntityUpdate | exe+0x3BFD30 |
| EntityPositionPhysics | exe+0x3B89A0 |
| Handle resolver | exe+0x4AD270 |
| Camera struct | exe+0x718C60 |
| Camera actor ptr | camStruct+0x50 |
| Processed entry 0 | exe+0xBF31A0 |
| Active entity list head | exe+0x2A171C8 |
| Friend1 actor ptr | Slot1+0x220 (Slot1 = exe+0x2A23518+0x278) |
| Follow-steering timer | actor+0xBA8 |
| Animation motion ID | actor+0x180 |
| Animation sub-state | actor+0x184 |
| Velocity XYZ | actor+0xB98/B9C/BA0 |
| Acceleration XYZ | actor+0xA58/A5C/A60 |
| Entity transform | actor+0x640 |
| Position XYZ | entity+0x30/0x34/0x38 |
| ROT_Y / facing | entity+0x4C, COS at +0x60, SIN at +0x64 |
