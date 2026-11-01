"""Co-op route verdict from the logs both seats already write (VUH-2337, VUH-2344).

Reads one host inject log, one or more friend inject logs and, optionally, the
relay log. Works the same for a one-PC two-instance run and for logs collected
from two PCs. Inject lines carry no timestamps, so this checks what happened,
not how fast:

- every room the host published (`host arrived epoch=E`) has a friend arrival
  for that epoch at the same location; an epoch the host left before the friend
  issued it is a warning, and the host's last epoch is required. Each room row
  also records, without judging it, when each seat got there: the first
  `[statehash] ... epoch=E ... tickMs=` stamp per seat (GetTickCount, logged
  with the arrival and then about once a second in gameplay) and the friend's
  lag behind the host (VUH-2898). Only one-PC runs share that clock;
  between two PCs lagMs is meaningless;
- every progress version a friend applied (at room boundaries, so skipped
  intermediate versions are normal) has the host's hash for that version, and
  personal SAVE bytes stayed unchanged; the host's latest version not applied
  yet is a warning;
- no relay `Desync:` report, no `EXCEPTION` line and no `[crash]` line in any
  inject log. The inject crash handler logs `[crash] unhandled exception 0xCODE
  at ADDR; minidump written|FAILED: PATH` (inject/src/CrashDump.cpp); each such
  line fails the run with the seat and the exception code named. A
  progress-only desync (fields=4) is a warning instead when every friend later
  applies, at a room boundary after that epoch, a host version with a matching
  hash: friends apply progress at boundaries, so their own copies of an event
  can drift mid-room. A progress-only desync in the epoch of a combat-admission
  hold (VUH-2897) is also a warning when the host, frozen behind the friend,
  reaches the friend's hash within 2 s of host play after `admission-release`
  and both seats end the epoch on it; past the bound it fails. A progress-only
  desync at a room entry (VUH-2631) is a warning when it is exactly that race:
  every friend applied, at that epoch's boundary, the host version the relay saw
  on the friend; the host's first sample in the new room still carried its
  previous version's hash (the relay's host hash), every later host sample has
  the friend's hash, and every friend sample there has it too. A second stale
  host sample, or any other shape, fails. One that never converges fails. Room
  and enemy desyncs always fail. The relay's desync-reports manifests (--desync-dir) add the room
  and both progress hashes to each entry.

    python -B tools/scenario/coop_verdict.py --host H.log --friend F.log [--relay relay.log]
        [--desync-dir RUN/desync-reports] [--out v.json]

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
DESYNC = re.compile(r"Desync: (?P<detail>(?P<peer>\S+) fields=(?P<fields>\d+) epoch=(?P<epoch>\d+).*)")
CLIENT_ISSUED = re.compile(r"\[warp\] client issued epoch=(?P<epoch>\d+)")
STATEHASH_TICK = re.compile(r"\[statehash\] role=(?:host|client) epoch=(?P<epoch>\d+) .*?tickMs=(?P<tick>\d+)")
STATEHASH_PROGRESS = re.compile(
    r"\[statehash\] role=(?:host|client) epoch=(?P<epoch>\d+) .*?progress=(?P<progress>[0-9A-F]+) .*?tickMs=(?P<tick>\d+)")
ADMISSION_RELEASED = re.compile(r"\[combat-admission\] released .*?epoch=(?P<epoch>\d+) ")
CRASH = re.compile(r"\[crash\][^\r\n]*")  # any line from inject/src/CrashDump.cpp
CRASH_DETAIL = re.compile(r"unhandled exception (?P<code>0x[0-9A-Fa-f]+) at (?P<address>\S+?); minidump (?P<dump>\w+)")
DESYNC_PROGRESS = 4  # DesyncField::DesyncProgress
ADMISSION_CONVERGE_MS = 2000  # host play after its first post-release sample (VUH-2897 run 043745: 1015 ms)


def location(m: re.Match) -> dict:
    return {"world": int(m["world"], 16), "room": int(m["room"], 16), "door": int(m["door"]),
            "map": int(m["map"]), "btl": int(m["btl"]), "evt": int(m["evt"])}


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


def crashes(text: str) -> list[dict]:
    """Every `[crash]` line, with the exception code and minidump status when the line has them."""
    out = []
    for m in CRASH.finditer(text):
        d = CRASH_DETAIL.search(m.group(0))
        out.append({"line": m.group(0).strip(), "code": d["code"].upper().replace("0X", "0x") if d else None,
                    "minidump": d["dump"] if d else None})
    return out


def crash_problem(seat: str, crash: dict) -> str:
    if crash["code"] is None:
        return f"{seat}: crash line: {crash['line']}"
    return f"{seat}: crash: unhandled exception {crash['code']} (minidump {crash['minidump']})"


def first_ticks(text: str) -> dict[int, int]:
    """Epoch -> the seat's first statehash tickMs in it (its arrival, to about a frame)."""
    out: dict[int, int] = {}
    for m in STATEHASH_TICK.finditer(text):
        out.setdefault(int(m["epoch"]), int(m["tick"]))
    return out


