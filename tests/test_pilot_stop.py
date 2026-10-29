"""VUH-2921 (lead #118): combat pilot input ends with the fight. Offline: no game, no pilot process.

The base fight returns on a room change before any other branch, and `pilot_wait` with `stop` ends the
background Thorn pilots at the host's boss-end receipt.

    python -B tests/test_pilot_stop.py
"""
import importlib.util
import itertools
import json
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("USERPROFILE", str(Path.home()))
sys.path.insert(0, str(ROOT / "tools/rig/route_pilot"))
PILOT_SPEC = importlib.util.spec_from_file_location("pilot_stop_base", ROOT / "tools/rig/route_pilot/pilot.py")
BASE = importlib.util.module_from_spec(PILOT_SPEC)
PILOT_SPEC.loader.exec_module(BASE)
RUN_SPEC = importlib.util.spec_from_file_location("pilot_stop_runner", ROOT / "tools/scenario/run.py")
RUNNER = importlib.util.module_from_spec(RUN_SPEC)
RUN_SPEC.loader.exec_module(RUNNER)
FIXTURE = ROOT / "tools/scenario/scenarios/net_twilight_thorn_after_chain.json"
BOSS_END = "log_count('[mission-mirror] host end trap=mission_dead_boss',0)"


class Kb:
    def __init__(self):
        self.log = []

    def hold(self, actions):
        self.log.append(("hold", tuple(sorted(actions))))

    def tap(self, action, ms=90):
        self.log.append(("tap", action))
        return True


def seat(**changes):
    """A free, living seat in the Thorn arena."""
    return {"world": 2, "room": 34, "roomScope": [2, 34, 0, 157, 157, 157], "free": True, "event": False,
            "inField": True, "menu": 255, "battle": 2, "react": 0, "hp": 20, "maxHp": 20, "player": 1,
            "pos": [0.0, 0.0, 400.0], "playerPos": [0.0, 0.0, 400.0], "forward": [0.0, 1.0], **changes}


class FightRoomExitTest(unittest.TestCase):
    """chain-03: after Thorn died the story stayed in events for minutes; the not-free branch tapped Cross
    through them and the step never returned."""

    def fight(self, after):
        """One arena turn, then `after` until the step returns or its 5 s deadline passes; returns
        (result or the Stuck raised, the pad log)."""
        pilot = BASE.Pilot.__new__(BASE.Pilot)
        pilot.kb, pilot.cooperative, pilot.game = Kb(), None, SimpleNamespace(pid=1)
        pilot.write = lambda *a, **k: None
        states = itertools.chain([seat()], itertools.repeat(after))
        pilot.state = lambda: dict(next(states))
        saved = BASE.enemies, BASE.time.sleep
        BASE.enemies = lambda pid, timeout=60: [{"position": {"x": 0.0, "z": 800.0}, "hp": 1}]
        BASE.time.sleep = lambda _: None
        try:
            return pilot.fight({"reach": 300, "clearSec": 8, "lowHp": 0}, BASE.time.monotonic() + 0.3), pilot.kb.log
        except BASE.Stuck as stuck:
            return stuck, pilot.kb.log
        finally:
            BASE.enemies, BASE.time.sleep = saved

    def assert_exit_without_input(self, after):
        result, log = self.fight(after)
        self.assertIsInstance(result, dict, result)
        self.assertEqual(result["exitRoom"], [after["world"], after["room"]])
        self.assertEqual(log, [("hold", ())], "only the release hold; no tap reaches the next room")

    def test_event_in_the_next_room_gets_no_cross(self):
        self.assert_exit_without_input(seat(room=0, free=False, event=True))

    def test_hp_unavailable_in_the_next_room_returns(self):
        """chain-03's last state: 02/01, event, no player HP."""
        self.assert_exit_without_input(seat(room=1, free=False, event=True, hp=None, maxHp=None))

    def test_offered_reaction_in_the_next_room_is_not_taken(self):
        self.assert_exit_without_input(seat(room=0, react=4))

    def test_loading_world_is_not_an_exit(self):
        """World 0xFF is the load between rooms: the fight goes on until its deadline."""
        result, _ = self.fight(seat(world=0xFF, room=0xFF, free=False, hp=None))
        self.assertIsInstance(result, BASE.Stuck)

    def test_arena_event_keeps_its_cross(self):
        """The in-arena not-free handling is unchanged: an event there still gets Cross."""
        result, log = self.fight(seat(free=False, event=True))
        self.assertIsInstance(result, BASE.Stuck)
        self.assertIn(("tap", "confirm"), log)


class FakeProc:
    def __init__(self, name, events, code=None, pid=None):
        self.name, self.events, self.returncode, self.pid = name, events, code, pid

    def poll(self):
        return self.returncode

    def kill(self):
        self.events.append(("kill", self.name))
        self.returncode = 1

    def wait(self, timeout=None):
        return self.returncode


class PilotWaitStopTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.events = []
        self.ctx = SimpleNamespace(saved={}, artifacts=[], sleep=lambda _: None)
        release = patch.object(RUNNER.subprocess, "run", side_effect=self.release)
        release.start()
        self.addCleanup(release.stop)
        self.addCleanup(self.tmp.cleanup)

    def release(self, cmd, **_):
        self.events.append(("release", cmd[cmd.index("--pid") + 1]))
        return SimpleNamespace(returncode=0)

    def pilots(self, *codes, names=None):
        out = Path(self.tmp.name)
        names = names or [f"p{i}" for i in range(len(codes))]
        self.ctx.saved["_background_pilots"] = [
            (FakeProc(f"p{i}", self.events, code, pid=900 + i), RUNNER.time.monotonic() + 1,
             SimpleNamespace(pid=100 + i), {"input": "pad"}, names[i], out / names[i]) for i, code in enumerate(codes)]

    def test_stop_kills_every_running_pilot_before_collecting_any(self):
        self.pilots(None, None)
        result = RUNNER.step_pilot_wait(self.ctx, {"do": "pilot_wait", "stop": True})
        self.assertEqual(self.events, [("kill", "p0"), ("kill", "p1"), ("release", "100"), ("release", "101")])
        self.assertEqual(result, {"p0": {"exit": 1, "stopped": True, "pid": 900},
                                  "p1": {"exit": 1, "stopped": True, "pid": 901}})
        self.assertNotIn("_background_pilots", self.ctx.saved)

    def test_a_pilot_that_already_failed_still_fails(self):
        self.pilots(3, None)
        with self.assertRaisesRegex(RUNNER.StepFailed, "pilot exited 3"):
            RUNNER.step_pilot_wait(self.ctx, {"do": "pilot_wait", "stop": True})
        self.assertEqual(self.events, [("kill", "p1"), ("release", "100"), ("release", "101")])

    def test_a_failed_pilot_sharing_a_name_with_a_stopped_one_still_fails(self):
        """Ione 60a76c2b: both pilots use the default name `pilot`; the first already exited 3, the second
        is running. On ebe9b61 stopping the second marked the name, and the failure passed as stopped."""
        self.pilots(3, None, names=["pilot", "pilot"])
        with self.assertRaisesRegex(RUNNER.StepFailed, "pilot exited 3"):
            RUNNER.step_pilot_wait(self.ctx, {"do": "pilot_wait", "stop": True})
        self.assertEqual(self.events, [("kill", "p1"), ("release", "100"), ("release", "101")])

    def test_a_pilot_that_already_finished_passes_unstopped(self):
        self.pilots(0, None)
        result = RUNNER.step_pilot_wait(self.ctx, {"do": "pilot_wait", "stop": True})
        self.assertEqual(result["p0"], {"exit": 0})

    def test_without_stop_nothing_is_killed_and_a_failed_exit_fails(self):
        self.pilots(1)
        with self.assertRaisesRegex(RUNNER.StepFailed, "pilot exited 1"):
            RUNNER.step_pilot_wait(self.ctx, {"do": "pilot_wait"})
        self.assertEqual(self.events, [("release", "100")])

    def test_stop_must_be_a_boolean(self):
        self.pilots(None)
        with self.assertRaises(RUNNER.StepFailed):
            RUNNER.step_pilot_wait(self.ctx, {"do": "pilot_wait", "stop": "yes"})
        self.assertEqual(self.events, [])
        with self.assertRaises(ValueError):
            RUNNER.validate_scenario({"steps": [{"do": "pilot_wait", "stop": 1}]})


class AfterChainStopTest(unittest.TestCase):
    """The post-Thorn fixture stops both Thorn pilots at the host's boss-end receipt (lead #118)."""

    def setUp(self):
        self.steps = json.loads(FIXTURE.read_text(encoding="utf-8"))["steps"]

    def test_stop_follows_the_boss_end_receipt(self):
        end = next(i for i, s in enumerate(self.steps) if s["do"] == "wait_until" and s["expr"].startswith(BOSS_END))
        self.assertLessEqual(self.steps[end].get("pollMs", 500), 100)
        self.assertEqual(self.steps[end + 1], {"do": "pilot_wait", "stop": True})

    def test_no_pilot_input_outlives_the_fight(self):
        thorn = [i for i, s in enumerate(self.steps) if s["do"] == "pilot" and s.get("background")
                 and s["name"] in ("host_thorn", "friend_thorn")]
        self.assertEqual(len(thorn), 2)
        waits = [i for i, s in enumerate(self.steps) if s["do"] == "pilot_wait" and i > thorn[-1]]
        self.assertEqual(len(waits), 1)
        self.assertIs(self.steps[waits[0]].get("stop"), True)
        later = [s for s in self.steps[waits[0]:] if s["do"] in ("pilot", "fight")]
        self.assertEqual(later, [], "no pilot or fight input to either seat after the boss-end stop")


if __name__ == "__main__":
    unittest.main()
