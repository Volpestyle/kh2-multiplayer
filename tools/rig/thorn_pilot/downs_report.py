"""Lead #200: every logical down in a Thorn run, for the wipe-chain question.

Usage: downs_report.py RUN_DIR
For each down: the seat, the killing hit (host: last [hittrace]/[damagepolicy] input on that actor with its
attackId/damage/owner and the [hittrace] event callerRva of the same seq; client: the [enemy-mirror] hit on the
local player, or a [hittrace] input if present), both seats' last observed HP, what the surviving seat was doing
in the 3 s before (reviving / reserve / attacking / other), and the time until the next down of the other seat.
Downs are matched in order: the nth [downed] gate intercept in a seat's inject log is that seat's nth
coop_revive own-logical-downed row.
"""
import json
import re
import sys
from pathlib import Path

run = Path(sys.argv[1])
seats = {}
for s in ("host_thorn", "friend_thorn"):
    seats[s] = [json.loads(l) for l in (run / s / "pilot.jsonl").read_text(encoding="utf-8").splitlines() if l.strip()]
t0 = min(rows[0]["t"] for rows in seats.values())

logs = {}
for f in run.glob("kh2coop_inject_*.log"):
    text = f.read_text(encoding="utf-8", errors="replace").splitlines()
    if any(l.startswith("[thorn-trace] host") for l in text[:20000]):
        logs["host_thorn"] = text
    elif any(l.startswith("[thorn-trace] client") for l in text[:20000]):
        logs["friend_thorn"] = text

field = lambda line, key: (re.search(rf"\b{key}=([^\s]+)", line) or [None, None])[1]


def killing_hit(lines, i, actor):
    """Search back from the intercept at index i for the hit that downed `actor`."""
    for j in range(i - 1, max(0, i - 400), -1):
        l = lines[j]
        if l.startswith("[enemy-mirror] hit") and "victim=P_" in l:
            return f"mirror atkp={field(l, 'atkp')} dmg={field(l, 'damage')} thornMotion={field(l, 'motion')}"
        if l.startswith(("[hittrace] input", "[damagepolicy] input")) and field(l, "tracked") == actor \
                and (field(l, "damage") or "0") != "0":
            seq = field(l, "seq")
            caller = next((field(x, "callerRva") for x in lines[max(0, j - 30):j + 30]
                           if x.startswith("[hittrace] event") and field(x, "seq") == seq), None)
            owner = field(l, "owner")
            return f"attackId={field(l, 'attackId')} dmg={field(l, 'damage')} kind={field(l, 'kind')} owner={owner} callerRva={caller}"
    return "hit not found in log window"


downs = []
for s, rows in seats.items():
    times = [r["t"] for r in rows if r["kind"] == "coop_revive" and r.get("state") == "own-logical-downed"]
    # Collapse repeated rows of the same episode (rows recur while it lasts).
    starts = [t for k, t in enumerate(times) if k == 0 or t - times[k - 1] > 5]
    lines = logs.get(s, [])
    inter = [(i, l) for i, l in enumerate(lines) if l.startswith("[downed] gate intercept")]
    for k, t in enumerate(starts):
        hit = "no inject log"
        if k < len(inter):
            i, l = inter[k]
            actor = (field(l, "actor") or "").lstrip("0")
            actor = "7FF" + actor[3:] if actor.startswith("7FF") else actor
            hit = f"hp={field(l, 'hp')} delta={field(l, 'delta')} frame={field(l, 'frame')}; " + \
                  killing_hit(lines, i, field(l, "actor") or "")
        downs.append((t, s, hit))
downs.sort()


def last_hp(rows, t):
    hp = None
    for r in rows:
        if r["t"] > t:
            break
        if r["kind"] == "item_slots":
            h = r.get("hp")
        elif r["kind"] == "thorn_observe" and isinstance(r.get("state"), dict):
            h = r["state"].get("hp")
        else:
            continue
        if isinstance(h, int):
            hp = h
    return hp


RESCUE = ("coop_hold_begin", "coop_approach", "coop_detour", "coop_stuck_escape", "coop_reaction")


def onset(rows, t):
    """The survivor's pilot sees a peer down from the inject log, often before the downed pilot writes its
    own row; its first rescue row in the 5 s before t marks the down more exactly."""
    early = [r["t"] for r in rows if t - 5 <= r["t"] <= t and r["kind"] in RESCUE]
    return min(early + [t])


def doing(rows, t):
    # Strictly before the down's onset.
    w = [r for r in rows if t - 3 <= r["t"] < t - 0.1]
    if any(r["kind"] in RESCUE for r in w):
        return "reviving/approaching"
    if any(r["kind"] == "thorn_reserve" for r in w):
        return "reserve"
    if any(r["kind"] in ("heal", "pre_revive_heal", "heal_repair") for r in w):
        return "healing"
    if any(r["kind"] == "thorn_swing" and r.get("sent") for r in w):
        return "attacking"
    if any(r["kind"] == "thorn_head_approach" for r in w):
        return "closing on Thorn"
    if any(r["kind"] == "thorn_swing" for r in w):
        return "holding swings (yield)"
    return "other"


for n, (t, s, hit) in enumerate(downs):
    other = "friend_thorn" if s == "host_thorn" else "host_thorn"
    nxt = next((t2 for t2, s2, _ in downs[n + 1:] if s2 == other), None)
    print(f"down +{t - t0:.1f}s {s}: {hit}")
    t_on = onset(seats[other], t)
    print(f"    hp {s}={last_hp(seats[s], t_on)} {other}={last_hp(seats[other], t_on)}; "
          f"{s} itself was {doing(seats[s], t_on)}, {other} was {doing(seats[other], t_on)}; "
          f"next {other} down {'+%.1fs later' % (nxt - t) if nxt else 'none'}")
