"""Run the existing scenario engine with the separate VUH-2617 pilot entry.

Usage: python -B tools/scenario/run_thorn.py SCENARIO [--validate]
All ownership, assertions, process cleanup and native controls stay in run.py.
"""
import sys
from pathlib import Path

import run as runner

runner.PILOT = Path(__file__).resolve().parents[1] / "rig/route_pilot/thorn.py"

if __name__ == "__main__":
    sys.exit(runner.main())
