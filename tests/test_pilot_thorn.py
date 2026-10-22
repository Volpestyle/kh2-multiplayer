"""VUH-2617 Thorn pilot r1, offline: telegraph dodge, keep-away, revive detour and the base fight hook."""
import importlib.util
import itertools
import json
import math
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools/rig/route_pilot"))
SPEC = importlib.util.spec_from_file_location("route_pilot_thorn", ROOT / "tools/rig/route_pilot/thorn.py")
THORN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(THORN)
BASE = THORN.base
ROUTE = json.loads((ROOT / "tools/rig/route_pilot/routes/twilight_thorn_host_fight.json").read_text(encoding="utf-8"))
CONFIG = ROUTE["steps"][0]["thornCombat"]


def thorn(motion=0, time=10.0, x=0.0, z=800.0, hp=300):
    return {"objectId": 307, "objectType": 12, "motionId": motion, "motionTime": time, "hp": hp,
            "position": {"x": x, "y": 0.0, "z": z}, "address": "0x1"}


def own(hp=20, x=0.0, z=400.0, max_hp=20):
    return {"hp": hp, "maxHp": max_hp, "playerPos": [x, 0.0, z], "pos": [x, 0.0, z], "forward": [0.0, 1.0],
            "world": 2, "room": 34, "free": True, "react": 0}


class NoSleep(unittest.TestCase):
    """The decisions sleep between pad frames; offline they need not."""

    def setUp(self):
        self.saved_sleep = THORN.time.sleep

    def tearDown(self):
        THORN.time.sleep = self.saved_sleep


class Kb:
    def __init__(self):
        self.held, self.log = set(), []

    def hold(self, actions):
        self.held = set(actions)
        self.log.append(tuple(sorted(actions)))

    def tap(self, action, ms=90):
        self.log.append(("tap", action))
        return True


