# Camera RE Session — CAMERA RETARGET CONFIRMED

## Result

**Camera follow-target can be redirected by changing a pointer in the camera struct.** The camera struct at `exe+0x718C60` contains a pointer to the followed actor object at offset `+0x50`. Changing this pointer makes the camera follow a different entity.

Tested by allocating a fake actor object, copying the original actor data, modifying the entity transform position, and pointing the camera at it. The camera smoothly followed the fake position for the duration of the test.

## Camera Struct Layout

Camera struct base: `exe+0x718C60`

| Offset | Type | Field | Notes |
|--------|------|-------|-------|
| +0x00 | float | FOV or angle | ~0.0524 (3 degrees) |
| +0x04 | float | Unknown | 1.0 |
| +0x08 | Vec4 | Smoothed look-at | Interpolated camera target (X,Y,Z,W) |
| +0x18 | Vec4 | Eye position (interpolated) | Where the camera sits in 3D space |
| +0x28 | float | Unknown | 1.5 |
| +0x48 | dword | CAMERA_TYPE | Camera mode (already known from KH2LIB) |
| +0x50 | qword | **Actor pointer** | Pointer to the followed actor object |
| +0x58 | float | Follow distance | ~500.0 |
| +0x5C | float | Vertical angle | ~1.38 (~79 degrees) |
| +0x60 | float | Vertical angle copy | Same as +0x5C |
| +0x64 | Vec4 | Eye position (raw) | Camera eye world position |
| +0x74 | Vec4 | Eye position (copy) | Duplicate of +0x64 |
| +0x84 | Vec4 | **Look-at target (raw)** | Matches player XZ, Y offset by ~170 |
| +0x94 | Vec4 | Look-at target (copy) | Duplicate of +0x84 |
| +0xA4 | float | Height offset | 1.5 |
| +0xCC | float | Max distance | 420.0 |
| +0xD0 | float | Min distance | 380.0 |
| +0xD4 | float | Unknown distance | 250.0 |
| +0xD8 | float | Max range | 500.0 |

## Actor Pointer Chain

The camera follows whatever actor is pointed to by `camStruct+0x50`:

```
camStruct+0x50  -->  actor object  (+0x640)-->  entity transform  (+0x30)-->  position (X,Y,Z,W)
```

- `camStruct+0x50` = `exe+0x251EC20` (actor object base)
- Actor object + `0x640` = `exe+0x251F260` (entity transform struct — same as Session 3 discovery)
- Entity transform + `0x30` = position floats

The camera reads position from this chain via `MEMCPY_4FLOATS` at `exe+0x1A8E60`, once per frame.

## How to Retarget

1. Save the original pointer: `origPtr = read(camStruct + 0x50)`
2. Allocate 0x700 bytes in the target process
3. Copy the original actor object into the allocation
4. Write the desired position into `allocation + 0x640 + 0x30` (entity POS_X/Y/Z/W)
5. Point the camera: `write(camStruct + 0x50, allocation)`
6. Each frame: update the position at `allocation + 0x640 + 0x30`
7. To restore: `write(camStruct + 0x50, origPtr)` and free the allocation

### Important Notes

- The actor object copy must be a full 0x700-byte copy of the original, or the game may crash reading other fields.
- The camera pointer must be continuously maintained (re-written each frame) to prevent the game from resetting it.
- The look-at target Y includes a fixed height offset (~170 units above the entity position).
- The camera eye position is computed by the game based on the look-at target, distance, and angle settings.

## Write Verification

Direct writes to the look-at address (`+0x84`) produce visible flicker but are overwritten by `MEMCPY_4FLOATS` each frame. The pointer redirect approach is reliable because the game reads from whatever actor the pointer references.

## What This Enables

- **M2 (Camera Retarget)**: Each client can follow its own slot's actor by pointing `camStruct+0x50` at the appropriate actor object.
- For Friend1/Friend2 clients: allocate a fake actor, populate its entity transform with the replicated position from the network, and point the camera at it.
- Cutscene/event cameras may override this — need to detect `CAMERA_TYPE` changes and yield control during cinematics.

## Open Questions

1. Does the camera pointer get reset during room transitions? (Likely yes — need to re-apply after transition)
2. Does changing CAMERA_TYPE affect the pointer behavior?
3. Are there other camera struct instances for split-screen or battle cameras?
4. Can we find Friend1/Friend2 actor objects directly instead of using fake allocations?
