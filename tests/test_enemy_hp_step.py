"""VUH-2763: the test-only scenario step test_enemy_hp against a fake kh2ctl. No game.

    python -B tests/test_enemy_hp_step.py
"""
import importlib.util
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("enemy_hp_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)

BASE, STATUS = 0x140000000, 0x2B000000
THORN = {"address": "0x2A000000", "objectId": 307, "objectType": 12, "status": f"0x{STATUS:X}", "hp": 300, "maxHp": 300}


class FakeGame:
    """i32 memory at absolute addresses, read by RVA keys and written by --addr like kh2ctl."""

    def __init__(self, hp=300, max_hp=300, stick=False):
        self.memory = {STATUS: hp, STATUS + 4: max_hp}
        self.stick, self.pokes = stick, []

    def __call__(self, *args, pid=None, **_):
        if args[0] == "peek":
            fields = [field.split(":")[0] for field in args[2].split(",")]
            return {"moduleBase": hex(BASE),
                    # kh2ctl adds the RVA to the module base modulo 2**64 (heap below the image).
                    "samples": [{key: self.memory.get((BASE + int(key, 16)) % 2**64, 0) for key in fields}]}
        if args[0] == "poke":
            address, value = int(args[2], 16), int(args[6])
            self.pokes.append((pid, address, args[4], value))
            if not self.stick:
                self.memory[address] = value
            return {"ok": True}
        raise AssertionError(args)


def context(actors, test_only=("enemy_hp_write",)):
    inst = SimpleNamespace(pid=4242)
    return SimpleNamespace(test_only=set(test_only), inst=lambda index: inst, actors=lambda index: actors, saved={})


class EnemyHpStepTest(unittest.TestCase):
    def run_step(self, game, ctx, **step):
        original = RUN.kh2ctl
        RUN.kh2ctl = game
        try:
            return RUN.step_test_enemy_hp(ctx, {"do": "test_enemy_hp", "objectId": 307, "hp": 1, **step})
        finally:
            RUN.kh2ctl = original

    def test_lowers_hp_after_identity_check(self):
        game, ctx = FakeGame(), context([dict(THORN)])
        result = self.run_step(game, ctx, **{"as": "assist"})
        self.assertEqual(game.pokes, [(4242, STATUS, "i32", 1)])
        self.assertEqual((result["before"], result["readback"]), ([300, 300], [1, 300]))
        self.assertIs(ctx.saved["assist"], result)

    def test_refused_without_scenario_opt_in(self):
        game = FakeGame()
        with self.assertRaisesRegex(RUN.StepFailed, "testOnly"):
            self.run_step(game, context([dict(THORN)], test_only=()))
        self.assertEqual(game.pokes, [])

    def test_refuses_mismatched_status_or_raise(self):
        for game, actors, hp, message in (
                (FakeGame(hp=250), [dict(THORN)], 1, "disagrees"),
                (FakeGame(), [dict(THORN), dict(THORN)], 1, "found 2"),
                (FakeGame(), [], 1, "found 0"),
                (FakeGame(), [dict(THORN)], 300, "below current")):
            with self.subTest(message), self.assertRaisesRegex(RUN.StepFailed, message):
                self.run_step(game, context(actors), hp=hp)
            self.assertEqual(game.pokes, [])

    def test_failed_readback_is_reported(self):
        with self.assertRaisesRegex(RUN.StepFailed, "readback"):
            self.run_step(FakeGame(stick=True), context([dict(THORN)]))

    def test_validator_requires_opt_in(self):
        steps = [{"do": "boot"}, {"do": "test_enemy_hp", "instance": 0, "objectId": 307, "hp": 1}]
        with self.assertRaisesRegex(ValueError, "testOnly"):
            RUN.validate_scenario({"steps": steps})
        RUN.validate_scenario({"testOnly": ["enemy_hp_write"], "steps": steps})
        with self.assertRaisesRegex(ValueError, "hp >= 1"):
            RUN.validate_scenario({"testOnly": ["enemy_hp_write"], "steps": [steps[0], {**steps[1], "hp": 0}]})


if __name__ == "__main__":
    unittest.main()