class PolicyTest(unittest.TestCase):
    def setUp(self):
        self.p = THORN.ThornPolicy(CONFIG)

    def test_route_opts_in_with_the_attempt05_bands(self):
        self.assertEqual(self.p.seed[(202, 7)], "ranged", "projectile 1041 from motionTime ~390")
        self.assertEqual(self.p.seed[(202, 2)], "near", "melee 448 around motionTime 160")
        self.assertEqual(self.p.reversals, frozenset({345, 426, 432, 666, 667}))
        self.assertEqual((self.p.kite_below, sorted(self.p.openings_seed)), (0.25, [209, 210, 211]))
        self.assertEqual(ROUTE["steps"][0]["pinnedDrop"], 0.55)
        other = json.loads((ROOT / "tools/rig/route_pilot/routes/twilight_thorn_fight.json").read_text(encoding="utf-8"))
        self.assertEqual(other["steps"][0]["thornCombat"], CONFIG)

    def test_motion_time_bands(self):
        self.assertEqual([self.p.band_of(thorn(202, time=t)) for t in (0, 10, 63, 64, 160, 471)], [0, 0, 0, 1, 2, 7])
        self.assertIsNone(self.p.band_of(thorn(202, time=None)))

    def test_a_near_band_is_a_sidestep_once_per_visit(self):
        t = thorn(202, time=10)
        self.p.observe(t, own(), 1.0)
        self.assertTrue(self.p.threat(t, own(), 1.0))
        self.p.took_dodge(1.0)
        self.assertFalse(self.p.threat(t, own(), 5.0), "the same visit is dodged once")
        self.assertFalse(self.p.threat(t, own(z=-200), 5.0), "farther than the radius (1000)")
        later = thorn(202, time=150)
        self.p.observe(later, own(), 5.2)
        self.assertTrue(self.p.threat(later, own(), 5.2), "a new near band (448's) is a new visit")
        self.p.took_dodge(5.2)
        self.p.observe(thorn(202, time=10), own(), 5.4)
        self.assertFalse(self.p.threat(thorn(202, time=10), own(), 5.4), "cooldown 1.2 s")
        self.assertFalse(self.p.threat(thorn(202, time=420), own(), 9.0), "a ranged band is not a sidestep")
        off = THORN.ThornPolicy({**CONFIG, "dodge": False})
        off.observe(t, own(), 1.0)
        self.assertFalse(off.threat(t, own(), 1.0))

    def test_a_ranged_band_is_telemetry_not_a_move(self):
        self.assertEqual(self.p.danger(202, 7), "ranged", "still learned and logged")
        self.assertFalse(hasattr(self.p, "volley"), "r5 withdrew the ranged-band strafe")
        self.assertFalse(self.p.threat(thorn(202, time=420), own(), 1.0), "and a ranged band is no sidestep")

    def visit(self, motion, time, hp_before, hp_after, at, z=400.0):
        self.p.observe(thorn(0), own(hp_before, z=z), at)
        self.p.observe(thorn(motion, time=time), own(hp_before, z=z), at + 0.6)
        self.p.observe(thorn(motion, time=time), own(hp_after, z=z), at + 1.2)

    def test_bands_are_learned_from_own_hp_loss_near_or_from_range(self):
        hp = 20
        for i in range(3):
            self.visit(150, 200, hp, hp - 3 if i < 2 else hp, i * 2.0, z=-700)  # 1500 away
            hp -= 3 if i < 2 else 0
        self.assertEqual(self.p.hits[(150, 3)], {"near": 0, "ranged": 2})
        self.assertEqual(self.p.danger(150, 3), "ranged", "2 hits in 3 visits, from range")
        self.assertIn("150:3:ranged", self.p.learned())
        for i in range(2):
            self.visit(160, 20, 20, 18, 10 + i * 2.0)
        self.assertEqual(self.p.danger(160, 0), "near")
        self.assertIsNone(self.p.danger(0, 0), "the idle motion between them took no hit")

    def test_a_band_seen_often_with_rare_hits_is_not_learned(self):
        for i in range(8):
            self.visit(170, 100, 20, 18 if i in (2, 6) else 20, i * 2.0)
        self.assertEqual(sum(self.p.hits[(170, 1)].values()), 2)
        self.assertIsNone(self.p.danger(170, 1), "2 of 8 visits is under the 0.34 ratio")

    def test_a_hit_just_after_a_band_change_also_counts_for_the_band_before(self):
        self.p.observe(thorn(180, time=60), own(20), 0.0)
        self.p.observe(thorn(180, time=70), own(20), 0.3)   # band 0 -> 1
        self.p.observe(thorn(180, time=75), own(17), 0.6)   # 0.3 s into band 1
        self.assertIn((180, 0), self.p.hits)
        self.assertIn((180, 1), self.p.hits)

    def test_a_reversal_is_pressed_at_once_then_not_again_for_a_beat(self):
        never = float("-inf")
        self.assertTrue(self.p.reversal(own() | {"react": 432}, 1.0, never))
        self.assertFalse(self.p.reversal(own() | {"react": 432}, 1.15, 1.0), "within 0.2 s of the last press")
        self.assertTrue(self.p.reversal(own() | {"react": 345}, 1.25, 1.0))
        self.assertFalse(self.p.reversal(own() | {"react": 297}, 5.0, never), "event RCs stay the base's")
        self.assertFalse(self.p.reversal(own(), 5.0, never))

    def test_openings_are_seeded_or_learned_from_thorn_hp_lost_in_one_visit(self):
        self.assertTrue(self.p.opening(thorn(210)))
        self.assertFalse(self.p.opening(thorn(202)))
        self.p.observe(thorn(185, hp=300), own(), 0.0)
        self.p.observe(thorn(185, hp=285), own(), 0.5)
        self.assertFalse(self.p.opening(thorn(185)), "15 so far")
        self.p.observe(thorn(185, hp=268), own(), 1.0)
        self.assertTrue(self.p.opening(thorn(185)), "32 lost in this visit")
        self.assertIn(185, self.p.openings())
        self.p.observe(thorn(0, hp=268), own(), 1.5)
        self.p.observe(thorn(185, hp=268), own(), 2.0)
        self.assertFalse(self.p.opening(thorn(185)), "a new visit starts from zero")

    def test_strafe_circles_thorn_and_flips_direction(self):
        t, me = thorn(z=800), own(z=0)
        first = self.p.strafe_target(me, t, 0.0, 800)
        self.assertAlmostEqual(math.hypot(first[0], first[1] - 800), 800, delta=1, msg="on the given ring")
        self.assertAlmostEqual(abs(math.atan2(first[1] - 800, first[0]) - math.atan2(-800, 0)), 0.6, delta=0.01)
        same = self.p.strafe_target(me, t, 1.0, 800)
        self.assertEqual(same, first, "same direction within 2 s")
        flipped = self.p.strafe_target(me, t, 2.5, 800)
        self.assertLess(first[0] * flipped[0], 0, "the other way round after 2 s")

    def test_keep_away_at_a_quarter_hp_only_for_the_lower_seat(self):
        t = thorn()
        self.assertIsNone(self.p.kite(own(6), t, {"hp": 20, "downed": False}, 0.0), "above 25%: fight on")
        self.assertIsNone(self.p.kite(own(5), t, {"hp": 8, "downed": False}, 0.0), "partner not 4 HP healthier")
        self.assertIsNone(self.p.kite(own(5), t, {"hp": 5, "downed": False}, 0.0), "both low: neither stops chipping")
        self.assertIsNone(self.p.kite(own(5), t, {"hp": 20, "downed": True}, 0.0), "partner downed: stay to revive")
        self.assertIsNone(self.p.kite(own(5), t, None, 0.0), "no partner known (solo host): fight on")
        goal = self.p.kite(own(5, z=400), t, {"hp": 20, "downed": False}, 0.0)
        self.assertAlmostEqual(math.hypot(goal[0], goal[1] - 800), 750, delta=1, msg="out on the 750 ring")
        far = self.p.kite(own(5, z=-200), t, {"hp": 20, "downed": False}, 0.0)
        self.assertAlmostEqual(math.hypot(far[0], far[1] - 800), 1000, delta=1, msg="already outside: keeps moving")
        self.assertTrue(self.p.kiting)
        self.assertIsNone(self.p.kite(own(0), t, {"hp": 20, "downed": False}, 0.0), "downed: never kites")

    def test_thorn_hp_lost_is_counted_per_motion(self):
        self.p.observe(thorn(0, hp=300), own(), 0.0)
        self.p.observe(thorn(180, hp=300), own(), 0.5)
        self.p.observe(thorn(180, hp=288), own(), 1.0)
        self.p.observe(thorn(180, hp=280), own(), 1.5)
        self.p.observe(thorn(0, hp=280), own(), 2.0)
        self.assertEqual(self.p.snapshot()["dealtByMotion"], {180: 20})

    def test_snapshot_is_json_telemetry(self):
        self.p.observe(thorn(202), own(), 0.0)
        snap = json.loads(json.dumps(self.p.snapshot()))
        self.assertEqual((snap["thorn"]["motionId"], snap["thorn"]["band"]), (202, 0))
        self.assertIn("202:7:ranged", snap["learned"])
        self.assertEqual(snap["openings"], [209, 210, 211])


