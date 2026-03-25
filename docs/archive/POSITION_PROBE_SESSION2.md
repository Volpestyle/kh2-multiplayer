# Position Probe Session 2 — Handoff Notes

## Setup
- **Cheat Engine MCP Bridge** is configured and working (`~/.claude.json` has the `cheatengine` MCP server)
- Bridge Lua script runs in CE's Lua Engine modal (Ctrl+Alt+L), NOT File > Execute Script
- CE is attached to `KINGDOM HEARTS II FINAL MIX.exe`, base `0x7FF6E4220000`
- Character is **Roxas** (early Twilight Town), ground Y = 1000.0
- `kh2-lua-library` is at `C:\Users\volpe\kh2-lua-library` — it has NO entity position offsets

## What We Confirmed

### The position copies we found (ALL are OUTPUT-ONLY — writing does NOT teleport)

| Address | Type | Notes |
|---------|------|-------|
| `0x1B384C4E118` | Heap entity slot | Roxas's exact position, entity stride = 0x200, pairs at +0x118 |
| `0x1B384C4E230` | Heap entity copy | Copy of above within same struct |
| `0x1B384CC9870` | Heap render buffer | Render copy, stride 0xF30 between entities |
| `0x7FF6E4CF6BC8` (exe+0xAD6BC8) | Static exe | Path history buffer (many sequential positions stored) |
| `0x7FF6E4CF9138` (exe+0xAD9138) | Static exe | Another static copy |

### Entity structure layout (heap, 0x1B384C4xxxx range)
- Entities are 0x200 bytes apart
- Entity 0 (party member): base ~0x1B384C4D890, pos at +0x88
- Entity 4 (Roxas): base ~0x1B384C4E090, pos at +0x88
- Each entity has a position sub-struct pointer at `entity+0x160`
- Position within sub-struct at `[entity+0x160]+0x20` = X,Y,Z,W (float,float,float,1.0)
- Position is also at entity+0x88 (position A) and entity+0x88+0x118 (position B) — both copies

### Copy chain we traced
```
Real position (UNKNOWN)
  → stack variable [RBX]
    → entity sub-struct [[RDI+0x160]+0x20]  (instruction: movups [rax+20],xmm0 at 7FF6E42656E3)
      → render buffer [RAX+0x110]  (instruction: movups [rax+110],xmm0 at 7FF6E42A26CA)
```

### Key code addresses
| Address | Instruction | What it does |
|---------|-------------|--------------|
| `7FF6E42656E3` | `movups [rax+20],xmm0` | Copies position from stack to entity sub-struct |
| `7FF6E42A26CA` | `movups [rax+110],xmm0` | Copies position from entity to render buffer |
| `7FF6E439F7D0` | Function | Entity processing, accesses entity+0x3C90 area |
| `7FF6E43A1418` | `mov rdx,[r14+40]` | Game manager accessing unit slot via pointer chain |

### Game manager / R14 entity
- R14 = `0x7FF6E675F2D0` (exe+0x253F2D0) — main game state object
- `[R14+0x40]` = pointer to unit slot area
- `[[R14+0x40]+0x5C0]` = HP address (confirmed)
- Entity has offsets up to 0x3C90+ but NO position floats or heap pointers in it

## What We RULED OUT
1. **Position is NOT in the unit slot** (0x278-byte slot at exe+0x2A23518)
2. **Position is NOT at any static exe offset we found** — those are all output copies
3. **Position is NOT in the heap entity structs we found** — those are also copies
4. **Writing to any found address does NOT teleport** — even continuous writes 24000x over 3 seconds
5. **Position is NOT stored as doubles** in the entity region
6. **The Slot0 pointer at exe+0x2A23518 leads to exe+0x2A23320** — this area contains only 0s and 1s, no position
7. **KH2SteamGlobal.lua has no position offsets** — only save data, menus, world IDs

## What to Try Next

### Approach 1: Trace the stack source
The position gets copied FROM `[RBX]` (stack) at instruction `7FF6E42656E0`. Find the CALLER of the function containing this instruction. The caller must have put the position on the stack. Disassemble backward from `7FF6E42656E3` to find the function prologue, then use `find_call_references` to find callers. The caller should reveal where it reads the real position from.

### Approach 2: CE's "Find what writes" on a FRESH copy
After a room transition (so all addresses refresh), set a write breakpoint on the entity position and capture the FULL call stack (if the bridge supports `capture_stack: true`). The return addresses in the stack will reveal the entire copy chain.

### Approach 3: Input/movement code tracing
Find the code that processes analog stick input (`Input = 0xBF3120` from KH2SteamGlobal.lua). Set a read breakpoint on the input address. The code that reads input must also write to the real position. Follow that code path.

### Approach 4: Try ALL entity offsets
The entity at 0x1B384C4E090 is 0x200 bytes. We only tested writing to the position at +0x88. There might be OTHER float offsets within the entity that ARE the authoritative position. Scan ALL float offsets within the entity for position-like values and try writing to each one.

### Approach 5: Broader memory scan
We only scanned ~215MB of the largest heap regions. There are 163 writable regions total. The real position might be in a smaller region we didn't scan. Do a comprehensive scan of ALL 163 writable regions.

### Approach 6: Fixed-point search
KH2 was a PS2 game. Position might be stored internally as fixed-point integers. Try scanning for the integer representations of known coordinates (e.g., 1805 * 16 = 28880, or 1805 * 256 = 462080).

## WARNINGS
- **Several copy addresses are CORRUPTED** from our write tests (showing X=2063.76 or X=2105.70 instead of real values). Walking around may or may not fix them. A room transition will definitely fix them.
- The heap addresses change between game sessions (ASLR). Only exe-relative offsets are stable.
- The bridge's `scan_all` MCP tool does integer scans by default. For float scans, use `evaluate_lua` to drive CE's scanner, or pass `value_type: "Float"` if the bridge supports it (the Python wrapper may not expose this parameter).
