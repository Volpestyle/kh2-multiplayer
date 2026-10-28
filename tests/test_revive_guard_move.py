"""VUH-2912 (run vuh2912-hp-02, Ione 00c3ac51): ReviveGuard's per-tick revive move and its guard loop. While a
proven native RC owns Triangle (Axel's Burst Frontier offer), the reviver steps sideways out of the line through the
nearest enemy instead of waiting in it, only inside a qualified envelope, in fenced pulses, and never after its stop
or budget. No game.

    python -B -m unittest discover -s tests -p test_revive_guard_move.py -v
"""
import importlib.util
import json
import math
import threading
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("revive_guard_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)
GUARD = RUN.ReviveGuard
move = GUARD.revive_move
REACH = GUARD.REACH
AXEL = {"x": 0.0, "z": 0.0}
BURST_FRONTIER = 444
SCOPE = (2, 20, 0, 137, 137, 137)
CHECKPOINT = (2, 21, 99, 2, 0, 0)
ARENA = {"roomScope": list(SCOPE), "center": [0, 300], "radius": 450}
YIELD = {"source": "downed-prompt-v7", "filtered": False, "frame": 900, "promptKind": 0, "promptHide": 7}


class ReviveMoveTest(unittest.TestCase):
    def test_walks_to_the_downed_seat_until_in_reach(self):
        kind, dx, dz = move((0, 0, 500), (0, 0, 800), 0, False, [AXEL], REACH)
        self.assertEqual(kind, "walk")
        self.assertEqual((dx, dz), (0, 300))

    def test_holds_to_revive_in_reach_with_triangle_free(self):
        self.assertEqual(move((0, 0, 500), (0, 0, 560), 0, False, [AXEL], REACH, ARENA), ("hold",))

    def test_a_proven_native_rc_in_reach_steps_aside_off_the_line_through_axel(self):
        # The reviver at 500 in front of Axel (on Burst Frontier's line); the downed seat beside it.
        kind, dx, dz = move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, True, [AXEL], REACH, ARENA)
        self.assertEqual(kind, "aside")
        self.assertAlmostEqual(dx * 0.0 + dz * 500.0, 0.0)  # perpendicular to Axel -> reviver
        self.assertGreater(math.hypot(dx, dz), 0)

    def test_aside_uses_the_nearest_living_enemy(self):
        far = {"x": 0.0, "z": 5000.0}
        arena = {**ARENA, "center": [300, 0]}
        kind, dx, dz = move((300, 0, 0), (300, 0, 50), BURST_FRONTIER, True, [far, AXEL], REACH, arena)
        self.assertEqual(kind, "aside")
        self.assertAlmostEqual(dx * 300 + dz * 0, 0.0)  # perpendicular to the nearer Axel, not the far enemy

    def test_an_unproven_rc_or_no_envelope_waits(self):
        self.assertEqual(move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, False, [AXEL], REACH, ARENA), ("wait",))
        self.assertEqual(move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, True, [AXEL], REACH, None), ("wait",))

    def test_a_native_rc_with_no_enemy_listed_waits(self):
        self.assertEqual(move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, True, [], REACH, ARENA), ("wait",))

    def test_a_native_rc_out_of_reach_still_walks_in(self):
        self.assertEqual(move((0, 0, 0), (0, 0, 400), BURST_FRONTIER, True, [AXEL], REACH, ARENA)[0], "walk")

    def test_the_aside_takes_the_other_side_when_one_endpoint_leaves_the_envelope(self):
        arena = {**ARENA, "center": [-200, 500], "radius": 300}  # +x leaves it, -x stays inside
        kind, dx, dz = move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, True, [AXEL], REACH, arena)
        self.assertEqual(kind, "aside")
        self.assertLess(dx, 0)

    def test_an_aside_whose_endpoints_both_leave_the_envelope_waits(self):
        arena = {**ARENA, "center": [0, 500], "radius": 150}  # holds the reviver, not 300 units to either side
        self.assertEqual(move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, True, [AXEL], REACH, arena), ("wait",))
        outside = {**ARENA, "center": [0, 2000], "radius": 450}  # the reviver itself is outside
        self.assertEqual(move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, True, [AXEL], REACH, outside), ("wait",))


