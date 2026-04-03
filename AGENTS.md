### General Guidelines
- Refer to `docs/` as the primary source of truth, and always keep up to date with code changes
- Utilize cheat engine MCP and ghidra MCP when necessary
- **Before using Cheat Engine MCP tools**, verify CE is attached to the running KH2 process by calling `ping` and `get_process_info`. If the process is not attached or the game is not running, do not proceed with memory reads/writes/scans — prompt the user to launch KH2 and attach CE first.
- Always write detailed commit messages

### Quick KH2 Restart (`scripts/restart-kh2.ps1`)
Use this script to kill and relaunch KH2 without going through the Steam collection launcher:
```powershell
.\scripts\restart-kh2.ps1           # kill + rebuild inject DLL + relaunch
.\scripts\restart-kh2.ps1 -NoBuild  # kill + relaunch only
.\scripts\restart-kh2.ps1 -Kill     # kill only
.\scripts\restart-kh2.ps1 -CopyDll  # also copy inject DLL to game dir
```
Requires `steam_appid.txt` (containing `2552430`) in the game directory to bypass the launcher. Already placed there.

### KH2 Control CLI / MCP
Use `kh2ctl` as the canonical local control surface for automated KH2 testing. See `docs/KH2_CONTROL_CLI.md` for the full command set and current limitations.

Build:
```powershell
cmake --build build --target kh2ctl --config Release
```

Primary commands:
```powershell
.\build\tools\kh2ctl\Release\kh2ctl.exe state
.\build\tools\kh2ctl\Release\kh2ctl.exe wait-room --world 2 --room 1
.\build\tools\kh2ctl\Release\kh2ctl.exe load-save --slot 1
.\build\tools\kh2ctl\Release\kh2ctl.exe boot-load-save --slot 1
.\build\tools\kh2ctl\Release\kh2ctl.exe player-move --x 0.0 --y 1.0 --duration-ms 500
.\build\tools\kh2ctl\Release\kh2ctl.exe player-press --button confirm --duration-ms 100
.\build\tools\kh2ctl\Release\kh2ctl.exe move --slot friend1 --x 0.0 --y 1.0 --duration-ms 500
.\build\tools\kh2ctl\Release\kh2ctl.exe press --slot friend1 --button attack --duration-ms 100
```

Rules:

- Prefer `kh2ctl` over ad-hoc PowerShell/UI scripting when testing KH2 flows.
- Treat the CLI as the implementation source of truth. The MCP wrapper should stay thin and shell out to `kh2ctl`.
- Use the MCP wrapper (`tools/mcp_kh2ctl/server.py` or `scripts/run-kh2ctl-mcp.ps1`) when the client supports MCP tools and structured game-control calls are more convenient than shelling the CLI.
- Use `boot-load-save` for "restart KH2 and land in a playable save" workflows instead of manually chaining restart + title wait + menu navigation.
- Use `player-input/player-move/player-press` for native slot-0 control. These go through the inject DLL's raw input collector hook, not keyboard events.
- Friend-slot gameplay automation should go through mailbox-backed `kh2ctl input/move/press` commands, not bespoke memory writes.
- `load-save` / `boot-load-save` are still keyboard-driven macros for now. Native slot-0 gameplay input exists, but the save-menu flow has not been migrated off the keyboard helper yet.

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
Four reference mods showing proven character swap/addition techniques:

- **axel-mix** — Binary patching + KH2Moose DLL injection. Shows full `00battle.bin` + `03system.bin` binary replacement, custom magic effects, battle voice swaps per world, and Keyblade weapon reskin pipeline (all `W_EX010_*` variants).
- **dual-wield-roxas** — Pure YAML ObjEntry + Skeleton patching. Shows how to define custom PLAYER entities with dual-wield bone attachment (Bone1=172, Bone2=63) and custom ObjectForm types.
- **vanitas-remaster** — Most comprehensive. Shows `memt_0.list` party composition (18 slots per world), custom PLAYER entities in FRIEND slots, form/moveset mapping, full stat/bonus configuration, custom event scripting (ARD files), and multi-language message patching.
- **master-trio** — Armored Sora/Riku/Kairi. Replaces Goofy & Donald with Riku & Kairi. Uses `AtkpList.yml` (attack parameters) and `PlrpList.yml` (player parameters) for combat customization. Shows `SkltList.yml` skeleton definitions, custom keyblades (`W_EX020_*` for Donald's staff → Riku's weapon, `W_EX030_*` for Goofy's shield → Kairi's weapon), limit break overrides, and libretto (cutscene dialog) patches.

