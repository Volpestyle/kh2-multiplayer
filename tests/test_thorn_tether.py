"""Offline controls for tools/rig/thorn_pilot/thorn_tether.py (VUH-2617 lead #344, partner tether in the volley).

The real revive core (thorn.CooperativeFight.step) runs under the real OrbHoldCore and TetherCore and writes the real
Pad stack; the production-MRO cases run every layer of thorn_tether.CooperativeFight. Only the downed logs, the
partner's state, the head observation, the clock, the pilot's write, the mailbox write and the head/Creeper turns of
the higher layers are fakes. The far-start case replays zr121's third window (20261025-162330): before the host went
down 1923 from the friend, the host (787, 916) was the seat far from Thorn (0, -860) and the friend (271, -937) near it.
Run: python -B tests/test_thorn_tether.py
"""
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import thorn  # noqa: E402
import thorn_approach  # noqa: E402
import thorn_head  # noqa: E402
import thorn_orbhold as o  # noqa: E402
import thorn_tether as tt  # noqa: E402

base = tt.base
failures = 0
REACT_KEY = base.KEYS["react"].upper()
MOVE_KEYS = {base.KEYS[k].upper() for k in ("up", "down", "left", "right")}
CFG = dict(radius=1300.0)
ORB = dict(far=1250.0, hold=1000.0, finish=300.0, maxWaitSec=30.0)
ROOT = (0.0, 0.0, -860.0)
HOST_AT, FRIEND_AT = (787.0, 0.0, 916.0), (271.0, 0.0, -937.0)   # zr121 W3: 1923 apart; host 1943 / friend 281 from Thorn


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


def head(motion=202, root=ROOT, known=True):
    return dict(known=known, motion=motion, rootWorldPos=root) if known else dict(known=False, reason="x")


# ---- pure rule ---------------------------------------------------------------------------------------------------
t = tt.tether(CFG, head(), HOST_AT, FRIEND_AT, 100, 200)
check(t is not None and t["gap"] == 1923.5 and t["ownToThorn"] > t["peerToThorn"],
      "zr121 W3 positions in the volley: the host (far from Thorn) is tethered toward the friend")
check(tt.tether(CFG, head(), FRIEND_AT, HOST_AT, 200, 100) is None, "the friend, nearer Thorn, is not the one that moves")
check(tt.tether(CFG, head(motion=207), HOST_AT, FRIEND_AT, 100, 200) is None, "Thorn not in its volley: no tether")
check(tt.tether(CFG, head(known=False), HOST_AT, FRIEND_AT, 100, 200) is None, "unknown head: no tether")
check(tt.tether(CFG, None, HOST_AT, FRIEND_AT, 100, 200) is None, "no head read: no tether")
check(tt.tether(CFG, dict(known=False, motion=202, rootWorldPos=ROOT), HOST_AT, FRIEND_AT, 100, 200) is None,
      "a head marked unknown never tethers, even with motion 202 in it")
check(tt.tether(None, head(), HOST_AT, FRIEND_AT, 100, 200) is None, "switch off: no tether")
check(tt.tether(CFG, head(), (0.0, 0.0, 1300.0), (0.0, 0.0, 0.0), 1, 2) is None, "exactly 1300 apart: inside the radius")
check(tt.tether(CFG, head(), (0.0, 0.0, 1300.1), (0.0, 0.0, 0.0), 1, 2) is not None, "1300.1 apart: tethered")
check(tt.tether(CFG, head(), (0.0, -500.0, 1300.0), (0.0, 0.0, 0.0), 1, 2) is None, "height is ignored")
check(tt.tether(CFG, head(), (700.0, 0.0, -860.0), (-700.0, 0.0, -860.0), 1, 2) is not None
      and tt.tether(CFG, head(), (-700.0, 0.0, -860.0), (700.0, 0.0, -860.0), 2, 1) is None,
      "equal distances from Thorn: only the lower pid moves")
check(tt.tether(CFG, head(root=(float("nan"), 0, 0)), HOST_AT, FRIEND_AT, 1, 2) is None, "non-finite root: no tether")


# ---- the real core under the hook --------------------------------------------------------------------------------
PLAYER, PARTNER = 0x1000, 0x2000
SCOPE = [2, 34, 0, 157, 157, 157]


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


clock = Clock()
for module in (o, tt, thorn):
    module.time.monotonic = clock