class Stop:
    """threading.Event on the rig's clock: the guard's outer wait advances it, and the run ends at `rig.until`."""

    def __init__(self, rig):
        self.rig, self.flag = rig, False

    def is_set(self):
        return self.flag or self.rig.t >= self.rig.until

    def set(self):
        self.flag = True

    def wait(self, seconds):
        self.rig.sleep(seconds)
        return self.is_set()


class Rig:
    """A downed host (seat 0) within reach of the reviver (seat 1), whose native RC prompt yields Triangle, Axel at
    the origin, a simulated clock and every kh2ctl input recorded. `on_input` and `lookup` change the world."""

    def __init__(self, observation=YIELD, aside=ARENA, until=8.0, lookup=None, on_input=None, before=None):
        self.t, self.until, self.lookups, self.inputs, self.before = 0.0, until, 0, [], before
        self.friend = {"free": True, "pos": (0.0, 0.0, 500.0), "react": BURST_FRONTIER, "reactObservation": observation,
                       "forward": (0.0, 1.0), "roomScope": SCOPE, "player": 0xA000}
        self.host = {"free": True, "pos": (0.0, 0.0, 560.0), "roomScope": SCOPE, "player": 0xB000}
        self.lookup, self.on_input = lookup, on_input
        g = GUARD.__new__(GUARD)
        g.ctx = SimpleNamespace(actors=self.actors)
        g.host, g.friend = SimpleNamespace(pid=10, index=0), SimpleNamespace(pid=11, index=1)
        g.events, g.openError, g.keepAwaySteps, g.aside = [], "", 0, aside
        g.now, g.sleep, g.stop = (lambda: self.t), self.sleep, Stop(self)
        g.pilot = SimpleNamespace(Game=lambda pid: SimpleNamespace(state=lambda: self.read(pid)))
        self.revives, self.on_read = 0, None
        g.count = lambda inst, needle: int(self.t >= 1.0) if needle == "[downed] gate intercept" else self.revives
        self.guard = g

    def sleep(self, seconds):
        self.t = round(self.t + seconds, 3)

    def read(self, pid):
        if self.on_read:
            self.on_read(self)
        return dict(self.host if pid == 10 else self.friend)

    def actors(self, index):
        if self.t >= 1.0:  # the episode's lookups; keep_away's run before the down
            self.lookups += 1
            if self.lookup:
                self.lookup(self)
        elif self.before:
            self.before(self)
        return [{"objectType": 3, "name": "B_EX110", "hp": 900, "position": dict(AXEL)}]

    def kh2ctl(self, *args, pid=None, check=True, timeout=120, env=None):
        ms = int(args[args.index("--duration-ms") + 1])
        self.inputs.append({"t": self.t, "cmd": args[0], "ms": ms, "stopped": self.guard.stop.is_set()})
        self.sleep(ms / 1000)
        if self.on_input:
            self.on_input(self)
        return {"ok": True}

    def run(self):
        with mock.patch.object(RUN, "kh2ctl", self.kh2ctl):
            self.guard.run()
        return self.guard.events


