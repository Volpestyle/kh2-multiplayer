# Retarget a sealed single-PC fixture to PC2

PC2 runs self-contained packets, without updating or building its checkout.
The first two-instance smoke passed on 2026-10-09 UTC: run
`pc2-smoke-20261009-01`, report
`scenarios/20261008-213945_two_instances_same_room_1/report.json` in its evidence
directory. Both instances reached GoA, moved and produced renderer captures.
Save/app-ID inventories matched; native closure verified no KH2, owned driver
or descendants, and neither lock remained. PC1 and PC2 both used Steam Global
buildid `15194255`. This qualifies the lane, not other fixtures or networking.

## D15 desktop readiness (VUH-2339)

**Design only; not installed or qualified.** `bld` belongs to another person
(`ccroc`). The lead requires James's explicit OK before installing a physical
input observer, a persistent user task or a reclaim gate there. Until that
decision, keep `bld` read-only: no bridge starts, queued jobs, Steam starts or
account changes. The earlier account-B consent does not authorize monitoring
someone's everyday desktop. James also owns the account-B access decision.

### Read-only inventory, 2026-10-17 06:30–06:32 UTC

- Explorer was in Session 1; the SSH commands were in Session 0. No Steam,
  KH2, official launcher or desktop PowerShell/Python bridge was observed.
  Steam `ActiveUser=0`, `pid=0`: the signed-in-B gate currently refuses.
  Account B appears to be the desktop user's account; do not sign anyone out
  or assume the historical install owner proves current authentication.
- The historical queue is `%USERPROFILE%/desk/jobs` (`*.job`), with results
  under `desk/done` and `desk/agent.log`. The queue was empty; the log's last
  completion was 2026-10-08 00:08:18, in its original local timestamps.
  `C:/desk` and the usual `desk/agent.ps1` source paths were absent on PC2.
  Task Scheduler COM showed no root-folder task matching `desk` or `kh2`;
  the CIM query was denied to this standard user.
- The preserved wrapper is `%USERPROFILE%/lead-inbox/rev11-start-bridge.ps1`.
  It calls the historical
  `C:/Users/volpe/repos/kh2-multiplayer/build/rig/steam-confirm-20261007-01/live-step2-rev11/pc2/pc2-bridge.ps1`.
  That source exists and matches its old verifier's SHA-256. The wrapper
  requires Session 2, while Explorer is now in Session 1. Its stop file is
  checked only **after the running job finishes**. It cannot establish D15's
  two-second reclaim behavior. Neither wrapper nor verifier was executed.
- Two installed `KH2-Co-op` directories were checked beneath
  `%USERPROFILE%/KH2-package-join27-20261009T124156Z-b5354662-friend/` and
  `%USERPROFILE%/KH2-causal-20261012T173655Z-f96e8c3e-friend/`. All 1,684 and
  1,694 manifest-listed files respectively matched their hashes, including
  the five native products. Both contain `tools/launcher/friend.py` and
  `python/python.exe`; both default to Steam. They are historical protocol
  10 / AvatarBridge 3 and protocol 17 / AvatarBridge 4 packages. Main at
  `1e5e94b` uses protocol 22 / AvatarBridge 4; the packaging product recipe
  still pins protocol 18. Select a current, matching cohort for both seats
  before a D15 run. These checks cover only the two named directories, not
  every package on the machine, and did not execute packaged code.
- KH2 is installed under Steam's
  `steamapps/common/KINGDOM HEARTS -HD 1.5+2.5 ReMIX-/`. Its EXE matches the
  supported Steam Global allowlist and app `2552430` reports build `15194255`.
  The separate `%USERPROFILE%/dev/kh2-multiplayer` checkout is at `7a37f9b`;
  it was not updated. Both documented PC2 rig-lock paths were absent.
  None of these observations proves the person is away or grants a live lane.

