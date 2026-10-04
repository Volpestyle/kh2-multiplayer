"""Saved synthetic evidence controls; no native, network or CLI execution."""
import copy
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('resync_evidence', ROOT / 'tools/scenario/resync_evidence.py')
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def line(prefix, row):
    return prefix + ' ' + ' '.join(f'{k}="{v}"' if k == 'error' else f'{k}={v}' for k, v in row.items()) + '\n'


def fixture(nonempty=False):
    host = 0x100000001
    key = dict(session='a' * 32, host=host, request=0x200000002)
    targets = [(1, 0x300000003, 0x500000005), (2, 0x400000004, 0x600000006)]
    room = dict(epoch=7, world=4, room=26, door=0, map=300, battle=0, event=0)
    plan = dict(key, phase=0, stage=0, targetCount=2, remainingMs=29900, priorGeneration=12, **room)
    for index, target in enumerate(targets):
        plan.update({f'target{index}{field}': value for field, value in zip(('Slot', 'Connection', 'Delivery'), target)})
    native = {'0': line('[resync] plan', plan)}
    for slot, conn, delivery in targets:
        target_plan = dict(plan, stage=1)
        ack = dict(key, targetSlot=slot, connection=conn, delivery=delivery, phase=0, cut=0x700000007,
                   snapshotSHA='b' * 64, fingerprint='c' * 64, status=2, **room,
                   enemyCount=1 if nonempty else 0, deadCount=0, loadBefore=5, loadAfter=6,
                   frame1=200, frame2=201, checksMask=63, serialized=1, bridgeSent=0,
                   relayAccepted='unknown', error='')
        native[str(slot)] = line('[resync] plan', target_plan) + line('[resync] ack', ack)
    terminal = dict(key, reason=0, targetCount=2)
    for index, target in enumerate(targets):
        terminal['target' + str(index)] = ','.join(map(str, (*target, 2, 0x700000007, 'c' * 64, '-')))
    logs = {component: line('[resync-result]', dict(component=component, observerSlot=255 if component == 'relay' else 0,
                                                  observerConnection=0 if component == 'relay' else host, **terminal))
            for component in ('relay', 'runtime')}
    live = [[1, 309, 73]] if nonempty else []
    samples = []
    census = []
    for ordinal, frame in enumerate((220, 230)):
        peers, hashes = {}, {}
        for slot in range(3):
            address = 0x100000 + slot * 0x1000
            native_rows = [dict(netId=1, objectId=309, hp=73, actor=address, epoch=7, frame=frame)] if nonempty else []
            hashes[str(slot)] = dict(frame=frame, location=[4, 26, 0, 300, 0, 0], enemies=AUDIT._native_hash(live),
                                      progress=1234, count=len(live), unmatched=0, nativeRows=native_rows, liveRows=copy.deepcopy(live))
            logged = [dict(netId='1', objectId='309', hp='73', actor=format(address, 'X'))] if nonempty else []
            h = dict(frame=str(frame + 20), epoch='7', world='04', room='1A', door='0', map='300', btl='0', evt='0',
                     enemies=format(AUDIT._native_hash(live), 'X'), progress='4D2', count=str(len(live)), observed=str(len(live)), unmatched='0')
            peers[str(slot)] = dict(complete=True, comparison=dict(comparisonValid=True, nativeLivingAbsentFromLatestHash=[], publishedRowsAbsentFromNativeList=[]),
                                   livingCombatRows=[dict(address=address, objectId=309, hp=73)] if nonempty else [],
                                   logsAfter=dict(nativeRows=logged, hash=h))
        samples.append(dict(epoch=7, instances=hashes))
        census.append(dict(ordinal=ordinal, peers=peers))
    return dict(invocation=dict(processId=123,targetMask=6,slot='all'), queue=dict(ok=True, queued=True, nativeConvergence=False, processId=123, targetMask=6,
                           generation=12, deliverySerial=0x800000008), native=native, **logs,
                statehash=dict(ready=True, problems=[], matchingSamples=samples), census=dict(complete=True, snapshots=census))


