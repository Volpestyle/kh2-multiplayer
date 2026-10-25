"""Offline controls for tools/rig/thorn_pilot/thorn_orbhold.py (VUH-2617 lead #302/#305, wait out Thorn's orb volley).

The real revive core (thorn.CooperativeFight.step) runs under the real OrbHoldCore hook and writes the real Pad stack;
the pre-revive Potion case adds the real thorn_next.PreReviveCooperativeMixin above it. Only the downed logs, the
partner's state, the head observation, the item read, the clock, the pilot's write and the mailbox write are fakes.
Run: python -B tests/test_thorn_orbhold.py
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import orbhold_report  # noqa: E402
import rescue_report  # noqa: E402
import thorn  # noqa: E402
import thorn_approach  # noqa: E402
import thorn_creeper  # noqa: E402
import thorn_next  # noqa: E402
import thorn_orbhold as o  # noqa: E402

base = o.base
ORIGINAL_STEER = base.steer
failures = 0
REACT_KEY = base.KEYS["react"].upper()
MOVE_KEYS = {base.KEYS[k].upper() for k in ("up", "down", "left", "right")}
CFG = dict(far=1250.0, hold=1000.0, finish=300.0, maxWaitSec=30.0)
ROOT = (0.0, 0.0, -860.0)


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


def head(motion=202, root=ROOT, known=True):
    return dict(known=known, motion=motion, rootWorldPos=root) if known else dict(known=False, reason="x")


# ---- pure rule ---------------------------------------------------------------------------------------------------
FAR_PARTNER = [0.0, 0.0, 700.0]          # 1560 from the root
OWN_OUT, OWN_IN = [0.0, 0.0, 300.0], [0.0, 0.0, 100.0]   # 1160 / 960 from the root, 400 / 600 from the partner
h = o.orb_hold(CFG, head(), OWN_OUT, FAR_PARTNER, 0.0)
check(h is not None and h["partnerToThorn"] == 1560.0 and h["ownToThorn"] == 1160.0 and not h["inside"],
      "volley, partner 1560 from Thorn, rescuer 1160 from Thorn: hold off and move in")
check(o.orb_hold(CFG, head(), OWN_IN, FAR_PARTNER, 0.0)["inside"], "rescuer 960 from Thorn: inside the hold radius")
check(o.orb_hold(CFG, head(motion=207), OWN_OUT, FAR_PARTNER, 0.0) is None, "Thorn not in 202: no hold-off")
check(o.orb_hold(CFG, head(known=False), OWN_OUT, FAR_PARTNER, 0.0) is None, "unknown head: no hold-off")
check(o.orb_hold(CFG, None, OWN_OUT, FAR_PARTNER, 0.0) is None, "no head observation: no hold-off")
check(o.orb_hold(CFG, dict(known=False, motion=202, rootWorldPos=ROOT), OWN_OUT, FAR_PARTNER, 0.0) is None,
      "a head marked unknown never holds off, even with motion 202 in it")
check(o.orb_hold(None, head(), OWN_OUT, FAR_PARTNER, 0.0) is None, "switch off: no hold-off")
check(o.orb_hold(CFG, head(), [0.0, 0.0, -100.0], [0.0, 0.0, 390.0], 0.0) is not None,
      "partner exactly 1250 from Thorn: hold-off (far is inclusive)")
check(o.orb_hold(CFG, head(), [0.0, 0.0, -100.0], [0.0, 0.0, 389.9], 0.0) is None, "partner 1249.9 from Thorn: approach")
check(o.orb_hold(CFG, head(), [0.0, 0.0, 400.0], FAR_PARTNER, 0.0) is None, "rescuer exactly 300 from the partner: finish")
check(o.orb_hold(CFG, head(), [0.0, 0.0, 399.9], FAR_PARTNER, 0.0) is not None, "rescuer 300.1 from the partner: hold-off")
check(o.orb_hold(CFG, head(), OWN_OUT, FAR_PARTNER, 30.0) is None, "30 s already waited in this window: approach")
check(o.orb_hold(CFG, head(), OWN_OUT, FAR_PARTNER, 29.9) is not None, "29.9 s waited: still holding off")
check(o.orb_hold(CFG, head(), OWN_OUT, [0.0, -500.0, 700.0], 0.0)["partnerToThorn"] == 1560.0, "height is ignored")
check(o.orb_hold(CFG, head(root=(float("nan"), 0.0, 0.0)), OWN_OUT, FAR_PARTNER, 0.0) is None, "non-finite root: no hold-off")
check(o.orb_hold(CFG, head(root=None), OWN_OUT, FAR_PARTNER, 0.0) is None, "missing root: no hold-off")
check(o.orb_hold(CFG, head(), None, FAR_PARTNER, 0.0) is None, "unknown own position: no hold-off")


# ---- the real revive core under the hook -------------------------------------------------------------------------
PLAYER, PARTNER = 0x1000, 0x2000
SCOPE = [2, 34, 0, 157, 157, 157]


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


clock = Clock()
for module in (o, thorn, thorn_next):
    module.time.monotonic = clock


class Log:
    def __init__(self, downed=False, full=False, actor=0, known=True):
        self.downed, self.full_party, self.actor, self.known = downed, full, actor, known
        self.downs = 1 if downed else 0
        self.revives = 0
        self.path = Path("unused")

    def update(self):
        return self.known


class Peer:
    pid = 222

    def __init__(self, pos=FAR_PARTNER, error=False, **over):
        self.pos, self.error, self.reads, self.over = pos, error, 0, over

    def state(self):
        self.reads += 1
        if self.error:
            raise OSError("peer unreadable")
        return dict(dict(free=True, roomScope=SCOPE, playerPos=list(self.pos) if self.pos else None, player=PARTNER),
                    **self.over)


def make_pad():
    pad = object.__new__(o.previous.Pad)
    pad.keys, pad.held, pad.game, pad.frames = base.KEYS, set(), None, []
    pad.write = lambda names: pad.frames.append(set(names))
    return pad


class FakePilot:
    def __init__(self, cfg=CFG, observed=None, reader=True):
        self.kb, self.rows, self.orbhold_cfg = make_pad(), [], cfg
        self.orbhold_stats = dict(episodes=0, seconds=0.0, ends={})
        self.observed = head() if observed is None else observed
        self.head_reader = object() if reader else None
        self.head_reads = 0
        self.game = None

    def observe_head(self, state):
        self.head_reads += 1
        return self.observed

    def write(self, kind, **f):
        self.rows.append((kind, dict(f, _t=clock.now)))

    def jsonl(self):
        return "".join(json.dumps(dict(t=f["_t"], kind=k, **{x: v for x, v in f.items() if x != "_t"})) + "\n"
                       for k, f in self.rows)

    def kinds(self):
        return [k for k, _ in self.rows]

    def row(self, kind):
        return next(f for k, f in self.rows if k == kind)


def init_core(c, peer, own_log, peer_log):
    # thorn.CooperativeFight's own state, without the base's DownedLog file readers.
    c.peer, c.own_log, c.peer_log = peer, own_log, peer_log
    c.since, c.mode = None, ""
    c.hold_started = c.hold_frame = c.hold_identity = None
    c.rc_next = c.rc_until = c.observed_at = 0.0
    c.arm_frame = c.armed_identity = None
    c.waiting_release = False
    c.last_progress = 0
    c.last_hold_hp = None


class Core(o.OrbHoldCore):
    def __init__(self, peer, own_log, peer_log):
        init_core(self, peer, own_log, peer_log)


def own(react=0, pos=OWN_OUT, **kw):
    s = dict(world=2, room=34, free=True, inField=True, hp=12, maxHp=20, roomScope=SCOPE, playerPos=list(pos) if pos else None,
             player=PLAYER, react=react, forward=[0.0, -1.0], reactObservation={}, event=0, menu=255)
    s.update(kw)
    return s


def setup(cfg=CFG, observed=None, partner=FAR_PARTNER, own_down=False, full=False, peer_down=True, known=True,
          error=False, reader=True, actor=PARTNER, **peer_over):
    p = FakePilot(cfg, observed, reader)
    c = Core(Peer(partner, error, **peer_over), Log(own_down, full, known=known),
             Log(peer_down, full, actor=actor if peer_down else 0, known=known))
    return p, c


def pressed(p):
    return any(REACT_KEY in f for f in p.kb.frames)


def last_move(p):
    return p.kb.frames[-1] & MOVE_KEYS if p.kb.frames else set()


def toward(state, target):
    return {base.KEYS[k].upper() for k in base.steer({**state, "pos": state["playerPos"]}, (target[0], target[2]))}


# Held off: the core's own approach steers toward Thorn, not the partner.
p, c = setup()
s = own()
res = c.step(p, s)
check(res is True and last_move(p) == toward(s, ROOT) and last_move(p) != toward(s, FAR_PARTNER) and not pressed(p),
      "volley + far partner: the core's approach heads toward Thorn, not the partner, with no Triangle")
check(p.kinds()[:2] == ["coop_revive", "coop_approach"] and p.rows[0][1]["state"] == "peer-logical-downed",
      "the core still writes its window marker and approach row itself")
row = p.row("coop_orb_hold")
check(row["partnerToThorn"] == 1560.0 and row["episode"] == 1 and p.orbhold_stats["episodes"] == 1,
      "the hold-off episode is logged once with the partner's distance to Thorn")
check(base.steer is ORIGINAL_STEER, "the core's steer is restored after the tick")
clock.now += 0.1
c.step(p, own())
check(p.kinds().count("coop_orb_hold") == 1, "a second held-off tick does not open another episode")
check(p.head_reads == 1, "the head observation is reused within its 0.25 s TTL")
clock.now += 0.3
c.step(p, own())
check(p.head_reads == 2, "a fresh head observation after the TTL")

p, c = setup()
c.step(p, own(pos=OWN_IN))
check(last_move(p) == set() and not pressed(p), "inside 1000 of Thorn: the rescuer stands")

# Every core refusal wins (Sage P2 a): the hook is never reached, nothing is held off, no movement.
for label, kw, state in (("partner not free", dict(free=False), {}),
                         ("partner in another scope", dict(roomScope=[2, 34, 0, 1, 1, 1]), {}),
                         ("partner actor replaced", dict(player=0x3000), {}),
                         ("partner position unknown", dict(), {"partner": None}),
                         ("own HP 0", dict(), {"hp": 0}),
                         ("own position unknown", dict(), {"pos": None})):
    if "partner" in state:
        p, c = setup(partner=None)
    else:
        p, c = setup(**kw)
    s = own(**{k: v for k, v in state.items() if k != "partner"})
    c.step(p, s)
    check("coop_orb_hold" not in p.kinds() and p.head_reads == 0 and not any(f & MOVE_KEYS for f in p.kb.frames),
          f"{label}: the core refuses, no hold-off, no head read, no movement")

p, c = setup(error=True)
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0 and not any(f & MOVE_KEYS for f in p.kb.frames),
      "unreadable partner: the core stops, no hold-off")

p, c = setup()
c.step(p, own(free=False))
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "own seat not free (menu/event): the core's stop wins")

p, c = setup()
res = c.step(p, own(), event_reaction=True)
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "an event-reaction tick goes to the core untouched")

for label, kw in (("own logical down", dict(own_down=True)), ("full party down", dict(full=True)),
                  ("living partner", dict(peer_down=False)), ("unknown logs", dict(known=False))):
    p, c = setup(**kw)
    c.step(p, own())
    check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, f"{label}: no hold-off and no head read")

p, c = setup(reader=False)
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and last_move(p) == toward(own(), FAR_PARTNER),
      "no head reader on this pilot: the core approaches the partner as before")

p, c = setup(cfg=None)
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0 and last_move(p) == toward(own(), FAR_PARTNER),
      "switch off: unchanged core")

p, c = setup()
c.step(p, own(world=2, room=33))
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "outside the Thorn room: no hold-off")

p, c = setup(partner=[0.0, 0.0, 300.0])
c.step(p, own(pos=[0.0, 0.0, -300.0]))
check("coop_orb_hold" not in p.kinds() and last_move(p) == toward(own(pos=[0.0, 0.0, -300.0]), [0.0, 0.0, 300.0]),
      "partner 1160 from Thorn: the core approaches")

# Inside revive range the core's own hold logic runs; the hook never holds off (finish 300 > 140).
p, c = setup(partner=[0.0, 0.0, 400.0])
c.hold_started, c.hold_frame, c.hold_identity = clock.now, 5, (PLAYER, PARTNER, tuple(SCOPE))
c.step(p, own(pos=[0.0, 0.0, 300.0], reactObservation=dict(source="downed-prompt-v7", frame=9, reviveProgress=300,
                                                           promptKind=1, promptHide=0)))
check("coop_orb_hold" not in p.kinds() and any(REACT_KEY in f for f in p.kb.frames),
      "a started revive hold 100 from the partner keeps holding Triangle")

# RCs: the core's press, steered toward Thorn while held off; the episode stays open.
p, c = setup()
c.step(p, own())
clock.now += 0.1
res = c.step(p, own(react=432))
check(res is True and pressed(p) and "coop_reaction" in p.kinds() and "coop_orb_hold_end" not in p.kinds()
      and last_move(p) == toward(own(), ROOT), "an RC during a hold-off is pressed with the hold-off direction")

# Release paths.
p, c = setup()
c.step(p, own())
p.observed = head(motion=207)
clock.now += 0.3
res = c.step(p, own())
end = p.row("coop_orb_hold_end")
check(res is True and end["reason"] == "released" and last_move(p) == toward(own(), FAR_PARTNER),
      "Thorn leaves 202: the episode ends 'released' and the core walks to the partner")
check(p.orbhold_stats["ends"] == {"released": 1}, "the summary counts the release reason")

p, c = setup()
c.step(p, own())
clock.now += 0.3
c.step(p, own(), event_reaction=True)
check(p.row("coop_orb_hold_end")["reason"] == "core", "an event reaction closes an open episode ('core')")

# The pre-revive Potion wins (Sage P2 b): the real PreReviveCooperativeMixin over the hook.
class PotionCore(thorn_next.PreReviveCooperativeMixin, o.OrbHoldCore):
    def __init__(self, peer, own_log, peer_log):
        init_core(self, peer, own_log, peer_log)


def potion_pilot():
    p = FakePilot()
    p.head_active, p.next_options = True, {"healBeforeRevive": True}
    p.preheal_attempted, p.preheal_logged, p.preheal_aborts = set(), set(), 0
    p.heal_used, p.heal_cfg = 0, {"max": 3}
    p.observe_items = lambda s: {"known": True, "slot0Potion": True}
    p.heal_available = lambda s: True
    p.potions = []
    p.pre_revive_heal = lambda s: p.potions.append(s["hp"])
    return p


p = potion_pilot()
c = PotionCore(Peer(), Log(), Log(True, actor=PARTNER))
res = c.step(p, own(hp=5))
check(res is True and p.potions == [5] and "coop_orb_hold" not in p.kinds() and p.head_reads == 0,
      "5/20 HP with a proven Potion during a volley: the pre-revive Potion runs, no hold-off")
clock.now += 0.3
c.step(p, own(hp=15))
check(p.kinds().count("coop_orb_hold") == 1 and p.potions == [5],
      "after the one Potion attempt (now 15/20) the volley hold-off applies")
p = potion_pilot()
c = PotionCore(Peer(), Log(), Log(True, actor=PARTNER))
c.step(p, own(hp=12))
check(p.potions == [] and p.kinds().count("coop_orb_hold") == 1, "12/20 HP (not eligible): no Potion, hold-off as usual")

# thorn_approach's plateau monitor does not read the stand-off as a stall.
p, c = setup()
c.anchor, c.detour = ("key", (0, 0, 0), (1, 1, 1), 0.0), dict(pulses=0)
c.step(p, own(pos=OWN_IN))
check(c.anchor is None and c.detour is None, "a held-off tick resets the approach plateau anchor and any detour")
p, c = setup(cfg=None)
c.anchor, c.detour = ("key", (0, 0, 0), (1, 1, 1), 0.0), dict(pulses=0)
c.step(p, own(pos=OWN_IN))
check(c.anchor is not None and c.detour is not None, "control: without a hold-off the plateau state is untouched")

# The core's own 60 s bound.
p, c = setup(cfg=dict(CFG, maxWaitSec=100.0))
c.step(p, own())
clock.now += 60.5
try:
    c.step(p, own())
    raised = False
except base.Stuck as e:
    raised = "exceeded 60s" in str(e)
check(raised and base.steer is ORIGINAL_STEER,
      "past 60 s the core raises its own Stuck, and the steer is restored")

# The 30 s cap, and a fresh cap per revive window.
p, c = setup()
for ticks in range(1, 200):
    c.step(p, own())
    if "coop_orb_hold_end" in p.kinds():
        break
    clock.now += 0.25
check(p.row("coop_orb_hold_end")["reason"] == "cap" and abs(c.orbhold_waited - 30.0) < 1e-9 and ticks == 122
      and last_move(p) == toward(own(), FAR_PARTNER), "after 30 s of holding off in one window the approach resumes ('cap')")
n = p.kinds().count("coop_orb_hold")
c.step(p, own())
check(p.kinds().count("coop_orb_hold") == n, "the capped window does not hold off again")
c.peer_log.downed = False
c.step(p, own())
c.peer_log.downed, c.peer_log.actor = True, PARTNER
clock.now += 0.3
c.step(p, own())
check(p.kinds().count("coop_orb_hold") == n + 1 and c.orbhold_waited == 0.0, "a new revive window gets a fresh 30 s cap")

p, c = setup()
c.step(p, own())
clock.now += 1.0
c.step(p, own())
clock.now += 1.0
c.step(p, own())
check(abs(c.orbhold_waited - 2.0) < 1e-9, "the wait counts held-off time only")
p.observed = head(motion=207)
clock.now += 5.0
c.step(p, own())
p.observed = head()
clock.now += 1.0
c.step(p, own())
check(abs(c.orbhold_waited - 2.0) < 1e-9 and p.kinds().count("coop_orb_hold") == 2,
      "time between two volleys in one window is not counted; the second volley opens episode 2")


# ---- r4: the finish latch (zr116's throwback re-arm) -------------------------------------------------------------
# zr116: held off, reached 173 from the partner, a 432 throwback pushed it back to 350 in the same volley, and the
# hold-off re-armed and walked it away. Partner at FAR_PARTNER (z 700), 1560 from Thorn throughout.
p, c = setup()
c.step(p, own(pos=OWN_OUT))                                   # 400 from the partner: held off
check(p.kinds().count("coop_orb_hold") == 1, "r4: the volley holds the rescuer off at first")
clock.now += 0.3
c.step(p, own(pos=[0.0, 0.0, 527.0]))                         # 173 from the partner: finish
check(c.orbhold_finished and p.row("coop_orb_hold_end")["reason"] == "finish-latched",
      "r4: reaching finish (173) latches the window and ends the episode 'finish-latched'")
clock.now += 0.3
p.kb.frames.clear()
c.step(p, own(react=432, pos=[0.0, 0.0, 350.0]))              # the 432 is pressed with the partner's direction
clock.now += 0.3
c.step(p, own(pos=[0.0, 0.0, 350.0]))                         # thrown back to 350, Thorn still in 202
check(p.kinds().count("coop_orb_hold") == 1 and last_move(p) == toward(own(pos=[0.0, 0.0, 350.0]), FAR_PARTNER),
      "r4: thrown back to 350 in the same volley, it keeps walking to the partner instead of re-arming")
clock.now += 0.3
c.step(p, own(pos=OWN_OUT))                                   # even back at 400 or farther
check(p.kinds().count("coop_orb_hold") == 1, "r4: the latched window never holds off again, however far it is pushed")
c.peer_log.downed = False
c.step(p, own(pos=OWN_OUT))
check(not c.orbhold_finished, "r4: the partner's recovery clears the latch")
c.peer_log.downed, c.peer_log.actor = True, PARTNER
clock.now += 0.3
c.step(p, own(pos=OWN_OUT))
check(p.kinds().count("coop_orb_hold") == 2, "r4: the next revive window can hold off again")

p, c = setup()
c.step(p, own(pos=OWN_OUT))
clock.now += 0.3
c.step(p, own(pos=[0.0, 0.0, 400.0]))                         # exactly 300: finish is inclusive
check(c.orbhold_finished, "r4: exactly `finish` (300) from the partner latches")
p, c = setup()
c.step(p, own(pos=OWN_OUT))
clock.now += 0.3
c.step(p, own(pos=[0.0, 0.0, 399.9]))                         # 300.1: still held off, no latch
check(not c.orbhold_finished and p.kinds().count("coop_orb_hold") == 1 and "coop_orb_hold_end" not in p.kinds(),
      "r4: 300.1 from the partner neither latches nor releases")

# r4b (1): the latch and the cap belong to the core's window, whatever tick ends it (Sage, on r4).
p, c = setup()
c.step(p, own(pos=OWN_OUT))
clock.now += 0.3
c.step(p, own(pos=[0.0, 0.0, 527.0]))                         # latched at 173
c.peer_log.downed = False
clock.now += 0.3
c.step(p, own(pos=OWN_OUT), event_reaction=True)              # the partner recovers on an event-reaction tick
check(not c.orbhold_finished and c.since is None, "r4b: a recovery on an event-reaction tick clears the latch")
c.peer_log.downed, c.peer_log.actor = True, PARTNER
clock.now += 0.3
c.step(p, own(pos=OWN_OUT))                                   # the next window, volley, partner far
check(p.kinds().count("coop_orb_hold") == 2, "r4b: the next window holds off again after an event-tick recovery")

p, c = setup(cfg=dict(CFG, maxWaitSec=1.0))
for _ in range(6):
    c.step(p, own(pos=OWN_OUT))
    clock.now += 0.25
check(p.row("coop_orb_hold_end")["reason"] == "cap", "r4b: a 1 s cap is reached in the first window")
c.peer_log.downed = False
clock.now += 0.3
c.step(p, own(pos=OWN_OUT), event_reaction=True)
c.peer_log.downed, c.peer_log.actor = True, PARTNER
clock.now += 0.3
n = p.kinds().count("coop_orb_hold")
c.step(p, own(pos=OWN_OUT))
check(c.orbhold_waited == 0.0 and p.kinds().count("coop_orb_hold") == n + 1,
      "r4b: the cap is fresh in the next window after an event-tick recovery")

# r4c: one DownedLog poll can coalesce a recovery and a fresh interception (Sage, on r4b). The production DownedLog
# reads a real file: downs and revives advance while `downed` stays true and the core's `since` never changes.
with tempfile.TemporaryDirectory() as d:
    log = base.DownedLog(Peer.pid)
    log.path = Path(d) / "peer.log"
    log.path.write_text("[downed] gate intercept actor=2000 x=1\n", encoding="utf-8")
    p = FakePilot()
    c = Core(Peer(), Log(), log)
    c.step(p, own(pos=[0.0, 0.0, 527.0]))                     # window 1, latched at 173
    before = (c.orbhold_finished, c.since, log.downs, log.revives)
    with log.path.open("a", encoding="utf-8") as fh:
        fh.write("[downed] revive returned state=1 hp=20/20 fault=0\n[downed] gate intercept actor=2000 x=1\n")
    clock.now += 0.3
    c.step(p, own(pos=OWN_OUT))                               # one poll: recovered and downed again
    after = (c.since, log.downs, log.revives, log.downed)
check(before[0] is True and before[2:] == (1, 0), "r4c: window 1 latched on the real DownedLog (downs 1, revives 0)")
check(after[0] == before[1] and after[1:] == (2, 1, True),
      "r4c: the coalesced poll advanced downs/revives while downed and the core's since stayed the same")
check(p.kinds().count("coop_orb_hold") == 1 and not c.orbhold_finished,
      "r4c: the new episode holds off on that very tick; the old latch doesn't leak into it")

# A plain re-poll with no new rows keeps the episode (and its latch).
with tempfile.TemporaryDirectory() as d:
    log = base.DownedLog(Peer.pid)
    log.path = Path(d) / "peer.log"
    log.path.write_text("[downed] gate intercept actor=2000 x=1\n", encoding="utf-8")
    p = FakePilot()
    c = Core(Peer(), Log(), log)
    c.step(p, own(pos=[0.0, 0.0, 527.0]))
    clock.now += 0.3
    c.step(p, own(pos=OWN_OUT))
check(c.orbhold_finished and p.kinds().count("coop_orb_hold") == 0,
      "r4c control: with no new log rows the same episode stays latched")


# r4b (2): arrivals within 80 never steer, yet the core's own validated gap latches them.
for label, arrive in (("50", [0.0, 0.0, 650.0]), ("0", [0.0, 0.0, 700.0])):
    p, c = setup()
    c.step(p, own(pos=OWN_OUT))                               # held off
    clock.now += 0.3
    c.step(p, own(pos=arrive))                                # arrives at 50 / 0: the core never steers
    check(c.orbhold_finished, f"r4b: an arrival at {label} (no steer) latches the window")
    clock.now += 0.3
    c.step(p, own(pos=[0.0, 0.0, 350.0]))                     # thrown back to 350, still in the volley
    check(p.kinds().count("coop_orb_hold") == 1 and last_move(p) == toward(own(pos=[0.0, 0.0, 350.0]), FAR_PARTNER),
          f"r4b: after arriving at {label}, a throwback to 350 doesn't re-arm the hold-off")

p, c = setup(free=False)
c.step(p, own(pos=[0.0, 0.0, 650.0]))                         # 50 away, but the core refuses (partner not free)
check(not c.orbhold_finished, "r4b: a refused tick records no gap and doesn't latch (validated)")
check(thorn.math is __import__("math"), "r4b: thorn's math is restored after the tick")


# Controls: r3 behaviour is unchanged when the rescuer never reaches finish, and outside revive windows.
p, c = setup()
c.step(p, own())
p.observed = head(motion=207)
clock.now += 0.3
c.step(p, own())
p.observed = head()
clock.now += 0.3
c.step(p, own())
check(not c.orbhold_finished and p.kinds().count("coop_orb_hold") == 2,
      "control: without reaching finish, a second volley in the window still holds off (r3)")
p, c = setup(peer_down=False)
c.step(p, own(pos=[0.0, 0.0, 527.0]))
check(not c.orbhold_finished and "coop_orb_hold" not in p.kinds(), "control: outside a revive window nothing latches")
p, c = setup(cfg=None)
c.step(p, own(pos=[0.0, 0.0, 527.0]))
check(not c.orbhold_finished, "control: switch off, nothing latches")


# ---- full production MRO: a stalled approach, then a volley (Sage, on r2) ----------------------------------------
class ApproachCore(thorn_approach.ApproachRevisionMixin, Core):
    pass


class RcGateStub:
    def drain(self):
        return []


class CallbacksStub:
    def poll(self):
        return True, 0


thorn_approach.time.sleep = lambda seconds: None


def stalled_then_volley(cfg):
    """Tick 1: an ordinary approach (Thorn in 207) anchors the real plateau monitor. Tick 2, 3 s later at the same
    positions: Thorn is in its volley and this seat is inside the hold radius. Returns tick 2's pilot and fight."""
    clock.now = 100.0
    p, _ = setup(cfg=cfg, observed=head(motion=207))
    s = own(pos=OWN_IN)
    p.state = lambda: s
    seed = ApproachCore(Peer(), Log(), Log(True, actor=PARTNER))
    c = object.__new__(o.CooperativeFight)
    c.__dict__.update(seed.__dict__)
    c.rc_gate, c.callback_reader = RcGateStub(), CallbacksStub()
    c.step(p, s)
    p.rows.clear()
    p.kb.frames.clear()
    clock.now += 3.0
    p.observed = head()
    c.plateau_log_at = clock.now  # skip only the plateau row's memory snapshot
    c.step(p, s)
    return p, c


