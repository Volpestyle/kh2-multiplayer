"""VUH-2869: a forced downing must come before the second wave, in both seats' own logs.

The pure checker (tools/scenario/downed_order.py) over synthetic traces built from real log
line formats (run 20261019-193619), and the committed downed-variant scenarios' own assert/save
steps evaluated by the production runner Context over the same traces. No game.

    python -B -m unittest discover -s tests -p test_downed_wave_order.py -v
"""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ORDER = load("downed_order_under_test", "tools/scenario/downed_order.py")
RUN = load("downed_order_runner", "tools/scenario/run.py")
MARK = "control released at=02/13 evt=134 site=tt19-134"
EP = "70ECF08300000001"

HOST_RELEASE = f"[prologue] {MARK} host"
FRIEND_RELEASE = f"[prologue] {MARK} friend"
HOST_WAVE2 = ["[coop-difficulty] scale frame=5124 objectId=309 actor=7FF627465E40 players=2 mode=1 hp=37->60 max=37->60",
              "[enemysync] host manifest epoch 3 frame 5124: 2 new (append), 6 total; last objectId=309@7FF6274650C0 at (450,0,0)"]
FRIEND_WAVE2 = ["[enemysync] client: local spawn 4 frame 2564 objectId 309 @7FF627477860 at (-450,0,-0)",
                "[enemysync] client: local spawn 5 frame 2564 objectId 309 @7FF627466130 at (450,0,0)"]
WAVE1 = ["[statehash] native epoch=3 frame=2000 netId=1 objectId=318 hp=40 actor=7FF6274000A0",
         "[enemysync] host death epoch 3 netId 1", "[enemysync] host death epoch 3 netId 2"]
DEATHS2 = ["[enemysync] host death epoch 3 netId 3", "[enemysync] host death epoch 3 netId 4",
           "[enemysync] host death epoch 3 netId 5", "[enemysync] host death epoch 3 netId 6"]


def kill(ep=EP):
    return ["[downed] kill actor=7FF627445600 hp=22/22 frame=6470 control=0",
            f"[downed] gate intercept mode=4 logical=1 episode={ep} actor=7FF627445600 hp=22 delta=-22 -> engine hp 1, dead bit clear room=2/19/0/134/134/134 frame=6471",
            "[downed] kill returned=1 hp=1 flags9B8=0x0 deadAction=0 controllerOff=0 task=0 state=2"]


def seen(ep=EP):
    return [f"[revive-prompt] out-of-range slot=1 episode={ep} dist=542.9 nativeRC=0"]


def ended(ep=EP):
    return [f"[downed] logical hold ended reason=2 episode={ep} retired; resolving at 25% on the next field frame"]


def traces(downed, order):
    """order 'good': kill/episode on both, then wave 2; 'births-first': wave 2 then kill (the review's trace)."""
    good = order == "good"
    if downed == 0:  # the host's own log holds every host death
        host = (kill() + HOST_WAVE2 if good else HOST_WAVE2 + kill()) + DEATHS2 + ended()
        friend = seen() + FRIEND_WAVE2 if good else FRIEND_WAVE2 + seen()
    else:
        host = (seen() + HOST_WAVE2 if good else HOST_WAVE2 + seen()) + DEATHS2
        friend = (kill() + FRIEND_WAVE2 if good else FRIEND_WAVE2 + kill()) + ended()
    return ["\n".join([HOST_RELEASE] + WAVE1 + host), "\n".join([FRIEND_RELEASE] + friend)]


class CheckerTest(unittest.TestCase):
    def test_genuine_order_passes_for_each_downed_seat(self):
        for downed in (0, 1):
            r = ORDER.wave_order(traces(downed, "good"), downed, MARK)
            self.assertTrue(r["ok"], r["why"])
            self.assertEqual(r["episode"], EP)
            self.assertEqual(r["episodeSeen"], [True, True])

    def test_births_before_the_downing_are_rejected(self):
        for downed in (0, 1):
            r = ORDER.wave_order(traces(downed, "births-first"), downed, MARK)
            self.assertFalse(r["ok"])
            self.assertIn("started", r["why"])

    def test_other_seat_birth_before_it_names_the_episode_is_rejected(self):
        host, friend = traces(0, "good")
        friend = friend.replace(seen()[0] + "\n", "") + "\n" + seen()[0]
        r = ORDER.wave_order([host, friend], 0, MARK)
        self.assertFalse(r["ok"])
        self.assertIn("seat 1", r["why"])

    def test_a_natural_downing_before_the_kill_is_not_the_fresh_episode(self):
        host, friend = traces(1, "good")
        natural = "[downed] gate intercept mode=4 logical=1 episode=70ECF08300000000 actor=1 hp=2 delta=-3 -> engine hp 1"
        friend = friend.replace(FRIEND_RELEASE, FRIEND_RELEASE + "\n" + natural)
        r = ORDER.wave_order([host, friend], 1, MARK)
        self.assertTrue(r["ok"], r["why"])
        self.assertEqual(r["episode"], EP)

    def test_end_must_name_the_same_episode(self):
        for replacement in (ended("70ECF08300000009")[0],
                            f"[downed] return owed kind=1 episode={EP}: in an event, applied when it ends"):
            host, friend = traces(1, "good")
            friend = friend.replace(ended()[0], replacement)
            r = ORDER.wave_order([host, friend], 1, MARK)
            self.assertFalse(r["ok"])
            self.assertFalse(r["ended"])

    def test_exactly_one_forced_kill(self):
        host, friend = traces(1, "good")
        friend = friend + "\n" + kill("70ECF08300000002")[0]
        self.assertFalse(ORDER.wave_order([host, friend], 1, MARK)["ok"])
        host, friend = traces(1, "good")
        self.assertFalse(ORDER.wave_order([host, friend.replace(kill()[0], "")], 1, MARK)["ok"])

    def test_a_missing_marker_or_wave_is_rejected(self):
        host, friend = traces(0, "good")
        self.assertFalse(ORDER.wave_order([host.replace(MARK, "elsewhere"), friend], 0, MARK)["ok"])
        r = ORDER.wave_order([host.replace("objectId=309", "objectId=318"), friend], 0, MARK)
        self.assertFalse(r["ok"])
        self.assertIn("never started", r["why"])


