"""Production control/parser with synthetic I/O; no native/CLI execution proof."""
import ast
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('progress_resync_control', ROOT / 'tools/scenario/progress_resync_control.py')
CONTROL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTROL)


def snapshot(byte=0):
    data = {'instances': {}}
    for peer in range(3):
        ranges = {}
        for name, offset, length in CONTROL.SHARED + CONTROL.PERSONAL:
            raw = bytearray(length)
            if name == 'chests':
                raw[-1] = byte
            ranges[name] = {'saveOffset': offset, 'length': length, 'hex': raw.hex(),
                            'sha256': hashlib.sha256(raw).hexdigest()}
        data['instances'][str(peer)] = {'ranges': ranges}
    return data


def set_byte(data, peer, name, at, value):
    row = data['instances'][str(peer)]['ranges'][name]
    raw = bytearray.fromhex(row['hex'])
    raw[at] = value
    row.update(hex=raw.hex(), sha256=hashlib.sha256(raw).hexdigest())


ARRIVAL = (r'\[enemysync\] (?:host|client) arrived epoch=(?P<epoch>\d+) '
           r'room=(?P<world>[0-9A-Fa-f]+)/(?P<room>[0-9A-Fa-f]+) '
           r'door=(?P<door>\d+) map=(?P<map>\d+) btl=(?P<btl>\d+) evt=(?P<evt>\d+)')


def native_log(value, load=3, transition=2):
    return ('[progresssync] client full version=1 spans=4 bytes=8108 complete=1\n'
            '[warp] client queued epoch=7 target=05/06 door=0 map=1 btl=1 evt=0\n'
            f'[progresssync] apply version=1 spans=1 bytes=1 hash={value:08X} personal_before=12345678 personal_after=12345678 personal_unchanged=1\n'
            f'[warp] client issued epoch=7 transition={transition}\n'
            f'[warp] load complete serial={load} transition={transition} room=05/06 door=0 map=1 btl=1 evt=0\n'
            '[enemysync] client arrived epoch=7 room=05/06 door=0 map=1 btl=1 evt=0\n')


def runtime_log(pid, peer='peer1'):
    return (f'[Runtime] Network: peer_id={peer} content=none\n'
            f'[Runtime] Attached to KH2 process (PID={pid})\n'
            '[Runtime] Network: SessionState session=' + 'a' * 32 + ' actors=3 room=anything\n'
            '[Runtime] Verified membership; native bootstrap remains separate\n')


