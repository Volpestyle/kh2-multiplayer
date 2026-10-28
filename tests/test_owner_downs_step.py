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

    def __init__(self, script=None, revive=3.0, kill_result="Ok"):
        self.t, self.counts, self.script = 0.0, {(i, n): 0 for i in (0, 1) for n in (DOWN, UP, FULL)}, dict(script or {})
        self.revive, self.kill_result, self.kills, self.clips, self.guards = revive, kill_result, [], [], []
        self.pending = []  # (time, seat) revives
        self.ticks = {0: None, 1: None}  # each seat's latest down line tickMs

    def game_over(self, request=True, fault=False):
        """The host's full-party line, by the game-over request unless `request` is false."""
        self.counts[(0, FULL)] += 1
        self.counts[(0, REQUEST)] = self.counts.get((0, REQUEST), 0) + int(request)
        self.counts[(0, FAULT)] = self.counts.get((0, FAULT), 0) + int(fault)

    def down(self, seat):
        self.counts[(seat, DOWN)] += 1
        self.pending.append((self.t + self.revive, seat))

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

    def run(self, evaluate=None, **step):
        ctx = SimpleNamespace(check_all=lambda: None, saved={}, sleep=self.sleep, eval=evaluate or (lambda text: True))
        try:
            return RUN.step_owner_downs(ctx, {"owners": [1, 0], **step}, _count=lambda i, n: self.counts.get((i, n), 0),
                                        _kill=self.kill, _guard=self.guard, _clip=self.clips.append,
                                        _now=lambda: self.t, _sleep=self.sleep, _tick=self.ticks.get), ctx, None
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
            r.counts[(1, UP)] += 1
            r.pending = [(at, s) for at, s in r.pending if s != 1]
        rig = Rig(revive=30.0, script={3.0: lambda r: r.down(0), 4.5: revive_friend, 6.0: lambda r: r.game_over()})
        _, _, error = rig.run(fullParty="continue", fullPartyMs=2000)
        self.assertIn("PRODUCT FAILURE", str(error))  # and a game over with the friend standing is acceptance 3

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
