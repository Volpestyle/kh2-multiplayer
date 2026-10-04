"""Runner reconnect adapter controls; AST extraction excludes all live setup/imports."""
import ast
import hashlib
import json
from pathlib import Path
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]


class StepFailed(Exception):
    pass


class InstanceDied(Exception):
    pass


class Process:
    def __init__(self, pid, args):
        self.pid, self.args, self.returncode = pid, args, None

    def poll(self):
        return self.returncode


class AdapterTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.now = 0.0
        self.runtime = self.directory / 'runtime.exe'
        self.instances = [types.SimpleNamespace(index=i, pid=100+i,
            inject_log=self.directory / f'native{i}.log') for i in range(3)]
        processes = [(f'runtime_{i}', Process(200+i, [str(self.runtime), '--pid', str(100+i),
            '--role', ('player', 'friend1', 'friend2')[i]]), None) for i in range(3)]
        processes.append(('relay', Process(300, ['relay.exe']), None))
        self.ctx = types.SimpleNamespace(run_dir=self.directory, saved={}, artifacts=[],
            instances=self.instances, processes=processes, check_all=lambda: None,
            inst=lambda i: self.instances[i], sleep=self.sleep)
        for i in range(3):
            (self.directory / f'runtime_{i}.log').write_bytes(f'runtime {i}\n'.encode())
            self.instances[i].inject_log.write_bytes(f'native {i}\n'.encode())
        (self.directory / 'relay.log').write_bytes(b'relay original\n')
        names = {'reconnect_runtime', 'reconnect_log_bytes', 'save_reconnect_sample',
                 'reconnect_failure_sample', 'retain_reconnect_failure', 'check_reconnect_bindings',
                 'step_reconnect_mark', 'step_runtime_pause', 'step_reconnect_check', 'validate_scenario'}
        tree = ast.parse((ROOT / 'tools/scenario/run.py').read_text())
        tree.body = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in names]
        self.env = {'Context': object, 'Path': Path, 'StepFailed': StepFailed, 'time': types.SimpleNamespace(monotonic=lambda: self.now),
            'RUNTIME': self.runtime, 'LOGS': self.directory, 'json': json, 'hashlib': hashlib,
            'STEPS': dict.fromkeys(('boot', 'runtime_pause', 'reconnect_mark', 'reconnect_check')),
            'collect_reconnect_sample': lambda ctx, deadline: self.sample(),
            '_native_reconnect': types.SimpleNamespace(capture_baseline=lambda sample: self.baseline(),
                validate_reconnect=lambda baseline, sample: {'ready': True, 'problems': []}),
            '_runtime_lifecycle': types.SimpleNamespace(pause_owned_friend_runtime=lambda *args, **kwargs:
                {'completed': True, 'resumed': True, 'pinPreserved': True}), 'sys': __import__('sys')}
        exec(compile(tree, '<isolated reconnect adapter>', 'exec'), self.env)

    def sleep(self, seconds):
        self.now += seconds

    def sample(self):
        return {'relayLog': b'relay exact\n', 'peers': [dict(slot=i, gamePid=100+i,
            runtimePid=200+i, runtimeArgv=list(self.ctx.processes[i][1].args),
            nativeLog=f'native exact {i}\n'.encode(), runtimeLog=f'runtime exact {i}\n'.encode())
            for i in range(3)]}

    def baseline(self):
        return {'ready': True, 'problems': [], 'roster': [10, 11, 12], 'relayLog': {'bytes': 5}}

    def mark(self):
        return self.env['step_reconnect_mark'](self.ctx, {'as': 'before'})

    def pause(self):
        return self.env['step_runtime_pause'](self.ctx, {'instance': 1, 'after': 'before', 'as': 'pause'})

    def check(self):
        return self.env['step_reconnect_check'](self.ctx, {'after': 'before', 'as': 'after', 'timeoutMs': 1000})

    def artifact(self, name):
        return json.loads((self.directory / f'{name}_sample.json').read_text())

    def test_success_carries_exact_pause_and_raw_bytes(self):
        self.mark(); receipt = self.pause(); result = self.check()
        self.assertTrue(result['ready'])
        self.assertEqual(result['pauseReceipt'], receipt)
        self.assertEqual(receipt['baselineName'], 'before')
        self.assertEqual((self.directory / 'after_relay.bin').read_bytes(), b'relay exact\n')
        self.assertFalse(self.artifact('after')['collectionIncomplete'])

    def test_missing_failed_wrong_or_duplicate_pause_cannot_pass(self):
        for variant in ('missing', 'failed', 'wrong', 'duplicate'):
            with self.subTest(variant=variant):
                self.ctx.saved.clear(); self.mark()
                if variant != 'missing':
                    receipt = self.pause()
                    if variant == 'failed': receipt['pinPreserved'] = False
                    if variant == 'wrong': receipt['baselineName'] = 'different'
                    if variant == 'duplicate': self.ctx.saved['second'] = dict(receipt)
                with self.assertRaisesRegex(StepFailed, 'exactly one successful'):
                    self.check()
                self.assertFalse(self.ctx.saved['after']['ready'])

    def test_relay_exit_or_replacement_and_runtime_drift_fail(self):
        self.mark(); self.pause()
        original = self.ctx.processes[:]
        for change in ('relay_exit', 'relay_replaced', 'runtime_replaced', 'argv', 'game_pid'):
            with self.subTest(change=change):
                self.ctx.processes[:] = original
                self.ctx.processes[-1][1].returncode = None
                self.ctx.processes[0][1].args = list(self.ctx.saved['before']['runnerBindings'][0]['runtimeArgv'])
                self.instances[0].pid = 100
                self.ctx.saved.pop('after', None)
                if change == 'relay_exit': self.ctx.processes[-1][1].returncode = 0
                if change == 'relay_replaced': self.ctx.processes[-1] = ('relay', Process(300, ['relay.exe']), None)
                if change == 'runtime_replaced': self.ctx.processes[0] = ('runtime_0', Process(200, list(original[0][1].args)), None)
                if change == 'argv': self.ctx.processes[0][1].args += ['--different']
                if change == 'game_pid': self.instances[0].pid = 999
                with self.assertRaises(StepFailed): self.check()
                self.assertFalse(self.ctx.saved['after']['ready'])

    def test_relay_dies_during_collector(self):
        self.mark(); self.pause()
        def collect(ctx, deadline):
            self.ctx.processes[-1][1].returncode = 0
            return self.sample()
        self.env['collect_reconnect_sample'] = collect
        with self.assertRaisesRegex(StepFailed, 'relay'): self.check()
        self.assertFalse(self.artifact('after')['evidence']['ready'])

    def test_failures_preserve_exception_and_missing_log_labels(self):
        for error in (OSError('read failed'), StepFailed('bad read'), InstanceDied('game died'), KeyboardInterrupt()):
            with self.subTest(error=type(error).__name__):
                self.ctx.saved.clear()
                missing = self.instances[1].inject_log
                missing.unlink(missing_ok=True)
                def collect(ctx, deadline): raise error
                self.env['collect_reconnect_sample'] = collect
                with self.assertRaises(type(error)) as raised: self.mark()
                self.assertIs(raised.exception, error)
                result = self.artifact('before')
                self.assertFalse(result['evidence']['ready'])
                self.assertTrue(result['collectionIncomplete'])
                self.assertTrue(any(row['source'] == 'peer1.nativeLog' and not row['emptyFallbackIsAbsenceProof']
                                    for row in result['missingLogs']))
                self.assertEqual((self.directory / 'before_relay.bin').read_bytes(), b'relay original\n')

    def test_partial_and_validator_failure_retain_exact_inputs(self):
        error = OSError('validator')
        self.env['_native_reconnect'].capture_baseline = lambda sample: (_ for _ in ()).throw(error)
        with self.assertRaises(OSError): self.mark()
        self.assertEqual((self.directory / 'before_relay.bin').read_bytes(), b'relay exact\n')
        self.ctx.saved.clear()
        def collect(ctx, deadline):
            ctx._reconnect_partial_sample = {'peers': [{'slot': 0, 'nativeLog': b'partial exact'}]}
            raise error
        self.env['collect_reconnect_sample'] = collect
        with self.assertRaises(OSError): self.mark()
        self.assertEqual((self.directory / 'before_peer0_nativeLog.bin').read_bytes(), b'partial exact')

    def test_persistence_failure_does_not_mask_original(self):
        error = KeyboardInterrupt('cancel')
        self.env['collect_reconnect_sample'] = lambda *args: (_ for _ in ()).throw(error)
        self.env['save_reconnect_sample'] = lambda *args: (_ for _ in ()).throw(OSError('disk full'))
        with self.assertRaises(KeyboardInterrupt) as raised: self.mark()
        self.assertIs(raised.exception, error)
        self.assertIn('disk full', self.ctx.saved['before']['artifactPersistenceError'])

    def test_mark_and_check_reject_late_ready(self):
        def late(sample):
            self.now += 20
            return self.baseline()
        self.env['_native_reconnect'].capture_baseline = late
        with self.assertRaisesRegex(StepFailed, 'deadline'): self.mark()
        self.ctx.saved.clear(); self.now = 0
        self.env['_native_reconnect'].capture_baseline = lambda sample: self.baseline()
        self.mark(); self.pause()
        def late_check(baseline, sample):
            self.now += 2
            return {'ready': True}
        self.env['_native_reconnect'].validate_reconnect = late_check
        with self.assertRaisesRegex(StepFailed, 'deadline'): self.check()
        self.assertFalse(self.ctx.saved['after']['ready'])

    def test_timeout_retains_previous_sample_without_extra_collection(self):
        self.mark(); self.pause()
        calls = []
        self.env['collect_reconnect_sample'] = lambda *args: calls.append(1) or self.sample()
        self.env['_native_reconnect'].validate_reconnect = lambda *args: {'ready': False, 'problems': ['pending']}
        with self.assertRaisesRegex(StepFailed, 'deadline'): self.check()
        self.assertEqual(len(calls), 4)
        self.assertEqual((self.directory / 'after_relay.bin').read_bytes(), b'relay exact\n')
        self.assertIn('pending', self.ctx.saved['after']['problems'])

    def test_post_collection_and_validator_exceptions_keep_pause_receipt(self):
        for origin in ('collector', 'validator'):
            for error in (OSError('read'), StepFailed('bad'), InstanceDied('dead'), KeyboardInterrupt()):
                with self.subTest(origin=origin, error=type(error).__name__):
                    self.ctx.saved.clear()
                    self.env['collect_reconnect_sample'] = lambda *args: self.sample()
                    self.env['_native_reconnect'].validate_reconnect = lambda *args: {'ready': True}
                    self.mark(); self.pause()
                    def fail(*args): raise error
                    if origin == 'collector': self.env['collect_reconnect_sample'] = fail
                    else: self.env['_native_reconnect'].validate_reconnect = fail
                    with self.assertRaises(type(error)) as raised: self.check()
                    self.assertIs(raised.exception, error)
                    evidence = self.artifact('after')['evidence']
                    self.assertFalse(evidence['ready'])
                    self.assertTrue(evidence['pauseReceipt']['completed'])

    def test_failed_pause_is_tagged_and_second_fault_refused(self):
        self.mark()
        def fail(*args, **kwargs):
            error = OSError('suspend failure')
            error.receipt = {'completed': False, 'resumed': True, 'pinPreserved': True}
            raise error
        self.env['_runtime_lifecycle'].pause_owned_friend_runtime = fail
        with self.assertRaises(StepFailed): self.pause()
        self.assertEqual(self.ctx.saved['pause']['baselineName'], 'before')
        with self.assertRaisesRegex(StepFailed, 'exactly one successful'): self.check()
        with self.assertRaisesRegex(StepFailed, 'already has a pause receipt'):
            self.env['step_runtime_pause'](self.ctx, {'instance': 1, 'after': 'before', 'as': 'second'})

    def test_partial_failure_keeps_previous_complete_capture(self):
        self.mark(); self.pause()
        calls = []
        def collect(ctx, deadline):
            calls.append(1)
            if len(calls) == 1: return self.sample()
            ctx._reconnect_partial_sample = {'peers': [{'slot': 0, 'nativeLog': b'new partial'}]}
            raise OSError('later read failed')
        self.env['collect_reconnect_sample'] = collect
        self.env['_native_reconnect'].validate_reconnect = lambda *args: {'ready': False}
        with self.assertRaises(OSError): self.check()
        self.assertEqual((self.directory / 'after_last_complete_peer0_nativeLog.bin').read_bytes(), b'native exact 0\n')
        self.assertEqual((self.directory / 'after_peer0_nativeLog.bin').read_bytes(), b'new partial')

    def test_pending_retirement_is_polling_not_terminal_proof(self):
        self.mark()
        proof = {'retired': False, 'identityComplete': False, 'problems': ['not yet']}
        self.env['_native_reconnect'].retirement_proof = lambda *args: proof
        def pause(*args, **kwargs):
            poll = args[6]
            self.assertIsNone(poll(kwargs['relay_offset']))
            proof.update(retired=True, identityComplete=True)
            self.assertIs(poll(kwargs['relay_offset']), proof)
            return {'completed': True, 'resumed': True, 'pinPreserved': True}
        self.env['_runtime_lifecycle'].pause_owned_friend_runtime = pause
        self.pause()

    def test_pause_validation_matches_helper_minimum(self):
        for timeout, valid in ((999, False), (1000, True), (20000, True), (20001, False)):
            scenario = {'steps': [{'do': 'boot', 'instance': i} for i in range(3)] +
                        [{'do': 'runtime_pause', 'instance': 1, 'after': 'before', 'timeoutMs': timeout}]}
            if valid: self.env['validate_scenario'](scenario)
            else:
                with self.assertRaises(ValueError): self.env['validate_scenario'](scenario)


if __name__ == '__main__':
    unittest.main()
