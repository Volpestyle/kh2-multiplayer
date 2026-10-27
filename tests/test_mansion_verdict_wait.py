"""VUH-2898: the Mansion scenarios' coop_verdict waits, bounded, for the friend's arrival in Axel's room,
and _host_down protects its lone fighting friend only through wave 2.

The committed scenarios' own coop_verdict and protect steps run through the production runner
Context over log lines in the formats of run 20261026-215808 (no game). The friend's arrival is
appended while the step waits, as it is logged ~5.7 s after the host's in a live run.

    python -B -m unittest discover -s tests -p test_mansion_verdict_wait.py -v
"""
import importlib.util
import json
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("mansion_wait_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(RUN)
SCENARIOS = ("net_prologue_mansion_hall", "net_prologue_mansion_hall_host_down", "net_prologue_mansion_hall_friend_down")

AXEL = "room=02/14 door=0 map=137 btl=137 evt=137"
HALL = "room=02/13 door=0 map=134 btl=134 evt=134"
HOST = "\n".join([
    "[progresssync] host full version=1 spans=4 bytes=8108 hash=24690699",
    f"[enemysync] host arrived epoch=3 {HALL}",
    "[mission-mirror] host end trap=mission_complete arg=4294967295",
    f"[warp] load complete serial=4 transition=3 {AXEL}",
    f"[enemysync] host arrived epoch=4 {AXEL}",
    f"[statehash] role=host epoch=4 frame=8525 {AXEL} enemies=210A0FB3 progress=30BEFB6E count=1 unmatched=0 "
    "observed=1 tickMs=803293796", ""])
FRIEND = "\n".join([
    "[progresssync] apply version=1 spans=4 bytes=8108 hash=24690699 personal_before=1 personal_after=1 personal_unchanged=1",
    f"[enemysync] client arrived epoch=3 {HALL}",
    "[enemysync] client: host epoch 4 room 02/14 btl 137",
    "[warp] client issued epoch=4 transition=4 at its own blocked native exit",
    f"[warp] load complete serial=5 transition=4 {AXEL}", ""])
ARRIVAL = "\n".join([
    f"[enemysync] client arrived epoch=4 {AXEL}",
    f"[statehash] role=client epoch=4 frame=4200 {AXEL} enemies=210A0FB3 progress=30BEFB6E count=1 unmatched=0 "
    "observed=1 tickMs=803299496", ""])


def scenario(name):
    return json.loads((ROOT / f"tools/scenario/scenarios/{name}.json").read_text(encoding="utf-8"))


def verdict_step(name):
    return next(s for s in scenario(name)["steps"] if s["do"] == "coop_verdict")


class VerdictWaitTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.dir = Path(temp.name)
        saved = RUN.LOGS
        RUN.LOGS = self.dir
        self.addCleanup(setattr, RUN, "LOGS", saved)
        self.ctx = RUN.Context(self.dir)
        self.addCleanup(self.ctx._stop.set)
        for index, pid in enumerate((920001, 920002)):
            inst = RUN.Instance(index, pid)
            inst.expect_exit = True
            self.ctx.instances.append(inst)
        self.host = self.dir / "kh2coop_inject_920001.log"
        self.friend = self.dir / "kh2coop_inject_920002.log"
        self.host.write_text(HOST, encoding="utf-8")
        self.friend.write_text(FRIEND, encoding="utf-8")

    def arrive_after(self, seconds):
        def append():
            with self.friend.open("a", encoding="utf-8") as f:
                f.write(ARRIVAL)
        timer = threading.Timer(seconds, append)
        timer.start()
        self.addCleanup(timer.cancel)

    def test_every_mansion_verdict_waits_twenty_seconds(self):
        for name in SCENARIOS:
            step = verdict_step(name)
            self.assertEqual(step["arrivalTimeoutMs"], 20000, name)
            self.assertTrue(step["require"], name)

    def test_a_late_arrival_passes_and_reports_its_real_lag(self):
        for name in SCENARIOS:
            with self.subTest(name):
                self.friend.write_text(FRIEND, encoding="utf-8")
                self.arrive_after(0.6)
                out = RUN.STEPS["coop_verdict"](self.ctx, verdict_step(name))
                self.assertTrue(out["pass"], out["problems"])
                self.assertEqual(out["arrivalWait"]["epoch"], 4)
                self.assertEqual(out["arrivalWait"]["waiting"], [])
                self.assertGreaterEqual(out["arrivalWait"]["seconds"], 0.5)
                self.assertLess(out["arrivalWait"]["seconds"], 5)
                self.assertEqual(out["lastRoom"]["friend1"],
                                 {"epoch": 4, "result": "followed", "hostTickMs": 803293796,
                                  "friendTickMs": 803299496, "lagMs": 5700})
                saved = json.loads((self.dir / "verdict.json").read_text(encoding="utf-8"))
                self.assertEqual(saved["arrivalWait"]["epoch"], 4)

    def test_without_the_wait_the_same_trace_fails_as_the_live_runs_did(self):
        step = {k: v for k, v in verdict_step("net_prologue_mansion_hall").items() if k != "arrivalTimeoutMs"}
        self.arrive_after(0.6)
        with self.assertRaisesRegex(RUN.StepFailed, r"friend1: epoch 4: no friend arrival'\]$"):
            RUN.STEPS["coop_verdict"](self.ctx, step)

    def test_a_missing_arrival_still_fails_after_the_bound_and_names_the_wait(self):
        step = {**verdict_step("net_prologue_mansion_hall"), "arrivalTimeoutMs": 800}
        with self.assertRaises(RUN.StepFailed) as caught:
            RUN.STEPS["coop_verdict"](self.ctx, step)
        message = str(caught.exception)
        self.assertIn("friend1: epoch 4: no friend arrival", message)
        self.assertRegex(message, r"after waiting [\d.]+ s \(limit 0\.8 s\) for friend1 to arrive at host epoch 4$")
        wait = self.ctx.saved["verdict"]["arrivalWait"]
        self.assertGreaterEqual(wait["seconds"], 0.8)
        self.assertLess(wait["seconds"], 2)
        self.assertEqual(wait["waiting"], ["friend1"])

    def test_an_arrival_waits_for_its_statehash_before_judging(self):
        # The arrival line alone (its tick not yet logged) keeps waiting; the tick then lands.
        with self.friend.open("a", encoding="utf-8") as f:
            f.write(ARRIVAL.splitlines()[0] + "\n")
        def tick():
            with self.friend.open("a", encoding="utf-8") as f:
                f.write(ARRIVAL.splitlines()[1] + "\n")
        timer = threading.Timer(0.6, tick)
        timer.start()
        self.addCleanup(timer.cancel)
        out = RUN.STEPS["coop_verdict"](self.ctx, verdict_step("net_prologue_mansion_hall"))
        self.assertGreaterEqual(out["arrivalWait"]["seconds"], 0.5)
        self.assertEqual(out["lastRoom"]["friend1"]["lagMs"], 5700)


class HostDownProtectTest(unittest.TestCase):
    def test_protection_covers_only_the_lone_seats_wave_two_fight(self):
        # host_down's lone friend (VUH-2898) and friend_down's lone host (VUH-2901, run 063606).
        for name, lone in (("net_prologue_mansion_hall_host_down", 1), ("net_prologue_mansion_hall_friend_down", 0)):
            with self.subTest(name):
                steps = scenario(name)["steps"]
                on = [k for k, s in enumerate(steps) if s["do"] == "protect" and s.get("on", True)]
                off = [k for k, s in enumerate(steps) if s["do"] == "protect" and not s.get("on", True)]
                self.assertEqual(len(on), 1)
                self.assertEqual(len(off), 1)
                (on,), (off,) = on, off
                self.assertEqual(steps[on - 1]["message"], "wave 2 started, or the host's third wave-1 death triggered it, before both seats knew the forced episode")
                self.assertEqual([s["do"] for s in steps[:on]].count("downed_kill"), 1)
                self.assertEqual((steps[on + 1]["do"], steps[on + 1]["instances"], steps[on + 1]["as"]), ("fight", [lone], "wave2"))
                self.assertEqual(off, on + 2)
                self.assertEqual(steps[on]["instance"], lone)
                self.assertEqual(steps[off]["instance"], lone)
                self.assertEqual(scenario(name)["protect"], [])
                piloted = lone in [s.get("instance") for s in steps if s["do"] == "pilot"]
                self.assertEqual(steps[on].get("afterPilot", False), piloted)  # only friend_down's host walked

    def test_the_base_mansion_scenario_protects_no_one(self):
        d = scenario("net_prologue_mansion_hall")
        self.assertEqual(d["protect"], [])
        self.assertNotIn("protect", [s["do"] for s in d["steps"]])

    def test_the_protect_steps_toggle_the_runner_protection_set(self):
        with tempfile.TemporaryDirectory() as temp:
            ctx = RUN.Context(Path(temp))
            try:
                ctx.instances += [RUN.Instance(0, 930001), RUN.Instance(1, 930002)]
                for inst in ctx.instances:
                    inst.expect_exit = True
                steps = [s for s in scenario("net_prologue_mansion_hall_host_down")["steps"] if s["do"] == "protect"]
                RUN.STEPS["protect"](ctx, steps[0])
                self.assertEqual(ctx.protect, {1})
                RUN.STEPS["protect"](ctx, steps[1])
                self.assertEqual(ctx.protect, set())
            finally:
                ctx._stop.set()


if __name__ == "__main__":
    unittest.main()
