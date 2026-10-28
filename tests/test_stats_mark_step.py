"""VUH-2912 (lead #76, run vuh2912-hp-09): the scenario step stats_mark, which asks each seat's inject to log the enemy
mirror's per-actor stats line now (inject/src/StatsMark.hpp), against fake channels and a simulated clock. No game.

    python -B -m unittest discover -s tests -p test_stats_mark_step.py -v
"""
import importlib.util
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stats_mark_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)

MAGIC, VERSION = RUN.STATS_MARK_MAGIC, RUN.STATS_MARK_VERSION


class Channel:
    """A seat's channel: `logs_after` reads after a request, the game thread stores it in `logged` (None: never)."""

    def __init__(self, requested=4, logged=4, magic=MAGIC, version=VERSION, logs_after=2, present=True):
        self.row = [magic, version, requested, logged]
        self.logs_after, self.present, self.reads, self.writes, self.closed = logs_after, present, 0, [], False

    def read(self):
        if not self.present:
            return None
        self.reads += 1
        if self.writes and self.logs_after is not None:
            self.logs_after -= 1
            if self.logs_after < 0:
                self.row[3] = self.row[2]
        return tuple(self.row)

    def request(self, seq):
        self.writes.append(seq)
        self.row[2] = seq

    def close(self):
        self.closed = True


class StatsMarkTest(unittest.TestCase):
    def run_step(self, channels, **step):
        clock = [0.0]
        ctx = SimpleNamespace(inst=lambda i: SimpleNamespace(index=i, pid=100 + i))
        opened = []

        def open_channel(pid):
            opened.append(pid)
            return channels[pid - 100]

        def sleep(seconds):
            clock[0] += seconds

        try:
            return RUN.step_stats_mark(ctx, step, _open=open_channel, _now=lambda: clock[0], _sleep=sleep), opened, None
        except RUN.StepFailed as error:
            return None, opened, error

    def test_each_seat_is_asked_once_and_the_step_waits_for_both_receipts(self):
        seats = [Channel(requested=4, logged=4, logs_after=1), Channel(requested=9, logged=9, logs_after=3)]
        result, opened, error = self.run_step(seats)
        self.assertIsNone(error)
        self.assertEqual(opened, [100, 101])
        self.assertEqual(result, {"seq": {0: 5, 1: 10}})
        self.assertEqual([s.writes for s in seats], [[5], [10]])
        self.assertEqual([s.row[3] for s in seats], [5, 10])
        self.assertTrue(all(s.closed for s in seats))

    def test_only_the_named_instances(self):
        seats = [Channel(), Channel()]
        result, opened, error = self.run_step(seats, instances=[1])
        self.assertIsNone(error)
        self.assertEqual((opened, result), ([101], {"seq": {1: 5}}))
        self.assertEqual(seats[0].writes, [])

    def test_a_missing_channel_fails_and_names_the_trace(self):
        seats = [Channel(), Channel(present=False)]
        _, _, error = self.run_step(seats)
        self.assertIn("no stats-mark channel on instance 1", str(error))
        self.assertTrue(all(s.closed for s in seats))

    def test_a_foreign_layout_is_never_written(self):
        for bad in ({"magic": 0x1234}, {"version": VERSION + 1}):
            with self.subTest(bad=bad):
                seat = Channel(**bad)
                _, _, error = self.run_step([seat], instances=[0])
                self.assertIn("magic/version", str(error))
                self.assertEqual(seat.writes, [])

    def test_no_receipt_fails_after_the_timeout(self):
        seats = [Channel(), Channel(logs_after=None)]
        _, _, error = self.run_step(seats, timeoutMs=500)
        self.assertIn("no stats logged on instance(s) [1]", str(error))
        self.assertTrue(all(s.closed for s in seats))

    def test_the_step_is_registered(self):
        self.assertIs(RUN.STEPS["stats_mark"], RUN.step_stats_mark)


if __name__ == "__main__":
    unittest.main()
