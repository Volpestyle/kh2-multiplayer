"""VUH-1499: do two instances spawn the same enemies?

Boots two rig instances from the same save. For each room: warp both,
keep both Soras untouchable, drive them with the same inputs (interleaved),
and record every enemy (objentry type 3/4) each instance spawns: first-seen
list order, actor address, objentry id, name and first position. Then kill
the wave on both (`kh2ctl hit kill`) and record the next wave, up to
--waves. Compares the two sides per wave under several candidate keys.

    python tools/scenario/spikes/enemy_parity.py 05/00 05/06 05/01:evt=1
Writes build/scenarios/<stamp>_enemy_parity/parity.json.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run  # noqa: E402


def is_enemy(a: dict) -> bool:
    # F_ objects can carry type 3/4 (breakables); they aren't spawned enemies.
    return a.get("objectType") in (3, 4) and not a["name"].startswith("F_")


class EnemyLog:
    """Background sampler: first sighting of every enemy on each instance."""

    def __init__(self, ctx: run.Context) -> None:
        self.ctx = ctx
        self.first: list[dict[str, dict]] = [{}, {}]   # instance -> address -> record
        self.alive: list[set[str]] = [set(), set()]
        self.ignore: list[set[str]] = [set(), set()]  # alive when the current wave began
        self.wave = 1
        self.t0 = time.monotonic()
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self) -> None:
        while not self._stop.is_set():
            for i in (0, 1):
                try:
                    actors = self.ctx.actors(i)
                except Exception:  # noqa: BLE001
                    continue
                alive = set()
                for index, a in enumerate(actors):
                    if not is_enemy(a):
                        continue
                    alive.add(a["address"])
                    if a["address"] in self.ignore[i]:
                        continue
                    key = f"{self.wave}:{a['address']}"
                    if key not in self.first[i]:
                        p = a["position"]
                        self.first[i][key] = {
                            "wave": self.wave, "address": a["address"], "name": a["name"],
                            "objectId": a.get("objectId"), "type": a["objectType"],
                            "listIndex": index, "pos": (p["x"], p["y"], p["z"]),
                            "t": round(time.monotonic() - self.t0, 2), "maxHp": a.get("maxHp")}
                self.alive[i] = alive
                self.ignore[i] &= alive  # a reused slot after a death is a new enemy
            time.sleep(0.05)

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=5)

    def wave_records(self, instance: int, wave: int) -> list[dict]:
        recs = [r for r in self.first[instance].values() if r["wave"] == wave]
        return sorted(recs, key=lambda r: (r["t"], r["listIndex"]))


def compare(a: list[dict], b: list[dict]) -> dict:
    """Match rates of instance 1's enemies against instance 0's under each
    candidate key, plus position deltas for the address key."""
    def rate(key) -> float:
        if not a and not b:
            return 1.0
        ka = [key(i, r) for i, r in enumerate(a)]
        kb = [key(i, r) for i, r in enumerate(b)]
        matched = 0
        pool = list(kb)
        for k in ka:
            if k in pool:
                pool.remove(k)
                matched += 1
        return round(matched / max(len(a), len(b)), 3)

    out = {
        "count": [len(a), len(b)],
        "names": [sorted(r["name"] for r in a), sorted(r["name"] for r in b)],
        "key_address": rate(lambda i, r: (r["address"], r["name"])),
        "key_order_name": rate(lambda i, r: (i, r["name"])),
        "key_objectId": rate(lambda i, r: (r["objectId"], r["name"])),
        "key_multiset_name": rate(lambda i, r: r["name"]),
    }
    by_addr = {r["address"]: r for r in b}
    deltas = [math.dist(r["pos"], by_addr[r["address"]]["pos"]) for r in a if r["address"] in by_addr]
    if deltas:
        deltas.sort()
        out["spawnPosDelta"] = {"median": round(deltas[len(deltas) // 2], 1), "max": round(deltas[-1], 1)}
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("rooms", nargs="+", help="WW/RR[:evt=N][:btl=N][:door=N] (hex)")
    ap.add_argument("--waves", type=int, default=3)
    args = ap.parse_args()

    lock = run.RigLock()
    problem = lock.acquire()
    if problem:
        print(problem)
        return run.EXIT_RIG
    run_dir = run.RUNS / f"{time.strftime('%Y%m%d-%H%M%S')}_enemy_parity"
    run_dir.mkdir(parents=True, exist_ok=True)
    ctx = run.Context(run_dir)
    results = []

    def fresh() -> None:
        for inst in ctx.instances:
            run.kh2ctl("kill", pid=inst.pid, check=False)
        ctx.instances.clear()
        ctx.protect.clear()
        run.step_boot(ctx, {})
        run.step_boot(ctx, {})
        ctx.protect.update({0, 1})

    try:
        fresh()
        for spec in args.rooms:
            parts = spec.split(":")
            world, room = (int(x, 16) for x in parts[0].split("/"))
            extra = dict(p.split("=") for p in parts[1:])
            entry = {"room": spec}
            try:
                for i in (0, 1):
                    step = {"instance": i, "world": world, "room": room}
                    step.update({k: int(v, 0) for k, v in extra.items()})
                    run.step_warp(ctx, step)
                log = EnemyLog(ctx)
                try:
                    for _ in range(3):  # identical inputs, interleaved
                        for i in (0, 1):
                            run.kh2ctl("player-input", "--ly", "1", "--duration-ms", "900",
                                       pid=ctx.instances[i].pid)
                    ctx.sleep(4)
                    waves = []
                    for wave in range(1, args.waves + 1):
                        a, b = log.wave_records(0, wave), log.wave_records(1, wave)
                        if not a and not b:
                            break
                        waves.append({"wave": wave, **compare(a, b), "records": [a, b]})
                        if wave == args.waves:
                            break
                        # End this wave on both sides, and only start counting the
                        # next one once every killed enemy has left the list.
                        # Kill rounds until both lists are empty (enemies can be out
                        # of the list for a moment, e.g. burrowed, and miss a round).
                        killed: list[set[str]] = [set(), set()]
                        failures = 0
                        for _ in range(4):
                            for i in (0, 1):
                                for addr in list(log.alive[i]):
                                    r = run.kh2ctl("hit", "kill", "--victim", addr,
                                                   pid=ctx.instances[i].pid, check=False)
                                    if r.get("ok"):
                                        killed[i].add(addr)
                                    else:
                                        failures += 1
                            ctx.sleep(1.5)
                            if not log.alive[0] and not log.alive[1]:
                                break
                        deadline = time.monotonic() + 8
                        while time.monotonic() < deadline and any(killed[i] & log.alive[i] for i in (0, 1)):
                            ctx.sleep(0.25)
                        waves[-1]["killed"] = [len(killed[0]), len(killed[1])]
                        waves[-1]["killFailures"] = failures
                        waves[-1]["stillAlive"] = [len(killed[i] & log.alive[i]) for i in (0, 1)]
                        log.ignore = [set(log.alive[0]), set(log.alive[1])]  # survivors aren't new
                        log.wave = wave + 1
                        ctx.sleep(8)
                    entry.update(status="ok", waves=waves)
                finally:
                    log.stop()
            except (run.StepFailed, run.InstanceDied) as e:
                entry.update(status="failed", error=str(e)[:300])
                fresh()
            results.append(entry)
            summary = [(w["wave"], w["count"], w["key_address"], w["key_order_name"]) for w in entry.get("waves", [])]
            print(spec, entry["status"], summary, entry.get("error", ""), flush=True)
            (run_dir / "parity.json").write_text(json.dumps(results, indent=1))
    finally:
        ctx.close()
        for inst in ctx.instances:
            run.kh2ctl("kill", pid=inst.pid, check=False)
        lock.release()
    print(run_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
