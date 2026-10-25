"""Offline controls for tools/rig/thorn_pilot/thorn_orbhold.py (VUH-2617 lead #302, wait out Thorn's orb volley).

The real revive core (thorn.CooperativeFight.step) runs under the real OrbHoldCooperativeMixin and writes the real
Pad stack; only the downed logs, the partner's state, the head observation, the clock, the pilot's write and the
mailbox write are fakes. Run: python -B tests/test_thorn_orbhold.py
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import thorn  # noqa: E402
import thorn_creeper  # noqa: E402
import orbhold_report  # noqa: E402
import rescue_report  # noqa: E402
import thorn_orbhold as o  # noqa: E402

base = o.base
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


# ---- the real revive core under the mixin ------------------------------------------------------------------------
PLAYER, PARTNER = 0x1000, 0x2000
SCOPE = [2, 34, 0, 157, 157, 157]


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


clock = Clock()
o.time.monotonic = clock
thorn.time.monotonic = clock


class Log:
    def __init__(self, downed=False, full=False, actor=0, known=True):
        self.downed, self.full_party, self.actor, self.known = downed, full, actor, known
        self.downs = self.revives = 0
        self.path = Path("unused")

    def update(self):
        return self.known


class Peer:
    pid = 222

    def __init__(self, pos=FAR_PARTNER, error=False):
        self.pos, self.error, self.reads = pos, error, 0

    def state(self):
        self.reads += 1
        if self.error:
            raise OSError("peer unreadable")
        return dict(free=True, roomScope=SCOPE, playerPos=list(self.pos), player=PARTNER)


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
        self.rows.append((kind, f))

    def jsonl(self):
        return "".join(json.dumps(dict(t=f.get("_t", 0.0), kind=k, **{x: v for x, v in f.items() if x != "_t"})) + "\n"
                       for k, f in self.rows)

    def kinds(self):
        return [k for k, _ in self.rows]

    def row(self, kind):
        return next(f for k, f in self.rows if k == kind)


class Core(o.OrbHoldCooperativeMixin, thorn.CooperativeFight):
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


def own(react=0, pos=OWN_OUT, **kw):
    s = dict(world=2, room=34, free=True, hp=12, maxHp=20, roomScope=SCOPE, playerPos=list(pos), player=PLAYER,
             react=react, forward=[0.0, -1.0], reactObservation={}, event=0, menu=255)
    s.update(kw)
    return s


def setup(cfg=CFG, observed=None, partner=FAR_PARTNER, own_down=False, full=False, peer_down=True, known=True,
          error=False, reader=True):
    p = FakePilot(cfg, observed, reader)
    c = Core(Peer(partner, error), Log(own_down, full, known=known),
             Log(peer_down, full, actor=PARTNER if peer_down else 0, known=known))
    return p, c


def pressed(p):
    return any(REACT_KEY in f for f in p.kb.frames)


def last_move(p):
    return p.kb.frames[-1] & MOVE_KEYS if p.kb.frames else set()


def toward(state, target):
    return {base.KEYS[k].upper() for k in base.steer({**state, "pos": state["playerPos"]}, (target[0], target[2]))}


# Held off: moves toward Thorn, not the partner; the core's approach never runs.
p, c = setup()
s = own()
res = c.step(p, s)
check(res is True and last_move(p) == toward(s, ROOT) and last_move(p) != toward(s, FAR_PARTNER),
      "volley + far partner: the rescuer heads toward Thorn, not the partner")
check("coop_approach" not in p.kinds() and not pressed(p), "the core's approach does not run and no Triangle is sent")
row = p.row("coop_orb_hold")
check(row["partnerToThorn"] == 1560.0 and row["episode"] == 1 and p.orbhold_stats["episodes"] == 1,
      "the hold-off episode is logged once with the partner's distance to Thorn")
check("coop_orb_hold_sample" in p.kinds(), "a hold-off sample row carries the rescuer's position")
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

# The core's window bookkeeping runs on a held-off first tick.
p, c = setup()
start = clock.now
c.step(p, own())
check(p.kinds()[0] == "coop_revive" and p.rows[0][1]["state"] == "peer-logical-downed" and c.since == start
      and c.mode == "peer-logical-downed", "a held-off first tick writes the window marker and starts the core's 60 s bound")
clock.now += 0.3
c.step(p, own())
check(p.kinds().count("coop_revive") == 1, "the marker is written once per window")
p.observed = head(motion=207)
clock.now += 0.3
c.step(p, own())
check(p.kinds().count("coop_revive") == 1 and c.since == start, "the core keeps the same window start after a release")

p, c = setup(cfg=dict(CFG, maxWaitSec=100.0))
c.step(p, own())
clock.now += 60.5
try:
    c.step(p, own())
    raised = False
except base.Stuck as e:
    raised = "exceeded 60s" in str(e)
check(raised and p.row("coop_orb_hold_end")["reason"] == "timeout",
      "past the core's 60 s bound the tick goes back to the core, which raises its own Stuck")

# Release paths go to the core and close the episode.
p, c = setup()
c.step(p, own())
p.observed = head(motion=207)
clock.now += 0.3
res = c.step(p, own())
end = p.row("coop_orb_hold_end")
check(res is True and end["reason"] == "released" and "coop_approach" in p.kinds() and last_move(p) == toward(own(), FAR_PARTNER),
      "Thorn leaves 202: the episode ends 'released' and the core walks to the partner")
check(p.orbhold_stats["ends"] == {"released": 1}, "the summary counts the release reason")

p, c = setup()
c.step(p, own())
clock.now += 0.1
res = c.step(p, own(react=432))
check(res is True and pressed(p) and "coop_reaction" in p.kinds() and "coop_orb_hold_end" not in p.kinds(),
      "an RC during a hold-off goes to the core's press and the episode stays open")
clock.now += 0.1
c.step(p, own())
check(p.kinds().count("coop_orb_hold") == 1 and "coop_orb_hold_end" not in p.kinds(), "after the RC the same episode resumes")

p, c = setup()
c.hold_started = clock.now
c.hold_frame, c.hold_identity = 5, (PLAYER, PARTNER, tuple(SCOPE))
res = c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "a revive hold already started is never held off")

p, c = setup()
c.waiting_release = True
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "a hold awaiting release is never held off")

p, c = setup()
res = c.step(p, own(), event_reaction=True)
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "an event-reaction tick goes to the core")

for label, kw in (("own logical down", dict(own_down=True)), ("full party down", dict(full=True)),
                  ("living partner", dict(peer_down=False)), ("unknown logs", dict(known=False))):
    p, c = setup(**kw)
    c.step(p, own())
    check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, f"{label}: no hold-off and no head read")

p, c = setup(error=True)
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "unreadable partner: no hold-off, the core decides")

p, c = setup(reader=False)
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0 and "coop_approach" in p.kinds(),
      "no head reader on this pilot: the core approaches as before")

p, c = setup(cfg=None)
c.step(p, own())
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0 and "coop_approach" in p.kinds(), "switch off: unchanged core")

p, c = setup()
c.step(p, own(world=2, room=33))
check("coop_orb_hold" not in p.kinds() and p.head_reads == 0, "outside the Thorn room: no hold-off")

p, c = setup(partner=[0.0, 0.0, 300.0])
c.step(p, own(pos=[0.0, 0.0, -300.0]))
check("coop_orb_hold" not in p.kinds() and "coop_approach" in p.kinds(), "partner 1160 from Thorn: the core approaches")

# The 30 s cap, and a fresh cap per revive window.
p, c = setup()
for ticks in range(1, 200):
    c.step(p, own())
    if "coop_orb_hold_end" in p.kinds():
        break
    clock.now += 0.25
check(p.row("coop_orb_hold_end")["reason"] == "cap" and abs(c.orbhold_waited - 30.0) < 1e-9 and ticks == 122
      and "coop_approach" in p.kinds(), "after 30 s of holding off in one window the approach resumes ('cap')")
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


# ---- the report counts hits taken while held off ------------------------------------------------------------------
class TimedPilot(FakePilot):
    def write(self, kind, **f):
        self.rows.append((kind, dict(f, _t=clock.now)))


p = TimedPilot()
c = Core(Peer(), Log(), Log(True, actor=PARTNER))
for frame in (100, 130, 160):                      # three held-off ticks, own-game frames 100..160
    c.step(p, own(reactObservation={"frame": frame}))
    clock.now += 0.5
p.observed = head(motion=207)
c.step(p, own(reactObservation={"frame": 190}))    # released: the core approaches (coop_approach at 190)
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
check(old[0]["orbHits"] == 0, "control: rescue_report's coop_approach-only span would have missed both orb hits")


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

mro = [k.__name__ for k in o.CooperativeFight.__mro__]
check(mro[:3] == ["CooperativeFight", "OrbHoldCooperativeMixin", "CooperativeFight"]
      and o.CooperativeFight.__mro__[2] is thorn_creeper.CooperativeFight, "the hold-off sits directly over thorn_creeper")
check(o.Pilot.__mro__[1] is o.OrbHoldPilotMixin and o.Pilot.__mro__[2] is thorn_creeper.Pilot, "Pilot: orb hold over thorn_creeper")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