class ScenarioStepsTest(unittest.TestCase):
    """The committed variants' own assert/save steps, through the production runner Context."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.logs = Path(self.temp.name)
        self.saved_logs = RUN.LOGS
        RUN.LOGS = self.logs
        self.addCleanup(setattr, RUN, "LOGS", self.saved_logs)
        self.ctx = RUN.Context(self.logs)
        self.addCleanup(self.ctx._stop.set)
        for index, pid in enumerate((910001, 910002)):
            inst = RUN.Instance(index, pid)
            inst.expect_exit = True
            self.ctx.instances.append(inst)

    def write(self, host, friend):
        (self.logs / "kh2coop_inject_910001.log").write_text(host, encoding="utf-8")
        (self.logs / "kh2coop_inject_910002.log").write_text(friend, encoding="utf-8")

    def steps(self, downed):
        name = "host" if downed == 0 else "friend"
        path = ROOT / f"tools/scenario/scenarios/net_prologue_mansion_hall_{name}_down.json"
        return json.loads(path.read_text(encoding="utf-8"))["steps"]

    def run_step(self, step):
        return RUN.STEPS[step["do"]](self.ctx, step)

    def before_down(self, downed):
        steps = self.steps(downed)
        i = next(k for k, s in enumerate(steps) if s.get("as") == "before_down")
        return steps[i:i + 2]

    def final(self, downed):
        """From the post-kill episode save through the ordered-end assert, without game reads."""
        steps = self.steps(downed)
        start = next(k for k, s in enumerate(steps) if s.get("as") == "down_seen")
        picked = [steps[start]]
        after = next(k for k, s in enumerate(steps) if s.get("as") == "combat")
        for s in steps[after:]:
            if s["do"] in ("save", "assert") and "location(" not in s["expr"]:
                picked.append(s)
        return picked

    def test_reject_wave_two_already_started_at_the_downing(self):
        for downed in (0, 1):
            self.ctx.saved = {"deaths_before": 0}
            host = "\n".join([HOST_RELEASE] + WAVE1 + HOST_WAVE2)
            friend = "\n".join([FRIEND_RELEASE] + FRIEND_WAVE2)
            self.write(host, friend)
            save, check = self.before_down(downed)
            self.run_step(save)
            with self.assertRaises(RUN.StepFailed):
                self.run_step(check)

    def test_accept_wave_one_half_done_at_the_downing(self):
        for downed in (0, 1):
            self.ctx.saved = {"deaths_before": 0}
            self.write("\n".join([HOST_RELEASE] + WAVE1), FRIEND_RELEASE)
            for step in self.before_down(downed):
                self.run_step(step)

    def test_final_steps_reject_births_before_the_downing(self):
        for downed in (0, 1):
            self.ctx.saved = {"deaths_before": 0}
            host, friend = traces(downed, "births-first")
            host += "\n[mission-mirror] host end trap=mission_complete arg=0"
            self.write(host, friend)
            with self.assertRaises(RUN.StepFailed):
                for step in self.final(downed):
                    self.run_step(step)

    def test_final_steps_accept_a_genuine_order(self):
        for downed in (0, 1):
            self.ctx.saved = {"deaths_before": 0}
            host, friend = traces(downed, "good")
            host += "\n[mission-mirror] host end trap=mission_complete arg=0"
            friend += "\n[mission-mirror] client end like the host file=TT19_MS602.bar trap=x arg=0 ok=1 path=x"
            self.write(host, friend)
            for step in self.final(downed):
                self.run_step(step)
            self.assertTrue(self.ctx.saved["order"]["ok"])
            self.assertGreaterEqual(self.ctx.saved["combat"]["hostDeaths"], 6)


if __name__ == "__main__":
    unittest.main()