class Log:
    def __init__(self, downed=False, full=False, actor=0):
        self.downed, self.full_party, self.actor = downed, full, actor
        self.downs = 1 if downed else 0
        self.revives = 0
        self.path = Path("unused")
        self.known, self.pending = True, False

    def update(self):
        if self.pending:  # an interception appended since the last poll (DownedLog reads it on update)
            self.pending = False
            self.downed, self.actor, self.downs = True, PARTNER, self.downs + 1
        return self.known


class Peer:
    pid = 200

    def __init__(self, pos=FRIEND_AT, error=False, **over):
        self.pos, self.error, self.over = pos, error, over
        self.during = None  # called inside state(): something that happens while the partner is being read

    def state(self):
        if self.during is not None:
            self.during()
        if self.error:
            raise OSError("peer unreadable")
        return dict(dict(free=True, roomScope=SCOPE, playerPos=list(self.pos), player=PARTNER, hp=20), **self.over)


class Game:
    pid = 100


def make_pad():
    pad = object.__new__(tt.child.child.Pad)
    pad.keys, pad.held, pad.game, pad.frames = base.KEYS, set(), None, []
    pad.write = lambda names: pad.frames.append(set(names))
    return pad


class FakePilot:
    def __init__(self, cfg=CFG, observed=None, orb=ORB):
        self.kb, self.rows, self.tether_cfg, self.orbhold_cfg = make_pad(), [], cfg, orb
        self.tether_stats = dict(episodes=0, seconds=0.0, ends={})
        self.orbhold_stats = dict(episodes=0, seconds=0.0, ends={})
        self.observed = head() if observed is None else observed
        self.head_reader = object()
        self.game = Game()

    def observe_head(self, state):
        return self.observed

    def write(self, kind, **f):
        self.rows.append((kind, dict(f, _t=clock.now)))

    def kinds(self):
        return [k for k, _ in self.rows]

    def row(self, kind):
        return next(f for k, f in self.rows if k == kind)


def init_core(c, peer, own_log, peer_log):
    c.peer, c.own_log, c.peer_log = peer, own_log, peer_log
    c.since, c.mode = None, ""
    c.hold_started = c.hold_frame = c.hold_identity = None
    c.rc_next = c.rc_until = c.observed_at = 0.0
    c.arm_frame = c.armed_identity = None
    c.waiting_release = False
    c.last_progress = 0
    c.last_hold_hp = None


class Core(tt.TetherCore):
    def __init__(self, peer, own_log, peer_log):
        init_core(self, peer, own_log, peer_log)


def own(pos=HOST_AT, react=0, **kw):
    s = dict(world=2, room=34, free=True, inField=True, hp=14, maxHp=20, roomScope=SCOPE, playerPos=list(pos),
             player=PLAYER, react=react, forward=[0.0, -1.0], reactObservation={}, event=0, menu=255)
    s.update(kw)
    return s


def setup(cfg=CFG, observed=None, peer_pos=FRIEND_AT, own_down=False, peer_down=False, full=False, error=False, **over):
    p = FakePilot(cfg, observed)
    c = Core(Peer(peer_pos, error, **over), Log(own_down, full), Log(peer_down, full, actor=PARTNER if peer_down else 0))
    return p, c


def toward(state, target):
    return {base.KEYS[k].upper() for k in base.steer({**state, "pos": state["playerPos"]}, (target[0], target[2]))}


# Far-start replay (zr121 W3): the host is tethered toward the friend, then released once within 1300.
p, c = setup()
res = c.step(p, own())
check(res is True and p.kb.frames and p.kb.frames[-1] & MOVE_KEYS == toward(own(), FRIEND_AT)
      and not any(REACT_KEY in f for f in p.kb.frames),
      "zr121 W3 replay: the host steers toward the friend (no Triangle), the tick is consumed")
row = p.row("coop_tether")
check(row["gap"] == 1923.5 and row["episode"] == 1 and p.tether_stats["episodes"] == 1,
      "the tether episode is logged once with the gap")
check(base.steer is tt.STEER and tt.STEER is thorn_head.base.steer, "the steer is pilot.py's own and is restored")
path = [(700.0, 0.0, 650.0), (600.0, 0.0, 380.0), (520.0, 0.0, 300.0)]   # closing in: 1644, 1357, 1262 from the friend
results = []
for pos in path:
    clock.now += 0.5
    results.append(c.step(p, own(pos=pos)))
