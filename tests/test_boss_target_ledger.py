"""Boss target ledger (VUH-2912): the host boss chose the seat, that seat took a witnessed native hit after the
choice, and a downed seat left the candidates. Negative controls follow Ione's review probes (24860051)."""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


targets = load("boss_target_ledger", os.environ.get("BOSS_TARGET_LEDGER", ROOT / "tools/scenario/boss_target_ledger.py"))
H = load("boss_hp_ledger_test_helpers", ROOT / "tests/test_boss_hp_ledger.py")  # the ledger tests' hit-scope builder

BOSS, COPY, HOST_ME, FRIEND_ME = "7FF6274329C0", "7FF627433000", "7FF62741A1D0", "7FF62741B2E0"
LIFETIME = {"epoch": 2, "hostActor": BOSS, "friendActor": COPY}


def choice(target, owner, valid, changed=1, frame=1):
    return (f"[enemy-target] boss-target frame={frame} enemy={BOSS} objectId=81 target={target} owner={owner} mode=2 "
            f"valid={valid} changed={changed} at=0,0")


def witness(seq, victim, motion=(0, 101), ignore=(0.0, 30.0), same=1, frame=900, atkp=12):
    return (f"[hitwitness] seq={seq} hitFrame={frame} frame={frame + 4} attacker={COPY} victim={victim} atkp={atkp} "
            f"hp=22->16 motion={motion[0]}->{motion[1]} ignoreHit={ignore[0]:.1f}->{ignore[1]:.1f} same={same}")


def mirror_hit(driven=1, motion=201, set_motion=201, run_start=841, set_frame=890, frame=900, actor=COPY, atkp=12):
    return (f"[enemy-mirror] hit frame={frame} netId=1 actor={actor} driven={driven} runStart={run_start} runFrames=60 "
            f"motion={motion} time=12.0 setMotion={set_motion} setFrame={set_frame} cursor=12 atkp={atkp} "
            f"victim=P_EX110_BTLF@{FRIEND_ME} damage=6 stat=0 updates=60 skips=60")


def native_hit(seq, victim, source, seat):
    return H.hit(seq, victim, 0, 81, 22, 16, 1, 5, 0, 10, source=source, seat=seat, frame=900)


def build(friend_choice=True, choice_after_end=False, away_before_hit=False, friend_witness=None, host_witness=None,
          mirror=None, extra_host=(), friend_down=True, host_down=True, down_choices=None):
    """Friend round, host round, then the downs. Ticks: host 1000.. and friend 1100.. advance together."""
    host, friend, marks = [H.tick(1000)], [H.tick(1100)], {}
    marks["f0"] = [len(host), len(friend)]
    host += [choice(0, 0, 3, frame=1)]
    if friend_choice and not choice_after_end:
        host += [choice(1, 1, 3, frame=2)]
    if away_before_hit:
        host += [choice(0, 0, 3, frame=3)]
    host += [*extra_host, H.tick(2000)]
    friend += [mirror if mirror is not None else mirror_hit(), *native_hit(7, FRIEND_ME, COPY, "friend"), H.tick(2100),
               friend_witness if friend_witness is not None else witness(7, FRIEND_ME)]
    host += [H.tick(3000)]
    friend += [H.tick(3100)]
    marks["f1"] = [len(host), len(friend)]
    if choice_after_end:
        host += [choice(1, 1, 3, frame=4)]
    marks["h0"] = [len(host), len(friend)]
    host += [choice(0, 0, 3, frame=5), H.tick(4000), *native_hit(8, HOST_ME, BOSS, "host"), H.tick(4100),
             host_witness if host_witness is not None else witness(8, HOST_ME)]
    friend += [H.tick(4200)]
    marks["h1"] = [len(host), len(friend)]
    marks["d0"] = [len(host), len(friend)]
    host += [H.tick(5000)]
    friend += [H.tick(5000)]
    if friend_down:
        friend += ["[downed] gate intercept mode=4 logical=1 episode=1 actor=x hp=1 delta=-6", H.tick(6000)]
    host += [H.tick(6000), *(down_choices if down_choices is not None else [choice(0, 0, 1, frame=6)]), H.tick(7000)]
    if friend_down:
        friend += [H.tick(7000), "[downed] revive returned hp=5/22 flags9B8=0x0 logical=1 fault=0 state=0", H.tick(8000)]
    if host_down:
        host += ["[downed] gate intercept mode=4 logical=1 episode=2 actor=y hp=1 delta=-6", H.tick(9000),
                 choice(1, 1, 2, frame=7), H.tick(10000), "[downed] revive returned hp=5/22", H.tick(11000)]
    host += [H.tick(12000)]
    friend += [H.tick(12000)]
    marks["d1"] = [len(host), len(friend)]
    return "\n".join(host), "\n".join(friend), {
        "marks": marks, "boss": LIFETIME, "downs": {"from": "d0", "to": "d1"},
        "rounds": [{"name": "friend_target", "from": "f0", "to": "f1", "seat": 1},
                   {"name": "host_target", "from": "h0", "to": "h1", "seat": 0}]}


