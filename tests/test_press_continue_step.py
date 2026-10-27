"""VUH-2911: the scenario step press_continue (the host's native Continue after a full-party Game Over)
and progress_snapshot's opt-in extraRanges, against stand-in peeks, presses and logs. No game.

    python -B -m unittest discover -s tests -p test_press_continue_step.py -v
"""
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("press_continue_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)

RETRY = ("[combat-admission] continue-retry from=02/14 btl=137 to=02/15 door=99 map=2 btl=0 evt=0 "
         "transition=8 armed=0")


def context(run_dir):
    inst = SimpleNamespace(index=0, pid=4242)
    return SimpleNamespace(inst=lambda index: inst, check_all=lambda: None, sleep=lambda seconds: None,
                           run_dir=Path(run_dir), artifacts=[], saved={})


class Game:
    """Game-over task/menu samples in order (the last repeats); a press appends the receipt when `answers`."""

    def __init__(self, samples, answers=True, log=""):
        self.samples, self.answers, self.log, self.presses = list(samples), answers, log, 0

    def read(self):
        task, menu = self.samples.pop(0) if len(self.samples) > 1 else self.samples[0]
        return {"0x2AE8050": hex(task), "0x7435D0": menu}

    def press(self):
        self.presses += 1
        if self.answers:
            self.log += RETRY + "\n"
        return {"ok": True}


class PressContinueTest(unittest.TestCase):
    def run_step(self, game, **step):
        with tempfile.TemporaryDirectory() as tmp:
            ctx = context(tmp)
            try:
                return RUN.step_press_continue(ctx, step, _read=game.read, _press=game.press, _log=lambda: game.log), ctx, None
            except RUN.StepFailed as error:
                saved = json.loads((Path(tmp) / f"{step.get('as', 'retry')}.json").read_text()) \
                    if (Path(tmp) / f"{step.get('as', 'retry')}.json").exists() else None
                return saved, ctx, error

    def test_presses_once_menu_up_and_reads_the_receipt(self):
        game = Game([(0, 255), (0x1234, 3), (0x1234, 8)])
        result, ctx, error = self.run_step(game)
        self.assertIsNone(error)
        self.assertEqual(game.presses, 1)
        self.assertEqual(result["destination"], [2, 21, 99, 2, 0, 0])
        self.assertEqual((result["source"], result["transition"], result["armed"]), ("02/14", 8, 0))
        self.assertEqual(ctx.saved["retry"]["retryRow"], RETRY)
        self.assertEqual(ctx.artifacts, ["retry.json"])

    def test_refuses_a_log_that_already_continued(self):
        game = Game([(0x1234, 8)], log=RETRY + "\n")
        _, _, error = self.run_step(game)
        self.assertIsNotNone(error)
        self.assertEqual(game.presses, 0)

    def test_bounded_presses_without_a_receipt_fail(self):
        game = Game([(0x1234, 8)], answers=False)
        saved, _, error = self.run_step(game, presses=3)
        self.assertIsNotNone(error)
        self.assertEqual(game.presses, 3)
        self.assertIsNone(saved["retryRow"])
        self.assertEqual(len(saved["inputs"]), 3)

    def test_never_presses_outside_the_game_over_menu(self):
        game = Game([(0, 8), (0x1234, 255)])
        _, _, error = self.run_step(game, timeoutMs=300)
        self.assertIsNotNone(error)
        self.assertEqual(game.presses, 0)


class ExtraRangesTest(unittest.TestCase):
    def test_extra_range_is_read_and_compared(self):
        memory = {}

        def read(*args, pid=None):
            specs = args[2].split(",")
            return {"samples": [{s.split(":")[0]: memory.get(int(s.split(":")[0], 16), 0) for s in specs}]}

        with tempfile.TemporaryDirectory() as tmp:
            ctx = context(tmp)
            step = {"instances": [0], "as": "before", "extraRanges": [["forms", 0x34EC, 0x38]]}
            RUN.step_progress_snapshot(ctx, step, _read=read)
            forms = ctx.saved["before"]["instances"]["0"]["ranges"]["forms"]
            self.assertEqual((forms["saveOffset"], forms["length"], len(forms["hex"])), (0x34EC, 0x38, 0x70))
            memory[0x9A98B0 + 0x34EC] = 1 << 16  # the form row's level byte (+2) in its first 8-byte field
            RUN.step_progress_snapshot(ctx, {**step, "as": "after", "compare": "before"}, _read=read)
            diffs = ctx.saved["after"]["instances"]["0"]["diffs"]
            self.assertEqual(diffs["forms"], [{"saveOffset": 0x34EE, "before": 0, "after": 1}])
            self.assertEqual(diffs["characters"], [])
            # Without extraRanges the default ranges are unchanged.
            RUN.step_progress_snapshot(ctx, {"instances": [0], "as": "plain"}, _read=read)
            self.assertNotIn("forms", ctx.saved["plain"]["instances"]["0"]["ranges"])

    def test_only_reads_just_those_ranges_and_sees_byte_16(self):
        memory, calls = {}, []

        def read(*args, pid=None):
            specs = args[2].split(",")
            calls.append(len(specs))
            return {"samples": [{s.split(":")[0]: memory.get(int(s.split(":")[0], 16), 0) for s in specs}]}

        with tempfile.TemporaryDirectory() as tmp:
            ctx = context(tmp)
            step = {"instances": [0], "as": "before", "only": ["characters", "forms"], "extraRanges": [["forms", 0x32F4, 0x230]]}
            RUN.step_progress_snapshot(ctx, step, _read=read)
            self.assertEqual(sorted(ctx.saved["before"]["instances"]["0"]["ranges"]), ["characters", "forms"])
            self.assertEqual(sum(calls), 0xE04 // 8 + 1 + 0x230 // 8)  # every byte of both, nothing else
            memory[0x9A98B0 + 0x24F0 + 0x10] = 0x7F  # byte 16 of the first character record
            RUN.step_progress_snapshot(ctx, {**step, "as": "after", "compare": "before"}, _read=read)
            diffs = ctx.saved["after"]["instances"]["0"]["diffs"]
            self.assertEqual(diffs["characters"], [{"saveOffset": 0x2500, "before": 0, "after": 0x7F}])
            self.assertEqual(diffs["forms"], [])
            with self.assertRaises(RUN.StepFailed):
                RUN.step_progress_snapshot(ctx, {"instances": [0], "only": ["character"]}, _read=read)


if __name__ == "__main__":
    unittest.main()
