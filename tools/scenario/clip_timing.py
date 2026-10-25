"""Offline first-visible-onset bounds from native Present QPC receipts.

Marker frame numbers are zero-based decoded frames. The operator must identify
the first visible defeat frame AND check the preceding frame is not defeat.
This tool never substitutes a later315 or a mission-state sample for pixels.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path, PureWindowsPath


def onset(receipt_path: Path, frame: int) -> dict:
    receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    if not receipt.get("ok") or receipt.get("timingMethod") != "native-Present-QPC-bounds-v1":
        raise ValueError("native capture-time receipt required")
    for name in ("gameFpsBefore", "gameFpsDuringCapture", "captureSeconds", "clipFps"):
        value = receipt.get(name)
        if not isinstance(value, (float, int)) or not 0 < value < float("inf"):
            raise ValueError(f"invalid present-rate receipt: {name}")
    # Resolve the sidecar adjacent to the exported receipt, so sealed evidence
    # is portable even when its original absolute capture path no longer exists.
    timing_path = receipt_path.with_name(PureWindowsPath(receipt["captureTiming"]).name)
    rows = [json.loads(line) for line in timing_path.read_text(encoding="utf-8").splitlines()]
    count = receipt["frames"]
    if type(count) is not int or len(rows) != count or count < 2:
        raise ValueError("incomplete frame timing")
    rows.sort(key=lambda row: row["frame"])
    frequency = rows[0]["qpcFrequency"]
    if type(frequency) is not int or frequency <= 0:
        raise ValueError("invalid QPC frequency")
    previous = None
    for index, row in enumerate(rows):
        if row["schema"] != 1 or row["frame"] != index or row["qpcFrequency"] != frequency:
            raise ValueError("duplicate/missing frame or changed clock")
        begin, end, present = row["qpcBegin"], row["qpcEnd"], row["present"]
        if any(type(n) is not int for n in (begin, end, present)) or not 0 < begin <= end:
            raise ValueError("invalid native frame bounds")
        if previous and (begin <= previous["qpcEnd"] or present <= previous["present"]):
            raise ValueError("nonmonotonic native capture")
        previous = row
    if not 0 < frame < count:
        raise ValueError("onset needs both first defeat frame and preceding non-defeat frame")
    return {"firstVisibleFrame": frame, "frequency": frequency,
            "earliestQpc": rows[frame - 1]["qpcBegin"], "latestQpc": rows[frame]["qpcEnd"],
            "presentRateBefore": receipt["gameFpsBefore"],
            "presentRateDuring": receipt["gameFpsDuringCapture"],
            "processId": receipt["processId"], "clockDomain": receipt.get("clockDomain")}


def compare(host: dict, friend: dict, *, same_pc: bool, threshold_ms: float = 1000) -> dict:
    if not same_pc or not host["clockDomain"] or host["clockDomain"] != friend["clockDomain"]:
        raise ValueError("explicit same-PC topology and matching clock domain required")
    if host["frequency"] != friend["frequency"] or host["processId"] == friend["processId"]:
        raise ValueError("different seats on one QPC clock required")
    scale = 1000 / host["frequency"]
    low = (friend["earliestQpc"] - host["latestQpc"]) * scale
    high = (friend["latestQpc"] - host["earliestQpc"]) * scale
    # A wide interval crossing the threshold is unqualified, never an average
    # that happens to pass. A definite miss is reported as FAIL.
    upper = max(abs(low), abs(high))
    lower = 0 if low <= 0 <= high else min(abs(low), abs(high))
    verdict = "PASS" if upper <= threshold_ms else "FAIL" if lower > threshold_ms else "UNQUALIFIED"
    return {"schema": 1, "timing": verdict, "signedSkewBoundsMs": [low, high],
            "absoluteSkewUpperBoundMs": upper, "thresholdMs": threshold_ms,
            "host": host, "friend": friend,
            "markerRequirement": "first visible defeat, with preceding non-defeat frame; decode integrity checked separately"}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host-receipt", type=Path, required=True)
    parser.add_argument("--friend-receipt", type=Path, required=True)
    parser.add_argument("--host-frame", type=int, required=True)
    parser.add_argument("--friend-frame", type=int, required=True)
    parser.add_argument("--same-pc", action="store_true")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compare(onset(args.host_receipt, args.host_frame),
                         onset(args.friend_receipt, args.friend_frame), same_pc=args.same_pc)
    except (ValueError, KeyError, TypeError, OSError) as exc:
        result = {"schema": 1, "timing": "UNQUALIFIED", "error": str(exc)}
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result))
    return 0 if result["timing"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