class DetourTest(NoSleep):
    def test_detour_goes_around_thorn(self):
        point, target, root = [0.0, 0.0, 0.0], [0.0, 0.0, 1000.0], (100.0, 500.0)
        gx, gz = THORN.detour_goal(point, target, 1, root)
        self.assertLess(gx, 0, "the side away from Thorn's root")
        self.assertAlmostEqual(gz, 500, delta=1)
        self.assertGreater(THORN.detour_goal(point, target, 1, (-100.0, 500.0))[0], 0)
        self.assertLess(THORN.detour_goal(point, target, 1)[0] * THORN.detour_goal(point, target, -1)[0], 0,
                        "without Thorn's root the given side decides")

    def fight(self):
        coop = THORN.CooperativeFight.__new__(THORN.CooperativeFight)
        coop.progress, coop.detour_until, coop.detour_point, coop.detours, coop.detour_side = None, 0.0, None, 0, 1
        pilot = SimpleNamespace(kb=Kb(), thorn_policy=None, writes=[])
        pilot.write = lambda kind, **f: pilot.writes.append((kind, f))
        return coop, pilot

    def test_a_stalled_approach_detours_with_a_moving_jump(self):
        coop, pilot = self.fight()
        state, point, target = own(), [0.0, 0.0, 0.0], [0.0, 0.0, 1000.0]
        straight = coop.approach(pilot, state, point, target, 1000.0, 0.0)
        self.assertEqual(straight, {"up"})
        coop.progress = (0.0, 1000.0)
        keys = coop.approach(pilot, state, point, target, 990.0, 2.1)
        self.assertEqual(coop.detours, 1)
        self.assertIn(("coop_detour"), [kind for kind, _ in pilot.writes])
        self.assertIn(tuple(sorted(keys | {"jump"})), pilot.kb.log, "the stick stays held through the jump")
        self.assertEqual(coop.approach(pilot, state, point, target, 990.0, 2.5), keys, "the detour holds its waypoint")
        coop.progress = (3.2, 990.0)
        self.assertEqual(coop.approach(pilot, state, point, target, 900.0, 5.3), {"up"}, "progress resumes straight")
        self.assertEqual(coop.detours, 1)

    def test_detours_are_bounded(self):
        coop, pilot = self.fight()
        state, point, target = own(), [0.0, 0.0, 0.0], [0.0, 0.0, 1000.0]
        now = 0.0
        for _ in range(30):
            coop.progress = (now, 1000.0)
            now += 3.0
            coop.approach(pilot, state, point, target, 1000.0, now)
        self.assertEqual(coop.detours, THORN.CooperativeFight.MAX_DETOURS)

    def test_near_the_peer_no_steering(self):
        coop, pilot = self.fight()
        self.assertEqual(coop.approach(pilot, own(), [0.0, 0.0, 0.0], [0.0, 0.0, 60.0], 60.0, 0.0), set())


