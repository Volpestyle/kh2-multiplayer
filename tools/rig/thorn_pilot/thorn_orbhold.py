"""VUH-2617 lead #302: a rescuer waits out Thorn's orb volley near Thorn, over thorn_creeper (Creeper-first kept).

Thorn's motion 202 winds up for ~340 frames, then loops a firing segment for up to ~30 s; every orb hit (1041/1014)
in zr98-114 landed in that loop. Orb hits per exposed second were ~2.2x higher 1250+ from Thorn's root (0.36/s) than
inside it (0.16/s), and lost revive windows were the ones spent in the loop with the downed seat far from Thorn
(median firing share 1.0, partner 1584 from Thorn; recovered: 0.18, 1114). A revive hold itself needs ~1.3 s, and the
gap between occurrences is ~16 s with no orbs (.local/review/vuh2617-orb-hold). Opt-in per fight step:

  "rescueOrbHold": {"far": 1250, "hold": 1000, "finish": 300, "maxWaitSec": 30}

While this seat's partner is logically downed (and this seat is not), Thorn is in motion 202, the partner lies `far`
or more from Thorn's root and this seat is more than `finish` from the partner, the rescuer does not walk to the
partner: it moves toward Thorn until it is within `hold` of the root, then stands. The approach resumes when Thorn
leaves 202, when the partner is no longer far, or after `maxWaitSec` of waiting in this revive window. The core's 60 s
revive bound and window marker run from the window's first tick either way. RCs, a hold already started, a hold
awaiting release, event reactions and every tick outside a rescue go to the revive core unchanged. Thorn is read through the existing head observation (no new reads); an unknown head never holds back.
Input-only, no native writes.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_creeper as child

base, previous = child.base, child.previous
VOLLEY = 202
HEAD_TTL = 0.25  # s: one head observation serves this many seconds of cooperative ticks


def flat(a, b):
    return math.hypot(b[0] - a[0], b[2] - a[2])


def orb_hold(cfg, head, own_pos, peer_pos, waited):
    """The hold-off for this tick, or None to let the revive core approach: Thorn in its volley, the downed partner far
    from Thorn, this seat not about to finish, and the window's wait under the cap."""
    if cfg is None or not head or not head.get('known') or head.get('motion') != VOLLEY:
        return None
    root = head.get('rootWorldPos')
    if not root or not own_pos or not peer_pos or not all(math.isfinite(v) for v in (*root, *own_pos, *peer_pos)):
        return None
    if waited >= cfg['maxWaitSec']:
        return None
    partner, own, gap = flat(root, peer_pos), flat(root, own_pos), flat(own_pos, peer_pos)
    if partner < cfg['far'] or gap <= cfg['finish']:
        return None
    return dict(partnerToThorn=round(partner, 1), ownToThorn=round(own, 1), toPartner=round(gap, 1),
                root=[round(v, 1) for v in root], inside=own <= cfg['hold'])


class OrbHoldPilotMixin:
    orbhold_cfg = None

    def fight(self, step, deadline):
        cfg = step.get('rescueOrbHold') if step.get('eventReactions') else None
        if cfg is True:
            cfg = {}
        self.orbhold_cfg = None if not isinstance(cfg, dict) else dict(
            far=float(cfg.get('far', 1250)), hold=float(cfg.get('hold', 1000)),
            finish=float(cfg.get('finish', 300)), maxWaitSec=float(cfg.get('maxWaitSec', 30)))
        self.orbhold_stats = dict(episodes=0, seconds=0.0, ends={})
        try:
            return super().fight(step, deadline)
        finally:
            if self.orbhold_cfg is not None:
                self.write('rescue_orb_hold_summary', **{**self.orbhold_stats,
                                                         'seconds': round(self.orbhold_stats['seconds'], 2)})
            self.orbhold_cfg = None


