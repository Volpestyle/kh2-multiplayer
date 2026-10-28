"""Delayed Bailey waves: exercise the real fight step without rig imports or input."""
import ast
import json
import math
from pathlib import Path
import subprocess
import tempfile
import threading
import time
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
PLAYER = '0x1000'  # the fake player actor's address, as kh2ctl peek and actors report it


class StepFailed(Exception):
    pass


class FightTest(unittest.TestCase):
    def run_fight(self, seconds, until=None, birth_at=40, player='P_EX100', clips=None, encode=None):
        # Virtual time advances only at simulated input/wait boundaries. Real
        # threads yield cooperatively; no wall-clock thresholds decide the test.
        clock_lock = threading.Lock()
        now = 0.0

        def monotonic():
            with clock_lock:
                return now

        def sleep(dt):
            nonlocal now
            with clock_lock:
                now += dt
            time.sleep(0.001)

        class Context:
            def __init__(self, directory):
                self.run_dir = directory
                self.saved = {}
                self.artifacts = []
                self.observed = []

            def inst(self, index):
                return types.SimpleNamespace(pid=100 + index, index=index)

            def actors(self, index):
                t = monotonic()
                # At 30s, the real run still had a Dusk318 at45HP. Include
                # an empty wave gap before the later living Creeper + receipt.
                foes = [] if birth_at - 3 <= t < birth_at else [
                    dict(name='M_DUSK' if t < birth_at else 'M_CREEPER', address='0x2000',
                         objectId=318 if t < birth_at else 317, objectType=4,
                         hp=45, position=dict(x=1000, z=0))]
                return [dict(name=player, address=PLAYER, position=dict(x=0, z=0)), *foes]

            def eval(self, expr):
                self.observed.append(monotonic())
                return eval(expr, {'__builtins__': {}}, {
                    'log_matches': lambda *_: [{'line': 'ticket objectId=317'}] if monotonic() >= birth_at else [],
                    'enemies': lambda i: self.actors(i)[1:], 'len': len, 'any': any})

        def kh2ctl(command, *args, **kwargs):
            if command == 'clip':  # recording and encoding: held until the test releases it
                encode.wait(10)
                return {'ok': True}
            if command == 'peek':
                # The step finds its game's canonical player through 0x2A105D0 (VUH-2466).
                return {'samples': [{'0x718C68': 0, '0x718C70': 1, '0x718C78': 0, '0x718C80': 0, '0x2A105D0': PLAYER}]}
            self.assertEqual(command, 'player-input')
            sleep(1)
            return {}

        tree = ast.parse((ROOT / 'tools/scenario/run.py').read_text(encoding='utf-8'))
        tree.body = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'step_fight']
        env = dict(Context=Context, threading=threading, time=types.SimpleNamespace(monotonic=monotonic, sleep=sleep),
                   math=math, kh2ctl=kh2ctl, StepFailed=StepFailed, subprocess=subprocess, Path=Path, json=json,
                   clip_receipt=lambda ctx, out, result: {**result, 'path': out.name})
        exec(compile(tree, '<real fight step>', 'exec'), env)
        with tempfile.TemporaryDirectory() as directory:
            ctx = Context(Path(directory))
            ctx.sleep = sleep
            step = dict(instances=[0, 1], seconds=seconds)
            if until is not None:
                step['until'] = until
            if clips is not None:
                step['clips'] = clips
            result = env['step_fight'](ctx, step)
            self.assertTrue(all(result[str(i)]['pulses'] > 0 and 'error' not in result[str(i)] for i in range(2)))
            self.saved = ctx.saved
            return result, monotonic(), ctx.observed

    def milestone(self):
        fixture = json.loads((ROOT / 'tools/scenario/scenarios/net_hb1_bailey.json').read_text(encoding='utf-8'))
        step = next(s for s in fixture['steps'] if s.get('as') == 'bailey_fight_before_pause')
        self.assertEqual(step['seconds'], 120)
        return step['until']

    def test_old_30s_segment_stops_in_the_first_wave(self):
        _, elapsed, _ = self.run_fight(30)
        self.assertGreaterEqual(elapsed, 30)
        self.assertLess(elapsed, 40)

    def test_delayed_creeper_ticket_stops_combat_after_wave_gap(self):
        result, elapsed, observations = self.run_fight(120, self.milestone())
        self.assertTrue(result['until']['reached'])
        self.assertTrue(any(t < 30 for t in observations))
        self.assertGreaterEqual(elapsed, 40)
        self.assertLess(elapsed, 120)

    def test_a_prologue_roxas_is_driven_as_the_player(self):
        # VUH-2466: both prologue seats' players are P_EX110; the step finds its own through 0x2A105D0, not by name.
        result, elapsed, _ = self.run_fight(30, player='P_EX110')
        self.assertGreaterEqual(elapsed, 30)

    # VUH-2912 (vuh2912-hp-06): a held clip kept the round open past its milestone until it encoded; a background
    # clip ends the step at the milestone and is left for clip_wait.
    def film(self, background):
        encode = threading.Event()
        clips = [dict(instance=i, seconds=20, fps=30, name=f'round_{i}', background=background) for i in (0, 1)]
        done = {}
        runner = threading.Thread(target=lambda: done.update(out=self.run_fight(120, self.milestone(), clips=clips,
                                                                                encode=encode)), daemon=True)
        runner.start()
        runner.join(2)
        returned = not runner.is_alive()  # before any clip finished encoding
        encode.set()
        runner.join(10)
        return returned, done['out'][0], self.saved

    def test_background_clips_end_the_fight_at_its_milestone(self):
        returned, result, saved = self.film(True)
        self.assertTrue(returned)
        self.assertTrue(result['until']['reached'])
        self.assertEqual(result['backgroundClips'], ['round_0_0.mp4', 'round_1_1.mp4'])
        self.assertNotIn('clips', result)
        self.assertEqual([out.name for _, out, _ in saved['_background_clips']], ['round_0_0.mp4', 'round_1_1.mp4'])

    def test_held_clips_still_hold_the_fight(self):  # the control: without background the step waits for encoding
        returned, result, saved = self.film(False)
        self.assertFalse(returned)
        self.assertEqual([c['path'] for c in result['clips']], ['round_0_0.mp4', 'round_1_1.mp4'])
        self.assertNotIn('_background_clips', saved)

    def test_missing_ticket_still_stops_at_the_deadline(self):
        result, elapsed, _ = self.run_fight(120, self.milestone(), birth_at=1000)
        self.assertFalse(result['until']['reached'])
        self.assertGreaterEqual(elapsed, 120)
        self.assertLess(elapsed, 130)


if __name__ == '__main__':
    unittest.main()