Local evidence: `build/rig/vuh2339-readiness-20261017/`. Keep raw account/path
inventories local; post the sanitized result on
[VUH-2339](https://linear.app/vuhlp/issue/VUH-2339). No save folder was read
or written, and no desktop input, launch, installation or persistent change
was made during this inventory.

### Proposed launch and reclaim gate

After explicit authorization, use one removable, unelevated interactive-user
task and one exclusive desktop-driver lease per PC. A task name is not proof
of ownership: record its action, user, driver PID and native creation time.
Refuse an existing driver or lease; never replace someone else's bridge or
task. Reuse `pc2_session` to bind the driver to a fresh active-console session,
and additionally require an unlocked input desktop. SSH Session 0,
session changes, unavailable observations and stale observer heartbeats refuse.

Before every launch, require all of the following together:

- At least 15 minutes since physical input, measured in the console session.
  `GetLastInputInfo` alone is insufficient because it also counts injected
  input. A candidate implementation uses keyboard/mouse low-level hook flags
  to distinguish injected events; it records only idle duration and health,
  never keys, text, pointer positions, titles or screenshots. On startup,
  reboot, observer failure or session change, earn a new 15-minute interval;
  do not reconstruct physical idle time from an earlier generic idle reading.
  Unattributed input must conservatively reclaim the desk.
- No foreign game or fullscreen application, and no existing KH2 or official
  launcher, regardless of whether an old registry calls it owned. An absent
  process is not evidence of physical idleness.
- Steam already running and authenticated as the explicitly approved B on
  PC2, A on PC1. A remembered login or app-manifest `LastOwner` is insufficient.
  Mismatch or sign-out refuses; the agent never starts a login flow.
- The shared live lane is available. On PC2, check both existing lock paths
  described below before and after acquiring the desktop lease. On PC1,
  coordinate with the saves/pilot owner and check the canonical live-lane lock;
  a clean worktree does not make the physical PC available. Keep live-lane
  ownership with the scenario/runner rather than acquiring its lock twice.

One driver sends all agent OS keyboard input to its exact owned game window;
other bridge dispatch and launcher paths must pass the same admission and
lease. D15 gameplay has no warp, teleport or memory-based movement commands.
Recheck admission immediately before launch, and keep reclaim cancellation
active during boot and long-running jobs as well as gameplay.

On physical return, stop dispatch and cancel queued/held agent input without
moving focus. Close only this run's retained PID/creation identities through
canonical `kh2ctl kill`; verify exit and remove only the matching lease bytes.
Never kill a foreign game or close Steam. The watchdog must operate independently
of the input runner; a synchronous job cannot delay reclaim. Missing health or
an uncertain cleanup stops further input and retains the lease for inspection.
The two-second requirement covers input cessation, owned-game exit and lease
release together; it remains unproven until measured in a physical reclaim test.

### Readiness checklist after owner authorization

- [ ] Record permission for monitoring/persistence, its duration and removal;
  confirm account-B use without changing anyone's account.
- [ ] Implement the common PC1/PC2 gate and route every agent desktop entry
  through its single-driver lease, without taking over existing lanes.
- [ ] Verify default-user task installation/removal and fresh console binding.
- [ ] Show refusal for recent physical input, foreign game/fullscreen app,
  existing KH2/launcher, wrong/signed-out Steam, occupied lane and lost health.
- [ ] Show injected agent input does not reset physical idle, and a real
  physical reclaim stops input, closes only the owned game and releases the
  lease within two seconds. Synthetic callbacks alone do not prove this.
- [ ] Collect 48 hours of authorized availability observations, including gaps
  and restarts. Retain coarse idle/eligibility intervals and refusal categories,
  without raw input or account data. Propose run windows from that coverage;
  no schedule is supported by the current point-in-time inventory.
- [ ] Verify the selected matching player package on both PCs, then attach
  the log, reclaim result and proposed schedule to VUH-2339 for lead closure.

These are ordinary harness changes under D14, without sealed-packet ceremony.
Existing sealed historical packets remain untouched. Inject/save-adjacent
changes and milestone acceptance retain their separate rigor requirements.

## Prepare a new revision offline

1. Coordinate with the fixture owner. Copy its scenario, helpers and exact
   products into a new packet; retain the original packet and evidence. Record
   the original pins and every deliberate adapter change. Preserve its gameplay
   assertions and save/memory authorization boundaries.
2. Carry the friend ZIP's `bin/` products and bundled Python. Copy the full
   diagnostic kh2ctl separately when the scenario needs commands unavailable
   in the portable CLI, after qualifying its native ownership registry as below.
   Copy any fixture-specific DLL into the packet and pin
   it; never pin mutable `build/` outputs as runtime dependencies.
3. Use portable kh2ctl for launch, instances and owned kill, with its unpacked
   package as cwd and an explicit pinned DLL. Launch from the default real
   Steam game directory; check game hash/build separately. The copied full
   CLI may have an embedded PC1 source root: use it only for reviewed commands
   with explicit PID/output paths, never its launch, restart or ownership state.
   Explicit PID is insufficient for `poke`: it also checks PID and creation time
   against its own `RepoRoot()/build/rig/owned.txt`. Worldmap01 failed at its
   first poke on 2026-10-09 UTC because portable launch and full diagnostic CLI
   resolved different registries; no setup bytes were applied. Memory fixtures
   need a reviewed internal diagnostic product with package-root semantics and
   a limited command allowlist, sharing the launcher's native ownership registry.
   Preserve native ownership checks and ordinary friend-package restrictions;
   never copy registry entries into a checkout or substitute ad-hoc writes.
   Build `kh2ctl_diagnostic` from the reviewed source worktree and use
   `tools/packaging/add_rig_diagnostic.py` to add it to a new rig ZIP. Run
   `bin/kh2ctl_diagnostic.exe` with unpacked-package cwd and explicit owned PID;
   verify its bytes against the packet product pin and package manifest before
   dispatch. It retains a native PID/creation/image handle for each command and
   keeps the existing `poke` ownership check. The ordinary friend CLI and full
   checkout CLI retain their existing command boundaries. Source products remain
   copied unchanged; the adapter selects this additional diagnostic product.
4. Derive home from `USERPROFILE`, packet from `__file__`/`PSScriptRoot`, and
   evidence/unpack from run context. Resolve Documents through the Known Folder
   API, using the target account's native save container. Never import saves or
   change app-ID to make a fixture run. Reuse the no-recall inventory helpers.
5. The ZIP's Python is isolated by `python311._pth`. Add packet, tools and
   dependency directories explicitly. Carry matching-ABI dependencies such as
   Pillow, pin them, and test with the actual bundled interpreter. A system
   Python pass does not establish package portability. The `_pth` file also
   drops the script's own directory from `sys.path`, so every child script must
   add its package-relative import roots from `__file__`. Causal05 failed at the
   friend's `console_diag.py` on 2026-10-10 with `ModuleNotFoundError:
   system_sessions` after the entry itself passed. Before sealing, replay the
   full host and friend Setup from a copied unpack with the bundled `-I`
   interpreter and no checkout on the path, running every spawned child up to
   game launch with native, game and network leaves stubbed.
6. Reuse the guarded canonical `BoundBoot` load path: highlighted LOAD row,
   recognized save-list OCR before confirmation, bounded deadline and no prompt
   dismissal. Keep all scenario steps inside the fixture's reviewed scope.

## Serialize, seal and stage

Desktop session numbers change across reboots. For every future PC2 packet,
including JOIN friend roles and HB native-sync adapters, bind the expected
active-console session in fresh consent instead of hardcoding Session 2.
Read `WTSGetActiveConsoleSessionId` at admission and immediately before each
game launch. Require a nonzero integer session, reject `0xffffffff` (no active
console), and require the actual driver process session from the native snapshot
to equal both consent and active console. A mismatch refuses before launch;
never launch from SSH Session 0 or silently select another session. Verify a
logged-in user, Steam signed in, and no KH2/official launcher separately. Existing
sealed packets retain their original checks and require a new reviewed revision
when their session no longer matches. Smoke06 refused after the 2026-10-09 reboot
changed PC2's active console to Session 1; smoke07 introduces this runtime binding.

The tracked implementation is `tools/rig/pc2_session/`: `binding.py` preserves
Smoke07's exact consent/console/driver predicates and `system_sessions.py`
preserves the native census used by acceptance04. Packet adapters call
`require_current_session(consent['session'])` at admission and immediately before
each launch. It takes a fresh census and console reading every time. Package
these files together; do not replace this with a cached admission result.
Offline checks: `python -m tools.rig.pc2_session.controls`.

For owned renderer clips, use the separate `kh2ctl_clip` product and
`tools/rig/pc2_clip` adapter; see its README for build, pinned encoder and receipt
contracts. `tools/packaging/add_rig_clip.py` adds the explicit internal capability
to a fresh verified package without changing existing profiles. Arrival acceptance
still requires source-owned causal proof, full media replay and independent
closure of every canonical helper, encoder and verifier identity.

PC2's shared lock is `%USERPROFILE%/lead-outbox/kh2-rig/rig.lock`. Acquire by
exclusive create before setup/launch; any existing lock refuses, with no stale
takeover. Bind the JSON to driver PID/native creation FILETIME, run ID, packet
pins, lane and UTC time. Hold through verified owned closure and after checks;
remove only matching retained owner bytes. If closure is uncertain, retain it.

While any JOIN revision still uses the checkout's `build/rig/rig.lock`, refuse
that lock too, both before and after shared-lock acquisition. Obtain a fresh
run/pins-bound JOIN-owner receipt reserving the window and confirming no running
or unspent staged JOIN. Spent JOIN evidence may remain. JOIN17+ adopts the shared
lock in new packets; existing sealed packets remain intact.

Validate hashes, scenario structure, script syntax/BOM bytes and bundled Python
imports offline. Seal `pins.json` PENDING, obtain independent review, then have
the lead write the actual ADOPT receipt and a same-UTC-day consent record. Use a
seal-specific exclusive-create spent marker: one attempt per sealed packet.

Consent records James's standing availability; it is not a per-run question.
James says when PC2 is free and when he reclaims it. While it is free, the lead
writes consent for each reviewed packet from that standing statement and the
run proceeds on review approval, without asking him first. A reclaim stops the
run immediately and ends the availability until he frees PC2 again.

Copied offline controls must accept explicit packet/scratch paths or resolve
their new location correctly. A fixed `parents[2]` root that worked under
`.local/pc2/` resolves to `build/` when copied into a packet, breaking direct
replay. Verify the retained control entry from its final location before sealing;
the production adapter's separate `--verify` is not a replayability check.

Create the PC2 destination lane before `scp -r`; otherwise scp can copy contents
without the expected nested packet directory. Copy the actual ADOPT receipt,
consent and JOIN release. Verify every pin and authorization on PC2 and fetch a
fresh readiness receipt before GO. Send the worker a brief file; remote Herdr
prompts can lose multiline text. Supply every mandatory script parameter and
use `powershell -NonInteractive`. Write UTF-8 and keep operator logs outside
the sealed packet.

Preserve the scenario's primary error before asserting that two launches were
recorded. A first-launch startup failure can leave no returned owned instance;
report its boot error, then recover native ownership from the portable registry
for closure. The launch-count check still applies to the exact intended terminal.

## Finish and release

Retain launch ownership even when the helper times out before returning JSON:
the fresh portable unpack's `build/rig/owned.txt` records PID/creation identities.
Close only matching owned instances through kh2ctl. Require a passing scenario,
requested captures, unchanged native save/app-ID inventory, and verified process
exit. Independently check no KH2 or owned driver/descendants and both locks absent
before releasing the window to the JOIN owner. PC2's standard account can deny
`Get-CimInstance`; reuse the native `system_sessions.py` snapshot instead.

Preserve packet, unpack and evidence on failure; archive by moving and never
retry the seal. SSH commands can linger after remote completion. Fetch the
receipt separately; SFTP batch `ls/get` works without rerunning the remote job.
Poll the known `result.json` directly with scp/SFTP. A status SSH command that
prints only the started receipt can stall before reading later files, even when
the result already exists; its partial output does not establish that a run is
still active.
Keep raw account/path inventories and logs local; publish sanitized reports and
game-only renderer captures.
