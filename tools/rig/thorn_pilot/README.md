# Thorn pilot stack (VUH-2617)

This is the Twilight Thorn bot pilot behind the VUH-2617 kill-rate runs zr98–110: the s254b react stack plus the
host's opt-in Creeper-first targeting. Before this it lived as a lane copy (`.local/bot01-next/route_pilot/`). It was
landed unchanged under lead #289/#290: Sage approved the Creeper-first r3 packet `vuh2617-creeper-first-review-r3.zip`
(sha256 `49d5c246…`), and the inherited modules are the ones listed in that review's dependency zip.

It is separate from `tools/rig/route_pilot/`. `pilot.py` here is main's `tools/rig/route_pilot/pilot.py` at
`5dd231f`, and `thorn.py` is bot01's (sha256 `7260f9da…`). Every module puts its own directory first on `sys.path`, so
this stack never imports the route_pilot copies. Main's scenario engine runs it by pointing `runner.PILOT` at
`thorn_creeper.py`.

## Run

```
python -B tools/rig/thorn_pilot/run_creeper.py tools/rig/thorn_pilot/scenarios/net_twilight_thorn_bots_dmgtrace600s254b_trace_react_creeper.json [--validate]
```

- Only the host's fight route (`routes/twilight_thorn_host_fight_creeper.json`) carries `creeperFirst`. Without it
  the stack is thorn_react299g, as in zr98–107.
- The friend's fight route is `routes/twilight_thorn_fight.json`.
- The walk-in routes are main's `tools/rig/route_pilot/routes/` (identical content to the lane copies).
- Live runs follow `docs/THORN_BOTS.md` and the kh2-rig skill (desk bridge, PC1 gate).

## Layers (outermost first)

- `CooperativeFight.step`: Creeper → RcGate → ReactLog299 → Survival254b → Reserve → ApproachRevision → PreRevive →
  HeadCooperative → `thorn.CooperativeFight` (revive core) → `pilot.CooperativeFight`.
- `Pilot`: Creeper → Survival254b → Reserve → LockRetry → `thorn_next` → DeferredRepair → Heal2 → Head → `thorn` →
  `pilot`.
- `Pad`: Survival254b → Reserve → `thorn_next` → DeferredRepair → Heal2 → HeadPad → `thorn.Pad` → `pilot.PadInput`.

Creeper-first rules are in `creeper_policy.py` (pure) and their execution is in `thorn_creeper.py`. The priority
gate, the engagement bound and the cooldown are described in the review packet's README.

## No throwback Reversals while rescuing (lead #291)

`thorn_rescue.py` sits over `thorn_creeper.py` and is run with `run_rescue.py` and the `_creeper_rescue` scenario.

- **Why.** In zr98–110 the revive core pressed every offered RC on the way to a downed partner. A completed 432/426
  from within 600 threw the rescuer back a median +295 to +498. Rescues with three or more such throwbacks recovered
  1 of 5 (median 55.6 s), against 9 of 9 with one or two.
- **What.** It's opt-in per fight step: `"rescueNoReversal": {"commands": [432, 426], "within": 600}`, on both seats'
  `_rescue` routes. When the partner is logically downed, this seat isn't, a listed command is on screen and the
  partner is within `within` horizontally, the pad drops Triangle for that tick. The revive core is unchanged: it ends
  a hold for the RC and keeps walking, and the hold starts again once the prompt shows with no RC.
- **Logs.**
  - `coop_reversal_suppressed`, once per episode;
  - `rescue_no_reversal_summary`, per fight.
  - The core still writes `coop_reaction` for the RC it saw, so in a masked tick that row means it was seen, not
    pressed.
- **Measures.** `rescue_report.py RUN_DIR...`, per revive window:
  - result and length;
  - holds;
  - suppressed episodes;
  - throwbacks still taken;
  - the rescuer's hits, with orb hits (1041/1014) called out.

- **Result (zr112–114, lead #295).** No throwbacks were taken, but 0 kills in 3, and in the failed windows the rescuer
  took the orb stream instead (6 orb hits each). Lead #302 dropped `rescueNoReversal` from the batch config.

## Waiting out the orb volley while rescuing (lead #302)

`thorn_orbhold.py` sits over `thorn_creeper.py` (not over `thorn_rescue.py`) and is run with `run_orbhold.py` and the
`_creeper_orbhold` scenario.

- **Why.** Thorn's motion 202 winds up for ~340 frames, then loops a firing segment for up to ~30 s (median ~1805
  frames). Every orb hit in zr98–114 landed in that loop. Seats 1250+ from Thorn's root took ~0.36 orb hits per second
  there, against ~0.16 inside 1250. Lost revive windows were the ones spent in the loop with the downed partner far from
  Thorn: a median firing share of 1.0 with the partner 1584 away, against 0.18 and 1114 for recovered windows. A revive
  hold needs ~1.3 s, and occurrences are ~16 s apart.
- **What.** It's opt-in per fight step: `"rescueOrbHold": {"far": 1250, "hold": 1000, "finish": 300, "maxWaitSec": 30}`,
  on both seats' `_orbhold` routes. The rescuer holds off when all of these are true:
  - the partner is logically downed and this seat isn't;
  - Thorn (the existing head observation, reused for 0.25 s) is in 202;
  - the partner is `far` or more from Thorn's root;
  - this seat is more than `finish` from the partner.

  While held off, the rescuer moves toward Thorn until within `hold` of the root, then stands. The approach resumes
  when any of those stops being true, or after `maxWaitSec` held off in one revive window.

  Placement (r2, after Sage's r1 review): the hook, `OrbHoldCore`, is the class's last base, so C3 puts it directly
  above `thorn.CooperativeFight`. Every higher layer takes its tick first: the pre-revive Potion, RC gate,
  Creeper-first and approach revision. The core still makes every refusal of its own: logs, 60 s bound, event/menu,
  partner state, identity and scope, HP. Only the core's one steer toward the partner is swapped, for that tick. RCs
  are pressed with the hold-off direction. While held off, `thorn_approach`'s plateau anchor and any detour are reset,
  so the stand-off isn't treated as a stall. The outermost `OrbHoldApproachMixin` empties that layer's `facts()` for
  the rest of a held tick (r3), so a plateau candidate cached before the core can't fire a detour or escape after it.
- **Logs.**
  - `coop_orb_hold` / `coop_orb_hold_end`, per episode, with the end reason: `released`, `cap`, `timeout`,
    `peer-recovered` or `core`;
  - `rescue_orb_hold_summary`, per fight.
- **Measures.** `orbhold_report.py RUN_DIR...`: per revive window, as `rescue_report.py` plus hold-offs. The core keeps
  writing `coop_approach` while held off, so both reports place the same hits.

## Checks and analysis

- `tests/test_thorn_creeper.py`, `tests/test_thorn_rescue.py` and `tests/test_thorn_orbhold.py`: offline controls (ctest
  `kh2coop_py_test_thorn_*`).
- `creeper_report.py RUN_DIR...`: per run, the kill, Thorn's end HP, Creeper kills, atk-400 host downs, revive
  exposures and Creeper turns.
- `downs_report.py RUN_DIR`: per down, the killing hit and what each seat was doing.
