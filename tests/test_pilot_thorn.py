"""VUH-2617 Thorn pilot r1, offline: telegraph dodge, keep-away, revive detour and the base fight hook."""
import importlib.util
import itertools
import json
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


class PolicyTest(unittest.TestCase):
    def setUp(self):
        self.p = THORN.ThornPolicy(CONFIG)

    def test_route_opts_in_with_the_seeded_telegraphs(self):
        self.assertEqual(sorted(self.p.seed), [202, 204, 226])
        self.assertEqual(ROUTE["steps"][0]["pinnedDrop"], 0.55)
        other = json.loads((ROOT / "tools/rig/route_pilot/routes/twilight_thorn_fight.json").read_text(encoding="utf-8"))
        self.assertEqual(other["steps"][0]["thornCombat"], CONFIG)

    def test_seeded_motion_early_and_near_is_a_threat_once_per_instance(self):
        t = thorn(202, time=10)
        self.p.observe(t, own(), 1.0)
        self.assertTrue(self.p.threat(t, own(), 1.0))
        self.p.took_dodge(1.0)
        self.assertFalse(self.p.threat(t, own(), 5.0), "the same motion instance is dodged once")
        self.p.observe(thorn(0), own(), 5.1)
        self.p.observe(thorn(202, time=5), own(), 5.2)
        self.assertTrue(self.p.threat(thorn(202, time=5), own(), 5.2), "a new instance may be dodged again")

    def test_late_far_idle_or_cooling_down_is_not_a_threat(self):
        self.p.observe(thorn(202), own(), 1.0)
        self.assertFalse(self.p.threat(thorn(202, time=90), own(), 1.0), "too late in the motion")
        self.assertFalse(self.p.threat(thorn(202), own(z=-200), 1.0), "farther than the radius (1000)")
        self.assertFalse(self.p.threat(thorn(0), own(), 1.0), "idle motion")
        self.p.took_dodge(1.0)
        self.p.observe(thorn(0), own(), 1.2)
        self.p.observe(thorn(204), own(), 1.4)
        self.assertFalse(self.p.threat(thorn(204), own(), 1.4), "cooldown 1.2 s")
        self.assertTrue(self.p.threat(thorn(204), own(), 2.3))
        off = THORN.ThornPolicy({**CONFIG, "dodge": False})
        off.observe(thorn(202), own(), 1.0)
        self.assertFalse(off.threat(thorn(202), own(), 1.0))

    def test_motions_are_learned_from_own_hp_loss_near_thorn(self):
        hp = 20
        for instance in range(3):
            self.p.observe(thorn(0), own(hp), instance * 2.0)
            self.p.observe(thorn(150), own(hp), instance * 2.0 + 0.6)
            if instance < 2:
                hp -= 3
            self.p.observe(thorn(150), own(hp), instance * 2.0 + 1.2)
        self.assertEqual(self.p.hits.get(150), 2)
        self.assertTrue(self.p.dangerous(150), "2 hits in 3 instances")
        self.assertIn(150, self.p.learned())
        self.assertFalse(self.p.dangerous(0), "the idle motion between them took no hit")

    def test_a_motion_seen_often_with_rare_hits_is_not_learned(self):
        hp = 20
        for instance in range(8):
            self.p.observe(thorn(0), own(hp), instance * 2.0)
            self.p.observe(thorn(160), own(hp), instance * 2.0 + 0.6)
            if instance in (2, 6):
                hp -= 2
            self.p.observe(thorn(160), own(hp), instance * 2.0 + 1.2)
        self.assertEqual(self.p.hits.get(160), 2)
        self.assertFalse(self.p.dangerous(160), "2 of 8 instances is under the 0.34 ratio")

    def test_far_hits_teach_nothing(self):
        self.p.observe(thorn(170), own(20, z=-800), 0.0)
        self.p.observe(thorn(170), own(14, z=-800), 1.0)  # 1600 away: not Thorn's doing
        self.assertNotIn(170, self.p.hits)

    def test_dodge_goes_across_the_line_and_alternates(self):
        first = self.p.dodge_target(own(), thorn())
        self.assertAlmostEqual(first[1], 400 - 150, delta=1, msg="outward, away from Thorn")
        self.assertAlmostEqual(abs(first[0]), 400, delta=1, msg="sideways across Thorn's line")
        self.p.took_dodge(0.0)
        second = self.p.dodge_target(own(), thorn())
        self.assertLess(first[0] * second[0], 0, "the next dodge takes the other side")

    def test_keep_away_only_when_low_and_the_partner_is_alive_and_healthier(self):
        t = thorn()
        self.assertIsNone(self.p.kite(own(20), t, {"hp": 20, "downed": False}), "healthy: fight on")
        self.assertIsNone(self.p.kite(own(5), t, {"hp": 8, "downed": False}), "partner not 4 HP healthier")
        self.assertIsNone(self.p.kite(own(5), t, {"hp": 20, "downed": True}), "partner downed: stay to revive")
        self.assertIsNone(self.p.kite(own(5), t, None), "no partner known (solo host): fight on")
        goal = self.p.kite(own(5, z=400), t, {"hp": 20, "downed": False})
        self.assertEqual(goal, (0.0, 800 - 750), "back out to the 750 ring, on this seat's side")
        self.assertEqual(self.p.kite(own(5, z=-100), t, {"hp": 20, "downed": False}), "hold", "already outside")
        self.assertTrue(self.p.kiting)
        self.assertIsNone(self.p.kite(own(0), t, {"hp": 20, "downed": False}), "downed: never kites")

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
        self.assertEqual(snap["thorn"]["motionId"], 202)
        self.assertIn(202, snap["learned"])


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
        self.assertFalse(self.pilot(peer={"hp": 20, "downed": False}).combat_turn(own(20), [thorn(0)], thorn(0), 400, 300, {}))

    def test_the_base_pilot_never_takes_a_turn(self):
        self.assertFalse(BASE.Pilot.combat_turn(object(), own(), [thorn(202)], thorn(202), 400, 300, {}))


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