def run(**changes):
    return targets.ledger(*build(**changes)[:2], 81, build(**changes)[2])


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

    # REQUIRED 3: the choice belongs to the round, and precedes the hit.
    def test_a_round_without_the_choice_fails(self):
        self.fails("round friend_target: the boss never chose seat 1", friend_choice=False)

    def test_a_choice_only_after_the_round_end_does_not_count(self):
        self.fails("round friend_target: the boss never chose seat 1", choice_after_end=True)

    def test_a_hit_after_the_boss_turned_away_does_not_qualify(self):
        self.fails("not after the boss chose this seat", away_before_hit=True)

    # REQUIRED 4: the hit is native (reaction, i-frames) and, on the friend, from the driven host motion.
    def test_a_hit_without_its_witness_fails(self):
        self.fails("no witness", friend_witness="")
        self.fails("no witness", host_witness="")

    def test_a_hit_without_reaction_or_iframes_fails(self):
        self.fails("no reaction/i-frames", friend_witness=witness(7, FRIEND_ME, motion=(0, 0)))
        self.fails("no reaction/i-frames", host_witness=witness(8, HOST_ME, ignore=(0.0, 0.0)))
        self.fails("another victim", friend_witness=witness(7, FRIEND_ME, same=0))

    def test_a_downing_hit_qualifies_by_its_logical_down(self):
        # On a 5-HP seat the hit downs it: no ignore-hit timer, but its own gate intercept on that frame.
        down = f"[downed] gate intercept mode=4 logical=1 episode=1 actor={FRIEND_ME} hp=5 delta=-6 -> engine hp 1, dead bit clear room=2/20/0/137/137/137 frame=901"
        v = run(friend_witness=witness(7, FRIEND_ME, ignore=(0.0, 0.0)) + chr(10) + down)
        self.assertTrue(v["pass"], v["problems"])
        self.assertEqual(v["rounds"][0]["qualified"][0]["kind"], "downing")
        self.assertEqual(run()["rounds"][0]["qualified"][0]["kind"], "reaction")
        late = down.replace("frame=901", "frame=990")  # a later, unrelated down
        self.fails("no reaction/i-frames", friend_witness=witness(7, FRIEND_ME, ignore=(0.0, 0.0)) + chr(10) + late)
        self.fails("no reaction/i-frames", friend_witness=witness(7, FRIEND_ME, motion=(5, 5), ignore=(0.0, 0.0)) + chr(10) + down)

    def test_a_friend_hit_not_on_the_driven_host_motion_fails(self):
        self.fails("mirror hit not on a driven host motion", mirror=mirror_hit(driven=0))
        self.fails("mirror hit not on a driven host motion", mirror=mirror_hit(set_motion=200))  # a carried motion
        self.fails("mirror hit not on a driven host motion", mirror=mirror_hit(set_frame=841))  # set before the run
        self.fails("no mirror hit line", mirror="")
        self.fails("no mirror hit line", mirror=mirror_hit(actor=BOSS))  # not the bound copy

    # REQUIRED 2: invalid selections and the down intervals.
    def test_choosing_an_invalid_candidate_fails(self):
        # Ione's probe: both receipts choose a candidate whose valid bit is clear.
        self.fails("whose valid bit is clear", extra_host=[choice(0, 0, 2)])
        self.fails("whose valid bit is clear", extra_host=[choice(1, 1, 1)])

    def test_each_down_needs_an_exclusion_inside_it(self):
        # An unrelated exclusion before the down (Ione's probe) does not stand in for one during it.
        v = run(extra_host=[choice(0, 0, 1, changed=0)], down_choices=[choice(0, 0, 3)])
        self.assertFalse(v["pass"])
        self.assertTrue(any("seat 1 down" in p and "out of the candidates" in p for p in v["problems"]), v["problems"])

    def test_choosing_a_downed_seat_after_delivery_fails(self):
        self.fails("the boss chose it", down_choices=[choice(0, 0, 1), choice(1, 1, 3, frame=8)])

    def test_each_seat_must_be_downed(self):
        self.fails("seat 1 was never downed", friend_down=False)
        self.fails("seat 0 was never downed", host_down=False)


if __name__ == "__main__":
    unittest.main()
