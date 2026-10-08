# Development Workflow

How to build, test, and iterate on the inject DLL against a running KH2 instance.

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
[012155](../build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined6-result.md).

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

Relay cached world records now require complete exact frames and the current
nonzero room epoch; HP/death also require its current manifest. The world-cache
receipt in `build/rig/world_cache_epoch_offline_receipt_20261003.json` records
separate current checks; the earlier reconnect zip stays frozen. The protocol 9
candidate adds offline-accepted bounded native record-content qualification to
the fresh host-native resync transaction introduced in protocol 8. Its unchanged
89-step local living Bootstrap passed on 2026-10-04; absent-pack reconnect
recovery remains open ([current witness scope](FORCED_RESYNC.md#native-record-content-candidate-2026-10-04)). Do not remove the
fresh-actor anti-refill guard to make a reload inherit old host deaths: checked
creation/lifecycle evidence and a same-point alive-refill negative control are
required. Automatic desync reporting now transfers bounded actual peer artifacts
under a frozen session/connection roster and preserves partial results. Shared
CaptureChannel caller leases serialize automatic and CLI capture/clip requests;
outstanding timed-out sequences still block reuse. See `DESYNC_REPORTS.md` for
registration, output roots, capture witnesses and collection limits.

Protocol **7** sequences absolute enemy HP at the DLL publication point and
rejects older same-room samples before relay caching or client/native mutation.
Reliable equal-sequence cache replay remains supported; see `HP_ORDERING.md`.
That ordering result alone does not prove native convergence; the protocol 8
transaction adds separate source cuts and per-target delivery fences.

The current source uses protocol **10**, AvatarBridge **3**,
WorldBridge **12** and CaptureChannel **1**. AvatarBridge v3 appends the bounded
HUD roster-label slot; mixed v2/v3 mappings are refused. The reviewed name patch
has a matched private Release DLL/runtime/avatarctl build, but no live names
acceptance or deployment. Protocol 10 retains the fresh-observation requirement
for automatic resync. Runtime, relay, inject DLL and avatarctl must be rebuilt
together. Shared mappings with incompatible versions fail their version checks;
the changed WorldBridge layout rejects older mappings. An already injected
older DLL is not current validation. Existing protocol-v3 native receipts stay
historical. AvatarState telemetry/recording layout and the v3 claim shape are
unchanged. V5 introduced opaque world-incarnation identity and typed closure
reasons; v6 adds diagnostic request/chunk/done messages and a third ENet channel.
WorldBridge 12 retains captured generation/delivery/source context and uses a
separate bounded CAS operator mailbox, preserving the DLL ring's single producer.
Protocol 9 adds full record witnesses (native coverage255); generic synthetic
coverage127 is rejected by the native consumer. Exact content qualification and
write fences do not prove controller lifetime or creation authority. Historical
protocol-8 native results retain their checked scope and do not validate v9.
Cancellation immediately unarms native world authority; lazy attachment retains
bounded post-cut continuation after the validated bootstrap. The relay defaults
to native traffic; `--simulate` opts into legacy simulation, which cannot satisfy
native capture. The CLI's host `world-resync` queue receipt is not native success.
Friend rejoin retries at most five times within 60 seconds, after
1/2/4/8/8-second delays, with separate four-second transport and roster
deadlines. Initial failure and Player loss stop networking; host loss ends the
session. Ten uninterrupted seconds of verified membership reset the retry
budget, without asserting native bootstrap readiness. Avatar restoration,
native world recovery and progress/warp behavior still need a native run.

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
