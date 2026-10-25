"""VUH-2617 lead #282: Creeper-first measures per Thorn run.

Usage: creeper_report.py RUN_DIR [RUN_DIR ...]
Per run: the result (fight_outcome-style kill/Thorn end from the host statehash), each Creeper (objectId 317) from the
host statehash (first/last HP, killed = a row at HP 0, or listed rows ending at HP <= 0), host downs by Creeper
attack 400 vs all host downs (downs_report.py's killing hit), revive exposures (each seat's `coop_revive
peer-logical-downed` episodes), and the host pilot's `creeper_turn` actions and `creeper_summary` (when present).
"""
import collections
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def rows(path):
    return [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()]


def host_log(run):
    for f in run.glob("kh2coop_inject_*.log"):
        head = f.read_text(encoding="utf-8", errors="replace")[:400000]
        if "role=client" not in head:
            return f
    return None


def analyse(run):
    out = {"run": run.name}
    host = host_log(run)
    text = host.read_text(encoding="utf-8", errors="replace") if host else ""
    hp = collections.defaultdict(list)  # (objectId, netId) -> [(frame, hp)]
    for m in re.finditer(r"\[statehash\] native epoch=\d+ frame=(\d+) netId=(\d+) objectId=(\d+) hp=(-?\d+)", text):
        frame, net, obj, value = map(int, m.groups())
        if obj in (307, 317):
            hp[(obj, net)].append((frame, value))
    thorn = [v for (o, _), series in hp.items() if o == 307 for v in series]
    out["thornEnd"] = thorn[-1][1] if thorn else None
    out["kill"] = bool(thorn) and min(v for _, v in thorn) <= 0
    creepers = []
    for (o, net), series in sorted(hp.items()):
        if o != 317:
            continue
        creepers.append(dict(netId=net, firstHp=series[0][1], lastHp=series[-1][1],
                             killed=min(v for _, v in series) <= 0, frames=series[-1][0] - series[0][0]))
    out["creepers"] = creepers
    out["creeperKills"] = sum(c["killed"] for c in creepers)
    downs = subprocess.run([sys.executable, "-B", str(HERE / "downs_report.py"), str(run)],
                           capture_output=True, text=True).stdout
    host_downs = re.findall(r"^down \S+ host_thorn: .*?attackId=(\d+)", downs, re.M)
    out["hostDowns"] = len(host_downs)
    out["hostDownsByCreeper400"] = sum(a == "400" for a in host_downs)
    exposures = {}
    for seat in ("host_thorn", "friend_thorn"):
        path = run / seat / "pilot.jsonl"
        exposures[seat] = sum(1 for r in rows(path) if r.get("kind") == "coop_revive"
                              and r.get("state") == "peer-logical-downed") if path.exists() else None
    out["reviveExposures"] = exposures
    path = run / "host_thorn" / "pilot.jsonl"
    host_rows = rows(path) if path.exists() else []
    out["creeperTurns"] = dict(collections.Counter(r["action"] for r in host_rows if r.get("kind") == "creeper_turn"))
    summary = [r for r in host_rows if r.get("kind") == "creeper_summary"]
    out["creeperSummary"] = {k: v for k, v in summary[-1].items() if k not in ("t", "kind")} if summary else None
    return out


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        r = analyse(Path(arg))
        print(json.dumps(r))
        print(f"  {r['run']}: kill={r['kill']} thornEnd={r['thornEnd']} creeperKills={r['creeperKills']}/{len(r['creepers'])} "
              f"hostDowns={r['hostDowns']} (atk400 {r['hostDownsByCreeper400']}) reviveExposures={r['reviveExposures']} "
              f"creeperTurns={r['creeperTurns']}")
