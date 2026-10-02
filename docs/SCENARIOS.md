# Scenarios (VUH-1488)

A scenario is a JSON script that the runner plays against rig-launched KH2
instances: launch → load the save → warp → inputs → waits → memory
assertions → captures. Every run writes a report with its artifacts. A crash
or hang ends the run with a bundle instead of stalling it. Scenarios are the
regression suite for live behavior, and their reports are the evidence an
issue closes with.

```powershell
python tools/scenario/run.py tools/scenario/scenarios/boot_to_goa.json
python tools/scenario/run.py tools/scenario/scenarios/*.json --repeat 5
python tools/scenario/run.py tools/scenario/scenarios/net_host_transitions_smoke.json --validate
```

Requires the DLL and `kh2ctl` built (`cmake --build build --config Release`)
and the desktop session (KH2 crashes at startup from session 0).

`--validate` only parses JSON, checks supported steps, required fields and
instance references, and compiles expression syntax. It does not evaluate
expressions, acquire the rig lock, inspect processes or saves, or launch
anything. It does not establish live behavior or validate arbitrary CLI args.

## What a run does

1. **Takes the rig lock** (`build/rig/rig.lock`, holder pid and command).
   One live lane at a time: a second runner exits with code 3. A lock left by
   a dead process is stale and taken over.
2. **Refuses if a KH2 the rig didn't launch is running** (exit 3): James may be
   playing. It never touches that process.
