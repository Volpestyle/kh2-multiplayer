### Plan and tracking
- Plan of record: `docs/ONLINE_COOP_PLAN.md` (decisions D1–D12, risks, phase gates). Work is tracked in the Linear project **KH2 Multiplayer** (vuhlp workspace); close an issue with its evidence (scenario report, screenshots or clips).
- Only one agent drives live KH2 at a time (the "live lane"). Other agents work offline: network/protocol code, headless Ghidra, tooling, docs. The scenario runner enforces this with the rig lock `build/rig/rig.lock`: if it's held by a live process, don't launch KH2 — ask the holder (its pid and command are in the file).

### Scenarios (the live regression suite)
```powershell
python tools/scenario/run.py tools/scenario/scenarios/boot_to_goa.json --repeat 5
```
- A scenario is JSON steps (boot, warp, input, press, wait, assert, capture, …) with Python assertions over live state. Format, helpers and the example scenarios: `docs/SCENARIOS.md`.
- Each run writes `build/scenarios/<stamp>_<name>_<n>/report.md` + `report.json` with captures and logs. A crash or hang produces a bundle (minidump + inject log), fails the run, kills its instances, and the runner moves on.
- **Evidence:** close a live-behavior issue with a passing scenario report (and its captures/clips) attached to the Linear issue. Write the scenario first when the behavior can be checked from memory or the hit log; add it under `tools/scenario/scenarios/` so it joins the suite.
- Exit codes: 0 pass, 1 fail (or save changed), 2 crash/hang, 3 rig unavailable (lock held or a non-rig KH2 is running — James may be playing; wait).

### Safety rules
- Never write James's save under `OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX/` — Steam Cloud syncs it. Never save in-game during automation. Backstops: the inject DLL's save guard redirects every write open in that folder to `build/rig/logs/save_sandbox_<pid>/` and denies deletes, moves and copies there (`[saveguard]` lines in the log), and the scenario runner fails the suite if any save file's SHA-256 changes. Neither is a licence to try: don't attempt an in-game save without James's approval.
- Launch, restart and kill KH2 only through `kh2ctl launch/restart/kill`: the rig kills only processes it launched and refuses to restart while a KH2 it didn't launch is open (James may be playing). Agents never run `restart-kh2.ps1`; it kills every KH2 process.
- No public network exposure, accounts or third-party services without James.

### Proportional rigor
Match verification to what a change can break. On 2026-10-04 four live runs in a row failed in test setup, and each one-line harness fix took about an hour of freezing, sealing and independent review. Don't repeat that.
- **Full rigor** (pinned inputs, independent review, sealed results): code that runs inside the game (inject DLL, memory writes, hooks), anything near James's save, and acceptance runs for a Linear gate.
- **Just change it and run it:** scenario fixtures and harness steps (timing, ordering, movement helpers, budgets), diagnostics and logging that don't change behaviour, and docs. Read the report; if it fails, fix and rerun.
- Use the deterministic test as the everyday regression (e.g. the forced resync for VUH-1508); keep long or flaky acceptance runs for when a change is ready.
- When the last two attempts failed in setup or harness work rather than in the feature, stop polishing evidence and shorten the path to an actual attempt.
- Record limits once, in the result file. Status and result write-ups are for James: short, plain language, run ID and evidence path, no hash dumps or stacked qualifiers.

### General Guidelines
- Refer to `docs/` as the primary source of truth, and always keep up to date with code changes
- See `docs/CODEBASE_MAP.md` for the full directory/file inventory
- See `docs/DEVELOPMENT_WORKFLOW.md` for the build/inject/test loop
- See `docs/LESSONS_LEARNED.md` before starting any RE work
- Always write detailed commit messages

### Cheat Engine MCP
- **Before using Cheat Engine MCP tools**, verify CE is attached to the running KH2 process by calling `ping` and `get_process_info`. If the process is not attached or the game is not running, do not proceed with memory reads/writes/scans — prompt the user to launch KH2 and attach CE first.
- **When you need the user to do something in-game that cannot be automated via `kh2ctl`** (e.g., trigger a specific breakpoint scenario, engage a particular enemy, enter a cutscene, or perform a complex sequence not covered by the CLI), **stop and make a clear, explicit request to the user** before continuing. Do not assume the user has done it or proceed without confirmation. Wait for the user to confirm they have completed the action before resuming analysis. For actions that *can* be automated (loading saves, basic movement, button presses), prefer using `kh2ctl` instead of asking the user.

