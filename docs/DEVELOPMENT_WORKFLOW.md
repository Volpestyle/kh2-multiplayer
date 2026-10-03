# Development Workflow

How to build, test, and iterate on the inject DLL against a running KH2 instance.

## Prerequisites

- KH2 HD 1.5+2.5 ReMIX (Steam Global), with Steam running
- Visual Studio 2019+ build tools (for CMake/MSVC)
- `steam_appid.txt` containing `2552430` in the KH2 game directory (bypasses Steam launcher)
- A desktop session. KH2 crashes at startup (`0xC0000005`) when launched from
  Windows session 0 (services, SSH, some remote agents), which has no display.
  Run the rig from a terminal on the logged-in desktop.
- Cheat Engine 7.x for runtime analysis only; injection no longer needs it

## The development loop

```
1. Edit code (inject/src/EntityHook.cpp)
2. kh2ctl restart        kill rig instances, rebuild the DLL, launch, inject
3. Load a save (get into gameplay with Donald in party)
4. Press F5 to toggle solo mode
5. Test, read build/rig/logs/kh2coop_inject_<pid>.log, repeat from step 1
```

`kh2ctl` is `.\build\tools\kh2ctl\Release\kh2ctl.exe`. The kh2ctl MCP server
exposes the same commands (`launch_kh2`, `inject_kh2`, `list_instances`,
`kill_kh2`, `restart_kh2`).

```powershell
kh2ctl launch            # launch + inject; prints pid, log path, installed hooks
kh2ctl instances         # KH2 processes, and which ones the rig owns
kh2ctl kill --all        # kill rig-launched instances only
kh2ctl inject --pid N    # inject into an already running KH2
```

To see an instance, use `kh2ctl capture --pid N` (PNG from inside the
renderer, works behind other windows) or `kh2ctl clip --pid N` (MP4), and
`kh2ctl overlay --pid N on` for pid, frame, world/room and fps on screen.

`launch` reports `"hooksInstalled":true` and the hook list once the DLL's init
log shows every hook. Measured 2026-10-01: 10 of 10 launches installed all four
hooks, about 2 s from launch to injection.

Loading a save is still manual or step-by-step `tap-key`; `boot-load-save` is
known broken (see `KH2_CONTROL_CLI.md`).

## Safety

- The rig records the processes it launches in `build/rig/owned.txt` and kills
  only those. `restart` refuses to run while a KH2 it didn't launch is open,
  because that means James is playing.
- `scripts/restart-kh2.ps1` kills **every** KH2 process. It is for a human at
  the keyboard; agents use `kh2ctl restart` and `kh2ctl kill`.
- Never save in-game during automation (AGENTS.md).

## Running the rig from a session-0 agent

Herdr's current server and its PowerShell panes run in Windows session 0.
The game and its `Local\\kh2coop_*` shared-memory channels need the logged-in
desktop session. On this PC the existing `desk-agent` task runs
`C:\desk\agent.ps1` in session 1. It executes `.ps1` jobs from `C:\desk\jobs`
and moves each completed job, with its `.out` result, to `C:\desk\done`.
This bridge was verified on 2026-10-02 with a read-only job reporting session 1.

Use that existing bridge for `tools/scenario/run.py` and `kh2ctl`; the usual
rig lock, process ownership and save guards still apply. Write a uniquely named
job outside the queue, then move it into the queue so the agent never reads a
partial script. For a long scenario, start a hidden process with explicit
stdout/stderr files under `build/rig/`, retain its PID, and inspect its report
and exit status. Do not create another scheduled task or change the bridge.

Check that `desk-agent` is actually running and that a read-only job reports
the interactive session before using it. A task definition alone does not
establish an available desktop worker.

A Herdr session name does not establish its Windows session. Restarting a
desktop-named session from SSH can place the server and tool processes back
in session 0. Check the actual `exec_command` process SessionId before live
testing, then use the existing desktop bridge if it is 0. Native agent tasks
may also stop when their owning CLI is replaced; preserve their partial files
and verify the old runtime stopped before assigning those paths again.

## Rebuilding while KH2 runs

Each injection loads a fresh copy of `build/inject/staging/kh2coop_inject.dll`
from `build/rig/dll/`, so the linker never hits a locked DLL and you can rebuild
while instances run. A running instance keeps the build it was injected with;
relaunch it to pick up a new one. Old copies in `build/rig/dll/` can be deleted
when no instance is running.

## Log file

Rig-launched instances log to `build/rig/logs/kh2coop_inject_<pid>.log`
(`launch` sets `KH2COOP_LOG_DIR` for the game process). A DLL injected any other
way logs to `kh2coop_inject_<pid>.log` in the KH2 game directory.

Check the log for:
- Hook installation success/failure
- Friend entity detection
- Animation override events
- Movement injection diagnostics
- Periodic status (every 300 frames)

## Manual injection (Cheat Engine fallback)

Only needed when `kh2ctl inject` can't be used:

```lua
openProcess("KINGDOM HEARTS II FINAL MIX.exe")
local loadLibA = getAddress("kernel32.LoadLibraryA")
local mem = allocateMemory(512)
writeString(mem, "C:\\Users\\volpe\\repos\\kh2-multiplayer\\build\\inject\\staging\\kh2coop_inject.dll", false)
createRemoteThread(loadLibA, mem)
```

A DLL loaded this way locks the staging file until KH2 exits.

## Building other targets

```powershell
cmake --build build --target kh2ctl --config Release           # CLI tool
cmake --build build --target kh2coop_server --config Release   # multiplayer server
cmake --build build --target kh2coop_fake_sim --config Release # E2E test (no KH2 needed)
cmake --build build --config Release                           # everything
```

## Testing without KH2

The E2E test (`kh2coop_fake_sim`) exercises the codec, networking, and server with 3 simulated clients. No running KH2 instance needed:

```powershell
.\build\Release\kh2coop_fake_sim.exe
```

## Using kh2ctl

The CLI tool for automated KH2 control. Player and friend input commands need the inject DLL loaded.

```powershell
.\build\tools\kh2ctl\Release\kh2ctl.exe state               # attach; room and party actor state
.\build\tools\kh2ctl\Release\kh2ctl.exe wait-ingame          # wait for a live room
.\build\tools\kh2ctl\Release\kh2ctl.exe tap-key --key down   # focus KH2 and tap a key
.\build\tools\kh2ctl\Release\kh2ctl.exe player-press --button confirm
```

See `docs/KH2_CONTROL_CLI.md` for the full command reference.

## RE workflow (Cheat Engine + Ghidra)

When reverse engineering game internals:

1. **Find the address** — CE scans, data breakpoints, pointer chains
2. **Understand the code** — Ghidra decompile at the address, trace xrefs
3. **Verify live** — CE breakpoints to confirm behavior matches decompilation
4. **Document** — Add offset to `KH2Offsets.hpp`, update `pointer_map_v1.md`
5. **Hook it** — Add to `EntityHook.cpp` if needed

See `docs/LESSONS_LEARNED.md` for hard-won RE insights.
