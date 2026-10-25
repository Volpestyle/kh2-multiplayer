"""VUH-2617 lead #328: revive-protect r5, over thorn_orbhold (orb-volley hold-off and Creeper-first kept).

In the r4d batch the hold-off was not what lost revive windows; the rescuer's own Reversal presses were. In zr119 the
rescuer reached its partner (71 away) and started four holds: three ended when the core met a 432 and pressed it (the
throwback carried it to 341), one on an orb hit, and 667 presses then kept it 400-700 away until the 60 s bound. The
revive core (thorn.CooperativeFight.step) ends any hold for an offered RC and presses it ("RCs still win Triangle"),
and Triangle is also the revive button, so any fresh Triangle press with an RC on screen is that RC. Opt-in per fight
step:

  "rescueProtect": {"commands": [432, 667], "within": 300}

While this seat's partner is logically downed (and this seat is not, and the party is not wiped), one of `commands` is
offered, and this seat's revive is in progress (a hold, or a full bar awaiting recovery) or it stands within `within`
of the partner horizontally, the core sees no RC on that tick, and the pad sends no fresh Triangle press: a revive
hold already held stays held, nothing else adds Triangle. The core keeps every rule of its own, so a hidden revive
prompt still releases the hold and a visible one is re-pressed once the RC is gone. No RC completes, so
thorn_approach's own-callback resets do not follow either. Every other RC, every tick farther than `within` with no
revive in progress (zr118's stall, 1070-1620 away, stays as before), and every tick with no downed partner behave as
before. The rescuer can take the hit the Reversal would have countered (rescueNoReversal, zr112-114: 432/426 within
600, no throwbacks, 0 kills in 3). Input-only, no native writes.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn  # noqa: E402
import thorn_orbhold as child  # noqa: E402

base, previous = child.base, child.previous


def protect(cfg, react, reviving, horizontal):
    """True when this tick's offered RC must not be taken: a listed command, offered while this seat's revive is in
    progress or it stands within `within` of its downed partner."""
    if cfg is None or not react or int(react) not in cfg['commands']:
        return False
    return reviving or (horizontal is not None and horizontal <= cfg['within'])


class ProtectPadMixin:
    protect_mask = False  # this cooperative tick's offered RC is protected

    def protect_strip(self, action_names):
        if self.protect_mask and 'react' in action_names and self.keys.get('react', 'react').upper() not in self.held:
            self.protect_stripped = getattr(self, 'protect_stripped', 0) + 1
            return True
        return False

    def hold(self, actions):
        if self.protect_strip(actions):
            actions = set(actions) - {'react'}
        return super().hold(actions)

    def tap(self, action, ms=90):
        if self.protect_strip({action}):
            return False
        return super().tap(action, ms)


class ProtectPilotMixin:
    protect_cfg = None

    def fight(self, step, deadline):
        cfg = step.get('rescueProtect') if step.get('eventReactions') else None
        if cfg is True:
            cfg = {}
        self.protect_cfg = None if not isinstance(cfg, dict) else dict(
            commands=frozenset(int(c) for c in cfg.get('commands', (432, 667))), within=float(cfg.get('within', 300)))
        self.protect_stats = dict(episodes=0, ticks=0, byCommand={}, reviving=0)
        try:
            return super().fight(step, deadline)
        finally:
            if self.protect_cfg is not None:
                self.write('rescue_protect_summary', stripped=getattr(self.kb, 'protect_stripped', 0),
                           **self.protect_stats)
            self.protect_cfg = None
            self.kb.protect_mask = False


class ProtectCore(thorn.CooperativeFight):
    """Listed after OrbHoldCore, so C3 places it directly above thorn.CooperativeFight (see CooperativeFight below)."""
    protect_last = (None, 0.0)  # (command, monotonic) of the last protected tick

    def protect_check(self, pilot, own, event_reaction):
        """(command, horizontal or None, reviving) when this tick's RC is protected, else None."""
        cfg = getattr(pilot, 'protect_cfg', None)
        react = own.get('react')
        if cfg is None or event_reaction or not react or int(react) not in cfg['commands'] \
                or (own.get('world'), own.get('room')) != (2, 34):
            return None
        if not self.own_log.update() or not self.peer_log.update() or self.own_log.downed \
                or not self.peer_log.downed or self.own_log.full_party or self.peer_log.full_party:
            return None
        reviving = self.hold_started is not None or self.waiting_release
        horizontal = None
        try:
            peer = self.peer.state()
        except (OSError, ValueError):
            peer = None
        a, b = own.get('playerPos'), peer.get('playerPos') if peer else None
        if a and b and peer.get('player') == self.peer_log.actor:
            horizontal = round(math.hypot(b[0] - a[0], b[2] - a[2]), 1)
        if not protect(cfg, react, reviving, horizontal):
            return None
        return int(react), horizontal, reviving

    def step(self, pilot, own, event_reaction=False):
        mask = self.protect_check(pilot, own, event_reaction)
        if mask is None:
            return super().step(pilot, own, event_reaction)
        now = time.monotonic()
        stats = pilot.protect_stats
        stats['ticks'] += 1
        stats['reviving'] += mask[2]
        if mask[0] != self.protect_last[0] or now - self.protect_last[1] > 0.5:
            stats['episodes'] += 1
            stats['byCommand'][str(mask[0])] = stats['byCommand'].get(str(mask[0]), 0) + 1
            pilot.write('coop_rc_protected', command=mask[0], horizontal=mask[1], reviving=mask[2],
                        hp=own.get('hp'), peerPid=self.peer.pid, episode=stats['episodes'])
        self.protect_last = (mask[0], now)
        pilot.kb.protect_mask = True
        try:
            return super().step(pilot, {**own, 'react': 0}, event_reaction)
        finally:
            pilot.kb.protect_mask = False


class Pad(ProtectPadMixin, previous.Pad):
    pass


class Pilot(ProtectPilotMixin, child.Pilot):
    pass


class CooperativeFight(child.CooperativeFight, ProtectCore):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