def boundary_applies(text: str) -> list[dict]:
    """Progress applies, each tagged with the host epoch whose load it preceded."""
    out, pending = [], []
    for m in re.finditer(CLIENT_PROGRESS.pattern + "|" + CLIENT_ISSUED.pattern, text):
        if m["version"] is not None:
            pending.append({"version": int(m["version"]), "hash": m["hash"], "epoch": None})
        else:
            for row in pending:
                row["epoch"] = int(m["epoch"])
            out += pending
            pending = []
    return out + pending


def desync_details(desync_dir: Path | None) -> dict:
    """(epoch, fields) -> room and both progress hashes from relay manifests."""
    out = {}
    for manifest in sorted(desync_dir.glob("*/*/manifest.json")) if desync_dir else []:
        try:
            t = json.loads(manifest.read_text(encoding="utf-8"))["trigger"]
            out[(t["epoch"], t["fields"])] = {
                "room": f"{t['hostHash']['worldId']:02X}/{t['hostHash']['roomId']:02X}",
                "hostProgress": f"{t['hostHash']['progressHash']:08X}",
                "friendProgress": f"{t['clientHash']['progressHash']:08X}"}
        except (OSError, KeyError, ValueError, TypeError):
            continue
    return out


def admission_convergence(host_text: str, friend_texts: dict[str, str], epoch: int, detail: dict) -> int | None:
    """VUH-2897: ms of host play, from its first statehash after `admission-release` to the sample
    that reaches the friend's diverged progress; None when this desync is not that case.

    The combat-admission hold keeps the host frozen after the friend's own intro, and a frozen
    player never sets the progress byte a controllable one does (static hazard H5), so the
    friend runs ahead until release. It counts only when the relay's host hash is a host
    sample of that epoch taken before it converged, the host then reaches the relay's friend
    hash (and never had it before), and every seat ends the epoch on it. Statehash samples
    only in gameplay, so a native pause across the release is not counted."""
    release = next((m for m in ADMISSION_RELEASED.finditer(host_text) if int(m["epoch"]) == epoch), None)
    if release is None or not detail:
        return None
    rows = [(m.start(), m["progress"], int(m["tick"])) for m in STATEHASH_PROGRESS.finditer(host_text)
            if int(m["epoch"]) == epoch]
    after = [r for r in rows if r[0] > release.start()]
    target = detail["friendProgress"]
    reached = next((r for r in after if r[1] == target), None)
    if reached is None:
        return None
    before = [r[1] for r in rows if r[0] < reached[0]]
    if target in before or detail["hostProgress"] not in before or any(r[1] != target for r in after if r[0] > reached[0]):
        return None
    for text in friend_texts.values():
        seen = [m["progress"] for m in STATEHASH_PROGRESS.finditer(text) if int(m["epoch"]) == epoch]
        if not seen or seen[-1] != target:
            return None
    return reached[2] - after[0][2]