class ResyncEvidenceTest(unittest.TestCase):
    def valid(self, value):
        result = AUDIT.audit(value)
        self.assertTrue(result['complete'], result['problems'])
        self.assertTrue(result['populationChecked'])
        return result

    def invalid(self, value):
        result = AUDIT.audit(value)
        self.assertFalse(result['complete'])
        self.assertTrue(result['problems'])
        return result

    def test_empty_full_width_native_and_both_terminals(self):
        result = self.valid(fixture())
        self.assertEqual(result['targets'][0][1], 0x300000003)
        self.assertEqual(result['room'][4], 300)

    def test_nonempty_exact_native_type_hp_join(self):
        self.valid(fixture(True))

    def test_absent_historical_schema(self):
        value = fixture(); value['native'] = {'0': '[enemysync] ready\n'}
        self.invalid(value)

    def test_queue_and_received_arrived_not_success(self):
        for status in ('0', '1', '3', '4'):
            with self.subTest(status=status):
                value = fixture(); value['native']['1'] = value['native']['1'].replace('status=2', 'status=' + status)
                self.invalid(value)

    def test_native_alone_cannot_prove_bridge_or_relay(self):
        for component in ('relay', 'runtime'):
            with self.subTest(component=component):
                value = fixture(); value[component] = 'Resync result request=8589934594 reason=0\n'
                self.invalid(value)

    def test_missing_wrong_duplicate_or_changed_denominator(self):
        for replacement in ('targetCount=1', 'targetCount=0', 'targetCount=3'):
            value = fixture(); value['relay'] = value['relay'].replace('targetCount=2', replacement)
            self.invalid(value)
        value = fixture(); value['native'].pop('2'); self.invalid(value)
        value = fixture(); value['native']['2'] = value['native']['2'].replace('connection=17179869188', 'connection=12884901891'); self.invalid(value)

    def test_native_missing_checks_frames_load_and_serialization(self):
        replacements = [('checksMask=63', 'checksMask=31'), ('frame2=201', 'frame2=200'), ('loadAfter=6', 'loadAfter=5'),
                        ('serialized=1', 'serialized=0'), ('snapshotSHA=' + 'b' * 64, 'snapshotSHA=' + '0' * 64),
                        ('bridgeSent=0', 'bridgeSent=1'), ('relayAccepted=unknown', 'relayAccepted=true')]
        for old, new in replacements:
            with self.subTest(new=new):
                value = fixture(); value['native']['1'] = value['native']['1'].replace(old, new); self.invalid(value)

    def test_mixed_cut_phase_sha_fingerprint(self):
        for old, new in [('cut=30064771079', 'cut=30064771080'), ('phase=0', 'phase=1'),
                         ('snapshotSHA=' + 'b'*64, 'snapshotSHA=' + 'd'*64), ('fingerprint=' + 'c'*64, 'fingerprint=' + 'd'*64)]:
            value = fixture(); value['native']['2'] = value['native']['2'].replace(old, new); self.invalid(value)

    def test_terminal_key_observer_target_status_cut_and_error(self):
        replacements = [('host=4294967297', 'host=1'), ('reason=0', 'reason=6'), ('observerConnection=4294967297', 'observerConnection=1'),
                        (',2,30064771079,', ',1,30064771079,'), (',2,30064771079,', ',2,30064771080,'), (',-\n', ',6572726f72\n')]
        for old, new in replacements:
            with self.subTest(new=new):
                value = fixture(); value['runtime'] = value['runtime'].replace(old, new); self.invalid(value)

    def test_truncated_malformed_duplicate_and_suppressed(self):
        for suffix in ('[resync] ack session=a\n', '[resync] evidence suppressed=1\n'):
            value = fixture(); value['native']['1'] += suffix; self.invalid(value)
        value = fixture(); value['native']['1'] = value['native']['1'].rstrip('\n'); self.invalid(value)
        value = fixture(); value['runtime'] = value['runtime'].replace('reason=0', 'reason=0 reason=0'); self.invalid(value)
        value = fixture(); value['native']['1'] += value['native']['1'].splitlines(keepends=True)[1].replace('frame2=201', 'frame2=202'); self.invalid(value)

    def test_terminal_conflict_and_unknown_error_encoding(self):
        value = fixture(); value['relay'] += value['relay'].replace('reason=0', 'reason=1'); self.invalid(value)
        value = fixture(); value['relay'] = value['relay'].replace(',-\n', ',0\n'); self.invalid(value)

    def test_uint64_overflow_or_alias(self):
        for replacement in ('18446744073709551616', '1'):
            value = fixture(); value['native']['1'] = value['native']['1'].replace('connection=12884901891', 'connection=' + replacement); self.invalid(value)

    def test_repeated_missing_or_before_native_hash_frame(self):
        for frame in (200, 220):
            value = fixture(); value['statehash']['matchingSamples'][1]['instances']['1']['frame'] = frame; self.invalid(value)
        value = fixture(); value['statehash']['matchingSamples'].pop(); self.invalid(value)

    def test_progress_room_or_raw_hash_mismatch(self):
        for field, data in [('progress', 10), ('location', [4,26,0,44,0,0]), ('enemies', 123), ('unmatched', 1)]:
            value = fixture(True); value['statehash']['matchingSamples'][0]['instances']['1'][field] = data; self.invalid(value)

    def test_census_address_type_hp_missing_extra_join(self):
        for field, data in [('address', 123), ('objectId', 311), ('hp', 72)]:
            value = fixture(True); value['census']['snapshots'][0]['peers']['1']['livingCombatRows'][0][field] = data; self.invalid(value)
        value = fixture(True); value['census']['snapshots'][0]['peers']['1']['logsAfter']['nativeRows'] = []; self.invalid(value)
        value = fixture(True); value['census']['snapshots'][0]['peers']['1']['livingCombatRows'] *= 2; self.invalid(value)
        value = fixture(True); value['census']['snapshots'][0]['peers']['1']['comparison']['nativeLivingAbsentFromLatestHash'] = [123]; self.invalid(value)

    def test_incomplete_observation_never_discarded(self):
        value = fixture(); value['census']['snapshots'][0]['peers']['2']['complete'] = False; self.invalid(value)
        value = fixture(); value['collectionProblems'] = ['native2 log truncated/replaced']; self.invalid(value)

    def test_checkpoint_preserves_targets_and_requires_bootstrap_load(self):
        value = fixture()
        for peer, text in list(value['native'].items()):
            plan = text.splitlines(keepends=True)[0]
            checkpoint = plan.replace('phase=0', 'phase=1').replace('remainingMs=29900', 'remainingMs=25000')
            if peer == '0':
                value['native'][peer] += checkpoint
            else:
                ack = text.splitlines(keepends=True)[1]
                # Bootstrap was locally converged; checkpoint observes same loaded
                # room without a second reset. Terminal joins only final cut.
                checkpoint_ack = ack.replace('phase=0', 'phase=1').replace('loadBefore=5', 'loadBefore=6').replace('cut=30064771079', 'cut=30064771080').replace('frame1=200', 'frame1=210').replace('frame2=201', 'frame2=211')
                value['native'][peer] += checkpoint + checkpoint_ack
        for component in ('relay', 'runtime'):
            value[component] = value[component].replace(',30064771079,', ',30064771080,')
        self.valid(value)

    def test_recorded_exact_duplicates_are_idempotent(self):
        value = fixture(); value['runtime'] *= 2
        value['native']['1'] += value['native']['1'].splitlines(keepends=True)[1]
        self.valid(value)

    def test_census_hash_frame_epoch_and_progress_are_fresh(self):
        for field, data in [('epoch', '8'), ('progress', 'FF'), ('enemies', '01'), ('frame', '1')]:
            value = fixture(True)
            value['census']['snapshots'][0]['peers']['1']['logsAfter']['hash'][field] = data
            self.invalid(value)
        value = fixture()
        value['census']['snapshots'][1]['peers']['1']['logsAfter']['hash']['frame'] = '240'
        self.invalid(value)

    def test_numeric_frame_order_alias_and_width(self):
        for before, after in [('202', '201'), ('200', '0200'), ('0', '201'), ('200', '18446744073709551616')]:
            value = fixture()
            value['native']['1'] = value['native']['1'].replace('frame1=200', 'frame1=' + before).replace('frame2=201', 'frame2=' + after)
            self.invalid(value)

    def test_reply_does_not_choose_the_invoked_pid_or_mask(self):
        for field, number in [('processId', 999), ('targetMask', 2)]:
            value = fixture(); value['invocation'][field] = number; self.invalid(value)
        value = fixture(); del value['invocation']; self.invalid(value)

    def test_checkpoint_cannot_reuse_weak_bootstrap_or_stale_cut(self):
        # Append genuine-looking checkpoint lines, then mutate only bootstrap.
        base = fixture()
        for peer, text in list(base['native'].items()):
            lines = text.splitlines(keepends=True)
            checkpoint = lines[0].replace('phase=0', 'phase=1')
            if peer != '0':
                checkpoint += lines[1].replace('phase=0', 'phase=1').replace('loadBefore=5', 'loadBefore=6').replace('cut=30064771079', 'cut=30064771080').replace('frame1=200', 'frame1=210').replace('frame2=201', 'frame2=211')
            base['native'][peer] += checkpoint
        for component in ('relay', 'runtime'):
            base[component] = base[component].replace(',30064771079,', ',30064771080,')
        self.valid(base)
        for old, new in [('status=2', 'status=0'), ('status=2', 'status=1'), ('checksMask=63', 'checksMask=0'),
                         ('serialized=1', 'serialized=0'), ('loadAfter=6', 'loadAfter=5'),
                         ('snapshotSHA=' + 'b'*64, 'snapshotSHA=' + '0'*64), ('world=4', 'world=5'),
                         ('frame2=201', 'frame2=200')]:
            value = copy.deepcopy(base)
            lines = value['native']['1'].splitlines(keepends=True)
            lines[1] = lines[1].replace(old, new)
            value['native']['1'] = ''.join(lines)
            self.invalid(value)
        for old, new in [('cut=30064771080', 'cut=30064771079'), ('cut=30064771080', 'cut=30064771078'),
                         ('loadBefore=6', 'loadBefore=7'), ('loadAfter=6', 'loadAfter=7')]:
            value = copy.deepcopy(base)
            lines = value['native']['1'].splitlines(keepends=True)
            lines[3] = lines[3].replace(old, new)
            value['native']['1'] = ''.join(lines)
            self.invalid(value)

    def test_registration_failure_saves_exact_queue_and_incomplete_logs(self):
        class Failure(Exception): pass
        class Fake:
            def __init__(self, root): self.run_dir=root; self.saved={}; self.artifacts=[]
            def inst(self, slot): return type('Instance', (), {'pid':100+slot})()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); ctx = Fake(root); steps = {}; calls=[]
            def command(*args, **kw):
                calls.append((args, kw)); return dict(ok=True,queued=True,nativeConvergence=False,processId=100,targetMask=6,generation=12,deliverySerial=7)
            def wait(ctx, predicate, *args):
                self.assertFalse(predicate()); raise Failure('deadline')
            AUDIT.register(steps,kh2ctl=command,logs=root,step_failed=Failure,wait_for=wait)
            with self.assertRaises(Failure): steps['forced_resync'](ctx, {'instance':0,'slot':'all'})
            self.assertEqual(calls, [(('world-resync','--slot','all'),{'pid':100})])
            self.assertFalse(ctx.saved['forced_resync']['audit']['complete'])
            self.assertTrue((root/'forced_resync.json').exists())

    def test_registration_wrong_pid_or_mask_fails_before_wait_and_preserves_reply(self):
        class Failure(Exception): pass
        class Fake:
            def __init__(self, root): self.run_dir=root; self.saved={}; self.artifacts=[]
            def inst(self, slot): return type('Instance', (), {'pid':100+slot})()
        for field, bad in [('processId', 999), ('targetMask', 2)]:
            with tempfile.TemporaryDirectory() as temp:
                root=Path(temp);ctx=Fake(root);steps={}
                reply=dict(ok=True,queued=True,nativeConvergence=False,processId=100,targetMask=6,generation=12,deliverySerial=7)
                reply[field]=bad
                def no_wait(*args): self.fail('mismatched reply must not enter wait')
                AUDIT.register(steps,kh2ctl=lambda *a, **kw: reply,logs=root,step_failed=Failure,wait_for=no_wait)
                with self.assertRaises(Failure): steps['forced_resync'](ctx, {'instance':0,'slot':'all'})
                self.assertEqual(ctx.saved['forced_resync']['queue'], reply)
                self.assertEqual(ctx.saved['forced_resync']['invocation']['processId'], 100)
                self.assertEqual(ctx.saved['forced_resync']['invocation']['targetMask'], 6)


if __name__ == '__main__':
    unittest.main()
