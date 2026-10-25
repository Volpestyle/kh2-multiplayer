"""VUH-2617 lead #343: keep the fight's last Potion for a rescue, over thorn_protect (r5 and the hold-off kept).

In zr98-123, 9 of the 11 rescuers that went down within 20 s of a revive window started it under 40% HP. Four of those
had no Potion left for the pre-revive heal; in zr102 and zr110 the fight's only Potion had gone to the post-revive
grace heal (.local/review/vuh2617-rescuer-loss). Opt-in per fight step:

  "rescuePotionReserve": true

While this seat holds its fight's last Potion (at most one Potion among its item slots), that Potion is not available
to a heal unless the partner is logically downed (and this seat is not). Ordinary, opening and post-revive-grace heals
see it as unavailable, exactly as they see an empty slot; the pre-revive heal, whose start and key-1 checks both run
while the partner is down, still uses it. Every heal keeps its own checks and caps (heal_available's slot-0 proof,
Potions used and menu misses); this only adds one refusal. Input-only, no native writes.
"""
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_protect as child  # noqa: E402

base, previous = child.base, child.previous
LOG_GAP = 5.0  # s between `potion_reserved` rows while the reservation keeps refusing


class PotionReservePilotMixin:
    potion_reserve = False
    potion_reserved_at = None

    def fight(self, step, deadline):
        self.potion_reserve = bool(step.get('rescuePotionReserve')) and bool(step.get('eventReactions'))
        self.potion_reserve_stats = dict(refusals=0, rescueAllowed=0)
        try:
            return super().fight(step, deadline)
        finally:
            if self.potion_reserve:
                self.write('rescue_potion_reserve_summary', **self.potion_reserve_stats)
            self.potion_reserve = False

    def potion_rescue(self):
        """The partner is logically downed and this seat is not: a heal now is the rescue's."""
        coop = getattr(self, 'cooperative', None)
        return coop is not None and bool(coop.peer_log.downed) and not coop.own_log.downed \
            and not coop.own_log.full_party and not coop.peer_log.full_party

    def heal_available(self, state):
        available = super().heal_available(state)
        if not available or not self.potion_reserve:
            return available
        items = self.observe_items(state)
        if not items.get('known') or items.get('potionSlots', 0) > 1:
            return available
        if self.potion_rescue():
            self.potion_reserve_stats['rescueAllowed'] += 1
            return True
        self.potion_reserve_stats['refusals'] += 1
        now = time.monotonic()
        if self.potion_reserved_at is None or now - self.potion_reserved_at >= LOG_GAP:
            self.potion_reserved_at = now
            self.write('potion_reserved', hp=state.get('hp'), maxHp=state.get('maxHp'),
                       potionSlots=items.get('potionSlots'), refusals=self.potion_reserve_stats['refusals'])
        return False


class Pilot(PotionReservePilotMixin, child.Pilot):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, child.Pad, Pilot
    base.CooperativeFight = child.CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
