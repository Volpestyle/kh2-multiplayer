"""VUH-2911: the scenario step owner_downs (one logical down and partner revive per owner, tolerant of natural
downs), against a simulated clock, log counts, Kill channel and revive guards. No game.

    python -B -m unittest discover -s tests -p test_owner_downs_step.py -v
"""
import importlib.util
import threading
import unittest
from types import SimpleNamespace
from unittest import mock

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("owner_downs_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)

DOWN, UP, FULL = RUN.DOWNED_DOWN, RUN.DOWNED_REVIVED, RUN.DOWNED_FULL
REQUEST, FAULT = RUN.GAME_OVER_REQUEST, RUN.GAME_OVER_FAULT


class Rig:
    """Log counts per seat; `script` maps a simulated second to callbacks; a Kill downs its seat (Ok) and the
    other seat's guard revives it `revive` seconds later unless that seat is down too."""

    def __init__(self, script=None, revive=3.0, kill_result="Ok", clearance=None):
        self.t, self.counts, self.script = 0.0, {(i, n): 0 for i in (0, 1) for n in (DOWN, UP, FULL)}, dict(script or {})
        self.revive, self.kill_result, self.kills, self.clips, self.guards = revive, kill_result, [], [], []
        self.pending = []  # (time, seat) revives
        self.ticks = {0: None, 1: None}  # each seat's latest down line tickMs
        self.seq = {0: [], 1: []}         # each seat's downed lines in log order (downed_sequence)
        self.on_read = {}                 # seat -> callback after its log is read

    def game_over(self, request=True, fault=False):
        """The host's full-party line, by the game-over request unless `request` is false."""
        self.counts[(0, FULL)] += 1
        self.seq[0].append(("full", "game-over-request" if request else "native-death"))
        if fault:
            self.seq[0].append(("fault",))

    def down(self, seat, revive=True):
        self.counts[(seat, DOWN)] += 1
        self.seq[seat].append(("down", f"E{seat}"))
        if revive:
            self.pending.append((self.t + self.revive, seat))

    def revived(self, seat):
        self.counts[(seat, UP)] += 1
        self.seq[seat].append(("up",))

    def read(self, seat):
        seq = list(self.seq[seat])
        if seat in self.on_read:
            self.on_read[seat](self)
        return seq

    def is_down(self, seat):
        return self.counts[(seat, DOWN)] > self.counts[(seat, UP)]

    def sleep(self, seconds):
        self.t = round(self.t + seconds, 2)
        if self.t > 1000:  # every bound the step has is far shorter
            raise AssertionError("owner_downs never ended")
        for at in sorted(k for k in self.script if k <= self.t):
            self.script.pop(at)(self)
        for at, seat in list(self.pending):
            if at <= self.t:
                self.pending.remove((at, seat))
                if not self.is_down(1 - seat):  # a downed reviver revives nobody
                    self.revived(seat)
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

    def run(self, evaluate=None, **step):
        ctx = SimpleNamespace(check_all=lambda: None, saved={}, sleep=self.sleep, eval=evaluate or (lambda text: True))
        try:
            return RUN.step_owner_downs(ctx, {"owners": [1, 0], **step}, _count=lambda i, n: self.counts.get((i, n), 0),
                                        _kill=self.kill, _guard=self.guard, _clip=self.clips.append,
                                        _now=lambda: self.t, _sleep=self.sleep, _tick=self.ticks.get,
                                        _seq=self.read), ctx, None
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

    # VUH-2931 (run vuh2931-sora-01): the Kill waits for killWhen, e.g. Axel clear of both seats.
    def test_kill_when_defers_each_forced_kill(self):
        rig = Rig()
        # Due at 1.5 s (the gap) and clear then; the second is due at 6.0 s (revive at 4.5 + gap), clear only at 12.
        clear = lambda text: text == "axel clear" and (1.5 <= rig.t < 2.0 or 5.0 <= rig.t < 5.5 or rig.t >= 12.0)
        result, _, error = rig.run(evaluate=clear, killWhen="axel clear")
        self.assertIsNone(error)
        self.assertEqual([seat for _, seat in rig.kills], [1, 0])
        self.assertLess(rig.kills[0][0], 2.0)            # due and clear at once
        self.assertGreaterEqual(rig.kills[1][0], 12.0)   # held from 6.0 until Axel was clear again
        self.assertGreater(result["killDeferred"], 0)

    def test_kill_when_never_defers_a_natural_down(self):
        rig = Rig(script={1.0: lambda r: r.down(1), 6.0: lambda r: r.down(0)})
        result, _, error = rig.run(evaluate=lambda text: False, killWhen="never")
        self.assertIsNone(error)
        self.assertEqual(rig.kills, [])
        self.assertEqual(result["owners"], {"1": {"kind": "natural", "seat": 1}, "0": {"kind": "natural", "seat": 0}})

    def test_kill_when_that_cannot_be_evaluated_is_false_and_the_timeout_still_bounds(self):
        rig = Rig()
        def loading(text):
            raise KeyError("B_EX110")
        _, ctx, error = rig.run(evaluate=loading, killWhen="pos('B_EX110', 0)", timeoutMs=20000)
        self.assertIn("no revive within 20000 ms", str(error))
        self.assertEqual(rig.kills, [])
        self.assertGreater(ctx.saved["owner_downs"]["killDeferred"], 0)

    # Ione 9acdd2ef: killWhen's reads run within the step's budget, and the deadline holds before a Kill.
    @staticmethod
    def slow_rpc(rig, seconds, value):
        """A predicate whose one kh2ctl read takes `seconds` unless rpc_deadline caps it sooner."""
        def evaluate(text):
            took = RUN.rpc_timeout(seconds)
            rig.t = round(rig.t + took, 2)
            if took < seconds:
                raise RUN.subprocess.TimeoutExpired("entities", took)
            return value
        return evaluate

    def test_a_slow_true_predicate_is_capped_to_the_step_budget(self):
        rig = Rig()
        _, _, error = rig.run(evaluate=self.slow_rpc(rig, 30.0, True), killWhen="slow", timeoutMs=20000)
        self.assertIn("no revive within 20000 ms", str(error))
        self.assertEqual(rig.kills, [])  # uncapped, it returned True at 31.5 s and Killed seat 1
        self.assertLessEqual(rig.t, 20.5)

    def test_a_slow_false_predicate_is_capped_to_the_step_budget(self):
        rig = Rig()
        _, _, error = rig.run(evaluate=self.slow_rpc(rig, 30.0, False), killWhen="slow", timeoutMs=20000)
        self.assertIn("no revive within 20000 ms", str(error))
        self.assertEqual(rig.kills, [])
        self.assertLessEqual(rig.t, 20.5)

    def test_a_true_predicate_that_ends_past_the_deadline_issues_no_kill(self):
        rig = Rig()
        def late(text):
            rig.t = round(rig.t + 25.0, 2)  # a read the cap does not cover
            return True
        _, _, error = rig.run(evaluate=late, killWhen="late", timeoutMs=20000)
        self.assertIn("no revive within 20000 ms", str(error))
        self.assertEqual(rig.kills, [])

    def test_a_fast_predicate_inside_the_budget_still_kills(self):
        rig = Rig()
        result, _, error = rig.run(evaluate=self.slow_rpc(rig, 0.5, True), killWhen="fast", timeoutMs=60000)
        self.assertIsNone(error)
        self.assertEqual([seat for _, seat in rig.kills], [1, 0])

    def test_rpc_deadline_caps_kh2ctl_and_nests_only_tighter(self):
        clock = [0.0]
        now = lambda: clock[0]
        self.assertEqual(RUN.rpc_timeout(120), 120)  # no deadline: unchanged
        with RUN.rpc_deadline(10.0, now):
            self.assertEqual(RUN.rpc_timeout(120), 10.0)
            with RUN.rpc_deadline(50.0, now):  # a looser inner deadline cannot extend it
                self.assertEqual(RUN.rpc_timeout(120), 10.0)
            with RUN.rpc_deadline(4.0, now):
                self.assertEqual(RUN.rpc_timeout(120), 4.0)
            seen = {}
            with mock.patch.object(RUN.subprocess, "run", lambda cmd, **kw: seen.update(kw) or
                                   SimpleNamespace(stdout='{"ok": true}', stderr="")):
                RUN.kh2ctl("entities", pid=1)
            self.assertEqual(seen["timeout"], 10.0)
            clock[0] = 10.0
            with self.assertRaises(RUN.subprocess.TimeoutExpired):
                RUN.rpc_timeout(120)
        self.assertEqual(RUN.rpc_timeout(120), 120)

    def test_without_kill_when_nothing_is_deferred(self):
        rig = Rig()
        result, _, error = rig.run(evaluate=lambda text: False)
        self.assertIsNone(error)
        self.assertEqual(len(rig.kills), 2)
        self.assertEqual(result["killDeferred"], 0)

    # VUH-2931 (runs vuh2931-sora-01/02, lead #35): the Sora host's natural full-party down may continue to Continue.
    @staticmethod
    def both_down_then_game_over(friend_tick=1000, host_tick=2500, at=4.0, **game_over):
        """The forced Kill downs the friend at 1.5 s; Axel downs the host at 3.0 s before the slow revive; the host
        takes the game over at `at`."""
        def host_down(r):
            r.down(0)
            r.ticks.update({0: host_tick, 1: friend_tick})
        return Rig(revive=30.0, script={3.0: host_down, at: lambda r: r.game_over(**game_over)})

    def test_full_party_continue_passes_on_a_game_over_request_with_both_down(self):
        rig = self.both_down_then_game_over()
        result, ctx, error = rig.run(fullParty="continue")
        self.assertIsNone(error)
        party = result["fullParty"]
        self.assertEqual((party["first"], party["order"], party["by"]), (1, [1, 0], "tickMs"))
        self.assertEqual(party["kinds"], {"0": "natural", "1": "forced"})
        self.assertEqual(party["path"], "game-over-request")
        self.assertGreaterEqual(party["requestAtS"], 4.0)
        self.assertEqual(result["owners"], {"1": None, "0": None})  # nobody was revived: the skipIf steps key on this
        self.assertEqual([seat for _, seat in rig.kills], [1])        # no Kill while a seat is down
        self.assertIs(ctx.saved["owner_downs"], result)

    def test_the_first_seat_down_is_read_from_the_down_lines(self):
        rig = self.both_down_then_game_over(friend_tick=9000, host_tick=2500)  # the host's line is older
        result, _, error = rig.run(fullParty="continue")
        self.assertIsNone(error)
        self.assertEqual((result["fullParty"]["first"], result["fullParty"]["order"]), (0, [0, 1]))

    def test_without_both_down_lines_the_order_is_the_observed_one(self):
        rig = self.both_down_then_game_over(friend_tick=None)
        result, _, error = rig.run(fullParty="continue")
        self.assertIsNone(error)
        self.assertEqual((result["fullParty"]["by"], result["fullParty"]["first"]), ("observed", 1))

    def test_a_full_party_down_while_a_seat_stands_is_a_product_failure(self):
        for name, script in (("nobody down", {0.5: lambda r: r.game_over()}),
                             ("the friend down", {2.0: lambda r: r.game_over()})):
            with self.subTest(name):
                rig = Rig(revive=30.0, script=script)
                result, ctx, error = rig.run(fullParty="continue")
                self.assertIsNone(result)
                self.assertIn("PRODUCT FAILURE (VUH-2931 acceptance 3)", str(error))
                self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_a_full_party_down_that_is_not_the_game_over_request_fails(self):
        for game_over in ({"request": False}, {"fault": True}):
            with self.subTest(**game_over):
                _, _, error = self.both_down_then_game_over(**game_over).run(fullParty="continue")
                self.assertIn("was not a game-over request", str(error))

    def test_both_down_with_no_game_over_request_fails_within_the_bound(self):
        rig = self.both_down_then_game_over(at=1e9)
        _, ctx, error = rig.run(fullParty="continue", fullPartyMs=5000)
        self.assertIn("both seats down and no game-over request within 5000 ms", str(error))
        self.assertLessEqual(rig.t, 3.0 + 5.0 + 0.5)
        self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_a_seat_revived_before_the_game_over_resets_the_bound(self):
        # Both down at 3.0 with fullPartyMs 2000; the friend is revived at 4.5, so the bound never expires.
        def revive_friend(r):
            r.revived(1)
            r.pending = [(at, s) for at, s in r.pending if s != 1]
        rig = Rig(revive=30.0, script={3.0: lambda r: r.down(0), 4.5: revive_friend, 6.0: lambda r: r.game_over()})
        _, _, error = rig.run(fullParty="continue", fullPartyMs=2000)
        self.assertIn("PRODUCT FAILURE", str(error))  # and a game over with the friend standing is acceptance 3

    # Ione 313a3a5d (1): proof that both seats were down before the request, from each seat's own log order.
    def test_a_request_while_the_friend_stands_fails_even_if_the_friend_falls_right_after(self):
        def friend_falls_after_the_host_read(r):  # the friend's down lands just after the host's log is read
            if r.seq[0] and r.seq[0][-1][0] == "full" and not r.is_down(1):
                r.down(1, revive=False)
        rig = Rig(revive=30.0, script={0.5: lambda r: r.down(0), 2.0: lambda r: r.game_over()})
        rig.kill_result = "WrongState"  # no forced Kill: the friend stands until after the request
        rig.on_read[0] = friend_falls_after_the_host_read
        _, ctx, error = rig.run(fullParty="continue")
        self.assertIn("PRODUCT FAILURE (VUH-2931 acceptance 3)", str(error))
        self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_a_friend_down_logged_after_the_request_has_no_witness_and_fails(self):
        # Request at 2.0 while the friend stands; the friend's down lands in the same tick, before the next reads.
        def request_then_friend_down(r):
            r.game_over()
            r.down(1, revive=False)
        rig = Rig(revive=30.0, script={0.5: lambda r: r.down(0), 2.0: request_then_friend_down})
        rig.kill_result = "WrongState"
        _, ctx, error = rig.run(fullParty="continue")
        self.assertIn("no earlier both-down witness", str(error))
        self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_two_downs_and_one_revive_leave_the_friend_standing(self):
        # A re-down within one episode, then one revive: the friend stands although its downs exceed its revives.
        def friend_twice_then_up(r):
            r.down(1, revive=False)
            r.down(1, revive=False)
            r.revived(1)
        rig = Rig(revive=30.0, script={0.5: lambda r: r.down(0), 1.0: friend_twice_then_up, 2.0: lambda r: r.game_over()})
        rig.kill_result = "WrongState"
        _, _, error = rig.run(fullParty="continue")
        self.assertIn("PRODUCT FAILURE (VUH-2931 acceptance 3)", str(error))

    def test_a_revive_or_re_down_between_the_witness_and_the_request_is_not_proven(self):
        for name, change in (("revive and re-down", lambda r: (r.revived(1), r.down(1, revive=False))),
                             ("re-down", lambda r: r.down(1, revive=False))):
            with self.subTest(name):
                def at_request(r, change=change):
                    change(r)
                    r.game_over()
                rig = Rig(revive=30.0, script={3.0: lambda r: r.down(0), 4.0: at_request})
                _, ctx, error = rig.run(fullParty="continue")
                self.assertIn("between the both-down witness and the full-party line", str(error))
                self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_a_host_revived_and_downed_again_at_the_request_is_not_proven(self):
        rig = self.both_down_then_game_over()
        def host_up_and_down(r):
            r.revived(0)
            r.down(0, revive=False)  # down again by the next read, but not the witness's down
        rig.script[4.0] = lambda r: (r.game_over(), host_up_and_down(r))
        _, _, error = rig.run(fullParty="continue")
        self.assertIn("not proven", str(error))

    # Ione 313a3a5d (2): the full-party wait and its acceptance stay inside timeoutMs.
    def test_the_full_party_wait_ends_at_the_step_deadline(self):
        rig = Rig(revive=30.0, script={0.5: lambda r: r.down(0), 1.0: lambda r: r.down(1, revive=False)})
        rig.kill_result = "WrongState"
        _, _, error = rig.run(fullParty="continue", timeoutMs=2000, fullPartyMs=5000)
        self.assertIn("within 2000 ms", str(error))
        self.assertLessEqual(rig.t, 2.25)

    def test_a_request_after_the_step_deadline_does_not_pass(self):
        rig = Rig(revive=30.0, script={0.5: lambda r: r.down(0), 1.0: lambda r: r.down(1, revive=False),
                                       2.25: lambda r: r.game_over()})
        rig.kill_result = "WrongState"
        result, ctx, error = rig.run(fullParty="continue", timeoutMs=2000, fullPartyMs=5000)
        self.assertIsNone(result)
        self.assertIn("within 2000 ms", str(error))
        self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    # Ione ad07d1be (1): friend reads bracket the request, so a revive after the last pre-request friend read fails.
    def test_a_friend_revived_after_its_read_and_before_the_request_is_not_proven(self):
        rig = self.both_down_then_game_over(at=1e9)
        def revive_then_request(r):
            if r.t >= 4.0 and not r.counts[(0, FULL)]:
                r.revived(1)  # after the friend's read, before the host's read
                r.game_over()
        rig.on_read[1] = revive_then_request
        _, ctx, error = rig.run(fullParty="continue")
        self.assertIn("seat(s) [1] revived, downed again or unreadable", str(error))
        self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_an_unreadable_friend_log_at_the_request_is_not_proven(self):
        rig = self.both_down_then_game_over()
        read = rig.read
        rig.read = lambda seat: [] if seat == 1 and rig.counts[(0, FULL)] else read(seat)
        _, _, error = rig.run(fullParty="continue")
        self.assertIn("seat(s) [1] revived, downed again or unreadable", str(error))

    # Ione ad07d1be (2): both bounds hold at acceptance, after the reads that saw the request.
    def test_a_request_read_across_either_bound_does_not_pass(self):
        # Both down at 3.0. The request lands in the poll at `starts`; the friend's reread after it ends at `ends`,
        # past the bound but after the poll's own deadline check.
        for name, starts, ends, bounds in (("fullPartyMs", 7.75, 8.75, {"fullPartyMs": 5000, "timeoutMs": 20000}),
                                           ("timeoutMs", 3.75, 4.5, {"fullPartyMs": 5000, "timeoutMs": 4000})):
            with self.subTest(name):
                rig = self.both_down_then_game_over(at=1e9)
                def slow_read(r, starts=starts, ends=ends):
                    if r.t >= starts and not r.counts[(0, FULL)]:
                        r.game_over()
                    elif r.counts[(0, FULL)]:
                        r.t = ends
                rig.on_read[1] = slow_read
                _, ctx, error = rig.run(fullParty="continue", **bounds)
                self.assertIn(f"accepted at {ends} s, past", str(error))
                self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_a_request_read_that_ends_inside_both_bounds_passes(self):
        rig = self.both_down_then_game_over(at=1e9)
        def slow_read(r):
            if r.t >= 7.75 and not r.counts[(0, FULL)]:
                r.game_over()
            elif r.counts[(0, FULL)]:
                r.t = 7.9
        rig.on_read[1] = slow_read
        result, _, error = rig.run(fullParty="continue", fullPartyMs=5000, timeoutMs=20000)
        self.assertIsNone(error)
        self.assertEqual((result["fullParty"]["witnessAtS"], result["fullParty"]["requestAtS"]), (3.0, 7.9))

    def test_the_parser_reads_each_seats_own_log_order(self):
        host = ("[downed] gate intercept mode=0 episode=90E43BDB00000001 actor=7FF7969BE050 hp=0 tickMs=932101718\n"
                "[downed] full party down episode=90E43BDB00000001: the host takes the native game over (Continue "
                "retries the room) path=game-over-request mode=0\n[downed] full party down: game-over request FAULT\n")
        friend = ("[downed] gate intercept mode=4 logical=1 episode=D339765E00000001 actor=7F hp=14 tickMs=932095468\n"
                  "[downed] revive returned hp=5\n[downed] gate intercept mode=4 logical=1 episode=D339765E00000002 tickMs=1\n")
        self.assertEqual(RUN.downed_sequence(host), [("down", "90E43BDB00000001"), ("full", "game-over-request"), ("fault",)])
        view = RUN.downed_view(RUN.downed_sequence(friend))
        self.assertEqual((view["down"], view["mark"]), (True, (2, "D339765E00000002")))
        self.assertFalse(RUN.downed_view([("down", "E"), ("down", "E"), ("up",)])["down"])  # downs > revives, standing
        self.assertEqual(RUN.downed_view([("down", "E"), ("full", "game-over-request")])["fulls"],
                         [{"path": "game-over-request", "afterOpenDown": True}])

    def test_the_validator_checks_full_party_and_skip_if(self):
        boot = {"do": "boot"}
        downs = {"do": "owner_downs", "owners": [1, 0]}
        RUN.validate_scenario({"steps": [boot, {**downs, "fullParty": "continue", "fullPartyMs": 10000},
                                         {"do": "wait", "ms": 1, "skipIf": "saved['owner_downs']['fullParty'] is not None"}]})
        for bad, message in (({**downs, "fullParty": "pass"}, 'fullParty may only be "continue"'),
                             ({**downs, "fullPartyMs": 0}, "fullPartyMs must be a positive integer"),
                             ({"do": "wait", "ms": 1, "skipIf": "saved["}, "skipIf"),
                             ({"do": "checkpoint", "name": "A", "skipIf": "True"}, "never on a checkpoint")):
            with self.subTest(message), self.assertRaisesRegex((ValueError, SyntaxError), message):
                RUN.validate_scenario({"steps": [boot, bad]})
        for name in ("net_prologue_mansion_axel_form", "net_prologue_mansion_axel_form_sora_host",
                     "net_prologue_mansion_axel_form_sora_friend"):
            RUN.validate_scenario(RUN.json.loads((ROOT / f"tools/scenario/scenarios/{name}.json").read_text(encoding="utf-8")))

    def test_without_the_option_a_game_over_request_is_still_a_harness_failure(self):
        result, ctx, error = self.both_down_then_game_over().run()
        self.assertIn("HARNESS FAILURE, both seats down", str(error))
        self.assertIsNone(ctx.saved["owner_downs"]["fullParty"])

    def test_other_kill_results_fail(self):
        rig = Rig(kill_result="InEvent")
        _, _, error = rig.run()
        self.assertIn("returned InEvent", str(error))


if __name__ == "__main__":
    unittest.main()
