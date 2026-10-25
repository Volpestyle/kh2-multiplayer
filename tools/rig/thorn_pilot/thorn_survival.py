"""UNRUN input-only reserve policy over the frozen thorn_next_policy child.

Keep one healthier seat moving during 202 when either seat is low. Positions
are steering estimates, never a native safe-zone or hit-admission claim.
"""
import math
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_next_policy as previous

base, thorn = previous.base, previous.thorn
Game, k32, psapi = previous.Game, previous.k32, previous.psapi


def reserve_choice(own, peer, own_pid, peer_pid):
    """Pure choice on already qualified living, same-field seat snapshots."""
    a, b = own['hp'] / own['maxHp'], peer['hp'] / peer['maxHp']
    return min(a, b) <= 0.4 and (a > b or (a == b and own_pid < peer_pid))


def movement(own, root, side):
    """Keep lateral motion; separate from the body and turn inward at the rim.

    None of these planning distances is claimed to defeat projectile 1041.
    One pulse is 120 ms, followed by release and new state/role qualification.
    """
    x, _, z = own['playerPos']
    dx, dz = x - root[0], z - root[2]
    radius = math.hypot(dx, dz)
    if radius < 1:
        dx, dz, radius = 1.0, 0.0, 1.0
    ux, uz = dx / radius, dz / radius
    radial = 0.7 if radius < 1100 else -0.4 if radius > 1700 else 0.0
    vx, vz = -side * uz + radial * ux, side * ux + radial * uz
    arena_radius = math.hypot(x, z)
    if arena_radius > 1400:
        vx -= x / arena_radius
        vz -= z / arena_radius
    return base.steer({**own, 'pos': own['playerPos']}, (x + 200 * vx, z + 200 * vz))


class ReservePilotMixin:
    def fight(self, step, deadline):
        self.reserve_entries, self.reserve_last_at = 0, None
        try:
            return super().fight(step, deadline)
        finally:
            self.reserve_last_at = None

    def reserve_allowed(self):
        last = getattr(self, 'reserve_last_at', None)
        new_entry = last is None or time.monotonic() - last > 1.0
        return not new_entry or getattr(self, 'reserve_entries', 0) < 2

    def reserve_facts(self, own):
        if not self.reserve_allowed():
            return None
        c = self.cooperative
        if not getattr(self, 'head_active', False) or c is None \
                or c.hold_started is not None or c.waiting_release:
            return None
        if not (c.own_log.update() and c.peer_log.update()) \
                or c.own_log.downed or c.peer_log.downed \
                or c.own_log.full_party or c.peer_log.full_party:
            return None  # the original cooperative path owns every rescue
        try:
            peer = c.peer.state()
        except (OSError, ValueError):
            return None
        for s in (own, peer):
            if not s.get('free') or not s.get('inField') or s.get('menu') != 255 \
                    or s.get('event') or s.get('react') or (s.get('world'), s.get('room')) != (2, 34) \
                    or not s.get('player') or not s.get('hp') or not s.get('maxHp') \
                    or s['hp'] <= 0 or s['maxHp'] <= 0 or not s.get('playerPos') \
                    or not all(math.isfinite(v) for v in s['playerPos']):
                return None
        if own['roomScope'] != peer['roomScope'] \
                or not reserve_choice(own, peer, self.game.pid, c.peer.pid):
            return None  # no added head/model reads for healthy or attacking seat
        try:
            head = self.observe_head(own)
            after = self.game.state()
        except (OSError, ValueError):
            return None
        if not after.get('free') or not after.get('inField') or after.get('menu') != 255 \
                or after.get('event') or after.get('react') \
                or (after.get('world'), after.get('room')) != (2, 34) \
                or not after.get('playerPos') or not all(math.isfinite(v) for v in after['playerPos']):
            return None
        if own['roomScope'] != peer['roomScope'] or own['roomScope'] != after['roomScope'] \
                or own['player'] != after['player'] or own['hp'] != after['hp'] \
                or own['maxHp'] != after['maxHp'] or not head.get('known') \
                or head.get('motion') != 202 or not head.get('rootWorldPos') \
                or not all(math.isfinite(v) for v in head['rootWorldPos']):
            return None
        # Downing may have been appended during the external reads.
        if not (c.own_log.update() and c.peer_log.update()) \
                or c.own_log.downed or c.peer_log.downed \
                or c.own_log.full_party or c.peer_log.full_party:
            return None
        if not reserve_choice(after, peer, self.game.pid, c.peer.pid):
            return None
        if not self.reserve_allowed():
            return None
        return after, peer, head

    def reserve_turn(self, own):
        facts = self.reserve_facts(own)
        if facts is None:
            return False
        state, peer, head = facts
        # A real available Potion keeps its reviewed heal path; an empty slot
        # must not turn reserve motion into another stationary failed heal.
        if state['hp'] < 0.4 * state['maxHp'] and self.heal_available(state):
            return False
        if not self.reserve_allowed():
            return False
        now = time.monotonic()
        last = getattr(self, 'reserve_last_at', None)
        if last is None or now - last > 1.0:
            self.reserve_entries = getattr(self, 'reserve_entries', 0) + 1
        self.reserve_last_at = now
        side = 1 if self.game.pid < self.cooperative.peer.pid else -1
        keys = movement(state, head['rootWorldPos'], side)
        self.write('thorn_reserve', ownHp=state['hp'], peerHp=peer['hp'],
                   own=state['playerPos'], peer=peer['playerPos'], root=head['rootWorldPos'],
                   motion=head['motion'], direction=sorted(keys), pulseSec=0.12,
                   entry=self.reserve_entries, entryLimit=2)
        try:
            self.kb.hold(keys)
            time.sleep(0.12)
        finally:
            self.kb.hold(set())
        return True


class ReservePadMixin:
    def tap(self, action, ms=90):
        p = getattr(self, 'head_owner', None)
        if action == 'attack' and p is not None and p.reserve_facts(p.state()) is not None:
            self.hold(set())
            p.write('thorn_reserve_swing', sent=False, reason='reserve-mid-burst')
            return False
        return super().tap(action, ms)


class ReserveCooperativeMixin:
    def step(self, pilot, own, event_reaction=False):
        if not event_reaction and getattr(pilot, 'head_active', False) and pilot.reserve_turn(own):
            return True
        return super().step(pilot, own, event_reaction)


class Pad(ReservePadMixin, previous.Pad):
    pass


class Pilot(ReservePilotMixin, previous.Pilot):
    pass


class CooperativeFight(ReserveCooperativeMixin, previous.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