class OrbHoldCooperativeMixin:
    orbhold_since = None       # monotonic start of the open hold-off episode
    orbhold_last = None        # monotonic of the last held-off tick
    orbhold_waited = 0.0       # seconds held off in this revive window
    orbhold_sample_at = 0.0
    orbhold_head = (None, -1.0)

    def orbhold_end(self, pilot, reason):
        if self.orbhold_since is None:
            return
        seconds = time.monotonic() - self.orbhold_since
        stats = pilot.orbhold_stats
        stats['seconds'] += seconds
        stats['ends'][reason] = stats['ends'].get(reason, 0) + 1
        pilot.write('coop_orb_hold_end', reason=reason, seconds=round(seconds, 2),
                    waited=round(self.orbhold_waited, 2), peerPid=self.peer.pid)
        self.orbhold_since = self.orbhold_last = None

    def orbhold_observe(self, pilot, own):
        head, at = self.orbhold_head
        now = time.monotonic()
        if now - at >= HEAD_TTL:
            head = pilot.observe_head(own) if getattr(pilot, 'head_reader', None) is not None else None
            self.orbhold_head = (head, now)
        return head

    def step(self, pilot, own, event_reaction=False):
        cfg = getattr(pilot, 'orbhold_cfg', None)
        if cfg is None or (own.get('world'), own.get('room')) != (2, 34):
            return super().step(pilot, own, event_reaction)
        if own.get('react') and not event_reaction:
            return super().step(pilot, own, event_reaction)  # the core's RC press; the episode stays open
        hold = None
        reason = 'core'
        if not event_reaction and own.get('free') and self.hold_started is None and not self.waiting_release \
                and self.own_log.update() and self.peer_log.update():
            if not self.peer_log.downed:
                self.orbhold_waited = 0.0  # a new revive window gets a fresh cap
                reason = 'peer-recovered'
            elif not (self.own_log.downed or self.own_log.full_party or self.peer_log.full_party):
                try:
                    peer = self.peer.state()
                except (OSError, ValueError):
                    peer = None
                head = self.orbhold_observe(pilot, own) if peer is not None else None
                hold = orb_hold(cfg, head, own.get('playerPos'), (peer or {}).get('playerPos'), self.orbhold_waited)
                reason = 'released' if hold is None else reason
                if hold is None and self.orbhold_waited >= cfg['maxWaitSec']:
                    reason = 'cap'
        now = time.monotonic()
        if hold is not None:
            # The core's own window bookkeeping, which it would have done this tick: its 60 s bound starts at the
            # window's first tick whoever handles it, and the window marker is written before any hold-off row.
            if self.since is None:
                self.since = now
            if now - self.since > self.TIMEOUT:
                hold, reason = None, 'timeout'  # the core raises its own bound
            elif self.mode != 'peer-logical-downed':
                pilot.write('coop_revive', state='peer-logical-downed', peerPid=self.peer.pid)
                self.mode = 'peer-logical-downed'
        if hold is None:
            self.orbhold_end(pilot, reason)
            return super().step(pilot, own, event_reaction)
        if self.orbhold_last is not None:
            self.orbhold_waited += now - self.orbhold_last
        self.orbhold_last = now
        if self.orbhold_since is None:
            self.orbhold_since = now
            pilot.orbhold_stats['episodes'] += 1
            pilot.write('coop_orb_hold', peerPid=self.peer.pid, waited=round(self.orbhold_waited, 2),
                        hp=own.get('hp'), episode=pilot.orbhold_stats['episodes'], **hold)
        if now - self.orbhold_sample_at >= 0.5:
            pilot.write('coop_orb_hold_sample', own=list(own['playerPos']), observation=own.get('reactObservation'),
                        **{k: hold[k] for k in ('partnerToThorn', 'ownToThorn', 'toPartner', 'inside')})
            self.orbhold_sample_at = now
        root = hold['root']
        direction = set() if hold['inside'] else base.steer({**own, 'pos': own['playerPos']}, (root[0], root[2]))
        pilot.kb.hold(direction)
        return True


class Pilot(OrbHoldPilotMixin, child.Pilot):
    pass


class CooperativeFight(OrbHoldCooperativeMixin, child.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, previous.Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
