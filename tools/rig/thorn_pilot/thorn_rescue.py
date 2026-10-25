"""VUH-2617 lead #291: no throwback Reversals near a downed partner, over thorn_creeper (Creeper-first kept).

The revive core (thorn.CooperativeFight.step) presses any offered RC while it walks to a downed partner. In zr98-110 a
completed 432/426 from within 600 of the downed seat threw the rescuer back a median +295..+498, and rescues with three
or more such throwbacks recovered 1 of 5 (median 55.6 s) against 9 of 9 with one or two
(.local/review/vuh2617-orb-revive-breakdown). Opt-in per fight step:

  "rescueNoReversal": {"commands": [432, 426], "within": 600}

While this seat's partner is logically downed (and this seat is not), one of `commands` is on screen, and the partner
is within `within` horizontally, the rescue keeps its direction but sends no Triangle: the pad drops `react` for that
tick. The revive core is unchanged (it still ends a hold for the RC and walks); the hold starts as before once the
prompt is visible and no RC is. Every other RC, every tick outside a rescue, and the friend/host routes without the
key behave as before. Input-only, no native writes.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_creeper as child

base, previous = child.base, child.previous


def suppress(cfg, react, own_down, peer_down, full_party, own_pos, peer_pos):
    """True when this tick's RC must not be pressed: a listed Reversal, offered to a rescuer near its downed partner."""
    if cfg is None or not react or int(react) not in cfg['commands']:
        return False
    if own_down or not peer_down or full_party or not own_pos or not peer_pos:
        return False
    return math.hypot(peer_pos[0] - own_pos[0], peer_pos[2] - own_pos[2]) <= cfg['within']


class RescuePadMixin:
    rescue_mask = None  # (command, horizontal) for the current cooperative tick, else None

    def hold(self, actions):
        if self.rescue_mask is not None and 'react' in actions:
            self.rescue_stripped = getattr(self, 'rescue_stripped', 0) + 1
            actions = set(actions) - {'react'}
        return super().hold(actions)

    def tap(self, action, ms=90):
        if self.rescue_mask is not None and action == 'react':
            self.rescue_stripped = getattr(self, 'rescue_stripped', 0) + 1
            return False
        return super().tap(action, ms)


class RescuePilotMixin:
    rescue_cfg = None

    def fight(self, step, deadline):
        cfg = step.get('rescueNoReversal') if step.get('eventReactions') else None
        if cfg is True:
            cfg = {}
        self.rescue_cfg = None if not isinstance(cfg, dict) else dict(
            commands=frozenset(int(c) for c in cfg.get('commands', (432, 426))), within=float(cfg.get('within', 600)))
        self.rescue_stats = dict(episodes=0, ticks=0, byCommand={})
        try:
            return super().fight(step, deadline)
        finally:
            if self.rescue_cfg is not None:
                self.write('rescue_no_reversal_summary', stripped=getattr(self.kb, 'rescue_stripped', 0),
                           **self.rescue_stats)
            self.rescue_cfg = None
            self.kb.rescue_mask = None


class RescueCooperativeMixin:
    rescue_last = (None, 0.0)  # (command, monotonic) of the last suppressed tick

    def step(self, pilot, own, event_reaction=False):
        cfg = getattr(pilot, 'rescue_cfg', None)
        mask = None
        if cfg is not None and not event_reaction and own.get('react') and int(own['react']) in cfg['commands'] \
                and (own.get('world'), own.get('room')) == (2, 34) and self.own_log.update() and self.peer_log.update():
            try:
                peer = self.peer.state() if self.peer_log.downed else None
            except (OSError, ValueError):
                peer = None
            if peer is not None and suppress(cfg, own['react'], self.own_log.downed, self.peer_log.downed,
                                             self.own_log.full_party or self.peer_log.full_party,
                                             own.get('playerPos'), peer.get('playerPos')):
                a, b = own['playerPos'], peer['playerPos']
                mask = (int(own['react']), round(math.hypot(b[0] - a[0], b[2] - a[2]), 1))
        if mask is not None:
            now = time.monotonic()
            stats = pilot.rescue_stats
            stats['ticks'] += 1
            if mask[0] != self.rescue_last[0] or now - self.rescue_last[1] > 0.5:
                stats['episodes'] += 1
                stats['byCommand'][str(mask[0])] = stats['byCommand'].get(str(mask[0]), 0) + 1
                pilot.write('coop_reversal_suppressed', command=mask[0], horizontal=mask[1], hp=own.get('hp'),
                            peerPid=self.peer.pid, episode=stats['episodes'])
            self.rescue_last = (mask[0], now)
        pilot.kb.rescue_mask = mask
        try:
            return super().step(pilot, own, event_reaction)
        finally:
            pilot.kb.rescue_mask = None


class Pad(RescuePadMixin, previous.Pad):
    pass


class Pilot(RescuePilotMixin, child.Pilot):
    pass


class CooperativeFight(RescueCooperativeMixin, child.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
