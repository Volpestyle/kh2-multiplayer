"""VUH-2911: the scenario step owner_downs (one logical down and partner revive per owner, tolerant of natural
downs), against a simulated clock, log counts, Kill channel and revive guards. No game.

    python -B -m unittest discover -s tests -p test_owner_downs_step.py -v
"""
import importlib.util
import threading
import unittest
from types import SimpleNamespace

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("owner_downs_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)

DOWN, UP, FULL = RUN.DOWNED_DOWN, RUN.DOWNED_REVIVED, RUN.DOWNED_FULL


class Rig:
    """Log counts per seat; `script` maps a simulated second to callbacks; a Kill downs its seat (Ok) and the
    other seat's guard revives it `revive` seconds later unless that seat is down too."""

    def __init__(self, script=None, revive=3.0, kill_result="Ok"):
        self.t, self.counts, self.script = 0.0, {(i, n): 0 for i in (0, 1) for n in (DOWN, UP, FULL)}, dict(script or {})
        self.revive, self.kill_result, self.kills, self.clips, self.guards = revive, kill_result, [], [], []
        self.pending = []  # (time, seat) revives

    def down(self, seat):
        self.counts[(seat, DOWN)] += 1
        self.pending.append((self.t + self.revive, seat))

    def is_down(self, seat):
        return self.counts[(seat, DOWN)] > self.counts[(seat, UP)]

    def sleep(self, seconds):
        self.t = round(self.t + seconds, 2)
        for at in sorted(k for k in self.script if k <= self.t):
            self.script.pop(at)(self)
        for at, seat in list(self.pending):
            if at <= self.t:
                self.pending.remove((at, seat))
                if not self.is_down(1 - seat):  # a downed reviver revives nobody
                    self.counts[(seat, UP)] += 1
                else:
                    self.pending.append((at + 0.5, seat))

    def kill(self, seat):
        self.kills.append((self.t, seat))
        if self.kill_result == "Ok":
            self.down(seat)
        return self.kill_result

    def guard(self, downed, reviver):
        g = SimpleNamespace(stop=threading.Event(), events=[{"host": downed, "friend": reviver}],
                            thread=SimpleNamespace(start=lambda: None, join=lambda timeout=None: None))
        self.guards.append(g)
        return g

    def run(self, **step):
        ctx = SimpleNamespace(check_all=lambda: None, saved={}, sleep=self.sleep)
        try:
            return RUN.step_owner_downs(ctx, {"owners": [1, 0], **step}, _count=lambda i, n: self.counts[(i, n)],
                                        _kill=self.kill, _guard=self.guard, _clip=self.clips.append,
                                        _now=lambda: self.t, _sleep=self.sleep), ctx, None
        except RUN.StepFailed as error:
            return None, ctx, error


class OwnerDownsTest(unittest.TestCase):
    def test_forced_path_downs_each_owner_once_in_order(self):
        rig = Rig()
        result, ctx, error = rig.run()
        self.assertIsNone(error)
        self.assertEqual([seat for _, seat in rig.kills], [1, 0])
        self.assertEqual(result["owners"], {"1": {"kind": "forced", "seat": 1}, "0": {"kind": "forced", "seat": 0}})
        # The second Kill waits for the first revive plus the gap: never two open downings.
        self.assertGreaterEqual(rig.kills[1][0], 3.0 + 1.5)
        self.assertEqual(len(rig.clips), 2)
        self.assertTrue(all(g.stop.is_set() for g in rig.guards))
        self.assertEqual([g.events[0] for g in rig.guards], [{"host": 0, "friend": 1}, {"host": 1, "friend": 0}])
        self.assertIs(ctx.saved["owner_downs"], result)

    def test_natural_down_counts_and_is_not_forced_again(self):
        rig = Rig(script={0.0: lambda r: r.down(0)})  # Axel downs the host before the first Kill
        result, _, error = rig.run()
        self.assertIsNone(error)
        self.assertEqual([seat for _, seat in rig.kills], [1])  # only the friend is forced
        self.assertEqual(result["owners"]["0"], {"kind": "natural", "seat": 0})
        self.assertEqual(result["owners"]["1"], {"kind": "forced", "seat": 1})
        self.assertGreater(rig.kills[0][0], 3.0)  # not while the host was down

    def test_both_down_is_a_harness_failure(self):
        rig = Rig(revive=10.0, script={2.0: lambda r: r.down(0)})  # the reviver falls while the friend is down
        result, ctx, error = rig.run()
        self.assertIsNone(result)
        self.assertIn("HARNESS FAILURE, both seats down", str(error))
        self.assertEqual([seat for _, seat in rig.kills], [1])
        self.assertTrue(all(g.stop.is_set() for g in rig.guards))
        self.assertIsNone(ctx.saved["owner_downs"]["owners"]["0"])
        # VUH-2912 (vuh2912-hp-05): the guards' events ride on the failure, in its text and its report details.
        guards = [g.events for g in rig.guards]
        self.assertIn('guards [[{"host":0,"friend":1}],[{"host":1,"friend":0}]]', str(error))
        self.assertEqual(error.details, {"owner_downs": ctx.saved["owner_downs"]})
        self.assertEqual(error.details["owner_downs"]["guards"], guards)

    def test_every_failure_carries_the_guard_events(self):
        for rig, step in ((Rig(revive=1e9), {"timeoutMs": 20000}), (Rig(kill_result="InEvent"), {}),
                          (Rig(revive=10.0, script={0.5: lambda r: r.counts.__setitem__((0, FULL), 1)}), {})):
            _, ctx, error = rig.run(**step)
            self.assertIn("; guards [[", str(error))
            self.assertEqual(error.details["owner_downs"]["guards"], [g.events for g in rig.guards])

    def test_a_pass_carries_no_failure_details(self):
        result, ctx, error = Rig().run()
        self.assertIsNone(error)
        self.assertEqual(result["guards"], [[{"host": 0, "friend": 1}], [{"host": 1, "friend": 0}]])

    def test_full_party_line_is_a_harness_failure(self):
        rig = Rig(revive=10.0, script={0.5: lambda r: r.counts.__setitem__((0, FULL), 1)})
        _, _, error = rig.run()
        self.assertIn("HARNESS FAILURE, both seats down", str(error))

    def test_no_revive_times_out_as_a_harness_failure(self):
        rig = Rig(revive=1e9)
        _, _, error = rig.run(timeoutMs=20000)
        self.assertIn("no revive within 20000 ms", str(error))
        self.assertEqual(len(rig.kills), 1)

    def test_wrong_state_kill_waits_for_that_natural_downing(self):
        rig = Rig(kill_result="WrongState", script={0.6: lambda r: r.down(1), 6.0: lambda r: r.down(0)})
        result, _, error = rig.run()
        self.assertIsNone(error)
        self.assertEqual(result["owners"], {"1": {"kind": "natural", "seat": 1}, "0": {"kind": "natural", "seat": 0}})

    def test_other_kill_results_fail(self):
        rig = Rig(kill_result="InEvent")
        _, _, error = rig.run()
        self.assertIn("returned InEvent", str(error))


if __name__ == "__main__":
    unittest.main()
