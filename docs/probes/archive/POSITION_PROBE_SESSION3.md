# Position Probe Session 3 — POSITION FOUND

## Result

**Player position is fully readable and writable.** Teleportation confirmed across multiple rooms including drastic cross-map teleports, skydives from Y=-3000, and rapid zigzag sequences.

The player entity struct address **changes per room transition**, but can be dynamically discovered. A universal write procedure (continuous dual-write) works in all tested rooms.

## Entity Transform Struct Layout

The player entity is a struct in the exe data section. Its base address changes per room, but the internal layout is consistent:

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| +0x00 | QWORD | Vtable pointer | Points to exe 0x253xxxx range |
| +0x08 | DWORD | Airborne flag | 0=grounded, 1=airborne |
| +0x30 | float | **Position X** | Horizontal |
| +0x34 | float | **Position Y** | Vertical (**negative = up/airborne**, ground varies by room: 0 or 1000) |
| +0x38 | float | **Position Z** | Depth |
| +0x3C | float | Position W | Always 1.0 |
| +0x40 | float | cos(facing) | Cosine of facing angle |
| +0x48 | float | sin(facing) | Sine of facing angle |
| +0x4C | float | Facing angle | Radians |
| +0xA4 | float | Y velocity | Appears when airborne |
| +0x100 | DWORD | Movement state | 2=grounded, 3=airborne |
| +0x104 | DWORD | Airborne sub-flag | 0=grounded, 1=airborne |

### Known struct base addresses (room-dependent)
- Room 1 (early Twilight Town): `exe+0x251F260`, position at `exe+0x251F290`
- Room 2 (Twilight Town populated): `exe+0x25224E0`, position at `exe+0x2522510`

## How to Use

### Reading position (any room)
```lua
local base = getAddress("KINGDOM HEARTS II FINAL MIX.exe")
-- Option A: Read from entity struct (must discover address first)
local x = readFloat(entityStructBase + 0x30)
local y = readFloat(entityStructBase + 0x34)
local z = readFloat(entityStructBase + 0x38)

-- Option B: Read from buffer array (always at fixed offset, find player slot)
local bufferEntry = base + 0xAD9170  -- slot varies per room
local x = readFloat(bufferEntry)
```

### Writing position / teleporting (universal, all rooms)
```lua
-- Continuous dual-write: works in ALL rooms (simple and physics-active)
for i = 1, 90 do  -- ~1.5 seconds at 60fps
  writeFloat(entityPos, targetX)
  writeFloat(entityPos + 4, targetY)
  writeFloat(entityPos + 8, targetZ)
  writeFloat(bufferPos, targetX)
  writeFloat(bufferPos + 4, targetY)
  writeFloat(bufferPos + 8, targetZ)
  sleep(16)
end
```

### Teleporting into the air (skydive)
```lua
writeInteger(structBase + 0x08, 1)   -- airborne flag
writeInteger(structBase + 0x100, 3)  -- movement state = airborne
writeInteger(structBase + 0x104, 1)  -- airborne sub-flag
writeFloat(posY, -3000.0)            -- negative Y = up! Game gravity handles the fall
```

## Confirmed Tests
- Single-axis teleport (X+200, X+500) — confirmed visual movement
- Cross-map teleport (3000, 1000, 3000) and (-2000, 1000, -2000) — confirmed
- Skydive from Y=-500 and Y=-2000 and Y=-3000 — confirmed with natural gravity fall
- Rapid zigzag (5 positions in sequence) — confirmed
- All tests confirmed in BOTH simple and physics-active rooms

## Copy Chain (fully traced)

```
exe+0x251F290 (AUTHORITATIVE — writes here teleport!)
  ↓ memcpy_4floats at 7FF6E43C8E60
exe+0xAD91E0 (R15 buffer — intermediate staging)
  ↓ function 7FF6E43574EA reads from R15, builds stack position
    ↓ function 7FF6E42A25B2 passes through
      ↓ function 7FF6E42583B2 (setEntityPosition via vtable)
        ↓ function 7FF6E4265696 validates + copies to entity sub-struct
[[entity+0x160]+0x20] (entity sub-struct position — OUTPUT ONLY)
  ↓ function 7FF6E42A2630 copies to render buffer
Render buffer (heap, stride 0xF30 — OUTPUT ONLY)
```

## How We Found It

