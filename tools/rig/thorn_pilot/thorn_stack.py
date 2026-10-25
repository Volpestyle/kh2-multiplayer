"""UNRUN lead244 stack: capped survival Pad/Pilot plus adopted approach revision."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_survival as survival
import thorn_approach as approach
import thorn_next

base, thorn = survival.base, survival.thorn
Game, k32, psapi, Pad, Pilot = survival.Game, survival.k32, survival.psapi, survival.Pad, survival.Pilot


class CooperativeFight(survival.ReserveCooperativeMixin, approach.ApproachRevisionMixin,
                       thorn_next.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = thorn.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
