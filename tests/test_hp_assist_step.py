"""VUH-2912 assisted reruns (lead #105): the test-only hp_assist step and its two assisted Axel splits. No game.

    python -B tests/test_hp_assist_step.py
"""
import importlib.util
import json
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("hp_assist_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)
SCENARIOS = ROOT / "tools/scenario/scenarios"

BASE, STATUS, CANON = 0x7FF6A0000000, 0x7FF6C2B40000, "0x7FF7014EE050"


class FakeGame:
    """The canonical player's status i32s at absolute addresses, read by RVA keys and written by --addr like kh2ctl.
    `moves` scripts status HP per status read (a hit landing mid-check); `stick` ignores pokes."""

    def __init__(self, hp=10, max_hp=22, moves=None, stick=False):
        self.memory = {STATUS: hp, STATUS + 4: max_hp}
        self.moves, self.stick, self.pokes, self.reads = list(moves or []), stick, [], 0

    def __call__(self, *args, pid=None, **_):
        if args[0] == "peek":
            fields = [field.split(":")[0] for field in args[2].split(",")]
            if f"0x{STATUS - BASE:X}" in fields and self.moves and not self.pokes:
                self.memory[STATUS] = self.moves[min(self.reads, len(self.moves) - 1)]
                self.reads += 1
            return {"moduleBase": hex(BASE), "samples": [{k: self.memory.get(BASE + int(k, 16), 0) for k in fields}]}
        if args[0] == "poke":
            address, value = int(args[2], 16), int(args[6])
            self.pokes.append((pid, address, args[4], value))
            if not self.stick:
                self.memory[address] = value
            return {"ok": True}
        raise AssertionError(args)


def context(game, downs=0, revives=0, canonical=CANON, test_only=("player_hp_refill",)):
    inst = SimpleNamespace(pid=4242)

    def actors(index):
        return [{"address": canonical, "name": "P_EX110_BTLF", "status": f"0x{STATUS:X}",
                 "hp": game.memory[STATUS], "maxHp": game.memory[STATUS + 4]},
                {"address": "0x7FF701508DA0", "name": "B_EX110", "status": "0x1", "hp": 449, "maxHp": 449}]
    counts = {RUN.DOWNED_DOWN: downs, RUN.DOWNED_REVIVED: revives}
    namespace = {"log_count": lambda text, index=0: counts.get(text, 0), "peek": lambda rva, kind="u32", index=0: CANON}
    return SimpleNamespace(test_only=set(test_only), inst=lambda index: inst, actors=actors, saved={},
                           namespace=lambda: namespace)


class HpAssistTest(unittest.TestCase):
    def setUp(self):
        self.original = RUN.kh2ctl

    def tearDown(self):
        RUN.kh2ctl = self.original

    def tick(self, game, ctx, floor=12):
        RUN.kh2ctl = game
        return RUN.HpAssist(ctx, [0], floor).tick(0)

    def test_refills_the_canonical_player_to_max_at_or_below_the_floor(self):
        for hp in (12, 10, 1):
            with self.subTest(hp):
                game = FakeGame(hp=hp)
                event = self.tick(game, context(game))
                self.assertEqual(game.pokes, [(4242, STATUS, "i32", 22)])
                self.assertEqual((event["before"], event["readback"], event["actor"]), ([hp, 22], [22, 22], CANON))
                self.assertNotIn("readbackMismatch", event)

    def test_leaves_hp_above_the_floor_at_max_or_at_zero_alone(self):
        for hp, max_hp in ((13, 22), (22, 22), (0, 22), (5, 5)):
            with self.subTest((hp, max_hp)):
                game = FakeGame(hp=hp, max_hp=max_hp)
                self.assertIsNone(self.tick(game, context(game)))
                self.assertEqual(game.pokes, [])

    def test_never_refills_a_downed_seat(self):
        game = FakeGame(hp=1)
        self.assertIsNone(self.tick(game, context(game, downs=2, revives=1)))
        self.assertEqual(game.pokes, [])
        self.assertIsNotNone(self.tick(game, context(game, downs=2, revives=2)))  # revived: assisted again

    def test_only_the_seats_own_canonical_player(self):
        game = FakeGame(hp=4)
        self.assertIsNone(self.tick(game, context(game, canonical="0x7FF7014D3300")))  # the clone, not canonical
        self.assertEqual(game.pokes, [])

    def test_a_hit_during_the_check_defers_the_write(self):
        game = FakeGame(hp=10, moves=[10, 6, 6])  # status 10, the row then reads 6, status 6: not agreement
        self.assertIsNone(self.tick(game, context(game)))
        self.assertEqual(game.pokes, [])

    def test_a_failed_readback_is_recorded(self):
        game = FakeGame(hp=8, stick=True)
        event = self.tick(game, context(game))
        self.assertTrue(event["readbackMismatch"])
        self.assertEqual(event["readback"], [8, 22])

    def test_step_needs_the_opt_in_and_saves_its_events(self):
        game = FakeGame(hp=9)
        RUN.kh2ctl = game
        with self.assertRaisesRegex(RUN.StepFailed, "player_hp_refill"):
            RUN.step_hp_assist(context(game, test_only=()), {"do": "hp_assist"})
        ctx = context(game)
        self.assertEqual(RUN.step_hp_assist(ctx, {"do": "hp_assist", "seats": [0], "floor": 12})["floor"], 12)
        with self.assertRaisesRegex(RUN.StepFailed, "already on"):
            RUN.step_hp_assist(ctx, {"do": "hp_assist"})
        ctx.hp_assist.thread.join(timeout=0.5)  # one round runs before the first wait
        result = RUN.step_hp_assist(ctx, {"do": "hp_assist", "on": False, "as": "assist"})
        self.assertIs(ctx.saved["assist"], result)
        self.assertEqual((result["refills"], result["error"], game.pokes[0][3]), (1, "", 22))
        self.assertIsNone(ctx.hp_assist)

    def test_validator_requires_the_opt_in_and_sane_arguments(self):
        steps = [{"do": "boot"}, {"do": "hp_assist", "seats": [0, 1], "floor": 12}, {"do": "hp_assist", "on": False}]
        with self.assertRaisesRegex(ValueError, "player_hp_refill"):
            RUN.validate_scenario({"steps": steps})
        RUN.validate_scenario({"testOnly": ["player_hp_refill"], "steps": steps})
        for bad in ({"floor": 0}, {"floor": "12"}, {"seats": [0, "1"]}, {"seats": 0}, {"on": "no"}):
            with self.subTest(bad), self.assertRaisesRegex(ValueError, "hp_assist needs"):
                RUN.validate_scenario({"testOnly": ["player_hp_refill"],
                                       "steps": [steps[0], {"do": "hp_assist", **bad}]})


class AssistedSplitsTest(unittest.TestCase):
    """Each assisted split is its native split plus exactly two hp_assist steps, opted in and labelled."""

    def load(self, name):
        return json.loads((SCENARIOS / f"{name}.json").read_text(encoding="utf-8"))

    def test_assisted_splits_are_the_native_ones_with_the_assist_around_the_judged_part(self):
        for split, off in (("b1_inter", "m_interleaved1"), ("b2", "owner_downs")):
            with self.subTest(split):
                native = self.load(f"net_prologue_mansion_axel_hp_{split}")
                assisted = self.load(f"net_prologue_mansion_axel_hp_{split}_assisted")
                steps = assisted["steps"]
                on, stop = [i for i, s in enumerate(steps) if s["do"] == "hp_assist"]
                self.assertEqual([s for s in steps if s["do"] != "hp_assist"], native["steps"])
                self.assertEqual(steps[on], {"do": "hp_assist", "seats": [0, 1], "floor": 12})
                self.assertEqual(steps[stop], {"do": "hp_assist", "on": False, "as": "hp_assist"})
                self.assertEqual((steps[on - 1]["do"], steps[on - 1]["role"]), ("runtime", "friend1"))
                before_first_fight = next(i for i, s in enumerate(steps) if s["do"] == "fight")
                self.assertLess(on, before_first_fight)  # the room fight before Axel is covered (hp-13)
                if off == "owner_downs":
                    self.assertEqual(steps[stop + 1]["do"], "owner_downs")  # forced downs are never refilled
                else:
                    self.assertEqual(steps[stop - 1].get("as"), off)
                self.assertEqual(assisted["testOnly"], ["player_hp_refill"])
                self.assertTrue(assisted["description"].startswith("ASSISTED (lead #105)"))
                self.assertEqual(assisted["name"], f"net_prologue_mansion_axel_hp_{split}_assisted")
                RUN.validate_scenario(assisted)
                self.assertNotIn("testOnly", native)


if __name__ == "__main__":
    unittest.main()
