"""VUH-2617 lead #302: per Thorn run, the revive windows and what the orb-volley hold-off did.

Usage: orbhold_report.py RUN_DIR [RUN_DIR ...]
Per run: kill / Thorn end HP / Creeper kills (creeper_report.py). Per revive window (a rescuer's `coop_revive
peer-logical-downed` until `coop_revive combat` or a stuck row):
- result (recovered, or the stuck reason), seconds, revive holds begun, closest approach (`coop_approach` horizontal);
- hold-offs (thorn_orbhold.py): `coop_orb_hold` episodes, their seconds and end reasons, and the partner's distance to
  Thorn when each began;
- revive-protect (thorn_protect.py, r5): `coop_rc_protected` episodes by command, RCs the core still took
  (`coop_reaction`) and why each revive hold ended (`coop_hold_end`);
- the rescuer's hits inside the window, by attack, with the orb (1041/1014) hits called out (rescue_report.hits). The
  window's frame span is the own-game frames of its `coop_approach` rows +-30 (the revive core keeps writing them while
  held off; `coop_orb_hold_sample` rows from r1 logs are read too).
"""
import collections
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import creeper_report  # noqa: E402
import rescue_report  # noqa: E402

ORB = rescue_report.ORB


def windows(run, seat, seat_hits):
    path = run / seat / "pilot.jsonl"
    if not path.exists():
        return []
    rows = [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines() if x.strip()]
    found, cur = [], None
    for r in rows:
        k = r.get("kind")
        if k == "coop_revive" and r.get("state") == "peer-logical-downed":
            cur = dict(rescuer=seat, t=r["t"], holds=0, minDist=None, frames=[], holdOffs=[], ends=collections.Counter(),
                       protected=collections.Counter(), rcTaken=0, holdEnds=collections.Counter())
            continue
        if cur is None:
            continue
        if k in ("coop_approach", "coop_orb_hold_sample"):
            frame = (r.get("observation") or {}).get("frame")
            if frame is not None:
                cur["frames"].append(frame)
            if k == "coop_approach":
                d = r.get("horizontal")
                cur["minDist"] = d if cur["minDist"] is None else min(cur["minDist"], d)
        elif k == "coop_hold_begin":
            cur["holds"] += 1
        elif k == "coop_hold_end":
            cur["holdEnds"][r.get("reason")] += 1
        elif k == "coop_rc_protected":
            cur["protected"][str(r.get("command"))] += 1
        elif k == "coop_reaction":
            cur["rcTaken"] += 1
        elif k == "coop_orb_hold":
            cur["holdOffs"].append(dict(partnerToThorn=r.get("partnerToThorn"), seconds=None))
        elif k == "coop_orb_hold_end":
            if cur["holdOffs"]:
                cur["holdOffs"][-1]["seconds"] = r.get("seconds")
            cur["ends"][r.get("reason")] += 1
        elif (k == "coop_revive" and r.get("state") == "combat") or k == "stuck":
            frames = cur.pop("frames")
            lo, hi = (min(frames) - 30, max(frames) + 30) if frames else (None, None)
            taken = collections.Counter(a for f, a in seat_hits if lo is not None and lo <= f <= hi)
            found.append(dict(
                rescuer=seat, result="recovered" if k == "coop_revive" else r.get("reason"),
                seconds=round(r["t"] - cur["t"], 1), holds=cur["holds"],
                minDist=round(cur["minDist"]) if cur["minDist"] is not None else None,
                holdOffs=len(cur["holdOffs"]), holdOffSeconds=round(sum(h["seconds"] or 0 for h in cur["holdOffs"]), 1),
                holdOffEnds=dict(cur["ends"]), partnerToThorn=[h["partnerToThorn"] for h in cur["holdOffs"]],
                protected=dict(cur["protected"]), rcTaken=cur["rcTaken"], holdEnds=dict(cur["holdEnds"]),
                orbHits=sum(v for a, v in taken.items() if a in ORB), hits=dict(taken), frames=[lo, hi]))
            cur = None
    return found


def analyse(run):
    base = creeper_report.analyse(run)
    logs = rescue_report.seat_logs(run)
    out = dict(run=run.name, kill=base["kill"], thornEnd=base["thornEnd"], creeperKills=base["creeperKills"],
               creepers=len(base["creepers"]), windows=[])
    for seat in ("host_thorn", "friend_thorn"):
        out["windows"] += windows(run, seat, rescue_report.hits(seat, logs.get(seat, [])))
        path = run / seat / "pilot.jsonl"
        for kind, key in (("rescue_orb_hold_summary", "Summary"), ("rescue_protect_summary", "Protect")):
            summary = [json.loads(x) for x in path.read_text(encoding="utf-8").splitlines()
                       if f'"{kind}"' in x] if path.exists() else []
            out[f"{seat}{key}"] = {k: v for k, v in summary[-1].items() if k not in ("t", "kind")} if summary else None
    return out


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        r = analyse(Path(arg))
        print(json.dumps(r))
        print(f"  {r['run']}: kill={r['kill']} thornEnd={r['thornEnd']} creeperKills={r['creeperKills']}/{r['creepers']}")
        for w in r["windows"]:
            print(f"    {w['rescuer']}: {w['result']} {w['seconds']}s holds={w['holds']} minDist={w['minDist']} "
                  f"holdOffs={w['holdOffs']} ({w['holdOffSeconds']}s, {w['holdOffEnds']}) protected={w['protected']} "
                  f"rcTaken={w['rcTaken']} holdEnds={w['holdEnds']} orbHits={w['orbHits']} hits={w['hits']}")