def room_entry_race(host_text: str, friend_texts: dict[str, str], applies: dict[str, list[dict]],
                    host_progress: list[dict], epoch: int, detail: dict) -> dict | None:
    """VUH-2631: the version and stale hash when this progress desync is the room-entry race;
    None otherwise.

    At a room boundary the friend applies the host's new version, while the host's first
    statehash in the new room still hashes its previous version (run 20261021-121721: one
    D8E9050A sample at 02/21, then 201 with 8D692D6C). It counts only when every friend applied,
    at this epoch's boundary, the version whose host hash is the relay's friend hash; the relay's
    host hash is the host's previous published version; the host's first sample after arriving
    in the epoch has it and every later one (at least one) has the friend's hash; and every
    friend sample in the epoch has the friend's hash."""
    if not detail or detail["hostProgress"] == detail["friendProgress"]:
        return None
    stale, fresh = detail["hostProgress"], detail["friendProgress"]
    versions = []
    for rows in applies.values():
        row = next((a for a in rows if a["epoch"] == epoch and a["hash"] == fresh), None)
        if row is None:
            return None
        versions.append(row["version"])
    if len(set(versions)) != 1:
        return None
    version = versions[0]
    published = sorted((p["version"], p["hash"]) for p in host_progress)
    want = next((h for v, h in published if v == version), None)
    previous = [h for v, h in published if v < version]
    if want != fresh or not previous or previous[-1] != stale:
        return None
    arrived = next((m for m in HOST_ARRIVED.finditer(host_text) if int(m["epoch"]) == epoch), None)
    if arrived is None:
        return None
    host = [m["progress"] for m in STATEHASH_PROGRESS.finditer(host_text, arrived.end()) if int(m["epoch"]) == epoch]
    if len(host) < 2 or host[0] != stale or any(h != fresh for h in host[1:]):
        return None
    for text in friend_texts.values():
        seen = [m["progress"] for m in STATEHASH_PROGRESS.finditer(text) if int(m["epoch"]) == epoch]
        if not seen or any(h != fresh for h in seen):
            return None
    return {"version": version, "stale": stale, "fresh": fresh, "hostSamples": len(host)}


def friend_report(text: str, host_rooms: list[dict], host_progress: list[dict]) -> dict:
    arrivals, arrived_at = {}, {}
    for m in CLIENT_ARRIVED.finditer(text):
        arrivals[int(m["epoch"])] = location(m)
        arrived_at[int(m["epoch"])] = m.end()
    applied = {int(m["version"]): {"hash": m["hash"], "personalUnchanged": m["personal"] == "1"}
               for m in CLIENT_PROGRESS.finditer(text)}
    problems, warnings, rooms = [], [], []
    for i, host in enumerate(host_rooms):
        got = arrivals.get(host["epoch"])
        last = i == len(host_rooms) - 1
        friend_tick = first_ticks(text[arrived_at[host["epoch"]]:]).get(host["epoch"]) if got is not None else None
        row = {"epoch": host["epoch"], "host": host["location"], "friend": got,
               "hostTickMs": host.get("tickMs"), "friendTickMs": friend_tick,
               "lagMs": friend_tick - host["tickMs"] if friend_tick is not None and host.get("tickMs") is not None
               else None}
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
        "crashes": crashes(text),
        "problems": problems, "warnings": warnings,
    }


