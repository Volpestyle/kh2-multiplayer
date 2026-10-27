"""Stand-in friend protect (VUH-2364 follow-up): offline, kh2ctl is faked."""
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("USERPROFILE", str(Path.home()))
SPEC = importlib.util.spec_from_file_location("stand_in_protect_runner", ROOT / "tools/scenario/run.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)

ROXAS, CLONE = 0x7FF7A0001000, 0x7FF7A0002000


class FakeCli:
    """Instance 1's camera follows Roxas; a Sora clone (P_EX100) is also listed."""

    def __init__(self):
        self.pokes = []

    def __call__(self, *args, pid=None, check=True, timeout=120, env=None):
        if args[0] == "peek":
            return {"ok": True, "samples": [{"0x718CB0": ROXAS & 0xFFFFFFFF, "0x718CB4": ROXAS >> 32}]}
        if args[0] == "entities":
            return {"ok": True, "actors": [{"address": f"0x{CLONE:X}", "name": "P_EX100"},
                                           {"address": f"0x{ROXAS:X}", "name": "P_EX110"}]}
        if args[0] == "poke":
            self.pokes.append((pid, args[2]))
            return {"ok": True}
        raise AssertionError(f"unexpected kh2ctl {args}")


class StandInProtectTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.cli = FakeCli()
        patcher = patch.object(RUNNER, "kh2ctl", self.cli)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.ctx = RUNNER.Context(Path(self.temp.name))
        self.ctx._stop.set()  # no background loop: each test runs protect_once itself
        self.addCleanup(self.ctx.close)
        self.ctx.instances = [RUNNER.Instance(0, 1000), RUNNER.Instance(1, 2000)]

    def test_protects_the_followed_player_not_a_sora_clone(self):
        self.ctx.protect.add(1)
        self.ctx.protect_once()
        self.assertEqual(self.cli.pokes, [(2000, f"0x{ROXAS + 0x4DC:X}")])
        self.assertEqual(self.ctx.protected_actors, {1: {"P_EX110"}})

    def test_pilot_seat_is_never_protected(self):
        self.ctx.protect.add(0)
        self.ctx.piloted.add(0)
        self.ctx.protect_once()
        self.assertEqual(self.cli.pokes, [])
        with self.assertRaises(RUNNER.StepFailed):
            RUNNER.step_protect(self.ctx, {"do": "protect", "instance": 0})
        self.ctx.piloted.clear()
        with self.assertRaises(RUNNER.StepFailed):  # protected first: the pilot refuses it
            RUNNER.step_pilot(self.ctx, {"do": "pilot", "instance": 0, "teach": "x"})

    def test_after_pilot_protects_a_finished_pilot_seat_only(self):
        # VUH-2901: friend_down's lone host, whose route pilot ended at the hall.
        self.ctx.piloted.add(0)
        RUNNER.step_protect(self.ctx, {"do": "protect", "instance": 0, "afterPilot": True})
        self.ctx.protect_once()
        self.assertEqual(self.cli.pokes, [(1000, f"0x{ROXAS + 0x4DC:X}")])
        self.assertEqual(self.ctx.after_pilot_covered, {0})
        with self.assertRaises(RUNNER.StepFailed):  # still protected: no pilot may play it
            RUNNER.step_pilot(self.ctx, {"do": "pilot", "instance": 0, "teach": "x"})
        RUNNER.step_protect(self.ctx, {"do": "protect", "instance": 0, "on": False})
        self.assertEqual((self.ctx.protect, self.ctx.after_pilot), (set(), set()))
        self.ctx.protect.add(0)  # a stale plain entry stays skipped for a piloted seat
        self.ctx.protect_once()
        self.assertEqual(len(self.cli.pokes), 1)

    def test_after_pilot_refusals(self):
        with self.assertRaises(RUNNER.StepFailed):  # never piloted: plain protect is the stand-in form
            RUNNER.step_protect(self.ctx, {"do": "protect", "instance": 1, "afterPilot": True})
        self.ctx.piloted.add(0)
        self.ctx.saved["_background_pilots"] = [(None, 0, self.ctx.instances[0], {}, "p", None)]
        with self.assertRaises(RUNNER.StepFailed):  # its pilot is still running
            RUNNER.step_protect(self.ctx, {"do": "protect", "instance": 0, "afterPilot": True})
        self.ctx.saved.clear()
        with self.assertRaises(RUNNER.StepFailed):
            RUNNER.step_protect(self.ctx, {"do": "protect", "instance": 0, "afterPilot": "yes"})
        self.assertEqual(self.ctx.protect, set())
        with self.assertRaises(ValueError):
            RUNNER.validate_scenario({"steps": [{"do": "boot"}, {"do": "protect", "instance": 0, "afterPilot": 1}]})

    def test_after_pilot_report_note(self):
        text = RUNNER.render_md({"scenario": "s", "attempt": 1, "status": "pass", "started": "t", "steps": [],
                                 "standInProtected": {"label": "stand-in seat protected", "instances": [],
                                                      "actors": {"0": ["P_EX110"]}, "afterPilot": [0]}})
        self.assertIn("only after their route pilot finished", text)
        self.assertNotIn("never protected", text)

    def test_report_is_labelled(self):
        text = RUNNER.render_md({"scenario": "s", "attempt": 1, "status": "pass", "started": "t", "steps": [],
                                 "standInProtected": {"label": "stand-in friend protected", "instances": [1],
                                                      "actors": {"1": ["P_EX110"]}}})
        self.assertIn("**stand-in friend protected**", text)


if __name__ == "__main__":
    unittest.main()
