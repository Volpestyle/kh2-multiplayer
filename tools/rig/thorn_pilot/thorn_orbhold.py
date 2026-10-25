"""VUH-2617 lead #302: a rescuer waits out Thorn's orb volley near Thorn, over thorn_creeper (Creeper-first kept).

Thorn's motion 202 winds up for ~340 frames, then loops a firing segment for up to ~30 s; every orb hit (1041/1014)
in zr98-114 landed in that loop. Orb hits per exposed second were ~2.2x higher 1250+ from Thorn's root (0.36/s) than
inside it (0.16/s), and lost revive windows were the ones spent in the loop with the downed seat far from Thorn
(median firing share 1.0, partner 1584 from Thorn; recovered: 0.18, 1114). A revive hold itself needs ~1.3 s, and the
gap between occurrences is ~16 s with no orbs (.local/review/vuh2617-orb-hold). Opt-in per fight step:

  "rescueOrbHold": {"far": 1250, "hold": 1000, "finish": 300, "maxWaitSec": 30}

The hold-off lives at the revive core, not above the stack (Sage, VUH-2617, on r1): OrbHoldCore sits directly above
thorn.CooperativeFight, so every higher layer (the pre-revive Potion, RC gate, Creeper-first, approach revision, ...)
takes its tick first, and the core still makes every refusal of its own (logs, timeout, event/menu, partner state,
identity and scope, HP). Only the core's single steer toward the downed partner is changed, and only while Thorn is in
motion 202, the partner lies `far` or more from Thorn's root and this seat is more than `finish` from the partner: the
rescuer then steers toward Thorn until within `hold` of the root and stands. RCs keep their press (with that
direction). The approach resumes when Thorn leaves 202, when the partner is no longer far, or after `maxWaitSec` held
off in one revive window. Thorn is read through the existing head observation (no new reads); an unknown head never
holds back. While held off, thorn_approach's plateau monitor must not read the deliberate stand-off as a stall: the
hook resets its anchor and detour, and the outermost OrbHoldApproachMixin makes its `facts()` empty for the rest of
a held tick, so the candidate it cached before the core can't start a detour or escape after it (Sage, on r2).
Input-only, no native writes.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn  # noqa: E402
import thorn_creeper as child  # noqa: E402

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


class OrbHoldApproachMixin:
    """Outermost. thorn_approach.ApproachRevisionMixin caches a plateau candidate before the core runs and, after it,
    re-reads `facts()` before any detour pulse or jump/vertical escape. On a tick the hook held off, that read is
    empty, so none of those post-core inputs can follow a deliberate stand-off."""
    orbhold_tick_held = False

    def step(self, pilot, own, event_reaction=False):
        self.orbhold_tick_held = False
        return super().step(pilot, own, event_reaction)

    def facts(self, *args, **kwargs):
        if self.orbhold_tick_held:
            return None
        return super().facts(*args, **kwargs)


class OrbHoldCore(thorn.CooperativeFight):
    """Listed as the last base, so C3 places it directly above thorn.CooperativeFight (see CooperativeFight below)."""
    orbhold_since = None       # monotonic start of the open hold-off episode
    orbhold_last = None        # monotonic of the last held-off tick
    orbhold_waited = 0.0       # seconds held off in this revive window
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
        if cfg is None or event_reaction or (own.get('world'), own.get('room')) != (2, 34):
            if cfg is not None:
                self.orbhold_end(pilot, 'core')
            return super().step(pilot, own, event_reaction)
        original, held, reached = base.steer, [], []

        def steer(s, target, *args, **kwargs):
            # The core's one steer toward the partner, reached only past all of its refusals.
            reached.append(True)
            peer_pos = (target[0], 0.0, target[1])
            hold = orb_hold(cfg, self.orbhold_observe(pilot, own), s.get('pos'), peer_pos, self.orbhold_waited)
            if hold is None:
                return original(s, target, *args, **kwargs)
            held.append(hold)
            return set() if hold['inside'] else original(s, (hold['root'][0], hold['root'][2]), *args, **kwargs)

        base.steer = steer
        try:
            result = super().step(pilot, own, event_reaction)
        finally:
            base.steer = original
        now = time.monotonic()
        if not held:
            if not self.peer_log.downed:
                self.orbhold_waited = 0.0  # a new revive window gets a fresh cap
                reason = 'peer-recovered'
            elif not reached:
                reason = 'core'
            else:
                reason = 'cap' if self.orbhold_waited >= cfg['maxWaitSec'] else 'released'
            self.orbhold_end(pilot, reason)
            return result
        hold = held[-1]
        self.orbhold_tick_held = True  # OrbHoldApproachMixin empties the approach layer's post-core facts
        # Standing off is deliberate: thorn_approach's plateau monitor would read it as a stalled approach and spend
        # its detour/jump escapes walking toward the partner, so a held-off tick resets its anchor and any detour.
        if hasattr(self, 'anchor'):
            self.anchor = None
        if getattr(self, 'detour', None) is not None:
            self.detour = None
        if self.orbhold_last is not None:
            self.orbhold_waited += now - self.orbhold_last
        self.orbhold_last = now
        if self.orbhold_since is None:
            self.orbhold_since = now
            pilot.orbhold_stats['episodes'] += 1
            pilot.write('coop_orb_hold', peerPid=self.peer.pid, waited=round(self.orbhold_waited, 2),
                        hp=own.get('hp'), episode=pilot.orbhold_stats['episodes'], **hold)
        return result


class Pilot(OrbHoldPilotMixin, child.Pilot):
    pass


class CooperativeFight(OrbHoldApproachMixin, child.CooperativeFight, OrbHoldCore):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, previous.Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
