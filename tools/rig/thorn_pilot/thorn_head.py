"""Head-only mixins: external reads, input and telemetry; no heal guard.

Compose inside Linden's HealPad/HealPilot. The head branch leaves every tagged
heal key, reset, Triangle and tutorial/menu confirm to its existing owner.
"""
import math
import time
import thorn
import next_policy as rules
from head_observation import Reader

base = thorn.base


class HeadPadMixin:
    head_owner = None
    def tap(self, action, ms=90):
        p = self.head_owner
        if p is None or not getattr(p, 'head_active', False):
            return super().tap(action, ms)
        if action == 'attack':
            s = p.state()
            head = p.head_reader.head(s)
            own_ok = p.cooperative is not None and p.cooperative.own_log.update()
            peer_ok = p.cooperative is not None and p.cooperative.peer_log.update()
            own_down = not own_ok or p.cooperative.own_log.downed or p.cooperative.own_log.full_party
            peer_down = not peer_ok or p.cooperative.peer_log.downed or p.cooperative.peer_log.full_party
            decision = rules.head_action(s, head, own_down=own_down, peer_down=peer_down,
                                         logs_known=own_ok and peer_ok)
            allowed = decision == 'attack' if p.head_mode else not (
                own_down or peer_down or not s.get('free') or s.get('react'))
            p.swing_seq += 1
            p.write('thorn_swing', swing=p.swing_seq, sent=allowed, control='head' if p.head_mode else 'legacy',
                    player=s.get('player'),playerPos=head.get('playerPos',s.get('playerPos')),hp=s.get('hp'),scope=s.get('roomScope'),
                    nativeFrame=s.get('reactObservation',{}).get('frame'),
                    targetPart=head.get('targetPart'), lockedHead=head.get('lockedHead'),
                    inReachEstimate=rules.reach(s,head), nativeCanHit=None,
                    distance3D=math.dist(head.get('playerPos',s['playerPos']),head['headPos'])
                    if head.get('known') and s.get('playerPos') else None,
                    bladeReachEstimate=220, head=head, decision=decision)
            if not allowed:
                self.hold(set())
                return False
        return super().tap(action, ms)


class HeadCooperativeMixin:
    def step(self, pilot, own, event_reaction=False):
        consumed = super().step(pilot, own, event_reaction)
        if consumed or not getattr(pilot, 'head_active', False) or event_reaction:
            return consumed
        return pilot.head_turn(own) if pilot.next_options.get('head', False) else False


class HeadPilotMixin:
    def observe_items(self, state, stage='proof'):
        # Also callable before this mixin's fight() entry, by the outer heal mixin.
        reader = getattr(self, 'head_reader', None) or Reader(self.game)
        result = reader.items(state)
        if getattr(self, 'head_active', False):
            self.write('item_slots', stage=stage, items=result, hp=state.get('hp'))
        return result

    def observe_head(self, state):
        return self.head_reader.head(state)

    def state(self):
        s = super().state()
        if getattr(self, 'head_active', False) and time.monotonic()-self.item_at >= 1.0:
            self.item_at = time.monotonic()
            self.observe_items(s, stage='observe')
        return s

    def write(self, kind, **fields):
        if getattr(self, 'head_active', False) and kind == 'heal':
            try:
                fields['itemsAfter'] = self.observe_items(self.game.state(), stage='heal-end')
            except (OSError, ValueError):
                fields['itemsAfter'] = dict(known=False, reason='post-heal-state-unavailable')
        return super().write(kind, **fields)

    def fight(self, step, deadline):
        self.next_options = step.get('nextSeries', {})
        self.head_active = bool(self.next_options) and bool(step.get('eventReactions'))
        self.head_reader, self.item_at = Reader(self.game), 0.0
        self.head_mode, self.swing_seq = False, 0
        self.lock_tries, self.lock_at, self.dazed_jump = 0, 0.0, False
        self.approach_since = None
        self.preheal_attempted, self.preheal_logged = set(), set()
        self.preheal_aborts = 0
        self.preheal_deadline = deadline
        if self.head_active and (not isinstance(self.kb, HeadPadMixin) or self.cooperative is None):
            raise base.Stuck('next series requires existing linked pad pilots')
        if self.next_options.get('healBeforeRevive') and not getattr(self, 'heal_cfg', None):
            raise ValueError('pre-revive heal requires the outer reviewed HealPilot mixin')
        self.kb.head_owner = self
        try:
            return super().fight(step, deadline)
        finally:
            self.head_active = False
            self.kb.head_owner = None

    def head_turn(self,s):
        c = self.cooperative
        known = c.own_log.update() and c.peer_log.update()
        head = self.observe_head(s)
        action = rules.head_action(s,head,own_down=c.own_log.downed or c.own_log.full_party,
                peer_down=c.peer_log.downed or c.peer_log.full_party,logs_known=known)
        if head.get('motion') not in (209,210,211):
            self.dazed_jump = False
        if action == 'yield':
            self.approach_since = None
            return False
        if action == 'lock':
            # R1 is an input request, never a target write. Read back the next tick.
            if self.lock_tries >= 4:
                return False
            if time.monotonic()-self.lock_at >= 1.0:
                self.lock_at = time.monotonic()
                self.lock_tries += 1
                self.kb.hold(set())
                self.kb.tap('lockon',100)
                self.write('thorn_head_lock',attempt=self.lock_tries,head=head)
            return True
        if action == 'approach':
            self.approach_since = self.approach_since or time.monotonic()
            if time.monotonic()-self.approach_since > 3.0:
                return False  # no endless chase of a high head; original RC path continues
            h,p = head['targetPos'],head.get('playerPos',s['playerPos'])
            horizontal = math.hypot(h[0]-p[0],h[2]-p[2])
            self.kb.hold(base.steer({**s,'pos':p},(h[0],h[2])) if horizontal > 100 else set())
            self.write('thorn_head_approach',head=head,horizontal=horizontal,inReachEstimate=False)
            time.sleep(0.1)
            return True
        self.approach_since = None
        self.kb.hold(set())
        if head['motion'] in (209,210,211) and not self.dazed_jump:
            self.dazed_jump = True
            self.kb.tap('jump',80)  # one short jump, then fresh per-swing qualification
            time.sleep(0.1)
        self.head_mode = True
        try:
            self.kb.tap('attack',70)
        finally:
            self.head_mode = False
        time.sleep(0.18)
        return True
