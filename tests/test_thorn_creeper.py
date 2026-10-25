"""Offline controls for tools/rig/thorn_pilot/creeper_policy.py and thorn_creeper.py (VUH-2617 lead #282, Creeper-first).

The real creeper_policy rules and the real thorn_creeper.CreeperPilotMixin.creeper_turn / CreeperCooperativeMixin.step
run; only base.enemies (entity rows), the head reader (lock/motion, list handles), the pad (recorded), the pilot's
write/state and the clock are fakes. Run: python -B tests/test_thorn_creeper.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import creeper_policy as rules  # noqa: E402
import thorn_creeper as tc  # noqa: E402

base = tc.base
failures = 0


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


THORN_H, C1_H, C2_H = 0x80001000, 0x80002000, 0x80003000
C1, C2, DUSK = "0x7FF600010000", "0x7FF600020000", "0x7FF600030000"


def foe(address, x, z, hp=32, object_id=317, object_type=4):
    return dict(address=address, objectId=object_id, objectType=object_type, hp=hp, position=dict(x=x, y=0, z=z))


def state(**kw):
    s = dict(world=2, room=34, free=True, react=0, event=0, hp=20, maxHp=20, playerPos=[0.0, 0.0, 0.0],
             forward=[0.0, 1.0], pos=[0.0, 0.0, 0.0], roomScope=[2, 34, 0, 157, 157, 157])
    s.update(kw)
    return s


def act(s, foes, **kw):
    args = dict(own_down=False, peer_down=False, logs_known=True)
    args.update(kw)
    return rules.creeper_action(s, foes, **args)


# ---- pure rules --------------------------------------------------------------------------------------------------
near = [foe(C1, 300, 0)]
for label, s, kw in [
    ("unknown logs yield", state(), dict(logs_known=False)),
    ("own logical down yields", state(), dict(own_down=True)),
    ("a downed peer yields to revive", state(), dict(peer_down=True)),
    ("an offered RC/Reversal yields", state(react=297), {}),
    ("an event yields", state(event=1), {}),
    ("not free yields", state(free=False), {}),
    ("below 40% HP yields to heal/evasion", state(hp=7), {}),
    ("Thorn's dazed opening yields to the head pilot", state(), dict(head_motion=210)),
    ("a cooled Creeper is not chased", state(), dict(cooled={C1})),
]:
    check(act(s, near, **kw) == ('yield', None), label)
check(act(state(), [foe(C1, 900, 0)]) == ('yield', None), "a Creeper beyond the engage radius is left alone")
check(act(state(), [foe(DUSK, 100, 0, object_id=318)]) == ('yield', None), "other mobs are not Creepers")
check(act(state(), [foe(C1, 100, 0, hp=0)]) == ('yield', None), "a dead Creeper is not a target")
check(act(state(), [foe(C1, 300, 0), foe(C2, 150, 0)])[1]['address'] == C2, "the nearest Creeper is chosen")
lock_thorn = dict(mode=2, handle=THORN_H)
check(act(state(), near, lock=lock_thorn, handles={C1: C1_H})[0] == 'unlock', "a lock on Thorn is released first")
check(act(state(), near, lock=dict(mode=2, handle=C1_H), handles={C1: C1_H})[0] == 'approach',
      "a lock already on the target Creeper is kept")
check(act(state(), near, lock=dict(mode=0, handle=0))[0] == 'approach', "unlocked and out of reach: approach")
check(act(state(), [foe(C1, 120, 0)], lock=dict(mode=0, handle=0))[0] == 'attack', "unlocked in reach: attack")
check(act(state(), [foe(C1, 120, 0)], lock=None)[0] == 'attack', "an unread lock does not block the attack")
check(act(state(hp=8), [foe(C1, 120, 0)])[0] == 'attack', "at exactly 40% HP the Creeper is still fought")


# ---- creeper_turn with the real mixin ---------------------------------------------------------------------------
class Clock:
    def __init__(self):
        self.t = 1000.0

    def monotonic(self):
        return self.t

    def sleep(self, s):
        self.t += s


class Log:
    def __init__(self):
        self.downed = self.full_party = False

    def update(self):
        return True


class Coop:
    def __init__(self):
        self.own_log, self.peer_log = Log(), Log()


class Pad:
    def __init__(self, veto=False):
        self.holds, self.taps, self.veto = [], [], veto

    def hold(self, names):
        self.holds.append(frozenset(names))

    def tap(self, action, ms=90):
        self.taps.append(action)
        return False if self.veto and action == 'attack' else None


class Reader:
    def __init__(self, members):
        self.m = members

    def members(self):
        return dict(self.m)


def pilot(cfg=None, foes=(), head=None, members=None, states=None, veto=False):
    clock = Clock()
    tc.time = clock
    p = object.__new__(tc.Pilot)
    p.creeper_cfg = cfg if cfg is not None else dict(engage=800, reach=170, chaseSec=5, engageSec=20, coolSec=6, pollSec=0.3)
    p.creeper_foes_at, p.creeper_foes, p.creeper_engage, p.creeper_cool_all = 0.0, [], None, 0.0
    p.heads = 0
    p.creeper_unlock_at, p.creeper_row = 0.0, (None, 0.0)
    p.creeper_stats = dict(unlock=0, approach=0, attack=0, swings=0, chaseTimeouts=0, seen={})
    p.cooperative, p.kb, p.rows, p.polls = Coop(), Pad(veto), [], 0
    p.game = type('G', (), {'pid': 1})()
    p.head_reader = Reader(members or {int(C1, 16): C1_H, int(C2, 16): C2_H})
    def observe_head(s):
        p.heads += 1
        return head if head is not None else dict(known=True, motion=202, lockMode=0, lockHandle=0)
    p.observe_head = observe_head
    p.write = lambda kind, **f: p.rows.append((kind, f))
    seq = list(states or [])
    p.state = lambda: seq.pop(0) if seq else state()

    def enemies(pid, timeout=60):
        p.polls += 1
        return list(foes)
    base.enemies = enemies
    return p, clock


p, clock = pilot(cfg=None)
p.creeper_cfg = None
check(p.creeper_turn(state()) is False and not p.kb.taps and not p.kb.holds, "without creeperFirst nothing changes")
p, clock = pilot(foes=[foe(C1, 300, 0)])
check(p.creeper_turn(state(world=2, room=33)) is False, "outside the Thorn room nothing changes")
p, clock = pilot(foes=[foe(C1, 300, 0)])
p.cooperative.peer_log.downed = True
check(p.creeper_turn(state()) is False and not p.kb.taps, "a downed peer: revive keeps the inputs")
p, clock = pilot(foes=[foe(C1, 300, 0)], head=dict(known=True, motion=202, lockMode=2, lockHandle=THORN_H))
ok1 = p.creeper_turn(state()) is True and p.kb.taps == ['lockon']
clock.t += 0.3
ok2 = p.creeper_turn(state()) is True and p.kb.taps == ['lockon']
clock.t += 1.0
ok3 = p.creeper_turn(state()) is True and p.kb.taps == ['lockon', 'lockon']
check(ok1 and ok2 and ok3, "a Thorn lock gets one R1 release request per second, read back in between")
p, clock = pilot(foes=[foe(C1, 300, 0)])
check(p.creeper_turn(state()) is True and p.kb.holds and p.kb.holds[-1] and not p.kb.taps,
      "out of reach: steer toward the Creeper, no swing")
p, clock = pilot(foes=[foe(C1, 120, 0)])
check(p.creeper_turn(state()) is True and p.kb.taps == ['attack'] * 3 and p.creeper_stats['swings'] == 3,
      "in reach: one three-swing burst")
p, clock = pilot(foes=[foe(C1, 120, 0)], states=[state(), state(react=297)])
check(p.creeper_turn(state()) is True and p.kb.taps == ['attack'] * 2, "an RC offered mid-burst ends the burst")
p, clock = pilot(foes=[foe(C1, 120, 0)], states=[state(hp=5)])
check(p.creeper_turn(state()) is True and p.kb.taps == ['attack'], "HP below 40% mid-burst ends the burst")
p, clock = pilot(foes=[foe(C1, 120, 0)], veto=True)
check(p.creeper_turn(state()) is True and p.kb.taps == ['attack'] and p.creeper_stats['swings'] == 0,
      "an existing pad veto (reserve/wounded 202) stops the burst and counts no swing")
p, clock = pilot(foes=[foe(C1, 600, 0)])
p.creeper_turn(state())
clock.t += 5.5
timed_out = p.creeper_turn(state()) is False and p.creeper_stats['chaseTimeouts'] == 1
clock.t += 1.0
cooled = p.creeper_turn(state()) is False
clock.t += 6.0
resumed = p.creeper_turn(state()) is True
check(timed_out and cooled and resumed, "a chase past chaseSec yields to Thorn, the Creeper cools for coolSec, then resumes")
# Sage early P2: a lock that never releases (held readback) must not own every tick.
p, clock = pilot(foes=[foe(C1, 300, 0)], head=dict(known=True, motion=202, lockMode=2, lockHandle=THORN_H))
taken, ticks = 0, 0
while clock.t < 1020.0:  # 20 s of 0.1 s ticks with the lock stuck on Thorn
    ticks += 1
    taken += 1 if p.creeper_turn(state()) else 0
    clock.t += 0.1
check(p.creeper_stats["chaseTimeouts"] >= 2 and taken <= ticks // 2 and p.kb.taps.count("lockon") <= 12
      and any(k == 'creeper_chase_timeout' and f['phase'] == 'unlock' for k, f in p.rows),
      "a lock that never releases times out under the acquire timer, cools, and yields ticks back to Thorn")
p, clock = pilot(foes=[foe(C1, 120, 0)])
for _ in range(25):  # ~14 s of attack bursts in reach: progress, under the 20 s engagement cap
    p.creeper_turn(state())
check(p.creeper_stats['chaseTimeouts'] == 0 and clock.t - 1000.0 > 5.0,
      "swings in reach renew the acquire timer (no timeout while attacking)")
# Sage CHANGES #286 (1): swings that never kill are capped per engagement too.
p, clock = pilot(foes=[foe(C1, 120, 0)])
results = [p.creeper_turn(state()) for _ in range(60)]
check(p.creeper_stats['chaseTimeouts'] == 1 and False in results
      and any(k == 'creeper_chase_timeout' and f['reason'] == 'engagement' for k, f in p.rows),
      "an engagement longer than engageSec cools every Creeper and yields, even while swinging")
# Sage CHANGES #286 (1): alternating nearest Creepers with a wrong lock share one engagement bound.
p, clock = pilot(foes=[foe(C1, 400, 0), foe(C2, 0, 401)], head=dict(known=True, motion=202, lockMode=2, lockHandle=THORN_H))
taken = ticks = 0
while clock.t < 1012.0:
    ticks += 1
    p.creeper_foes = [foe(C1, 400 + (ticks % 2) * 5, 0), foe(C2, 0, 401 - (ticks % 2) * 5)]  # nearest flips each tick
    p.creeper_foes_at = clock.t
    taken += 1 if p.creeper_turn(state()) else 0
    clock.t += 0.1
check(p.creeper_stats['chaseTimeouts'] >= 1 and taken <= ticks * 3 // 4
      and any(k == 'creeper_chase_timeout' and f['reason'] == 'acquire' for k, f in p.rows),
      "alternating nearest Creepers cannot renew the acquire bound: the engagement times out and yields")
# Sage #287 (1): a vetoed attack intent (0 swings sent) does not renew the progress bound.
p, clock = pilot(foes=[foe(C1, 120, 0)], veto=True)
taken = ticks = 0
while clock.t < 1012.0:
    ticks += 1
    taken += 1 if p.creeper_turn(state()) else 0
    clock.t += 0.1
check(p.creeper_stats['swings'] == 0 and p.creeper_stats['chaseTimeouts'] >= 1 and taken < ticks
      and any(k == 'creeper_chase_timeout' and f['reason'] == 'acquire' and f['phase'] == 'attack' for k, f in p.rows),
      "a vetoed attack intent sends no swing, does not renew the bound, and times out to Thorn")
# Sage CHANGES #286 (2): known priorities never wait on observer reads.
for label, mutate in [("an offered RC", lambda p: None), ("a downed peer", lambda p: setattr(p.cooperative.peer_log, 'downed', True)),
                      ("own logical down", lambda p: setattr(p.cooperative.own_log, 'downed', True))]:
    p, clock = pilot(foes=[foe(C1, 300, 0)])
    mutate(p)
    s = state(react=297) if label == "an offered RC" else state()
    check(p.creeper_turn(s) is False and p.polls == 0 and p.heads == 0, f"{label} yields before any entity or head read")
p, clock = pilot(foes=[foe(C1, 300, 0)])
p.creeper_cool_all = clock.t + 3
check(p.creeper_turn(state()) is False and p.polls == 0 and p.heads == 0, "a cooling engagement yields before any read")
# Sage CHANGES #286 (3): a later swing in a burst rechecks event and seat state, not only the pad veto.
p, clock = pilot(foes=[foe(C1, 120, 0)], states=[state(event=1)])
check(p.creeper_turn(state()) is True and p.kb.taps == ['attack'], "an event mid-burst stops the burst after one swing")
p, clock = pilot(foes=[foe(C1, 120, 0)])
orig_update = p.cooperative.peer_log.update
calls = {'n': 0}
def flip():
    calls['n'] += 1
    if calls['n'] > 1:
        p.cooperative.peer_log.downed = True
    return True
p.cooperative.peer_log.update = flip
check(p.creeper_turn(state()) is True and p.kb.taps == ['attack'], "a peer going down mid-burst stops the burst after one swing")
p, clock = pilot(foes=[foe(C1, 900, 0)])  # out of engage: yields without sleeping
for _ in range(5):
    p.creeper_turn(state())
    clock.t += 0.05
early = p.polls
clock.t += 0.3
p.creeper_turn(state())
check(early == 1 and p.polls == 2, "entity rows are polled at most once per pollSec")
p, clock = pilot(foes=[foe(C1, 300, 0)])
p.creeper_turn(state())
check(any(k == 'creeper_turn' and f['action'] == 'approach' and f['target'] == C1 for k, f in p.rows)
      and p.creeper_stats['seen'][C1]['firstHp'] == 32, "creeper_turn rows and per-Creeper HP are recorded")


# ---- step precedence --------------------------------------------------------------------------------------------
class Below:
    def __init__(self):
        self.calls = 0

    def step(self, pilot, own, event_reaction=False):
        self.calls += 1
        return 'below'


class Step(tc.CreeperCooperativeMixin, Below):
    pass


class FakePilot:
    def __init__(self, cfg, result):
        self.creeper_cfg, self.result, self.turns = cfg, result, 0

    def creeper_turn(self, own):
        self.turns += 1
        return self.result


s = Step()
check(s.step(FakePilot({}, True), state()) is True and s.calls == 0 and Step.mro()[1] is tc.CreeperCooperativeMixin,
      "a taken Creeper turn consumes the tick before the lower layers")
s = Step()
check(s.step(FakePilot({}, False), state()) == 'below' and s.calls == 1, "a yield falls through to the lower layers")
s, fp = Step(), FakePilot({}, True)
check(s.step(fp, state(), event_reaction=True) == 'below' and fp.turns == 0, "event RCs never reach the Creeper turn")
s, fp = Step(), FakePilot(None, True)
check(s.step(fp, state()) == 'below' and fp.turns == 0, "the friend's route (no creeperFirst) never takes a turn")
class FightBase:
    def fight(self, step, deadline):
        return self.creeper_cfg


class FightProbe(tc.CreeperPilotMixin, FightBase):
    def write(self, kind, **f):
        pass


fp2 = FightProbe()
cfgs = [fp2.fight(step, 0) for step in (dict(eventReactions=[297], creeperFirst={}), dict(eventReactions=[297], creeperFirst=True),
        dict(eventReactions=[297], creeperFirst=dict(engage=600)), dict(eventReactions=[297]), dict(creeperFirst={}))]
check(cfgs == [{}, {}, dict(engage=600), None, None] and fp2.creeper_cfg is None,
      "creeperFirst {} or true enables defaults; absent, or outside the Thorn fight step, stays off; cleared after the fight")
check(tc.CooperativeFight.mro()[1] is tc.CreeperCooperativeMixin and tc.Pilot.mro()[1] is tc.CreeperPilotMixin,
      "the Creeper mixins sit outermost over thorn_react299g")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