check(results == [True, True, False] and p.kinds().count("coop_tether") == 1,
      "the same episode keeps pulling until within 1300, then the tick is released (False) to the layers above")
end = p.row("coop_tether_end")
check(end["reason"] == "released" and abs(end["seconds"] - 1.5) < 1e-9 and p.tether_stats["ends"] == {"released": 1},
      "the episode ends 'released' with its time")
p, c = setup()
c.step(p, own(pos=FRIEND_AT), )
check("coop_tether" not in p.kinds(), "the near seat (the friend) of the same pair is not moved")


# Controls: identical to the switch off.
def same(label, setup_kw=None, own_kw=None, event_reaction=False, creeper=False, pilot_kw=None):
    runs = []
    for cfg in (CFG, None):
        clock.now = 2000.0
        p, c = setup(cfg=cfg, **(setup_kw or {}))
        for k, v in (pilot_kw or {}).items():
            setattr(p, k, v)
        if creeper:
            p.creeper_engage = dict(start=clock.now, swing=clock.now)
        res = c.step(p, own(**(own_kw or {})), event_reaction)
        runs.append((res, [sorted(f) for f in p.kb.frames], [k for k in p.kinds()]))
    check(runs[0] == runs[1] and "coop_tether" not in runs[0][2], f"{label}: identical to the tether off")


same("revive window (partner downed): the revive core acts, the tether doesn't", setup_kw=dict(peer_down=True))
same("own seat logically down", setup_kw=dict(own_down=True))
same("full party down", setup_kw=dict(full=True))
same("active Creeper engagement (host on a Creeper objective)", creeper=True)
same("RC offered", own_kw=dict(react=432))
same("event-reaction tick", event_reaction=True)
same("Thorn not in its volley", setup_kw=dict(observed=head(motion=207)))
same("unknown head", setup_kw=dict(observed=head(known=False)))
same("outside the Thorn room", own_kw=dict(world=2, room=33))
same("own seat not free (menu/event)", own_kw=dict(free=False))
same("partner unreadable", setup_kw=dict(error=True))
same("partner in another scope", setup_kw=dict(roomScope=[2, 34, 0, 1, 1, 1]))
same("partner HP 0 (not yet logged down)", setup_kw=dict(hp=0))
same("within 1300 already", own_kw=dict(pos=(520.0, 0.0, 300.0)))
p, c = setup()
c.since = clock.now
check(c.tether_check(p, own(), False) == (None, "yield"), "a revive window still open (core's since set): no tether")
for label, kw in (("own seat logically down", dict(own_down=True)), ("full party down", dict(full=True)),
                  ("partner logically down", dict(peer_down=True))):
    p, c = setup(**kw)
    check(c.tether_check(p, own(), False) == (None, "yield"), f"{label}: the tether's own check refuses too")
p, c = setup()
c.step(p, own())
p.creeper_engage = dict(start=clock.now, swing=clock.now)
clock.now += 0.5
c.step(p, own())
check(p.row("coop_tether_end")["reason"] == "creeper" and not p.kb.held,
      "a Creeper engagement starting mid-tether ends the episode ('creeper') and releases the keys")
p, c = setup()
c.step(p, own())
c.peer_log.downed, c.peer_log.actor = True, PARTNER
clock.now += 0.3
c.step(p, own())
check([f["reason"] for k, f in p.rows if k == "coop_tether_end"] == ["core"] and "coop_revive" in p.kinds(),
      "the partner going down mid-tether: the revive core takes the tick and the episode ends ('core')")


# ---- r2 (Sage, on r1): ending the tether never clears a lower core's input from the same tick -----------------
FAR_OWN, FAR_PARTNER = (-1300.0, 0.0, 1200.0), (0.0, 0.0, 700.0)   # 1393 apart; own 2436 / partner 1560 from Thorn


def lower_core_tick(cfg, react=0, motion=207):
    """A tether episode open (cfg on), then the partner goes down: this tick's input belongs to the revive core."""
    clock.now = 4000.0
    p, c = setup(cfg=cfg, peer_pos=FAR_PARTNER)
    c.step(p, own(pos=FAR_OWN))                                    # Thorn in 202: the tether pulls (if on)
    opened = "coop_tether" in p.kinds()
    c.peer_log.downed, c.peer_log.actor, c.peer_log.downs = True, PARTNER, 1
    p.observed = head(motion=motion)
    clock.now += 0.3                                               # past the head TTL
    res = c.step(p, own(pos=FAR_OWN, react=react))
    return p, c, res, opened


