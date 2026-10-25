"""Run current main's scenario engine with the orb-volley rescue pilot (thorn_orbhold.py over thorn_creeper.py, VUH-2617 lead #302).

Usage: python -B tools/rig/thorn_pilot/run_orbhold.py tools/rig/thorn_pilot/scenarios/net_twilight_thorn_bots_dmgtrace600s254b_trace_react_creeper_orbhold.json [--validate]
Both Thorn fight routes (routes/*_orbhold.json) carry `rescueOrbHold`; only the host's carries `creeperFirst`.
Every other step runs thorn_react299g unchanged.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "scenario"))
import run as runner  # noqa: E402

runner.PILOT = Path(__file__).resolve().parent / "thorn_orbhold.py"

if __name__ == "__main__":
    sys.exit(runner.main())