class ReviveStepTest(unittest.TestCase):
    """The real CooperativeFight.step: the detour is for out-of-range approaches only (Quill r1)."""

    class Pad(BASE.PadInput):
        def __init__(self):
            self.held, self.calls = set(), []

        def hold(self, keys):
            self.held = set(keys)
            self.calls.append(sorted(keys))

    def setUp(self):
        self.saved = THORN.time.monotonic, THORN.time.sleep
        self.now = [0.0]
        THORN.time.monotonic, THORN.time.sleep = (lambda: self.now[0]), (lambda _: None)
        self.peer = {"free": True, "roomScope": [2, 34, 1], "playerPos": [0.0, 0.0, 100.0], "player": 2}
        self.coop = THORN.CooperativeFight(SimpleNamespace(pid=1), SimpleNamespace(pid=2, state=lambda: self.peer))
        self.coop.own_log = SimpleNamespace(update=lambda: True, downed=False, full_party=False)
        self.coop.peer_log = SimpleNamespace(update=lambda: True, downed=True, full_party=False, actor=2)
        self.pad = self.Pad()
        self.writes = []
        self.pilot = SimpleNamespace(kb=self.pad, thorn_policy=None,
                                     write=lambda kind, **f: self.writes.append(kind))

    def tearDown(self):
        THORN.time.monotonic, THORN.time.sleep = self.saved

    def own(self, progress=200, frame=10, react=0):
        return {"world": 2, "room": 34, "free": True, "hp": 20, "player": 1, "playerPos": [0.0, 0.0, 0.0],
                "roomScope": [2, 34, 1], "react": react, "forward": [0.0, 1.0],
                "reactObservation": {"source": "downed-prompt-v7", "frame": frame, "reviveProgress": progress,
                                     "promptKind": 1, "promptHide": 0}}

    def tick(self, at, state):
        self.now[0] = at
        start = len(self.pad.calls)
        self.assertTrue(self.coop.step(self.pilot, state))
        return self.pad.calls[start:]

    def test_a_progressing_in_range_hold_never_detours(self):
        identity = (1, 2, (2, 34, 1))
        c = self.coop
        c.hold_started, c.hold_frame, c.hold_identity = 0.0, 0, identity
        c.arm_frame, c.armed_identity, c.last_hold_hp, c.last_progress = 0, identity, 20, 200
        self.assertEqual(self.tick(0.0, self.own(200, 10)), [["react"]])
        for i, at in enumerate((2.1, 4.2, 6.3)):
            self.assertEqual(self.tick(at, self.own(300 + 200 * i, 140 + 120 * i)), [["react"]],
                             "standing still while the bar fills is not a stalled approach")
        self.assertEqual(c.detours, 0)
        self.assertNotIn("coop_detour", self.writes)

    def test_a_stalled_out_of_range_approach_still_detours(self):
        self.peer["playerPos"] = [0.0, 0.0, 1000.0]
        self.assertEqual(self.tick(0.0, self.own()), [["up"]])
        inputs = self.tick(2.1, self.own())
        self.assertEqual(self.coop.detours, 1)
        self.assertIn("coop_detour", self.writes)
        self.assertTrue(any("jump" in keys for keys in inputs))

    def test_entering_revive_range_restarts_the_stall_window(self):
        self.peer["playerPos"] = [0.0, 0.0, 1000.0]
        self.tick(0.0, self.own())
        self.peer["playerPos"] = [0.0, 0.0, 100.0]
        self.tick(1.5, self.own())
        self.assertIsNone(self.coop.progress)
        self.peer["playerPos"] = [0.0, 0.0, 1000.0]
        self.tick(2.1, self.own())
        self.assertEqual(self.coop.detours, 0, "the out-of-range window starts again, not from 0.0")
        self.tick(4.2, self.own())
        self.assertEqual(self.coop.detours, 1)

    def test_no_detour_during_a_reaction(self):
        self.peer["playerPos"] = [0.0, 0.0, 1000.0]
        self.tick(0.0, self.own(react=5))
        inputs = self.tick(2.1, self.own(react=5))
        self.assertEqual(self.coop.detours, 0)
        self.assertFalse(any("jump" in keys for keys in inputs))


