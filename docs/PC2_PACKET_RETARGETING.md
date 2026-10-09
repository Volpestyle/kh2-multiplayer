# Retarget a sealed single-PC fixture to PC2

PC2 runs self-contained packets, without updating or building its checkout.
The first two-instance smoke passed on 2026-10-09 UTC: run
`pc2-smoke-20261009-01`, report
`scenarios/20261008-213945_two_instances_same_room_1/report.json` in its evidence
directory. Both instances reached GoA, moved and produced renderer captures.
Save/app-ID inventories matched; native closure verified no KH2, owned driver
or descendants, and neither lock remained. PC1 and PC2 both used Steam Global
buildid `15194255`. This qualifies the lane, not other fixtures or networking.

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
4. Derive home from `USERPROFILE`, packet from `__file__`/`PSScriptRoot`, and
   evidence/unpack from run context. Resolve Documents through the Known Folder
   API, using the target account's native save container. Never import saves or
   change app-ID to make a fixture run. Reuse the no-recall inventory helpers.
5. The ZIP's Python is isolated by `python311._pth`. Add packet, tools and
   dependency directories explicitly. Carry matching-ABI dependencies such as
   Pillow, pin them, and test with the actual bundled interpreter. A system
   Python pass does not establish package portability.
6. Reuse the guarded canonical `BoundBoot` load path: highlighted LOAD row,
   recognized save-list OCR before confirmation, bounded deadline and no prompt
   dismissal. Keep all scenario steps inside the fixture's reviewed scope.

## Serialize, seal and stage

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
the lead write the actual ADOPT receipt and fresh same-UTC-day consent. Use a
seal-specific exclusive-create spent marker: one attempt per sealed packet.

Create the PC2 destination lane before `scp -r`; otherwise scp can copy contents
without the expected nested packet directory. Copy the actual ADOPT receipt,
consent and JOIN release. Verify every pin and authorization on PC2 and fetch a
fresh readiness receipt before GO. Send the worker a brief file; remote Herdr
prompts can lose multiline text. Supply every mandatory script parameter and
use `powershell -NonInteractive`. Write UTF-8 and keep operator logs outside
the sealed packet.

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
Keep raw account/path inventories and logs local; publish sanitized reports and
game-only renderer captures.