class Provider:
    """Synthetic memory and log sources; execute() itself is the production flow."""
    def __init__(self):
        self.baseline = snapshot(0xA0)  # excluded upper bits must survive.
        self.current = copy.deepcopy(self.baseline)
        value = CONTROL.khp1(CONTROL.regions(self.baseline, 0))
        self.native = {str(i): CONTROL.native_context(native_log(value), ARRIVAL, True) for i in range(3)}
        self.runtime = {str(i): CONTROL.runtime_identity(runtime_log(100 + i, 'peer' + str(i)), 100 + i) for i in range(3)}
        self.receipt = {'commands': []}
        self.writes = []
        self.calls = 0
        self.fail_stage = None
        self.readback_failure = False
        self.guard_changed = False
        self.repair_fails = False
        self.unsafe_cleanup = False
        self.extra_cleanup_bit = False
        self.saved = None
        self.result = None

    def sample(self, label):
        if self.fail_stage == label:
            raise ValueError('injected ' + label + ' capture failure')
        return {'native': copy.deepcopy(self.native), 'runtime': copy.deepcopy(self.runtime),
                'bytes': {str(i): CONTROL.regions(self.current, i)['chests'][-1] for i in range(3)},
                'snapshot': copy.deepcopy(self.current)}

    def guard(self, before, expected_byte=None):
        if self.guard_changed or self.unsafe_cleanup or self.native != before['native']:
            raise ValueError('stale original context')
        value = CONTROL.regions(self.current, 1)['chests'][-1]
        if expected_byte is not None:
            CONTROL.require(value == expected_byte, 'immediate byte changed')
        if self.extra_cleanup_bit and expected_byte is None:
            value |= 0x10
            set_byte(self.current, 1, 'chests', -1, value)
        return value

    def write(self, value, purpose):
        self.writes.append((purpose, value))
        self.receipt['commands'].append({'purpose': purpose, 'response': {'ok': True, 'value': value}})
        set_byte(self.current, 1, 'chests', -1, value)
        if self.readback_failure and purpose == 'fault':
            raise ValueError('fault readback failed')

    def expected_rows(self):
        return [[n, 302, 17] for n in range(1, 6)]

    def hashes(self):
        peers = {str(i): {'location': CONTROL.LOCATION, 'count': 5, 'unmatched': 0,
                         'liveRows': self.expected_rows(), 'progress': CONTROL.khp1(CONTROL.regions(self.current, i))}
                 for i in range(3)}
        return {'ready': True, 'problems': [], 'matchingSamples': [
            {'epoch': 7, 'instances': {i: dict(row, frame=frame) for i, row in peers.items()}}
            for frame in (100, 101)], 'relayDesync': [{'peer': 'peer1', 'fields': '4', 'epoch': '7'}]}

    def resync(self, step):
        self.calls += 1
        if not self.repair_fails:
            self.current = copy.deepcopy(self.baseline)
        value = CONTROL.khp1(CONTROL.regions(self.baseline, 0))
        plan = ('[resync] plan session=' + 'a' * 32 + ' host=4294967297 request=7 phase=0 stage=1 targetCount=2 '
                'target0Slot=1 target0Connection=4294967298 target0Delivery=2 target1Slot=2 target1Connection=4294967299 target1Delivery=2\n')
        ack = {'session': 'a' * 32, 'host': '4294967297', 'request': '7', 'phase': '0',
               'loadBefore': '3', 'loadAfter': '4'}
        self.result = {'audit': {'complete': True, 'acknowledgments': {1: ack, 2: copy.deepcopy(ack)}},
                       'native': {'1': plan + '[enemysync] session reset: host epoch and pending target cleared\n' +
                                  native_log(value, 4, 3) + '[resync] ack synthetic bounded parser input\n'}}
        for i in (1, 2):
            self.native[str(i)] = CONTROL.native_context(native_log(value, 4, 3), ARRIVAL, True)

    def bundle(self):
        return self.result

    def persist(self, receipt):
        self.saved = copy.deepcopy(receipt)


class OrchestrationTests(unittest.TestCase):
    def run_control(self, provider):
        return CONTROL.execute(provider, {'instance': 0, 'slot': 'all', 'as': 'forced_resync'})

    def test_one_transaction_and_repair_before_cleanup(self):
        io = Provider()
        receipt = self.run_control(io)
        self.assertTrue(receipt['complete'])
        self.assertEqual(io.calls, 1)
        self.assertEqual(io.writes, [('fault', 0xA2)])
        self.assertEqual(receipt['repair']['bytes']['1'], 0xA0)
        self.assertFalse(receipt['cleanup']['available'])  # new load disallows blind old-context restoration

    def test_initial_capture_unavailable_no_write_or_transaction(self):
        io = Provider(); io.fail_stage = 'before'
        with self.assertRaises(ValueError): self.run_control(io)
        self.assertEqual((io.writes, io.calls), ([], 0))
        self.assertFalse(io.saved['complete'])

    def test_stale_immediate_context_refuses_write(self):
        io = Provider(); io.guard_changed = True
        with self.assertRaises(ValueError): self.run_control(io)
        self.assertEqual((io.writes, io.calls), ([], 0))

    def test_partial_write_readback_failure_still_attempts_safe_cleanup(self):
        io = Provider(); io.readback_failure = True
        with self.assertRaisesRegex(ValueError, 'readback'): self.run_control(io)
        self.assertEqual(io.calls, 0)
        self.assertEqual(io.writes, [('fault', 0xA2), ('cleanup', 0xA0)])
        self.assertFalse(io.saved['complete'])

    def test_prequeue_failure_never_calls_resync(self):
        io = Provider(); io.fail_stage = 'prequeue'
        with self.assertRaises(ValueError): self.run_control(io)
        self.assertEqual(io.calls, 0)
        self.assertTrue(io.saved['cleanup']['wrote'])

    def test_failed_repair_not_rescued_by_cleanup(self):
        io = Provider(); io.repair_fails = True
        with self.assertRaisesRegex(ValueError, 'before cleanup'): self.run_control(io)
        self.assertEqual(io.calls, 1)
        self.assertFalse(io.saved['complete'])
        self.assertFalse(io.saved['cleanup']['available'])
        self.assertEqual(io.writes, [('fault', 0xA2)])

    def test_unsafe_cleanup_skips_write(self):
        io = Provider()
        def failed_hashes():
            io.unsafe_cleanup = True
            raise ValueError('lost context during observation')
        io.hashes = failed_hashes
        with self.assertRaises(ValueError): self.run_control(io)
        self.assertEqual(io.writes, [('fault', 0xA2)])
        self.assertFalse(io.saved['cleanup']['available'])

    def test_cleanup_preserves_other_bits(self):
        io = Provider(); io.fail_stage = 'prequeue'; io.extra_cleanup_bit = True
        with self.assertRaises(ValueError): self.run_control(io)
        self.assertEqual(io.writes[-1], ('cleanup', 0xB0))
        self.assertFalse(io.saved['complete'])

    def test_wrong_host_or_target_refused(self):
        for step in ({'instance': 1}, {'slot': '1'}, {'as': 'different'}):
            io = Provider()
            with self.assertRaises(ValueError): CONTROL.execute(io, step)
            self.assertEqual(io.writes, [])