class CombatTurnTest(NoSleep):
    def pilot(self, peer=None):
        pilot = THORN.Pilot.__new__(THORN.Pilot)
        pilot.kb, pilot.thorn_policy, pilot.writes = Kb(), THORN.ThornPolicy(CONFIG), []
        pilot.cooperative = None
        pilot.kite_logged_at = 0.0
        pilot.write = lambda kind, **f: pilot.writes.append((kind, f))
        if peer is not None:
            pilot.peer_vitals = lambda: peer
        return pilot

    def test_a_seeded_telegraph_takes_a_dodge_turn(self):
        pilot, stats = self.pilot(), {}
        self.assertTrue(pilot.combat_turn(own(), [thorn(202)], thorn(202), 400, 300, stats))
        self.assertEqual(stats["dodges"], 1)
        self.assertEqual(pilot.writes[0][0], "thorn_dodge")
        self.assertTrue(any("jump" in entry for entry in pilot.kb.log if entry and entry[0] != "tap"))
        self.assertEqual(pilot.kb.held, set(), "released after the dodge")

    def test_no_turn_outside_thorn_or_while_reacting_or_downed(self):
        for state in (own() | {"react": 297}, own(hp=0), own() | {"free": False}, own() | {"room": 33}):
            self.assertFalse(self.pilot().combat_turn(state, [thorn(202)], thorn(202), 400, 300, {}))
        self.assertFalse(self.pilot().combat_turn(own(), [], {}, 400, 300, {}), "no Thorn listed")
        none = self.pilot(); none.thorn_policy = None
        self.assertFalse(none.combat_turn(own(), [thorn(202)], thorn(202), 400, 300, {}), "policy off: base fight")

    def test_low_with_a_healthy_partner_keeps_away(self):
        pilot, stats = self.pilot(peer={"hp": 20, "downed": False}), {}
        self.assertTrue(pilot.combat_turn(own(5), [thorn(0)], thorn(0), 400, 300, stats))
        self.assertEqual(stats["kites"], 1)
        self.assertEqual(pilot.writes[0][0], "thorn_kite")
        self.assertTrue(pilot.kb.held, "keeps moving: never a standing hold")
        self.assertFalse(self.pilot(peer={"hp": 20, "downed": False}).combat_turn(own(20), [thorn(0)], thorn(0), 400, 300, {}))

    def test_a_reversal_takes_the_turn_before_any_move(self):
        pilot, stats = self.pilot(), {}
        self.assertTrue(pilot.combat_turn(own() | {"react": 432}, [thorn(202)], thorn(202), 400, 300, stats))
        self.assertEqual((pilot.kb.log, stats["reversals"], pilot.writes[0][0]), ([("tap", "react")], 1, "thorn_reversal"))
        pilot.kb.reversal_at = THORN.time.monotonic()  # the pad's cadence record (ReversalCadenceTest drives the real one)
        saved, slept = THORN.time.sleep, []
        THORN.time.sleep = slept.append
        try:
            self.assertTrue(pilot.combat_turn(own() | {"react": 432}, [thorn(202)], thorn(202), 400, 300, stats))
        finally:
            THORN.time.sleep = saved
        self.assertTrue(slept and 0 < slept[0] <= 0.2, "pressed a beat ago: waits out the cadence, then presses")
        self.assertEqual(stats["reversals"], 2)

    def test_an_opening_commits_attacks_without_dodging(self):
        pilot, stats = self.pilot(), {}
        pilot.state = lambda: own()
        boss = thorn(210, time=10)
        self.assertTrue(pilot.combat_turn(own(), [boss], boss, 250, 300, stats))
        self.assertEqual([e for e in pilot.kb.log if e and e[0] == "tap"], [("tap", "attack")] * 3)
        self.assertEqual((stats["commits"], stats.get("dodges", 0)), (1, 0))
        far = self.pilot()
        far.state = lambda: own()
        self.assertTrue(far.combat_turn(own(z=-200), [boss], boss, 1000, 300, {}))
        self.assertEqual(far.kb.log, [("up",)], "out of reach: approach Thorn")
        stop = self.pilot()
        stop.state = lambda: own() | {"react": 432}
        self.assertTrue(stop.combat_turn(own(), [boss], boss, 250, 300, {}))
        self.assertEqual([e for e in stop.kb.log if e and e[0] == "tap"], [("tap", "attack")], "a reaction breaks the burst")

    def test_a_ranged_band_leaves_the_turn_to_the_base_fight(self):
        pilot, stats = self.pilot(peer={"hp": 20, "downed": False}), {}
        boss = thorn(202, time=420)
        self.assertFalse(pilot.combat_turn(own(z=-1200), [boss], boss, 2000, 300, stats), "approach and attack (r5)")
        self.assertEqual((stats, pilot.writes, pilot.kb.log), ({}, [], []))

    def test_the_base_pilot_never_takes_a_turn(self):
        self.assertFalse(BASE.Pilot.combat_turn(object(), own(), [thorn(202)], thorn(202), 400, 300, {}))