p, c = stalled_then_volley(CFG)
check("coop_orb_hold" in p.kinds() and not any(k in ("coop_detour", "coop_stuck_escape", "coop_vertical_escape")
                                                for k in p.kinds()),
      "full MRO, stalled then volley: held off with no detour pulse or escape on that tick")
check(c.escape_tries == 0 and c.detour is None and not any(f & MOVE_KEYS for f in p.kb.frames),
      "no escape is spent and the seat stands inside the hold radius")
check(c.orbhold_tick_held is True and c.facts(p, own(pos=OWN_IN)) is None,
      "the approach layer's facts read empty for the rest of a held tick")
clock.now += 0.5
p.observed = head(motion=207)
c.step(p, own(pos=OWN_IN))
f = c.facts(p, own(pos=OWN_IN))
check(c.orbhold_tick_held is False and f is not None and f[2] == tuple(OWN_IN),
      "once Thorn leaves 202 the next tick's facts are live again, so approach revision keeps working")
p, c = stalled_then_volley(None)
check(any(k == "coop_detour" and f.get("stage") == "pulse" for k, f in p.rows) and c.escape_tries == 1,
      "control (hold-off off): the same stalled sequence spends escape 1 on a detour pulse")
clock.now = 1000.0


# ---- the report counts hits taken while held off ------------------------------------------------------------------
p = FakePilot()
c = Core(Peer(), Log(), Log(True, actor=PARTNER))
for frame in (100, 130, 160):                      # three held-off ticks, own-game frames 100..160
    c.step(p, own(reactObservation={"frame": frame}))
    clock.now += 0.5
