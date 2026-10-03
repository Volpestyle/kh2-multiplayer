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
| `kh2ctl` | `instance args` or `argsExpr` | Run the supplied argument array against that instance. `argsExpr` evaluates an expression to an array, for explicit reversible fixture writes |
| `transition_check` | `as after instances target timeoutMs capture` | Wait for completed-load parity, relay ACKs, and restored puppet slots; save structured evidence under `as` (default `transition`). `after` names an earlier checkpoint whose epoch must advance; `target` is the expected `[world, room]`. Default instances are `[0,1,2]`, default timeout 30 s. On timeout, capture every participating instance and preserve the last observation as `<as>_failed.json`. `capture: true` also captures successful checkpoints |

| `progress_snapshot` | `as instances compare` | Save actual allow-listed progress and complete personal byte ranges to JSON. Optional `compare` names a prior snapshot and emits each changed SAVE offset and before/after byte |
| `statehash_check` | `as instances minEnemies timeoutMs` | Require fresh post-arrival hashes on all peers at the same completed epoch and exact six-field location, nonempty native enemy rows, zero unmatched actors, and matching netID/object/HP populations. Save evidence or a failure artifact |
| `dismiss_goa_map_reward` | `as instance` | With flag409 open in GoA, capture the reward popup and poll the unchanged native safe gate; while unsafe, send cross150ms and wait700ms, bounded to10s. Save every gate sample, press count and failure capture |
| `approach_goa_chest` | `as instance` | In GoA on the central platform, walk toward flag409 using camera-basis projections and 200 ms native pulses. Stop within 100 units, require progress over each three-pulse window, fail after 15 s with a trace/capture |
| `courtyard_diagnostic` | `as instance baseline timeoutMs` | Replay the original courtyard client pulse/combo after a positive hash baseline; capture player/camera/spawn bindings and require a persistent extra unmatched actor plus enemy-desync. No reproduction fails explicitly as INCONCLUSIVE; a room/epoch change invalidates the diagnostic |
| `enemy_hash_control` | `as instance` | Separate native enemy fault: temporarily set one matched client's per-actor HP-lock bit, kill its host counterpart through the native hit path, require failed client death and two fresh native hash disagreements with relay enemy-desync, and restore only that bit if actor identity still matches |
| `progress_hash_control` | `as instance minEnemies timeoutMs` | Outside GoA, after a successful progress apply, toggle only chest flag 409 on the selected rig client, verify readback and fresh relay `fields=4` evidence, then restore and verify the original byte in `finally` |

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
working with captures and structured evidence. `chests(i)` resolves native `F_*`
actors through their bounded `actor+0xC00` treasure pointer, validates the
record against the current world/room, and returns native actor state with
its treasure/item IDs, overall completion flag, SAVE byte and opened bit.
It requires the CLI's `peek.moduleBase` metadata; invalid reads fail explicitly.

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

The transition route evidence above covers transition and puppet behavior;
it does not establish enemy/progress hash acceptance. The applied-state
publisher and the additional nonempty fixture below now provide that check,
but require a passing live report from the current build. Cutscene
hold/resume remains separate.

## Progress and applied-state hash fixtures

- `progress_chest_goa_probe`: one instance boots, enters `04/1A`, captures
  the room and records all actors, native treasure records and flag 409.
  This is exploratory evidence, with no memory write or chest interaction.
- `progress_chest_goa_native_open`: native camera-aware approach and reaction
  press, capture/dismiss the obtained-map popup with native confirm, and wait
  for normal gameplay before checking settled/opened state. This calibration
  fixture requires an initially closed chest and performs no memory writes.
- `net_progress_chest_goa`: three-instance chest acceptance. It clears only
  flag409 in memory outside GoA, verifies closed native initialization, opens
  the host chest through movement/reaction input, checks client next-load
  application, late-join current state and subsequent reload persistence.
  Each client apply must log unchanged personal bytes around allowed writes.