3. **Hashes the save folder** (SHA-256 of every file under
   `My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX\`, read-only) before and after the
   whole suite. Any change other than Steam's own `steam_autocloud.vdf` fails
   the suite (`saveViolation`).
4. Runs each scenario's steps in order; the first failing step ends that run.
   While a step runs, every instance is watched:
   - process exited → **CRASH**: the bundle has the DLL's minidump
     (`kh2coop_crash_<pid>.dmp`, written by its unhandled-exception filter)
     and the inject log;
   - gameplay frames stalled for 30 s in the field → **HANG**: the runner
     writes an outside minidump (`kh2ctl dump`) and kills the instance.
5. Kills every instance the run launched, pass or fail, and moves on to the
   next scenario.

Output goes to `build/scenarios/<stamp>_<name>_<attempt>/` (`report.json`,
`report.md`, captures, clips, logs, bundle). `build/scenarios/last_suite.json`
holds the suite summary. Exit codes: 0 all passed, 1 a scenario failed or the
save changed, 2 a scenario crashed or hung, 3 rig unavailable.

## Save safety

Two independent layers:

- **The DLL's save guard** (`inject/src/SaveGuard.cpp`) is installed first
  at init and covers everything under the save folder from inside the game.
  - **Write opens are redirected.** A write-capable open through KernelBase
    `CreateFileW/A` or `CreateFile2` goes to the same relative path under
    `build/rig/logs/save_sandbox_<pid>/`. If the open keeps contents
    (`r+b`), the original is copied in first. After that, reads of that file
    go to the sandbox too, so the game sees what it "saved".
  - **Why redirect rather than deny:** the save writer doesn't check
    `_wfopen_s` for NULL before libpng, so a denied open would likely crash
    the game mid-save (repos-60's static read).
  - **Everything else is denied:** copies (`CopyFileA/W/ExW/2`), moves
    (`MoveFileExA/W`, `MoveFileWithProgressW`), `ReplaceFileW`, deletes
    (`DeleteFileA/W`) and `RemoveDirectoryA/W`. Copy goes to NtCreateFile
    without passing CreateFileW, so it's hooked by name. If no sandbox can be
    created, write opens are denied too (fail closed).
  - **Logging:** every operation is logged as
    `[saveguard] redirected|blocked …`.
  - **Self-test:** setting `KH2COOP_SAVEGUARD_TEST_DIR` before
    `kh2ctl launch` guards that directory too and runs a self-test at init.
    It plants a probe file there through the unhooked API, then tries every
    path against it. That includes `_wfopen_s` called through the exe's own
    import thunk (RVA `0x471BC8`), first `r+b` (which must read the original),
    then `wb`, and the `DeleteFileA` the exe imports.
  - **Result on 2026-10-02:** 0 of 14 calls reached the probe. Opens were
    redirected, and `r+b` read the original. Everything else was denied.
- **How the game writes saves** (headless Ghidra, 2026-10-02):
  - The save path comes from the string
    `KINGDOM HEARTS HD 1.5+2.5 ReMIX\Steam\KHIIFM_WW.png` (RVA `0x7164E0`).
  - `FUN_1400fef80` opens the save container read-only (CRT
    `_sopen_dispatch`). If the file reads as empty, it calls `DeleteFileA` on
    it.
  - `FUN_140100810` creates the folder and checks free space.
  - The PNG writer `FUN_140145170` opens the file with ucrtbase
    `_wfopen_s(path, L"r+b")`, then `L"wb"`. Both reach KernelBase
    `CreateFileW` with write access, which the guard redirects.
  - The game changes its working directory (`_chdir` in `FUN_140101000`), so
    the guard resolves relative paths to full paths before matching.
- **The suite's before/after hash**, which catches anything the guard misses.

Never save in-game anyway.

## Writing a scenario

```json
{
  "name": "sora_move_attack",
  "description": "What it shows, in one sentence.",
  "protect": [0],
  "steps": [
    {"do": "boot"},
    {"do": "warp", "world": 5, "room": 6},
    {"do": "save", "as": "start", "expr": "pos()"},
    {"do": "input", "ly": 1, "ms": 900},
    {"do": "assert", "expr": "dist(pos(), saved['start']) > 300", "message": "Sora didn't move"},
    {"do": "press", "button": "cross", "times": 20, "gapMs": 300},
    {"do": "capture", "name": "after_combo"}
  ]
}
```

`"instance": N` (default 0) on a step picks the instance; `boot`/`launch`
append a new one. `"protect": [N]` keeps that instance's Sora on team 0 (no
attack hits him) for the whole run, so combat rooms don't end in a game over.

| Step | Fields | Does |
|---|---|---|
| `boot` | `timeoutSec` | Launch + inject, mute, pick LOAD on the title menu (checked by pixel in captures), load the save list's default slot, wait until the room is live. Menu presses go through the DLL's input collector (`player-press`), so no window focus is needed |
| `launch` | `mute` | Launch + inject only (stays on the title) |
| `warp` | `world room door map btl evt` | `kh2ctl warp`; returns once the room has loaded |
| `input` | `lx ly rx ry ms` | Hold sticks (`player-input`) |
| `align_courtyard_exit` | `instance as blockedExpr` | BC courtyard entrance-0 fixture only: pulse screen-left (`lx=-1`) for 250 ms at a time, recording each start/end position. Stop at `abs(x)<60` or a true `blockedExpr`, with a 6 s limit. Each unblocked pulse must reduce `abs(x)`; otherwise capture and fail with a diagnostic JSON. No host movement |
| `press` | `button ms times gapMs` | Press a button (`player-press`; PS2 names: `cross`, `circle`, `triangle`, `square`, `start`, …) |
| `wait` | `ms` | Sleep while watching the instances |
| `wait_until` | `expr timeoutMs pollMs` | Poll an expression until true |
| `assert` | `expr message` | Fail the run unless the expression is true |
| `save` | `as expr` | Store a value in `saved[...]` |
| `protect` | `on` | Start/stop Sora protection mid-run |
| `capture` | `name` | PNG from inside the renderer → artifact |
| `clip` | `name seconds` | MP4 → artifact |
| `crash`, `freeze` | | Runner self-tests: fault or suspend the instance |
| `relay` | `port build content mod args` | Start `kh2coop_server` on loopback for this run; the version gate defaults to what the runtime sends |
| `runtime` | `instance role peerId link args expect timeoutMs` | Start `kh2coop_runtime_scaffold --network --no-camera --pid <instance>` and wait for `expect` in its log (default `connected to server`). `link: {latencyMs, jitterMs, lossPct}` sets the runtime's impairment both ways (owner→viewer crosses two runtimes, so 50 ms each = 100 ms) |
| `record` / `record_stop` | `as instances` | Sample every party actor's position (with the instance's world/room) on those instances in the background; `record_stop` writes `<as>.csv` |
| `wander` | `instances seconds seed` | Seeded random stick walks with occasional jumps, taking turns (soaks) |
| `hit_all` | `instance op amount` | `kh2ctl hit damage/kill` on every live combat enemy of that instance (host-only damage tests) |
| `kh2ctl` | `instance args` | Run the supplied argument array against that instance |
| `transition_check` | `as after instances target timeoutMs capture` | Wait for completed-load parity, relay ACKs, and restored puppet slots; save structured evidence under `as` (default `transition`). `after` names an earlier checkpoint whose epoch must advance; `target` is the expected `[world, room]`. Default instances are `[0,1,2]`, default timeout 30 s. On timeout, capture every participating instance and preserve the last observation as `<as>_failed.json`. `capture: true` also captures successful checkpoints |

Processes started by `relay`/`runtime` are stopped at the end of the run.

Expressions are Python with these helpers (the instance index is the last
argument, default 0): `room()` → `(world, room)`; `pos(name='P_EX100')` →
`(x, y, z)`; `actor(name)` → the `kh2ctl entities` record (`hp`, `maxHp`,
`team`, `motionId`, `address`, …); `actors()`; `enemies()` (objentry type 3/4);
`peek(rva, kind='u32')`; `log_count(text)` (lines in the inject log, e.g.
`'attacker=P_EX100'` from the hit log); `dist(a, b)`; `saved`;
`enemy_hps(i)` (sorted `(name, hp)` of live combat enemies); `bridge(i)` (the instance's AvatarBridge via `avatarctl peek`: local
frames/s and both puppet slots); `puppet_error(saved['rec'], owner, viewer)`.

`location(i)` returns `[world, room, door, map, btl, evt]` from one NOW sample;
`log_matches(pattern, i)` returns named regex captures from the inject log;
`runtime_log(i)` returns the runtime log. `int` and `str` are available when
working with captures and structured evidence.

`puppet_error` scores how well the viewer's puppet follows the owner's
Sora in a recording:
- Each sample from a viewer actor (other than the viewer's own Sora) is
  compared with the owner's positions over the preceding 0.5 s, because the
  puppet renders behind on purpose. The best-matching actor counts as the
  puppet.
- It returns `mean`, `p95` and `max` error in KH2 units (100 = 1 m), the
  fitted `lagMs`, and `ownerTravel`.
- Check `ownerTravel` too. If the owner barely moved, a low error proves
  nothing.
- Only samples taken while owner and viewer are in the same world/room
  count. Puppets are matched by actor name, since addresses repeat across
  rooms.

`jitter_stats(saved['rec'], owner, viewer, err['actor'])` counts "pops": puppet
steps between consecutive samples that exceed the owner's largest step over
the last 0.5 s by more than 30 units. Steps only count while the owner has
been in the viewer's room for at least 1.5 s.
`tools/scenario/spikes/pops.py RUN_DIR OWNER VIEWER NAME` lists each pop
with its context. The sampling runs at about 20–40 Hz per instance, so
single-frame jitter needs a clip.

## Example scenarios

`tools/scenario/scenarios/`:

- `boot_to_goa`: load the save, warp to the GoA, Sora moves.
- `sora_move_attack`: BC courtyard, run in, combo until Sora's hits land in
  the hit log.
- `two_instances_same_room`: two instances load and warp to the GoA; both
  are there and both Soras move.
- VUH-1492 networking (relay on loopback, one runtime per instance):
  - `net_two_instances`, `net_two_instances_impaired` (100 ms + 2% loss),
    `net_three_instances`: every puppet follows its owner.
  - `net_room_hide`: a puppet hides while its owner is in another room.
  - `net_mismatch`: a mismatched peer is refused with a reason.
  - `net_viewer_leaves`: the viewer changes rooms with a puppet active.
  - `net_soak_10min` + `soak_control_no_network`: the 10-minute soak and its
    no-network control.
- VUH-1502 enemy sync (host + 2 clients): `net_enemy_sync_courtyard`,
  `net_enemy_sync_waves`.
- VUH-1496 host-led transitions: `net_host_transitions_smoke`,
  `net_host_transitions_acceptance`, and the calibrated supplementary
  `net_host_transitions_native_exit` fixture (details below).
- `forced_crash`, `forced_hang`: runner self-tests. They're expected to
  report CRASH/HANG with a bundle; don't include them in a pass/fail suite.

## Host-led transition evidence (VUH-1496)

Run the smoke first, then the route, from the desktop live lane:

```powershell
python tools/scenario/run.py tools/scenario/scenarios/net_host_transitions_smoke.json
python tools/scenario/run.py tools/scenario/scenarios/net_host_transitions_acceptance.json
```

Both fixtures boot three games before starting the runtimes. The host starts
in GoA, client 1 starts in a different room, and client 2 connects only after
the host and client 1 have completed another transition. The late join must
arrive and acknowledge the existing host epoch. Session slots must equal
instance indices: start the `player`, `friend1`, and `friend2` runtimes in that
order. Boot uses the save list's default slot, so the room programs and party
availability still depend on that save.

The acceptance route then performs 20 host `warp` requests across five rooms
(`04/1A`, `05/01`, `05/04`, `05/05`, `05/06`). All three Soras are protected
in these fixtures. Each checkpoint requires:

- A new completed-load epoch on all three instances, not just matching IDs.
- Exact equality of world, room, entrance, map, battle and event programs
  between every current NOW sample and its arrival log, and between peers.
- A successful `TransitionAck` for each client's current epoch and room in
  the relay log.
- A live local avatar, both active puppet poses with the expected distinct
  remote owners and room, and both native friend actors within 100 game
  units of their published poses. The latter is a settled-position check;
  continuous motion fidelity remains covered by `net_three_instances`.

Native actors are resolved against the active entity list. Companion targets
come from the two unit-slot-1 actor pointers (`0x2A239B0`, `0x2A239B8`). With
non-head player-class actors present, the DLL selects Sora clones instead;
the checker takes the actual per-slot addresses from `[puppet N] ... actor=`
motion-driver logs after the latest `[warp] load complete` marker. Names or
list ordering do not establish clone ownership. Both targets must be distinct,
nonzero, different from the local actor and present in the current entity
list. Reports retain the pointers, resolution source, candidate clone
addresses and resolved actors. A default `kh2ctl state` friend record is
insufficient: it can contain a zero position when no friend entity exists.

The first live smoke on 2026-10-02 reached matching epochs, locations and ACKs
in Twilight Town `02/00` but failed puppet-position checks. Its report remains
under `build/scenarios/20261002-142449_net_host_transitions_smoke_1/`. The
replacement route avoids that party-capacity fixture and avoids BC `05/02`,
which can open a cutscene. The passing replacement runs below establish two
native companion targets in every route room for the current save; missing
native puppets continue to fail. The late
join checkpoint captures a successful sample on every instance, and every
failed checkpoint attempts captures before writing its diagnostic snapshot.

**Passing live evidence, 2026-10-02 (WorldBridge v4).**

| Scenario | Report | Result |
|---|---|---|
| Smoke | [20261002-143623](../build/scenarios/20261002-143623_net_host_transitions_smoke_1/report.json) | PASS, 139.7 s. Initial host-follow, late join at the existing epoch, and a same-room reload with all three instances and restored native puppet targets. |
| Acceptance route | [20261002-143907](../build/scenarios/20261002-143907_net_host_transitions_acceptance_1/report.json) | PASS, 195.8 s. Twenty route loads at epochs 3–22 across all five rooms; every checkpoint has both client ACKs and six native puppet-position checks. Largest settled error: 37.57 units (limit 100). |
| Native walking exits | [20261002-145121](../build/scenarios/20261002-145121_net_host_transitions_native_exit_1/report.json) | PASS, 153.1 s. A client request for `05/00` was blocked at epoch 3; the host's native walking loads advanced through epochs 4–6 with both client ACKs and a final all-instance puppet check. |

All three runs checked four save-file hashes with no changes or save violations.
These fixtures used native Donald/Goofy companions, not replacement Sora
models. The final two acceptance loads had the exact location
`[5, 1, 0, 0, 0, 20]` with different completed-load epochs. Each report retains
the complete per-instance location, owner, native actor and ACK evidence;
the late join and end checkpoints have screenshots of all three games.

The last two loads have the identical six-field location. Their epochs must
still advance. This exercises native request/load hooks via host CLI warps;
it does not prove a naturally triggered same-room script reload. The smoke
includes the same reload check with two route loads.

Arrival logs come from the native load lifecycle. Static tracing found that
`RequestTransition` (`0x152990`) stages the packet, commit (`0x152BE0`) writes
the entrance as one byte at NOW+2 and resolves programs, and the load task
(`0x152680`) schedules completion (`0x152CD0`) after the loaders. Completion
sets `IN_FIELD` and runs finalizers; the direct equivalent `0x152F40` is also
hooked. The request and both completion hooks check 24 executable bytes
before installation. Client acknowledgement waits for completion return,
the gameplay gate and all six fields to match. A pause/stall alone is not
load completion evidence. These implementation facts still require the
live scenarios to establish behavior on the running build.

`net_host_transitions_native_exit` is a supplementary walking fixture,
**calibrated live on 2026-10-02**. At BC courtyard entrance 0, client 1 first aligns toward the
doorway centerline using 250 ms screen-left pulses (`lx=-1`). Each pulse
records its start/end position and must reduce `abs(x)`; a wrong direction
or obstruction captures and fails. Alignment stops at `abs(x)<60`, after a
new blocked native request, or fails with a capture at the 6 s bound. The
blocked-request baseline is recorded before alignment. The client then
holds backward for 4 s; the host is untouched until its own backward pulse.
It requires a new `[warp] client native exit blocked` log and then unchanged
full locations and host epoch with restored puppets. Staying put without
an intercepted native request fails. The fixture records both starting
positions and captures the client immediately after its walking pulse, before
waiting for the blocked request, so a missed doorway leaves visual evidence.
It derives the destination from the first new blocked request, then gives
the untouched host the same 4-second backward pulse without an intervening
warp. The host must reach that world/room in a newer completed-load epoch;
all clients must arrive, acknowledge and restore their puppets. The successful
host-exit checkpoint captures every instance. It never substitutes a client
CLI warp for walking. The passing run aligned client X from -101.878 to
-25.278 in two pulses, then intercepted `05/00` with unresolved programs
`0xFFFF`. All instances stayed at `[5,6,0,1,1,0]`, epoch 3, after that client
attempt, with zero settled puppet error. The host's held walking pulse crossed
the doorway three times (`05/00` → `05/06` → `05/00`); both clients acknowledged
epochs 4, 5 and 6. The final checkpoint had `[5,0,0,0,1,5]` on all three games,
and maximum settled puppet error 14.14 units. Intermediate native-load puppet
positions were not sampled by that checkpoint. A natural same-room script
reload remains separate.

The first walking attempt (`20261002-144249_net_host_transitions_native_exit_1`)
missed the castle doorway: client start `(-450.7,-260,1950)` reached the wall
at `(-535.8,-260,2045)` without a native request. The captures show the
lateral offset; the host started near center at `(48.5,-260,1950)`. This is
why alignment checks measured progress rather than assuming every client's
spawn remains centered. The subsequent passing run and its per-pulse samples
are retained in the table above.

No current live `StateHash` publisher provides meaningful shared enemy or
progress hashes to these fixtures. Empty rooms cannot prove enemy parity;
location equality cannot prove mirrored story state. Passing the route
therefore covers the transition/puppet portion of VUH-1496, **not** full P2
acceptance. Meaningful hashes of actual local enemy state and verified
progress still need implementation and calibration before closing VUH-1496's
hash requirement; cutscene hold/resume is separate.

## Known limits

- `boot` loads whatever slot the save list opens on (the last used one) and
  doesn't pick a slot. The pixel checks assume 1920×1080.
- The hang check needs the room to be live: it doesn't cover the title
  screen or a load that never finishes (those surface as step timeouts).
- Not proven live: a save attempted from the in-game save menu. The menu
  runs as a scheduled task (`0x1512B0`), so there's no clean call to open it.
  The guard is proven against every API on the game's save path (static
  trace above, plus the self-test through the exe's own imports). A real
  in-game save attempt waits for James's approval.
