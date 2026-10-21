# Development Workflow

How to build, test, and iterate on the inject DLL against a running KH2 instance. See [setup](../SETUP.md) for build requirements. Read the `kh2-rig` skill before live work, and respect the single live lane and `build/rig/rig.lock`.

## Prerequisites

- KH2 HD 1.5+2.5 ReMIX (Steam Global), with Steam running
- Visual Studio 2019+ build tools (for CMake/MSVC)
- 64-bit MSVC and MASM (`ml64`); the Windows inject target enables `ASM_MASM`
  for its internal owned-emitter gateway
- `steam_appid.txt` containing `2552430` in the KH2 game directory (bypasses Steam launcher)
- A desktop session. KH2 crashes at startup (`0xC0000005`) when launched from
  Windows session 0 (services, SSH, some remote agents), which has no display.
  Run the rig from a terminal on the logged-in desktop.
- Cheat Engine 7.x for runtime analysis only; injection no longer needs it

## The development loop

```
1. Edit code (inject/src/EntityHook.cpp)
2. kh2ctl restart        kill rig instances, rebuild the DLL, launch, inject
3. Load a save with the scenario runner's checked boot step
4. Start matched runtime(s) / relay, or use the player launcher for its defaults
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
`kh2ctl overlay --pid N on` for pid, frame, world/room, fps and connected RTT/loss on screen.

The D3D12 renderer selects fresh external DIRECT submissions from the Present
thread and retains one queue/device/swapchain for its fence ring. A nonblocking
gate serializes injected CPU work across the game's startup-to-main Present
thread handoff; switching CPU callers does not switch the GPU queue. It skips work
without an eligible submission and stops GPU work after a failed reset, close,
signal, wait, or removed-device fence result; restart the instance after a GPU
failure. Last queue references are intentionally retained at process teardown.
Set `KH2COOP_RENDER_DIAGNOSTICS=1` before launch for bounded queue, owner,
Present metadata and first-failure receipts. Diagnostics do not establish queue
association by themselves. The combined overlay-ON native result is
historical local run `012155`; its ignored receipt is
`build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined6-result.md`.

`launch` reports `"hooksInstalled":true` and the hook list once the DLL's init
log shows every hook. Measured 2026-10-01: 10 of 10 launches installed all four
hooks, about 2 s from launch to injection.

Use the scenario runner's checked `boot` step to load a save; `boot-load-save` is
known broken (see `KH2_CONTROL_CLI.md`).

## Friendly fire (VUH-2421)

Set `KH2COOP_FRIENDLY_FIRE=0` on the **host runtime** to turn friendly fire
off; unset or any other value keeps it on (the default). The relay advertises
the admitted host's setting to every peer at session start, and a friend's
environment cannot override it (protocol v25). Off refuses damaging
player-to-player hits at the native hit check (3D2060), so there is no damage,
reaction or knockback from those hits; heal-like kinds 5/6 and enemy hits are unchanged. On preserves the existing native hit policy.
`KH2COOP_ALLY_HIT=0` installs no hit hook, so the setting has no effect there.
A change applies when the host next connects.

## Co-op enemy health (VUH-2194)

Set `KH2COOP_COOP_DIFFICULTY=off|normal|hard` on the **host runtime**.
Unset or unknown values select Off. The relay advertises the admitted host's
mode; a friend's environment cannot override it. This feature was introduced in
protocol v18; current builds require matching protocol **32** peers. Older peers
receive a protocol mismatch rejection. The player launcher selects Normal;
direct runtime launches default to Off.

The host locks the mode and connected player count when it announces a native
room, including when hosting starts mid-room. Joins/leaves apply to the next
room. The room packet carries this count to friends, including late joiners.
Factors are placeholders, in one/two/three-player order:

| Mode | Ordinary enemies | Bosses |
|---|---|---|
| Off | 1 / 1 / 1 | 1 / 1 / 1 |
| Normal | 1 / 1.6 / 2.2 | 1 / 1.3 / 1.6 |
| Hard | 1.3 / 2 / 2.7 | 1.2 / 1.7 / 2.2 |

On the host, the first valid living HP sample for each existing native spawn
scales current and max HP once, rounding upward and saturating at INT32_MAX.
Transport resets retain this once-only history; native room teardown clears it.
Friends mirror both values through the existing shared-HP path; native deaths
still use its existing death path. Off makes no difficulty HP writes and keeps
the vanilla native values. Enemy count and incoming player damage are unchanged.

Focused checks: `kh2coop_coop_difficulty_test`, `kh2coop_avatar_test`,
`kh2coop_world_test`, and `kh2coop_bridge_test`. Live regression:
`python -B tools/scenario/run.py tools/scenario/scenarios/net_coop_difficulty_health.json`.
That fixture checks host Normal versus friend Hard, two-player native current/max
HP, once-only scaling after damage, and shared deaths, with game clips.

## Safety

- The rig records the processes it launches in `build/rig/owned.txt` and kills
  only those. `restart` refuses to run while a KH2 it didn't launch is open,
  because that means James is playing.
- `scripts/restart-kh2.ps1` kills **every** KH2 process. It is for a human at
  the keyboard; agents use `kh2ctl restart` and `kh2ctl kill`.
- Do not save in-game during automation without James's explicit approval (AGENTS.md).
  The single attempt approved on 2026-10-04 is consumed: its sandbox write and
  unchanged four originals are [verified separately](SCENARIOS.md#guarded-native-menu-save-2026-10-04),
  while its automation deadline result remains FAIL. It grants no further saves.

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

On 2026-10-03, a strict wave census failed with WinError 5 at Python
`_winapi.CreateProcess`; matching Defender 1116/1117 events identified a
command-line detection for `kh2ctl peek`. The helper never started for those
reads. Retain `readFailures` stage/argv/traceback and matching local event
evidence; incomplete classification is not native enemy absence. This is
separate from an `OpenProcess`/`ReadProcessMemory` error inside a running helper.
The detection's correctness remains unestablished. Live runs were held and
offline development continued without protection or permission changes.

## Rebuilding while KH2 runs

Current source uses protocol **32**, AvatarBridge **4**, WorldBridge **12**
and CaptureChannel **1**. Rebuild runtime, relay, inject DLL, avatarctl and kh2ctl
from the same source cohort. Incompatible mappings fail their version checks;
an older DLL already injected into KH2 cannot validate the new runtime.

The normal co-op path is local-primary avatar/puppet and host-native world
replication. The relay defaults to native traffic; `--simulate` enables the
legacy synthetic test mode. The runtime's `--legacy-replica` path is also opt-in.
Steam uses the in-game broker and Valve relays without a separate ENet server.
See [runtime](../runtime/README.md), [server](../server/README.md),
[forced resync](FORCED_RESYNC.md), [HP ordering](HP_ORDERING.md) and
[desync reports](DESYNC_REPORTS.md) for their respective behavior and limits.
A host `world-resync` queue receipt alone is not native recovery success.

The player launcher's environment defaults differ from direct developer
launches. Its `child_environment` enables enemy mirror/population/all-target,
Normal difficulty, friendly fire, event hold, downed and revive prompts, while
filtering developer switches and keeping general population spawning and legacy
targeting parked. Explicit inherited values for enabled flags override those
defaults. See [the plan](ONLINE_COOP_PLAN.md#player-launch-defaults-reviewed-2026-10-20)
and `tools/launcher/friend_package.py` for scope and profiles.

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

For offline queries against the saved `build/ghidra/kh2_full` project, use
`scripts/ghidra.ps1` without `-Setup`. Coordinate one headless query at a time:
on2026-10-03, concurrent `-noanalysis -readOnly` readers still produced
`LockException: Unable to lock project`, despite the helper's parallel-safe
comment. Wait for the query owner's actual handle to become terminal before
retrying; do not clear its lock or kill its process. Independent saved-PE byte
reads/dumpbin need no project lock. Inspect retained output for Ghidra errors:
a surrounding PowerShell output pipeline can report exit0 after a failed query.

### Runtime writer lease (2026-10-07)

WorldBridge 12 adds runtime PID at byte 116 and GetTickCount heartbeat at byte 52,
using reserved words; the 128-byte header and ring offsets stay unchanged. Both
DLL and runtime must use v12; earlier versions are rejected. The runtime's world
pump renews the lease, including its inner wait loop. No background watchdog
conceals a stalled pump. A heartbeat age of 5000 ms remains live; greater ages
expire. Unsigned subtraction handles the 32-bit clock wrap. Timestamp 0 is valid;
PID 0 means no writer. Missing writer on an armed generation fails closed.

Expiry retires that raw generation permanently in the DLL. Native frame handling
clears stored party intents/kits and ordered world authority, releases client warp
authority and drains stale packets without writing runtime-owned headers or ring
indices. Current actors are not rewritten: the next native load restores the
party. The loading hook independently checks freshness and its seqlock-published
plan generation before member writes, so a loading/menu interval cannot apply a
stale plan before the next frame. Puppet and world authority also require a lease.

Derived generation 0 still holds stored plans during ordering/delivery flicker;
it is not an expiry signal. A raw generation change prevents an old cached plan
from applying even while the derived generation is0. Graceful disconnected
clients may retain stored intents until rebase; this change does not redefine
that policy. An expired raw generation cannot revive from a resumed heartbeat,
a new PID alone or queued reset. Recovery requires a fresh heartbeat, a newer
nonzero raw generation and its matching ordered reset/delivery before new world
work is admitted. Generation comparisons assume no reader lags by 2^31 session
boundaries. A runtime stalled over 5 seconds is intentionally treated like a dead writer. A writer PID change within the same observed raw generation also retires that binding; heartbeat publication cannot renew an older writer's plan before the new reset.
