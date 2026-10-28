"""VUH-2912: the generated Axel II HP and targeting fixture's own expressions and step order, evaluated against
synthetic logs and actors, with negative controls (Ione 24860051): an Axel hit or claim before the admitted release,
a round whose walk left the seats unseparated or whose milestone never came, a baseline taken before the walk, a
round longer than its film, and the ledgers' judged windows. No game.

    python -B -m unittest discover -s tests -p test_axel_hp_fixture.py -v

AXEL_HP_FIXTURE names another generated JSON (for example the f31e14b one) to run these checks against, and
AXEL_HP_SPLITS (comma-separated, default b1_host,b1_inter,b2) the split fixtures the split checks read.
"""
import json
import os
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = os.environ.get("AXEL_HP_FIXTURE", ROOT / "tools/scenario/scenarios/net_prologue_mansion_axel_hp.json")
STEPS = json.loads(Path(FIXTURE).read_text(encoding="utf-8"))["steps"]
RELEASED_HOST = "[combat-admission] released admitted epoch=3 incarnation=1"
RELEASED_FRIEND = "[prologue] control released at=02/14 evt=137 site=tt20-137 localBound=1"
AXEL_HIT = "[hit] frame 900 attacker=B_EX110 (handle 0000A001) atkp=12 -> victim=P_EX110_BTLF@7FF600001000 damage=6 stat=0 hpBefore=22"
ROXAS_HIT = "[hit] frame 900 attacker=P_EX110_BTLF (handle 0000A002) atkp=3 -> victim=B_EX110@7FF600002000 damage=4 stat=0 hpBefore=449"
DUSK_HIT = "[hit] frame 800 attacker=M_EX010 (handle 0000A003) atkp=5 -> victim=P_EX110_BTLF@7FF600001000 damage=2 stat=0 hpBefore=22"
CLAIM = "[enemysync] {} hit claim connection=11 seq=1 epoch=3 netId=1 objectId=81 attackId=1 damage=4"
ROUNDS = (("friend_target", 1, 0), ("host_target", 0, 1))  # (name, fighter, reviver)
HOST, CLONE, AXEL = "0x7FF600001000", "0x7FF600003000", "0x7FF600002000"


def target(t, valid=3):
    return f"[enemy-target] boss-target frame=1 enemy=7FF600002000 objectId=81 target={t} owner={t} mode=2 valid={valid} changed=1 at=0,0"


def step(fragment, key=None):
    """The one step whose `as` is exactly `fragment`, else whose message contains it (`key` forces either)."""
    by_as = [s for s in STEPS if s.get("as") == fragment]
    found = by_as if key == "as" or (key is None and by_as) else [s for s in STEPS if fragment in s.get("message", "")]
    assert len(found) == 1, (fragment, len(found))
    return found[0]


def index(fragment, key="as"):
    return STEPS.index(step(fragment, key))


def evaluate(text, logs=None, saved=None, actors=None, positions=None, peeks=None):
    logs = {i: "\n".join(lines) for i, lines in (logs or {}).items()}
    return eval(text, {"__builtins__": {}, "len": len, "int": int, "max": max, "sum": sum, "all": all, "saved": saved or {},
                       "log_matches": lambda p, i: [m.groupdict() for m in re.finditer(p, logs.get(i, ""))],
                       "log_count": lambda t, i: sum(t in line for line in logs.get(i, "").splitlines()),
                       "actors": lambda i: (actors or {}).get(i, []), "pos": lambda name, i: (positions or {})[(name, i)],
                       "peek": lambda rva, kind, i: (peeks or {})[i]})


def roxas(address, x, hp=22):
    return {"address": address, "name": "P_EX110_BTLF", "hp": hp, "position": {"x": x, "z": 0}}


