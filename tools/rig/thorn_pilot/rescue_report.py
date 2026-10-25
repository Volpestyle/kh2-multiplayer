"""VUH-2617 lead #291: per Thorn run, the revive windows and what the no-throwback rescue changed.

Usage: rescue_report.py RUN_DIR [RUN_DIR ...]
Per run: kill / Thorn end HP / Creeper kills (creeper_report.py). Per revive window (a rescuer's `coop_revive
peer-logical-downed` until `coop_revive combat` or a stuck row):
- result (recovered, or the stuck reason), seconds, revive holds begun, closest approach (`coop_approach` horizontal);
- suppressed Reversals: `coop_reversal_suppressed` episodes by command (thorn_rescue.py), including the first tick's
  row, which the mixin writes just before the core's window marker;
- throwbacks still taken: own RC completions (`coop_approach_reset own-callback-consumed`) of 432/426 whose press burst
  started within 600 of the downed seat and whose next approach row (>= 0.5 s later) was > 200 farther, one per burst;
- the rescuer's hits inside the window, by attack (host: `[hittrace] input phase=0` on its own actor, placed at the frame
  of the last `[thorn-trace] host` row above it; friend: `[enemy-mirror] hit` on its player, by client frame), with the
  orb (1041/1014) hits called out. The window's frame span is its `coop_approach` rows' own-game frames +-30.
"""
import collections
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import creeper_report  # noqa: E402

F = lambda line, key: (re.search(rf"\b{key}=([^\s;]+)", line) or [None, None])[1]
ORB = ("1041", "1014")
SAME_TICK = 0.5  # s: a suppression row this close before a window's marker was written by the window's first tick


def seat_logs(run):
    logs = {}
    for f in run.glob("kh2coop_inject_*.log"):
        lines = f.read_text(encoding="utf-8", errors="replace").splitlines()
        head = lines[:20000]
        if any(l.startswith("[thorn-trace] host") for l in head):
            logs["host_thorn"] = lines
        elif any(l.startswith("[thorn-trace] client") for l in head):
            logs["friend_thorn"] = lines
    return logs


def hits(seat, lines):
    """[(own frame, attack id)] of damage taken by this seat's player."""
    out = []
    if seat == "host_thorn":
        frame, actor = None, None
        for l in lines:
            if l.startswith("[thorn-trace] host ") and F(l, "frame"):
                frame = int(F(l, "frame"))
            elif l.startswith("[downed] gate intercept") and F(l, "actor"):
                actor = F(l, "actor")
            elif l.startswith("[hittrace] input ") and F(l, "phase") == "0" and (F(l, "damage") or "0") != "0" \
                    and F(l, "tracked") == F(l, "canonicalPlayer") and frame is not None:
                out.append((frame, F(l, "attackId")))
    else:
        for l in lines:
            if l.startswith("[enemy-mirror] hit ") and "victim=P_" in l and F(l, "frame"):
                out.append((int(F(l, "frame")), F(l, "atkp")))
    return out


def windows(run, seat, seat_hits):
    path = run / seat / "pilot.jsonl"
    if not path.exists():
        return []
    rows = [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines() if x.strip()]
    found, cur, start, early = [], None, 0, []
    for j, r in enumerate(rows):
        k = r.get("kind")
        if k == "coop_revive" and r.get("state") == "peer-logical-downed":
            cur, start = dict(rescuer=seat, t=r["t"], holds=0, minDist=None, suppressed=collections.Counter(),
                              frames=[], bursts={}), j
            # thorn_rescue's mixin runs above the revive core, so on a window's first tick its suppression row is
            # written just before the core's window marker: it belongs to this window (Sage, VUH-2617 26c5559c).
            for x in early:
                if r["t"] - x["t"] <= SAME_TICK:
                    cur["suppressed"][str(x.get("command"))] += 1
            early = []
            continue
        if cur is None:
            if k == "coop_reversal_suppressed":
                early.append(r)
            continue
        if k == "coop_approach":
            d = r.get("horizontal")
            cur["minDist"] = d if cur["minDist"] is None else min(cur["minDist"], d)
            frame = (r.get("observation") or {}).get("frame")
            if frame is not None:
                cur["frames"].append(frame)
        elif k == "coop_hold_begin":
            cur["holds"] += 1
        elif k == "coop_reversal_suppressed":
            cur["suppressed"][str(r.get("command"))] += 1
        elif k == "coop_approach_reset" and r.get("reason") == "own-callback-consumed":
            back = rows[start:j]
            burst = [x for x in back if x["kind"] == "coop_reaction" and r["t"] - x["t"] <= 1.5]
            first = burst[0]["t"] if burst else r["t"]
            before = [x for x in back if x["kind"] == "coop_approach" and x["t"] <= first]
            after = next((x for x in rows[j:] if x["kind"] == "coop_approach" and x["t"] >= r["t"] + 0.5), None)
            command = burst[-1]["command"] if burst else None
            if before and after and after["t"] - r["t"] <= 2.0 and command in (432, 426) \
                    and before[-1]["horizontal"] <= 600:
                key = before[-1]["t"]
                cur["bursts"][key] = max(cur["bursts"].get(key, -1e9), after["horizontal"] - before[-1]["horizontal"])
        elif (k == "coop_revive" and r.get("state") == "combat") or k == "stuck":
            frames = cur.pop("frames")
            lo, hi = (min(frames) - 30, max(frames) + 30) if frames else (None, None)
            taken = collections.Counter(a for f, a in seat_hits if lo is not None and lo <= f <= hi)
            bursts = cur.pop("bursts")
            found.append(dict(
                rescuer=seat, result="recovered" if k == "coop_revive" else r.get("reason"),
                seconds=round(r["t"] - cur["t"], 1), holds=cur["holds"],
                minDist=round(cur["minDist"]) if cur["minDist"] is not None else None,
                suppressed=dict(cur["suppressed"]), throwbacks=sum(v > 200 for v in bursts.values()),
                orbHits=sum(v for a, v in taken.items() if a in ORB), hits=dict(taken), frames=[lo, hi]))
            cur = None
    return found


def analyse(run):
    base = creeper_report.analyse(run)
    logs = seat_logs(run)
    out = dict(run=run.name, kill=base["kill"], thornEnd=base["thornEnd"], creeperKills=base["creeperKills"],
               creepers=len(base["creepers"]), hostDownsByCreeper400=base["hostDownsByCreeper400"], windows=[])
    for seat in ("host_thorn", "friend_thorn"):
        out["windows"] += windows(run, seat, hits(seat, logs.get(seat, [])))
        path = run / seat / "pilot.jsonl"
        summary = [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines()
                   if '"rescue_no_reversal_summary"' in x] if path.exists() else []
        out[f"{seat}Summary"] = {k: v for k, v in summary[-1].items() if k not in ("t", "kind")} if summary else None
    return out


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        r = analyse(Path(arg))
        print(json.dumps(r))
        print(f"  {r['run']}: kill={r['kill']} thornEnd={r['thornEnd']} creeperKills={r['creeperKills']}/{r['creepers']}")
        for w in r["windows"]:
            print(f"    {w['rescuer']}: {w['result']} {w['seconds']}s holds={w['holds']} minDist={w['minDist']} "
                  f"suppressed={w['suppressed']} throwbacks={w['throwbacks']} orbHits={w['orbHits']} hits={w['hits']}")
