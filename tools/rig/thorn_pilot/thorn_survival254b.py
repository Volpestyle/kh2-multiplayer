"""Lead254/255: opening heals and wounded-seat 202 movement, over lead244.

Input-only child for the composed 2506f0f binary. Pinned dependencies stay
unchanged. The existing tagged heal guard, cursor repair and rescue own inputs.
"""
import math
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_stack as previous
import thorn_heal2 as heal

base, thorn = previous.base, previous.thorn
Game, k32, psapi = previous.Game, previous.k32, previous.psapi
BINARY = '2506f0f6c1707204ed7d8915a05c0967a66eb345'
OPENINGS = (209, 210, 211)


class PromptReader(thorn.PromptReader):
    def reset(self):
        super().reset()
        self.grace_stamp = None

    def interpret(self, state, raw):
        before = self.frame
        command, info = super().interpret(state, raw)
        # Reuse the existing read-only, double-read v7 snapshot and all its
        # identity, field, freshness and30-frame settling checks. Only a frame
        # advance stamps metadata; repeats keep that stamp, never renew it.
        if info.get('source') == 'downed-prompt-v7':
            remaining = struct.unpack_from('<f', raw, 220)[0]
            drops, active = struct.unpack_from('<II', raw, 232)
            count = struct.unpack_from('<I', raw, 92)[0]
            if math.isfinite(remaining) and 0 < remaining <= 120 and active == 1 and count > 0:
                key = state['player'], tuple(state['roomScope']), count, info['frame']
                if info['frame'] != before:
                    self.grace_stamp = key, time.monotonic()
                if self.grace_stamp is not None and self.grace_stamp[0] == key \
                        and 0 <= time.monotonic()-self.grace_stamp[1] <= .2:
                    info['reviveGrace'] = dict(frames=remaining, hpDrops=drops, count=count,
                        actor=state['player'], scope=list(state['roomScope']), frame=info['frame'],
                        sampledAt=self.grace_stamp[1])
        return command, info


def ordinary(s):
    hp, maximum = s.get('hp'), s.get('maxHp')
    pos = s.get('playerPos')
    return bool(s.get('free') and s.get('inField') and s.get('menu') == 255
                and not s.get('event') and not s.get('react') and s.get('player')
                and (s.get('world'), s.get('room')) == (2, 34)
                and isinstance(hp, (int, float)) and math.isfinite(hp) and hp > 0
                and isinstance(maximum, (int, float)) and math.isfinite(maximum) and maximum > 0
                and pos and len(pos) == 3 and all(math.isfinite(v) for v in pos))


