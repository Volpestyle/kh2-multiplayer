"""VUH-2617 lead #282: Creeper-first targeting, over the natural react299g pilot (rc_gate sampler kept).

Opt-in per fight step: only a step carrying `creeperFirst` (the host route's) changes behavior; every other step and
the friend's route run thorn_react299g unchanged. Input-only: native lock-on/attack/move requests through the existing
pad mailbox; no native writes. Rules are in creeper_policy.py.

  "creeperFirst": {"engage": 800, "reach": 170, "chaseSec": 5, "engageSec": 20, "coolSec": 6, "pollSec": 0.3}
"""
import math
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_react299g as child
import creeper_policy as rules

base, previous = child.base, child.previous


class CreeperPilotMixin:
    creeper_cfg = None

    def fight(self, step, deadline):
        cfg = step.get('creeperFirst') if step.get('eventReactions') else None
        self.creeper_cfg = {} if cfg is True else dict(cfg) if isinstance(cfg, dict) else None  # {} = defaults
        self.creeper_foes_at, self.creeper_foes = 0.0, []
        self.creeper_engage = None         # dict(start, swing) for the current engagement
        self.creeper_cool_all = 0.0        # every Creeper cools until then, after an engagement times out
        self.creeper_unlock_at = 0.0
        self.creeper_row = (None, 0.0)     # (action, address), last written
        self.creeper_stats = dict(unlock=0, approach=0, attack=0, swings=0, chaseTimeouts=0, seen={})
        try:
            return super().fight(step, deadline)
        finally:
            if self.creeper_cfg is not None:
                self.write('creeper_summary', **{k: v for k, v in self.creeper_stats.items() if k != 'seen'},
                           creepers=self.creeper_stats['seen'])
            self.creeper_cfg = None

    def creeper_poll(self):
        now = time.monotonic()
        if now - self.creeper_foes_at >= float(self.creeper_cfg.get('pollSec', 0.3)):
            self.creeper_foes = base.enemies(self.game.pid, timeout=2)
            self.creeper_foes_at = now
            for f in rules.creepers(self.creeper_foes):
                seen = self.creeper_stats['seen'].setdefault(f['address'], dict(firstHp=f['hp'], lastHp=f['hp']))
                seen['lastHp'] = f['hp']
        return self.creeper_foes

    def creeper_handles(self, foes):
        reader = getattr(self, 'head_reader', None)
        if reader is None:
            return {}
        members = reader.members()
        return {f['address']: members.get(int(str(f['address']), 16)) for f in rules.creepers(foes)}

    def creeper_turn(self, s):
        cfg = self.creeper_cfg
        if cfg is None or (s.get('world'), s.get('room')) != (2, 34):
            return False
        c = self.cooperative
        known = c is not None and c.own_log.update() and c.peer_log.update()
        own_down = not known or c.own_log.downed or c.own_log.full_party
        peer_down = not known or c.peer_log.downed or c.peer_log.full_party
        # Everything decidable without an observer call comes first: an offered RC, a downed seat or an unknown log
        # never waits on the entity/head reads (Sage CHANGES #286).
        if rules.priority_yield(s, own_down=own_down, peer_down=peer_down, logs_known=known):
            return False
        now = time.monotonic()
        if now < self.creeper_cool_all:
            return False
        try:
            foes = self.creeper_poll()
            head = self.observe_head(s) if getattr(self, 'head_reader', None) else {}
            handles = self.creeper_handles(foes) if head.get('known') else {}
        except (OSError, ValueError, subprocess.TimeoutExpired):
            return False
        lock = dict(mode=head.get('lockMode'), handle=head.get('lockHandle')) if head.get('known') else None
        engage, reach = float(cfg.get('engage', 800)), float(cfg.get('reach', 170))
        action, target = rules.creeper_action(
            s, foes, own_down=own_down, peer_down=peer_down, logs_known=known, head_motion=head.get('motion'),
            lock=lock, handles=handles, engage=engage, reach=reach)
        if action == 'yield':
            if not any(rules.horizontal(s['playerPos'], f) <= engage for f in rules.creepers(foes)):
                self.creeper_engage = None  # no Creeper near: the engagement is over
            return False
        # One bound per engagement, not per Creeper (Sage CHANGES #286/#287): neither alternating targets nor a vetoed
        # attack intent renews it. Only a swing actually sent renews the progress timer; the whole engagement is capped
        # as well, so swings that never kill cannot keep the host off Thorn either. Either limit cools every Creeper.
        if self.creeper_engage is None:
            self.creeper_engage = dict(start=now, swing=now)
        idle, total = now - self.creeper_engage['swing'], now - self.creeper_engage['start']
        if idle > float(cfg.get('chaseSec', 5)) or total > float(cfg.get('engageSec', 20)):
            self.creeper_cool_all = now + float(cfg.get('coolSec', 6))
            reason = 'acquire' if idle > float(cfg.get('chaseSec', 5)) else 'engagement'
            self.creeper_engage = None
            self.creeper_stats['chaseTimeouts'] += 1
            self.write('creeper_chase_timeout', target=target['address'], hp=target['hp'], phase=action, reason=reason,
                       idle=round(idle, 2), total=round(total, 2))
            return False
        address = target['address']
        point = s['playerPos']
        gap = rules.horizontal(point, target)
        if (action, address) != self.creeper_row[0] or now - self.creeper_row[1] >= 0.5:
            self.write('creeper_turn', action=action, target=address, hp=target['hp'], gap=round(gap, 1),
                       lock=lock, ownHp=s.get('hp'))
            self.creeper_row = ((action, address), now)
        self.creeper_stats[action] += 1
        if action == 'unlock':
            self.kb.hold(set())
            if now - self.creeper_unlock_at >= 1.0:  # R1 is a request; read the lock back next tick
                self.creeper_unlock_at = now
                self.kb.tap('lockon', 100)
            time.sleep(0.1)
            return True
        if action == 'approach':
            tx, tz = target['position']['x'], target['position']['z']
            self.kb.hold(base.steer({**s, 'pos': point}, (tx, tz)))
            time.sleep(0.1)
            return True
        if gap > 60:  # one short facing pulse; unlocked swings go where Roxas faces
            self.kb.hold(base.steer({**s, 'pos': point}, (target['position']['x'], target['position']['z'])))
            time.sleep(0.06)
        self.kb.hold(set())
        for i in range(3):  # the base loop's burst; every later swing rechecks the same priorities first
            if i:
                now_state = self.state()
                fresh = c.own_log.update() and c.peer_log.update()
                if rules.priority_yield(now_state, logs_known=fresh,
                                        own_down=not fresh or c.own_log.downed or c.own_log.full_party,
                                        peer_down=not fresh or c.peer_log.downed or c.peer_log.full_party):
                    break
            if self.kb.tap('attack', 70) is False:  # the inherited pad vetoes still apply to every swing
                break
            self.creeper_stats['swings'] += 1
            if self.creeper_engage is not None:
                self.creeper_engage['swing'] = time.monotonic()  # progress: a swing was sent
            time.sleep(0.18)
        return True


class CreeperCooperativeMixin:
    def step(self, pilot, own, event_reaction=False):
        if not event_reaction and getattr(pilot, 'creeper_cfg', None) is not None and pilot.creeper_turn(own):
            return True
        return super().step(pilot, own, event_reaction)


class Pilot(CreeperPilotMixin, previous.Pilot):
    pass


class CooperativeFight(CreeperCooperativeMixin, child.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, previous.Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
