"""VUH-2617 lead #355: commit to Thorn's grounded head during the volley, over thorn_tether (the full stack kept).

Thorn's second dazed opening starts only at <= ~175 HP (155-175 in all 7 runs that got one; all 7 then killed or nearly
killed), and runs stall at 199-260 in the 202 -> 222 -> 152/153 -> 0 loop. During 202 Thorn's head lies grounded ~1700
from its root (87% of samples), and that is where the seats' hits land: zr130's friend at the head (9 of 11 swings in
reach) removed 43 HP in 72 s, zr129's at ~1448 (0 of 52) 5 HP. thorn_head.head_turn caps a head approach at 3 s and
keeps declining after that until the head is in reach or the turn yields; 54 of 198 friend approaches to the grounded
head stopped there ~700 short, and the base loop then swings near the root. Opt-in per fight step:

  "headCommit202": true

While Thorn is in motion 202 with its head grounded (head height above GROUND_Y, i.e. headPos y > -300), the head
approach is not capped: each head turn's own head read refreshes the approach clock, so the 3 s check never runs out.
With the head raised in 202, or outside 202, the cap is as before. When Thorn leaves 202 after such a commitment, the
approach clock is cleared, so the next approach starts its own 3 s. Everything else about the head turn is unchanged
(head_action still yields to RCs, low HP, downed seats and unknown heads), and every higher layer still takes the tick
first: RCs, Creeper-first, survival heals/low-defence/reserve, the revive core, the tether. Input-only, no native writes.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_tether as child  # noqa: E402

base, previous = child.base, child.previous
VOLLEY = 202
GROUND_Y = -300.0  # head heights above this (KH2 y grows downward) count as grounded


def grounded_volley(head):
    """Thorn in its volley with its head on the ground."""
    if not head or not head.get('known') or head.get('motion') != VOLLEY:
        return False
    pos = head.get('headPos')
    return bool(pos) and len(pos) == 3 and all(math.isfinite(v) for v in pos) and pos[1] > GROUND_Y


class HeadCommitPilotMixin:
    head_commit = False
    head_commit_turn = False     # inside head_turn: its own head read decides the cap
    head_commit_202 = False      # this 202 occurrence has refreshed the approach clock

    def fight(self, step, deadline):
        self.head_commit = bool(step.get('headCommit202')) and bool(step.get('eventReactions'))
        self.head_commit_202 = False
        self.head_commit_stats = dict(occurrences=0, refreshes=0, exits=0)
        try:
            return super().fight(step, deadline)
        finally:
            if self.head_commit:
                self.write('head_commit_summary', **self.head_commit_stats)
            self.head_commit = False

    def observe_head(self, state):
        head = super().observe_head(state)
        if not self.head_commit_turn:
            return head
        if grounded_volley(head):
            if getattr(self, 'approach_since', None) is not None:
                self.approach_since = time.monotonic()  # the 3 s cap never runs out on a grounded head in 202
                self.head_commit_stats['refreshes'] += 1
            if not self.head_commit_202:
                self.head_commit_202 = True
                self.head_commit_stats['occurrences'] += 1
                pos, me = head['headPos'], head.get('playerPos') or state.get('playerPos')
                self.write('head_commit_202', headPos=[round(v, 1) for v in pos], hp=state.get('hp'),
                           gap=round(math.hypot(pos[0] - me[0], pos[2] - me[2]), 1) if me else None)
        elif self.head_commit_202 and head.get('motion') != VOLLEY:
            self.head_commit_202 = False
            self.approach_since = None  # Thorn left 202: the next approach starts its own 3 s
            self.head_commit_stats['exits'] += 1
        return head

    def head_turn(self, s):
        self.head_commit_turn = self.head_commit
        try:
            return super().head_turn(s)
        finally:
            self.head_commit_turn = False


class Pilot(HeadCommitPilotMixin, child.Pilot):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, child.child.child.Pad, Pilot
    base.CooperativeFight = child.CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