for label, kw, want in (("the approach (W toward the partner)", dict(), lambda s: toward(s, FAR_PARTNER)),
                        ("the reaction press (direction + Triangle)", dict(react=345),
                         lambda s: toward(s, FAR_PARTNER) | {REACT_KEY}),
                        ("the orb hold-off (W toward Thorn)", dict(motion=202), lambda s: toward(s, ROOT))):
    p, c, res, opened = lower_core_tick(CFG, **kw)
    q, _, _, _ = lower_core_tick(None, **kw)
    expect = want(own(pos=FAR_OWN))
    check(opened and res is True and set(p.kb.held) == expect and set(q.kb.held) == expect
          and [f["reason"] for k, f in p.rows if k == "coop_tether_end"] == ["core"],
          f"r2: a tether ending on the core's tick keeps {label}, as with the tether off")
p, c = setup()
c.step(p, own())
p.kb.hold({base.KEYS["attack"]})                                   # a higher layer pressed something since
clock.now += 0.3
p.observed = head(motion=207)
c.step(p, own())
check(p.kb.held == {base.KEYS["attack"].upper()}
      and [f["reason"] for k, f in p.rows if k == "coop_tether_end"] == ["released"],
      "r2: keys another layer wrote since the tether's last press are not released when the episode ends")


# ---- r2 (Sage, on r1): the logs are validated fresh right before tether input, failing closed ------------------
p, c = setup()
c.peer.during = lambda: setattr(c.peer_log, "pending", True)       # an interception logged during the partner read
res = c.step(p, own())
check(res is False and not any(f & MOVE_KEYS for f in p.kb.frames) and "coop_tether" not in p.kinds(),
      "r2: a downing logged while the partner is read stops the tether before any input")
p, c = setup()
c.peer.during = lambda: setattr(c.own_log, "known", False)         # the own log turns unreadable
res = c.step(p, own())
check(res is False and not any(f & MOVE_KEYS for f in p.kb.frames) and "coop_tether" not in p.kinds(),
      "r2: an own log turning unreadable during the reads stops the tether (fail closed)")
p, c = setup()
c.peer.during = lambda: setattr(c.peer_log, "known", False)
res = c.step(p, own())
check(res is False and not any(f & MOVE_KEYS for f in p.kb.frames), "r2: a partner log turning unreadable stops it too")
p, c = setup()
c.step(p, own())
c.peer.during = lambda: setattr(c.peer_log, "pending", True)
clock.now += 0.5
frames = len(p.kb.frames)
res = c.step(p, own())
check(res is False and [f["reason"] for k, f in p.rows if k == "coop_tether_end"] == ["yield"] and not p.kb.held
      and not any(f & MOVE_KEYS for f in p.kb.frames[frames:]),
      "r2: mid-episode, the same late downing ends it ('yield') and releases only the tether's own press")
p, c = setup()
c.peer.during = lambda: None
check(c.step(p, own()) is True, "r2 control: with nothing happening during the reads, the tether pulls")


# ---- full production MRO: layers above take the tick first; the plateau monitor is unaffected ------------------
class ApproachCore(thorn_approach.ApproachRevisionMixin, Core):
    pass


class RcGateStub:
    def drain(self):
        return []


class CallbacksStub:
    def poll(self):
        return True, 0


thorn_approach.time.sleep = lambda seconds: None


def production(cfg=CFG, peer_down=False, creeper_turn=False, observed=None, pos=HOST_AT):
    clock.now = 3000.0
    p, _ = setup(cfg=cfg, observed=observed)
    seed = ApproachCore(Peer(), Log(), Log(peer_down, actor=PARTNER if peer_down else 0))
    c = object.__new__(tt.CooperativeFight)
    c.__dict__.update(seed.__dict__)
    c.rc_gate, c.callback_reader = RcGateStub(), CallbacksStub()
    s = own(pos=pos)
    p.state = lambda: s
    p.head_active, p.next_options = True, {"head": True, "healBeforeRevive": False}
    p.heads = []
    p.head_turn = lambda state: p.heads.append(state["playerPos"]) or True
    p.reserve_turn = lambda state: False
    p.survival254_active = False
    p.creeper_cfg = {} if creeper_turn else None
    p.creeper_turn = lambda state: True
    p.protect_cfg = None
    c.plateau_log_at = clock.now
    res = c.step(p, s)
    return p, c, res


