"""UNRUN approach revision: retained RC-offer anchor and bounded lateral route.

Replaces only the policy child's approach mixin, incorporates lead241's vertical
escape and diagnostic, and has no dependency on lead240's reserve policy.
"""
import math
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_next_policy as previous
from thorn_vertical import capture_snapshot

base, thorn = previous.base, previous.thorn
Game, k32, psapi, Pad, Pilot = previous.Game, previous.k32, previous.psapi, previous.Pad, previous.Pilot


class CallbackReceipts:
    """Own-process callback observations, not offers or host victory authority.

    Client local-completion means its proven local callback consumed the role;
    it is not proof that the host accepted the claim or that damage happened.
    Host complete owner=0 is its own native callback return. Never use the
    host's owner=1 rows as this seat's RCs. Old bytes are baselined away.
    """
    LINE = re.compile(r'^\[script-rc\] (local completion|complete) source=(\d+) resource=(\d+) '
                      r'occurrence=(\d+) command=(\d+) owner=(\d+)(?: queued=([01]))?\s*$')
    def __init__(self, path):
        self.path, self.offset, self.pending = Path(path), None, b''
        self.serial, self.seen = 0, []

    def poll(self):
        try:
            with self.path.open('rb') as f:
                size = f.seek(0, 2)
                if self.offset is None:
                    self.offset, self.pending = size, b''
                    return True, self.serial
                if size < self.offset or size - self.offset > 1024 * 1024:
                    self.offset, self.pending = None, b''
                    return False, self.serial
                f.seek(self.offset)
                data = f.read(1024 * 1024)
                self.offset += len(data)
        except OSError:
            self.offset, self.pending = None, b''
            return False, self.serial
        lines = (self.pending + data).split(b'\n')
        self.pending = lines.pop()
        if len(self.pending) > 16384:
            self.offset, self.pending = None, b''
            return False, self.serial
        for line in lines:
            m = self.LINE.fullmatch(line.decode('utf-8', 'replace').rstrip('\r'))
            if not m:
                continue
            kind, source, resource, occurrence, command, owner, queued = m.groups()
            if int(command) not in (297, 298, 299, 345, 426, 432, 666, 667) \
                    or not int(source) or not int(resource) or not int(occurrence) \
                    or (kind == 'complete' and (owner != '0' or queued is not None)) \
                    or (kind == 'local completion' and (owner not in ('1', '2') or queued is None)):
                continue
            receipt = (source, resource, occurrence, command, owner)
            if receipt not in self.seen:
                self.seen = (self.seen + [receipt])[-256:]
                self.serial += 1
        return True, self.serial


def detour_vector(a, b, root, attempt):
    """Root corridor is a planning estimate, not collision geometry."""
    dx, dz = b[0] - a[0], b[2] - a[2]
    length = math.hypot(dx, dz)
    ux, uz = dx / length, dz / length
    sign = 1 if attempt == 1 else -1
    if root is not None and all(math.isfinite(x) for x in root):
        rx, rz = root[0] - a[0], root[2] - a[2]
        projection = (rx * dx + rz * dz) / (length * length)
        cross = abs(rx * uz - rz * ux)
        radius = math.hypot(rx, rz)
        if .05 < projection < .95 and cross <= 220 and 1 < radius <= 800 \
                and abs(root[1] - a[1]) <= 300:
            # Choose a root tangent toward the peer, then the opposite side on attempt2.
            tx, tz = rz / radius, -rx / radius
            if tx * dx + tz * dz < 0:
                tx, tz = -tx, -tz
            return (sign * tx, sign * tz), 'root-tangent', projection, cross
    return (-sign * uz, sign * ux), 'rescue-perpendicular', None, None


