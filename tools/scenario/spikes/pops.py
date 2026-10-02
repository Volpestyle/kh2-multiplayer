"""List puppet pops in a recording (rec.csv with room/head columns).

Same rule as run.jitter_stats: a pop is a puppet step between consecutive
samples, with the owner in the viewer's room for the step and the 1.5 s
before it, that exceeds the owner's largest step in the last 0.5 s by more
than --threshold units. Each pop prints the puppet step, the owner's
recent steps and the time since the owner entered the room.

    python tools/scenario/spikes/pops.py RUN_DIR OWNER VIEWER PUPPET_NAME [THRESHOLD]
"""

import bisect
import csv
import math
import sys
from pathlib import Path


def main() -> int:
    run_dir, owner, viewer, name = Path(sys.argv[1]), sys.argv[2], sys.argv[3], sys.argv[4]
    threshold = float(sys.argv[5]) if len(sys.argv) > 5 else 30.0
    rows = list(csv.DictReader(open(run_dir / "rec.csv")))

    def pos(r):
        return (float(r["x"]), float(r["y"]), float(r["z"]))

    truth = [(float(r["t"]), r["room"], pos(r)) for r in rows
             if r["instance"] == owner and r["address"] == r["head"]]
    times = [s[0] for s in truth]
    puppet = [(float(r["t"]), r["room"], r["address"], pos(r)) for r in rows
              if r["instance"] == viewer and r["name"] == name and r["address"] != r["head"]]
    t0 = puppet[0][0]
    arrivals = [b[0] for a, b in zip(truth, truth[1:]) if a[1] != b[1]]

    count = 0
    for (ta, ra, aa, p), (tb, rb, ab, q) in zip(puppet, puppet[1:]):
        if ra != rb or aa != ab or tb - ta > 0.25:
            continue
        window = truth[bisect.bisect_left(times, ta - 1.5):bisect.bisect_right(times, tb)]
        if not window or any(r != ra for _, r, _ in window) or window[0][0] > ta - 1.25:
            continue
        recent = [s for s in window if s[0] >= ta - 0.5]
        owner_steps = [math.dist(a[2], b[2]) for a, b in zip(recent, recent[1:])]
        step = math.dist(p, q)
        excess = step - max(owner_steps, default=0.0)
        if excess > threshold:
            count += 1
            since = ta - max((t for t in arrivals if t <= ta), default=truth[0][0])
            print(f"t={ta - t0:7.1f}s room={ra} step={step:6.1f} excess={excess:6.1f} "
                  f"ownerSteps={[round(s) for s in owner_steps]} ownerInRoomFor={since:6.1f}s "
                  f"puppet {tuple(round(v) for v in p)} -> {tuple(round(v) for v in q)}")
    print(f"{count} pops")
    return 0


if __name__ == "__main__":
    sys.exit(main())
