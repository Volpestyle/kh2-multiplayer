"""Which seat a host boss targeted, and what that seat took natively (VUH-2912, Axel II).

Reads the host and friend inject logs of a run with KH2COOP_TRACE_HITS=1 and KH2COOP_ENEMY_MIRROR_TRACE=1 on both
seats, the boss lifetime the boss HP ledger pinned (`boss`: epoch, hostActor, friendActor), and line marks saved as
`[log_count('', host), log_count('', friend)]`.

The host logs `[enemy-target] boss-target ... target=<k> owner=<slot> valid=<mask> changed=<0|1> ... tickMs=<t>`
whenever the boss changes its chosen candidate or its candidate set (candidate 0 is the host's player, k > 0 a remote
clone; valid bit k: candidate k was eligible at that search). The last such line before a moment is therefore the
selection in force then, until the inject's budget of 256 lines is spent. Each seat logs `[hitwitness]
seq=<hittrace seq> hitTickMs=<t> ... reads=<mask>` a few frames after a boss hit lowered its own canonical player's
HP: the motion and ignore-hit timer (+0xD70) before the hit and then, with one bit per successful read.

Order. Two events in one seat's log are ordered by line. Across seats they are ordered only by GetTickCount64
(`tickMs`, one clock for every process on the PC): a strictly lower tick is earlier, and an equal or missing tick
leaves the order unknown. An order this ledger needs that is unknown fails as unproven; it never passes.

Rounds (`rounds`: name, from, to, seat). The selection in force when the round starts, or one set inside its host
lines, must name that seat. Within the seat's own lines its canonical player must take a qualifying native hit from
its copy of the pinned boss:
- a complete hittrace scope (the HP ledger's rules) with victim class LocalAvatar, action Native and HP lost;
- its witness: all four reads made (reads=15, no 0xFFFFFFFF motion), the same victim still canonical, a changed
  motion (the reaction) and a running ignore-hit timer. For Axel II only (Ione 5fb4a767: his hits on a player never
  run the timer), a witness read on its own frame (hitFrame + 4) whose motion entered the pinned damage set from
  outside it stands instead, reported as `damageMotion`: damage-motion evidence, not an i-frame test. A hit landing
  while the seat is already in a damage motion (18->18, 11->10) does not qualify, however recent its last reaction;
  the HP ledger still counts it. Or, for a hit that downed the seat (on PC1's 5-HP seats every Axel hit does), a
  changed motion and that seat's own `[downed] gate intercept` for the same actor within two frames: lethal-hit
  evidence, reported as `downing`, which does not test i-frames;
- on the friend, its `[enemy-mirror] hit` line on the same frame, victim and atkp from the bound boss copy with
  driven=1 and an attack motion the mirror entered mid-run (runStart < setFrame <= frame, setMotion == motion): the
  host's motion, not a carried local one;
- the selection in force at the hit names that seat: the last boss-target line definitely before the hit names it,
  and so does every line whose order against the hit is unknown.

Downs (`downs`: from, to). Each seat's own `[downed] gate intercept` ... `[downed] revive returned` in the window is
one down interval. Each seat must have at least one. A boss-target line definitely inside each interval must show
that seat's candidate bit clear. No line naming the seat may fall in the interval once `deliveryMs` (default 1000)
has passed: a line that is neither definitely before the down plus delivery nor definitely after the revive counts.

Anywhere in the host log, a boss-target line that chooses a candidate whose valid bit is clear is a failure.

    python -B tools/scenario/boss_target_ledger.py --host H.log --friend F.log --object 81 --marks marks.json

marks.json: {"marks": {...}, "boss": {"epoch": 3, "hostActor": "...", "friendActor": "..."}, "rounds": [{"name":
"friend_target", "from": "m0", "to": "m1", "seat": 1}], "downs": {"from": "d0", "to": "d1"}}. Exit 0 on pass.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import re
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location("kh2coop_boss_hp_ledger_for_targets",
                                               Path(__file__).with_name("boss_hp_ledger.py"))
L = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(L)

TARGET = re.compile(r"\[enemy-target\] boss-target frame=(?P<frame>\d+) enemy=(?P<enemy>[0-9A-F]+) objectId=(?P<objectId>\d+) "
                    r"target=(?P<target>-?\d+) owner=(?P<owner>\d+) mode=(?P<mode>\d+) valid=(?P<valid>\d+) changed=(?P<changed>\d)"
                    r"(?: at=\S+)?(?: tickMs=(?P<tickMs>\d+))?")
WITNESS = re.compile(r"\[hitwitness\] seq=(?P<seq>\d+) hitFrame=(?P<hitFrame>\d+) hitTickMs=(?P<tickMs>\d+) frame=(?P<frame>\d+) "
                     r"attacker=(?P<attacker>[0-9A-F]+) victim=(?P<victim>[0-9A-F]+) atkp=(?P<atkp>\d+) "
                     r"hp=(?P<hp0>-?\d+)->(?P<hp1>-?\d+) motion=(?P<m0>\d+)->(?P<m1>\d+) "
                     r"ignoreHit=(?P<i0>-?[\d.]+)->(?P<i1>-?[\d.]+) reads=(?P<reads>\d+) same=(?P<same>\d)")
ALL_READS, UNREAD_MOTION = 15, str(0xFFFFFFFF)  # motion and ignore-hit timer, before and after; the failed-read value
WITNESS_FRAMES = 4  # inject/src/EntityHook.cpp kWitnessFrames: the witness reads the motion this many frames after the hit
AXEL_II = 81  # common/include/kh2coop/EnemyClass.hpp AxelII (B_EX110)
# The damage motions a boss's hit puts a player in, pinned per boss. Axel II: OpenKh.Kh2/MotionSet.cs (OpenKH/OpenKh
# b67a7434) DAMAGE_S_FRONT_HIGH 10, DAMAGE_S_FRONT_LOW 11, DAMAGE_S_BACK_HIGH 12, DAMAGE_S_BACK_LOW 13,
# DAMAGE_BLOW_FRONT 16 and DAMAGE_BLOW_BACK 18: the reactions his witnessed hits on players showed (hp-09 and hp-10;
# 12 in five earlier Axel HP runs on 2026-10-28).
DAMAGE_MOTIONS = {AXEL_II: frozenset({10, 11, 12, 13, 16, 18})}
MIRROR_HIT = re.compile(r"\[enemy-mirror\] hit frame=(?P<frame>\d+) netId=(?P<netId>\d+) actor=(?P<actor>[0-9A-F]+) "
                        r"driven=(?P<driven>-?\d+) runStart=(?P<runStart>\d+) runFrames=\d+ motion=(?P<motion>\d+) time=\S+ "
                        r"setMotion=(?P<setMotion>-?\d+) setFrame=(?P<setFrame>\d+) cursor=\S+ atkp=(?P<atkp>\d+) "
                        r"victim=\S*?@(?P<victim>[0-9A-F]+) damage=(?P<damage>-?\d+)")
DOWNING = re.compile(r"\[downed\] gate intercept mode=\d+ (?:logical=1 )?episode=[0-9A-F]+ actor=(?P<actor>[0-9A-F]+) .*?frame=(?P<frame>\d+)")
DOWN, REVIVED = "[downed] gate intercept", "[downed] revive returned"
TICK_MS = re.compile(r" tickMs=(\d+)")
BOSS_LOG_BUDGET = 256  # inject/src/EnemyTargetRemote.inl: boss-target lines per process


def line_tick(text: str) -> int | None:
    m = TICK_MS.search(text)
    return int(m[1]) if m else None


def order(a: dict, b: dict) -> int:
    """-1 if event a was definitely before b, 1 if definitely after, 0 if unknown. Events are {seat, line, tickMs}."""
    if a["seat"] == b["seat"]:
        return (a["line"] > b["line"]) - (a["line"] < b["line"])
    if a["tickMs"] is None or b["tickMs"] is None or a["tickMs"] == b["tickMs"]:
        return 0
    return -1 if a["tickMs"] < b["tickMs"] else 1


def same_actor(a, b) -> bool:
    return a is not None and b is not None and int(str(a), 16) == int(str(b), 16)


def ledger(host_text: str, friend_text: str, objectId: int, marks: dict) -> dict:
    lines = {0: host_text.splitlines(), 1: friend_text.splitlines()}
    m, boss = marks["marks"], marks["boss"]
    problems: list[str] = []
    at = lambda seat, n: {"seat": seat, "line": n, "tickMs": line_tick(lines[seat][n]) if n < len(lines[seat]) else None}

    every = [n for n, text in enumerate(lines[0]) if TARGET.search(text)]
    choices = [{**r.groupdict(), "seat": 0, "line": n} for n, text in enumerate(lines[0]) for r in [TARGET.search(text)]
               if r and int(r["objectId"]) == objectId]
    for c in choices:
        for k in ("target", "owner", "valid", "changed", "frame"):
            c[k] = int(c[k])
        c["tickMs"] = int(c["tickMs"]) if c["tickMs"] else None
    # Once the budget is spent the inject logs no more changes, so the selection after its last line is unknown.
    spent = at(0, every[BOSS_LOG_BUDGET - 1]) if len(every) >= BOSS_LOG_BUDGET else None
    unlogged = lambda ev: spent is not None and order(ev, spent) != -1
    # Candidate index of each owner slot, from the choices that name it (the host's player is candidate 0).
    index = {0: {0}, 1: {c["target"] for c in choices if c["owner"] == 1 and c["target"] > 0}}
    if len(index[1]) > 1:
        problems.append(f"the friend's seat appears as several candidates {sorted(index[1])}")
    names = lambda c, seat: c["target"] >= 0 and c["owner"] == seat and (c["target"] == 0) == (seat == 0)
    for c in choices:
        if c["target"] >= 0 and not c["valid"] >> c["target"] & 1:
            problems.append(f"host line {c['line']}: boss chose candidate {c['target']} whose valid bit is clear (valid={c['valid']})")

    witnesses = {seat: {int(w["seq"]): {**w.groupdict(), "line": n} for n, text in enumerate(lines[seat])
                        for w in [WITNESS.search(text)] if w} for seat in (0, 1)}
    downings = {seat: [d.groupdict() for text in lines[seat] for d in [DOWNING.search(text)] if d] for seat in (0, 1)}
    mirror_hits = [{**h.groupdict(), "line": n} for n, text in enumerate(lines[1]) for h in [MIRROR_HIT.search(text)] if h]
    copy = {0: boss.get("hostActor"), 1: boss.get("friendActor")}
    damage = DAMAGE_MOTIONS.get(objectId, frozenset())

    def damage_motion(w: dict) -> str | None:
        """Why the witness shows no fresh entry into the boss's damage motions, or None."""
        m0, m1 = int(w["m0"]), int(w["m1"])
        if m1 not in damage:
            return f"{m1} is not a damage motion"
        if m0 in damage:
            return f"held from damage motion {m0}"
        if int(w["frame"]) - int(w["hitFrame"]) != WITNESS_FRAMES:
            return f"read {int(w['frame']) - int(w['hitFrame'])} frames after the hit, not {WITNESS_FRAMES}"
        return None

    def qualify(seat: int, e: dict) -> str | None:
        """Why this native hit on the seat does not qualify, or None."""
        w = witnesses[seat].get(e["seq"])
        if not w:
            return "no witness"
        if int(w["reads"]) != ALL_READS or UNREAD_MOTION in (w["m0"], w["m1"]):
            return f"witness reads failed (reads={w['reads']}, motion {w['m0']}->{w['m1']})"
        if not same_actor(w["victim"], e["victimActor"]) or w["same"] != "1":
            return "witness is for another victim or the player changed"
        downing = any(same_actor(d["actor"], w["victim"]) and 0 <= int(d["frame"]) - int(w["hitFrame"]) <= 2
                      for d in downings[seat])
        fresh = damage_motion(w) if damage else "no damage motions pinned"
        if w["m1"] != w["m0"] and float(w["i1"]) > 0:
            e["kind"] = "reaction"
        elif fresh is None:
            e["kind"] = "damageMotion"
        elif w["m1"] != w["m0"] and downing:
            e["kind"] = "downing"
        else:
            return (f"no reaction/i-frames (motion {w['m0']}->{w['m1']}, ignoreHit {w['i0']}->{w['i1']})"
                    + (f"; no fresh damage motion: {fresh}" if damage else ""))
        if seat == 1:
            hit = [h for h in mirror_hits if h["frame"] == w["hitFrame"] and same_actor(h["victim"], w["victim"]) and
                   same_actor(h["actor"], copy[1]) and h["atkp"] == w["atkp"]]
            if not hit:
                return "no mirror hit line from the bound boss copy on that frame"
            h = hit[-1]
            if not (h["driven"] == "1" and h["setMotion"] == h["motion"] and
                    int(h["runStart"]) < int(h["setFrame"]) <= int(h["frame"])):
                return f"mirror hit not on a driven host motion ({h})"
        hit = {"seat": seat, "line": e["line"], "tickMs": int(w["tickMs"])}
        if unlogged(hit):
            return "after the boss-target budget was spent: the selection at the hit is unknown"
        prior = [c for c in choices if order(c, hit) == -1]
        if not prior or not names(prior[-1], seat):
            return "not after the boss chose this seat"
        unordered = [c["line"] for c in choices if order(c, hit) == 0 and not names(c, seat)]
        if unordered:
            return f"selection at the hit unproven: host lines {unordered} are not ordered against it"
        return None

    rounds = []
    for r in marks.get("rounds", []):
        seat = r["seat"]
        (h0, f0), (h1, f1) = m[r["from"]], m[r["to"]]
        lo, hi = (h0, h1) if seat == 0 else (f0, f1)
        in_force = [c for c in choices if c["line"] < h0][-1:]  # the selection the round started with
        chose = [c for c in in_force + [c for c in choices if h0 <= c["line"] < h1] if names(c, seat)]
        hits = [e for e in L.hit_events(lines[seat], lo, hi) if not e["invalid"] and e["sourceObject"] == objectId and
                same_actor(e["sourceActor"], copy[seat]) and e["epoch"] == boss.get("epoch") and
                e["victimClass"] == L.LOCAL_AVATAR and e["action"] == L.NATIVE and e["hpBefore"] > e["hpAfter"]]
        verdicts = [{"seq": e["seq"], "line": e["line"], "hp": [e["hpBefore"], e["hpAfter"]],
                     "why": qualify(seat, e), "kind": e.get("kind")} for e in hits]
        row = {"name": r["name"], "seat": seat, "choices": len(chose), "nativeHits": len(hits),
               "qualified": [v for v in verdicts if v["why"] is None], "rejected": [v for v in verdicts if v["why"]]}
        if not chose:
            problems.append(f"round {r['name']}: the boss never chose seat {seat} in the round")
        if not row["qualified"]:
            problems.append(f"round {r['name']}: no qualifying native hit on seat {seat} "
                            f"({len(hits)} native hits: {[v['why'] for v in verdicts][:3]})")
        rounds.append(row)

    downs = []
    if "downs" in marks:
        (h0, f0), (h1, f1) = m[marks["downs"]["from"]], m[marks["downs"]["to"]]
        delivery = marks.get("deliveryMs", 1000)
        for seat, (lo, hi) in ((0, (h0, h1)), (1, (f0, f1))):
            intervals, start = [], None
            for n in range(lo, hi):
                if DOWN in lines[seat][n] and start is None:
                    start = n
                elif REVIVED in lines[seat][n] and start is not None:
                    intervals.append((start, n))
                    start = None
            if start is not None:
                intervals.append((start, hi))
            if not intervals:
                problems.append(f"seat {seat} was never downed in the downs window")
            bits = index[seat]
            for a, b in intervals:
                down, up = at(seat, a), at(seat, b)

                def before_delivery(c):  # definitely before the down's pose can have reached the host's search
                    if c["tickMs"] is not None and down["tickMs"] is not None:
                        return c["tickMs"] < down["tickMs"] + delivery
                    return order(c, down) == -1

                during = [c for c in choices if order(down, c) == -1 and order(c, up) == -1]
                excluded = [c for c in during if bits and all(not c["valid"] >> k & 1 for k in bits)]
                chosen = [c for c in choices if names(c, seat) and not before_delivery(c) and order(c, up) != 1]
                downs.append({"seat": seat, "lines": [a, b], "ticks": [down["tickMs"], up["tickMs"]],
                              "observed": len(during), "excluded": len(excluded),
                              "chosenWhileDown": [c["line"] for c in chosen]})
                if unlogged(up):
                    problems.append(f"seat {seat} down at line {a}: after the boss-target budget was spent")
                if not excluded:
                    problems.append(f"seat {seat} down at line {a}: no boss search definitely inside it showed it "
                                    "out of the candidates")
                if chosen:
                    problems.append(f"seat {seat} down at line {a}: the boss chose it "
                                    f"(host lines {[c['line'] for c in chosen]})")

    return {"pass": not problems, "problems": problems, "objectId": objectId, "boss": boss,
            "candidates": {str(k): sorted(v) for k, v in index.items()}, "rounds": rounds, "downs": downs,
            "choices": [{k: c[k] for k in ("line", "tickMs", "frame", "target", "owner", "valid", "changed")} for c in choices]}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", required=True, type=Path)
    ap.add_argument("--friend", required=True, type=Path)
    ap.add_argument("--object", required=True, type=int)
    ap.add_argument("--marks", required=True, type=Path)
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    read = lambda p: p.read_text(encoding="utf-8-sig", errors="replace")
    result = ledger(read(a.host), read(a.friend), a.object, json.loads(read(a.marks)))
    if a.out:
        a.out.write_text(json.dumps(result, indent=2), encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("pass", "problems", "rounds", "downs")}, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