class ApproachRevisionMixin:
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.escape_key, self.escape_tries, self.escape_at = None, 0, float('-inf')
        self.anchor, self.detour = None, None
        self.plateau_log_at, self.plateau_rows = float('-inf'), 0
        self.callback_reader, self.callback_serial = None, None
        self.callback_key = None

    def facts(self, pilot, entry, event_reaction=False, *, monitor=False):
        if event_reaction or self.hold_started is not None or self.waiting_release:
            return None
        if not (self.own_log.update() and self.peer_log.update()) or self.own_log.downed \
                or not self.peer_log.downed or self.own_log.full_party or self.peer_log.full_party:
            return None
        try:
            own, peer = pilot.state(), self.peer.state()
        except (OSError, ValueError):
            return None
        if not own.get('free') or not own.get('inField') or own.get('menu') != 255 \
                or own.get('event') or (own.get('react') and not monitor) \
                or not own.get('hp') or own['hp'] <= 0 or not peer.get('free') \
                or (own.get('world'), own.get('room')) != (2, 34) \
                or own['roomScope'] != peer['roomScope'] or not own.get('player') \
                or self.peer_log.actor != peer.get('player') \
                or own['player'] != entry['player'] or own['roomScope'] != entry['roomScope']:
            return None
        a, b = own.get('playerPos'), peer.get('playerPos')
        if not a or not b or not all(math.isfinite(v) for v in (*a, *b)) or math.dist(a, b) <= 150:
            return None
        if not (self.own_log.update() and self.peer_log.update()) or self.own_log.downed \
                or not self.peer_log.downed or self.own_log.full_party or self.peer_log.full_party \
                or self.peer_log.actor != peer['player']:
            return None
        key = (tuple(own['roomScope']), own['player'], peer['player'], self.peer_log.downs)
        return key, own, tuple(a), tuple(b)

    def monitor(self, pilot, own, event_reaction):
        f = self.facts(pilot, own, event_reaction, monitor=True)
        if f is None:
            self.anchor, self.detour = None, None
            self.callback_serial, self.callback_key = None, None
            return None
        if self.callback_key != f[0]:
            self.callback_key, self.callback_serial = f[0], None
        if self.callback_reader is None:
            self.callback_reader = CallbackReceipts(self.own_log.path)
        known, serial = self.callback_reader.poll()
        if not known:
            if self.callback_serial is not None:
                pilot.write('coop_approach_reset', reason='callback-log-unknown')
            self.anchor, self.detour, self.callback_serial = None, None, None
            return None
        key, state, a, b = f
        now = time.monotonic()
        receipt_changed = self.callback_serial is not None and serial != self.callback_serial
        self.callback_serial = serial
        if receipt_changed:
            self.anchor, self.detour = None, None
            pilot.write('coop_approach_reset', reason='own-callback-consumed', callbackSerial=serial)
        if self.anchor is None or self.anchor[0] != key or now < self.anchor[3] \
                or math.dist(a, self.anchor[1]) > 20 or math.dist(b, self.anchor[2]) > 20:
            if self.detour is not None and self.detour['pulses'] == 0:
                self.detour = None
            self.anchor = (key, a, b, now)
            return None
        seconds = now - self.anchor[3]
        if seconds < 2:
            return None
        if now - self.plateau_log_at >= 2 and self.plateau_rows < 64:
            self.plateau_log_at, self.plateau_rows = now, self.plateau_rows + 1
            pilot.write('coop_plateau', key=key, stationarySec=round(seconds, 3), own=a, target=b,
                        verticalGap=b[1]-a[1], horizontalGap=math.hypot(b[0]-a[0], b[2]-a[2]),
                        distance3D=math.dist(a,b), offered=state.get('react'), callbackSerial=serial,
                        capture=capture_snapshot(pilot.game, state))
        return f

    def root(self, pilot, state):
        h = pilot.observe_head(state)
        root = h.get('rootWorldPos') if h.get('known') else None
        return root if root and all(math.isfinite(v) for v in root) else None

    def route_pulse(self, pilot, f):
        key, state, a, b = f
        d = self.detour
        if key != d['key'] or (d['until'] is not None and time.monotonic() >= d['until']) or d['pulses'] >= 6 \
                or math.hypot(b[0]-a[0], b[2]-a[2]) <= 80 or abs(b[1]-a[1]) > 300:
            pilot.write('coop_detour', stage='end', attempt=d['attempt'], own=a,
                        moved3D=math.dist(a, d['start']), pulses=d['pulses'])
            self.detour = None
            pilot.kb.hold(set())
            return False
        root = self.root(pilot, state)
        vector, basis, projection, cross = detour_vector(a, b, root, d['attempt'])
        fresh = self.facts(pilot, state)
        if fresh is None or fresh[0] != key:
            return False  # active window keeps running; an unstarted plan spends nothing
        if d['pulses'] == 0 and (math.dist(a,fresh[2]) > 20 or math.dist(b,fresh[3]) > 20):
            self.anchor, self.detour = None, None
            return False
        _, state, a, b = fresh
        if math.hypot(b[0]-a[0],b[2]-a[2]) <= 80 or abs(b[1]-a[1]) > 300:
            self.detour = None
            return False
        now = time.monotonic()
        if (d['until'] is not None and now >= d['until']) or now-self.since >= self.TIMEOUT-.12:
            self.detour = None
            return False
        keys = base.steer({**state, 'pos': a}, (a[0]+200*vector[0], a[2]+200*vector[1]))
        if not keys:
            return False
        if d['pulses'] == 0:
            if self.escape_tries >= 2 or now-self.escape_at < 5 or now-self.since >= self.TIMEOUT-.4:
                self.detour = None
                return False
            self.escape_tries += 1
            self.escape_at, self.anchor = now, None
            d.update(attempt=self.escape_tries, start=a, until=now+1.)
            pilot.write('coop_detour', stage='begin', attempt=self.escape_tries, key=key, own=a, target=b)
        d['pulses'] += 1
        pilot.write('coop_detour', stage='pulse', attempt=d['attempt'], pulse=d['pulses'],
                    basis=basis, root=root, projection=projection, crossTrack=cross,
                    own=a, target=b, direction=sorted(keys))
        try:
            pilot.kb.hold(keys)
            time.sleep(.12)
        finally:
            pilot.kb.hold(set())
        return True

    def step(self, pilot, own, event_reaction=False):
        candidate = self.monitor(pilot, own, event_reaction)
        consumed = super().step(pilot, own, event_reaction)  # existing RC/preheal/rescue/deadline
        if not consumed:
            self.detour = None
            return consumed
        f = self.facts(pilot, own, event_reaction)  # monitor-only facts NEVER authorize input
        if f is None:
            return consumed
        if self.detour is not None:
            return self.route_pulse(pilot, f) or consumed
        if candidate is None or candidate[0] != f[0]:
            return consumed
        key, state, a, b = f
        now = time.monotonic()
        if key != self.escape_key:
            self.escape_key, self.escape_tries, self.escape_at = key, 0, float('-inf')
        if self.escape_tries >= 2 or now-self.escape_at < 5 or self.since is None \
                or now-self.since >= self.TIMEOUT-.4:
            return consumed
        if math.dist(a, candidate[2]) > 20 or math.dist(b, candidate[3]) > 20:
            self.anchor = None
            return consumed
        ygap, horizontal = b[1]-a[1], math.hypot(b[0]-a[0], b[2]-a[2])
        if abs(ygap) <= 300 and horizontal > 80:
            self.detour = dict(key=key, attempt=self.escape_tries+1, start=a, until=None, pulses=0)
            return self.route_pulse(pilot, f) or consumed
        if horizontal > 80:
            # Preserve the earlier jump/side response for a stationary airborne
            # gap that is not vertically aligned. It shares this same quota.
            self.escape_tries += 1
            self.escape_at, self.anchor = now, None
            pilot.write('coop_stuck_escape', stage='begin', attempt=self.escape_tries, key=key)
            try:
                pilot.kb.hold(set()); pilot.kb.tap('jump',80)
                after = self.facts(pilot, own, event_reaction)
                if after is None or after[0] != key \
                        or time.monotonic()-self.since >= self.TIMEOUT-.2:
                    return True
                pilot.kb.hold({'left' if self.escape_tries == 1 else 'right'})
                time.sleep(.2)
            finally:
                pilot.kb.hold(set())
            return True
        # Vertical/airborne branch preserves lead241's step then jump and .4s guard.
        if ygap <= 300 or horizontal > 80:
            return consumed
        root = self.root(pilot, state)
        basis = root if root is not None else b
        dx, dz = a[0]-basis[0], a[2]-basis[2]
        if math.hypot(dx,dz) < 1: dx, dz = 1., 0.
        fresh = self.facts(pilot, own, event_reaction)
        if fresh is None or fresh[0] != key or math.dist(a,fresh[2]) > 20 \
                or math.dist(b,fresh[3]) > 20 or time.monotonic()-self.since >= self.TIMEOUT-.4:
            return consumed
        _, state, a, b = fresh
        if b[1]-a[1] <= 300 or math.hypot(b[0]-a[0],b[2]-a[2]) > 80:
            return consumed
        keys = base.steer({**state,'pos':a}, (a[0]+dx,a[2]+dz))
        if not keys: return consumed
        self.escape_tries += 1
        self.escape_at, self.anchor = time.monotonic(), None
        pilot.write('coop_vertical_escape', stage='begin', attempt=self.escape_tries, own=a, target=b)
        try:
            pilot.kb.hold(keys); time.sleep(.2); pilot.kb.hold(set())
            after = self.facts(pilot, own, event_reaction)
            if after is None or after[0] != key or after[3][1]-after[2][1] <= 300 \
                    or time.monotonic()-self.since >= self.TIMEOUT-.08:
                return True
            pilot.write('coop_vertical_escape', stage='after-step', own=after[2], target=after[3],
                        moved3D=math.dist(a,after[2]))
            pilot.kb.tap('jump',80)
        finally:
            pilot.kb.hold(set())
        return True


# Use the same reviewed preheal/head/native revive chain, replacing the previous
# approach mixin rather than letting two escape implementations fire in one tick.
class CooperativeFight(ApproachRevisionMixin, previous.previous.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
