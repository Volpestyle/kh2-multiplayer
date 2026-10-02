"""Analyse a KH2COOP_PUPPET_TRACE log (per-frame puppet 0 trace).

    python tools/scenario/spikes/ptrace.py RUN_DIR

Reports whether the pose stream we write is smooth frame to frame, and how
far the game's own update moves the puppet away from our last write
(ground snapping, physics) before we overwrite it.
"""

import math
import re
import sys
from pathlib import Path

RX = re.compile(r"f=(\d+) t=(\d+) game=\(([-\d.]+),([-\d.]+),([-\d.]+)\) "
                r"pose=\(([-\d.]+),([-\d.]+),([-\d.]+)\) motion=(\d+)")


def main() -> int:
    run_dir = Path(sys.argv[1])
    frames = []
    for log in run_dir.glob("kh2coop_inject_*.log"):
        for line in log.read_text(errors="replace").splitlines():
            m = RX.search(line)
            if m:
                v = m.groups()
                frames.append((int(v[0]), int(v[1]), tuple(map(float, v[2:5])),
                               tuple(map(float, v[5:8])), int(v[8])))
    if len(frames) < 3:
        print("no trace")
        return 1
    print(f"{len(frames)} frames")
    steps = [math.dist(a[3], b[3]) for a, b in zip(frames, frames[1:])]
    print(f"pose step: max {max(steps):.1f}, p99 {sorted(steps)[int(.99 * len(steps))]:.1f}")
    jerks = [(frames[i + 1][0], steps[i], steps[i + 1]) for i in range(len(steps) - 1)
             if abs(steps[i + 1] - steps[i]) > 8]
    print(f"frames where the pose step changes by > 8 units: {len(jerks)}")
    for f, s1, s2 in jerks[:10]:
        print(f"  f={f} step {s1:.1f} -> {s2:.1f}")
    moved = [(b[0], math.dist(b[2], a[3]), b[2][1] - a[3][1], b[4])
             for a, b in zip(frames, frames[1:]) if b[0] == a[0] + 1]
    big = [m for m in moved if m[1] > 10]
    print(f"frames where the game moved the puppet > 10 units from our last write: {len(big)}")
    for f, d, dy, motion in big[:12]:
        print(f"  f={f} moved {d:.1f} (dy {dy:+.1f}) motion {motion}")
    gaps = [b[0] - a[0] for a, b in zip(frames, frames[1:])]
    print(f"frame gaps > 1: {sum(g > 1 for g in gaps)} (max {max(gaps)})")
    for i in sorted(sorted(range(len(steps)), key=lambda i: -steps[i])[:6]):
        a, b = frames[i], frames[i + 1]
        print(f"  big pose step f={a[0]}->{b[0]} {steps[i]:.1f} motion {a[4]}->{b[4]} "
              f"y {a[3][1]:.1f}->{b[3][1]:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
