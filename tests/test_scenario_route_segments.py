"""Route checkpoints (VUH-2253): a failure after a checkpoint ends only its segment."""
import ast
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time
import traceback
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


class StepFailed(Exception):
    pass


class InstanceDied(Exception):
    def __init__(self, kind):
        super().__init__(kind)
        self.kind = kind


class RouteSegmentsTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('KH2COOP_TEST_OUTPUT_DIR'))
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.ran = []

        class Context:
            def __init__(self, run_dir):
                self.run_dir = run_dir
                self.instances = []
                self.artifacts = []
                self.protect = set()
                self.protected_actors = {}  # run_scenario reports what protect covered (VUH-2364)
                self.player_env = {}

            def check_all(self):
                pass

            def close(self):
                pass

        tree = ast.parse((ROOT / 'tools/scenario/run.py').read_text())
        tree.body = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'run_scenario']
        self.env = dict(Context=Context, Path=Path, StepFailed=StepFailed, InstanceDied=InstanceDied, ROOT=ROOT,
                        RUNS=self.directory / 'runs', LOGS=self.directory / 'logs', json=json, dt=dt, time=time,
                        traceback=traceback, shutil=shutil, os=os, sys=sys, kh2ctl=lambda *a, **k: {},
                        link_desync_reports=lambda _: dict(reports=[], suppressionSummaries=[]),
                        render_md=lambda _: 'stub report', bundle=lambda *a: [])
        exec(compile(tree, '<isolated route segments>', 'exec'), self.env)

        def ok(ctx, step):
            self.ran.append(step.get('id'))
            return {}

        def bad(ctx, step):
            self.ran.append(step.get('id'))
            raise StepFailed(f"broke at {step.get('id')}")

        def crash(ctx, step):
            raise InstanceDied('exited')

        self.env['STEPS'] = {'ok': ok, 'bad': bad, 'crash': crash, 'checkpoint': lambda ctx, step: {'name': step['name']}}

    def run_steps(self, steps, **scenario):
        path = self.directory / 'scenario.json'
        path.write_text(json.dumps({'name': 'route', 'steps': steps, **scenario}))
        return self.env['run_scenario'](path, 1)

    def test_setup_failure_before_any_checkpoint_ends_the_run(self):
        report = self.run_steps([{'do': 'bad', 'id': 'boot'}, {'do': 'checkpoint', 'name': 'A'}, {'do': 'ok', 'id': 'a1'}])
        self.assertEqual(report['status'], 'fail')
        self.assertEqual(self.ran, ['boot'])
        self.assertEqual(report['route']['entered'], 0)

    def test_segment_failure_skips_to_next_checkpoint_and_ranks_failures(self):
        report = self.run_steps([
            {'do': 'ok', 'id': 'boot'},
            {'do': 'checkpoint', 'name': 'A'}, {'do': 'ok', 'id': 'a1'},
            {'do': 'checkpoint', 'name': 'B'}, {'do': 'bad', 'id': 'b1'}, {'do': 'ok', 'id': 'b2'},
            {'do': 'checkpoint', 'name': 'C'}, {'do': 'ok', 'id': 'c1'},
            {'do': 'checkpoint', 'name': 'D'}, {'do': 'bad', 'id': 'd1'}])
        self.assertEqual(self.ran, ['boot', 'a1', 'b1', 'c1', 'd1'])
        self.assertEqual(report['status'], 'fail')
        self.assertIn('broke at b1', report['error'])
        route = report['route']
        self.assertEqual((route['segments'], route['entered'], route['passed']), (4, 4, 2))
        self.assertEqual((route['reached'], route['furthestPass']), ('D', 'C'))
        self.assertEqual([f['segment'] for f in route['failures']], ['B', 'D'])
        self.assertEqual([s['status'] for s in report['steps'] if s['do'] == 'ok' and s['index'] == 5], ['skipped'])

    def test_crash_ends_the_run_and_marks_its_segment(self):
        report = self.run_steps([{'do': 'checkpoint', 'name': 'A'}, {'do': 'crash'},
                                 {'do': 'checkpoint', 'name': 'B'}, {'do': 'ok', 'id': 'b1'}])
        self.assertEqual(report['status'], 'crash')
        self.assertEqual(self.ran, [])
        self.assertEqual(report['route']['failures'][0]['status'], 'crash')
        self.assertEqual(report['route']['entered'], 1)

    def test_player_defaults_refuses_developer_variables(self):
        with mock.patch.dict(os.environ, {'KH2COOP_PUPPET_TRACE': '1'}):
            report = self.run_steps([{'do': 'ok', 'id': 'boot'}], playerDefaults=True)
        self.assertEqual(report['status'], 'fail')
        self.assertIn('KH2COOP_PUPPET_TRACE', report['error'])
        self.assertEqual(self.ran, [])

    def test_player_defaults_applies_the_package_policy(self):
        with mock.patch.dict(os.environ, {k: v for k, v in os.environ.items() if not k.startswith('KH2COOP_')}, clear=True):
            report = self.run_steps([{'do': 'ok', 'id': 'boot'}], playerDefaults=True)
        self.assertEqual(report['status'], 'pass')
        self.assertEqual(report['playerEnv']['KH2COOP_ENEMY_MIRROR'], '1')
        self.assertEqual(report['playerEnv']['KH2COOP_ENEMY_POPULATION_SPAWN'], '0')


if __name__ == '__main__':
    unittest.main()
