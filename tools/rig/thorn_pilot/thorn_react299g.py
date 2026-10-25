"""Lead #249 diagnostic child: react299 (natural, prompt-transition rows) plus the read-only RC gate sampler.

Behavior is thorn_react299.py unchanged; thorn_rcgate.RcGateMixin only adds `rc_gate` rows during Thorn-room events.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_react299 as child
import thorn_rcgate as rcgate

base, previous = child.base, child.previous


class CooperativeFight(rcgate.RcGateMixin, child.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = previous.Game, previous.Pad, previous.Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