class ReversalCadenceTest(unittest.TestCase):
    """r4 (Quill r3): the real base fight loop and the Thorn hook together, with the real Thorn Pad.tap pulse
    (180 ms) on a fake clock. Every Reversal press edge, base or child, respects the one cadence."""

    class Pad(THORN.Pad):
        def __init__(self, clock, presses):
            self.held, self.clock, self.presses = set(), clock, presses

        def hold(self, keys):
            if "react" in keys and "react" not in self.held:
                self.presses.append((round(self.clock[0], 5), self.react_command))
            self.held = set(keys)

    def setUp(self):
        self.saved = BASE.time.monotonic, BASE.time.sleep, BASE.enemies
        self.clock = [10.0]
        BASE.time.monotonic = lambda: self.clock[0]
        BASE.time.sleep = lambda sec: self.clock.__setitem__(0, self.clock[0] + sec)
        boss = thorn(202, time=0) | {"position": {"x": 0.0, "y": 0.0, "z": 100.0}}
        BASE.enemies = lambda *a, **k: [boss]

    def tearDown(self):
        BASE.time.monotonic, BASE.time.sleep, BASE.enemies = self.saved

    def run_loop(self, react_at, cooperative=None, until=12.0):
        """The real fight loop until `until`; `react_at(t)` is the command on screen at time t."""
        presses = []
        p = THORN.Pilot.__new__(THORN.Pilot)
        p.kb = self.Pad(self.clock, presses)
        p.kb.thorn_combat = True
        p.kb.reversals, p.kb.reversal_gap = THORN.ThornPolicy(CONFIG).reversals, 0.2
        p.cooperative, p.game, p.kite_logged_at = cooperative, SimpleNamespace(pid=1), 0.0
        p.thorn_policy = THORN.ThornPolicy(CONFIG)
        p.write = lambda *a, **k: None
        def state():
            s = own() | {"roomScope": [2, 34, 0, 157, 157, 157], "event": False, "inField": True, "menu": 255,
                         "battle": 2, "player": 1, "react": react_at(self.clock[0]), "playerPos": [0.0, 0.0, 0.0],
                         "pos": [0.0, 0.0, 0.0]}
            p.kb.react_command = s["react"]
            return s
        p.state = state
        try:
            BASE.Pilot.fight(p, {"reach": 500, "lowHp": 0, "reactGapSec": 0.35}, until)
        except BASE.Stuck:
            pass
        return presses

    def test_a_persistent_reversal_is_pressed_at_the_one_cadence(self):
        presses = self.run_loop(lambda t: 432)
        starts = [t for t, _ in presses]
        gaps = [round(b - a, 5) for a, b in zip(starts, starts[1:])]
        self.assertGreater(len(starts), 5)
        self.assertTrue(all(g >= 0.2 for g in gaps), gaps)
        self.assertLess(max(gaps[1:]), 0.25, "after the first lock-on turn the child holds the turn for the cadence, "
                        "not an attack burst")

    def test_an_event_rc_right_after_a_reversal_is_not_held_back(self):
        presses = self.run_loop(lambda t: 432 if t < 10.5 else 297, until=11.2)
        first = min(t for t, c in presses if c == 297)
        self.assertLessEqual(first - 10.5, 0.36 + 0.2, "297 waits only for the base's own 0.35 s gap and the pulse")
        pad = self.Pad(self.clock, [])
        pad.thorn_combat, pad.reversals, pad.reversal_gap = True, frozenset({432}), 0.2
        pad.react_command = 432
        self.assertTrue(pad.tap("react"))
        self.assertFalse(pad.tap("react"), "a Reversal 0.18 s after the last one is dropped")
        pad.react_command = 297
        self.assertTrue(pad.tap("react"), "a reaction outside the Reversal list is never gated")
        self.assertEqual([c for _, c in pad.presses], [432, 297])

    def test_a_downed_peer_wins_the_next_state(self):
        coop = SimpleNamespace(step=lambda pilot, s, rc: self.clock[0] >= 10.6,
                               peer=SimpleNamespace(state=lambda: {"hp": 20, "maxHp": 20}),
                               own_log=SimpleNamespace(downed=False, full_party=False, update=lambda: True),
                               peer_log=SimpleNamespace(downed=False, full_party=False, update=lambda: True))
        presses = self.run_loop(lambda t: 432, cooperative=coop, until=11.2)
        self.assertTrue(presses and all(t < 10.6 for t, _ in presses),
                        "once the cooperative revive takes the turn, no base or child Reversal press")


