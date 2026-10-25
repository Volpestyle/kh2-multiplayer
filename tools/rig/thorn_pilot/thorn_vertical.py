"""UNRUN vertical revive-approach escape; independent of lead240.

Stationarity is an input-policy proxy, not proof of native grounding. A stuck
capture may ignore these bounded inputs. No native cleanup or write is added.
"""
import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_next_policy as previous
from head_observation import Reader

base, thorn = previous.base, previous.thorn
Game, k32, psapi, Pad, Pilot = previous.Game, previous.k32, previous.psapi, previous.Pad, previous.Pilot


def capture_snapshot(game, state):
    """Bounded external reads of the current listed canonical actor only.

    Capture/bank/anchor values are printed, never followed. +18C belongs to the
    motion; its bit alone does not establish a live capture or grounded state.
    """
    r = Reader(game)
    try:
        gate = r.gate(state)
        actor = state['player']
        handle = r.members().get(actor)
        if not handle:
            raise ValueError('actor-not-listed')
        motion = r.u32(actor + 0x180)
        flags = r.u32(actor + 0x18C)
        anchor, source, bank = (r.u64(actor + off) for off in (0x2A0, 0x2B0, 0x2B8))
        coroutine = r.u64(actor + 0xD48)
        if r.members().get(actor) != handle:
            raise ValueError('actor-replaced')
        if r.u32(actor + 0x180) != motion or r.u32(actor + 0x18C) != flags \
                or tuple(r.u64(actor + off) for off in (0x2A0, 0x2B0, 0x2B8)) != (anchor, source, bank) \
                or r.u64(actor + 0xD48) != coroutine \
                or r.u32(actor + 4) != handle or r.gate(state) != gate:
            raise ValueError('capture-snapshot-drift')
        return dict(known=True, actor=actor, handle=handle,
                    motion=motion, flags18C=flags,
                    motionCaptureBit=bool(flags & 0x4000), anchorOwn=anchor == actor + 0x80,
                    captureObject=source, externalBank=bank, coroutine=coroutine,
                    borrowShape='own' if anchor == actor + 0x80 and source == 0 and bank == 0
                    else 'external-or-unwinding', grounded=None)
    except (OSError, ValueError, struct.error) as e:
        return dict(known=False, reason=str(e), grounded=None)


class VerticalEscapeMixin:
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.plateau_anchor = None
        self.plateau_log_at = float('-inf')
        self.plateau_rows = 0

    def approach_facts(self, pilot, entry, event_reaction=False):
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
                or own.get('event') or own.get('react') or not own.get('hp') or own['hp'] <= 0 \
                or not peer.get('free') or (own.get('world'), own.get('room')) != (2, 34) \
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

    def stationary(self, pilot, entry, event_reaction):
        facts = self.approach_facts(pilot, entry, event_reaction)
        if facts is None:
            self.plateau_anchor = None
            return None
        key, own, a, b = facts
        now = time.monotonic()
        anchor = self.plateau_anchor
        if anchor is None or anchor[0] != key or now < anchor[3] \
                or math.dist(a, anchor[1]) > 20 or math.dist(b, anchor[2]) > 20:
            self.plateau_anchor = (key, a, b, now)
            return None
        seconds = now - anchor[3]
        if seconds < 2:
            return None
        if now - self.plateau_log_at >= 2 and self.plateau_rows < 64:
            self.plateau_log_at, self.plateau_rows = now, self.plateau_rows + 1
            pilot.write('coop_plateau', key=key, stationarySec=round(seconds, 3),
                        own=a, target=b, verticalGap=b[1] - a[1],
                        horizontalGap=math.hypot(b[0] - a[0], b[2] - a[2]),
                        distance3D=math.dist(a, b), capture=capture_snapshot(pilot.game, own))
        return facts

    def step(self, pilot, own, event_reaction=False):
        candidate = self.stationary(pilot, own, event_reaction)
        consumed = super().step(pilot, own, event_reaction)  # original timeout/RC/hold first
        if not consumed or candidate is None:
            return consumed
        key, state, a, b = candidate
        if b[1] - a[1] <= 300 or math.hypot(b[0] - a[0], b[2] - a[2]) > 80:
            return consumed
        if key != self.escape_key:
            self.escape_key, self.escape_tries, self.escape_at = key, 0, float('-inf')
        now = time.monotonic()
        if self.escape_tries >= 2 or now - self.escape_at < 5 \
                or self.since is None or now - self.since >= self.TIMEOUT - 0.4:
            return consumed
        # Prefer away from the current Thorn root; the downed peer's XZ is a
        # qualified fallback for leaving the vertical alignment, not a safe zone.
        head = pilot.observe_head(state)
        root = head.get('rootWorldPos') if head.get('known') else None
        basis = root if root and all(math.isfinite(v) for v in root) else b
        dx, dz = a[0] - basis[0], a[2] - basis[2]
        if math.hypot(dx, dz) < 1:
            dx, dz = 1.0, 0.0
        fresh = self.approach_facts(pilot, own, event_reaction)
        if fresh is None or fresh[0] != key or math.dist(fresh[2], a) > 20 \
                or math.dist(fresh[3], b) > 20:
            self.plateau_anchor = None
            return consumed
        _, state, a, b = fresh
        if b[1] - a[1] <= 300 or math.hypot(b[0] - a[0], b[2] - a[2]) > 80 \
                or time.monotonic() - self.since >= self.TIMEOUT - 0.4:
            return consumed
        keys = base.steer({**state, 'pos': a}, (a[0] + dx, a[2] + dz))
        if not keys:
            return consumed
        self.escape_tries += 1  # SAME horizontal/vertical quota, consumed before input
        self.escape_at = time.monotonic()
        self.plateau_anchor = None
        pilot.write('coop_vertical_escape', stage='begin', attempt=self.escape_tries,
                    key=key, own=a, target=b, direction=sorted(keys),
                    basis='thorn-root' if basis is root else 'downed-peer')
        try:
            pilot.kb.hold(keys)
            time.sleep(0.2)
            pilot.kb.hold(set())
            after = self.approach_facts(pilot, own, event_reaction)
            if after is None or after[0] != key or after[3][1] - after[2][1] <= 300 \
                    or time.monotonic() - self.since >= self.TIMEOUT - 0.08:
                pilot.write('coop_vertical_escape', stage='stop', reason='after-step-context-or-height')
                return True  # already dropping/in range, or a higher-priority owner appeared
            pilot.write('coop_vertical_escape', stage='after-step', own=after[2], target=after[3],
                        moved3D=math.dist(a, after[2]))
            pilot.kb.tap('jump', 80)
        finally:
            pilot.kb.hold(set())
        pilot.write('coop_vertical_escape', stage='end', attempt=self.escape_tries)
        return True


class CooperativeFight(VerticalEscapeMixin, previous.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
