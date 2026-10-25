"""Lead #248 diagnostic child: the natural s254b pilot with force299's prompt-transition rows and no forcing.

Every change of the prompt reader's react value during an event, at the fight loop's 0.1 s cadence, on both seats,
is written as a `force299_react` row (same fields as thorn_force299.py, capped at 400), so a natural run shows
whether a 297..299 prompt appeared and when the pilot pressed it. Movement, reactions and revive are unchanged.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thorn_survival254b as previous

base = previous.base
Game, Pad, Pilot = previous.Game, previous.Pad, previous.Pilot


class ReactLog299Mixin:
    def step(self, pilot, own, event_reaction=False):
        react, event = own.get('react'), own.get('event')
        prev = getattr(self, 'force299_react', None)
        if event and react != prev and (react or prev) and getattr(self, 'force299_react_rows', 0) < 400:
            self.force299_react_rows = getattr(self, 'force299_react_rows', 0) + 1
            pilot.write('force299_react', react=react, previous=prev, eventReaction=bool(event_reaction),
                        free=own.get('free'), menu=own.get('menu'), source=(own.get('reactObservation') or {}).get('source'))
        self.force299_react = react if event else None
        return super().step(pilot, own, event_reaction)


class CooperativeFight(ReactLog299Mixin, previous.CooperativeFight):
    pass


def main():
    base.PromptReactionReader = previous.PromptReader
    base.Game, base.PadInput, base.Pilot = Game, Pad, Pilot
    base.CooperativeFight = CooperativeFight
    return base.main()


if __name__ == '__main__':
    sys.exit(main())
