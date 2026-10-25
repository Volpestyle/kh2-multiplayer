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

## Checks and analysis

- `tests/test_thorn_creeper.py`: offline controls (ctest `kh2coop_py_test_thorn_creeper`).
- `creeper_report.py RUN_DIR...`: per run, the kill, Thorn's end HP, Creeper kills, atk-400 host downs, revive
  exposures and Creeper turns.
- `downs_report.py RUN_DIR`: per down, the killing hit and what each seat was doing.
