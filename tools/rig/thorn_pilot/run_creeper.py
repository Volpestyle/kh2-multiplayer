"""Run current main's scenario engine with the Creeper-first pilot (thorn_creeper.py over thorn_react299g.py, VUH-2617 lead #282).

Usage: python -B tools/rig/thorn_pilot/run_creeper.py tools/rig/thorn_pilot/scenarios/net_twilight_thorn_bots_dmgtrace600s254b_trace_react_creeper.json [--validate]
Only the host route (routes/twilight_thorn_host_fight_creeper.json) carries `creeperFirst`; the friend's route and
every other step run thorn_react299g unchanged.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "scenario"))
import run as runner  # noqa: E402

runner.PILOT = Path(__file__).resolve().parent / "thorn_creeper.py"

if __name__ == "__main__":
    sys.exit(runner.main())
