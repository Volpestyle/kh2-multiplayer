"""Offline controls for tools/rig/thorn_pilot/thorn_protect.py (VUH-2617 lead #328, revive-protect r5).

The real revive core (thorn.CooperativeFight.step) runs under the real OrbHoldCore and ProtectCore hooks and writes the
real Pad stack (ProtectPadMixin included); the production-MRO case adds every higher layer. Only the downed logs, the
partner's state, the head observation, the clock, the pilot's write and the mailbox write are fakes. The zr118 and
zr119 cases replay those runs' rescuer positions, commands and prompt readings.
Run: python -B tests/test_thorn_protect.py
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import orbhold_report  # noqa: E402
import thorn  # noqa: E402
import thorn_approach  # noqa: E402
import thorn_next  # noqa: E402
import thorn_orbhold as o  # noqa: E402
import thorn_protect as r  # noqa: E402

base = r.base
failures = 0
REACT_KEY = base.KEYS["react"].upper()
MOVE_KEYS = {base.KEYS[k].upper() for k in ("up", "down", "left", "right")}
ORB = dict(far=1250.0, hold=1000.0, finish=300.0, maxWaitSec=30.0)
CFG = dict(commands=frozenset((432, 667)), within=300.0)


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


# ---- pure rule ---------------------------------------------------------------------------------------------------
check(r.protect(CFG, 432, True, None), "432 while reviving: protected (the distance is not needed)")
check(r.protect(CFG, 667, False, 300.0), "667 exactly 300 from the partner: protected (within is inclusive)")
check(not r.protect(CFG, 667, False, 300.1), "667 300.1 away, not reviving: taken as before")
check(not r.protect(CFG, 667, False, None), "unknown distance, not reviving: taken as before")
check(not r.protect(CFG, 426, True, 71.0), "426 is not listed: taken as before")
check(not r.protect(CFG, 0, True, 71.0) and not r.protect(CFG, None, True, 71.0), "no RC offered: nothing to protect")
check(not r.protect(None, 432, True, 71.0), "switch off: taken as before")


# ---- the real revive core under both hooks ------------------------------------------------------------------------
PLAYER, PARTNER = 0x1000, 0x2000
SCOPE = [2, 34, 0, 157, 157, 157]
ROOT = (0.0, 0.0, -860.0)


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


clock = Clock()
for module in (o, r, thorn, thorn_next):
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
        self.kb, self.rows, self.orbhold_cfg, self.protect_cfg = make_pad(), [], ORB, cfg
        self.orbhold_stats = dict(episodes=0, seconds=0.0, ends={})
        self.protect_stats = dict(episodes=0, ticks=0, byCommand={}, reviving=0)
        self.observed = dict(known=True, motion=207, rootWorldPos=ROOT)  # Thorn out of its volley: no hold-off
        self.head_reader = object()
        self.game = None

    def observe_head(self, state):
        return self.observed

    def write(self, kind, **f):
        self.rows.append((kind, dict(f, _t=clock.now)))

    def kinds(self):
        return [k for k, _ in self.rows]

    def rows_of(self, kind):
        return [f for k, f in self.rows if k == kind]

    def jsonl(self):
        return "".join(json.dumps(dict(t=f["_t"], kind=k, **{x: v for x, v in f.items() if x != "_t"})) + "\n"
                       for k, f in self.rows)


def init_core(c, peer, own_log, peer_log):
    c.peer, c.own_log, c.peer_log = peer, own_log, peer_log
    c.since, c.mode = None, ""
    c.hold_started = c.hold_frame = c.hold_identity = None
    c.rc_next = c.rc_until = c.observed_at = 0.0
    c.arm_frame = c.armed_identity = None
    c.waiting_release = False
    c.last_progress = 0
    c.last_hold_hp = None


class Core(o.OrbHoldCore, r.ProtectCore):
    def __init__(self, peer, own_log, peer_log):
        init_core(self, peer, own_log, peer_log)


def setup(cfg=CFG, partner=(-180.0, 0.0, 642.0), own_down=False, full=False, peer_down=True, known=True, error=False,
          actor=PARTNER):
    p = FakePilot(cfg)
    c = Core(Peer(partner, error), Log(own_down, full, known=known), Log(peer_down, full, actor=actor, known=known))
    return p, c


def prompt(frame, progress=0, hidden=False):
    return dict(source="downed-prompt-v7", frame=frame, reviveProgress=progress,
                promptKind=0 if hidden else 1, promptHide=7 if hidden else 0)


def own(pos, react=0, obs=None, hp=18, **kw):
    s = dict(world=2, room=34, free=True, inField=True, hp=hp, maxHp=20, roomScope=SCOPE, playerPos=list(pos),
             player=PLAYER, react=react, forward=[0.0, -1.0], reactObservation=obs or {}, event=0, menu=255)
    s.update(kw)
    return s


def tick(p, c, s, dt=0.1, event_reaction=False):
    before = len(p.kb.frames)
    res = c.step(p, s, event_reaction)
    clock.now += dt
    return res, p.kb.frames[before:]


def held_react(p):
    return REACT_KEY in p.kb.held


def toward(state, target):
    return {base.KEYS[k].upper() for k in base.steer({**state, "pos": state["playerPos"]}, (target[0], target[2]))}


# zr119 (20261025-150433, friend): the partner downed at (241.3, 815.6); the rescuer reached 71 and began a hold.
Z119_PARTNER = (241.3, 0.0, 815.6)
Z119_AT = (264.5, 0.0, 883.2)   # 71.5 away


def zr119_holding(cfg):
    """The rescuer 71 from its partner with a revive hold in progress (progress 200), as at zr119 +126.9 s."""
    p, c = setup(cfg, partner=Z119_PARTNER)
    tick(p, c, own(Z119_AT, obs=prompt(17423)))                 # initial fresh press: arm
    tick(p, c, own(Z119_AT, obs=prompt(17426)))                 # hold begins
    tick(p, c, own(Z119_AT, obs=prompt(17440, 200)))
    return p, c


# zr119 revive cut, prompt still visible: a 432 mid-hold.
p, c = zr119_holding(CFG)
began = p.kinds().count("coop_hold_begin")
_, frames = tick(p, c, own(Z119_AT, react=432, obs=prompt(17446, 300)))
check(began == 1 and held_react(p) and all(REACT_KEY in f for f in frames) and "coop_hold_end" not in p.kinds()
      and "coop_reaction" not in p.kinds(),
      "zr119: a 432 mid-hold with the revive prompt visible: the hold stays held, no RC press, no hold end")
row = p.rows_of("coop_rc_protected")
check(len(row) == 1 and row[0]["command"] == 432 and row[0]["reviving"] is True and row[0]["horizontal"] == 71.5,
      "zr119: the protected RC is logged once, with the command, the 71.5 gap and reviving")
tick(p, c, own(Z119_AT, react=432, obs=prompt(17452, 600)))
tick(p, c, own(Z119_AT, obs=prompt(17470, 1000)))
check([f["reason"] for f in p.rows_of("coop_hold_end")] == ["native-bar-full-release"] and not held_react(p)
      and c.waiting_release, "zr119: the same hold runs to a full bar and releases to fire the revive")
check(p.kb.__dict__.get("protect_stripped", 0) == 0, "an already-held Triangle is never stripped")
p, c = zr119_holding(None)
tick(p, c, own(Z119_AT, react=432, obs=prompt(17446, 300)))
check([f["reason"] for f in p.rows_of("coop_hold_end")] == ["native-reaction"] and "coop_reaction" in p.kinds()
      and held_react(p) and "coop_rc_protected" not in p.kinds(),
      "control zr119 (protect off): the core ends the hold for the 432 and presses it, as in the run")

# zr119 revive cut, prompt hidden behind the RC (the run's observation: promptKind 0, promptHide 7).
p, c = zr119_holding(CFG)
seq = []
for frame in (17446, 17452, 17458):
    _, frames = tick(p, c, own(Z119_AT, react=432, obs=prompt(frame, 0, hidden=True)))
    seq += frames
ends = [f["reason"] for f in p.rows_of("coop_hold_end")]
check(ends == ["prompt-not-visible"] and not any(REACT_KEY in f for f in seq) and "coop_reaction" not in p.kinds(),
      "zr119, prompt hidden: the core releases (prompt-not-visible) and no Triangle goes out while the 432 is offered")
seq = []
for frame in (17464, 17467, 17480):
    _, frames = tick(p, c, own(Z119_AT, obs=prompt(frame, 0 if frame < 17480 else 200)))
    seq += frames
check([f["reason"] for f in p.rows_of("coop_rearm")][-1] == "initial-fresh-press" and p.kinds().count("coop_hold_begin") == 2
      and any(REACT_KEY in f for f in seq), "zr119: once the RC is gone the core re-arms and presses a fresh hold")
p, c = zr119_holding(None)
seq = []
for frame in (17446, 17452, 17458):                         # the core's 0.18 s press every 0.35 s
    _, frames = tick(p, c, own(Z119_AT, react=432, obs=prompt(frame, 0, hidden=True)), dt=0.2)
    seq += frames
check("coop_reaction" in p.kinds() and seq and REACT_KEY not in seq[0] and any(REACT_KEY in f for f in seq[1:]),
      "control zr119 hidden prompt (protect off): the core releases and presses the 432 afresh")

# A fresh Triangle press with a protected RC on screen would be that RC: the pad strips it.
p, c = setup(partner=Z119_PARTNER)
tick(p, c, own(Z119_AT, react=667, obs=prompt(17500)))       # arm (the core sees no RC)
_, frames = tick(p, c, own(Z119_AT, react=667, obs=prompt(17503)))
check("coop_hold_begin" in p.kinds() and not any(REACT_KEY in f for f in p.kb.frames)
      and p.kb.protect_stripped == 1, "a hold that would start while a 667 is offered sends no fresh Triangle")
tick(p, c, own(Z119_AT, obs=prompt(17506)))
check(held_react(p), "the next tick without the RC presses Triangle for that hold")
check(p.kb.protect_mask is False, "the pad mask is cleared after every tick")

# zr119 after the throwbacks: 667 at 200 (within 300), no revive in progress: the core keeps walking to the partner.
NEAR = (241.3, 0.0, 615.6)
p, c = setup(partner=Z119_PARTNER)
_, frames = tick(p, c, own(NEAR, react=667))
check(frames and frames[-1] & MOVE_KEYS == toward(own(NEAR), Z119_PARTNER) and not any(REACT_KEY in f for f in frames)
      and "coop_reaction" not in p.kinds() and p.rows_of("coop_rc_protected")[0]["reviving"] is False,
      "667 at 200, not reviving: no press, the rescuer keeps walking to the partner")
p, c = setup(None, partner=Z119_PARTNER)
_, frames = tick(p, c, own(NEAR, react=667))
check(any(REACT_KEY in f for f in frames) and "coop_reaction" in p.kinds(), "control (protect off): the 667 is pressed")

# zr118 window 2 (20261025-143708): 667 offered 1070-1330 from the partner, no revive in progress. Out of scope: every
# frame and row is the same as with protect off.
Z118_PARTNER = (-292.6, 0.0, -291.2)
Z118_AT = ((875.9, 0.0, 336.4), (632.5, 0.0, 333.3), (586.4, 0.0, 353.2), (552.5, 0.0, 365.9))


def zr118(cfg):
    p, c = setup(cfg, partner=Z118_PARTNER)
    for i, at in enumerate(Z118_AT * 2):
        tick(p, c, own(at, react=667 if i % 2 == 0 else 0, hp=4), dt=0.4)
    return p


on, off = zr118(CFG), zr118(None)
check(on.kb.frames == off.kb.frames and [k for k in on.kinds()] == [k for k in off.kinds()]
      and "coop_rc_protected" not in on.kinds() and on.kinds().count("coop_reaction") >= 4,
      "zr118 window 2: 667s 1070-1330 away are pressed exactly as before (outside `within`)")

# Controls: unchanged when no partner is downed, for unlisted commands and for every refusal.
for label, kw, react in (("no partner downed", dict(peer_down=False), 432), ("own logical down", dict(own_down=True), 432),
                         ("full party down", dict(full=True), 432), ("unknown logs", dict(known=False), 432),
                         ("unlisted 426", dict(), 426), ("unlisted 345", dict(), 345)):
    runs = []
    for cfg in (CFG, None):
        clock.now = 2000.0
        p, c = setup(cfg, partner=Z119_PARTNER, **kw)
        res, _ = tick(p, c, own(Z119_AT, react=react, obs=prompt(17600)))
        runs.append((res, p.kb.frames, p.kinds()))
    check(runs[0] == runs[1] and "coop_rc_protected" not in runs[0][2], f"{label}: identical to protect off")
clock.now = 2000.0
p, c = setup(partner=Z119_PARTNER)
res, frames = tick(p, c, own(Z119_AT, react=432), event_reaction=True)
check("coop_rc_protected" not in p.kinds() and res is False, "an event-reaction tick goes to the core untouched")
p, c = setup(partner=Z119_PARTNER)
check(c.protect_check(p, own(Z119_AT, react=432, world=2, room=33), False) is None, "outside the Thorn room: unchanged")
pad = make_pad()
check(pad.tap("react", 0) is True and any(REACT_KEY in f for f in pad.frames), "unmasked pad: an RC tap goes out")
pad = make_pad()
pad.protect_mask = True
check(pad.tap("react", 0) is False and not pad.frames, "masked pad: a fresh RC tap is dropped")

# A revive in progress stays protected even if the partner can't be read this tick.
p, c = zr119_holding(CFG)
c.peer.error = True
mask = c.protect_check(p, own(Z119_AT, react=432, obs=prompt(17446, 300)), False)
check(mask == (432, None, True), "reviving with the partner unreadable: still protected")
p, c = setup(partner=Z119_PARTNER, error=True)
check(c.protect_check(p, own(NEAR, react=432), False) is None, "not reviving and the partner unreadable: not protected")
p, c = zr119_holding(CFG)
tick(p, c, own(Z119_AT, obs=prompt(17470, 1000)))
c.peer.error = True
check(c.waiting_release and c.protect_check(p, own(Z119_AT, react=432, obs=prompt(17476)), False) == (432, None, True),
      "a full bar awaiting recovery counts as a revive in progress")
p, c = setup(partner=Z119_PARTNER, actor=0x3000)
check(c.protect_check(p, own(NEAR, react=432), False) is None,
      "the partner read isn't the downed actor (replaced), not reviving: no distance, not protected")
p, c = setup(partner=Z119_PARTNER, peer_down=False)
check(c.protect_check(p, own(NEAR, react=432), False) is None, "a living partner 200 away: not protected")
LOW = (241.3, -349.0, 615.6)    # zr119 +105.0: the rescuer 349 below its partner's height, 200 away horizontally
p, c = setup(partner=Z119_PARTNER)
check(c.protect_check(p, own(LOW, react=667), False) == (667, 200.0, False),
      "height is ignored: 200 away horizontally (403 in 3D) is within 300")

# Episodes: one row per command run, a new one after a 0.5 s gap or a different command.
p, c = setup(partner=Z119_PARTNER)
for react, dt in ((667, 0.2), (667, 0.2), (667, 0.7), (667, 0.2), (432, 0.2)):
    tick(p, c, own(NEAR, react=react), dt=dt)
check([f["episode"] for f in p.rows_of("coop_rc_protected")] == [1, 2, 3] and p.protect_stats["ticks"] == 5
      and p.protect_stats["byCommand"] == {"667": 2, "432": 1}, "episodes split on a 0.5 s gap or a new command")


# ---- full production MRO: a protected RC near the partner -----------------------------------------------------------
class RcGateStub:
    def drain(self):
        return []


class CallbacksStub:
    def __init__(self):
        self.serial = 0

    def poll(self):
        return True, self.serial


thorn_approach.time.sleep = lambda seconds: None


class ApproachCore(thorn_approach.ApproachRevisionMixin, Core):
    pass


def production(cfg):
    clock.now = 3000.0
    p, _ = setup(cfg, partner=Z119_PARTNER)
    seed = ApproachCore(Peer(Z119_PARTNER), Log(), Log(True, actor=PARTNER))
    c = object.__new__(r.CooperativeFight)
    c.__dict__.update(seed.__dict__)
    c.rc_gate, c.callback_reader = RcGateStub(), CallbacksStub()
    s = own(NEAR, react=667)
    p.state = lambda: s
    c.plateau_log_at = clock.now
    _, frames = tick(p, c, s)
    return p, c, frames


p, c, frames = production(CFG)
check("coop_rc_protected" in p.kinds() and not any(REACT_KEY in f for f in frames) and "coop_reaction" not in p.kinds()
      and "coop_approach_reset" not in p.kinds(),
      "production MRO: the 667 at 200 is not pressed and the approach layer does not reset")
p, c, frames = production(None)
check("coop_reaction" in p.kinds() and any(REACT_KEY in f for f in frames), "control production MRO: the 667 is pressed")
mro = r.CooperativeFight.__mro__
check(mro.index(o.OrbHoldCore) + 1 == mro.index(r.ProtectCore) and mro[mro.index(r.ProtectCore) + 1] is thorn.CooperativeFight
      and mro[1] is o.CooperativeFight and mro[2] is o.OrbHoldApproachMixin and mro.index(thorn_approach.ApproachRevisionMixin) < mro.index(r.ProtectCore),
      "ProtectCore sits directly above the revive core, under the orb hold-off and every other layer")
check(r.Pilot.__mro__[1] is r.ProtectPilotMixin and r.Pilot.__mro__[2] is o.Pilot
      and r.Pad.__mro__[1] is r.ProtectPadMixin and r.Pad.__mro__[2] is o.previous.Pad,
      "Pilot and Pad: revive-protect over thorn_orbhold's")


# ---- switch semantics and summary ---------------------------------------------------------------------------------
class Lower:
    def fight(self, step, deadline):
        self.seen = getattr(self, "protect_cfg", None)
        return "fought"


class P(r.ProtectPilotMixin, Lower):
    def __init__(self):
        self.rows, self.kb = [], make_pad()

    def write(self, kind, **f):
        self.rows.append((kind, f))


pp = P()
check(pp.fight({"eventReactions": True, "rescueProtect": {}}, 0) == "fought" and pp.seen == CFG,
      "{} on the Thorn fight step gives the defaults (432/667 within 300)")
check(pp.rows and pp.rows[-1][0] == "rescue_protect_summary" and pp.protect_cfg is None, "one summary row; cfg cleared after the fight")
pp = P()
pp.fight({"eventReactions": True, "rescueProtect": {"commands": [432, 426, 667], "within": 250}}, 0)
check(pp.seen == dict(commands=frozenset((432, 426, 667)), within=250.0), "explicit values override the defaults")
pp = P()
pp.fight({"eventReactions": True}, 0)
check(pp.seen is None and not pp.rows, "absent key: off, no summary")
pp = P()
pp.fight({"rescueProtect": {}}, 0)
check(pp.seen is None, "the key outside the Thorn fight step is ignored")


# ---- the report counts protected RCs, RCs taken and hold ends -------------------------------------------------------
p, c = zr119_holding(CFG)
tick(p, c, own(Z119_AT, react=432, obs=prompt(17446, 300)))
tick(p, c, own(Z119_AT, obs=prompt(17470, 1000)))
c.peer_log.downed = False
tick(p, c, own(Z119_AT, obs=prompt(17500)))
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "friend_thorn").mkdir()
    (Path(d) / "friend_thorn" / "pilot.jsonl").write_text(p.jsonl(), encoding="utf-8")
    w = orbhold_report.windows(Path(d), "friend_thorn", [])
check(len(w) == 1 and w[0]["result"] == "recovered" and w[0]["protected"] == {"432": 1} and w[0]["rcTaken"] == 0
      and w[0]["holdEnds"] == {"native-bar-full-release": 1}, "orbhold_report: protected 432 x1, no RC taken, one full bar")
p, c = zr119_holding(None)
tick(p, c, own(Z119_AT, react=432, obs=prompt(17446, 300)))
c.peer_log.downed = False
tick(p, c, own(Z119_AT, obs=prompt(17500)))
with tempfile.TemporaryDirectory() as d:
    (Path(d) / "friend_thorn").mkdir()
    (Path(d) / "friend_thorn" / "pilot.jsonl").write_text(p.jsonl(), encoding="utf-8")
    w = orbhold_report.windows(Path(d), "friend_thorn", [])
check(w[0]["protected"] == {} and w[0]["rcTaken"] == 1 and w[0]["holdEnds"] == {"native-reaction": 1},
      "orbhold_report, protect off: the 432 taken and the hold it ended")

# r5b (Sage, on r5): a protection on a revive window's first tick belongs to that window. The real core writes its
# `coop_revive peer-logical-downed` marker during the tick, so the protected row must follow it.
for react, at in ((432, Z119_AT), (667, NEAR)):
    clock.now = 4000.0
    p, c = setup(partner=Z119_PARTNER)
    tick(p, c, own(at, react=react, obs=prompt(17700)))       # the window's first tick, RC already offered
    kinds = p.kinds()
    first = kinds.index("coop_revive") if "coop_revive" in kinds else None
    check(first is not None and "coop_rc_protected" in kinds and first < kinds.index("coop_rc_protected"),
          f"r5b: on the window's first tick the core's window marker precedes the protected {react}")
    c.peer_log.downed = False
    tick(p, c, own(at, obs=prompt(17710)))
    with tempfile.TemporaryDirectory() as d:
        (Path(d) / "friend_thorn").mkdir()
        (Path(d) / "friend_thorn" / "pilot.jsonl").write_text(p.jsonl(), encoding="utf-8")
        w = orbhold_report.windows(Path(d), "friend_thorn", [])
    check(len(w) == 1 and w[0]["protected"] == {str(react): 1} and p.protect_stats["byCommand"] == {str(react): 1},
          f"r5b: orbhold_report counts the first-tick protected {react} in its window")

# r5c (Sage, on r5b): coalescing is per revive window. The same protected RC recurring within 0.5 s after a recovery,
# in a new window, is that window's own row; so is one after a poll that coalesced a recovery and a new downing.
for react, at in ((432, Z119_AT), (667, NEAR)):
    clock.now = 5000.0
    p, c = setup(partner=Z119_PARTNER)
    tick(p, c, own(at, react=react, obs=prompt(17800)), dt=0.1)     # window 1, protected
    c.peer_log.downed = False
    tick(p, c, own(at, obs=prompt(17806)), dt=0.1)                  # recovered: the core ends the window
    c.peer_log.downed, c.peer_log.downs = True, 2
    tick(p, c, own(at, react=react, obs=prompt(17812)), dt=0.1)     # window 2, 0.2 s after window 1's row
    c.peer_log.downed = False
    tick(p, c, own(at, obs=prompt(17818)))
    with tempfile.TemporaryDirectory() as d:
        (Path(d) / "friend_thorn").mkdir()
        (Path(d) / "friend_thorn" / "pilot.jsonl").write_text(p.jsonl(), encoding="utf-8")
        w = orbhold_report.windows(Path(d), "friend_thorn", [])
    check(len(p.rows_of("coop_rc_protected")) == 2 and len(w) == 2
          and [x["protected"] for x in w] == [{str(react): 1}, {str(react): 1}],
          f"r5c: a {react} recurring 0.2 s into a new window after a recovery is logged and reported in that window")

    clock.now = 5100.0
    p, c = setup(partner=Z119_PARTNER)
    tick(p, c, own(at, react=react, obs=prompt(17900)), dt=0.1)
    since = c.since
    c.peer_log.downs = 2                                             # one poll: revived and downed again
    tick(p, c, own(at, react=react, obs=prompt(17906)), dt=0.1)
    check(c.since == since and [f["episode"] for f in p.rows_of("coop_rc_protected")] == [1, 2],
          f"r5c: a coalesced recovery + downing (same since, downs 2) gives the {react} a new row")

    clock.now = 5200.0
    p, c = setup(partner=Z119_PARTNER)
    for _ in range(3):
        tick(p, c, own(at, react=react, obs=prompt(18000)), dt=0.2)
    check(len(p.rows_of("coop_rc_protected")) == 1, f"r5c control: the same {react} within one window stays one row")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
