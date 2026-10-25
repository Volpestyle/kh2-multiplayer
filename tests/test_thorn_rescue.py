"""Offline controls for tools/rig/thorn_pilot/thorn_rescue.py (VUH-2617 lead #291, no throwback Reversals while rescuing).

The real revive core (thorn.CooperativeFight.step) runs under the real RescueCooperativeMixin, and the real
thorn_rescue.Pad stack writes the pad; only the downed logs, the partner's state, the pilot's write and the mailbox
write are fakes. Run: python -B tests/test_thorn_rescue.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import thorn  # noqa: E402
import thorn_creeper  # noqa: E402
import thorn_rescue as r  # noqa: E402

base = r.base
failures = 0
REACT_KEY = base.KEYS["react"].upper()
MOVE_KEYS = {base.KEYS[k].upper() for k in ("up", "down", "left", "right")}
CFG = dict(commands=frozenset({432, 426}), within=600.0)


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


# ---- pure rule ---------------------------------------------------------------------------------------------------
A, NEAR, FAR = [0.0, 0.0, 0.0], [400.0, 0.0, 0.0], [700.0, 0.0, 0.0]
check(r.suppress(CFG, 432, False, True, False, A, NEAR), "432 within 600 of a downed partner is suppressed")
check(r.suppress(CFG, 426, False, True, False, A, NEAR), "426 within 600 of a downed partner is suppressed")
check(not r.suppress(CFG, 667, False, True, False, A, NEAR), "667 (pulls closer) is still pressed")
check(not r.suppress(CFG, 345, False, True, False, A, NEAR), "345 is still pressed")
check(not r.suppress(CFG, 297, False, True, False, A, NEAR), "event RC 297 is still pressed")
check(not r.suppress(CFG, 432, False, True, False, A, FAR), "432 beyond 600 is still pressed")
check(r.suppress(CFG, 432, False, True, False, A, [600.0, -300.0, 0.0]), "exactly 600 horizontal is suppressed (height ignored)")
check(not r.suppress(CFG, 432, False, True, False, A, [600.1, 0.0, 0.0]), "600.1 is not")
check(not r.suppress(CFG, 432, False, False, False, A, NEAR), "a living partner: no suppression")
check(not r.suppress(CFG, 432, True, True, False, A, NEAR), "own logical down: no suppression")
check(not r.suppress(CFG, 432, False, True, True, A, NEAR), "full party down: no suppression")
check(not r.suppress(None, 432, False, True, False, A, NEAR), "switch off: no suppression")
check(not r.suppress(CFG, 0, False, True, False, A, NEAR), "no RC on screen: nothing to suppress")
check(not r.suppress(CFG, 432, False, True, False, None, NEAR), "unknown own position: no suppression")


# ---- the real revive core under the mixin ------------------------------------------------------------------------
PLAYER, PARTNER = 0x1000, 0x2000
SCOPE = [2, 34, 0, 157, 157, 157]


class Log:
    def __init__(self, downed=False, full=False, actor=0, known=True):
        self.downed, self.full_party, self.actor, self.known = downed, full, actor, known
        self.downs = self.revives = 0
        self.path = Path("unused")

    def update(self):
        return self.known


class Peer:
    pid = 222

    def __init__(self, pos, error=False):
        self.pos, self.error, self.reads = pos, error, 0

    def state(self):
        self.reads += 1
        if self.error:
            raise OSError("peer unreadable")
        return dict(free=True, roomScope=SCOPE, playerPos=list(self.pos), player=PARTNER)


def make_pad():
    pad = object.__new__(r.Pad)
    pad.keys, pad.held, pad.game, pad.frames = base.KEYS, set(), None, []
    pad.write = lambda names: pad.frames.append(set(names))
    return pad


class FakePilot:
    def __init__(self, cfg=CFG):
        self.kb, self.rows, self.rescue_cfg = make_pad(), [], cfg
        self.rescue_stats = dict(episodes=0, ticks=0, byCommand={})
        self.game = None

    def write(self, kind, **f):
        self.rows.append((kind, f))

    def kinds(self):
        return [k for k, _ in self.rows]


class Core(r.RescueCooperativeMixin, thorn.CooperativeFight):
    def __init__(self, peer, own_log, peer_log):
        # thorn.CooperativeFight's own state, without the base's DownedLog file readers.
        self.peer, self.own_log, self.peer_log = peer, own_log, peer_log
        self.since, self.mode = None, ""
        self.hold_started = self.hold_frame = self.hold_identity = None
        self.rc_next = self.rc_until = self.observed_at = 0.0
        self.arm_frame = self.armed_identity = None
        self.waiting_release = False
        self.last_progress = 0
        self.last_hold_hp = None


def own(react=0, pos=(0.0, 0.0, 0.0), **kw):
    s = dict(world=2, room=34, free=True, hp=12, maxHp=20, roomScope=SCOPE, playerPos=list(pos), player=PLAYER,
             react=react, forward=[0.0, 1.0], reactObservation={}, event=0, menu=255)
    s.update(kw)
    return s


def rescue(react, partner, cfg=CFG, own_down=False, full=False, peer_down=True, event_reaction=False, known=True,
           error=False):
    p = FakePilot(cfg)
    c = Core(Peer(partner, error), Log(own_down, full, known=known),
             Log(peer_down, full, actor=PARTNER if peer_down else 0, known=known))
    result = c.step(p, own(react), event_reaction)
    return p, c, result


def pressed(p):
    return any(REACT_KEY in f for f in p.kb.frames)


def moved(p):
    return any(f & MOVE_KEYS for f in p.kb.frames)


p, c, res = rescue(432, (0.0, 0.0, 400.0))
check(res is True and not pressed(p) and moved(p), "432 at 400: the rescue keeps walking and sends no Triangle")
check(p.kinds().count("coop_reversal_suppressed") == 1 and p.rows[0][1]["command"] == 432
      and p.rows[0][1]["horizontal"] == 400.0, "the suppression is logged with command and distance")
check(p.kb.rescue_mask is None, "the pad mask is cleared after the tick")
check(p.kb.rescue_stripped >= 1, "the pad counts the stripped Triangle")
p, c, res = rescue(426, (0.0, 0.0, 250.0))
check(res is True and not pressed(p) and moved(p), "426 at 250: no Triangle, still walking")
p, c, res = rescue(432, (0.0, 0.0, 700.0))
check(res is True and pressed(p) and "coop_reversal_suppressed" not in p.kinds(), "432 at 700: pressed as before")
p, c, res = rescue(667, (0.0, 0.0, 400.0))
check(res is True and pressed(p), "667 at 400: pressed as before")
p, c, res = rescue(345, (0.0, 0.0, 400.0))
check(res is True and pressed(p), "345 at 400: pressed as before")
p, c, res = rescue(432, (0.0, 0.0, 400.0), cfg=None)
check(res is True and pressed(p) and "coop_reversal_suppressed" not in p.kinds(), "switch off: 432 at 400 is pressed")
p, c, res = rescue(432, (0.0, 0.0, 400.0), event_reaction=True)
check("coop_reversal_suppressed" not in p.kinds(), "an event reaction tick is never masked")
p, c, res = rescue(432, (0.0, 0.0, 400.0), peer_down=False)
check("coop_reversal_suppressed" not in p.kinds() and c.peer.reads == 0,
      "a living partner: no mask and no partner read")
p, c, res = rescue(432, (0.0, 0.0, 400.0), own_down=True)
check("coop_reversal_suppressed" not in p.kinds(), "own logical down: no mask")
p, c, res = rescue(432, (0.0, 0.0, 400.0), full=True)
check("coop_reversal_suppressed" not in p.kinds(), "full party down: no mask")
p, c, res = rescue(432, (0.0, 0.0, 400.0), known=False)
check("coop_reversal_suppressed" not in p.kinds(), "unknown logs: no mask")
p, c, res = rescue(432, (0.0, 0.0, 400.0), error=True)
check("coop_reversal_suppressed" not in p.kinds() and p.kb.rescue_mask is None,
      "an unreadable partner: no mask (the core handles the rest)")
p, c, res = rescue(0, (0.0, 0.0, 400.0))
check(c.peer.reads == 1 and "coop_reversal_suppressed" not in p.kinds(),
      "no RC on screen: the mixin reads nothing (only the core reads the partner)")

# The revive hold itself is Triangle: it must never be stripped.
p = FakePilot()
c = Core(Peer((0.0, 0.0, 80.0)), Log(), Log(True, actor=PARTNER))
obs = lambda frame: dict(source="downed-prompt-v7", frame=frame, reviveProgress=0, promptKind=1, promptHide=0)
c.step(p, own(0, reactObservation=obs(100)))
c.step(p, own(0, reactObservation=obs(102)))
check("coop_hold_begin" in p.kinds() and REACT_KEY in p.kb.frames[-1], "the revive hold still sends Triangle")

# A 432 offered during the hold: the core ends the hold, the mask keeps Triangle off, walking continues.
p.kb.frames.clear()
c.step(p, own(432, reactObservation=obs(104)))
check("coop_hold_end" in p.kinds() and not pressed(p), "a 432 during the hold ends it without a Reversal press")
c.step(p, own(0, reactObservation=obs(106)))
c.step(p, own(0, reactObservation=obs(108)))
check(p.kinds().count("coop_hold_begin") == 2 and REACT_KEY in p.kb.frames[-1],
      "after the RC clears, the hold rearms and starts again")


# ---- episodes, tap, and the mask's lifetime ----------------------------------------------------------------------
class Clock:
    t = 50.0

    def monotonic(self):
        return self.t


clock, real_time = Clock(), r.time
r.time = clock
try:
    p = FakePilot()
    c = Core(Peer((0.0, 0.0, 400.0)), Log(), Log(True, actor=PARTNER))
    for dt in (0.0, 0.2, 0.4, 1.2):
        clock.t += dt
        c.step(p, own(432))
    clock.t += 0.1
    c.step(p, own(426))
finally:
    r.time = real_time
check(p.rescue_stats["ticks"] == 5 and p.rescue_stats["episodes"] == 3
      and p.rescue_stats["byCommand"] == {"432": 2, "426": 1},
      "an episode is one command until a 0.5 s gap or a command change")

pad = make_pad()
pad.rescue_mask = (432, 400.0)
check(pad.tap("react") is False and not pad.frames, "a masked tap('react') sends nothing")
pad.hold({"react", "up"})
check(pad.frames[-1] == {base.KEYS["up"].upper()}, "a masked hold keeps the direction and drops Triangle")
pad.rescue_mask = None
pad.hold({"react"})
check(pad.frames[-1] == {REACT_KEY}, "unmasked, Triangle goes through")


class Boom(Exception):
    pass


class Raising(r.RescueCooperativeMixin):
    def step(self, pilot, own, event_reaction=False):
        return super().step(pilot, own, event_reaction)


class Below:
    def step(self, pilot, own, event_reaction=False):
        assert pilot.kb.rescue_mask is not None
        raise Boom()


class RaisingCore(Raising, Below):
    def __init__(self):
        self.peer, self.own_log, self.peer_log = Peer((0.0, 0.0, 400.0)), Log(), Log(True, actor=PARTNER)


p = FakePilot()
try:
    RaisingCore().step(p, own(432))
    raised = False
except Boom:
    raised = True
check(raised and p.kb.rescue_mask is None, "the mask is cleared even when a lower layer raises")


# ---- switch semantics and placement ------------------------------------------------------------------------------
class FightBase:
    def fight(self, step, deadline):
        return self.rescue_cfg


class FightProbe(r.RescuePilotMixin, FightBase):
    def __init__(self):
        self.kb, self.rows = make_pad(), []

    def write(self, kind, **f):
        self.rows.append((kind, f))


fp = FightProbe()
cfgs = [fp.fight(step, 0) for step in (
    dict(eventReactions=[297], rescueNoReversal={}), dict(eventReactions=[297], rescueNoReversal=True),
    dict(eventReactions=[297], rescueNoReversal=dict(commands=[432], within=500)), dict(eventReactions=[297]),
    dict(rescueNoReversal={}))]
check(cfgs == [CFG, CFG, dict(commands=frozenset({432}), within=500.0), None, None] and fp.rescue_cfg is None,
      "rescueNoReversal {} or true enables 432/426 within 600; absent or outside the Thorn fight step stays off")
check([k for k, _ in fp.rows] == ["rescue_no_reversal_summary"] * 3, "one summary row per fight with the switch on")
check(r.CooperativeFight.mro()[1] is r.RescueCooperativeMixin and r.CooperativeFight.mro()[2] is thorn_creeper.CooperativeFight
      and r.Pilot.mro()[1] is r.RescuePilotMixin and r.Pad.mro()[1] is r.RescuePadMixin,
      "the rescue mixins sit outermost over thorn_creeper")
check(thorn.CooperativeFight in r.CooperativeFight.mro() and base.PadInput in r.Pad.mro(),
      "the real revive core and pad writer are underneath")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
