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

    def __init__(self, script=None, revive=3.0, kill_result="Ok", clearance=None):
        self.t, self.counts, self.script = 0.0, {(i, n): 0 for i in (0, 1) for n in (DOWN, UP, FULL)}, dict(script or {})
        self.revive, self.kill_result, self.kills, self.clips, self.guards = revive, kill_result, [], [], []
        self.pending = []  # (time, seat) revives
        self.clearance, self.reads = clearance or (lambda t: 1000.0), []  # nearest enemy to either seat at t

    def clear(self):
        self.reads.append(self.t)
        return self.clearance(self.t)

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
                                        _now=lambda: self.t, _sleep=self.sleep, _clearance=self.clear), ctx, None
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

    # Lead #72 (vuh2912-hp-08): Axel hit the reviver 14 times in 26 s and broke every hold. With `clearOf`, each
    # forced down first waits, bounded, until no living enemy stands within its distance of either seat.
    CLEAR = {"distance": 500, "maxMs": 20000}

    def test_a_forced_down_waits_until_the_enemy_is_clear_of_both_seats(self):
        rig = Rig(clearance=lambda t: 300.0 if t < 4.0 else 650.0)
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        self.assertGreaterEqual(rig.kills[0][0], 4.0)
        kill = next(e for e in result["events"] if "kill" in e)
        self.assertEqual(kill["clear"]["result"], "clear")
        self.assertEqual(kill["clear"]["nearest"], 650.0)
        self.assertGreaterEqual(kill["clear"]["waitedS"], 2.4)  # from the gap's end at 1.5 s

    def test_an_enemy_that_never_clears_still_downs_after_the_bound_and_says_so(self):
        rig = Rig(clearance=lambda t: 120.0)
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        self.assertEqual([seat for _, seat in rig.kills], [1, 0])
        (first, _), (second, _) = rig.kills
        self.assertTrue(21.5 <= first < 22.0)  # the 1.5 s gap, then the 20 s bound
        self.assertTrue(24.5 <= second - first < 25.0)  # the revive, the gap, then a fresh bound
        clears = [e["clear"] for e in result["events"] if "kill" in e]
        self.assertEqual([c["result"] for c in clears], ["timeout", "timeout"])
        self.assertEqual(clears[0]["nearest"], 120.0)

    def test_an_unreadable_clearance_is_not_clear(self):
        rig = Rig(clearance=lambda t: None)
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        clear = next(e for e in result["events"] if "kill" in e)["clear"]
        self.assertEqual((clear["result"], clear["nearest"]), ("timeout", None))
        self.assertGreaterEqual(rig.kills[0][0], 21.5)

    def test_a_natural_down_during_the_wait_is_counted_and_the_next_wait_has_its_own_bound(self):
        rig = Rig(clearance=lambda t: 100.0, script={5.0: lambda r: r.down(1)})
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        self.assertEqual(result["owners"]["1"], {"kind": "natural", "seat": 1})
        self.assertEqual([seat for _, seat in rig.kills], [0])
        self.assertGreaterEqual(rig.kills[0][0], 8.0 + 1.5 + 20.0)  # revived at 8, the gap, then a fresh 20 s

    def test_without_clear_of_nothing_is_read_and_nothing_waits(self):
        rig = Rig(clearance=lambda t: 0.0)
        result, _, error = rig.run()
        self.assertIsNone(error)
        self.assertEqual(rig.reads, [])
        self.assertLess(rig.kills[0][0], 2.0)  # right after the 1.5 s gap
        self.assertFalse(any("clear" in e for e in result["events"]))

    def test_other_kill_results_fail(self):
        rig = Rig(kill_result="InEvent")
        _, _, error = rig.run()
        self.assertIn("returned InEvent", str(error))


if __name__ == "__main__":
    unittest.main()
