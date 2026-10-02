"""Find combat rooms on the current save (VUH-1499 prep).

For each world/room: warp one rig instance there (default programs), keep
Sora untouchable, run forward, and count enemies (objentry type 3/4). A
warp the safe-state gate holds (cutscene, event) costs a reboot. Writes
build/scenarios/combat_rooms.json.

    python tools/scenario/spikes/combat_rooms.py --worlds 2,4,5,6,7,8 --rooms 0-15
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run  # noqa: E402


def parse_range(text: str) -> list[int]:
    out = []
    for part in text.split(","):
        if "-" in part:
            a, b = part.split("-")
            out += range(int(a, 0), int(b, 0) + 1)
        else:
            out.append(int(part, 0))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--worlds", default="2,4,5,6,7,8,10,11,12,14,16,17,18")
    ap.add_argument("--rooms", default="0-15")
    ap.add_argument("--btl", default="", help="battle programs to force, e.g. 1-3 (default: the save's)")
    args = ap.parse_args()

    lock = run.RigLock()
    problem = lock.acquire()
    if problem:
        print(problem)
        return run.EXIT_RIG
    out_path = run.RUNS / "combat_rooms.json"
    results = json.loads(out_path.read_text()) if out_path.exists() else {}
    run_dir = run.RUNS / f"{time.strftime('%Y%m%d-%H%M%S')}_combat_rooms"
    run_dir.mkdir(parents=True, exist_ok=True)
    ctx = run.Context(run_dir)
    try:
        def fresh() -> None:
            for inst in ctx.instances:
                run.kh2ctl("kill", pid=inst.pid, check=False)
            ctx.instances.clear()
            ctx.protect.clear()
            run.step_boot(ctx, {})
            ctx.protect.add(0)

        fresh()
        btls = parse_range(args.btl) if args.btl else [None]
        for world, room, btl in ((w, r, b) for w in parse_range(args.worlds)
                                 for r in parse_range(args.rooms) for b in btls):
                key = f"{world:02X}/{room:02X}" + (f":btl={btl}" if btl is not None else "")
                if key in results:
                    continue
                entry = {"world": world, "room": room, "btl": btl}
                try:
                    extra = ["--btl", str(btl)] if btl is not None else []
                    data = run.kh2ctl("warp", "--world", str(world), "--room", str(room), *extra,
                                      "--timeout-ms", "12000", pid=ctx.instances[0].pid, check=False, timeout=60)
                    if not data.get("ok"):
                        entry["status"] = "held" if "gate" in str(data) else "failed"
                        entry["error"] = str(data.get("error", ""))[:200]
                        results[key] = entry
                        print(key, entry["status"], flush=True)
                        fresh()
                        continue
                    seen: dict[str, dict] = {}
                    for _ in range(3):
                        run.kh2ctl("player-input", "--ly", "1", "--duration-ms", "900", pid=ctx.instances[0].pid,
                                   check=False)
                        for a in ctx.actors(0):
                            if a.get("objectType") in (3, 4):
                                seen.setdefault(a["address"], {"name": a["name"], "type": a["objectType"]})
                    ctx.sleep(2)
                    for a in ctx.actors(0):
                        if a.get("objectType") in (3, 4):
                            seen.setdefault(a["address"], {"name": a["name"], "type": a["objectType"]})
                    entry.update(status="ok", enemies=len(seen),
                                 names=sorted({v["name"] for v in seen.values()}),
                                 bosses=sum(v["type"] == 3 for v in seen.values()))
                except run.InstanceDied as died:
                    entry.update(status="died", error=str(died))
                    fresh()
                except run.StepFailed as e:
                    entry.update(status="failed", error=str(e)[:200])
                    fresh()
                results[key] = entry
                print(key, entry.get("status"), entry.get("enemies", ""), entry.get("names", ""), flush=True)
                out_path.write_text(json.dumps(results, indent=1))
    finally:
        out_path.write_text(json.dumps(results, indent=1))
        ctx.close()
        for inst in ctx.instances:
            run.kh2ctl("kill", pid=inst.pid, check=False)
        lock.release()
    return 0


if __name__ == "__main__":
    sys.exit(main())