def assert_background_films(test, steps):
    """Every filmed fight films in the background, and a clip_wait collects it before the next still, filmed fight,
    clip or owner_downs, and before the run ends. That clip_wait requires a finalized pair, as the fight's evidence;
    one collecting only owner_downs' diagnostic clips does not (Ione 2990c3d8)."""
    pending = False
    for i, s in enumerate(steps):
        if s["do"] == "fight" and s.get("clips"):
            test.assertTrue(all(c.get("background") is True for c in s["clips"]), (i, s.get("as")))
            test.assertFalse(pending, (i, s.get("as")))
            pending = True
        elif s["do"] in ("capture", "owner_downs", "clip"):
            test.assertFalse(pending, (i, s["do"], s.get("name")))
        elif s["do"] == "clip_wait":
            test.assertIs(s.get("require") is True, pending, (i, s))
            pending = False
    test.assertFalse(pending, "a run must collect its last fight's clips")


class AxelHpFixtureTest(unittest.TestCase):
    def test_lease_holds_when_axel_is_untouched_before_release(self):
        lease = step("the VUH-2897/2906 lease")["expr"]
        before = {0: [DUSK_HIT, RELEASED_HOST, AXEL_HIT, ROXAS_HIT, CLAIM.format("host")],
                  1: [DUSK_HIT, RELEASED_FRIEND, AXEL_HIT, CLAIM.format("client")]}
        self.assertTrue(evaluate(lease, before))
        for seat, line in ((0, AXEL_HIT), (0, ROXAS_HIT), (0, CLAIM.format("host")), (1, AXEL_HIT), (1, CLAIM.format("client"))):
            early = {i: list(lines) for i, lines in before.items()}
            early[seat].insert(1, line)
            self.assertFalse(evaluate(lease, early), (seat, line))

    # REQUIRED 3: measured separation, baseline after it, milestone reached, window closed at the fight's end.
    def test_each_round_starts_from_measured_separation_with_both_seats_standing(self):
        world = lambda host_x, clone_x: {0: [roxas(HOST, host_x), roxas(CLONE, clone_x)]}
        for name, fighter, reviver in ROUNDS:
            measure = step(f"{name}_separation")["expr"]
            check = step(f"SETUP FAILURE, not a targeting result: {name}")["expr"]
            near, far = (1000, 100) if fighter == 1 else (100, 1000)  # (host_x, clone_x)
            observed = lambda host_x, clone_x, logs=None: evaluate(
                check, saved={f"{name}_separation": evaluate(measure, logs or {}, actors=world(host_x, clone_x),
                                                             positions={("B_EX110", 0): (0, 0, 0)}, peeks={0: HOST})})
            self.assertTrue(observed(*(near, far)), name)
            self.assertFalse(observed(*((150, 100) if fighter == 1 else (100, 150))), name)  # the walk was blocked
            self.assertFalse(observed(near, far, {fighter: ["[downed] gate intercept mode=4 logical=1"]}), name)

    def test_each_round_baseline_and_window_follow_the_separation(self):
        for name, fighter, reviver in ROUNDS:
            walk, measured, base, start = (index(f"{name}_walk_away"), index(f"SETUP FAILURE, not a targeting result: {name}", "message"),
                                           index(f"{name}_base"), index(f"m_{name}0"))
            fight, end = index(name), index(f"m_{name}1")
            self.assertEqual((STEPS[walk]["instance"], STEPS[fight]["instances"]), (reviver, [fighter]))
            self.assertTrue(walk < measured < base < start < fight, name)
            # Nothing (a revive wait, a later choice) inside the window after the fight but its stats mark (lead #76).
            self.assertEqual((end, STEPS[fight + 1]), (fight + 2, {"do": "stats_mark"}), name)

    # vuh2912-hp-04: Axel moved ~400 units after the separation, chose the friend, and never searched again while the
    # host was nearer. The window now opens only on a selection that names the fighter.
    def test_each_window_opens_on_axels_selection_of_the_fighter(self):
        for name, fighter, reviver in ROUNDS:
            guard = [i for i, s in enumerate(STEPS) if s["do"] == "revive_guard" and s.get("host") == fighter
                     and s.get("on", True) and i > index(f"{name}_separation")][0]
            select = index(f"{name}_select")
            check = index(f"SETUP FAILURE (selection), not a targeting result: in 15 s Axel's selection never named the "
                          f"{('host', 'friend')[fighter]} before {name}'s window", "message")
            measured = index(f"SETUP FAILURE, not a targeting result: {name}", "message")
            self.assertTrue(measured < guard < select < check < index(f"{name}_base") < index(f"m_{name}0"), name)
            self.assertEqual((STEPS[guard]["friend"], STEPS[select]["instances"]), (reviver, [fighter]))
            self.assertLessEqual(STEPS[select]["seconds"], 15)
            self.assertEqual(STEPS[select]["until"], STEPS[check]["expr"])
            expr = STEPS[check]["expr"]
            self.assertTrue(evaluate(expr, {0: [target(1 - fighter), target(fighter)]}), name)
            self.assertFalse(evaluate(expr, {0: [target(fighter), target(1 - fighter)]}), name)  # hp-04: it turned away
            self.assertFalse(evaluate(expr, {0: []}), name)                                      # never chose at all

    # Ione fc9e195f: (2) the window's start HP is a fresh host sample equal to Axel's live HP with every friend claim
    # settled; (1) no hit, claim or selection change between that snapshot and the mark, and the selection names the
    # fighter at the mark.
    SAMPLE = "[statehash] native epoch=3 frame={} netId=1 objectId=81 hp={} actor=7FF600002000"
    HOST_CLAIM = "[enemysync] host hit claim connection=11 seq={} epoch=3 netId=1 objectId=81 attackId=1 damage=4 beforeHp=449 afterHp=445 nativeOk=1"
    CLIENT_CLAIM = "[enemysync] client hit claim connection=11 seq={} epoch=3 netId=1 objectId=81 attackId=1 damage=4"
    REJECT = "[hitclaim] reject epoch=3 seq={} slot=1 connection=11 netId=1 objectId=81 damage=4 currentEpoch=3 reason=late"

    def boss(self, hp):
        return {0: [{"address": AXEL, "name": "B_EX110", "hp": hp, "position": {"x": 0, "z": 0}}]}

    def test_each_window_opens_on_a_settled_hp_sample(self):
        for name, fighter, reviver in ROUNDS:
            settle0 = index(f"{name}_settle0")
            wait = STEPS[settle0 + 1]
            self.assertEqual(wait["do"], "wait_until")
            self.assertTrue(index(f"{name}_select") < settle0 < index(f"{name}_base") < index(f"{name}_quiet")
                            < index(f"m_{name}0"), name)
            saved = {f"{name}_settle0": evaluate(STEPS[settle0]["expr"], {0: [self.SAMPLE.format(1, 449)]})}
            self.assertEqual(saved[f"{name}_settle0"], 1)
            fresh = [self.SAMPLE.format(1, 449), self.HOST_CLAIM.format(1), self.SAMPLE.format(2, 445)]
            ok = lambda host, friend=(), hp=445: evaluate(wait["expr"], {0: list(host), 1: list(friend)}, saved, self.boss(hp))  # noqa: E731
            self.assertTrue(ok(fresh, [self.CLIENT_CLAIM.format(1)]), name)
            self.assertFalse(ok(fresh[:2], [self.CLIENT_CLAIM.format(1)]), name)            # no sample since the fight
            self.assertFalse(ok(fresh[:1], hp=449), name)  # no sample since the fight, even with HP still matching
            self.assertFalse(ok(fresh, [self.CLIENT_CLAIM.format(1)], hp=441), name)        # a hit after the last sample
            self.assertFalse(ok(fresh, [self.CLIENT_CLAIM.format(1), self.CLIENT_CLAIM.format(2)]), name)  # in flight
            self.assertTrue(ok(fresh + [self.REJECT.format(2)], [self.CLIENT_CLAIM.format(1), self.CLIENT_CLAIM.format(2)]), name)
            self.assertLessEqual(wait["timeoutMs"], 5000)

    def test_each_window_opens_only_if_nothing_moved_since_the_aligned_snapshot(self):
        for name, fighter, reviver in ROUNDS:
            quiet, mark = step(f"{name}_quiet"), index(f"m_{name}0")
            check = STEPS[mark + 1]
            self.assertIn(f"SETUP FAILURE (window), not a targeting result: {name}'s window", check["message"])
            self.assertEqual(STEPS[mark + 2], {"do": "stats_mark"})  # lead #76: just inside the window
            self.assertEqual(STEPS[mark + 3]["as"], name)  # then the round's fight at once
            host = [target(1 - fighter), target(fighter), self.SAMPLE.format(2, 445), self.HOST_CLAIM.format(1)]
            friend = [self.CLIENT_CLAIM.format(1)]
            snap = evaluate(quiet["expr"], {0: host, 1: friend}, actors=self.boss(445))
            self.assertEqual(snap, {"hits": 1, "claims": 1, "targets": 2, "aligned": True})
            self.assertFalse(evaluate(quiet["expr"], {0: host, 1: friend}, actors=self.boss(441))["aligned"])
            opens = lambda h, f=friend, s=snap: evaluate(check["expr"], {0: h, 1: list(f)}, {f"{name}_quiet": s})  # noqa: E731
            self.assertTrue(opens(host), name)
            self.assertFalse(opens(host + [ROXAS_HIT]), name)                                # a hit on Axel at the mark
            self.assertFalse(opens(host, friend + [self.CLIENT_CLAIM.format(2)]), name)      # a claim sent at the mark
            self.assertFalse(opens(host + [target(1 - fighter)]), name)                      # turned away at the mark
            self.assertFalse(opens(host, s=dict(snap, aligned=False)), name)                 # snapshot was not aligned
            # Ione's probe (1): Axel turned away during the settle and baseline reads, before the snapshot, so nothing
            # moves after it; the window still must not open on the other seat.
            turned = host + [target(1 - fighter)]
            late = evaluate(quiet["expr"], {0: turned, 1: friend}, actors=self.boss(445))
            self.assertFalse(opens(turned, s=late), name)
            # The selection fight may down a seat: both must stand when the window opens (Ione fc9e195f).
            for downed in (0, 1):
                logs = {0: list(host), 1: list(friend)}
                logs[downed].append("[downed] gate intercept mode=4 logical=1")
                snap_d = evaluate(quiet["expr"], logs, actors=self.boss(445))
                self.assertFalse(evaluate(check["expr"], logs, {f"{name}_quiet": snap_d}), (name, downed))
            # A setup claim sent before the snapshot and applied only after the mark is not aligned.
            sent = friend + [self.CLIENT_CLAIM.format(2)]
            crossing = evaluate(quiet["expr"], {0: host, 1: sent}, actors=self.boss(445))
            self.assertFalse(crossing["aligned"], name)
            self.assertFalse(opens(host + [self.HOST_CLAIM.format(2)], sent, crossing), name)

    def test_each_round_must_reach_its_milestone(self):
        for name, fighter, reviver in ROUNDS:
            reached = step(f"{name}: in 20 s Axel never chose")["expr"]
            self.assertTrue(evaluate(reached, saved={name: {"until": {"reached": True}}}))
            self.assertFalse(evaluate(reached, saved={name: {"until": {"reached": False}}}))
            self.assertGreater(index(f"{name}: in 20 s Axel never chose", "message"), index(name))

    def test_each_round_milestone_needs_the_choice_the_hit_and_damage(self):
        for name, fighter, reviver in ROUNDS:
            until = step(name)["until"]
            me = {"address": HOST, "name": "P_EX110_BTLF", "hp": 16, "position": {"x": 0, "z": 0}}
            boss = {"address": AXEL, "name": "B_EX110", "hp": 440, "position": {"x": 0, "z": 0}}
            logs = {0: [target(fighter), CLAIM.format("host") + " beforeHp=449 afterHp=445 nativeOk=1"]}
            saved = {f"{name}_base": {"chosen": 0, "claims": 0, "bossHp": 449, "hp": 22, "downs": 0}}
            actors = {fighter: [me], 0: [me, boss] if fighter == 0 else [boss]}
            peeks = {0: HOST, 1: HOST}
            self.assertTrue(evaluate(until, logs, saved, actors, peeks=peeks), name)
            # Logged on change only: a choice held from the walk, before the baseline, is still the selection.
            saved[f"{name}_base"]["chosen"] = 1
            self.assertTrue(evaluate(until, logs, saved, actors, peeks=peeks), name)
            self.assertFalse(evaluate(until, {0: [target(fighter), target(1 - fighter), *logs[0][1:]]}, saved, actors,
                                      peeks=peeks), name)  # then Axel turned to the other seat
            logs[0][0] = target(1 - fighter)  # Axel chose the other seat
            self.assertFalse(evaluate(until, logs, saved, actors, peeks=peeks), name)
            self.assertFalse(evaluate(until, {0: logs[0][1:]}, saved, actors, peeks=peeks), name)  # no choice logged

    def test_each_round_is_filmed_for_its_whole_length(self):
        for name, fighter, reviver in ROUNDS:
            fight = step(name)
            self.assertEqual(sorted(c["instance"] for c in fight["clips"]), [0, 1])
            self.assertTrue(all(c["seconds"] >= fight["seconds"] for c in fight["clips"]), name)
            self.assertIn("sample", fight)  # positions and Axel's motion, for the direct-aim measurement

    # vuh2912-hp-06: the fight step held the round open ~70 s past its milestone for its clips to encode, and Axel
    # downed both idle seats inside the window. Every fight films in the background, and a clip_wait collects those
    # clips before any still, any later filmed fight or owner_downs (which films too).
    def test_fights_film_in_the_background_and_collect_before_the_next_film_or_still(self):
        assert_background_films(self, STEPS)

    # B1-inter opens interleaved right after the take-over: both seats must stand at its window, as at a round's.
    def test_interleaved_opens_with_both_seats_standing(self):
        check = STEPS[index("m_interleaved0") - 1]
        self.assertEqual(check["message"], "SETUP FAILURE (window), not an HP result: interleaved needs both seats standing")
        self.assertTrue(evaluate(check["expr"], {0: [], 1: []}))
        for downed in (0, 1):
            self.assertFalse(evaluate(check["expr"], {downed: ["[downed] gate intercept mode=4 logical=1"]}), downed)
        revived = ["[downed] gate intercept mode=4 logical=1", "[downed] revive returned hp=5/22"]
        self.assertTrue(evaluate(check["expr"], {0: revived}))

    def test_the_reviver_walks_away_from_axel(self):
        for name, fighter, reviver in ROUNDS:
            walk = step(f"{name}_walk_away")
            self.assertEqual((walk["instance"], walk["body"]), (reviver, "self"))
            x, z = evaluate(walk["targetExpr"], actors={reviver: [roxas(HOST, 100)]},
                            positions={("B_EX110", reviver): (0, 0, 0)}, peeks={reviver: HOST})
            self.assertAlmostEqual(x, 900)
            self.assertAlmostEqual(z, 0)

    # vuh2912-hp-01: Axel knocked the host off a 4 s walk, and nothing had brought the friend near him.
    def test_the_fighter_walks_in_and_the_reviver_walks_again_if_still_too_close(self):
        world = lambda host_x, clone_x: {0: [roxas(HOST, host_x), roxas(CLONE, clone_x)], 1: [roxas(HOST, 1000)]}
        at = dict(positions={("B_EX110", 0): (0, 0, 0), ("B_EX110", 1): (0, 0, 0)}, peeks={0: HOST, 1: HOST})
        for name, fighter, reviver in ROUNDS:
            walk_in, away, retry = step(f"{name}_walk_in"), step(f"{name}_walk_away"), step(f"{name}_walk_away_retry")
            self.assertTrue(index(f"{name}_walk_in") < index(f"{name}_walk_away") < index(f"{name}_walk_away_retry")
                            < index(f"{name}_separation"), name)
            self.assertEqual((walk_in["instance"], walk_in["body"], retry["instance"]), (fighter, "self", reviver))
            self.assertGreaterEqual(min(away["timeoutMs"], retry["timeoutMs"]), 8000)
            x, z = evaluate(walk_in["targetExpr"], actors=world(1000, 1000), **at)
            self.assertAlmostEqual(x, 250)  # in toward Axel, short of him
            self.assertAlmostEqual(z, 0)
            near, far = (1000, 100) if fighter == 1 else (100, 1000)  # (host_x, clone_x) once apart
            self.assertTrue(evaluate(walk_in["until"], actors=world(near, far), **at), name)  # already in: no walk
            self.assertFalse(evaluate(walk_in["until"], actors=world(1000, 1000), **at), name)
            self.assertTrue(evaluate(retry["until"], actors=world(near, far), **at), name)  # apart: the retry does nothing
            self.assertFalse(evaluate(retry["until"], actors=world(near, near), **at), name)
            self.assertFalse(evaluate(retry["until"], {reviver: ["[downed] gate intercept mode=4 logical=1"]},
                                      actors=world(near, far), **at), name)

    # REQUIRED 1, 2 and 4 live in the ledgers; the fixture must ask them for it.
    def test_ledgers_judge_the_rounds_and_the_downs(self):
        ledger = step("ledger")
        self.assertEqual((ledger["objectId"], ledger["program"], ledger["window"]), (81, 137, ["m_friend_target0", "m_interleaved1"]))
        self.assertEqual([(p["name"], p["host"], p["friend"], p.get("hitOnHost", False), p.get("hitOnFriend", False),
                           p.get("cloneZero", False)) for p in ledger["phases"]],
                         [("friend_target", False, True, False, True, True), ("host_target", True, False, True, False, False),
                          ("interleaved", True, True, False, False, False)])
        targets = step("targets")
        self.assertEqual((targets["do"], targets["objectId"], targets["ledger"]), ("target_ledger", 81, "ledger"))
        self.assertEqual([(r["name"], r["from"], r["to"], r["seat"]) for r in targets["rounds"]],
                         [("friend_target", "m_friend_target0", "m_friend_target1", 1),
                          ("host_target", "m_host_target0", "m_host_target1", 0)])
        self.assertEqual(targets["downs"], {"from": "m_downs0", "to": "m_downs1"})
        self.assertTrue(index("m_downs0") < index("owner_downs") < index("m_downs1") < index("targets"))
        self.assertGreater(index("targets"), index("ledger"))
        self.assertEqual(evaluate(step("drop each downed seat", "message")["expr"], saved={"targets": {"pass": False}}), False)

    def test_diagnostics_and_no_stand_ins(self):
        boots = [s for s in STEPS if s["do"] == "boot"]
        self.assertTrue(all(b["env"]["KH2COOP_TRACE_HITS"] == "1" and b["env"]["KH2COOP_ENEMY_MIRROR_TRACE"] == "1" for b in boots))
        self.assertFalse(any(s["do"] in ("protect", "warp", "test_enemy_hp") for s in STEPS))


