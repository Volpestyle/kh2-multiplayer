"""VUH-2617 lead #344: a partner tether during Thorn's orb volley, over thorn_potion (r5, hold-off, Potion kept).

In zr98-123, revive windows that started more than 1300 from the downed partner lost 54% (13 of 24); 0-1300 lost
17-24%. In 22 of the 23 far starts, the seats were already more than 900 apart 3 s before the downing, so the gap came
before the down, not from it (.local/review/vuh2617-tether-estimate on VUH-2617). Opt-in per fight step:

  "partnerTether": {"radius": 1300}

While Thorn is in its volley (motion 202) and no revive window is open, both seats up, the seat farther from Thorn's
root steers toward its partner whenever they are more than `radius` apart horizontally. It yields to everything else:
- TetherCore is listed after the hold-off's core, so C3 puts it above OrbHoldCore, ProtectCore and the revive core
  but below thorn_head: it runs only after the revive core has declined the tick (no downed partner), and before the
  head-targeting turn, which it pre-empts;
- every layer above it takes the tick first: Creeper-first's turn, survival's opening/grace heals and reserve motion,
  the RC gate and the approach revision;
- it never acts with an RC offered, outside ordinary field play, in an event-reaction tick, with either seat down or
  a full-party wipe, with a revive window open, on an unknown head or partner read, or during an active Creeper
  engagement (no tethering on a Creeper objective, lead #344); the logs are read again just before any input, and an
  unreadable log or a downing logged during the partner/head reads stops it (r2);
- ending an episode releases only the keys the tether itself pressed, never a lower core's input from the same tick
  (r2).
The steer is pilot.py's own, taken at import (the hold-off swaps it only inside its core tick). Input-only, no native
writes.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_orbhold  # noqa: E402
import thorn_potion as child  # noqa: E402

base, previous = child.base, child.previous
STEER = base.steer
VOLLEY = thorn_orbhold.VOLLEY


def flat(a, b):
    return math.hypot(b[0] - a[0], b[2] - a[2])


def tether(cfg, head, own_pos, peer_pos, own_pid, peer_pid):
    """The tether for this tick, or None: Thorn in its volley, the seats more than `radius` apart and this seat the
    one farther from Thorn's root (a tie goes to the lower pid)."""
    if cfg is None or not head or not head.get('known') or head.get('motion') != VOLLEY:
        return None
    root = head.get('rootWorldPos')
    if not root or not own_pos or not peer_pos or not all(math.isfinite(v) for v in (*root, *own_pos, *peer_pos)):
        return None
    gap, own, peer = flat(own_pos, peer_pos), flat(root, own_pos), flat(root, peer_pos)
    if gap <= cfg['radius'] or own < peer or (own == peer and own_pid > peer_pid):
        return None
    return dict(gap=round(gap, 1), ownToThorn=round(own, 1), peerToThorn=round(peer, 1))


class TetherPilotMixin:
    tether_cfg = None

    def fight(self, step, deadline):
        cfg = step.get('partnerTether') if step.get('eventReactions') else None
        if cfg is True:
            cfg = {}
        self.tether_cfg = None if not isinstance(cfg, dict) else dict(radius=float(cfg.get('radius', 1300)))
        self.tether_stats = dict(episodes=0, seconds=0.0, ends={})
        try:
            return super().fight(step, deadline)
        finally:
            if self.tether_cfg is not None:
                self.write('partner_tether_summary', **{**self.tether_stats,
                                                        'seconds': round(self.tether_stats['seconds'], 2)})
            self.tether_cfg = None


class TetherCore(thorn_orbhold.OrbHoldCore):
    """Listed as the last base, after thorn_protect's CooperativeFight: C3 places it directly above OrbHoldCore."""
    tether_since = None
    tether_held = None  # the pad's held set right after the tether's own last press

    def tether_end(self, pilot, reason, release=True):
        if self.tether_since is None:
            return
        seconds = time.monotonic() - self.tether_since
        stats = pilot.tether_stats
        stats['seconds'] += seconds
        stats['ends'][reason] = stats['ends'].get(reason, 0) + 1
        pilot.write('coop_tether_end', reason=reason, seconds=round(seconds, 2), peerPid=self.peer.pid)
        self.tether_since = None
        # Release only what the tether itself pressed (Sage, on r1). A core that consumed this tick owns its input, even
        # when it re-held the very keys the tether last pressed (the pad then writes nothing), so that end never
        # releases; otherwise only a pad still holding exactly the tether's own press is released.
        if release and self.tether_held is not None and set(pilot.kb.held) == self.tether_held:
            pilot.kb.hold(set())
        self.tether_held = None

    def tether_check(self, pilot, own, event_reaction):
        """(this tick's tether, None) or (None, why it does not apply)."""
        cfg = getattr(pilot, 'tether_cfg', None)
        if cfg is None or event_reaction or (own.get('world'), own.get('room')) != (2, 34):
            return None, 'off'
        if own.get('react') or not own.get('free') or not own.get('inField') or own.get('menu') != 255 \
                or own.get('event') or not own.get('hp') or not own.get('playerPos'):
            return None, 'yield'
        if self.since is not None or self.own_log.downed or self.peer_log.downed \
                or self.own_log.full_party or self.peer_log.full_party:
            return None, 'yield'
        if getattr(pilot, 'creeper_engage', None) is not None:
            return None, 'creeper'
        try:
            peer = self.peer.state()
        except (OSError, ValueError):
            return None, 'yield'
        if not peer.get('playerPos') or peer.get('roomScope') != own.get('roomScope') or not peer.get('hp'):
            return None, 'yield'
        head = self.orbhold_observe(pilot, own)
        t = tether(cfg, head, own['playerPos'], peer['playerPos'], pilot.game.pid, self.peer.pid)
        if t is None:
            return None, 'released'
        # The partner and head reads take time: a downing logged meanwhile, or a log turning unreadable, must stop the
        # input. Validate the logical state fresh, immediately before pressing, and fail closed (Sage, on r1).
        if not self.own_log.update() or not self.peer_log.update() or self.since is not None \
                or self.own_log.downed or self.peer_log.downed or self.own_log.full_party or self.peer_log.full_party:
            return None, 'yield'
        return dict(t, peer=list(peer['playerPos'])), None

    def step(self, pilot, own, event_reaction=False):
        result = super().step(pilot, own, event_reaction)
        if result or getattr(pilot, 'tether_cfg', None) is None:
            if result:
                self.tether_end(pilot, 'core', release=False)
            return result
        t, why = self.tether_check(pilot, own, event_reaction)
        if t is None:
            self.tether_end(pilot, why)
            return result
        if self.tether_since is None:
            self.tether_since = time.monotonic()
            pilot.tether_stats['episodes'] += 1
            pilot.write('coop_tether', peerPid=self.peer.pid, hp=own.get('hp'), episode=pilot.tether_stats['episodes'],
                        **{k: v for k, v in t.items() if k != 'peer'})
        point = own['playerPos']
        pilot.kb.hold(STEER({**own, 'pos': point}, (t['peer'][0], t['peer'][2])))
        self.tether_held = set(pilot.kb.held)
        return True


class Pilot(TetherPilotMixin, child.Pilot):
    pass


class CooperativeFight(child.child.CooperativeFight, TetherCore):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, child.child.Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
