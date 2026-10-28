"""VUH-2808: flag canonical Thorn seat heights more than 1000 below floor Y=0.

Read-only, also run automatically at scenario closure. +Y is down. Missing
samples are unavailable; sampled heights cannot establish native recovery.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

FLOOR_Y = 0.0
MAX_BELOW = 1000.0


def evaluate(paths: list[Path]) -> dict:
    seats = []
    for path in paths:
        samples, unreadable = 0, 0
        first, worst = None, None
        try:
            stream = path.open(encoding="utf-8", errors="replace")
        except OSError as error:
            seats.append({"pilot": path.parent.name, "path": str(path), "samples": 0,
                          "unreadable": 1, "firstFall": None, "worst": None, "error": str(error)})
            continue
        with stream:
            for line, raw in enumerate(stream, 1):
                try:
                    row = json.loads(raw)
                except (ValueError, TypeError):
                    unreadable += 1
                    continue
                state = row.get("state") if isinstance(row, dict) else None
                if not isinstance(state, dict) or (state.get("world"), state.get("room")) != (2, 34):
                    continue
                # Native cinematics/loading do not establish an arena-floor observation.
                if state.get("inField") is not True or state.get("event") is not False:
                    continue
                position = state.get("playerPos")  # never the camera or a clone's streamed pose
                if not isinstance(position, list) or len(position) != 3 or \
                        any(type(v) not in (int, float) or not math.isfinite(v) for v in position):
                    unreadable += 1
                    continue
                samples += 1
                sample = {"line": line, "t": row.get("t"), "playerPos": position,
                          "belowFloor": position[1] - FLOOR_Y}
                if worst is None or sample["belowFloor"] > worst["belowFloor"]:
                    worst = sample
                if first is None and sample["belowFloor"] > MAX_BELOW:
                    first = sample
        seats.append({"pilot": path.parent.name, "path": str(path), "samples": samples,
                      "unreadable": unreadable, "firstFall": first, "worst": worst})
    falls = [s for s in seats if s["firstFall"] is not None]
    complete = bool(seats) and all(s["samples"] and not s["unreadable"] for s in seats)
    return {"status": "fail" if falls else "pass" if complete else "unavailable",
            "floorY": FLOOR_Y, "maxBelowFloor": MAX_BELOW,
            "samples": sum(s["samples"] for s in seats), "seats": seats,
            "limits": "Sampled canonical positions only; missing seats/intervals and native recovery are unproven."}


def check_run(run_dir: Path, report: dict, artifacts: list[str]) -> dict:
    result = evaluate(sorted(run_dir.glob("*/pilot.jsonl")))
    # Drop unrelated route pilots only from the automatic result; explicit CLI inputs remain required seats.
    result["seats"] = [s for s in result["seats"] if s["samples"] or "thorn" in s["pilot"]]
    if result["status"] != "fail":
        result["status"] = "pass" if result["seats"] and all(
            s["samples"] and not s["unreadable"] for s in result["seats"]) else "unavailable"
    if result["seats"] or "twilight_thorn" in report.get("scenario", ""):
        report["thornFloor"] = result
        name = "thorn_floor.json"
        (run_dir / name).write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        artifacts.append(name)
        if result["status"] == "fail":
            if report.get("status") == "pass":
                report["status"] = "fail"
            report.setdefault("error", "Thorn seat fell more than 1000 units below the arena floor; see thorn_floor.json")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pilots", nargs="+", type=Path, help="one pilot.jsonl per seat")
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    result = evaluate(args.pilots)
    text = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
