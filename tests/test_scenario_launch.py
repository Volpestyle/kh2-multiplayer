"""Launch/cleanup boundary controls; AST extraction excludes live setup/imports."""
import ast
import datetime as dt
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import traceback
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StepFailed(Exception):
    pass


class InstanceDied(Exception):
    pass


class LaunchTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('KH2COOP_TEST_OUTPUT_DIR'))
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.calls = []
        self.reply = dict(ok=True, command='launch', processId=795000,
                          hooksInstalled=True, log=str(self.directory / 'native.log'))
        owner = self

        class Context:
            def __init__(self, run_dir):
                self.run_dir = run_dir
                self.instances = []
                self.artifacts = []
                self.protect = set()
                self.closed = False
                owner.context = self

            def check_all(self):
                pass

            def close(self):
                self.closed = True

        names = {'step_launch', 'step_boot', 'run_scenario', 'validate_scenario'}
        tree = ast.parse((ROOT / 'tools/scenario/run.py').read_text())
        tree.body = [node for node in tree.body if
                     isinstance(node, ast.FunctionDef) and node.name in names or
                     isinstance(node, ast.ClassDef) and node.name == 'Instance']
        self.env = dict(Context=Context, Path=Path, StepFailed=StepFailed,
                        InstanceDied=InstanceDied, ROOT=ROOT, RUNS=self.directory / 'runs',
                        LOGS=self.directory / 'logs', kh2ctl=self.cli, json=json, dt=dt,
                        time=time, traceback=traceback, shutil=shutil,
                        link_desync_reports=lambda _: dict(reports=[], suppressionSummaries=[]),
                        render_md=lambda _: 'stub report',
                        wait_for=lambda *a: self.fail('failed launch entered title/load'),
                        title_rows=lambda *a: self.fail('failed launch captured title'),
                        press=lambda *a: self.fail('failed launch entered input'))
        exec(compile(tree, '<isolated launch boundary>', 'exec'), self.env)
        self.env['STEPS'] = {'launch': self.env['step_launch'], 'boot': self.env['step_boot'],
                             'after': lambda *a: self.fail('scenario continued after failed launch')}
        self.ctx = Context(self.directory)

    def cli(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        if args[0] == 'launch':
            self.assertIs(kwargs['check'], False)
            if isinstance(self.reply, BaseException):
                raise self.reply
            return self.reply
        self.assertIn(args[0], ('mute', 'kill'), 'unexpected game operation')
        self.assertIs(kwargs['check'], False)
        return {'ok': True}

    def launch(self, **step):
        return self.env['step_launch'](self.ctx, step)

    def scenario(self, kind='boot', **step):
        path = self.directory / 'scenario.json'
        path.write_text(json.dumps({'name': 'launch-control', 'steps': [dict(do=kind, **step), {'do': 'after'}]}))
        return self.env['run_scenario'](path, 1)

    def test_failed_injected_boot_is_registered_and_killed_without_load(self):
        self.reply.update(ok=False, hooksInstalled=False, errors=[])
        report = self.scenario(initTimeoutMs=45000)
        self.assertEqual(report['status'], 'fail')
        self.assertEqual(report['instances'], [795000])
        self.assertEqual(len(report['steps']), 1)
        self.assertIn("'hooksInstalled': False", report['error'])
        self.assertEqual([args[0] for args, _ in self.calls], ['launch', 'kill'])
        self.assertEqual(self.calls[-1][1]['pid'], 795000)
        self.assertTrue(self.context.closed)
        self.assertEqual(self.context.instances[0].inject_log, Path(self.reply['log']).resolve())

    def test_success_preserves_registration_mute_and_environment(self):
        result = self.launch(env={'KH2COOP_AUTOMATIC_RECOVERY': 0, 'CUSTOM': 'value'})
        self.assertEqual(result, {'processId': 795000, 'instance': 0})
        self.assertEqual(self.ctx.instances[0].inject_log, Path(self.reply['log']).resolve())
        self.assertEqual([args[0] for args, _ in self.calls], ['launch', 'mute'])
        self.assertEqual(self.calls[0], (('launch', '--init-timeout-ms', '15000'),
                         dict(check=False, timeout=120.0, env={'KH2COOP_AUTOMATIC_RECOVERY': '0', 'CUSTOM': 'value'})))
        self.assertEqual(self.calls[1][1]['pid'], 795000)

    def test_optional_mute_and_append_index_stay_unchanged(self):
        existing = object()
        self.ctx.instances.append(existing)
        self.assertEqual(self.launch(mute=False), {'processId': 795000, 'instance': 1})
        self.assertIs(self.ctx.instances[0], existing)
        self.assertEqual(len(self.calls), 1)

    def test_invalid_pid_or_command_never_enters_cleanup_list(self):
        for pid in [None, True, False, 0, -1, 1.0, '795000', 2**32, [], {}]:
            with self.subTest(pid=pid):
                self.reply['processId'] = pid
                with self.assertRaisesRegex(StepFailed, 'invalid launched processId'):
                    self.launch()
                self.assertEqual(self.ctx.instances, [])
        self.reply.update(processId=795000, command='inject')
        with self.assertRaisesRegex(StepFailed, 'processId/command'):
            self.launch()
        self.assertEqual(self.ctx.instances, [])

    def test_non_injected_errors_and_malformed_replies_register_nothing(self):
        for reply in [dict(ok=False, error='CreateProcessW failed'),
                      dict(ok=False, error='injection failed'), None, [], 'not-json',
                      dict(ok=True, command='launch')]:
            with self.subTest(reply=reply):
                self.reply = reply
                with self.assertRaises(StepFailed):
                    self.launch()
                self.assertEqual(self.ctx.instances, [])
        self.assertTrue(all(args[0] == 'launch' for args, _ in self.calls))

    def test_false_or_nonboolean_ok_never_mutes_or_loads(self):
        for ok in [False, None, 0, 1, 'true']:
            with self.subTest(ok=ok):
                self.ctx.instances.clear()
                self.reply['ok'] = ok
                with self.assertRaises(StepFailed):
                    self.env['step_boot'](self.ctx, {})
                self.assertEqual([inst.pid for inst in self.ctx.instances], [795000])
        self.assertTrue(all(args[0] == 'launch' for args, _ in self.calls))

    def test_bad_log_still_retains_valid_process_for_cleanup(self):
        self.reply['log'] = {'invalid': 'path'}
        report = self.scenario()
        self.assertEqual(report['status'], 'fail')
        self.assertEqual(report['instances'], [795000])
        self.assertEqual([args[0] for args, _ in self.calls], ['launch', 'kill'])

    def test_bounded_init_timeout_and_validation(self):
        for value in [1, 15000, 45000, 60000]:
            with self.subTest(value=value):
                self.env['validate_scenario']({'steps': [{'do': 'boot', 'initTimeoutMs': value}]})
                self.launch(initTimeoutMs=value, mute=False)
                args, kwargs = self.calls[-1]
                self.assertEqual(args, ('launch', '--init-timeout-ms', str(value)))
                self.assertEqual(kwargs['timeout'], 120 + max(0, value - 15000) / 1000)
                self.assertLessEqual(kwargs['timeout'], 165)
        for value in [None, True, False, 0, -1, 60001, 45000.0, '45000', [], {}]:
            with self.subTest(value=value):
                before = len(self.calls)
                with self.assertRaisesRegex(StepFailed, 'initTimeoutMs'):
                    self.launch(initTimeoutMs=value)
                for kind in ['boot', 'launch']:
                    with self.assertRaisesRegex(ValueError, 'initTimeoutMs'):
                        self.env['validate_scenario']({'steps': [{'do': kind, 'initTimeoutMs': value}]})
                self.assertEqual(len(self.calls), before)

    def test_subprocess_timeout_propagates_without_guessing_pid(self):
        self.reply = subprocess.TimeoutExpired('kh2ctl launch', 120)
        with self.assertRaises(subprocess.TimeoutExpired):
            self.launch()
        self.assertEqual(self.ctx.instances, [])
        self.assertEqual(len(self.calls), 1)


if __name__ == '__main__':
    unittest.main()
