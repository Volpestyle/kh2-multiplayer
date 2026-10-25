"""Composed input-only child. No c1/bisect files or native code change.

Linden owns HealPad/HealPilot; Quill owns head observation and bounded selection
of a pre-revive attempt. There is one tagged-input guard, installed by HealPilot.
Deferred repair wraps the reviewed heal/head pads. Command-row readback is
calibrated; submenu backout remains counted. Native hit/reach efficacy is unproven.
"""
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn
import thorn_head as head
import thorn_heal2 as heal
import thorn_heal_defer as defer
import next_policy as rules

base = thorn.base
Game, k32, psapi = thorn.Game, thorn.k32, thorn.psapi


class Pad(defer.DeferredRepairPadMixin, heal.HealPadMixin, head.HeadPadMixin, thorn.Pad):
    pass


class PreReviveCooperativeMixin:
    def step(self, pilot, own, event_reaction=False):
        if getattr(pilot, 'head_active', False) and pilot.next_options.get('healBeforeRevive', False):
            own_ok, peer_ok = self.own_log.update(), self.peer_log.update()
            if own_ok and peer_ok and self.peer_log.downed and not event_reaction and not own.get('react'):
                episode = (tuple(own['roomScope']), own['player'], self.peer_log.actor, self.peer_log.downs)
                if episode not in pilot.preheal_attempted and self.hold_started is None and not self.waiting_release:
                    items = pilot.observe_items(own)
                    reason = rules.preheal_reason(own, items, own_down=self.own_log.downed,
                        peer_down=True, full_party=self.own_log.full_party or self.peer_log.full_party,
                        logs_known=True, episode=episode, expected=episode, used=pilot.heal_used,
                        limit=(pilot.heal_cfg or {}).get('max', 3))
                    if not pilot.heal_available(own) or pilot.preheal_aborts >= 4:
                        reason = 'common-heal-cap-or-unavailable'
                    if self.since is not None and time.monotonic()-self.since >= self.TIMEOUT:
                        reason = 'original-revive-timeout'
                    if reason is None and pilot.next_options.get('healBeforeRevive', False):
                        pilot.preheal_attempted.add(episode)
                        if self.since is None:
                            self.since = time.monotonic()
                        self.stop(pilot, 'pre-revive-heal', own)
                        pilot.pre_revive_heal(own)
                        return True  # refresh before the original revive approach
                    if episode not in pilot.preheal_logged:
                        pilot.preheal_logged.add(episode)
                        pilot.write('pre_revive_heal', stage='skip', reason=reason or 'option-off', items=items)
        return super().step(pilot, own, event_reaction)


class CooperativeFight(PreReviveCooperativeMixin, head.HeadCooperativeMixin, thorn.CooperativeFight):
    pass


class Pilot(defer.DeferredRepairPilotMixin, heal.HealPilotMixin, head.HeadPilotMixin, thorn.Pilot):
    def pre_revive_heal(self, start):
        """One bounded attempt through the SAME tagged pad/guard as ordinary heals.

        No independent guard, cursor restore, heal counters or revive timeout.
        The outer guard binds the exception to the exact downing and rechecks it.
        """
        pad = self.kb
        cfg = self.heal_cfg
        until = min(time.monotonic()+5.0, self.preheal_deadline)
        grant = heal.episode(self.cooperative, start)
        self.heal_peer_exception = grant
        self.write('pre_revive_heal', stage='begin', episode=grant, hp=start['hp'], items=self.observe_items(start))
        started = False

        def expired():
            if time.monotonic() < until:
                return False
            if not pad.heal_aborted:
                pad.heal_aborted = True
                self.write('heal_guard', reason='preheal-input-deadline', preRevive=True)
                pad.hold(set())
                if started:
                    pad.heal_restore('preheal-input-deadline')  # same bounded, Cross-free repair
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
                time.sleep(max(0, min(0.25, until-time.monotonic())))
            if not pad.heal_aborted:
                time.sleep(max(0, min(1.5, until-time.monotonic())))
            after = self.state()
            self.write('heal', hp=start['hp'], after=after.get('hp'), preRevive=True)
            if pad.heal_aborted:
                self.preheal_aborts += 1
            elif not (after.get('hp') and after['hp'] > start['hp']):
                for key in cfg.get('reset', ['SPACE', 'SPACE']):
                    if expired():
                        break
                    pad.tap(f'{heal.RESET}{key}')
                    if pad.heal_aborted:
                        break
                    time.sleep(max(0, min(0.3, until-time.monotonic())))
        finally:
            pad.hold(set())
            self.heal_peer_exception = None
            # The normal next heal's KEY0 starts a fresh baseline.


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
