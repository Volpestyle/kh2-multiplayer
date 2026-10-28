"""Boss target ledger (VUH-2912): the host boss chose the seat, that seat took a witnessed native hit after the
choice, and a downed seat left the candidates. Negative controls follow Ione's review probes (24860051, 9204785c).

BOSS_TARGET_LEGACY_LOGS=1 writes the 5de54cb log lines (no tickMs, no witness reads), so the probes can be run
against that ledger (BOSS_TARGET_LEDGER) to show the false passes they reproduce.
"""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
LEGACY = os.environ.get("BOSS_TARGET_LEGACY_LOGS") == "1"


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


targets = load("boss_target_ledger", os.environ.get("BOSS_TARGET_LEDGER", ROOT / "tools/scenario/boss_target_ledger.py"))
H = load("boss_hp_ledger_test_helpers", ROOT / "tests/test_boss_hp_ledger.py")  # the ledger tests' hit-scope builder

BOSS, COPY, HOST_ME, FRIEND_ME = "7FF6274329C0", "7FF627433000", "7FF62741A1D0", "7FF62741B2E0"
LIFETIME = {"epoch": 2, "hostActor": BOSS, "friendActor": COPY}
SENTINEL = 0xFFFFFFFF


def stamp(t):
    return "" if LEGACY or t is None else f" tickMs={t}"


def choice(target, owner, valid, t, changed=1, frame=1):
    return (f"[enemy-target] boss-target frame={frame} enemy={BOSS} objectId=81 target={target} owner={owner} mode=2 "
            f"valid={valid} changed={changed} at=0,0{stamp(t)}")


def witness(seq, victim, t, motion=(0, 101), ignore=(0.0, 30.0), reads=15, same=1, frame=900, atkp=12):
    tick = "" if LEGACY else f" hitTickMs={t}"
    mask = "" if LEGACY else f" reads={reads}"
    return (f"[hitwitness] seq={seq} hitFrame={frame}{tick} frame={frame + 4} attacker={COPY} victim={victim} atkp={atkp} "
            f"hp=22->16 motion={motion[0]}->{motion[1]} ignoreHit={ignore[0]:.1f}->{ignore[1]:.1f}{mask} same={same}")


def down(t, actor="x", frame=1):
    return f"[downed] gate intercept mode=4 logical=1 episode=1 actor={actor} hp=1 delta=-6 frame={frame}{stamp(t)}"


def revive(t):
    return f"[downed] revive returned hp=5/22 flags9B8=0x0 logical=1 fault=0 state=0{stamp(t)}"


def mirror_hit(driven=1, motion=201, set_motion=201, run_start=841, set_frame=890, frame=900, actor=COPY, atkp=12):
    return (f"[enemy-mirror] hit frame={frame} netId=1 actor={actor} driven={driven} runStart={run_start} runFrames=60 "
            f"motion={motion} time=12.0 setMotion={set_motion} setFrame={set_frame} cursor=12 atkp={atkp} "
            f"victim=P_EX110_BTLF@{FRIEND_ME} damage=6 stat=0 updates=60 skips=60")


def native_hit(seq, victim, source, seat):
    return H.hit(seq, victim, 0, 81, 22, 16, 1, 5, 0, 10, source=source, seat=seat, frame=900)


