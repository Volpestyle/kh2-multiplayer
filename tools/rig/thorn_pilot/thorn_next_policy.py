"""UNRUN next-series child, leads #237/#238. Frozen thorn_next stays unchanged.

Only input policy: bounded timed lock retries and a bounded revive-approach escape.
No native calls/writes, wire, range, healing policy or revive deadline change.
Place beside the pinned thorn_next modules for a future independently reviewed run.
"""
import math
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_next as previous

base = previous.base
thorn = previous.thorn
Game, k32, psapi, Pad = previous.Game, previous.k32, previous.psapi, previous.Pad


class LockRetryMixin:
    lock_pending_observation = None

    def fight(self, step, deadline):
        self.lock_pending_observation = None
        try:
            return super().fight(step, deadline)
        finally:
            self.lock_pending_observation = None

    def head_turn(self, state):
        # Four attempts per burst, then at least 15 seconds since the last one.
        # The original one-second cadence and all head/RC/HP/revive guards remain.
        if self.lock_tries >= 4 and time.monotonic()-self.lock_at >= 15.0:
            self.write('thorn_head_lock_reset', reason='15s-since-last-attempt', attempts=self.lock_tries)
            self.lock_tries = 0
        return super().head_turn(state)

    def write(self, kind, **fields):
        if kind == 'thorn_head_lock':
            h = fields['head']
            self.lock_pending_observation = dict(at=time.monotonic(), attempt=fields['attempt'],
                beforeMode=h.get('lockMode'), beforeHandle=h.get('lockHandle'),
                identity=(h.get('lockOwner'), h.get('actor'), h.get('handle')))
        return super().write(kind, **fields)

    def observe_head(self, state):
        head = super().observe_head(state)  # reuse the existing read; no new RPM read
        pending = self.lock_pending_observation
        if pending is not None:
            self.lock_pending_observation = None
            self.write('thorn_head_lock_observed', **pending,
                delaySec=round(time.monotonic()-pending['at'],3), head=head,
                sameIdentity=pending['identity'] == (head.get('lockOwner'),head.get('actor'),head.get('handle')),
                note='next-head-turn-sample; not a native input acceptance receipt')
        return head


class ApproachEscapeMixin:
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.escape_key = self.escape_anchor = self.escape_anchor_at = None
        self.escape_tries, self.escape_at = 0, float('-inf')

    def escape_facts(self, pilot, event_reaction=False):
        if event_reaction or self.hold_started is not None or self.waiting_release:
            return None
        if not (self.own_log.update() and self.peer_log.update()) or self.own_log.downed \
                or not self.peer_log.downed or self.own_log.full_party or self.peer_log.full_party:
            return None
        try:
            own, peer = pilot.state(), self.peer.state()
        except (OSError, ValueError):
            return None
        if not (own['free'] and own['inField'] and own['menu'] == 255) or own['event'] or own['react'] \
                or own['hp'] is None or own['hp'] <= 0 or not peer['free'] \
                or (own['world'],own['room']) != (2,34) or own['roomScope'] != peer['roomScope'] \
                or not own.get('player') or self.peer_log.actor != peer.get('player'):
            return None
        a, b = own.get('playerPos'), peer.get('playerPos')
        if not a or not b or not all(math.isfinite(v) for v in (*a,*b)):
            return None
        distance = math.dist(a,b)
        if distance <= 150 or math.hypot(b[0]-a[0],b[2]-a[2]) <= 80:
            return None  # do not disrupt a possible native revive or an airborne-only gap
        key = (tuple(own['roomScope']),own['player'],peer['player'],self.peer_log.downs)
        return key, tuple(a), distance

    def step(self, pilot, own, event_reaction=False):
        consumed = super().step(pilot, own, event_reaction)
        if not consumed or not self.peer_log.downed:
            self.escape_anchor = self.escape_anchor_at = None
            return consumed
        facts = self.escape_facts(pilot,event_reaction)
        if facts is None or facts[0][0] != tuple(own['roomScope']) or facts[0][1] != own['player']:
            self.escape_anchor = self.escape_anchor_at = None
            return consumed  # retain quota through unknown/RC/hold gaps
        key, point, distance = facts
        now = time.monotonic()
        if key != self.escape_key:
            self.escape_key, self.escape_tries, self.escape_at = key, 0, float('-inf')
            self.escape_anchor = self.escape_anchor_at = None
        if self.escape_anchor is None or math.dist(point,self.escape_anchor) > 20 \
                or now < self.escape_anchor_at:
            self.escape_anchor, self.escape_anchor_at = point, now
            return consumed
        if now-self.escape_anchor_at < 2 or self.escape_tries >= 2 or now-self.escape_at < 5:
            return consumed
        if self.since is None or now-self.since >= self.TIMEOUT-0.4:
            return consumed  # never buy another interval on the original deadline
        self.escape_tries += 1  # consume before input; a partial escape is not retried
        self.escape_at = now
        self.escape_anchor = self.escape_anchor_at = None
        side = 'left' if self.escape_tries == 1 else 'right'
        pilot.write('coop_stuck_escape', stage='begin', attempt=self.escape_tries,
                    key=key, own=point, distance3D=distance, side=side)
        try:
            pilot.kb.hold(set())
            pilot.kb.tap('jump',80)
            after = self.escape_facts(pilot,event_reaction)
            if after is None or after[0] != key:
                pilot.write('coop_stuck_escape',stage='stop',reason='after-jump-context-or-range')
                return True
            pilot.kb.hold({side})
            time.sleep(0.2)
        finally:
            pilot.kb.hold(set())
        pilot.write('coop_stuck_escape',stage='end',attempt=self.escape_tries)
        return True


class Pilot(LockRetryMixin, previous.Pilot):
    pass


class CooperativeFight(ApproachEscapeMixin, previous.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = previous.thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    import sys
    sys.exit(main())
