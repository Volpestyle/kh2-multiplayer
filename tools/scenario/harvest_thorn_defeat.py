"""Read-only VUH-2807 receipt for one completed Thorn fight, without game input.

First run inventories a closed scenario and writes a review template. A second
run consumes the filled template and strictly decodes the two marked clips.
Pixels/rewards are operator observations, never inferred from a315 log line.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from clip_timing import compare, onset


VISUAL = ("overlay", "oneVictoryEach", "storyRewards", "bossCleanup", "onwardControl",
          "logsCompleteThroughOnward")


def read(path: Path) -> str:
    data = path.read_bytes()
    return data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load(path: Path) -> dict:
    return json.loads(read(path))


def check(status="UNQUALIFIED", reason="Evidence not reviewed", **evidence) -> dict:
    return dict(status=status, reason=reason, **evidence)


def log_path(run: Path, pid: int, explicit: Path | None) -> Path:
    if explicit:
        return explicit.resolve(strict=True)
    name = f"kh2coop_inject_{pid}.log"
    for path in (run / name, run.parents[1] / "rig/logs" / name):
        if path.is_file():
            return path
    raise ValueError(f"Missing full inject log for PID{pid}; pass --host-log/--friend-log")


def native_receipt(host: str, friend: str) -> dict:
    patterns = {
        "hostEnd16": r"\[mission-mirror\] host end trap=mission_dead_boss\b",
        "host315": r"\[thorn-defeat\] 315 entry path=native\b",
        "friendNative315": r"\[thorn-defeat\] 315 entry path=native\b",
        "friendReplay315": r"\[thorn-defeat\] 315 entry path=replay\b",
        "friend315Entries": r"\[thorn-defeat\] 315 entry path=",
        "retainedEnd16": r"\[mission-mirror\] client boss end host-led file=TT34_MS304.bar trap=16 replays=0 retained=1\b",
        "successfulPrelude": r"\[thorn-defeat\] prelude [^\n]*\bok=1\b",
        "unownedHost315Refusals": r"\[thorn-defeat\] host315 generic replay refused; no qualified owner\b",
        "nativeFailures": r"\[thorn-defeat\] (?:prelude [^\n]*\bok=0\b|315 context ok=0\b)",
    }
    facts = {}
    lines = {}
    for name, pattern in patterns.items():
        text = host if name.startswith("host") else friend
        matches = [(i, line) for i, line in enumerate(text.splitlines(), 1) if re.search(pattern, line)]
        facts[name] = len(matches)
        lines[name] = [dict(line=i, text=line) for i, line in matches[:8]]
    # end16 is generic in the log; require the accompanying exact Thorn scope.
    facts["killObserved"] = bool(facts["host315"] or
                                 (facts["hostEnd16"] and "TT34_MS304.bar" in host))
    facts["noEnd16OrLocal315"] = facts["retainedEnd16"] == 0 and facts["friendNative315"] == 0
    facts["zeroStartsQualificationFailure"] = facts["killObserved"] and facts["friend315Entries"] == 0
    if not facts["killObserved"]:
        result = check(reason="No Thorn defeat handoff exercised; zero starts on a wipe is UNQUALIFIED")
    elif facts["hostEnd16"] > 1 or facts["host315"] > 1:
        result = check(reason="Multiple host defeat occurrences; supply one complete fight per receipt")
    elif facts["friend315Entries"] != 1 or facts["friendNative315"] != 1 or facts["friendReplay315"]:
        result = check("FAIL", "A kill requires exactly one native315 and zero independent replay starts")
    elif facts["nativeFailures"]:
        result = check("FAIL", "Native prelude/context verification failed")
    else:
        result = check("PASS", "Exactly one native315, zero replay entries in supplied full friend log")
    return dict(**result, counts=facts, decisiveLines=lines)


def decode_video(receipt: Path, expected: int, pid: int) -> dict:
    """Run only after live closure: two owned offline decoders, each one thread."""
    video = receipt.with_name(receipt.name.removesuffix(".receipt.json"))
    data = load(receipt)
    if not video.is_file() or data["processId"] != pid:
        raise ValueError("Marked clip missing or belongs to the wrong seat")
    decoded = subprocess.run(["ffmpeg", "-v", "error", "-xerror", "-err_detect", "explode", "-threads", "1",
                              "-i", str(video), "-f", "null", "-"], check=True, capture_output=True)
    if decoded.stderr.strip():
        raise ValueError("ffmpeg reported decode errors despite a zero exit")
    probe = subprocess.run(["ffprobe", "-v", "error", "-select_streams", "v:0", "-count_frames",
                            "-threads", "1", "-show_entries", "stream=nb_read_frames", "-of", "json", str(video)],
                           check=True, capture_output=True)
    if probe.stderr.strip():
        raise ValueError("ffprobe reported decode errors despite completing its frame count")
    if int(json.loads(probe.stdout)["streams"][0]["nb_read_frames"]) != expected:
        raise ValueError("Decoded frame count differs from capture receipt")
    return dict(path=str(video), sha256=digest(video), frames=expected, strictDecode="PASS")


def timing_receipt(run: Path, review: dict, pids: list, decode: bool) -> dict:
    markers = review.get("timing", {})
    if not decode:
        return check(reason="Select first-visible frames, then rerun with --review and --decode after live closure")
    if markers.get("markerKind") != "first-visible-defeat-camera" or markers.get("precedingFramesNotDefeat") is not True:
        return check(reason="Initial death camera and preceding non-defeat frames must be visually reviewed")
    try:
        seats = []
        media = []
        for seat, pid in zip(("host", "friend"), pids):
            item = markers[seat]
            path = (run / item["receipt"]).resolve(strict=True)
            if not path.is_relative_to(run):
                raise ValueError("Marked capture receipt must be inside this run")
            data = load(path)
            media.append(decode_video(path, data["frames"], pid))
            seats.append(onset(path, item["firstVisibleFrame"]))
        result = compare(*seats, same_pc=markers.get("samePc") is True)
        return check(result["timing"], "Native Present-QPC onset interval; physical scanout is not measured",
                     bounds=result, decodedMedia=media)
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as exc:
        return check(reason=f"Timing evidence incomplete/corrupt: {exc}")


def reviewed_check(review: dict, name: str) -> dict:
    item = review.get(name, {})
    status = item.get("status", "UNQUALIFIED")
    evidence = item.get("evidence", "")
    if status not in ("PASS", "FAIL") or not isinstance(evidence, str) or not evidence.strip():
        return check(reason=f"Operator must review {name} and cite frames/report assertions")
    return check(status, "Operator-reviewed observation (not inferred from log count)", evidence=evidence)


def closure_receipt(path: Path | None, run: Path) -> dict:
    if path is None:
        return check(reason="Supply verified --closure directory after the live job finishes")
    try:
        closure = load(path / "closure.json")
        diff = load(path / "install-diff.json")
        flags = ("fullInstallDiffPass", "protectedSavesUnchanged", "ownedProcessesGone", "relayClosed")
        if closure.get("run") != run.name:
            return check(reason="Closure belongs to a different run")
        good = (all(closure.get(k) is True for k in flags) and diff.get("matched") is True and
                type(diff.get("beforeCount")) is int and diff["beforeCount"] > 0 and
                diff.get("beforeCount") == diff.get("afterCount") and
                all(diff.get(k) == [] for k in ("added", "removed", "changed")))
        return check("PASS" if good else "FAIL", "Retained install/save/process/relay closure receipt",
                     sources={p.name: dict(path=str(p.resolve()), sha256=digest(p))
                              for p in (path / "closure.json", path / "install-diff.json")})
    except (OSError, ValueError, KeyError, TypeError) as exc:
        return check(reason=f"Closure evidence unavailable: {exc}")


def harvest(run: Path, output: Path, *, host_log=None, friend_log=None, review_path=None,
            closure=None, decode=False) -> dict:
    run = run.resolve(strict=True)
    output = output.resolve()
    if output.exists() or output.is_relative_to(run):
        raise ValueError("--out must be fresh and outside the read-only scenario run")
    report_path = run / "report.json"
    report = load(report_path)
    pids = report["instances"]
    if len(pids) != 2 or len(set(pids)) != 2 or any(type(pid) is not int or pid <= 0 for pid in pids):
        raise ValueError("A complete two-seat scenario report is required")
    paths = dict(report=report_path, hostLog=log_path(run, pids[0], host_log),
                 friendLog=log_path(run, pids[1], friend_log))
    for capture in sorted(run.glob("*.mp4.*")):
        if capture.name.endswith((".receipt.json", ".capture-time.jsonl")):
            paths["capture:" + capture.name] = capture
    fingerprints = {name: digest(path) for name, path in paths.items()}
    review = load(review_path) if review_path else {}
    if review and review.get("inputSha256") != fingerprints:
        raise ValueError("Review is stale or belongs to different report/log bytes")
    native = native_receipt(read(paths["hostLog"]), read(paths["friendLog"]))
    checks = dict(native=native, timing=timing_receipt(run, review, pids, decode),
                  closure=closure_receipt(closure, run))
    checks.update({name: reviewed_check(review, name) for name in VISUAL})
    statuses = [row["status"] for row in checks.values()]
    verdict = ("UNQUALIFIED" if not native["counts"]["killObserved"] else
               "FAIL" if "FAIL" in statuses else "PASS" if all(s == "PASS" for s in statuses) else "UNQUALIFIED")
    result = dict(schema=1, issue="VUH-2807", status=verdict, run=str(run), scenarioStatus=report["status"],
                  instances=pids, inputSha256=fingerprints, sources={k: str(v) for k, v in paths.items()}, checks=checks,
                  clips=[str(p.relative_to(run)) for p in sorted(run.glob("*.mp4.receipt.json"))],
                  limits=["One complete Thorn fight per run; full logs through onward control are required.",
                          "No kill means UNQUALIFIED; a kill with zero starts is a qualification failure.",
                          "Manual fields are explicit operator observations. Missing proof never becomes PASS."])
    template = dict(inputSha256=fingerprints,
                    timing=dict(markerKind="first-visible-defeat-camera", samePc=False,
                                precedingFramesNotDefeat=False,
                                host=dict(receipt=None, firstVisibleFrame=None),
                                friend=dict(receipt=None, firstVisibleFrame=None)),
                    **{name: dict(status="UNQUALIFIED", evidence="") for name in VISUAL})
    output.mkdir(parents=True)
    for name, data in (("receipt.json", result), ("review-template.json", template)):
        (output / name).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    lines = [f"# VUH-2807: {verdict}", "", f"Run `{run.name}`; scenario `{report['status']}`.", "",
             "| Check | Result | Evidence / gap |", "|---|---|---|"]
    lines += [f"| {name} | {row['status']} | {row['reason']} |" for name, row in checks.items()]
    lines += ["", "Native counts: `" + json.dumps(native["counts"]) + "`.", "",
              "Review first-visible initial death-camera frames, overlay, one reward/story and onward control.",
              "A later315 is not the initial cinematic origin. This tool performs no game/desk/network action."]
    (output / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--host-log", type=Path)
    parser.add_argument("--friend-log", type=Path)
    parser.add_argument("--review", type=Path)
    parser.add_argument("--closure", type=Path)
    parser.add_argument("--decode", action="store_true", help="Strictly decode only the two marked clips after live closure")
    args = parser.parse_args()
    try:
        result = harvest(args.run, args.out, host_log=args.host_log, friend_log=args.friend_log,
                         review_path=args.review, closure=args.closure, decode=args.decode)
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print(json.dumps(dict(status="UNQUALIFIED", error=str(exc))))
        return 2
    print(json.dumps(dict(status=result["status"], output=str(args.out), counts=result["checks"]["native"]["counts"])))
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