class GuardLoopTest(unittest.TestCase):
    def test_a_proven_native_yield_steps_aside_in_fenced_pulses(self):
        rig = Rig()
        events = rig.run()
        self.assertEqual(len(events), 1)
        pulses = [i for i in rig.inputs if i["cmd"] == "player-input"]
        self.assertTrue(pulses)
        self.assertTrue(all(i["ms"] == GUARD.ASIDE_MS for i in pulses))
        self.assertGreater(events[0]["asides"], 0)
        self.assertLessEqual(events[0]["asidePulses"], GUARD.ASIDE_PULSES * events[0]["asides"])
        self.assertFalse(any(i["stopped"] for i in rig.inputs))

    def test_an_unproven_rc_observation_never_steps_aside_or_looks_up(self):
        raw = {"source": "raw", "filtered": False}  # unknown, frozen, stale or out-of-scope receipts all read raw
        for observation in (raw, {}, None, {**YIELD, "promptHide": 8}):
            with self.subTest(observation=observation):
                rig = Rig(observation=observation)
                events = rig.run()
                self.assertEqual(rig.inputs, [])
                self.assertEqual(rig.lookups, 0)
                self.assertNotIn("asides", events[0])

    def test_the_generic_guard_without_an_envelope_never_steps_aside(self):
        rig = Rig(aside=None)
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertGreater(events[0]["asideDeclined"]["no-envelope"], 0)

    def test_seats_in_different_rooms_send_nothing(self):
        for aside in (ARENA, None):  # the generic guard too (Ione 796116d1)
            for far in (False, True):  # in reach (aside or hold) or out of reach (walk)
                with self.subTest(aside=aside is not None, far=far):
                    rig = Rig(aside=aside)
                    rig.friend["roomScope"] = CHECKPOINT  # host in 02/14, reviver free in 02/15
                    if far:
                        rig.friend["pos"] = (0.0, 0.0, -800.0)  # out of reach, clear of Axel
                    events = rig.run()
                    self.assertEqual(rig.inputs, [])
                    self.assertEqual(events[0]["result"], "context-lost:room")

    def test_both_seats_outside_the_envelopes_room_decline_the_aside(self):
        rig = Rig()
        rig.friend["roomScope"] = rig.host["roomScope"] = CHECKPOINT  # together, but not in Axel's scope
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertEqual(rig.lookups, 0)
        self.assertGreater(events[0]["asideDeclined"]["room"], 0)

    def test_a_replaced_downed_actor_ends_the_episode(self):
        def replace(r):
            r.host["player"] = 0xC000
        rig = Rig(on_input=replace)
        events = rig.run()
        self.assertEqual(len(rig.inputs), 1)
        self.assertEqual(events[0]["asideStops"], {"context": 1})
        self.assertEqual(events[0]["result"], "context-lost:context")

    def test_a_load_during_an_aside_stops_its_pulses(self):
        def cross(r):  # the first pulse crosses into another room scope
            r.friend["roomScope"] = r.host["roomScope"] = CHECKPOINT
        rig = Rig(on_input=cross)
        events = rig.run()
        self.assertEqual(len(rig.inputs), 1)  # no input in the new room
        self.assertEqual(events[0]["asideStops"], {"room": 1})
        self.assertEqual(events[0]["result"], "context-lost:room")

    # Ione 796116d1 (1): a rejected context stops every episode input, not only the aside.
    def test_a_load_or_replaced_actor_during_a_walk_stops_the_walking(self):
        changes = {"load": lambda r: (r.friend.update(roomScope=CHECKPOINT), r.host.update(roomScope=CHECKPOINT)),
                   "respawn": lambda r: r.friend.update(player=0xD000),
                   "replaced": lambda r: r.host.update(player=0xC000)}
        for name, change in changes.items():
            for aside in (ARENA, None):
                with self.subTest(name, aside=aside is not None):
                    rig = Rig(aside=aside, on_input=change)
                    rig.friend["pos"] = (0.0, 0.0, -800.0)  # out of reach (it walks), clear of Axel
                    events = rig.run()
                    self.assertEqual([i["ms"] for i in rig.inputs], [400])  # the first walk only
                    self.assertTrue(events[0]["result"].startswith("context-lost:"), events[0])

    # Ione 796116d1 (2): the circle is rechecked with the remaining endpoint on every pulse.
    def test_a_push_out_of_the_circle_after_a_pulse_stops_the_aside(self):
        def push(r):
            r.friend["pos"] = (0.0, 0.0, 1000.0)  # knocked from z500 to z1000, outside ARENA (centre z300, r450)
        rig = Rig(on_input=push)
        events = rig.run()
        pulses = [i for i in rig.inputs if i["cmd"] == "player-input" and i["ms"] == GUARD.ASIDE_MS]
        self.assertEqual(len(pulses), 1)
        self.assertEqual(events[0]["asideStops"]["geometry"], 1)

    def test_an_aside_that_travels_as_planned_sends_all_its_pulses(self):
        # Axel at the origin, the reviver at z500: the aside heads -x; the circle just holds its 300-unit endpoint.
        def travel(r):
            x, y, z = r.friend["pos"]
            r.friend["pos"] = (x - GUARD.ASIDE_TRAVEL / GUARD.ASIDE_PULSES, y, z)
        rig = Rig(aside={**ARENA, "center": [0, 500], "radius": 310}, on_input=travel)
        events = rig.run()
        first = rig.inputs[:GUARD.ASIDE_PULSES]
        self.assertTrue(all(i["cmd"] == "player-input" and i["ms"] == GUARD.ASIDE_MS for i in first), rig.inputs)
        self.assertNotIn("geometry", events[0].get("asideStops", {}))  # the full 300 from each new spot would leave it

    def test_each_later_pulse_needs_only_its_remaining_endpoint_inside(self):
        # One pulse after an aside starting at the circle's edge: the remaining 225 units fit, the full 300 would not.
        arena = {**ARENA, "center": [0, 500], "radius": 320}
        own = (100.0, 0.0, 500.0)
        self.assertTrue(GUARD.aside_inside(own, 1.0, 0.0, arena, 220.0))
        self.assertFalse(GUARD.aside_inside(own, 1.0, 0.0, arena, 300.0))
        self.assertFalse(GUARD.aside_inside((0.0, 0.0, 1000.0), 1.0, 0.0, arena, 10.0))  # the reviver itself outside
        self.assertFalse(GUARD.aside_inside(own, 0.0, 0.0, arena, 10.0))  # no direction

    def test_a_yield_that_ends_during_an_aside_stops_it(self):
        def free(r):
            r.friend["react"], r.friend["reactObservation"] = 0, {**YIELD, "filtered": True}
        rig = Rig(on_input=free)
        events = rig.run()
        self.assertEqual(rig.inputs[0]["cmd"], "player-input")
        self.assertEqual(events[0]["asideStops"], {"yield-ended": 1})
        self.assertEqual(rig.inputs[1]["cmd"], "player-press")  # Triangle free again: the revive hold

    def test_a_delayed_lookup_with_stop_set_sends_no_input(self):
        def slow(r):  # Ione's probe: a 70 s entities read, the guard stopped while it is pending
            took = RUN.rpc_timeout(70.0)
            r.sleep(took)
            r.guard.stop.set()
            if took < 70.0:
                raise RUN.subprocess.TimeoutExpired("entities", took)
        rig = Rig(lookup=slow, until=200.0)
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertEqual(events[0]["result"], "timeout")
        self.assertLessEqual(rig.t, 1.5 + GUARD.TIMEOUT)  # capped to the episode, not the 120 s CLI default

    def test_a_lookup_past_the_episode_budget_records_it_and_sends_nothing_later(self):
        def slow(r):
            took = RUN.rpc_timeout(70.0)
            r.sleep(took)
            if took < 70.0:
                raise RUN.subprocess.TimeoutExpired("entities", took)
        rig = Rig(lookup=slow, until=200.0)
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertIn("no episode budget left", events[0]["lastError"])
        self.assertEqual(events[0]["result"], "timeout")

    def test_a_lookup_that_returns_after_the_stop_sends_no_input(self):
        def stop_during(r):
            r.sleep(0.2)
            r.guard.stop.set()
        rig = Rig(lookup=stop_during, until=200.0)
        rig.run()
        self.assertEqual(rig.inputs, [])

    def test_a_revive_during_the_lookup_sends_no_aside(self):
        def revive(r):
            r.revives = 1
        rig = Rig(lookup=revive)
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertEqual(events[0]["result"], "revived")

    def test_a_room_change_during_the_lookup_sends_no_aside(self):
        def cross(r):
            r.friend["roomScope"] = r.host["roomScope"] = CHECKPOINT
        rig = Rig(lookup=cross)
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertEqual(events[0]["result"], "context-lost:room")

    # Ione 796116d1 (3): a revive that lands during the post-lookup refresh sends no first pulse.
    def test_a_revive_during_the_post_lookup_refresh_sends_no_pulse(self):
        def arm(r):
            def revive_on_read(rr):
                rr.revives, rr.on_read = 1, None
            r.on_read = revive_on_read
        rig = Rig(lookup=arm)
        events = rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertEqual(events[0]["result"], "revived")
        self.assertNotIn("asides", events[0])  # caught right after the refresh, before any aside starts

    def test_a_revive_after_a_pulse_sends_no_more(self):
        def revive(r):
            r.revives = 1
        rig = Rig(on_input=revive)
        events = rig.run()
        self.assertEqual(len(rig.inputs), 1)
        self.assertEqual(events[0]["asideStops"], {"revived": 1})
        self.assertEqual(events[0]["result"], "revived")

    def test_keep_away_sends_nothing_after_a_lookup_that_returns_after_the_stop(self):
        def stop_during(r):
            r.sleep(0.2)
            r.guard.stop.set()
        rig = Rig(before=stop_during)
        rig.friend["pos"] = (0.0, 0.0, 200.0)  # inside KEEP_AWAY of Axel: it would step away
        rig.run()
        self.assertEqual(rig.inputs, [])
        self.assertEqual(rig.guard.keepAwaySteps, 0)

    def test_keep_away_still_steps_away_when_not_stopped(self):
        rig = Rig(until=0.9)
        rig.friend["pos"] = (0.0, 0.0, 200.0)
        rig.run()
        self.assertEqual(rig.guard.keepAwaySteps, 1)

    def test_no_input_starts_without_its_duration_and_slack_left(self):
        rig = Rig(observation={"source": "raw"}, until=200.0)
        rig.friend["react"] = 0  # a plain revive hold
        events = rig.run()
        holds = [i for i in rig.inputs if i["cmd"] == "player-press"]
        self.assertTrue(holds)
        end = 1.0 + GUARD.TIMEOUT
        self.assertTrue(all(i["t"] + GUARD.HOLD_MS / 1000 + GUARD.INPUT_SLACK <= end + 1e-9 for i in holds))
        self.assertEqual(events[0]["holds"], len(holds))


