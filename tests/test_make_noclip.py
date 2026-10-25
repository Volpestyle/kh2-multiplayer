"""tools/rig/thorn_pilot/make_noclip.py (VUH-2617 lead #301): the bld no-clip twin drops only clips, and the committed
twin is current. Run: python -B tests/test_make_noclip.py
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PILOT = ROOT / "tools" / "rig" / "thorn_pilot"
sys.path.insert(0, str(PILOT))
import make_noclip  # noqa: E402

failures = 0


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


src = {"name": "x", "description": "d", "protect": [], "steps": [
    {"do": "boot", "instance": 0},
    {"do": "clip", "instance": 0, "seconds": 30, "background": True},
    {"do": "clip_wait"},
    {"do": "fight", "seconds": 30, "as": "thorn_01", "clips": [{"instance": 0}], "sample": {"expr": "1"}},
    {"do": "save", "as": "clips", "expr": "'clips'"},
    {"do": "assert", "expr": "True", "message": "the current paired clip block"}]}
out = make_noclip.noclip(src)
check(out["name"] == "x_noclip" and out["description"].startswith("NOCLIP (VUH-2617 lead #301, bld): x ")
      and out["description"].endswith(" d"), "name suffix and description prefix")
check([s["do"] for s in out["steps"]] == ["boot", "fight", "save", "assert"], "clip and clip_wait steps are dropped")
check(out["steps"][1] == {"do": "fight", "seconds": 30, "as": "thorn_01", "sample": {"expr": "1"}},
      "a fight step loses only its clips")
check(out["steps"][2] == src["steps"][4] and out["steps"][3] == src["steps"][5],
      "a non-fight step's own 'clips' key and clip wording are left alone")
check(src["steps"][3].get("clips") and len(src["steps"]) == 6, "the source scenario is not modified")
check(out["protect"] == [] and set(out) == set(src), "every other top-level key is kept")

for scenario in ("net_twilight_thorn_bots_dmgtrace600s254b_trace_react_creeper.json",):
    r = subprocess.run([sys.executable, "-B", str(PILOT / "make_noclip.py"), str(PILOT / "scenarios" / scenario), "--check"],
                       capture_output=True, text=True)
    check(r.returncode == 0, f"the committed twin of {scenario} is current")
    twin = json.loads((PILOT / "scenarios" / scenario.replace(".json", "_noclip.json")).read_text(encoding="utf-8"))
    check(not any(s.get("do") in ("clip", "clip_wait") or (s.get("do") == "fight" and "clips" in s) for s in twin["steps"]),
          f"{scenario}'s twin records no clips")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
