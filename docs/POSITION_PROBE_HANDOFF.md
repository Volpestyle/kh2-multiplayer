# Position Probe Handoff

## Goal

Find the entity position (X, Y, Z floats) for the player character in KH2 Final Mix PC (Steam Global). This is the last major unknown needed for the co-op mod's `GameBridgePC`.

## Environment

- KH2 Final Mix running on Windows PC (Steam)
- Process name: `KINGDOM HEARTS II FINAL MIX.exe`
- Recommended tool: `pymem` (Python, Windows-native) or Cheat Engine Lua scripting
- Claude Code running in WSL on the same machine

### pymem setup

```powershell
# In Windows PowerShell (NOT WSL):
pip install pymem
```

```python
import pymem
pm = pymem.Pymem("KINGDOM HEARTS II FINAL MIX.exe")
base = pm.base_address

# Read a known value to verify connection:
world_id = pm.read_uchar(base + 0x717008)
print(f"World ID: {world_id}")  # Should be 2 for Twilight Town

hp = pm.read_int(base + 0x2A23598)
print(f"HP: {hp}")
```

## What We Know

### Confirmed addresses (exe-relative offsets, Steam Global)

| Offset | Type | What | Status |
|--------|------|------|--------|
| `0x717008` | byte | World ID | CONFIRMED |
| `0x717009` | byte | Room ID | CONFIRMED |
| `0x2A23518` | -- | Unit Slot 0 base (Sora/Roxas) | CONFIRMED |
| `0x278` | -- | Stride per unit slot | from kh2lib |
| `0x2A23598` | int32 | Slot 0 HP (= Slot0 + 0x80) | CONFIRMED (writable) |
| `0x2A2359C` | int32 | Slot 0 Max HP (= Slot0 + 0x84) | CONFIRMED (writable) |

### Full kh2lib address map

See `runtime/include/kh2coop/KH2Offsets.hpp` for 30+ additional addresses from KH2SteamGlobal.lua (game state, camera, input, menus, etc.).

### What we know about position

1. **Position is NOT in the unit slot.** The 0x278-byte slot at `exe+2A23518` contains stats (HP, MP, equipment, abilities) but NOT position. We verified this by checking offsets within the slot as floats -- none tracked movement.

2. **Position is HEAP-ALLOCATED.** Float scans that filtered out camera movement found addresses in the `F3D8xxxxxxxx` range (heap), not at static `exe+` offsets.

3. **Stack/temp copies exist.** The game copies position to many temporary locations (stack frames, render buffers). Writing to these has NO effect on the character. The "real" position is the one that teleports the character when written.

4. **Camera values are separate.** Camera coordinates change even when the player stands still. We filtered these out by scanning "Unchanged" while rotating the camera.

5. **The first 8 bytes of Slot 0 (`exe+2A23518`) contain a pointer** that points to `7FF6xxxxxxxx` (exe module range). Following this pointer at offset 0 gave a float value of 0. Other offsets within the pointed-to struct were not fully explored.

6. **Expected position values:** World coordinates are floats in the range -2000 to 2000. In Twilight Town, typical values are X: -700 to 200, Y: near 0 (height), Z: -1000 to 500.

## What to Try

### Approach 1: Explore the pointer at Slot0+0x00

The first 8 bytes of the unit slot are a pointer. Systematically read floats at every offset (0x00 through 0x200, step 4) from that pointer target. Walk the character between reads. Look for floats in the -2000 to 2000 range that change.