class SurvivalPilotMixin:
    def fight(self, step, deadline):
        self.survival254_active = bool(step.get('nextSeries')) and bool(step.get('heal'))
        self.opening_heal_ready = 0.0
        self.opening_health = None
        self.opening_quiet_since = time.monotonic()
        self.grace_heal_attempted = set()
        self.survival254_deadline = deadline
        # The child owns ordinary heal scheduling. Disabling the base threshold
        # also disables its predicted-drop trigger; caps/keys/guards stay shared.
        if self.survival254_active:
            step = {**step, 'heal': {**step['heal'], 'below': 0.0, 'lead': 0.0}}
        try:
            return super().fight(step, deadline)
        finally:
            self.survival254_active = False

    def survival_facts(self, own):
        c = self.cooperative
        if not getattr(self, 'survival254_active', False) or not getattr(self, 'head_active', False) \
                or c is None or c.hold_started is not None or c.waiting_release \
                or time.monotonic() >= self.survival254_deadline or not ordinary(own) \
                or own['hp'] > .5 * own['maxHp']:
            return None
        def living_logs():
            return (c.own_log.update() and c.peer_log.update()
                    and not (c.own_log.downed or c.peer_log.downed
                             or c.own_log.full_party or c.peer_log.full_party))
        if not living_logs():
            return None
        try:
            peer = c.peer.state()
            if not ordinary(peer) or own['roomScope'] != peer['roomScope']:
                return None
            head = self.observe_head(own)
            after = self.game.state()
        except (OSError, ValueError):
            return None
        if not ordinary(after) or any(after.get(k) != own.get(k)
                                     for k in ('player', 'roomScope', 'hp', 'maxHp')) \
                or not head.get('known') or not living_logs():
            return None
        key = (after['player'], tuple(after['roomScope']), after['maxHp'])
        old = self.opening_health
        if old is None or old[0] != key or after['hp'] < old[1]:
            self.opening_quiet_since = time.monotonic()
        self.opening_health = key, after['hp']
        return after, peer, head

    def heal_window(self, state, head):
        if head.get('motion') in OPENINGS and time.monotonic()-self.opening_quiet_since >= .7:
            return 'dazed'
        g = state.get('reactObservation', {}).get('reviveGrace', {})
        key = (state['player'], tuple(state['roomScope']), g.get('count'))
        # Grace is observed protection, not an inference from a wall-clock log
        # edge. The existing log must also prove this owner has revived.
        if self.cooperative.own_log.revives > 0 and g.get('actor') == state['player'] \
                and g.get('scope') == list(state['roomScope']) and g.get('hpDrops') == 0 \
                and isinstance(g.get('frames'), (int, float)) and 60 <= g['frames'] <= 120 \
                and isinstance(g.get('count'), int) and g['count'] > 0 \
                and isinstance(g.get('frame'), int) and g['frame'] > 0 \
                and g.get('frame') == state.get('reactObservation', {}).get('frame') \
                and isinstance(g.get('sampledAt'), (int, float)) \
                and 0 <= time.monotonic()-g['sampledAt'] <= .2 \
                and key not in self.grace_heal_attempted:
            return 'post-revive-grace'
        return None

    def opening_heal(self, facts):
        """One bounded ordinary sequence through the SAME HealPad/HealPilot.

        Unlike pre_revive_heal it grants no peer-down exception. That method
        remains inherited unchanged. No independent cursor/guard/counters.
        """
        start, _, head = facts
        if self.heal_window(start, head) is None or start['hp'] > .5 * start['maxHp'] \
                or time.monotonic() < self.opening_heal_ready \
                or not self.heal_available(start):
            return False
        # A downed-menu repair belongs to its existing receipt, not a new heal.
        # Run that reviewed repair first; a menu heal must start on Attack.
        repair = getattr(self.kb, 'heal_repair_hook', None)
        if repair is not None and not repair():
            return False
        cursor = getattr(self.kb, 'heal_cursor', None)
        if cursor is None or cursor()[0] != 0:
            return False
        # Availability reads can overlap an event/downing/phase change.
        fresh = self.survival_facts(self.state())
        if fresh is None or fresh[0]['player'] != start['player'] \
                or fresh[0]['roomScope'] != start['roomScope'] \
                or fresh[0]['hp'] > .5 * fresh[0]['maxHp'] \
                or self.heal_window(fresh[0], fresh[2]) is None:
            return False
        start, _, head = fresh
        window = self.heal_window(start, head)
        grace = start.get('reactObservation', {}).get('reviveGrace')
        if window == 'post-revive-grace':
            self.grace_heal_attempted.add((start['player'], tuple(start['roomScope']), grace['count']))
        pad, cfg = self.kb, self.heal_cfg
        until = min(time.monotonic() + 5.0, self.survival254_deadline)
        started = False
        self.write('opening_heal', stage='begin', hp=start['hp'], motion=head['motion'],
                   items=self.observe_items(start), deadline=until, window=window, grace=grace)

        def expired():
            if time.monotonic() < until:
                return False
            if not pad.heal_aborted:
                pad.heal_aborted = True
                self.write('heal_guard', reason='opening-heal-input-deadline')
                pad.hold(set())
                if started:
                    pad.heal_restore('opening-heal-input-deadline')
            return True

        try:
            pad.hold(set())
            for i, key in enumerate(cfg['keys']):
                if expired():
                    break
                started = True
                pad.tap(f'{heal.KEY}{i}:{key}')
                if pad.heal_aborted:
                    break
                time.sleep(max(0, min(.25, until-time.monotonic())))
            if not pad.heal_aborted:
                time.sleep(max(0, min(1.5, until-time.monotonic())))
            after = self.state()
            try:
                end = self.observe_head(after)
            except (OSError, ValueError):
                end = {'known': False}
            self.write('heal', hp=start['hp'], after=after.get('hp'), preRevive=False,
                       opening=True, startMotion=head['motion'], window=window,
                       endMotion=end.get('motion') if end.get('known') else None)
            improved = after.get('hp') is not None and after['hp'] > start['hp']
            self.opening_heal_ready = time.monotonic() + (3.0 if improved else 1.0)
            if not pad.heal_aborted and not improved:
                for key in cfg.get('reset', ['SPACE', 'SPACE']):
                    if expired():
                        break
                    pad.tap(f'{heal.RESET}{key}')
                    if pad.heal_aborted:
                        break
                    time.sleep(max(0, min(.3, until-time.monotonic())))
        finally:
            pad.hold(set())
        return True

    def survival_turn(self, own):
        facts = self.survival_facts(own)
        if facts is None:
            return False
        state, peer, head = facts
        if self.opening_heal(facts):
            return True  # before the inherited head combo
        if head.get('motion') != 202 or state['hp'] > .4 * state['maxHp']:
            return False
        root = head.get('rootWorldPos')
        if not root or len(root) != 3 or not all(math.isfinite(v) for v in root) \
                or time.monotonic() + .12 > self.survival254_deadline:
            return False
        side = 1 if self.game.pid < self.cooperative.peer.pid else -1
        keys = previous.survival.movement(state, root, side)
        self.write('thorn_low_defence', ownHp=state['hp'], peerHp=peer['hp'],
                   own=state['playerPos'], peer=peer['playerPos'], root=root,
                   motion=head['motion'], direction=sorted(keys), pulseSec=.12)
        try:
            self.kb.hold(keys)
            time.sleep(.12)
        finally:
            self.kb.hold(set())
        return True


class SurvivalPadMixin:
    def tap(self, action, ms=90):
        p = getattr(self, 'head_owner', None)
        if action == 'attack' and p is not None and getattr(p, 'survival254_active', False):
            repair = getattr(self, 'heal_repair_hook', None)
            if repair is not None and not repair():
                self.hold(set())
                return False
            facts = p.survival_facts(p.state())
            if facts is not None and facts[2].get('motion') == 202 \
                    and facts[0]['hp'] <= .4 * facts[0]['maxHp']:
                self.hold(set())
                p.write('thorn_low_defence_swing', sent=False, reason='wounded-202-mid-burst')
                return False
        return super().tap(action, ms)


class SurvivalCooperativeMixin:
    def step(self, pilot, own, event_reaction=False):
        if not event_reaction and getattr(pilot, 'survival254_active', False) and pilot.survival_turn(own):
            return True
        return super().step(pilot, own, event_reaction)


class Pad(SurvivalPadMixin, previous.Pad):
    pass


class Pilot(SurvivalPilotMixin, previous.Pilot):
    pass


class CooperativeFight(SurvivalCooperativeMixin, previous.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
