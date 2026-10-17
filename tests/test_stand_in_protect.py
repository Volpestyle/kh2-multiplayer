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

    def test_report_is_labelled(self):
        text = RUNNER.render_md({"scenario": "s", "attempt": 1, "status": "pass", "started": "t", "steps": [],
                                 "standInProtected": {"label": "stand-in friend protected", "instances": [1],
                                                      "actors": {"1": ["P_EX110"]}}})
        self.assertIn("**stand-in friend protected**", text)


if __name__ == "__main__":
    unittest.main()