p.observed = head(motion=207)
c.step(p, own(reactObservation={"frame": 190}))    # released: the core approaches the partner
clock.now += 0.5
c.peer_log.downed = False
c.step(p, own(reactObservation={"frame": 220}))    # the partner recovered: coop_revive combat
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "friend_thorn").mkdir()
    (Path(d) / "friend_thorn" / "pilot.jsonl").write_text(p.jsonl(), encoding="utf-8")
    hits = [(95, "1041"), (140, "1041"), (200, "400"), (300, "1041")]
    w = orbhold_report.windows(Path(d), "friend_thorn", hits)
    old = rescue_report.windows(Path(d), "friend_thorn", hits)
check(len(w) == 1 and w[0]["result"] == "recovered" and w[0]["holdOffs"] == 1 and w[0]["holdOffEnds"] == {"released": 1}
      and w[0]["partnerToThorn"] == [1560.0], "the report shows the window's hold-off, its end reason and the partner's distance")
check(w[0]["frames"] == [70, 220] and w[0]["orbHits"] == 2 and w[0]["hits"] == {"1041": 2, "400": 1},
      "hits while held off count (frames 70..220): two orb hits and the 400, not the one at 300")
check(old[0]["orbHits"] == 2 and old[0]["frames"] == w[0]["frames"],
      "rescue_report agrees: the core's own approach rows cover the held-off frames")