1. Set execution breakpoint on the known copy function (7FF6E4265696) with stack capture
2. Traced return addresses up the call chain (all indirect/vtable calls, no direct callers)
3. Found function 7FF6E43574EA which reads position from R15 register (= R9 parameter)
4. R15 pointed to `exe+0xAD91E0` (intermediate buffer)
5. Set WRITE breakpoint on `exe+0xAD91E0`
6. Found `memcpy_4floats` at 7FF6E43C8E60 copying FROM two sources:
   - `exe+0xAC1068` (some other entity)
   - `exe+0x251F290` (player entity)
7. Writing to `exe+0x251F290` **teleported the player** — confirmed as authoritative

## Other Addresses Found

| Address | What |
|---------|------|
| `exe+0xAC1068` | Another entity position (NPC or camera?) — also writable, different entity |
| `exe+0xAD91E0` | Intermediate staging buffer (R15 in copy chain) |
| `exe+0xAD9138` | Static copy (output only) |
| `exe+0xAD6BC8` | Path history buffer (output only) |

## Room Transition Findings

**The entity struct address changes per room.** `exe+0x251F290` was only valid in the first room.

### How to find the player entity dynamically
1. **Entity position buffer array** at `exe+0xAD9100`, stride `0x38`
   - Each entry: 16 bytes position (X,Y,Z,W) + 40 bytes metadata
   - Player's slot index changes per room (was slot 4 in room 1, slot 2 in room 2)
2. **Entity struct** can be found by scanning exe data section (0x2500000+) for:
   - A QWORD pointer in the 0x253xxxx range (vtable)
   - Followed by position (W=1.0) at +0x30 from that pointer
   - Confirmed: `exe+0x25224E0` in room 2 (was `exe+0x251F260` in room 1)
3. **Position = entity struct base + 0x30** (4 floats: X, Y, Z, W)

### Room-dependent physics behavior
- **Simple rooms** (e.g. early Twilight Town): Single write to entity struct position teleports. Physics does NOT actively overwrite.
- **Active rooms** (e.g. populated areas): Physics computes position from scratch each frame via stack. Single writes get overwritten within ~500ms. **Continuous writes to BOTH the entity struct AND buffer entry are needed.**

### Reliable teleport procedure (works in all rooms)
```lua
local base = getAddress("KINGDOM HEARTS II FINAL MIX.exe")
-- 1. Find entity struct (scan for vtable + W=1.0 pattern)
-- 2. Find buffer entry (breakpoint R9 at exe+0x1354E0, or scan array at exe+0xAD9100)
-- 3. Write continuously to both:
for i = 1, 120 do  -- 2 seconds at 60fps
  writeFloat(entityPos, targetX)
  writeFloat(entityPos + 4, targetY)
  writeFloat(entityPos + 8, targetZ)
  writeFloat(bufferPos, targetX)
  writeFloat(bufferPos + 4, targetY)
  writeFloat(bufferPos + 8, targetZ)
  sleep(16)
end
```

### Key stable addresses (do NOT change per room)
| Address | Purpose |
|---------|---------|
| `exe+0xAD9100` | Entity position buffer array (stride 0x38) |
| `exe+0xAC1068` | Secondary entity position source |
| `exe+0x1354E0` | Position update function entry (R9 = buffer entry for current entity) |
| `exe+0x1A8E60` | memcpy_4floats (copies position between buffers) |
| `exe+0x456696` | Entity sub-struct position writer (validates + copies XYZW) |

## Open Questions for Session 4
1. ~~Is `exe+0x251F290` stable across worlds/room transitions?~~ **RESOLVED: NO — address changes per room.** `Tick()` auto-discovers on room change.
2. ~~How to efficiently identify the PLAYER's slot in the buffer array?~~ **RESOLVED:** Use camera actor pointer chain as primary strategy (most reliable), with vtable+moveState scan as fallback. Implemented in `scanForEntityStruct()`.
3. Can we automate entity struct discovery with an auto-assemble hook instead of scanning?
4. ~~Does writing the facing angle (entity struct +0x4C) also rotate the character model?~~ **RESOLVED: YES.** `ApplyReplicaActorState()` writes ROT_Y + COS_FACING + SIN_FACING.
5. What happens when we write during a cutscene or battle?
6. For multiplayer: should we use a code hook (auto_assemble) for position injection vs continuous external writes?
