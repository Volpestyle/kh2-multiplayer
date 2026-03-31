### General Guidelines
- Refer to `docs/` as the primary source of truth, and always keep up to date with code changes
- Utilize cheat engine MCP and ghidra MCP when necessary
- Always write detailed commit messages

### Testing Principle
**Tests follow stabilization, not implementation.** Write regression tests only after an interface stops changing (e.g., codec is locked, offsets are confirmed via Cheat Engine). Do not write tests while actively discovering memory layouts or prototyping — that's churn. For live KH2 memory code (`GameBridgePC`, `CameraController`, etc.), manual smoke tests against a running game process are the real validation; you cannot meaningfully mock `ReadProcessMemory` against KH2. Reserve unit/integration tests for stabilized boundaries like the codec, protocol, and networking layer.

### OpenKH Reference (`../openkh`)
The OpenKH repository (https://github.com/OpenKH/OpenKh) is a sibling directory containing a KH2 modding toolkit.
See `docs/OPENKH_REFERENCE.md` for a full guide. Consult it when:

- **Looking up animation IDs** — `openkh/OpenKh.Kh2/MotionSet.cs` has the complete enum (IDLE=0, WALK=1, RUN=2, JUMP=3, etc.). Needed for animation sync.
- **Identifying entity types** — `openkh/OpenKh.Kh2/Objentry.cs` defines PLAYER=0, FRIEND=1, NPC=2, BOSS=3, ZAKO=4. Use during entity discovery RE.
- **Understanding party slot mapping** — `openkh/OpenKh.Kh2/SystemData/Memt.cs` defines which character occupies Friend1/Friend2 per world. Critical for friend entity RE.
- **Resolving character slot indices** — Sora=0, Donald=1, Goofy=2, Mickey=3, etc. Maps our SlotType to the game's internal indices.
- **Understanding attack/damage params** — `openkh/OpenKh.Kh2/Battle/Atkp.cs` for damage sync (M5).
- **Considering DLL injection** — `openkh/OpenKh.Research.Panacea/` has a working DLL injection + function hooking framework. If we need in-process hooks (camera stability, input injection), Panacea is the reference.
- **Looking up world IDs** — `openkh/OpenKh.Kh2/Constants.cs` maps all 19 world IDs to names.

Do NOT consult OpenKH for runtime memory offsets — those come from our Cheat Engine sessions and `KH2Offsets.hpp`. OpenKH's struct definitions are for on-disk file formats, not runtime memory layouts.

### KH2 Lua Library Reference (`../kh2-lua-library`)
A community-maintained collection of KH2 runtime memory addresses for all PC versions + PCSX2 (https://github.com/aliosgaming/KH2-Lua-Library). Already used as a source for many `[KH2LIB]`-tagged offsets in `KH2Offsets.hpp`. Consult it when:

- **Looking up addresses for non-Steam-Global builds** — `KH2EpicGlobal.lua`, `KH2SteamJP.lua`, `KH2Emulator.lua` have complete address tables for multi-version support.
- **Finding unit slot internal offsets** — The GoA example uses MP at `Slot+0x180/0x184`, invincibility at `Slot+0x1AE`, drive gauge at `Slot+0x1B0..0x1B2`.
- **Save file structure** — Complete inventory map (`Save+0x3580..0x36CA`), world progress flags, ability slots, equipment offsets.
- **Detecting game state** — `LoadingIndicator` (`0x8EC540`), `CurrentOpenMenu` (`0x7435D0`), `Continue` (`0x29FB500`).
- **Game version auto-detection** — Signature bytes at `0x566A8E` (Epic), `0x56668E` (Steam Global), `0x56640E` (Steam JP).

Do NOT consult for entity transforms, position data, camera struct details, animation IDs, enemy lists, or friend entity discovery — those require original Cheat Engine RE and are documented in our own `KH2Offsets.hpp`.

### Ghidra + GhidraMCP (`../GhidraMCP`)
Ghidra (installed at `../ghidra_12.0.4_PUBLIC`) with the GhidraMCP plugin provides static binary analysis of the KH2 executable via MCP. The Ghidra MCP server exposes tools over HTTP (port 8080) and is configured as an MCP server in `.claude.json`. Use it when:

- **Decompiling functions** — get C-like pseudocode for any function address discovered via Cheat Engine.
- **Tracing cross-references (xrefs)** — "who calls this function?" and "who reads/writes this address?" across the entire binary.
- **Understanding call chains** — statically trace entity spawn logic, party slot wiring, or animation dispatch without setting breakpoints.
- **Renaming/annotating** — label discovered functions and data in Ghidra so decompilation output stays readable across sessions.

Use Ghidra for static analysis (understanding code structure). Use Cheat Engine for dynamic analysis (runtime values, breakpoints, AOB scans). Cross-reference both: find addresses in CE, understand them in Ghidra, verify hypotheses back in CE.

### LuaBackend Source (`../kh2-tools/LuaBackend`)
The LuaBackend source repo (https://github.com/Sirius902/LuaBackend) — a Lua scripting engine that hooks into KH2's frame loop via DLL proxy injection (`DBGHELP.dll`). Consult it when:

- **Understanding the hook mechanism** — `main_dll.cpp` shows how the frame hook is installed via vtable patching after pattern scanning.
- **Reviewing the Lua API surface** — `LuaBackend.cpp` binds `ReadInt`/`WriteInt`/`ReadFloat`/`WriteFloat`/`GetPointer`/etc. to Lua. These are the same APIs used by community Lua mods.
- **Considering LuaBackend as a runtime layer** — for multiplayer, Lua scripts could read player state per-frame and feed it to a network layer. Scripts support hot reload (F1) and run at 60/120/240Hz synced to the game loop.
- **Referencing memory access patterns** — `MemoryLib.h` shows how the game's memory is read/written with proper page protection handling.

Do NOT modify LuaBackend source for our mod without careful consideration — it's an upstream dependency shared with the broader KH2 modding community.

### KHPCPatchManager Source (`../kh2-tools/KHPCPatchManager`)
The KH2 PC Patch Manager (https://github.com/AntonioDePau/KHPCPatchManager) — tool for creating and applying binary patches to KH2 game files. Consult it when:

- **Understanding how mods are distributed** — patches `00objentry.bin`, `00battle.bin`, `03system.bin`, `memt_0.list`, etc.
- **Planning mod packaging** — if the multiplayer mod needs to ship ObjEntry or party table changes, this is the distribution mechanism.

### Character Mod Examples (`../kh2-tools/mods/`)
Three reference mods showing proven character swap/addition techniques:

- **axel-mix** — Binary patching + KH2Moose DLL injection with 1.5GB cache. Shows runtime memory manipulation via DLL.
- **dual-wield-roxas** — Pure YAML ObjEntry + Skeleton patching. Shows how to define custom PLAYER entities with dual-wield bone attachment (Bone1=172, Bone2=63) and custom ObjectForm types.
- **vanitas-remaster** — Most comprehensive. Shows `memt_0.list` party composition (18 slots per world), custom PLAYER entities in FRIEND slots, form/moveset mapping, and full stat/bonus configuration.

Key patterns for multiplayer: use `memt_0.list` to spawn Player 2 in a FRIEND slot, define their ObjEntry with PLAYER type, and map to appropriate NeoMoveset/NeoStatus IDs.