class BaseHookTest(unittest.TestCase):
    """The base fight loop asks combat_turn after choosing its target, and a taken turn skips the attack."""

    def run_fight(self, turn):
        pilot = BASE.Pilot.__new__(BASE.Pilot)
        pilot.kb, pilot.cooperative, pilot.game = Kb(), None, SimpleNamespace(pid=1)
        pilot.write = lambda *a, **k: None
        full = own() | {"roomScope": [2, 34, 0, 157, 157, 157], "event": False, "inField": True, "menu": 255,
                        "battle": 2, "player": 1}
        states = itertools.repeat(full)
        pilot.state = lambda: dict(next(states))
        foes = itertools.chain([[thorn()], [thorn()], [thorn()]], itertools.repeat([]))
        saved_enemies, saved_sleep = BASE.enemies, BASE.time.sleep
        BASE.enemies, BASE.time.sleep = (lambda pid, timeout=60: next(foes)), (lambda _: None)
        calls = []
        pilot.combat_turn = lambda *a: calls.append(a) or turn
        try:
            stats = pilot.fight({"reach": 500, "clearSec": 0, "lowHp": 0}, BASE.time.monotonic() + 5)
        finally:
            BASE.enemies, BASE.time.sleep = saved_enemies, saved_sleep
        return stats, calls, pilot.kb.log

    def test_turn_taken_skips_the_attack(self):
        stats, calls, log = self.run_fight(True)
        self.assertEqual(len(calls), 3)
        self.assertEqual(stats["swings"], 0)

    def test_no_turn_keeps_the_base_attack(self):
        stats, calls, log = self.run_fight(False)
        self.assertEqual(len(calls), 3)
        self.assertGreater(stats["swings"], 0)


if __name__ == "__main__":
    unittest.main()
