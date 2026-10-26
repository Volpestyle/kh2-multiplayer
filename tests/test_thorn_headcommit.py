"""Offline controls for tools/rig/thorn_pilot/thorn_headcommit.py (VUH-2617 lead #355, commit to the grounded head).

The production Pilot runs its real head turn (thorn_next_policy's lock-retry wrapper over thorn_head.head_turn and
next_policy.head_action) and its real observe_head chain. Only the head read, the downed logs, the clock, the pad and the
pilot's write are fakes. The grounded head sits ~1700 from Thorn's root, as in zr98-130.
Run: python -B tests/test_thorn_headcommit.py
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "rig" / "thorn_pilot"))
import thorn_head  # noqa: E402
import thorn_headcommit as hc  # noqa: E402
import thorn_tether  # noqa: E402

failures = 0
ROOT = (0.0, 0.0, -860.0)
OWN = [0.0, 0.0, -600.0]                      # near the root, as the friend stood in zr129
GROUND = [0.0, -148.0, 840.0]                 # grounded head, 1700 from the root, 1440 from OWN
RAISED = [0.0, -961.0, 840.0]


def check(ok, label):
    global failures
    print(("PASS" if ok else "FAIL") + ": " + label)
    failures += 0 if ok else 1


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now


clock = Clock()
for module in (hc, thorn_head):
    module.time.monotonic = clock
thorn_head.time.sleep = lambda seconds: None


class Log:
    def __init__(self, downed=False):
        self.downed, self.full_party = downed, False

    def update(self):
        return True


class Coop:
    def __init__(self):
        self.own_log, self.peer_log = Log(), Log()


class Kb:
    def __init__(self):
        self.holds, self.taps = [], []

    def hold(self, keys):
        self.holds.append(set(keys))

    def tap(self, action, ms=90):
        self.taps.append(action)
        return True


class Reader:
    def __init__(self, p):
        self.p = p

    def head(self, state):
        return self.p.head


def head(motion=202, pos=GROUND, own=OWN, known=True):
    return dict(known=known, collisionEnabled=True, nativeVictimEligible=True, motion=motion, headPos=list(pos),
                targetPos=list(pos), headRadius=90, playerPos=list(own), rootWorldPos=list(ROOT), lockedHead=True)


def pilot(commit=True, h=None):
    p = object.__new__(hc.Pilot)
    p.rows = []
    p.write = lambda kind, **f: p.rows.append((kind, f))
    p.cooperative, p.kb = Coop(), Kb()
    p.head = h or head()
    p.head_reader = Reader(p)
    p.head_active, p.next_options = True, {"head": True}
    p.lock_tries, p.lock_at, p.dazed_jump, p.head_mode = 0, 0.0, False, False
    p.lock_pending_observation = None
    p.approach_since = None
    p.head_commit, p.head_commit_202 = commit, False
    p.head_commit_stats = dict(occurrences=0, refreshes=0, exits=0)
    return p


def state(hp=20, react=0, own=OWN):
    return dict(free=True, react=react, hp=hp, maxHp=20, playerPos=list(own), forward=[0.0, 1.0])


def ticks(p, times, s=None):
    """head_turn results at clock offsets (s) from 1000."""
    out = []
    for t in times:
        clock.now = 1000.0 + t
        out.append(p.head_turn(s or state()))
    return out


# ---- pure rule ---------------------------------------------------------------------------------------------------
check(hc.grounded_volley(head()), "202 with the head at y -148: grounded")
check(not hc.grounded_volley(head(pos=RAISED)), "202 with the head at y -961: raised")
check(not hc.grounded_volley(head(pos=[0.0, -300.0, 840.0])) and hc.grounded_volley(head(pos=[0.0, -299.9, 840.0])),
      "the boundary: y -300 is raised, -299.9 grounded")
check(not hc.grounded_volley(head(motion=209)), "outside 202 (209): not a grounded volley")
check(not hc.grounded_volley(head(known=False)) and not hc.grounded_volley(None), "unknown or missing head: no")
check(not hc.grounded_volley(head(pos=[float("nan"), -148.0, 840.0])), "non-finite head position: no")

# ---- the real head turn ------------------------------------------------------------------------------------------
T = [0.0, 1.0, 2.0, 3.1, 4.2, 6.0, 9.0]
p = pilot()
res = ticks(p, T)
check(all(res) and all(h for h in p.kb.holds), "202, grounded head 1440 away: the approach keeps steering past 3 s (9 s here)")
check(p.head_commit_stats["occurrences"] == 1 and p.head_commit_stats["refreshes"] == len(T) - 1
      and [k for k, _ in p.rows].count("head_commit_202") == 1, "one commit row per 202 occurrence; the clock refreshed each turn")
q = pilot(commit=False)
check(ticks(q, T) == [True, True, True, False, False, False, False], "control, switch off: capped after 3 s as before")
q = pilot(h=head(pos=RAISED))
check(ticks(q, T) == [True, True, True, False, False, False, False], "202 with the head raised: capped after 3 s")
q = pilot(h=head(motion=209))
check(ticks(q, T) == [True, True, True, False, False, False, False], "outside 202 (dazed 209, out of reach): capped after 3 s")

# The head rising within the same 202: the cap counts from the last grounded refresh, and nothing is reset.
p = pilot()
ticks(p, [0.0, 1.0, 2.0])                                # grounded: clock refreshed at 1002
p.head = head(pos=RAISED)
check(ticks(p, [2.5, 4.0, 5.1, 6.0]) == [True, True, False, False] and p.head_commit_stats["exits"] == 0,
      "head raised mid-202: capped 3 s after the last grounded refresh; still the same occurrence (no exit)")

# Reset when Thorn leaves 202: a 209 approach right after the commitment gets its own 3 s.
p = pilot()
ticks(p, [0.0, 1.0, 2.0])                                # committed: clock refreshed at 1002
p.head = head(motion=209)
res = ticks(p, [2.5, 3.6, 5.4, 5.6])                      # exit at 1002.5; a fresh 3 s runs to 1005.5
check(res == [True, True, True, False] and p.head_commit_stats["exits"] == 1 and p.approach_since is not None,
      "leaving 202 clears the clock: the next approach is capped 3 s from the exit (not from the last refresh)")
q = pilot()
ticks(q, [0.0, 1.0, 2.0])
q.head_commit_202 = False                                 # what a missing reset would leave
q.head = head(motion=209)
check(ticks(q, [2.5, 3.6, 5.4]) == [True, True, False], "control: without the reset the old clock would cap at 1005.0")

# Head reads outside the head turn (Creeper-first, the hold-off, the tether) never touch the clock.
p = pilot()
ticks(p, [0.0])
p.approach_since = 990.0
clock.now = 1005.0
p.observe_head(state())
check(p.approach_since == 990.0 and p.head_commit_stats["refreshes"] == 0, "a head read outside head_turn leaves the clock alone")

# head_action's own yields still win: an RC, low HP or a downed partner end the approach and clear the clock.
for label, kw, coop in (("RC offered", dict(react=432), None), ("own HP under 40%", dict(hp=7), None),
                        ("partner logically down", dict(), "peer")):
    p = pilot()
    ticks(p, [0.0, 1.0])
    if coop:
        p.cooperative.peer_log.downed = True
    clock.now = 1004.0
    check(p.head_turn(state(**kw)) is False and p.approach_since is None, f"{label}: the head turn yields as before")

# In reach: the attack path as before (no approach, a swing).
p = pilot(h=head(own=[0.0, 0.0, 700.0]))
res = ticks(p, [0.0], state(own=[0.0, 0.0, 700.0]))
check(res == [True] and "attack" in p.kb.taps and p.approach_since is None, "head in reach: the swing path is unchanged")

# ---- switch semantics, summary, MRO ------------------------------------------------------------------------------
class Lower:
    def fight(self, step, deadline):
        self.seen = self.head_commit
        return "fought"


class P(hc.HeadCommitPilotMixin, Lower):
    def __init__(self):
        self.rows = []

    def write(self, kind, **f):
        self.rows.append((kind, f))


pp = P()
check(pp.fight({"eventReactions": True, "headCommit202": True}, 0) == "fought" and pp.seen is True, "true turns it on")
check(bool(pp.rows) and pp.rows[-1][0] == "head_commit_summary" and pp.head_commit is False, "one summary row; off after")
pp = P()
pp.fight({"eventReactions": True}, 0)
check(pp.seen is False and not pp.rows, "absent key: off, no summary")
pp = P()
pp.fight({"headCommit202": True}, 0)
check(pp.seen is False, "the key outside the Thorn fight step is ignored")
mro = hc.Pilot.__mro__
check(mro[1] is hc.HeadCommitPilotMixin and mro[2] is thorn_tether.Pilot
      and [c for c in mro if "head_turn" in c.__dict__][0] is hc.HeadCommitPilotMixin
      and [c for c in mro if "observe_head" in c.__dict__][0] is hc.HeadCommitPilotMixin,
      "Pilot: head commit outermost over thorn_tether's, wrapping head_turn and observe_head")

print(f"failures={failures}")
sys.exit(1 if failures else 0)