Key patterns for multiplayer: use `memt_0.list` to spawn Player 2 in a FRIEND slot, define their ObjEntry with PLAYER type, and map to appropriate NeoMoveset/NeoStatus IDs. Master-trio's `AtkpList.yml` is the reference for how attack parameters can be customized per-character.

### Swarm Coordination

Portable coordination rules for multi-agent sessions using `swarm-mcp`.

Tool names are namespaced by the host. Depending on the client you may see `swarm_register`, `mcp__swarm__register`, or similar variants. Use whichever form your host exposes.

#### Register early

At the start of every session, call `register` before using any other swarm tool.

- `directory`: your current project directory (required)
- `scope`: omit unless you want multiple directories to share one swarm
- `file_root`: omit unless working in a disposable worktree that should share locks and annotations with a stable checkout
- `label`: optional, but prefer machine-readable tokens like `provider:codex-cli role:planner team:frontend`

No `role:` token means the session is a generalist.

#### Check for pending work

Immediately after registering, call `poll_messages`, `list_tasks`, and `list_instances`.

- If you have unread messages, read and act on them before starting new work.
- If there are tasks assigned to you (by instance ID or matching your `role:`), claim and prioritize them.
- If you see open `review` tasks and you handle reviews, claim them before starting implementation work.
- If nothing is waiting, proceed with your own task.

Check `poll_messages` and `list_tasks` periodically, not just at startup.

#### React to what you find

When you receive a task via `request_task`:

- `claim_task` immediately so no other session takes it
- Call `update_task` to `in_progress` when you start
- Call `update_task` to `done` with a short `result` when finished, or `failed` with what went wrong
- If the task requires follow-up, create a new `request_task` (e.g. the implementer sends a `review` task back to the planner)

When you receive a direct message via `send_message`:

- Treat it as coordination, not a formal task. Respond with `send_message` or take action.

When you see a `broadcast`:

- Use it for awareness. No response is required unless the content affects your current work.

#### Check before editing

Before editing a file, call `check_file` for that path. If another session has a lock or warning, avoid overlap and coordinate first.

#### Lock while editing

When you begin editing a file, call `lock_file` with a short reason.

Unlock it with `unlock_file` as soon as you are done. Keep locks short and specific.

#### Delegate clearly

Use `request_task` for review, implementation, fix, test, or research handoffs.

Include a short title, a useful description, and relevant `files` when possible. Set `assignee` only when you want a specific active session to take it.

When choosing who to delegate to, inspect `list_instances` labels:

- Prefer a session with a matching `role:` token (e.g. `role:reviewer` for review work)
- If the swarm uses `team:` labels, prefer a same-team specialist
- Fall back to any matching specialist, then to a generalist

#### Share context

Use `annotate` to leave findings, warnings, notes, bugs, or todos on files.

Use `broadcast` for short updates that help everyone stay in sync. Use `send_message` for direct coordination with one session.

#### Track shared state

Use `kv_set` and `kv_get` for small shared state like plans, owners, or handoff notes.

Keep values short and structured. JSON strings work well when the value needs a little shape.

##### Progress heartbeats

While working on a task, periodically update your status:

- Key: `progress/<your-instance-id>`
- Value: short summary of current activity and progress (e.g. `"implementing auth middleware, ~50% done"`)

This lets planners and other agents check on you with `kv_list("progress/")` without interrupting your work. Clear your progress key when you finish a task or go idle.

#### Stay autonomous

After your initial registration and inspection, **do not wait for user prompting between tasks**. Use `wait_for_activity` to stay in an active loop:

1. After completing a task or when you have nothing to do, call `wait_for_activity`.
2. When it returns with changes, act on them immediately:
   - **new_messages**: Read and respond. Messages prefixed with `[auto]` are system notifications about task assignments or completions.
   - **task_updates**: Claim open tasks or review completed ones, depending on your role.
   - **instance_changes**: Adapt to agents joining or leaving.
3. If it returns with `timeout: true`, call `wait_for_activity` again — or check `list_tasks` for anything you may have missed.
4. Repeat until the work is done.

Task creation and completion automatically notify the relevant parties via message. You don't need to manually `send_message` to inform someone about a task you created for them or completed — but you can add extra context if helpful.

#### Finish cleanly

When you complete assigned work:

1. `unlock_file` any files you locked
2. `update_task` with `done` and a short `result`
3. If follow-up is needed, create a new `request_task` (don't reuse the old one)
4. `broadcast` a short summary if other sessions should know