```python
import pymem, struct, time

pm = pymem.Pymem("KINGDOM HEARTS II FINAL MIX.exe")
base = pm.base_address

# Read the pointer at Slot0+0x00
slot0_ptr = pm.read_longlong(base + 0x2A23518)
print(f"Slot0 pointer -> 0x{slot0_ptr:X}")

# Scan through the pointed-to struct for position-like floats
print("Stand still for 3 seconds...")
time.sleep(3)

still_values = {}
for off in range(0, 0x300, 4):
    try:
        val = pm.read_float(slot0_ptr + off)
        if -2000 < val < 2000 and val != 0:
            still_values[off] = val
    except:
        pass

print(f"Found {len(still_values)} candidates while still")
input("Now WALK and press Enter...")

for off, old_val in still_values.items():
    try:
        new_val = pm.read_float(slot0_ptr + off)
        if abs(new_val - old_val) > 1.0:
            print(f"  +0x{off:03X}: {old_val:.2f} -> {new_val:.2f} (CHANGED)")
    except:
        pass
```

### Approach 2: Brute-force scan the exe module

Read all floats in the game's static memory range. Compare before/after walking. Filter out camera by comparing before/after camera rotation.

```python
import pymem, struct, time

pm = pymem.Pymem("KINGDOM HEARTS II FINAL MIX.exe")
base = pm.base_address
module_size = pm.process_base.SizeOfImage

print(f"Base: 0x{base:X}, Size: 0x{module_size:X}")
print("Stand still...")
time.sleep(2)

# Read entire module memory
data1 = pm.read_bytes(base, module_size)

input("ROTATE CAMERA (don't walk) then press Enter...")
data2 = pm.read_bytes(base, module_size)

input("Now WALK then press Enter...")
data3 = pm.read_bytes(base, module_size)

# Find floats that: same in data1 vs data2 (camera rotation) but different in data2 vs data3 (walking)
candidates = []
for i in range(0, len(data1) - 4, 4):
    v1 = struct.unpack_from('<f', data1, i)[0]
    v2 = struct.unpack_from('<f', data2, i)[0]
    v3 = struct.unpack_from('<f', data3, i)[0]
    if abs(v1 - v2) < 0.01 and abs(v2 - v3) > 1.0 and -2000 < v1 < 2000:
        candidates.append((i, v1, v3))

print(f"\n{len(candidates)} position candidates:")
for off, v1, v3 in candidates[:50]:
    print(f"  exe+0x{off:07X}: {v1:.2f} -> {v3:.2f}")
```

### Approach 3: Follow pointer chains from unit slot

Explore all 8-byte values in the unit slot that look like pointers (values > 0x10000). Follow each one and scan for position floats.

```python
import pymem

pm = pymem.Pymem("KINGDOM HEARTS II FINAL MIX.exe")
base = pm.base_address

slot0 = base + 0x2A23518

# Find all pointer-like values in the 0x278-byte slot
for off in range(0, 0x278, 8):
    try:
        val = pm.read_longlong(slot0 + off)
        if val > 0x10000 and val < 0x7FFFFFFFFFFF:
            print(f"  Slot0+0x{off:03X}: ptr -> 0x{val:X}")
            # Try reading a few floats at the pointed-to address
            for foff in range(0, 0x40, 4):
                try:
                    fval = pm.read_float(val + foff)
                    if -2000 < fval < 2000 and fval != 0:
                        print(f"    +0x{foff:02X}: {fval:.2f}")
                except:
                    pass
    except:
        pass
```

### Approach 4: Write-test candidates

Once you find float candidates that track movement, TEST if writing to them teleports the character:

```python
# Read current value
old_val = pm.read_float(candidate_address)
# Write a very different value
pm.write_float(candidate_address, old_val + 500.0)
# If the character teleports, this is the real position
```

## What to Update

Once position is found, update `runtime/include/kh2coop/KH2Offsets.hpp`:

1. If position is at a static offset: fill in `entity::POS_X`, `POS_Y`, `POS_Z` directly.
2. If position requires a pointer chain: document the chain (e.g., `exe+XXXX -> +YY = position`) and update `GameBridgePC.cpp` to follow it.

## Don't Touch

- `common/` -- stable codec and networking code
- `server/` -- stable session host
- `tests/` -- stable test harness
- The other addresses in `KH2Offsets.hpp` that are already confirmed