- `net_statehash_enemy_negative`: separate negative fixture using verified
  actor `+0x9B8` bit `0x4` (native ApplyStatDelta HP lock) on one client enemy,
  followed by a native host kill. It requires a FAILED client death log,
  actual surviving positive HP, two distinct fresh hash frame sets and
  relay DesyncEnemies. It restores only the original bit by read-modify-write
  while address/object/status identity still matches, then reloads through
  the host and requires nonempty parity. No shared object descriptor is
  modified. Failure remains failure; it cannot pass from a fabricated packet
  or an attempted mutation alone.
  The current fixture explicitly targets object ID311. Its prior object309
  run [`20261002-185625_net_statehash_enemy_negative_1`](../build/scenarios/20261002-185625_net_statehash_enemy_negative_1/report.json)
  correctly failed: client1 retained native HP153 after a failed mirrored
  death and produced relay `fields=2`, but client2 independently acquired an
  unmatched object309 actor at HP160. Unaffected-peer parity remains strict.
  The control restored flags1159 to1155; that useful negative observation
  does not turn the full failed run into acceptance. Targeting311 is an
  explicit fixture choice, recorded in control evidence, not a population
  filter in the hash checker.
  The three-peer311 attempt also failed in
  [`20261002-190415_net_statehash_enemy_negative_1`](../build/scenarios/20261002-190415_net_statehash_enemy_negative_1/report.json):
  client2 again gained an independent unmatched309/HP160 while the controlled
  client1 target remained alive. Both failed three-peer runs and their
  strict checker remain intact.
- `net_statehash_enemy_negative_isolated` boots exactly two peers (host and
  client1) to isolate the detector control for object311. The checker uses
  every instance in that run, requires two fresh actual native mismatches
  and relay enemy-desync, restores the guarded HP-lock bit and reloads to
  require nonempty parity. This is explicitly two-peer detector evidence;
  it neither replaces the three-peer positive hash proof nor resolves the
  independently observed third-peer spawn divergence.
  The isolated live run
  [`20261002-192119_net_statehash_enemy_negative_isolated_1`](../build/scenarios/20261002-192119_net_statehash_enemy_negative_isolated_1/report.json)
  **failed overall** (134.9s) at its post-reload transition checkpoint.
  Its earlier negative checkpoint passed: two full native observations
  (host/client frames3643/1228 and3703/1289) retained client netID5/object311
  at HP153 while the host counterpart was dead, the native client death log
  reported FAILED153?153, and the relay reported peer1 `fields=2`. Guarded
  restoration verified flags1155?1159?1155.
  Reload reached completed epoch2 and matching full locations, but both
  peers had an active logical puppet with native friend pointers `[0,0]`,
  no clone candidates and only the local actor. The strict native-puppet
  check correctly failed; the later restored nonempty hash check was not
  reached. Thus detector and flag-restoration checkpoints are proven within
  a failed run, while native-puppet reload recovery and the separate
  three-peer extra-spawn divergence remain unresolved. The fixture and
  checks remain unchanged; this run is not represented as acceptance.
- `net_statehash_nonempty`: extends the established `12/0B` battle-program-1
  two-wave fixture. Three peers must agree before and after host damage,
  with at least four living native enemies. It then toggles only client 1's
  in-memory GoA chest flag 409, requires a fresh relay notice with exactly
  `fields=4`, restores the byte and requires fresh agreement again. The
  original wave scenario and known courtyard failure are preserved.

The chest reward popup freezes native gameplay and holds animation `0x99`
(153); bit409 changes before it is dismissed. The fixtures capture the popup,
send bounded native confirmation pulses, then wait for unfrozen gameplay, a live field, menu
`0xFF`, native event state `exe+0xB65210 == 0` and event context
`exe+0x2A11478 == 0`. The retained timer is diagnostic. This preserves the native safe-state gate.
Do not interpret the popup animation as the reloaded opened chest state.
The confirmation pulses are a bounded calibration action, not proof that
confirm is what closes the popup: offline native tracing indicates an
animation/resource-driven lifetime. Gate samples also record UI tick delta
at `0x717484` to distinguish stalled timing from input readiness.
Calibration [`20261002-190126_progress_chest_goa_native_open_1`](../build/scenarios/20261002-190126_progress_chest_goa_native_open_1/report.json)
failed the strict safe-state wait even after the popup disappeared and normal
HUD/gameplay returned: frozen=0, inField=1, menu=255, but the sampled timer
retained90. This is evidence to investigate whether the timer represents
active playback or retained elapsed time; the fixture does not mask it.
Dismissal failures preserve native chest/actor state and gate samples even
while the gate rejects traversal. The runner now uses the bounded native event-state/context predicate above;
the single-instance calibration additionally leaves and reloads GoA to
require the native opened initialization motion `0x98`. Full mirrored-chest
acceptance uses the verified calibration below.