SCENARIOS = ROOT / "tools/scenario/scenarios"
SPLIT = {k: json.loads((SCENARIOS / f"net_prologue_mansion_axel_hp_{k}.json").read_text(encoding="utf-8"))["steps"]
         for k in os.environ.get("AXEL_HP_SPLITS", "b1_host,b1_inter,b2").split(",")}
FULL = json.loads((SCENARIOS / "net_prologue_mansion_axel_hp.json").read_text(encoding="utf-8"))["steps"]
SINGLE = {"b1_host": "host_target", "b1_inter": "interleaved"}  # lead #56: one judged round per run


class AxelHpSplitTest(unittest.TestCase):
    """Lead #37/#49/#56: B1-host is the host round, B1-inter interleaved, B2 the downs, each judged alone. Every other
    step is the full fixture's own, unchanged."""

    @staticmethod
    def named(steps, name):
        found = [s for s in steps if s.get("as") == name]
        assert len(found) == 1, (name, len(found))
        return found[0]

    def test_each_single_round_split_is_that_round_alone_judged_by_both_ledgers(self):
        for key, round_ in SINGLE.items():
            with self.subTest(key):
                steps = SPLIT[key]
                others = {"host_target", "interleaved", "friend_target"} - {round_}
                self.assertFalse(any(s["do"] == "owner_downs" or str(s.get("as", "")).startswith(
                    tuple(f"{p}{o}" for o in (*others, "downs") for p in ("", "m_"))) for s in steps), key)
                self.assertEqual(sum(s["do"] == "fight" and s.get("as") == round_ for s in steps), 1)
                ledger = self.named(steps, "ledger")
                self.assertEqual(ledger["window"], [f"m_{round_}0", f"m_{round_}1"])
                self.assertEqual(ledger["phases"], [p for p in self.named(FULL, "ledger")["phases"] if p["name"] == round_])
                targets = self.named(steps, "targets")
                self.assertEqual(targets["rounds"], [r for r in self.named(FULL, "targets")["rounds"] if r["name"] == round_])
                self.assertNotIn("downs", targets)

    # Lead #56 (vuh2912-hp-06): the round's ledgers run right after it, with no revive wait between, and nothing after
    # the verdict can fail on a later party down or game over.
    def test_each_single_round_split_judges_right_after_its_window(self):
        for key, round_ in SINGLE.items():
            with self.subTest(key):
                steps = SPLIT[key]
                at = lambda name: steps.index(self.named(steps, name))  # noqa: E731
                end, verdict = at(f"m_{round_}1"), at("verdict")
                self.assertTrue(end < at("boss_settled") < at("ledger") < at("targets") < verdict, key)
                between = steps[end + 1:verdict]
                self.assertFalse([s for s in between if s["do"] in ("wait_until", "clip_wait", "capture", "revive_guard")
                                  or "revive returned" in json.dumps(s)], key)
                self.assertEqual([s["do"] for s in between],
                                 ["assert"] * (round_ != "interleaved") + ["wait", "save", "hp_ledger", "assert", "assert",
                                                                           "assert", "target_ledger", "assert"])
                tail = steps[verdict + 1:]
                self.assertTrue(tail and all(s["do"] in ("revive_guard", "clip_wait", "capture") for s in tail), key)
                self.assertTrue(all(s.get("on") is False for s in tail if s["do"] == "revive_guard"), key)
                assert_background_films(self, steps)

    def test_b2_is_owner_downs_right_after_the_release_judged_over_its_window(self):
        b2 = SPLIT["b2"]
        takeover = max(i for i, s in enumerate(b2) if s["do"] == "wait_until" and "take-over" in s["expr"])
        self.assertFalse(any(s["do"] in ("fight", "walk_to") for s in b2[takeover:]))  # no round: the Hall fights come before
        self.assertEqual([s.get("as", s["do"]) for s in b2[takeover + 1:takeover + 4]],
                         ["m_downs0", "stats_mark", "owner_downs"])  # lead #76: the stats mark just inside
        ledger, targets = self.named(b2, "ledger"), self.named(b2, "targets")
        self.assertEqual((ledger["window"], ledger["phases"]),
                         (["m_downs0", "m_downs1"], [{"name": "downs", "from": "m_downs0", "to": "m_downs1"}]))
        self.assertEqual((targets["rounds"], targets["downs"]), ([], {"from": "m_downs0", "to": "m_downs1"}))
        order = [b2.index(self.named(b2, n)) for n in ("m_downs1", "boss_settled", "ledger", "targets")]
        self.assertEqual(order, sorted(order))  # the ledger judges the closed downs window and pins the boss first
        self.assertEqual(self.named(b2, "owner_downs"), self.named(FULL, "owner_downs"))

    # vuh2912-hp-07: owner_downs passed in 8.6 s, then its 42 s clip_wait left both seats idle with the guards off and
    # Axel took the party to a game over before m_downs1. B2 now closes and judges its window right after owner_downs,
    # and collects the clips and stills only after the verdict, as the single-round splits do.
    def test_b2_judges_right_after_owner_downs_and_collects_after_the_verdict(self):
        b2 = SPLIT["b2"]
        at = lambda name: b2.index(self.named(b2, name))  # noqa: E731
        downs, verdict = at("owner_downs"), at("verdict")
        self.assertEqual((b2[downs + 1], b2[downs + 2].get("as")), ({"do": "stats_mark"}, "m_downs1"))
        between = b2[downs + 2:verdict]
        self.assertFalse([s for s in between if s["do"] in ("wait_until", "clip_wait", "capture", "revive_guard")])
        self.assertEqual([s["do"] for s in between],
                         ["save", "assert", "save", "wait", "save", "hp_ledger", "assert", "assert", "assert",
                          "target_ledger", "assert"])
        self.assertEqual([s["do"] for s in b2[verdict + 1:]], ["clip_wait", "capture", "capture"])
        self.assertNotIn("require", b2[verdict + 1])  # owner_downs' films are diagnostics
        assert_background_films(self, b2)

    # Lead #72 (vuh2912-hp-08): Axel kept on the reviver and broke every hold. Each forced down waits, at most 20 s and
    # labelled, until Axel stands at least 500 units from both seats: just over the 90th percentile (479) of how far
    # he travelled in the second before each of his 96 hits on a seat in hp-05, hp-07 and hp-08 (max 591).
    def test_owner_downs_waits_for_axel_to_clear_both_seats_before_each_forced_down(self):
        for steps in (SPLIT["b2"], FULL):
            step = self.named(steps, "owner_downs")
            self.assertEqual((step["killWhen"], step["killWhenMs"]), ("enemy_clear(500)", 20000))
            self.assertNotIn("clearOf", step)

    # Lead #76 (vuh2912-hp-09): the round lasted 3.8 s, under the mirror's 300-frame stats interval, so the HP ledger's
    # one-brain check found no sample in its window. Every window now asks both seats for the stats line just inside
    # its marks: after the start mark (and its opening assert) and right before the end mark.
    # Lead #84 (vuh2912-hp-10): the host landed nothing in a 10 s interleaved window. It is 20 s, still ending at the
    # first of both hits, and each seat's background clip covers it.
    def test_the_interleaved_window_is_20_s_and_its_clips_cover_it(self):
        for key, steps in (("full", FULL), ("b1_inter", SPLIT["b1_inter"])):
            with self.subTest(key):
                fight = self.named(steps, "interleaved")
                self.assertEqual((fight["seconds"], fight["reach"]), (20, 120))
                self.assertIn("until", fight)
                self.assertEqual(len(fight["clips"]), 2)
                self.assertTrue(all(c["seconds"] >= fight["seconds"] for c in fight["clips"]))

    def test_every_window_holds_a_stats_mark_just_inside_each_end(self):
        for key, steps in [("full", FULL), *SPLIT.items()]:
            names = [s["as"][:-1] for s in steps if s["do"] == "save" and s.get("as", "").startswith("m_")
                     and s["as"].endswith("0")]
            self.assertTrue(names, key)
            for name in names:
                with self.subTest(key=key, window=name):
                    at = lambda n: next(i for i, s in enumerate(steps) if s.get("as") == n)  # noqa: E731
                    start, end = at(f"{name}0"), at(f"{name}1")
                    opening = start + 1 + (steps[start + 1]["do"] == "assert")
                    self.assertEqual(steps[opening], {"do": "stats_mark"})
                    self.assertEqual(steps[end - 1], {"do": "stats_mark"})
                    self.assertEqual([s for s in steps[start:end] if s["do"] == "stats_mark"], [{"do": "stats_mark"}] * 2)

    def test_every_other_split_step_is_the_full_fixtures_own(self):
        for key, steps in SPLIT.items():
            with self.subTest(key):
                others = [s for s in steps if s.get("as") not in ("ledger", "targets")]
                self.assertTrue(all(s in FULL for s in others), [s for s in others if s not in FULL][:2])
                self.assertEqual([s for s in steps if s["do"] == "boot"], [s for s in FULL if s["do"] == "boot"])
                self.assertEqual(self.named(steps, "verdict"), FULL[-1])  # the coop verdict
                if key not in SINGLE and key != "b2":
                    self.assertEqual(steps[-1], FULL[-1])


if __name__ == "__main__":
    unittest.main()
