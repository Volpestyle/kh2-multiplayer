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
        self.caps = []

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

    # Ione 0b4cdc89 (1): every clearance read ends by the wait's bound and the step's deadline, and a Kill follows
    # only while the step has budget and nothing changed during the read.
    def slow(self, rig, seconds, value, then=None):
        """A clearance read that takes `seconds` of the rig's clock unless its kh2ctl deadline cuts it short."""
        def read(t):
            cap = RUN.rpc_timeout(1e9)  # what kh2ctl would get: the caller's rpc_deadline
            rig.caps.append(cap)
            rig.t = round(rig.t + min(seconds, cap), 2)
            if then:
                then(rig)
            if seconds > cap:
                raise RUN.subprocess.TimeoutExpired("kh2ctl entities", cap)
            return value
        rig.caps = []
        return read

    def test_a_slow_false_read_never_overruns_the_step(self):
        rig = Rig()
        rig.clearance = self.slow(rig, 30.0, 100.0)
        _, _, error = rig.run(clearOf=self.CLEAR, timeoutMs=20000)
        self.assertIn("no revive within 20000 ms", str(error))
        self.assertEqual(rig.kills, [])  # the old code read for 30 s, then forced a down at about 31.5 s
        self.assertTrue(all(cap <= 20.0 - 1.5 + 1e-9 for cap in rig.caps[:1]))  # capped to the step's deadline

    def test_a_slow_true_read_inside_the_bounds_still_downs(self):
        rig = Rig()
        rig.clearance = self.slow(rig, 5.0, 650.0)
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        kill = next(e for e in result["events"] if "kill" in e)
        self.assertEqual(kill["clear"]["result"], "clear")
        self.assertGreaterEqual(rig.kills[0][0], 6.5)

    def test_a_read_past_the_wait_bound_is_the_labelled_timeout(self):
        rig = Rig()
        rig.clearance = self.slow(rig, 30.0, 650.0)  # a clear answer, but only after the 20 s bound: unproven
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        clear = next(e for e in result["events"] if "kill" in e)["clear"]
        self.assertEqual((clear["result"], clear["nearest"]), ("timeout", None))
        self.assertAlmostEqual(rig.caps[0], 20.0, places=6)

    def test_a_natural_down_during_a_read_forces_nothing(self):
        rig = Rig(revive=3.0)
        rig.clearance = self.slow(rig, 2.0, 650.0, then=lambda r: r.counts[(1, DOWN)] or r.down(1))
        result, _, error = rig.run(clearOf=self.CLEAR)
        self.assertIsNone(error)
        self.assertEqual(result["owners"]["1"], {"kind": "natural", "seat": 1})
        self.assertEqual([seat for _, seat in rig.kills], [0])
        self.assertGreater(rig.kills[0][0], 1.5 + 2.0 + 3.0)  # not while seat 1 was down

    def test_no_forced_down_once_the_step_is_out_of_time(self):
        rig = Rig()  # seat 1, downed at 1.5, is revived at 4.5; seat 0's down is ready at 6.0, past the 5.9 s deadline
        _, _, error = rig.run(timeoutMs=5900)
        self.assertIn("no revive within 5900 ms", str(error))
        self.assertEqual([seat for _, seat in rig.kills], [1])  # the old code forced seat 0 down at 6.0, then failed

    def test_other_kill_results_fail(self):
        rig = Rig(kill_result="InEvent")
        _, _, error = rig.run()
        self.assertIn("returned InEvent", str(error))


class ClearanceTest(unittest.TestCase):
    """Ione 0b4cdc89 (2): clearance measures each game's canonical player (Game.state's validated playerPos), not the
    camera's target, and a changed or unavailable context is unproven (None)."""

    SCOPE = (2, 20, 0, 137, 137, 137)
    AXEL = [{"objectType": 3, "name": "B_EX110", "hp": 449, "position": {"x": 0.0, "z": 0.0}}]

    def seat(self, player=0xA000, at=(100.0, 0.0, 0.0), camera=(1000.0, 0.0, 0.0), scope=None):
        return {"player": player, "playerPos": at, "pos": camera, "roomScope": scope or self.SCOPE}

    def measure(self, *seats, actors=None):
        """Each seat is (before, after): its state on the read before and after the enemies lookup."""
        games = [SimpleNamespace(state=(lambda rows=list(pair): rows.pop(0))) for pair in seats]
        ctx = SimpleNamespace(actors=actors or (lambda index: self.AXEL))
        return RUN.enemy_clearance(ctx, games)

    def test_players_beside_axel_are_not_clear_whatever_the_cameras_target(self):
        near, other = self.seat(), self.seat(player=0xB000, at=(0.0, 0.0, 150.0))
        self.assertEqual(self.measure((near, near), (other, other)), 100.0)

    def test_the_nearer_seat_decides(self):
        far, near = self.seat(at=(900.0, 0.0, 0.0)), self.seat(player=0xB000, at=(0.0, 0.0, 300.0))
        self.assertEqual(self.measure((far, far), (near, near)), 300.0)

    def test_an_unavailable_canonical_player_is_unproven(self):
        for bad in ({"playerPos": None}, {"player": 0}):
            with self.subTest(bad=bad):
                seat = dict(self.seat(), **bad)
                ok = self.seat(player=0xB000)
                self.assertIsNone(self.measure((seat, seat), (ok, ok)))

    def test_a_changed_player_or_room_across_the_lookup_is_unproven(self):
        ok = self.seat(player=0xB000)
        for after in (self.seat(player=0xC000), self.seat(scope=(2, 21, 0, 0, 0, 0))):
            with self.subTest(after=after):
                self.assertIsNone(self.measure((self.seat(), after), (ok, ok)))

    def test_seats_in_different_rooms_are_unproven(self):
        here, there = self.seat(), self.seat(player=0xB000, scope=(2, 21, 0, 0, 0, 0))
        self.assertIsNone(self.measure((here, here), (there, there)))

    def test_a_lookup_past_its_deadline_is_unproven(self):
        def late(index):
            raise RUN.subprocess.TimeoutExpired("kh2ctl entities", 0)
        seat = self.seat()
        self.assertIsNone(self.measure((seat, seat), (seat, seat), actors=late))

    def test_no_living_enemy_is_clear(self):
        seat = self.seat()
        self.assertEqual(self.measure((seat, seat), actors=lambda index: []), float("inf"))


if __name__ == "__main__":
    unittest.main()
