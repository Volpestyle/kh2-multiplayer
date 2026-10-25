"""Run current main's scenario engine with the potion-reserve rescue pilot (thorn_potion.py over thorn_protect.py, VUH-2617 lead #343).

Usage: python -B tools/rig/thorn_pilot/run_potion.py tools/rig/thorn_pilot/scenarios/net_twilight_thorn_bots_dmgtrace600s254b_trace_react_creeper_potion.json [--validate]
Both Thorn fight routes (routes/*_potion.json) carry `rescueOrbHold`, `rescueProtect` and `rescuePotionReserve`; only the host's carries `creeperFirst`.
Every other step runs thorn_react299g unchanged.
"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "scenario"))
import run as runner  # noqa: E402

runner.PILOT = Path(__file__).resolve().parent / "thorn_potion.py"

if __name__ == "__main__":
    sys.exit(runner.main())
