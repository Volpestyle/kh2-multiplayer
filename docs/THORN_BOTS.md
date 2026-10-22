# Twilight Thorn bot run

VUH-2617 uses `tools/scenario/run_thorn.py` to select the separate
`tools/rig/route_pilot/thorn.py` entry over the existing scenario engine and pad
pilot. `net_twilight_thorn_bots05.json` includes the Calling combat and door
routes. There is one input driver per seat, and no OS focus/input is required.
Use matched DLL/runtime/relay products built and pinned from current main.

The pilots approach a downed partner in 3D, yield to actionable boss reactions,
hold Triangle while native revive progress advances, and release when the native
bar completes. They remain neutral while logically downed. The strict Calling10
check precedes the door re-anchor. Damage and native-attack predicates are saved
at the original combat observation points and asserted after the onward checks;
later events cannot repair a false saved result.

## Run order and install detector

Ivo owns the live cohort. Run this sequence inside the existing Session1 desk
queue/governor, with the usual rig ownership, protected-save and cleanup checks.
Do not start a game from Session0 or while another live run owns the desk.

1. Capture a **fresh** SHA256 manifest of every file under the actual Steam
   install. Store it under this checkout's `.local`, outside the install.
2. Run bots05. Let the existing runner finish owned process/media cleanup.
3. Capture the install again, including when the scenario fails.
4. Diff the two manifests. Any changed, added or removed file, unreadable file,
   unstable capture or invalid manifest stops the cohort. Report the receipts to
   the lead; do not start another run or restore anything yourself.

Example body for an already authorized desk job (choose a fresh run directory):

```powershell
$install = 'C:/Program Files (x86)/Steam/steamapps/common/KINGDOM HEARTS -HD 1.5+2.5 ReMIX-'
$evidence = '.local/thorn/bots05-attempt01'
python -B tools/rig/install_manifest.py capture --root $install --out "$evidence/install-before.json"
if ($LASTEXITCODE -ne 0) { throw 'STOP: install baseline refused' }
$runExit = 1
try {
    python -B tools/scenario/run_thorn.py tools/scenario/scenarios/net_twilight_thorn_bots05.json
    $runExit = $LASTEXITCODE
} finally {
    python -B tools/rig/install_manifest.py capture --root $install --out "$evidence/install-after.json"
    if ($LASTEXITCODE -ne 0) { throw 'STOP: install post-capture refused; report to lead' }
    python -B tools/rig/install_manifest.py diff --before "$evidence/install-before.json" --after "$evidence/install-after.json" --out "$evidence/install-diff.json"
    if ($LASTEXITCODE -ne 0) { throw 'STOP: install comparison failed; no further runs; report to lead' }
}
if ($runExit -ne 0) { throw "Scenario failed ($runExit); retain its report" }
```

The helper opens install files read-only, inventories all files recursively, and
refuses reparse points or files that change during hashing. Receipts are written
exclusively under `.local`; existing receipts cannot be overwritten. `diff`
exits 0 for a content match, 1 for a mismatch and 2 for an error/refusal. It records
the actual file count, including any local extras; Steam's October22 verify
checked 536 depot files. This detects net content changes between captures and
does not identify the writer or detect a write restored between captures.

The previous twelve-movie baseline already contained altered `lop.mp4`; keep it
as historical evidence. The final no-game FileIO qualification passed, but its
old live wrapper binds bots04/exact35 and that baseline. Bots05 uses the full
install detector above; no old wrapper or old movie baseline is reused.

## Acceptance still open

Bots05 is an experimental regression candidate. Shared victory, rewards,
cleanup, both seats' onward movement, native damage and reaction checks all
remain mandatory. Qualify at least two of three consecutive runs on the same
pinned cohort. Natural revival is proved only when a downing actually occurs.
Retain each report and a captioned two-screen kill clip labeled single-PC
loopback; paired 30-second blocks have encoding gaps. The scenario changes no
native game code, wire or protected saves. Original exact28/human14 seals remain
historical and intact.
