"""Run current main's scenario engine with the head-commit pilot (thorn_headcommit.py over thorn_tether.py, VUH-2617 lead #355).

Usage: python -B tools/rig/thorn_pilot/run_headcommit.py tools/rig/thorn_pilot/scenarios/net_twilight_thorn_bots_dmgtrace600s254b_trace_react_creeper_headcommit.json [--validate]
Both Thorn fight routes (routes/*_headcommit.json) carry `rescueOrbHold`, `rescueProtect`, `rescuePotionReserve`, `partnerTether` and `headCommit202`; only the host's carries `creeperFirst`.
Every other step runs thorn_react299g unchanged.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "scenario"))
import run as runner  # noqa: E402

runner.PILOT = Path(__file__).resolve().parent / "thorn_headcommit.py"

if __name__ == "__main__":
    sys.exit(runner.main())