Corrected-gate single-instance calibration passed in
[`20261002-191026_progress_chest_goa_native_open_1`](../build/scenarios/20261002-191026_progress_chest_goa_native_open_1/report.json)
(66.3s). During the popup, native event state3 and nonzero context blocked
traversal with timer45. After2.906s, state/context were both zero while the
timer retained90; native leave-and-return warps succeeded and GameBridge
reported no active cutscene. The natively opened host chest used animation
153 with flag409 set. After native room reload it initialized to animation
152, while the other two closed chests stayed151. The full fixture therefore
requires immediate host153 and reloaded client/late-join152, not one animation
value across both lifecycle phases. Native opened-bit proof comes from the
reaction input, and no memory write changed the event state or timer.

Three-instance chest acceptance passed in
[`20261002-191345_net_progress_chest_goa_1`](../build/scenarios/20261002-191345_net_progress_chest_goa_1/report.json)
(189.3s). Native host opening changed flag409 and progress version1 to2;
the immediate snapshot showed only the host chest byte changed, while both
client bytes stayed closed until their allowed application boundary. Client1
then reloaded GoA at completed epoch3 with opened initialization motion152.
Client2 joined at that existing epoch, applied version2 and also initialized
the chest open. A subsequent all-peer reload advanced epoch4 with the exact
six-field location `[4,26,0,0,0,0]` and opened motion152 on all three peers.
Each transition checkpoint also retained relay acknowledgements and native
puppet checks; captures show next-load, late-join and final reload states.

Client1 applied versions1,2,2,2 and client2 applied2,2. Every immediate
around-write personal comparison reported before/after hash `124AA108` and
`personal_unchanged=1`. Full endpoint snapshots independently showed zero
changed bytes in characters, inventory, munny and EXP on all three peers.
The shared chest difference was exactly SAVE+0x23DF, byte0 to2, on each peer;
client2 also recorded a visited-room difference. All four on-disk
save hashes were unchanged. This proves native chest progress, boundary
application, late-join freshness and personal exclusions for this fixture;
it does not assert that every native story-setter side effect is mirrored.

The hash checkpoint starts a fresh observation window when called; old
matching logs cannot satisfy it. Every hash must follow the latest native
arrival, match its completed epoch and all six current NOW fields, and
include the complete raw row set associated by epoch/frame. It recomputes
`KHE1` FNV-1a from the sorted positive-HP `(netId, objectId, hp)` records and
compares the result with the published hash. Positive checks require zero
unmatched rows and identical populations across all three peers. Dead rows
remain in the artifact, and negative controls retain unmatched rows rather
than filtering them out. An empty room cannot satisfy the default minimum.

`progress_snapshot` records actual bytes and SHA-256 for programs
`SAVE+0x10/0x1C80`, story `+0x1C90/0x260`, visited `+0x22F8/0x98`, chests
`+0x23AC/0x34`, and these complete personal exclusions: characters
`+0x24F0/0xE04`, inventory `+0x3580/0x140`, munny `+0x2440/4`, EXP
`+0x36E0/4`. A comparison reports each differing SAVE offset, old byte and
new byte. Native room initialization may legitimately change personal
state; these snapshots do not assert whole-range equality across loads.
The narrower apply invariant comes from `[progresssync] apply` logs:
`personal_before`, `personal_after`, and `personal_unchanged=1` compare
personal bytes immediately around only the allowed writes, before native
room initialization.

Live probe on 2026-10-02:
[`20261002-182909_progress_chest_goa_probe_1`](../build/scenarios/20261002-182909_progress_chest_goa_probe_1/report.json)
passed. Installed native `F_EX040_HB` (object 2540) resolves to treasure
585, item 592, room index 100, event `0x101E`, overall flag 409. Its closed
motion is `0x97`, at `(0,460,900)`, and `RVA 0x9ABC8F & 2` was zero.
The other two GoA records resolve to flags 410 and 411. This confirms those
installed rows; broader chest-range coverage still relies on the bounded
native/table evidence in `build/rig/progress-ranges.md`. Native host opening and mirrored open initialization are established by the
three-instance acceptance run below.

Applied-state hash acceptance passed live in
[`20261002-184220_net_statehash_nonempty_1`](../build/scenarios/20261002-184220_net_statehash_nonempty_1/report.json)
(180.2 s). All three peers had completed epoch 1 and exact location
`[18,11,0,0,1,0]`, four native matched enemies (netIDs 3?6, object IDs
309/311), zero unmatched actors, and identical recomputed enemy hashes.
Native HP changed from 160 to 153 after host damage, with fresh agreement.
Toggling client1's actual flag409 byte from 0 to 2 after applied version 1
changed only its progress hash and produced relay `fields=4`; restoring 0
restored three-peer hash agreement. The full before/after snapshots had
zero changed bytes in every captured region on every peer, and all four
save-file hashes stayed unchanged. This run proves the hash and reversible
progress-control behavior; its later `second_wave` observation was empty
and is not additional two-wave-spawn evidence.

