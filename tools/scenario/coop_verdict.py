"""Co-op route verdict from the logs both seats already write (VUH-2337, VUH-2344).

Reads one host inject log, one or more friend inject logs and, optionally, the
relay log. Works the same for a one-PC two-instance run and for logs collected
from two PCs. Inject lines carry no timestamps, so this checks what happened,
not how fast:

- every room the host published (`host arrived epoch=E`) has a friend arrival
  for that epoch at the same location; an epoch the host left before the friend
  issued it is a warning, and the host's last epoch is required;
- every progress version a friend applied (at room boundaries, so skipped
  intermediate versions are normal) has the host's hash for that version, and
  personal SAVE bytes stayed unchanged; the host's latest version not applied
  yet is a warning;
- no relay `Desync:` report and no `EXCEPTION` line in any inject log.

    python -B tools/scenario/coop_verdict.py --host H.log --friend F.log [--relay relay.log] [--out v.json]

Exit 0 on pass, 1 on fail.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

LOCATION = r"room=(?P<world>[0-9A-F]{2})/(?P<room>[0-9A-F]{2}) door=(?P<door>\d+) map=(?P<map>\d+) btl=(?P<btl>\d+) evt=(?P<evt>\d+)"
HOST_ARRIVED = re.compile(r"\[enemysync\] host arrived epoch=(?P<epoch>\d+) " + LOCATION)
CLIENT_ARRIVED = re.compile(r"\[enemysync\] client arrived epoch=(?P<epoch>\d+) " + LOCATION)
HOST_PROGRESS = re.compile(r"\[progresssync\] host (?P<kind>full|delta) version=(?P<version>\d+) .*?hash=(?P<hash>[0-9A-F]+)")
CLIENT_PROGRESS = re.compile(
    r"\[progresssync\] apply version=(?P<version>\d+) .*?hash=(?P<hash>[0-9A-F]+) .*?personal_unchanged=(?P<personal>\d)")
TRAVEL = re.compile(r"\[travel\] (?P<what>native request|landed) (?P<detail>.*)")
HOLD_PUBLISHED = re.compile(r"\[event-hold\] published active=(?P<active>\d) epoch=(?P<epoch>\d+)")
HOLD_RELEASED = re.compile(r"\[event-control\] input phase=\d+ .*released=1")
HOLD_SKIPPED = re.compile(r"\[event-control\] hold skipped epoch=(?P<epoch>\d+)")
HOLD_TIMEOUT = re.compile(r"\[event-control\] owner did not converge")
EXIT_BLOCKED = re.compile(r"\[warp\] client native exit blocked")
DESYNC = re.compile(r"Desync: (?P<detail>.*)")


def location(m: re.Match) -> dict:
    return {"world": int(m["world"], 16), "room": int(m["room"], 16), "door": int(m["door"]),
            "map": int(m["map"]), "btl": int(m["btl"]), "evt": int(m["evt"])}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def friend_report(text: str, host_rooms: list[dict], host_progress: list[dict]) -> dict:
    arrivals = {}
    for m in CLIENT_ARRIVED.finditer(text):
        arrivals[int(m["epoch"])] = location(m)
    applied = {int(m["version"]): {"hash": m["hash"], "personalUnchanged": m["personal"] == "1"}
               for m in CLIENT_PROGRESS.finditer(text)}
    problems, warnings, rooms = [], [], []
    for i, host in enumerate(host_rooms):
        got = arrivals.get(host["epoch"])
        last = i == len(host_rooms) - 1
        row = {"epoch": host["epoch"], "host": host["location"], "friend": got}
        if got is None:
            row["result"] = "missing" if last else "superseded"
            (problems if last else warnings).append(
                f"epoch {host['epoch']}: no friend arrival" + ("" if last else " (host moved on)"))
        elif got != host["location"]:
            row["result"] = "mismatch"
            problems.append(f"epoch {host['epoch']}: friend arrived at {got}, host at {host['location']}")
        else:
            row["result"] = "followed"
        rooms.append(row)
    # A friend applies the latest snapshot at each room boundary, so skipped
    # intermediate versions are normal; whatever it applies must match the host.
    published = {p["version"]: p["hash"] for p in host_progress}
    progress = []
    for version, got in sorted(applied.items()):
        want = published.get(version)
        progress.append({"version": version, "hostHash": want, "friend": got})
        if want is None:
            problems.append(f"progress v{version}: applied but never published by the host")
        elif got["hash"] != want:
            problems.append(f"progress v{version}: hash {got['hash']} != host {want}")
        elif not got["personalUnchanged"]:
            problems.append(f"progress v{version}: personal bytes changed")
    if host_progress and not applied:
        problems.append("progress: the friend applied no host snapshot")
    elif host_progress and host_progress[-1]["version"] not in applied:
        warnings.append(f"progress v{host_progress[-1]['version']}: not applied yet (next room boundary)")
    return {
        "rooms": rooms, "progress": progress,
        "holdsReleased": len(HOLD_RELEASED.findall(text)),
        "holdsSkipped": [int(m["epoch"]) for m in HOLD_SKIPPED.finditer(text)],
        "holdTimeouts": len(HOLD_TIMEOUT.findall(text)),
        "nativeExitsBlocked": len(EXIT_BLOCKED.findall(text)),
        "exceptions": text.count("EXCEPTION"),
        "problems": problems, "warnings": warnings,
    }


def verdict(host_text: str, friend_texts: dict[str, str], relay_text: str = "") -> dict:
    host_rooms = [{"epoch": int(m["epoch"]), "location": location(m)} for m in HOST_ARRIVED.finditer(host_text)]
    host_progress = [{"version": int(m["version"]), "kind": m["kind"], "hash": m["hash"]}
                     for m in HOST_PROGRESS.finditer(host_text)]
    result = {
        "host": {
            "rooms": host_rooms, "progress": host_progress,
            "travel": [f"{m['what']} {m['detail'].strip()}" for m in TRAVEL.finditer(host_text)],
            "holdsPublished": [{"active": m["active"] == "1", "epoch": int(m["epoch"])}
                               for m in HOLD_PUBLISHED.finditer(host_text)],
            "exceptions": host_text.count("EXCEPTION"),
        },
        "friends": {name: friend_report(text, host_rooms, host_progress) for name, text in friend_texts.items()},
        "desync": [m["detail"].strip() for m in DESYNC.finditer(relay_text)],
    }
    problems = [f"{name}: {p}" for name, f in result["friends"].items() for p in f["problems"]]
    problems += [f"{name}: {f['exceptions']} EXCEPTION lines" for name, f in result["friends"].items() if f["exceptions"]]
    if result["host"]["exceptions"]:
        problems.append(f"host: {result['host']['exceptions']} EXCEPTION lines")
    if not host_rooms:
        problems.append("host published no room")
    problems += [f"relay desync: {d}" for d in result["desync"]]
    result["problems"] = problems
    result["pass"] = not problems
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--friend", type=Path, action="append", required=True)
    parser.add_argument("--relay", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    result = verdict(read(args.host), {p.name: read(p) for p in args.friend},
                     read(args.relay) if args.relay else "")
    text = json.dumps(result, indent=2)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    print(text)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
