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

## Combat pilot: Reversals, Thorn's danger bands, openings and keep-away (VUH-2617)

**Evidence.**
- **r1** (attempts 02–04): Thorn fell from 390 to about 220 HP in the first minute, then stalled while both
  seats sat at 1–5 HP until a full party down.
- **Attempt05 with r1** (`fd2b0b7`): the same stall at 212, with an analysis the logs now allow.
  - **Thorn's motion is a phase, not an attack.** The pilot's read matches the native trace: an opening
    sequence 0 → 204 → 205 → 206 → 226 → 207 → 208 → 209 → 210 → 211, where Thorn loses most of its HP
    (210 alone cost it 112 and 77), then **202 for about two minutes**.
  - **All seat HP was lost inside 202.** Joining the HP drops to the native `[thorn-trace] canhit` rows by
    game frame gives two attacks:
    - **448** (melee), around 202's motionTime 160;
    - **1041** (a projectile), from motionTime ~390 on, where 202 holds near its last frame. It hit at any
      distance, 1800–2000 units included. It caused 4 of the host's 7 drops (the whole block-01 collapse)
      and 5 of the friend's 6.
  - **Reversals were mostly missed.** 345/426/432/666/667 (432 is the projectile's,
    `docs/TWILIGHT_THORN.md`) were completed in 33 of about 118 offers. The host's hit rows show 432 on
    screen with nothing pressed.
  - **r1's keep-away `hold` at 1600–2000 units was still hit.**
- **Attempt06 with r3/r4** (`9fb8bf0`) and **Leg A attempt02** (solo host). The joins are retained in
  `.local/review/vuh-2617-thorn-pilot/r5-analysis/`.
  - **Reversal-first didn't reduce 1041 hits:** host 6 of 9 drops, friend 6 of 9. Host-owned 432 was
    completed 33/71, about the same rate as before.
  - **345's windows last only about 4–30 frames, in bursts:** 0/80 completed solo, about 1–15% elsewhere.
    That's shorter than the pilot's read-and-press latency.
  - **r3's ranged-band strafe cost all chip damage:** Thorn sat flat at 227 for 100 s. It didn't avoid 1041
    either; the hits landed at 1544–1983 units while strafing.
  - **Solo, the opening dealt only 45, and nothing after.** The host died at about 124 s to 447 and to 631
    bursts (kind 4, multi-hit) while 345 was on screen. So **a solo pilot kill isn't a realistic Leg A
    acceptance step**; Leg A proves the carry-in and the sit-out instead.
  - **Bot01's co-op kill** (an older aggressive pilot): opening 390 → 237, chip 237 → 128 over about two
    minutes while the friend stayed healthy, then 128 → 0 in about 10 s.

**Behavior (the `fight` step's opt-in `thornCombat`, both Thorn fight routes).** The base `pilot.py`
`combat_turn` hook never takes a turn. Priority order, each per seat and from its own reads:
1. **Reversal.** A Reversal on screen is pressed at once, at most every `reversalGapSec` (0.2 s), even when
   the base's reaction gap (0.35 s) has not elapsed. Logged as `thorn_reversal`. The Thorn pad owns this one
   cadence for every Reversal press, the base reaction branch's included: a Reversal tap within the gap of the
   last one is dropped (r4, Quill r3). Other reactions, the event RCs 297/298/299 and held Triangle (revive,
   the cooperative RC) stay the base's and are never gated.
2. **Openings.** In a seeded opening (209/210/211), or a motion that has cost Thorn `openingLearn` (30) HP in
   the current visit, the seat approaches and swings a three-hit burst. There is no dodge, keep-away or
   pinned escape; a reaction or a downing breaks the burst.
3. **Near bands: one sidestep.** Danger is keyed by (motion, motionTime band of `bandFrames` 64).
   - It is seeded from attempt05 (202 entry, 204, 226, and 448's band 2) and learned in the fight: at least
     `learn` (2) own-HP drops in at least `learnRatio` (0.34) of the band's visits, also counting the band
     before if Thorn changed band within 0.5 s.
   - On entering a near band within `radius` (650), the seat sidesteps across Thorn's line with a moving
     jump, once per visit, with a 1.2 s cooldown. Logged as `thorn_dodge`.
4. **Ranged bands: fight through them (r5).** A band is ranged if any of its hits came from beyond `radius`
   (seeded: 202's bands 6–7, the projectile stance). It's learned and logged, but the turn goes to the base
   fight, which approaches and attacks. r3/r4's strafe through these bands is withdrawn (see attempt06 above).
5. **Keep-away at 25%, for the lower seat only.** At ≤ `kiteBelow` (25%), with the partner alive and ≥ 4 HP
   healthier, the seat strafes on the 750 ring (or further out, if already there), `strafeRad` (0.6) further
   around each turn, flipping every 2 s, never holding still. The 4 HP margin decides it: at own 1 against a
   partner at 5, the lower seat keeps away; seats within 3 HP of each other both keep chipping.
   A downed partner still takes the revive first. Logged as `thorn_kite`.

These stay from r1–r2:
- the revive-approach detour (out of revive range and outside an RC only), logged as `coop_detour`;
- `pinnedDrop` 0.55.

**Telemetry.** `thorn_observe` carries `thornCombat`, which closes in `thorn_combat_summary`:
- Thorn's HP, motion, motionTime, band and root;
- band hits (`near`/`ranged`) and visits;
- the learned bands (`motion:band:kind`) and openings;
- Thorn's HP lost per motion (`dealtByMotion`);
- counters (`reversals`, `dodges`, `kites`, `commits`, `hitsNear`, `hitsRanged`).

**Offline only.** Tests: `python -B tests/test_pilot_thorn.py`, plus `test_pilot_fight.py` and
`test_pilot_pad.py`.

**Unproven:**
- whether fighting through 202 again restores bot01's chip damage without losing the party sooner;
- 345 stays out of the pilot's reach. A blind Triangle cadence could catch it, but it was deferred because
  its native effect is unproven.

The pilot can't see the projectile itself; it reads enemies and Thorn only. The m11 friend freeze, not the
pilot, ended attempt06.

**Measure on the next live run:**
- Thorn's HP per block, and the chip damage in 202 (`dealtByMotion`);
- Reversal offers against completions per command and owner (`[script-rc]` offer/complete);
- each seat's HP in 202, and `thorn_kite` time;
- whether Thorn returns to an opening or a late phase after 202.

A Thorn kill is the success test.

## Acceptance still open

Bots05 is an experimental regression candidate. Shared victory, rewards,
cleanup, both seats' onward movement, native damage and reaction checks all
remain mandatory. Qualify at least two of three consecutive runs on the same
pinned cohort. Natural revival is proved only when a downing actually occurs.
Retain each report and a captioned two-screen kill clip labeled single-PC
loopback; paired 30-second blocks have encoding gaps. The scenario changes no
native game code, wire or protected saves. Original exact28/human14 seals remain
historical and intact.