## Native enemy census diagnostic

The strict 20-load acceptance failed at `hash_transition_08` in
[`20261002-193320_net_host_transitions_statehash_acceptance_1`](../build/scenarios/20261002-193320_net_host_transitions_statehash_acceptance_1/report.json):
completed epoch 10, location `[5,6,0,1,1,0]`, published host count 0 versus
five unmatched object302/HP20 rows on each client, with equal progress hashes.
Those published counts came from the update-hook census; they were not an
independent complete native-list observation.

`net_enemy_census_transition08.json` retains that route through transition08,
then collects an immediate snapshot and two more after 1.5-second intervals
before retaining the original strict hash check. `native_enemy_census` reads
peers concurrently, with a 30-second bound per peer/sample. Its artifact always
retains errors and partial observations. Successful collection proves neither
peer parity nor enemy absence by itself.

The helper treats `entities` as candidates only. Checked reads validate native
HEAD/TAIL, every next handle and its region-table resolution, terminal handle0,
object descriptors including the `F_` prefix, status and actual HP, and stable
identity/location/arrival/lifecycle bookends during safe gameplay. Cycles, cap
hits, unreadable pointers and changing loads remain incomplete. Hash coverage
comparisons require the latest post-arrival hash to match epoch/full location
and complete raw records/recomputed canonical hash; unavailable comparisons
retain raw logs without a coverage conclusion.

The cache probe accepts only four inline buckets rooted at RVA `2AE5E60`,
reads all 256 u16 record IDs twice, and brackets active pointer/counter reads.
Record IDs are not object IDs. Controller/record shape checks and bounded
ordinary controller-table observations are separate causal evidence: script
entries are skipped, table cap64 is diagnostic rather than a proven native
limit, and incomplete provenance does not erase complete list evidence.
Controller stage/cooldown, tracked activation/player positions, raw spawn
records and table/key membership help distinguish activation history. These
observations remain read-only and do not establish every producer's semantics.

Live collection succeeded in
[`20261002-200856_net_enemy_census_transition08_1`](../build/scenarios/20261002-200856_net_enemy_census_transition08_1/report.json),
but the retained strict parity check made the overall scenario **FAIL**
(208.9 s). All nine peer snapshots were complete with valid hash comparisons.
The first snapshot had no native enemies and empty caches everywhere; the
later two had zero host enemies and five actual client enemies, matching each
peer's published rows. Type-2 controller header30 had flags2 on the host and
flags10 on clients; client caches gained record IDs `11,12,13,14,18` while the
host cache stayed empty, and activation positions diverged. This establishes
native spawn divergence in this reproduction, not a hidden host update-hook
population. Initially equal caches do not support retained cache history alone
as its cause. See [native findings and inference limits](ENEMY_PARITY.md).
All four disk saves remained unchanged.

The separate ignored party-availability probe
[`20261002-194936_net_party_availability_probe_1`](../build/scenarios/20261002-194936_net_party_availability_probe_1/report.json)
passed its diagnostic checks: both native friend pointers were already zero on
first native `12/0B` entry before networking and remained zero after epoch1/2;
world18 party word stayed `0x12121200` and all four disk saves were unchanged.
This narrows the earlier isolated negative-control recovery failure; it does
not turn that failed recovery into a pass or establish a network regression.

## Checked native-list coverage regressions

The checked native-list coverage change passed the Release DLL build and
the [native-wave regression](../build/scenarios/20261002-201456_net_enemy_sync_waves_1/report.json)
(153.6 s), [nonempty hash/progress control](../build/scenarios/20261002-201730_net_statehash_nonempty_1/report.json)
(166.1 s), and [focused native-death detector](../build/scenarios/20261002-202126_net_native_census_death_control_1/report.json)
(102.4 s). All four disk saves were unchanged in both suites. The wave fixture
recorded two real second-wave enemies and six native client deaths; the
nonempty fixture's later empty-wave sample is not extra wave evidence.

`net_native_census_death_control.json` preserves the isolated negative fixture
through its failed native lethal/detection/guarded bit-restoration checkpoint
and ends there. It verifies the changed lethal/census path independently of
the known unsupported battle-room native-puppet reload. It does not close
that original recovery failure or the courtyard spawn-authority gap.

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