def build(friend_choice=True, choice_after_end=False, away_before_hit=False, friend_witness=None, host_witness=None,
          mirror=None, extra_host=(), friend_down=True, host_down=True, down_choices=None, host_choice_after_hit=False,
          host_reselected_before_down=False, friend_down_at=5500, friend_revive_at=7500, choice_before_round=False):
    """Friend round, host round, then the downs. Each event carries its own tickMs; the statehash ticks (host 1000..,
    friend 1100..) are what the 5de54cb ledger ordered by."""
    host, friend, marks = [H.tick(1000)], [H.tick(1100)], {}
    if choice_before_round:  # chosen during the walk and held: no line inside the round
        host += [choice(1, 1, 3, 990, frame=1)]
    marks["f0"] = [len(host), len(friend)]
    if not choice_before_round:
        host += [choice(0, 0, 3, 1010, frame=1)]
    if friend_choice and not choice_after_end and not choice_before_round:
        host += [choice(1, 1, 3, 1020, frame=2)]
    if away_before_hit:
        host += [choice(0, 0, 3, 1030, frame=3)]
    host += [*extra_host, H.tick(2000)]
    friend += [mirror if mirror is not None else mirror_hit(), *native_hit(7, FRIEND_ME, COPY, "friend"), H.tick(2100),
               friend_witness if friend_witness is not None else witness(7, FRIEND_ME, 1500)]
    host += [H.tick(3000)]
    friend += [H.tick(3100)]
    marks["f1"] = [len(host), len(friend)]
    if choice_after_end:
        host += [choice(1, 1, 3, 3200, frame=4)]
    marks["h0"] = [len(host), len(friend)]
    if host_choice_after_hit:  # Ione 9204785c: the round's only host choice is logged after the hit, same tick bucket
        host += [H.tick(4000), *native_hit(8, HOST_ME, BOSS, "host"), choice(0, 0, 3, 4060, frame=901), H.tick(4100)]
    else:
        host += [choice(0, 0, 3, 3300, frame=5), H.tick(4000), *native_hit(8, HOST_ME, BOSS, "host"), H.tick(4100)]
    host += [host_witness if host_witness is not None else witness(8, HOST_ME, 4050)]
    friend += [H.tick(4200)]
    marks["h1"] = [len(host), len(friend)]
    marks["d0"] = [len(host), len(friend)]
    host += [H.tick(5000)]
    friend += [H.tick(5000)]
    if friend_down:
        friend += [down(friend_down_at), H.tick(6000)]
    host += [H.tick(6000), *(down_choices if down_choices is not None else [choice(0, 0, 1, 6500, frame=6)]), H.tick(7000)]
    if friend_down:
        friend += [H.tick(7000), revive(friend_revive_at), H.tick(8000)]
    if host_down and host_reselected_before_down:
        # Ione 9204785c: exclusion, reselection, then the down, in that host-log order and one tick bucket; nothing
        # during the down shows the host out of the candidates.
        host += [choice(1, 1, 2, 8400, frame=7), choice(0, 0, 3, 8450, frame=8), down(8500, actor="y"), H.tick(9000),
                 H.tick(10000), revive(10500), H.tick(11000)]
    elif host_down:
        host += [down(8500, actor="y"), H.tick(9000), choice(1, 1, 2, 9100, frame=7), H.tick(10000), revive(10500),
                 H.tick(11000)]
    host += [H.tick(12000)]
    friend += [H.tick(12000)]
    marks["d1"] = [len(host), len(friend)]
    return "\n".join(host), "\n".join(friend), {
        "marks": marks, "boss": LIFETIME, "downs": {"from": "d0", "to": "d1"},
        "rounds": [{"name": "friend_target", "from": "f0", "to": "f1", "seat": 1},
                   {"name": "host_target", "from": "h0", "to": "h1", "seat": 0}]}


def run(**changes):
    host, friend, marks = build(**changes)
    return targets.ledger(host, friend, 81, marks)


