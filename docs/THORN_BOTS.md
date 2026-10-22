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

## Combat pilot r1: dodge, keep-away and revive detour (VUH-2617)

**Why.** In bots05 attempts 02–04, Thorn fell from 390 to about 220 HP inside the first minute, mostly in
the large drops of the reaction sequences. Then it stalled (178–192 at the end) while both seats sat at 1–5
HP, often 1000–1800 units away, until a full party down. Ivo's read of the clips and `pilot.jsonl`: the
pilots soaked hits with no guard or dodge, and the revive approach had no fallback.

**What changed.** The `fight` step's opt-in `thornCombat` block is set in both Thorn fight routes. The base
`pilot.py` gains one hook, `combat_turn`, that never takes a turn, so every other route behaves the same.

- **Dodge on telegraphs.**
  - A Thorn motion counts as a telegraph if it is seeded: 202 (`atk_ground`, aimed at Thorn's target),
    and 204 and 226 (the native motion attacks that hit the friend in bot02).
  - It can also be learned during the fight: at least `learn` (2) of this seat's own HP drops while Thorn
    was in that motion and within 1.5×`radius`, in at least `learnRatio` (0.34) of its instances.
  - Early in such a motion (`motionTime` ≤ `earlyFrames`, 45) and within `radius` (650), the seat
    sidesteps across Thorn's line with a moving jump. The step is `dodgeStep` (400), slightly outward,
    alternating sides. It dodges once per motion instance, with a `dodgeCooldownSec` (1.2) cooldown.
  - It is logged as `thorn_dodge`.
- **Keep-away when low.** At or below `kiteBelow` (25%, 5 of Roxas's 20 HP), with the partner alive and
  at least `kiteMargin` (4) HP healthier, the seat holds the `kiteRadius` (750) ring around Thorn instead of
  attacking. That way one seat survives to revive the other, instead of both going down (D17's full-party
  game over).
  - It stops when the partner is downed (the revive takes priority), recovers, or the seat recovers.
  - Reactions are still taken first.
  - This changes the earlier "native 25%-revived seats keep attacking" rule only when the partner is
    healthier.
  - It is logged as `thorn_kite`.
- **Revive-approach fallback.** When the approach to a downed partner gains under 40 horizontal units in
  2 s, the seat detours to a waypoint beside the line, on the side away from Thorn's root, for 1 s with a
  moving jump. At most 8 times per downed episode. It is logged as `coop_detour`. It applies only out of
  revive range and outside an RC. Standing still while the native bar fills is not a stall, and entering
  range restarts the 2 s window.
- **Fewer run-aways.** `pinnedDrop` 0.55: escaping now needs 11 HP lost in 2 s, where before one or two
  Thorn hits triggered a run-and-jump away.
- **Telemetry.** `thorn_observe` adds `thornCombat`:
  - Thorn's HP, motion, motion time and root;
  - the learned telegraphs;
  - per-motion own-hit and instance counts;
  - **Thorn's HP lost per motion** (`dealtByMotion`), which marks the openings worth pressing;
  - counters.

  `thorn_combat_summary` closes each fight.

**Not done.** No Potion (no evidence this save has any), no direct use of openings yet (only measured), and
no change to reactions, which already fire. Offline: `python -B tests/test_pilot_thorn.py`, along with the
existing `test_pilot_fight.py` and `test_pilot_pad.py`.

**Measure on the next live run:**
- Thorn's HP per block against both seats' HP;
- dodge count and the HP lost within 1 s after a dodge;
- the learned set;
- kite time;
- detour count and whether the revive then landed;
- `dealtByMotion` (which motions are worth attacking).

Success is a Thorn kill, not these counts.

## Acceptance still open

Bots05 is an experimental regression candidate. Shared victory, rewards,
cleanup, both seats' onward movement, native damage and reaction checks all
remain mandatory. Qualify at least two of three consecutive runs on the same
pinned cohort. Natural revival is proved only when a downing actually occurs.
Retain each report and a captioned two-screen kill clip labeled single-PC
loopback; paired 30-second blocks have encoding gaps. The scenario changes no
native game code, wire or protected saves. Original exact28/human14 seals remain
historical and intact.