p, c, res = production()
check(res is True and "coop_tether" in p.kinds() and p.heads == [],
      "production MRO: the far seat tethers, pre-empting the head-targeting turn")
check(c.anchor is None and c.detour is None and c.escape_tries == 0
      and not any(k in ("coop_detour", "coop_stuck_escape", "coop_vertical_escape", "coop_plateau") for k in p.kinds()),
      "production MRO: the approach layer's plateau monitor is untouched (no anchor, detour or escape)")
p, c, res = production(cfg=None)
check(p.heads == [list(HOST_AT)] and "coop_tether" not in p.kinds(), "control: tether off, the head turn runs as before")
p, c, res = production(observed=head(motion=207))
check(p.heads == [list(HOST_AT)] and "coop_tether" not in p.kinds(),
      "control: out of the volley, the head turn runs as before")
p, c, res = production(creeper_turn=True)
check(res is True and "coop_tether" not in p.kinds() and p.heads == [],
      "production MRO: a Creeper turn above takes the tick; the tether never runs")


def stalled(cfg):
    """A revive window with a stalled approach (Thorn not in its volley): the plateau monitor's own escape."""
    clock.now = 100.0
    p, _ = setup(cfg=cfg, observed=head(motion=207), peer_down=True)
    s = own(pos=(0.0, 0.0, 100.0))
    p.state = lambda: s
    seed = ApproachCore(Peer((0.0, 0.0, 700.0)), Log(), Log(True, actor=PARTNER))
    c = object.__new__(tt.CooperativeFight)
    c.__dict__.update(seed.__dict__)
    c.rc_gate, c.callback_reader = RcGateStub(), CallbacksStub()
    p.protect_cfg = None
    c.step(p, s)
    p.rows.clear()
    clock.now += 3.0
    c.plateau_log_at = clock.now
    c.step(p, s)
    return p, c


(on, _), (off, _) = stalled(CFG), stalled(None)
check(any(k == "coop_detour" and f.get("stage") == "pulse" for k, f in on.rows) and on.kinds() == off.kinds()
      and "coop_tether" not in on.kinds(),
      "production MRO, revive window stalled: the plateau monitor's detour is the same with the tether on")

mro = tt.CooperativeFight.__mro__
i = mro.index(tt.TetherCore)
check(mro[i - 1] is thorn_head.HeadCooperativeMixin and mro[i + 1] is o.OrbHoldCore
      and mro[i + 2] is tt.child.child.ProtectCore
      and mro[i + 3] is thorn.CooperativeFight,
      "TetherCore sits below the head turn and above the hold-off, protect and revive cores")
check(tt.Pilot.__mro__[1] is tt.TetherPilotMixin and tt.Pilot.__mro__[2] is tt.child.Pilot,
      "Pilot: tether over thorn_potion")


# ---- switch semantics and summary ---------------------------------------------------------------------------------
class Lower:
    def fight(self, step, deadline):
        self.seen = self.tether_cfg
        return "fought"


class P(tt.TetherPilotMixin, Lower):
    def __init__(self):
        self.rows = []

    def write(self, kind, **f):
        self.rows.append((kind, f))


pp = P()
check(pp.fight({"eventReactions": True, "partnerTether": {}}, 0) == "fought" and pp.seen == CFG, "{} gives radius 1300")
check(bool(pp.rows) and pp.rows[-1][0] == "partner_tether_summary" and pp.tether_cfg is None,
      "one summary row; off after the fight")
pp = P()
pp.fight({"eventReactions": True, "partnerTether": {"radius": 1100}}, 0)
check(pp.seen == dict(radius=1100.0), "an explicit radius overrides the default")
pp = P()
pp.fight({"eventReactions": True}, 0)
check(pp.seen is None and not pp.rows, "absent key: off, no summary")
pp = P()
pp.fight({"partnerTether": {}}, 0)
check(pp.seen is None, "the key outside the Thorn fight step is ignored")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