class BossTargetLedgerTest(unittest.TestCase):
    def fails(self, text, **changes):
        v = run(**changes)
        self.assertFalse(v["pass"], v)
        self.assertTrue(any(text in p for p in v["problems"]), v["problems"])

    def test_chosen_seats_take_witnessed_native_hits_and_leave_when_downed(self):
        v = run()
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual([len(r["qualified"]) for r in v["rounds"]], [1, 1])
        self.assertEqual([(d["seat"], d["excluded"], d["chosenWhileDown"]) for d in v["downs"]], [(0, 1, []), (1, 1, [])])

    # The choice belongs to the round, and precedes the hit.
    def test_witness_lines_with_the_reaction_fields_judge_the_same(self):
        # VUH-2912: the 0x3D6FC0 reaction trace appends fields after same=; the ledger reads the line as before.
        global witness
        plain, original = run(), witness
        # Lead #76, Ione 10d8b494: then 0x3D6FC0's call counts in the witness window (-1 when the victim's identity
        # is not established) and the hit-frame lookback.
        suffixes = (" reactions=1 refused=0 reaction=2 reactRequest=40 reactFrame=901 reactHooked=1 callsAll=12 calls=4 "
                    "callRequests=1 callApplied=1 callIdentity=ok preArm=found preArmKind=2 preArmRequest=40",
                    " reactions=0 refused=0 reaction=-1 reactRequest=4294967295 reactFrame=0 reactHooked=1 callsAll=12 "
                    "calls=-1 callRequests=-1 callApplied=-1 callIdentity=changed preArm=unavailable preArmKind=-1 "
                    "preArmRequest=-1")
        for suffix in suffixes:
            with self.subTest(suffix=suffix):
                witness = lambda *a, s=suffix, **k: original(*a, **k) + s
                try:
                    extended = run()
                finally:
                    witness = original
                self.assertTrue(plain["pass"], plain["problems"])
                self.assertEqual(plain, extended)

    def test_a_round_without_the_choice_fails(self):
        self.fails("the boss never chose seat 1", friend_choice=False)

    def test_a_choice_only_after_the_round_end_does_not_count(self):
        self.fails("the boss never chose seat 1", choice_after_end=True)

    def test_a_hit_after_the_boss_turned_away_does_not_qualify(self):
        self.fails("not after the boss chose this seat", away_before_hit=True)

    def test_a_selection_in_force_from_before_the_round_counts(self):
        # Lines are logged on change only: a choice made during the walk and held through the round is the selection.
        v = run(choice_before_round=True)
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual(v["rounds"][0]["choices"], 1)

    # Ione 9204785c, REQUIRED 1: a tie in order is unproven, never a pass.
    def test_a_host_choice_logged_after_the_hit_does_not_qualify_it(self):
        # Her probe: hit at frame 900, the round's only host choice at 901 after it in the host log, one tick bucket.
        self.fails("not after the boss chose this seat", host_choice_after_hit=True)

    def test_a_pre_down_exclusion_does_not_count_inside_the_down(self):
        # Her probe: exclusion, reselection, then the down, in that order in the host log and one tick bucket.
        self.fails("seat 0 down", host_reselected_before_down=True)
        v = run(host_reselected_before_down=True)
        self.assertTrue(any("seat 0 down" in p and "out of the candidates" in p for p in v["problems"]), v["problems"])

    @unittest.skipIf(LEGACY, "5de54cb lines carry no tickMs")
    def test_a_cross_seat_tie_against_the_hit_is_unproven(self):
        # A host choice of the other seat on the friend's hit tick cannot be ordered against it.
        self.fails("selection at the hit unproven", extra_host=[choice(0, 0, 3, 1500, frame=3)])

    @unittest.skipIf(LEGACY, "5de54cb lines carry no tickMs")
    def test_a_cross_seat_tie_at_the_down_edges_is_not_inside_it(self):
        self.fails("no boss search definitely inside it", down_choices=[choice(0, 0, 1, 5500, frame=6)])  # down's tick
        self.fails("no boss search definitely inside it", down_choices=[choice(0, 0, 1, 7500, frame=6)])  # revive's
        # A choice of the downed seat on the revive's tick may be before it: it counts as chosen.
        self.fails("the boss chose it", down_choices=[choice(0, 0, 1, 6500), choice(1, 1, 3, 7500, frame=8)])

    @unittest.skipIf(LEGACY, "5de54cb lines carry no tickMs")
    def test_a_cross_seat_order_without_ticks_is_unproven(self):
        self.fails("no boss search definitely inside it", friend_down_at=None, friend_revive_at=None)

    # The hit is native (reaction, i-frames) and, on the friend, from the driven host motion.
    def test_a_hit_without_its_witness_fails(self):
        self.fails("no witness", friend_witness="")
        self.fails("no witness", host_witness="")

    def test_a_hit_without_reaction_or_iframes_fails(self):
        self.fails("no reaction/i-frames", friend_witness=witness(7, FRIEND_ME, 1500, motion=(0, 0)))
        self.fails("no reaction/i-frames", host_witness=witness(8, HOST_ME, 4050, ignore=(0.0, 0.0)))
        self.fails("another victim", friend_witness=witness(7, FRIEND_ME, 1500, same=0))

    # Ione 9204785c, REQUIRED 2: a failed motion read is not a reaction.
    def test_an_unread_motion_is_not_a_reaction(self):
        # Her probe: witness motions 0->0xFFFFFFFF and 0xFFFFFFFF->101, everything else unchanged.
        self.fails("witness reads failed", friend_witness=witness(7, FRIEND_ME, 1500, motion=(0, SENTINEL)))
        self.fails("witness reads failed", host_witness=witness(8, HOST_ME, 4050, motion=(SENTINEL, 101)))

    @unittest.skipIf(LEGACY, "5de54cb witness lines carry no reads mask")
    def test_any_failed_witness_read_fails(self):
        for reads in (15 - 1, 15 - 2, 15 - 4, 15 - 8, 0):  # motion before, timer before, motion after, timer after
            self.fails("witness reads failed", host_witness=witness(8, HOST_ME, 4050, reads=reads))

    def test_a_downing_hit_qualifies_by_its_logical_down(self):
        # On a 5-HP seat the hit downs it: no ignore-hit timer, but its own gate intercept on that frame.
        lethal = down(1501, actor=FRIEND_ME, frame=901)
        v = run(friend_witness=witness(7, FRIEND_ME, 1500, ignore=(0.0, 0.0)) + chr(10) + lethal)
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual(v["rounds"][0]["qualified"][0]["kind"], "downing")
        self.assertEqual(run()["rounds"][0]["qualified"][0]["kind"], "reaction")
        late = down(1600, actor=FRIEND_ME, frame=990)  # a later, unrelated down
        self.fails("no reaction/i-frames", friend_witness=witness(7, FRIEND_ME, 1500, ignore=(0.0, 0.0)) + chr(10) + late)
        self.fails("no reaction/i-frames",
                   friend_witness=witness(7, FRIEND_ME, 1500, motion=(5, 5), ignore=(0.0, 0.0)) + chr(10) + lethal)

    def test_a_friend_hit_not_on_the_driven_host_motion_fails(self):
        self.fails("mirror hit not on a driven host motion", mirror=mirror_hit(driven=0))
        self.fails("mirror hit not on a driven host motion", mirror=mirror_hit(set_motion=200))  # a carried motion
        self.fails("mirror hit not on a driven host motion", mirror=mirror_hit(set_frame=841))  # set before the run
        self.fails("no mirror hit line", mirror="")
        self.fails("no mirror hit line", mirror=mirror_hit(actor=BOSS))  # not the bound copy

    # Ione 24860051: invalid selections and the down intervals.
    def test_choosing_an_invalid_candidate_fails(self):
        # Her probe: both receipts choose a candidate whose valid bit is clear.
        self.fails("whose valid bit is clear", extra_host=[choice(0, 0, 2, 1025)])
        self.fails("whose valid bit is clear", extra_host=[choice(1, 1, 1, 1025)])

    def test_each_down_needs_an_exclusion_inside_it(self):
        # An unrelated exclusion before the down does not stand in for one during it.
        v = run(extra_host=[choice(0, 0, 1, 1025, changed=0)], down_choices=[choice(0, 0, 3, 6500)])
        self.assertFalse(v["pass"])
        self.assertTrue(any("seat 1 down" in p and "out of the candidates" in p for p in v["problems"]), v["problems"])

    def test_choosing_a_downed_seat_after_delivery_fails(self):
        self.fails("the boss chose it", down_choices=[choice(0, 0, 1, 6500), choice(1, 1, 3, 6800, frame=8)])

    def test_choosing_a_downed_seat_within_delivery_is_allowed(self):
        v = run(down_choices=[choice(1, 1, 3, 5600, frame=8), choice(0, 0, 1, 6500)])
        self.assertTrue(v["pass"], v["problems"])

    def test_each_seat_must_be_downed(self):
        self.fails("seat 1 was never downed", friend_down=False)
        self.fails("seat 0 was never downed", host_down=False)

    @unittest.skipIf(LEGACY, "5de54cb has no budget rule")
    def test_a_spent_boss_target_budget_leaves_later_selections_unknown(self):
        filler = [choice(1, 1, 3, 1020 + i % 3, changed=0) for i in range(targets.BOSS_LOG_BUDGET)]
        self.fails("budget was spent", extra_host=filler)


if __name__ == "__main__":
    unittest.main()