def verdict(host_text: str, friend_texts: dict[str, str], relay_text: str = "",
            desync_dir: Path | None = None) -> dict:
    host_rooms = [{"epoch": int(m["epoch"]), "location": location(m),
                   "tickMs": first_ticks(host_text[m.end():]).get(int(m["epoch"]))}
                  for m in HOST_ARRIVED.finditer(host_text)]
    host_progress = [{"version": int(m["version"]), "kind": m["kind"], "hash": m["hash"]}
                     for m in HOST_PROGRESS.finditer(host_text)]
    result = {
        "host": {
            "rooms": host_rooms, "progress": host_progress,
            "travel": [f"{m['what']} {m['detail'].strip()}" for m in TRAVEL.finditer(host_text)],
            "holdsPublished": [{"active": m["active"] == "1", "epoch": int(m["epoch"])}
                               for m in HOLD_PUBLISHED.finditer(host_text)],
            "exceptions": host_text.count("EXCEPTION"),
            "crashes": crashes(host_text),
        },
        "friends": {name: friend_report(text, host_rooms, host_progress) for name, text in friend_texts.items()},
        "desync": [m["detail"].strip() for m in DESYNC.finditer(relay_text)],
    }
    published = {p["version"]: p["hash"] for p in host_progress}
    applies = {name: boundary_applies(text) for name, text in friend_texts.items()}
    details = desync_details(desync_dir)
    desync_problems, desync_warnings = [], []
    for m in DESYNC.finditer(relay_text):
        epoch, fields = int(m["epoch"]), int(m["fields"])
        detail = details.get((epoch, fields), {})
        text = (f"{m['detail'].strip()}" + (f" room={detail['room']} hostProgress={detail['hostProgress']} "
                f"friendProgress={detail['friendProgress']}" if detail else ""))
        converged = {name: next((a for a in rows if a["epoch"] is not None and a["epoch"] > epoch and
                                 published.get(a["version"]) == a["hash"]), None) for name, rows in applies.items()}
        held = admission_convergence(host_text, friend_texts, epoch, detail) if fields == DESYNC_PROGRESS else None
        race = (room_entry_race(host_text, friend_texts, applies, host_progress, epoch, detail)
                if fields == DESYNC_PROGRESS else None)
        if fields == DESYNC_PROGRESS and converged and all(converged.values()):
            first = min((a for a in converged.values()), key=lambda a: a["epoch"])
            desync_warnings.append(f"{text}; converged at the epoch {first['epoch']} boundary "
                                   f"(v{first['version']} hash {first['hash']})")
        elif race is not None:
            desync_warnings.append(f"{text}; room-entry race (VUH-2631): the host's first epoch {epoch} sample "
                                   f"still had its previous version's hash {race['stale']}, the friend applied "
                                   f"v{race['version']} hash {race['fresh']} at that boundary, and all "
                                   f"{race['hostSamples'] - 1} later host samples match it")
        elif held is not None and held <= ADMISSION_CONVERGE_MS:
            desync_warnings.append(f"{text}; combat-admission hold, converged {held} ms into host play "
                                   f"after admission-release (bound {ADMISSION_CONVERGE_MS} ms)")
        elif held is not None:
            desync_problems.append(f"{text}; combat-admission hold, converged {held} ms into host play "
                                   f"after admission-release, over the {ADMISSION_CONVERGE_MS} ms bound")
        else:
            desync_problems.append(text + ("; progress never converged" if fields == DESYNC_PROGRESS else ""))
    result["desyncWarnings"] = desync_warnings
    problems = [f"{name}: {p}" for name, f in result["friends"].items() for p in f["problems"]]
    problems += [f"{name}: {f['exceptions']} EXCEPTION lines" for name, f in result["friends"].items() if f["exceptions"]]
    if result["host"]["exceptions"]:
        problems.append(f"host: {result['host']['exceptions']} EXCEPTION lines")
    problems += [crash_problem("host", c) for c in result["host"]["crashes"]]
    problems += [crash_problem(name, c) for name, f in result["friends"].items() for c in f["crashes"]]
    if not host_rooms:
        problems.append("host published no room")
    problems += [f"relay desync: {d}" for d in desync_problems]
    result["problems"] = problems
    result["warnings"] = [f"{name}: {w}" for name, f in result["friends"].items() for w in f["warnings"]] +         [f"relay progress desync (converged): {w}" for w in desync_warnings]
    result["pass"] = not problems
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--friend", type=Path, action="append", required=True)
    parser.add_argument("--relay", type=Path)
    parser.add_argument("--desync-dir", type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args(argv)
    result = verdict(read(args.host), {p.name: read(p) for p in args.friend},
                     read(args.relay) if args.relay else "", args.desync_dir)
    text = json.dumps(result, indent=2)
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    print(text)
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
