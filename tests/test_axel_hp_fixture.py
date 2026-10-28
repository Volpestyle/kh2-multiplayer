"""VUH-2912: the generated Axel II HP and targeting fixture's own expressions, evaluated against synthetic logs and
actors, with negative controls: an Axel hit or claim before the admitted release, a downed seat that never left
Axel's candidates, and the walk-away target. No game.

    python -B -m unittest discover -s tests -p test_axel_hp_fixture.py -v
"""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STEPS = json.loads((ROOT / "tools/scenario/scenarios/net_prologue_mansion_axel_hp.json").read_text(encoding="utf-8"))["steps"]
RELEASED_HOST = "[combat-admission] released admitted epoch=3 incarnation=1"
RELEASED_FRIEND = "[prologue] control released at=02/14 evt=137 site=tt20-137 localBound=1"
AXEL_HIT = "[hit] frame 900 attacker=B_EX110 (handle 0000A001) atkp=12 -> victim=P_EX110_BTLF@7FF600001000 damage=6 stat=0 hpBefore=22"
ROXAS_HIT = "[hit] frame 900 attacker=P_EX110_BTLF (handle 0000A002) atkp=3 -> victim=B_EX110@7FF600002000 damage=4 stat=0 hpBefore=449"
DUSK_HIT = "[hit] frame 800 attacker=M_EX010 (handle 0000A003) atkp=5 -> victim=P_EX110_BTLF@7FF600001000 damage=2 stat=0 hpBefore=22"
CLAIM = "[enemysync] {} hit claim connection=11 seq=1 epoch=3 netId=1 objectId=81 attackId=1 damage=4"


def target(t, valid, owner=None):
    return (f"[enemy-target] boss-target frame=1 enemy=7FF600002000 objectId=81 target={t} owner={t if owner is None else owner} "
            f"mode=2 valid={valid} changed=1 at=0,0")


def expr(fragment, key="message"):
    """The one step whose message contains `fragment`, or whose `as` is exactly it."""
    found = [s for s in STEPS if (fragment in s.get(key, "") if key == "message" else s.get(key) == fragment)]
    assert len(found) == 1, (fragment, len(found))
    return found[0]


def evaluate(text, logs, saved=None, actors=None, positions=None, peeks=None):
    logs = {i: "\n".join(lines) for i, lines in logs.items()}
    return eval(text, {"__builtins__": {}, "len": len, "int": int, "max": max, "sum": sum, "all": all, "saved": saved or {},
                       "log_matches": lambda p, i: [m.groupdict() for m in re.finditer(p, logs.get(i, ""))],
                       "log_count": lambda t, i: sum(t in line for line in logs.get(i, "").splitlines()),
                       "actors": lambda i: (actors or {}).get(i, []), "pos": lambda name, i: (positions or {})[(name, i)],
                       "peek": lambda rva, kind, i: (peeks or {})[i]})


class AxelHpFixtureTest(unittest.TestCase):
    def test_lease_holds_when_axel_is_untouched_before_release(self):
        lease = expr("the VUH-2897/2906 lease")["expr"]
        before = {0: [DUSK_HIT, RELEASED_HOST, AXEL_HIT, ROXAS_HIT, CLAIM.format("host")],
                  1: [DUSK_HIT, RELEASED_FRIEND, AXEL_HIT, CLAIM.format("client")]}
        self.assertTrue(evaluate(lease, before))
        for seat, line in ((0, AXEL_HIT), (0, ROXAS_HIT), (0, CLAIM.format("host")), (1, AXEL_HIT), (1, CLAIM.format("client"))):
            early = {i: list(lines) for i, lines in before.items()}
            early[seat].insert(1, line)
            self.assertFalse(evaluate(lease, early), (seat, line))

    def test_each_downed_seat_must_leave_axels_candidates(self):
        downed = expr("drop out of its candidates")["expr"]
        self.assertTrue(evaluate(downed, {0: [target(0, 7), target(0, 5), target(1, 6)]}))
        self.assertFalse(evaluate(downed, {0: [target(0, 7), target(0, 5)]}))  # the host never dropped out
        self.assertFalse(evaluate(downed, {0: [target(1, 6)]}))  # the friend never dropped out
        self.assertFalse(evaluate(downed, {0: [target(0, 5).replace("objectId=81", "objectId=2229"), target(1, 6)]}))

    def test_each_round_requires_axel_to_choose_the_fighting_seat(self):
        for name, seat in (("friend_target", 1), ("host_target", 0)):
            chosen = expr(f"{name}: the host's Axel never chose seat {seat}")["expr"]
            base = {f"{name}_base": {"chosen": 1}}
            self.assertTrue(evaluate(chosen, {}, {**base, f"{name}_end": {"chosen": 2}}))
            self.assertFalse(evaluate(chosen, {}, {**base, f"{name}_end": {"chosen": 1}}))
            until = expr(name, key="as")["until"]
            me = {"address": "0x7FF600001000", "name": "P_EX110_BTLF", "hp": 16, "position": {"x": 0, "z": 0}}
            boss = {"address": "0x7FF600002000", "name": "B_EX110", "hp": 440, "position": {"x": 0, "z": 0}}
            logs = {0: [target(seat, 3), CLAIM.format("host") + " beforeHp=449 afterHp=445 nativeOk=1"]}
            saved = {f"{name}_base": {"chosen": 0, "claims": 0, "bossHp": 449, "hp": 22, "downs": 0}}
            actors = {seat: [me], 0: [me, boss] if seat == 0 else [boss]}
            self.assertTrue(evaluate(until, logs, saved, actors, peeks={0: "0x7FF600001000", 1: "0x7FF600001000"}), name)
            logs[0][0] = target(1 - seat, 3)  # Axel chose the other seat
            self.assertFalse(evaluate(until, logs, saved, actors, peeks={0: "0x7FF600001000", 1: "0x7FF600001000"}), name)

    def test_the_reviver_walks_away_from_axel(self):
        for name, seat in (("friend_target_walk_away", 0), ("host_target_walk_away", 1)):
            walk = expr(name, key="as")
            self.assertEqual((walk["instance"], walk["body"]), (seat, "self"))
            me = {"address": "0x7FF600001000", "position": {"x": 100, "z": 0}}
            x, z = evaluate(walk["targetExpr"], {}, actors={seat: [me]}, positions={("B_EX110", seat): (0, 0, 0)},
                            peeks={seat: "0x7FF600001000"})
            self.assertAlmostEqual(x, 900)
            self.assertAlmostEqual(z, 0)

    def test_ledger_phases_and_scope(self):
        ledger = expr("ledger", key="as")
        self.assertEqual((ledger["objectId"], ledger["program"], ledger["window"]), (81, 137, ["m_friend_target0", "m_interleaved1"]))
        self.assertEqual([(p["name"], p["host"], p["friend"], p.get("hitOnHost", False), p.get("hitOnFriend", False))
                          for p in ledger["phases"]],
                         [("friend_target", False, True, False, True), ("host_target", True, False, True, False),
                          ("interleaved", True, True, False, False)])
        boots = [s for s in STEPS if s["do"] == "boot"]
        self.assertTrue(all(b["env"]["KH2COOP_TRACE_HITS"] == "1" and b["env"]["KH2COOP_ENEMY_MIRROR_TRACE"] == "1" for b in boots))
        self.assertFalse(any(s["do"] in ("protect", "warp", "test_enemy_hp") for s in STEPS))


if __name__ == "__main__":
    unittest.main()
