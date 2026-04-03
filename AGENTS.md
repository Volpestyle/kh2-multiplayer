### General Guidelines
- Refer to `docs/` as the primary source of truth, and always keep up to date with code changes
- See `docs/CODEBASE_MAP.md` for the full directory/file inventory
- See `docs/DEVELOPMENT_WORKFLOW.md` for the build/inject/test loop
- See `docs/LESSONS_LEARNED.md` before starting any RE work
- Always write detailed commit messages

### Cheat Engine MCP
- **Before using Cheat Engine MCP tools**, verify CE is attached to the running KH2 process by calling `ping` and `get_process_info`. If the process is not attached or the game is not running, do not proceed with memory reads/writes/scans — prompt the user to launch KH2 and attach CE first.
- **When you need the user to do something in-game that cannot be automated via `kh2ctl`** (e.g., trigger a specific breakpoint scenario, engage a particular enemy, enter a cutscene, or perform a complex sequence not covered by the CLI), **stop and make a clear, explicit request to the user** before continuing. Do not assume the user has done it or proceed without confirmation. Wait for the user to confirm they have completed the action before resuming analysis. For actions that *can* be automated (loading saves, basic movement, button presses), prefer using `kh2ctl` instead of asking the user.

### Quick KH2 Restart (`scripts/restart-kh2.ps1`)
```powershell
.\scripts\restart-kh2.ps1           # kill + rebuild inject DLL + relaunch
.\scripts\restart-kh2.ps1 -NoBuild  # kill + relaunch only
.\scripts\restart-kh2.ps1 -Kill     # kill only
.\scripts\restart-kh2.ps1 -CopyDll  # also copy inject DLL to game dir
```
Requires `steam_appid.txt` (containing `2552430`) in the game directory to bypass the launcher. Already placed there.

### KH2 Control CLI / MCP
Use `kh2ctl` as the canonical local control surface for automated KH2 testing. See `docs/KH2_CONTROL_CLI.md` for the full command set and current limitations.

Build: `cmake --build build --target kh2ctl --config Release`

Rules:
- Prefer `kh2ctl` over ad-hoc PowerShell/UI scripting when testing KH2 flows.
- Use `boot-load-save` for "restart KH2 and land in a playable save" workflows.
- Use `player-input/player-move/player-press` for native slot-0 control (goes through inject DLL's raw input collector hook).
- Friend-slot gameplay automation should go through mailbox-backed `kh2ctl input/move/press` commands.

### Testing Principle
**Tests follow stabilization, not implementation.** Write regression tests only after an interface stops changing. For live KH2 memory code (`GameBridgePC`, `CameraController`, etc.), manual smoke tests against a running game process are the real validation. Reserve unit/integration tests for stabilized boundaries like the codec, protocol, and networking layer.

### External Reference Repos

**OpenKH** (`../openkh`) — KH2 modding toolkit. See `docs/OPENKH_REFERENCE.md`. Use for animation IDs, entity types, party slot mapping, world IDs. Do NOT use for runtime memory offsets (those come from `KH2Offsets.hpp`).

**KH2 Lua Library** (`../kh2-lua-library`) — Community runtime memory addresses for all PC versions. Use for non-Steam-Global builds, unit slot internal offsets, save file structure, game state detection. Do NOT use for entity transforms, camera, or animation RE.

**Ghidra + GhidraMCP** (`../GhidraMCP`) — Static binary analysis via MCP. Use for decompiling functions, tracing xrefs, understanding call chains, renaming/annotating. Use Ghidra for static analysis, CE for dynamic analysis. Cross-reference both.

**LuaBackend** (`../kh2-tools/LuaBackend`) — Lua scripting engine with frame hook via DLL proxy. Reference for hook mechanisms, Lua API, memory access patterns.

**KHPCPatchManager** (`../kh2-tools/KHPCPatchManager`) — Binary patching tool. Reference for mod distribution packaging.

**Character Mod Examples** (`../kh2-tools/mods/`) — Four reference mods (axel-mix, dual-wield-roxas, vanitas-remaster, master-trio) showing character swap/addition techniques. Key patterns for multiplayer: `memt_0.list` for party composition, ObjEntry for custom entities, AtkpList for attack parameters.

### Swarm Coordination
For multi-agent sessions, load the `swarm-mcp` skill (or `swarm-planner`/`swarm-implementer` for specific roles). Those skills contain the full coordination protocol.