class EvidenceTests(unittest.TestCase):
    def test_registered_flow_keeps_original_artifact_and_snapshot_reader(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / 'relay.log').write_text('')
            io = Provider()
            value = CONTROL.khp1(CONTROL.regions(io.baseline, 0))
            for i in range(3):
                text = native_log(value)
                if i == 0:
                    text = text.replace('[enemysync] client arrived', '[enemysync] host arrived')
                (directory / f'kh2coop_inject_{100+i}.log').write_text(text)
                (directory / f'runtime_{i}.log').write_text(runtime_log(100+i, 'peer'+str(i)))
            ctx = types.SimpleNamespace(run_dir=directory, saved={'after_host_damage': {'instances': {'0': {'liveRows': io.expected_rows()}}}},
                                        artifacts=[], check_all=lambda: None, inst=lambda i: types.SimpleNamespace(pid=100+i))
            commands, snapshots, transactions = [], [], []
            def command(*args, **kwargs):
                commands.append((args, kwargs))
                peer = kwargs['pid'] - 100
                if args[0] == 'poke':
                    self.assertEqual(peer, 1)
                    set_byte(io.current, peer, 'chests', -1, int(args[args.index('--value')+1]))
                    return {'ok': True, 'processId': kwargs['pid']}
                values = dict(zip(('0x717008', '0x717009', '0x71700A', '0x71700C', '0x71700E', '0x717010'), CONTROL.LOCATION))
                values.update({'0x9A98B0': 0x4A32484B, '0x9ABC8F': CONTROL.regions(io.current, peer)['chests'][-1],
                               '0x2A171E8': 0, '0x9BA8D0': 1, '0x7435D0': 255, '0xB65210': 0, '0x2A11478': '0x0'})
                return {'ok': True, 'samples': [values]}
            def capture_snapshot(context, step, read):
                snapshots.append(step['as'])
                self.assertTrue(read('peek', '--rva', '0x9ABC8F:u8', pid=101)['ok'])
                context.saved[step['as']] = copy.deepcopy(io.current)
            def transaction(context, step):
                transactions.append(step)
                io.resync(step)
                for peer in (1, 2):
                    path = directory / f'kh2coop_inject_{100+peer}.log'
                    path.write_text(path.read_text() + io.result['native']['1'])
                host_path = directory / 'kh2coop_inject_100.log'
                host_plan = io.result['native']['1'].splitlines()[0].replace('stage=1', 'stage=0')
                host_path.write_text(host_path.read_text() + host_plan + '\n')
                context.saved['forced_resync'] = io.result
            steps = {'forced_resync': transaction, 'statehash_check': lambda *args: io.hashes()}
            CONTROL.register(steps, kh2ctl=command, logs=directory, step_failed=ValueError,
                             arrival_pattern=ARRIVAL, snapshot=capture_snapshot)
            result = steps['progress_fault_resync'](ctx, {'instance': 0, 'slot': 'all', 'as': 'forced_resync'})
            self.assertTrue(result['complete'])
            self.assertEqual(len(transactions), 1)
            self.assertIs(ctx.saved['forced_resync'], io.result)
            self.assertEqual(snapshots, ['progress_resync_'+name for name in ('before', 'fault', 'prequeue', 'repair')])
            self.assertEqual(len([call for call in commands if call[0][0] == 'poke']), 1)

    def test_registered_control_retains_failed_cli_response_without_write(self):
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            (directory / 'relay.log').write_text('')
            value = CONTROL.khp1(CONTROL.regions(snapshot(), 0))
            for i in range(3):
                text = native_log(value)
                if i == 0:
                    text = text.replace('[enemysync] client arrived', '[enemysync] host arrived')
                (directory / f'kh2coop_inject_{100+i}.log').write_text(text)
                (directory / f'runtime_{i}.log').write_text(runtime_log(100+i, 'peer'+str(i)))
            ctx = types.SimpleNamespace(run_dir=directory, saved={}, artifacts=[], check_all=lambda: None,
                                        inst=lambda i: types.SimpleNamespace(pid=100+i))
            calls = []
            def command(*args, **kwargs):
                calls.append((args, kwargs))
                return {'ok': False, 'error': 'synthetic owned-process read refusal', 'processId': kwargs['pid']}
            def unexpected_snapshot(*args):
                self.fail('unsafe read must abort before full snapshot')
            steps = {}
            CONTROL.register(steps, kh2ctl=command, logs=directory, step_failed=ValueError,
                             arrival_pattern=ARRIVAL, snapshot=unexpected_snapshot)
            with self.assertRaisesRegex(ValueError, 'read refusal'):
                steps['progress_fault_resync'](ctx, {})
            receipt = json.loads((directory / 'progress_resync_control.json').read_text())
            self.assertEqual(len(calls), 1)
            self.assertEqual(calls[0][0][0], 'peek')
            self.assertEqual(receipt['commands'][0]['response']['error'], 'synthetic owned-process read refusal')
            self.assertEqual(receipt['resyncCalls'], 0)
            self.assertFalse(receipt['complete'])

    def test_exact_fault_full_ranges_and_personal(self):
        before = snapshot(); fault = copy.deepcopy(before)
        set_byte(fault, 1, 'chests', -1, 2)
        CONTROL.check_snapshots(before, fault)
        for peer, name, at, value in ((0, 'chests', -1, 2), (2, 'story', 2, 1),
                                      (1, 'inventory', 0, 1), (1, 'chests', -1, 0x12)):
            bad = copy.deepcopy(fault); set_byte(bad, peer, name, at, value)
            with self.assertRaises(ValueError): CONTROL.check_snapshots(before, bad)

    def test_masked_hash_excludes_unverified_bits_but_sees_flag409(self):
        original = snapshot()
        excluded = snapshot(0xF0)
        selected = snapshot(2)
        self.assertEqual(CONTROL.khp1(CONTROL.regions(original, 0)), CONTROL.khp1(CONTROL.regions(excluded, 0)))
        self.assertNotEqual(CONTROL.khp1(CONTROL.regions(original, 0)), CONTROL.khp1(CONTROL.regions(selected, 0)))

    def test_snapshot_corruption_is_not_native_evidence(self):
        bad = snapshot(); bad['instances']['1']['ranges']['chests']['sha256'] = '0' * 64
        with self.assertRaises(ValueError): CONTROL.check_snapshots(bad)

    def test_runtime_pin_churn_or_ambiguity_refused(self):
        good = runtime_log(123)
        self.assertEqual(CONTROL.runtime_identity(good, 123)['session'], 'a' * 32)
        for bad in (good.replace('actors=3', 'actors=2'), good + '[Runtime] Network: closed code=3\n',
                    good + '[Runtime] Attached to KH2 process (PID=123)\n',
                    good + '[Runtime] Network: SessionState session=' + 'b' * 32 + ' actors=3\n'):
            with self.assertRaises(ValueError): CONTROL.runtime_identity(bad, 123)

    def test_latest_apply_attached_to_current_boundary(self):
        good = native_log(0x12345678)
        self.assertEqual(CONTROL.native_context(good, ARRIVAL, True)['load'], 3)
        for bad in (good + '[progresssync] apply failed version=1 reason=save-verification\n',
                    good + '[enemysync] session reset: host epoch and pending target cleared\n',
                    good + '[resync] plan session=' + 'a' * 32 + ' stage=1\n',
                    good + '[enemysync] role off\n',
                    good + '[progresssync] client delta version=2 spans=1 bytes=1 complete=1\n',
                    good.replace('transition=2 room=', 'transition=9 room='),
                    good.replace('personal_unchanged=1', 'personal_unchanged=0')):
            with self.assertRaises(ValueError): CONTROL.native_context(bad, ARRIVAL, True)

    def test_hash_control_requires_exact_rows_frames_and_relay(self):
        io = Provider(); io.write(0xA2, 'fault')
        valid = io.hashes()
        expected = {str(i): CONTROL.khp1(CONTROL.regions(io.current, i)) for i in range(3)}
        CONTROL.check_hashes(valid, io.expected_rows(), expected, 7)
        for alter in ('hp', 'frame', 'relay', 'unmatched'):
            bad = copy.deepcopy(valid)
            if alter == 'hp': bad['matchingSamples'][1]['instances']['1']['liveRows'][0][2] = 16
            elif alter == 'frame': bad['matchingSamples'][1]['instances']['1']['frame'] = 100
            elif alter == 'relay': bad['relayDesync'][0]['fields'] = '6'
            else: bad['matchingSamples'][0]['instances']['2']['unmatched'] = 1
            with self.assertRaises(ValueError): CONTROL.check_hashes(bad, io.expected_rows(), expected, 7)

    def test_apply_chain_rejects_zero_extra_failed_or_wrong_version(self):
        io = Provider(); before = io.sample('before'); hashes = CONTROL.check_snapshots(before['snapshot'])
        io.resync({}); good = io.bundle()
        CONTROL.check_repair_chain(good, before, hashes)
        substitutions = [('spans=1 bytes=1', 'spans=0 bytes=0'), ('spans=1 bytes=1', 'spans=1 bytes=2'),
                         ('apply version=1', 'apply version=2'), ('personal_unchanged=1', 'personal_unchanged=0'),
                         ('[warp] client issued', '[progresssync] apply failed version=1 reason=readback\n[warp] client issued'),
                         ('[progresssync] apply version', '[enemysync] session reset: host epoch and pending target cleared\n[progresssync] apply version'),
                         ('[enemysync] session reset: host epoch and pending target cleared\n', ''),
                         ('stage=1', 'stage=0'), ('session=' + 'a' * 32, 'session=' + 'b' * 32)]
        for old, new in substitutions:
            bad = copy.deepcopy(good); bad['native']['1'] = bad['native']['1'].replace(old, new)
            with self.assertRaises(ValueError): CONTROL.check_repair_chain(bad, before, hashes)

    def test_new_fixture_changes_only_one_existing_step(self):
        directory = ROOT / 'tools/scenario/scenarios'
        old = json.loads((directory / 'net_forced_resync_shadows_population.json').read_text())
        new = json.loads((directory / 'net_forced_resync_shadows_progress.json').read_text())
        self.assertEqual(len(old['steps']), 89)
        differences = [i for i, (a, b) in enumerate(zip(old['steps'], new['steps'])) if a != b]
        self.assertEqual(len(new['steps']), 89)
        self.assertEqual(len(differences), 1)
        changed = copy.deepcopy(new['steps'][differences[0]])
        self.assertEqual(changed['do'], 'progress_fault_resync')
        changed['do'] = 'forced_resync'
        self.assertEqual(changed, old['steps'][differences[0]])

    def test_capture_returns_actual_cli_receipt_unchanged(self):
        tree = ast.parse((ROOT / 'tools/scenario/run.py').read_text())
        function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == 'step_capture')
        receipt = {'ok': True, 'requestSequence': 17, 'doneSequence': 17, 'framesWritten': 1}
        env = {'Context': object, 'kh2ctl': lambda *a, **k: receipt}
        exec(compile(ast.Module(body=[function], type_ignores=[]), '<production step_capture>', 'exec'), env)
        with tempfile.TemporaryDirectory() as temp:
            ctx = types.SimpleNamespace(run_dir=Path(temp), artifacts=[], inst=lambda i: types.SimpleNamespace(index=i, pid=123))
            result = env['step_capture'](ctx, {'instance': 1, 'name': 'owned'})
        self.assertIs(result['receipt'], receipt)
        self.assertEqual(result['path'], 'owned_1.png')


if __name__ == '__main__':
    unittest.main()