# ---- switch semantics, summary, MRO ------------------------------------------------------------------------------
class Lower:
    def fight(self, step, deadline):
        self.seen = getattr(self, "orbhold_cfg", None)
        return "fought"


class P(o.OrbHoldPilotMixin, Lower):
    def __init__(self):
        self.rows = []

    def write(self, kind, **f):
        self.rows.append((kind, f))


pp = P()
check(pp.fight({"eventReactions": True, "rescueOrbHold": {}}, 0) == "fought" and pp.seen == CFG,
      "{} on the Thorn fight step gives the defaults")
check(pp.rows[-1][0] == "rescue_orb_hold_summary" and pp.orbhold_cfg is None, "one summary row; cfg cleared after the fight")
pp = P()
pp.fight({"eventReactions": True, "rescueOrbHold": True}, 0)
check(pp.seen == CFG, "true gives the defaults")
pp = P()
pp.fight({"eventReactions": True, "rescueOrbHold": {"far": 1400, "maxWaitSec": 20}}, 0)
check(pp.seen == dict(CFG, far=1400.0, maxWaitSec=20.0), "explicit values override the defaults")
pp = P()
pp.fight({"eventReactions": True}, 0)
check(pp.seen is None and not pp.rows, "absent key: off, no summary")
pp = P()
pp.fight({"rescueOrbHold": {}}, 0)
check(pp.seen is None, "the key outside the Thorn fight step is ignored")

mro = o.CooperativeFight.__mro__
i = mro.index(o.OrbHoldCore)
check(mro[1] is o.OrbHoldApproachMixin and mro[2] is thorn_creeper.CooperativeFight and mro[i + 1] is thorn.CooperativeFight
      and mro.index(thorn_next.PreReviveCooperativeMixin) < i,
      "the facts guard is outermost; the hook sits directly above the revive core, below the Potion and every other layer")
check(o.Pilot.__mro__[1] is o.OrbHoldPilotMixin and o.Pilot.__mro__[2] is thorn_creeper.Pilot, "Pilot: orb hold over thorn_creeper")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