### Launching KH2 (the rig)
```powershell
kh2ctl launch            # launch + inject the current DLL build; reports pid, log, hooks
kh2ctl restart           # kill rig instances, rebuild the DLL, launch + inject
kh2ctl instances         # KH2 processes and which ones the rig owns
kh2ctl kill --all        # kill rig-launched instances only
```
Run from the desktop session (KH2 crashes at startup from session 0). Logs: `build/rig/logs/kh2coop_inject_<pid>.log`. Requires `steam_appid.txt` (containing `2552430`) in the game directory to bypass the launcher; already placed there. See `docs/DEVELOPMENT_WORKFLOW.md`.

### KH2 Control CLI / MCP
Use `kh2ctl` as the canonical local control surface for automated KH2 testing. See `docs/KH2_CONTROL_CLI.md` for the full command set and current limitations.

Build: `cmake --build build --target kh2ctl --config Release`

Rules:
- Prefer `kh2ctl` over ad-hoc PowerShell/UI scripting when testing KH2 flows.
- `boot-load-save` is known broken (keys land before the title menu exists; see `docs/KH2_CONTROL_CLI.md`). Load saves with the scenario runner's `boot` step, which checks each menu move in a capture.
- Use `player-input/player-move/player-press` for native slot-0 control (goes through inject DLL's raw input collector hook).
- Friend-slot gameplay automation should go through mailbox-backed `kh2ctl input/move/press` commands.
- `avatarctl` (`build/Release/avatarctl.exe`) drives a KH2 instance's AvatarBridge without a network: `synth` runs a puppet around a circle, `record`/`replay` capture and replay the local avatar stream into a puppet slot, `fake-local` stands in for the DLL, `peek` prints the bridge. Usage is in `tools/avatarctl/main.cpp`.

### Testing Principle
**Tests follow stabilization, not implementation.** Write regression tests only after an interface stops changing. For live KH2 memory code (`GameBridgePC`, `CameraController`, etc.), manual smoke tests against a running game process are the real validation. Reserve unit/integration tests for stabilized boundaries like the codec, protocol, and networking layer.

### External Reference Repos

**OpenKH** (`~/openkh`) — KH2 modding toolkit. See `docs/OPENKH_REFERENCE.md`. Use for animation IDs, entity types, party slot mapping, world IDs. Do NOT use for runtime memory offsets (those come from `KH2Offsets.hpp`).

**KH2 Lua Library** (`~/kh2-lua-library`) — Community runtime memory addresses for all PC versions. Use for non-Steam-Global builds, unit slot internal offsets, save file structure, game state detection. Do NOT use for entity transforms, camera, or animation RE.

**Ghidra, headless** (`scripts/ghidra.ps1`) — no GUI needed, safe for several agents at once (read-only). `-Setup` builds the fully analyzed project in `build/ghidra/kh2_full` once; then `-Decompile 0xRVA[,0xRVA]` prints the function with its callers/callees, and `-Xrefs 0xRVA [-Window 0x40] [-MaxDecomp 3]` lists every read/write near an address and decompiles the users. RVAs match `pointer_map_v1.md`. Prefer this to `~/kh2.gpr`, which lacks data xrefs.

**Ghidra + GhidraMCP** (`~/GhidraMCP`) — Static binary analysis via MCP against a running Ghidra GUI. Use for renaming/annotating interactively. Use Ghidra for static analysis, CE (or the rig's probes) for dynamic analysis. Cross-reference both.

**LuaBackend** (`~/kh2-tools/LuaBackend`) — Lua scripting engine with frame hook via DLL proxy. Reference for hook mechanisms, Lua API, memory access patterns.

**KHPCPatchManager** (`~/kh2-tools/KHPCPatchManager`) — Binary patching tool. Reference for mod distribution packaging.

**Character Mod Examples** (`~/kh2-tools/mods/`) — Four reference mods (axel-mix, dual-wield-roxas, vanitas-remaster, master-trio) showing character swap/addition techniques. Key patterns for multiplayer: `memt_0.list` for party composition, ObjEntry for custom entities, AtkpList for attack parameters.

### Swarm Coordination
For multi-agent sessions, load the `swarm-mcp` skill (or `swarm-planner`/`swarm-implementer` for specific roles). Those skills contain the full coordination protocol.