class DeadlineTest(unittest.TestCase):
    def test_the_episode_deadline_caps_the_entities_call(self):
        clock, seen = [0.0], {}
        with RUN.rpc_deadline(10.0, lambda: clock[0]), \
                mock.patch.object(RUN.subprocess, "run", lambda cmd, **kw: seen.update(kw) or
                                  SimpleNamespace(stdout='{"ok": true, "actors": []}', stderr="")):
            RUN.kh2ctl("entities", pid=1)
            self.assertEqual(seen["timeout"], 10.0)  # not the 120 s CLI default
            clock[0] = 10.0
            with self.assertRaises(RUN.subprocess.TimeoutExpired):
                RUN.kh2ctl("entities", pid=1)


class EnvelopeTest(unittest.TestCase):
    def test_the_envelope_shape_is_validated(self):
        self.assertTrue(RUN.aside_envelope_ok(ARENA))
        for bad in (None, [], {**ARENA, "radius": 0}, {**ARENA, "roomScope": [2, 20]}, {**ARENA, "center": [0]},
                    {**ARENA, "center": [0, float("nan")]}, {**ARENA, "extra": 1}, {**ARENA, "roomScope": [2.0] * 6}):
            with self.subTest(bad=bad):
                self.assertFalse(RUN.aside_envelope_ok(bad))

    def test_only_the_hp_fixture_qualifies_axel_iis_arena(self):
        scenarios = ROOT / "tools/scenario/scenarios"
        hp = json.loads((scenarios / "net_prologue_mansion_axel_hp.json").read_text(encoding="utf-8"))["steps"]
        guards = [s for s in hp if s["do"] in ("revive_guard", "owner_downs") and s.get("on", True)]
        self.assertEqual(len(guards), 3)  # two rounds' guards and the owner downs
        self.assertTrue(all(s["aside"]["roomScope"] == list(SCOPE) and RUN.aside_envelope_ok(s["aside"]) for s in guards))
        for name in ("net_prologue_mansion_axel_form", "net_prologue_mansion_axel_form_sora_host",
                     "net_prologue_mansion_axel_form_sora_friend"):
            steps = json.loads((scenarios / f"{name}.json").read_text(encoding="utf-8"))["steps"]
            self.assertFalse(any("aside" in s for s in steps), name)


if __name__ == "__main__":
    unittest.main()
