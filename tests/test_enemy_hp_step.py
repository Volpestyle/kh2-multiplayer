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

# Retained Thorn descriptors have status pointers above the image base.
BASE, STATUS = 0x7FF6A0000000, 0x7FF6C2B40000
THORN = {"address": "0x2A000000", "objectId": 307, "objectType": 12, "status": f"0x{STATUS:X}", "hp": 300, "maxHp": 300}


class FakeGame:
    """i32 memory at absolute addresses, read by RVA keys and written by --addr like kh2ctl."""

    def __init__(self, hp=300, max_hp=300, stick=False):
        self.memory = {STATUS: hp, STATUS + 4: max_hp}
        self.stick, self.pokes = stick, []

    def __call__(self, *args, pid=None, **_):
        if args[0] == "peek":
            fields = [field.split(":")[0] for field in args[2].split(",")]
            # Like CmdPeek: an RVA that would overflow base+RVA is refused (the runner raises on it).
            if any(BASE + int(key, 16) >= 2**64 for key in fields):
                raise RUN.StepFailed("kh2ctl peek: peek failed to read RVA")
            return {"moduleBase": hex(BASE),
                    "samples": [{key: self.memory.get(BASE + int(key, 16), 0) for key in fields}]}
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


class CombatGame(FakeGame):
    """Thorn's status HP as the seats keep hitting him: each status read returns the next scripted HP (max 390)."""

    def __init__(self, reads):
        super().__init__(hp=reads[0], max_hp=390)
        self.reads, self.served, self.poked = list(reads), 0, False

    def __call__(self, *args, pid=None, **kwargs):
        if args[0] == "poke":
            self.poked = True  # the readback then sees the write
        elif args[0] == "peek" and not self.poked and f"0x{STATUS - BASE:X}:i32" in args[2]:
            self.memory[STATUS] = self.reads[min(self.served, len(self.reads) - 1)]
            self.served += 1
        return super().__call__(*args, pid=pid, **kwargs)


class Rows:
    """The entities listing's Thorn row: the n-th listing shows hps[n] (the last one repeats)."""

    def __init__(self, game, hps, replace_after=None):
        self.game, self.hps, self.calls, self.replace_after = game, hps, 0, replace_after

    def __call__(self, index):
        hp = self.hps[min(self.calls, len(self.hps) - 1)]
        replaced = self.replace_after is not None and self.calls >= self.replace_after
        self.calls += 1
        return [dict(THORN, hp=hp, maxHp=390, address="0x2B000000" if replaced else THORN["address"])]


def context_rows(rows):
    inst = SimpleNamespace(pid=4242)
    return SimpleNamespace(test_only={"enemy_hp_write"}, inst=lambda index: inst, actors=rows, saved={})


class EnemyHpStepTest(unittest.TestCase):
    def run_step(self, game, ctx, **step):
        original, pause = RUN.kh2ctl, RUN.ENEMY_HP_AGREE_PAUSE_S
        RUN.kh2ctl, RUN.ENEMY_HP_AGREE_PAUSE_S = game, 0
        try:
            return RUN.step_test_enemy_hp(ctx, {"do": "test_enemy_hp", "objectId": 307, "hp": 1, **step})
        finally:
            RUN.kh2ctl, RUN.ENEMY_HP_AGREE_PAUSE_S = original, pause

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

    def test_waits_until_status_and_row_agree_while_the_seats_fight(self):
        """vuh2921-thorn-chain-01: a hit landed between the row and the status read (380 vs 370). The row is now read
        between two status reads and taken only when all three agree; nothing is written before that."""
        game = CombatGame([380, 370, 370, 370, 370])  # status reads in order: a hit between read 1 and the row
        rows = Rows(game, [380, 370])                    # the row taken mid-hit, then settled
        result = self.run_step(game, context_rows(rows), **{"as": "assist"})
        self.assertEqual(game.pokes, [(4242, STATUS, "i32", 1)])
        self.assertEqual((result["before"], result["attempts"]), ([370, 390], 2))

    def test_a_hit_between_the_two_status_reads_is_not_agreement(self):
        game = CombatGame([380, 370, 370, 370])  # attempt 1: 380, row 380, 370; attempt 2: 370, row 370, 370
        rows = Rows(game, [380, 380, 370])
        result = self.run_step(game, context_rows(rows))
        self.assertEqual((result["before"], result["attempts"]), ([370, 390], 2))

    def test_refuses_when_they_never_agree_and_writes_nothing(self):
        game = CombatGame([390 - n for n in range(3 * RUN.ENEMY_HP_AGREE_ATTEMPTS + 3)])  # a hit before every read
        rows = Rows(game, [500])
        with self.assertRaisesRegex(RUN.StepFailed, f"disagrees with actor row 500/390 in all {RUN.ENEMY_HP_AGREE_ATTEMPTS}"):
            self.run_step(game, context_rows(rows))
        self.assertEqual(game.pokes, [])

    def test_refuses_an_enemy_replaced_during_the_check(self):
        game = CombatGame([380, 370, 370, 370])
        rows = Rows(game, [380, 370], replace_after=1)
        with self.assertRaisesRegex(RUN.StepFailed, "changed to"):
            self.run_step(game, context_rows(rows))
        self.assertEqual(game.pokes, [])

    def test_refuses_a_status_below_the_image(self):
        game = FakeGame()
        below = dict(THORN, status=f"0x{BASE - 0x1000:X}")
        with self.assertRaisesRegex(RUN.StepFailed, "below the module base"):
            self.run_step(game, context([below]))
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
