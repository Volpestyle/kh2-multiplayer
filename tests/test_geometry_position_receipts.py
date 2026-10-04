"""Actual census read path with synthetic CLI memory, never native/game proof."""
import ast
import copy
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import time
import traceback
from types import SimpleNamespace
import unittest


ROOT = Path(__file__).resolve().parents[1]


def collector_module(logs, *, without_raw_occupancy=False):
    # Execute the actual production functions, without importing rig startup or
    # accessing platform APIs. No collector/receipt algorithm is duplicated.
    tree = ast.parse((ROOT / 'tools/scenario/run.py').read_text(encoding='utf-8'))
    functions = {'native_enemy_census_snapshot', 'capture_native_regions', 'capture_raw_native_occupancy',
                 'native_geometry_comparisons', 'native_geometry_sampled_endpoints', 'evaluate_native_region'}
    constants = {'ARRIVAL_PATTERN', 'HASH_PATTERN', 'NATIVE_HASH_PATTERN'}
    nodes = [node for node in tree.body if
             (isinstance(node, ast.FunctionDef) and node.name in functions) or
             (isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id in constants for t in node.targets))]
    if without_raw_occupancy:
        # Reuse the unchanged original measurement body; omit only the new final
        # receipt call to compare its output and CLI schedule, not a second model.
        for node in nodes:
            if isinstance(node, ast.FunctionDef) and node.name == 'native_enemy_census_snapshot':
                node.body = [statement for statement in node.body if not (
                    isinstance(statement, ast.Assign) and isinstance(statement.value, ast.Call)
                    and isinstance(statement.value.func, ast.Name)
                    and statement.value.func.id == 'capture_raw_native_occupancy')]
    env = {'Context': object, 'StepFailed': RuntimeError, 'Path': Path, 'LOGS': logs,
           'KH2CTL': Path('synthetic-only-kh2ctl'), 're': re, 'struct': struct, 'time': time,
           'subprocess': subprocess, 'traceback': traceback, 'math': math}
    exec(compile(ast.Module(body=nodes, type_ignores=[]), '<production census functions>', 'exec'), env)
    return env


class Memory:
    base = 0x140000000
    actor_rvas = (0x100000, 0x110000)

    def __init__(self, *, alias=False, change=None, failure=None, omit=False, initial_y=2.0, missing_roots=False):
        self.alias, self.change, self.failure, self.omit = alias, change, failure, omit
        self.calls = []
        self.phase_counts = {'selectors': 0, 'positions': 0}
        base = self.base
        first, second = (base + rva for rva in self.actor_rvas)
        self.memory = {0x2A171C8: first, 0x2A171D0: second,
                       0x2AE6680: base + 0x2AE5E60, 0x2AE6688: 1,
                       0x9BA8D0: 1, 0x7435D0: 255, 0x717008: 5, 0x717009: 6,
                       0x71700C: 1, 0x71700E: 1, 0x2AE5E60: 6,
                       0x2A10420: first, 0x2A105D0: first if alias else second,
                       0x2B0D720: base, 0x100A90: 0x110000,
                       0x200000: 84, 0x200008: 0x5F50}
        if missing_roots:
            self.memory[0x2A10420] = self.memory[0x2A105D0] = 0
        self.raw = struct.pack('<4f', 1.0, initial_y, 3.0, 1.0)
        for rva in self.actor_rvas:
            self.memory[rva + 0x918] = base + 0x200000
            for n in range(2):
                self.memory[rva + 0x670 + n * 8] = int.from_bytes(self.raw[n*8:n*8+8], 'little')

    def __call__(self, *args, **kwargs):
        self.calls.append((args, kwargs))
        if args[0] == 'entities':
            return {'ok': True, 'actors': [{'address': hex(self.base + rva), 'name': 'P_EX100', 'objectType': 0}
                                          for rva in self.actor_rvas]}
        if args[0] != 'peek':
            raise AssertionError('unexpected live command')
        specs = [(int(token.split(':')[0], 0), token.split(':')[1]) for token in args[2].split(',')]
        phase = None
        if len(specs) == 6 and specs[0][0] == 0x1006A0:
            phase = 'selectors'
        elif len(specs) == 4 and specs[0][0] == 0x100670:
            phase = 'positions'
        if phase:
            self.phase_counts[phase] += 1
            phase += '-before' if self.phase_counts[phase] == 1 else '-after'
        if self.failure is not None and phase == self.failure and not self.omit:
            raise PermissionError('synthetic read denied')
        data = {}
        for index, (rva, kind) in enumerate(specs):
            if self.failure is not None and phase == self.failure and self.omit and index == 1:
                continue
            value = self.memory.get(rva, 0)
            if phase == 'positions-after' and self.change and rva == self.change[0]:
                value = self.change[1]
            if phase == 'selectors-after' and self.change and rva == self.change[0]:
                value = self.change[1]
            data[f'0x{rva:X}'] = hex(value) if kind == 'u64' else value
        return {'ok': True, 'moduleBase': hex(self.base), 'samples': [data]}


class GeometryReceipts(unittest.TestCase):
    def collect(self, *, without_raw_occupancy=False, memory=None, clock=None, **options):
        memory = memory or Memory(**options)
        with tempfile.TemporaryDirectory() as temp:
            logs = Path(temp)
            value = 2166136261
            for byte in struct.pack('<II', 0x3145484B, 0):
                value = ((value ^ byte) * 16777619) & 0xffffffff
            (logs / 'kh2coop_inject_123.log').write_text(
                '[warp] load complete serial=10 transition=10 room=05/06 door=0 map=1 btl=1 evt=0\n'
                '[enemysync] host arrived epoch=10 room=05/06 door=0 map=1 btl=1 evt=0\n'
                f'[statehash] role=host epoch=10 frame=100 room=05/06 door=0 map=1 btl=1 evt=0 enemies={value:08X} progress=12345678 count=0 unmatched=0 observed=0\n')
            env = collector_module(logs, without_raw_occupancy=without_raw_occupancy)
            memory.logs = logs
            if clock is not None:
                env['time'] = SimpleNamespace(monotonic=clock)
            env['kh2ctl'] = memory
            result = env['native_enemy_census_snapshot'](SimpleNamespace(inst=lambda index: SimpleNamespace(pid=123)), 0)
        return result, memory, env

    def test_equal_bytes_and_existing_read_schedule(self):
        result, memory, _ = self.collect()
        cause = result['causeContext']
        self.assertTrue(result['complete'] and cause['complete'])
        self.assertTrue(cause['positionsStableDuringGeometry'] and cause['positionSelectorsStable'])
        self.assertEqual(len(memory.calls), 24)  # Original17, five raw-list reads, two raw lifecycle headers.
        self.assertEqual(memory.phase_counts, {'selectors': 2, 'positions': 2})
        position_calls = [call[0] for call in memory.calls if call[0][0] == 'peek' and call[0][2].startswith('0x100670:')]
        selector_calls = [call[0] for call in memory.calls if call[0][0] == 'peek' and call[0][2].startswith('0x1006A0:')]
        self.assertEqual(position_calls[0], position_calls[1])
        self.assertEqual(selector_calls[0], selector_calls[1])
        for name in ('positionReadback', 'positionSelectorReadback'):
            receipt = cause[name]
            self.assertTrue(receipt['comparisonComplete'])
            self.assertEqual(receipt['scope'], ['activationActor', 'playerActor'])
            self.assertEqual(receipt['before']['bytes'], receipt['after']['bytes'])
            self.assertEqual(receipt['changes'], [])
        raw = cause['positionReadback']['before']['bytes']
        self.assertEqual(raw['activationActor:0'] + raw['activationActor:1'], memory.raw.hex())
        self.assertEqual(cause['activationActors']['activationActor']['positionHex'], memory.raw.hex())

    def test_player_only_motion_keeps_aggregate_false_and_initial_geometry_point(self):
        raw = struct.pack('<4f', 1.0, 2.000000238418579, 3.0, 1.0)
        result, _, env = self.collect(change=(0x110670, int.from_bytes(raw[:8], 'little')))
        cause = result['causeContext']
        self.assertTrue(cause['complete'])  # Existing semantics: movement alone is not a cause limit.
        self.assertFalse(cause['positionsStableDuringGeometry'])
        self.assertEqual([row['field'] for row in cause['positionReadback']['changes']], ['playerActor:0'])
        identity = [1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
        cause['controllers'] = [{'groupKey': 1, 'headerFields': {'headerId': 30}, 'geometry': {
            'complete': True, 'regions': [{'descriptorIndex': 0, 'kind': 'BOX', 'inverseMatrix': identity, 'extents': [4.,4.,4.]}]}}]
        comparison = env['native_geometry_comparisons']({'0': result})[0]
        self.assertTrue(comparison['available'])
        self.assertFalse(comparison['pointStableDuringCapture'])
        self.assertEqual(comparison['position4'], [1.,2.,3.,1.])

    def test_raw_one_ulp_w_signed_zero_and_nan_payload_changes(self):
        for word, raw in ((0, struct.pack('<II', 0x3f800000, 0x40000001)),
                          (1, struct.pack('<II', 0x40400000, 0x3f800001)),
                          (0, struct.pack('<II', 0x3f800000, 0x80000000)),
                          (0, struct.pack('<II', 0x3f800000, 0x7fc00001))):
            with self.subTest(raw=raw.hex()):
                initial = 0.0 if raw[4:] == b'\x00\x00\x00\x80' else float('nan') if raw[4:] == b'\x01\x00\xc0\x7f' else 2.0
                result, _, _ = self.collect(change=(0x100670 + word*8, int.from_bytes(raw, 'little')), initial_y=initial)
                cause = result['causeContext']
                self.assertFalse(cause['positionsStableDuringGeometry'])
                self.assertEqual(cause['positionReadback']['after']['bytes'][f'activationActor:{word}'], raw.hex())

    def test_distinct_samples_do_not_imply_within_capture_motion(self):
        first, _, _ = self.collect(initial_y=2.0)
        second, _, _ = self.collect(initial_y=2.000000238418579)
        self.assertNotEqual(first['causeContext']['activationActors']['activationActor']['positionHex'],
                            second['causeContext']['activationActors']['activationActor']['positionHex'])
        self.assertTrue(first['causeContext']['positionsStableDuringGeometry'])
        self.assertTrue(second['causeContext']['positionsStableDuringGeometry'])

    def test_alias_preserves_role_labels_and_duplicate_requested_addresses(self):
        result, memory, _ = self.collect(alias=True)
        receipt = result['causeContext']['positionReadback']
        self.assertEqual(len(receipt['fields']), 4)
        self.assertEqual(receipt['fields']['activationActor:0']['address'], receipt['fields']['playerActor:0']['address'])
        self.assertEqual(receipt['before']['bytes']['activationActor:0'], receipt['before']['bytes']['playerActor:0'])
        self.assertEqual(memory.phase_counts, {'selectors': 2, 'positions': 2})

    def test_partial_second_read_retains_first_word_and_old_missing_boolean(self):
        result, memory, _ = self.collect(failure='positions-after', omit=True)
        cause = result['causeContext']; receipt = cause['positionReadback']
        self.assertTrue(result['complete'])  # Geometry failure does not erase checked primary census.
        self.assertFalse(cause['complete'])
        self.assertNotIn('positionsStableDuringGeometry', cause)
        self.assertTrue(receipt['after']['attempted'])
        self.assertFalse(receipt['after']['complete'])
        self.assertEqual(list(receipt['after']['bytes']), ['activationActor:0'])
        self.assertEqual(receipt['after']['unreadFields'], ['activationActor:1', 'playerActor:0', 'playerActor:1'])
        self.assertEqual(result['readFailures'][-1]['stage'], 'geometry-positions-after')
        self.assertEqual(memory.phase_counts['positions'], 2)

    def test_partial_first_position_read_has_no_fabricated_float4(self):
        result, memory, _ = self.collect(failure='positions-before', omit=True)
        cause = result['causeContext']; receipt = cause['positionReadback']
        self.assertTrue(result['complete'])
        self.assertFalse(cause['complete'])
        self.assertEqual(list(receipt['before']['bytes']), ['activationActor:0'])
        self.assertFalse(receipt['before']['complete'])
        self.assertFalse(receipt['after']['attempted'])
        self.assertEqual(len(receipt['after']['unreadFields']), 4)
        self.assertNotIn('position4', cause['activationActors']['activationActor'])
        self.assertEqual(memory.phase_counts['positions'], 1)

    def test_missing_roots_do_not_claim_empty_map_position_coverage(self):
        result, memory, _ = self.collect(missing_roots=True)
        cause = result['causeContext']
        self.assertFalse(cause['complete'])
        self.assertTrue(cause['positionsStableDuringGeometry'])  # Preserve original empty dictionary comparison.
        self.assertEqual(cause['positionReadback']['scope'], [])
        self.assertEqual(cause['positionReadback']['fields'], {})
        self.assertFalse(cause['positionReadback']['comparisonComplete'])
        self.assertEqual(memory.phase_counts, {'selectors': 0, 'positions': 0})

    def test_selector_failure_does_not_attempt_later_position_read(self):
        for phase in ('selectors-before', 'selectors-after'):
            with self.subTest(phase=phase):
                result, memory, _ = self.collect(failure=phase)
                cause = result['causeContext']
                sample = cause['positionSelectorReadback'][phase.split('-')[1]]
                self.assertFalse(cause['complete'])
                self.assertTrue(sample['attempted'])
                self.assertFalse(sample['complete'])
                self.assertEqual(sample['bytes'], {})
                self.assertIn('PermissionError', sample['error'])
                self.assertFalse(cause['positionReadback']['after']['attempted'])
                self.assertEqual(memory.phase_counts['positions'], 0 if phase.endswith('before') else 1)

    def test_selector_change_retains_original_selected_position_address(self):
        result, _, _ = self.collect(change=(0x1006A0, 0x110000))
        cause = result['causeContext']
        self.assertFalse(cause['positionSelectorsStable'])
        self.assertFalse(cause['complete'])
        self.assertTrue(cause['positionsStableDuringGeometry'])
        self.assertEqual(cause['positionSelectorReadback']['changes'][0]['field'], 'activationActor:parentHandle')
        self.assertEqual(cause['activationActors']['activationActor']['selectedOffset'], 0x670)
        self.assertEqual(cause['positionReadback']['fields']['activationActor:0']['address'], Memory.base + 0x100670)

    def endpoint_fixture(self, after_y=6.0):
        raw = struct.pack('<2f', 2.0, after_y)
        result, memory, env = self.collect(initial_y=5.0, change=(0x100670, int.from_bytes(raw, 'little')))
        cause = result['causeContext']
        identity = [1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.]
        region_array, head = Memory.base + 0x400000, Memory.base + 0x500000
        entry = {'key': 808476514, 'groupKey': 808476514, 'flags': 0, 'pointer': Memory.base + 0x300000,
                 'tableIndex': 0, 'layoutValidated': True, 'tableKeyValidated': True,
                 'headerFields': {'headerId': 30, 'type': 2, 'regionCount': 7}, 'regionArray': region_array,
                 'regionHead': head, 'regionTail': head + 6 * 0x70,
                 'geometry': {'complete': True, 'bytesStable': True,
                     'descriptors': [{'address': region_array + n * 64} for n in range(7)],
                     'regions': [{'address': head + n * 0x70, 'descriptorIndex': n,
                                  'descriptor': region_array + n * 64,
                                  'nextAddress': head + (n+1) * 0x70 if n < 6 else 0,
                                  'kind': 'BOX', 'inverseMatrix': identity[:], 'extents': [4.,4.,4.]}
                                 for n in range(7)]}}
        cause['controllers'] = [entry]
        cause['before']['controllerCount'] = cause['after']['controllerCount'] = 1
        return result, memory, env, entry

    def test_moving_endpoints_add_only_saved_byte_results_without_native_calls(self):
        peer, memory, env, _ = self.endpoint_fixture()
        before = copy.deepcopy(peer); calls = copy.deepcopy(memory.calls)
        row = env['native_geometry_comparisons']({'0': peer})[0]
        endpoints = row['sampledEndpoints']
        self.assertTrue(row['available'] and endpoints['available'])
        self.assertFalse(row['pointStableDuringCapture'])
        self.assertFalse(peer['causeContext']['positionsStableDuringGeometry'])
        self.assertFalse(endpoints['positionBytesStable'])
        self.assertFalse(endpoints['nativePredicateCalled'])
        self.assertEqual(row['position4'], [1.,5.,3.,1.])
        self.assertEqual([e['phase'] for e in endpoints['endpoints']], ['before', 'after'])
        self.assertEqual([e['position4'] for e in endpoints['endpoints']], [[1.,5.,3.,1.], [2.,6.,3.,1.]])
        for endpoint in endpoints['endpoints']:
            self.assertEqual(len(endpoint['positionHex']), 32)
            self.assertEqual(len(endpoint['regions']), 7)
            self.assertTrue(all(r['accepted'] is False and r['edgeUncertain'] is False for r in endpoint['regions']))
        self.assertEqual(peer, before)
        self.assertEqual(memory.calls, calls)
        self.assertEqual(len(memory.calls), 24)
        self.assertEqual(memory.phase_counts, {'selectors': 2, 'positions': 2})

    def test_one_endpoint_inside_stays_available_but_cannot_prove_both_outside(self):
        peer, _, env, _ = self.endpoint_fixture(after_y=2.)
        row = env['native_geometry_comparisons']({'0': peer})[0]
        self.assertTrue(row['sampledEndpoints']['available'])
        self.assertTrue(all(not r['accepted'] for r in row['regions']))  # Original before-only result unchanged.
        first, last = row['sampledEndpoints']['endpoints']
        self.assertTrue(all(not r['accepted'] for r in first['regions']))
        self.assertTrue(all(r['accepted'] for r in last['regions']))

    def test_equal_endpoint_bytes_and_aliased_actor_labels(self):
        peer, _, env, entry = self.endpoint_fixture()
        receipt = peer['causeContext']['positionReadback']
        receipt['after']['bytes'] = copy.deepcopy(receipt['before']['bytes'])
        # Aliases do not create extra independent endpoints.
        for suffix in ('0', '1'):
            receipt['fields']['playerActor:' + suffix] = dict(receipt['fields']['activationActor:' + suffix])
        result = env['native_geometry_sampled_endpoints'](peer, entry, peer)
        self.assertTrue(result['available'] and result['positionBytesStable'])
        self.assertEqual(len(result['endpoints']), 2)

    def test_endpoint_readback_and_identity_fail_closed(self):
        def extra_field(peer, receipt_name, label, kind, size):
            receipt = peer['causeContext'][receipt_name]
            receipt['fields'][label] = {'address': Memory.base + 0x100680, 'type': kind}
            for phase in ('before', 'after'):
                receipt[phase]['bytes'][label] = '00' * size
        cases = {
            'null-context': lambda p, e: p.update(causeContext=None),
            'null-sample': lambda p, e: p['causeContext']['positionReadback'].update(before=None),
            'schema': lambda p, e: p['causeContext']['positionReadback'].update(schemaVersion=2),
            'encoding': lambda p, e: p['causeContext']['positionReadback'].update(encoding='big-endian'),
            'extra-position-word': lambda p, e: extra_field(p, 'positionReadback', 'activationActor:2', 'u64', 8),
            'extra-selector': lambda p, e: extra_field(p, 'positionSelectorReadback', 'activationActor:extra', 'u32', 4),
            'partial': lambda p, e: p['causeContext']['positionReadback']['after'].update(complete=False),
            'missing-word': lambda p, e: p['causeContext']['positionReadback']['after']['bytes'].pop('activationActor:1'),
            'short-word': lambda p, e: p['causeContext']['positionReadback']['after']['bytes'].update({'activationActor:0': '00'}),
            'whitespace-hex': lambda p, e: p['causeContext']['positionReadback']['after']['bytes'].update({'activationActor:0': '00 ' * 8}),
            'nonhex': lambda p, e: p['causeContext']['positionReadback']['after']['bytes'].update({'activationActor:0': 'x' * 16}),
            'nonfinite': lambda p, e: p['causeContext']['positionReadback']['after']['bytes'].update({'activationActor:0': struct.pack('<2f', 1., float('nan')).hex()}),
            'position-address': lambda p, e: p['causeContext']['positionReadback']['fields']['activationActor:1'].update(address=1),
            'selector-address': lambda p, e: p['causeContext']['positionSelectorReadback']['fields']['activationActor:status'].update(address=1),
            'selector-bytes': lambda p, e: p['causeContext']['positionSelectorReadback']['after']['bytes'].update({'activationActor:parentHandle': '01000000'}),
            'selector-flag': lambda p, e: p['causeContext'].update(positionSelectorsStable=False),
            'actor-identity': lambda p, e: p['nodes'][0].update(objectEntry=1),
            'actor-duplicate': lambda p, e: p['nodes'].append(copy.deepcopy(p['nodes'][0])),
            'actor-root': lambda p, e: p['causeContext']['after'].update(activationActor=1),
            'initial-bytes': lambda p, e: p['causeContext']['activationActors']['activationActor'].update(positionHex='00' * 16),
            'accessor-offset': lambda p, e: p['causeContext']['activationActors']['activationActor'].update(selectedOffset=0x70),
            'lifecycle': lambda p, e: p['causeContext'].update(lifecycleStable=False),
            'current-location': lambda p, e: p['after'].update(location=[5,7,0,1,1,0]),
            'arrival': lambda p, e: p['causeContext']['logsAfter']['arrival'].update(epoch='11'),
            'native-list': lambda p, e: p.update(actorIdentityStable=False),
        }
        for name, mutate in cases.items():
            with self.subTest(name=name):
                peer, _, env, entry = self.endpoint_fixture()
                mutate(peer, entry)
                result = env['native_geometry_sampled_endpoints'](peer, entry, peer)
                self.assertFalse(result['available'])
                self.assertEqual(result['endpoints'], [])
                self.assertTrue(result['reason'])

    def test_endpoint_geometry_cardinality_and_binding_fail_closed(self):
        cases = {
            'incomplete': lambda p, e: e['geometry'].update(complete=False),
            'drift': lambda p, e: e['geometry'].update(bytesStable=False),
            'roots-drift': lambda p, e: p['causeContext'].update(geometryRootsStable=False),
            'empty': lambda p, e: e['geometry'].update(regions=[]),
            'missing-node': lambda p, e: e['geometry']['regions'].pop(),
            'extra-descriptor': lambda p, e: e['geometry']['descriptors'].append({'address': 1}),
            'duplicate-address': lambda p, e: e['geometry']['regions'][1].update(address=e['regionHead']),
            'duplicate-index': lambda p, e: e['geometry']['regions'][1].update(descriptorIndex=0),
            'descriptor-source': lambda p, e: e['geometry']['regions'][0].update(descriptor=1),
            'broken-link': lambda p, e: e['geometry']['regions'][0].update(nextAddress=0),
            'nonfinite-matrix': lambda p, e: e['geometry']['regions'][0]['inverseMatrix'].__setitem__(0, float('inf')),
            'matrix-cardinality': lambda p, e: e['geometry']['regions'][0]['inverseMatrix'].append(0.),
            'unknown-kind': lambda p, e: e['geometry']['regions'][0].update(kind='UNKNOWN'),
            'table-key': lambda p, e: e.update(key=0),
            'alternate-table': lambda p, e: e.update(flags=1),
            'duplicate-controller': lambda p, e: p['causeContext']['controllers'].append(copy.deepcopy(e)),
        }
        for name, mutate in cases.items():
            with self.subTest(name=name):
                peer, _, env, entry = self.endpoint_fixture()
                mutate(peer, entry)
                result = env['native_geometry_sampled_endpoints'](peer, entry, peer)
                self.assertFalse(result['available'])
                self.assertEqual(result['endpoints'], [])
                self.assertTrue(result['reason'])


class RawMemory(Memory):
    """Same fake CLI boundary, with faults restricted to the additive reads."""
    def __init__(self, *, raw_change=None, raw_omit=None, raw_omit_after=None, raw_failure=False, **options):
        super().__init__(**options)
        self.raw_change, self.raw_omit, self.raw_failure = raw_change, raw_omit, raw_failure
        self.raw_omit_after = raw_omit_after
        self.raw_started = False
        self.raw_root_reads = 0
        self.raw_calls = []

    def __call__(self, *args, **kwargs):
        lifecycle_header = args[0] == 'peek' and args[2].startswith(
            '0x2A171C8:u64,0x2A171D0:u64,0x2AE6680:u64,')
        if args[0] == 'peek' and args[2].startswith('0x2A171C8:u64,0x2A171D0:u64,0x2A171D8:u64,'):
            self.raw_started = True
            self.raw_root_reads += 1
        if self.raw_started and not lifecycle_header:
            self.raw_calls.append((args, kwargs))
        result = super().__call__(*args, **kwargs)
        if self.raw_started and args[0] == 'peek' and not lifecycle_header:
            values = result['samples'][0]
            if self.raw_failure and self.raw_root_reads == 2:
                raise PermissionError('synthetic raw recheck failure')
            if self.raw_omit is not None:
                values.pop(f'0x{self.raw_omit:X}', None)
            if self.raw_root_reads == 2 and self.raw_omit_after is not None:
                values.pop(f'0x{self.raw_omit_after:X}', None)
            if self.raw_root_reads == 2 and self.raw_change:
                rva, value = self.raw_change
                key = f'0x{rva:X}'
                if key in values:
                    values[key] = hex(value) if isinstance(values[key], str) else value
        return result


class TypedAddressMemory(RawMemory):
    """Byte-backed overrides with CmdPeek's address-only, last-key-wins output."""
    def __init__(self, **options):
        super().__init__(**options)
        self.native_bytes = {}

    def put_bytes(self, rva, data):
        self.native_bytes.update({rva + offset: value for offset, value in enumerate(data)})

    def __call__(self, *args, **kwargs):
        result = super().__call__(*args, **kwargs)
        if args[0] == 'peek':
            values = result['samples'][0]
            for token in args[2].split(','):
                address, kind = token.split(':')
                rva = int(address, 0)
                size = {'u8': 1, 'u16': 2, 'u32': 4, 'i32': 4, 'u64': 8, 'f32': 4}[kind]
                if all(rva + n in self.native_bytes for n in range(size)):
                    raw = bytes(self.native_bytes[rva + n] for n in range(size))
                    value = struct.unpack('<f', raw)[0] if kind == 'f32' else int.from_bytes(raw, 'little', signed=kind == 'i32')
                    values[f'0x{rva:X}'] = hex(value) if kind == 'u64' else value
        return result


class LifecycleMemory(RawMemory):
    """Faults at the added header CLI boundaries; log changes use the real file reader."""
    def __init__(self, *, lifecycle_phase='after', lifecycle_fault=None, **options):
        super().__init__(**options)
        self.lifecycle_phase, self.lifecycle_fault = lifecycle_phase, lifecycle_fault
        self.header_calls = 0

    def __call__(self, *args, **kwargs):
        phase = None
        if args[0] == 'peek' and args[2].startswith('0x2A171C8:u64,0x2A171D0:u64,0x2AE6680:u64,'):
            self.header_calls += 1
            # This normal fixture exercises all four unchanged legacy headers.
            if self.header_calls in (5, 6):
                phase = 'before' if self.header_calls == 5 else 'after'
        result = super().__call__(*args, **kwargs)
        if phase is not None and self.lifecycle_phase in (phase, 'both'):
            fault = self.lifecycle_fault
            if fault == 'failure':
                raise PermissionError('synthetic raw lifecycle read denied')
            if fault == 'missing':
                result['samples'][0].pop('0x71700C')
            elif fault == 'tuple':
                result['samples'][0]['0x717009'] = 7
            elif fault == 'unsafe':
                result['samples'][0]['0x2A171E8'] = 1
            elif fault == 'empty-roots':
                result['samples'][0]['0x2A171C8'] = result['samples'][0]['0x2A171D0'] = '0x0'
            elif fault == 'bucket':
                result['samples'][0]['0x2B0D918'] = hex(self.base + 0x2000000)  # Bucket63, unused by these links.
            elif fault == 'bucket-missing':
                result['samples'][0].pop('0x2B0D918')
            elif fault in ('serial', 'transition', 'epoch', 'logs-missing', 'queued', 'serial-missing'):
                path = self.logs / 'kh2coop_inject_123.log'
                text = path.read_text()
                if fault == 'logs-missing':
                    text = ''
                else:
                    text = text.replace('serial=10 transition=10',
                        'serial=11 transition=10' if fault == 'serial' else
                        'serial=10 transition=11' if fault == 'transition' else
                        'transition=10' if fault == 'serial-missing' else 'serial=10 transition=10')
                    if fault == 'epoch':
                        text = text.replace('arrived epoch=10', 'arrived epoch=11')
                    if fault == 'queued':
                        text += '[warp] client queued serial=11 transition=11\n'
                path.write_text(text)
        return result


class RawOccupancyReceipts(unittest.TestCase):
    collect = GeometryReceipts.collect

    def projection(self, snapshot):
        return {key: value for key, value in snapshot.items() if key not in ('rawOccupancy', 'elapsedMs')}

    def test_raw_lifecycle_stable_has_own_scope_and_separate_read_work(self):
        result, memory, _ = self.collect(memory=LifecycleMemory())
        witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
        self.assertEqual(witness['schemaVersion'], 2)
        self.assertEqual(lifecycle['status'], 'stable')
        self.assertTrue(lifecycle['available'] and lifecycle['stable'] and witness['listedOccupancyComplete'])
        for phase in ('before', 'after'):
            self.assertEqual(lifecycle[phase]['scope'], {'loadSerial': 10, 'transitionSerial': 10,
                'epoch': 10, 'location': [5, 6, 0, 1, 1, 0]})
        old, old_memory, _ = self.collect(without_raw_occupancy=True)
        self.assertEqual(self.projection(result), self.projection(old))
        legacy_calls = [call[0] for call in old_memory.calls]
        calls = [call[0] for call in memory.calls]
        self.assertEqual(calls[:17], legacy_calls)
        self.assertEqual(calls[17], calls[-1])  # Same checked header specs bracket only raw work.
        self.assertEqual(calls[18:23], [call[0] for call in memory.raw_calls])
        for phase in ('before', 'after'):
            self.assertEqual(lifecycle[phase]['rawReadbackJoin'], {'complete': True, 'missing': [], 'mismatches': []})

    def test_lifecycle_raw_root_and_unused_bucket_conflicts_prevent_complete_without_hiding_values(self):
        old, old_memory, _ = self.collect(without_raw_occupancy=True)
        for phase in ('before', 'after', 'both'):
            for fault, fields in (('empty-roots', ['activeHead', 'activeTail']), ('bucket', ['bucket63'])):
                with self.subTest(phase=phase, fault=fault):
                    result, memory, _ = self.collect(memory=LifecycleMemory(lifecycle_phase=phase, lifecycle_fault=fault))
                    witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
                    self.assertTrue(lifecycle['available'])
                    self.assertFalse(lifecycle['stable'] or witness['listedOccupancyComplete'])
                    self.assertEqual(lifecycle['status'], 'partial')
                    self.assertEqual(len(witness['lists']['active']['nodes']), 2)
                    self.assertTrue(witness['readback']['before']['complete'] and witness['readback']['after']['complete'])
                    self.assertEqual(witness['readback']['changes'], [])
                    if phase == 'both':
                        self.assertEqual(lifecycle['changes'], [])  # Two internally stable but inconsistent layers.
                    else:
                        self.assertTrue(lifecycle['changes'])  # Native bookend drift remains explicit too.
                    for observed in ('before', 'after'):
                        join = lifecycle[observed]['rawReadbackJoin']
                        if phase in (observed, 'both'):
                            self.assertFalse(join['complete'])
                            self.assertEqual([m['rawField'] for m in join['mismatches']], fields)
                            self.assertEqual(join['missing'], [])
                        else:
                            self.assertTrue(join['complete'])
                    self.assertIn('lifecycle-raw-readback-mismatch', [r['code'] for r in lifecycle['reasons']])
                    self.assertEqual(self.projection(result), self.projection(old))
                    self.assertEqual([c[0] for c in memory.calls[:17]], [c[0] for c in old_memory.calls])
                    self.assertEqual(len(memory.calls), 24)
                    for key in ('atomic', 'pendingExclusionComplete', 'controllerIncarnationQualified', 'globalControllerIdCoverageComplete'):
                        self.assertFalse(witness[key])

    def test_lifecycle_raw_join_missing_values_are_explicit_on_either_side(self):
        cases = [(LifecycleMemory(lifecycle_fault='bucket-missing'), 'region63', 'bucket63', False, True),
                 (LifecycleMemory(raw_omit_after=0x2B0D918), 'region63', 'bucket63', True, False),
                 (LifecycleMemory(raw_omit_after=0x2A171D0), 'tail', 'activeTail', True, False)]
        for memory, native_field, raw_field, native_available, raw_available in cases:
            with self.subTest(field=raw_field, native_available=native_available):
                result, _, _ = self.collect(memory=memory)
                witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
                self.assertEqual(lifecycle['status'], 'partial')
                self.assertFalse(lifecycle['stable'] or witness['listedOccupancyComplete'])
                self.assertEqual(len(witness['lists']['active']['nodes']), 2)
                self.assertIn({'nativeField': native_field, 'rawField': raw_field,
                    'nativeAvailable': native_available, 'rawAvailable': raw_available},
                    lifecycle['after']['rawReadbackJoin']['missing'])
                self.assertIn('lifecycle-raw-readback-missing', [r['code'] for r in lifecycle['reasons']])

    def test_raw_lifecycle_serial_transition_epoch_tuple_and_gameplay_drift_retain_list(self):
        old, old_memory, _ = self.collect(without_raw_occupancy=True)
        for fault, source, field in (('serial', 'logs', 'lifecycle'), ('transition', 'logs', 'lifecycle'),
                                     ('epoch', 'logs', 'arrival'), ('tuple', 'native', 'location'),
                                     ('unsafe', 'native', 'frozen')):
            with self.subTest(fault=fault):
                result, memory, _ = self.collect(memory=LifecycleMemory(lifecycle_fault=fault))
                witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
                self.assertTrue(lifecycle['available'])
                self.assertFalse(lifecycle['stable'] or witness['listedOccupancyComplete'])
                self.assertEqual(lifecycle['status'], 'changed')
                self.assertIn((source, field), [(r['source'], r['field']) for r in lifecycle['changes']])
                self.assertTrue(witness['readback']['before']['complete'] and witness['readback']['after']['complete'])
                self.assertEqual(len(witness['lists']['active']['nodes']), 2)
                self.assertEqual(self.projection(result), self.projection(old))
                self.assertEqual([c[0] for c in memory.calls[:17]], [c[0] for c in old_memory.calls])
                for key in ('atomic', 'pendingExclusionComplete', 'controllerIncarnationQualified', 'globalControllerIdCoverageComplete'):
                    self.assertFalse(witness[key])

    def test_raw_lifecycle_before_after_missing_and_failure_are_separate_partial_evidence(self):
        old, _, _ = self.collect(without_raw_occupancy=True)
        for phase in ('before', 'after', 'both'):
            for fault in ('failure', 'missing'):
                with self.subTest(phase=phase, fault=fault):
                    result, _, _ = self.collect(memory=LifecycleMemory(lifecycle_phase=phase, lifecycle_fault=fault))
                    witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
                    self.assertEqual(lifecycle['status'], 'unavailable' if phase == 'both' else 'partial')
                    self.assertFalse(lifecycle['available'] or lifecycle['stable'] or witness['listedOccupancyComplete'])
                    self.assertEqual(len(witness['lists']['active']['nodes']), 2)
                    self.assertTrue(witness['readback']['after']['complete'])
                    bad = lifecycle['before' if phase == 'both' else phase]
                    self.assertFalse(bad['nativeComplete'])
                    self.assertTrue(bad['logs']['arrival'])
                    if fault == 'missing':
                        self.assertEqual(bad['native']['room'], 6)
                        self.assertNotIn('map', bad['native'])
                    else:
                        self.assertEqual(bad['native'], {})
                    self.assertTrue(all(f['stage'].startswith('raw-occupancy-lifecycle-') for f in lifecycle['readFailures']))
                    self.assertNotIn('readFailures', witness)
                    self.assertEqual(self.projection(result), self.projection(old))

    def test_raw_lifecycle_missing_logs_serial_or_uncompleted_load_never_qualifies(self):
        for fault in ('logs-missing', 'serial-missing', 'queued'):
            with self.subTest(fault=fault):
                result, _, _ = self.collect(memory=LifecycleMemory(lifecycle_fault=fault))
                witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
                self.assertTrue(lifecycle['after']['nativeComplete'])
                self.assertFalse(lifecycle['after']['available'] or lifecycle['stable'] or witness['listedOccupancyComplete'])
                self.assertEqual(lifecycle['status'], 'partial')
                self.assertEqual(len(witness['lists']['active']['nodes']), 2)

    def test_raw_lifecycle_deadline_uses_existing_budget_and_preserves_finished_raw_reads(self):
        now = [0.0]
        class ExpiringMemory(LifecycleMemory):
            def __call__(self, *args, **kwargs):
                result = super().__call__(*args, **kwargs)
                if self.raw_root_reads == 2:
                    now[0] = 31.0
                return result
        result, memory, _ = self.collect(memory=ExpiringMemory(), clock=lambda: now[0])
        witness = result['rawOccupancy']; lifecycle = witness['lifecycle']
        self.assertTrue(witness['readback']['after']['complete'])
        self.assertEqual(len(witness['lists']['active']['nodes']), 2)
        self.assertEqual(lifecycle['status'], 'partial')
        self.assertFalse(lifecycle['stable'] or witness['listedOccupancyComplete'])
        self.assertEqual(lifecycle['readFailures'][0]['phase'], 'deadline-check')
        self.assertFalse(lifecycle['readWork'][-1]['peekAttempted'])
        self.assertEqual(memory.header_calls, 5)  # No late after-header subprocess or retry.
        self.assertTrue(result['complete'] and result['causeContext']['complete'])

    def test_original_output_and_read_schedule_unchanged_including_unavailable_flags(self):
        for options in ({}, {'change': (0x1006A0, 0x110000)},
                        {'failure': 'positions-after', 'omit': True}, {'missing_roots': True}):
            with self.subTest(options=options):
                before, before_memory, _ = self.collect(without_raw_occupancy=True, **options)
                after, after_memory, _ = self.collect(memory=RawMemory(**options))
                self.assertEqual(self.projection(before), self.projection(after))
                # Timeout amounts are clocks, not requested read contents.
                self.assertEqual([call[0] for call in before_memory.calls],
                                 [call[0] for call in after_memory.calls[:len(before_memory.calls)]])
                self.assertTrue(after_memory.raw_calls)
        result, memory, _ = self.collect(memory=RawMemory())
        self.assertEqual(len(memory.calls), 24)
        self.assertEqual(len(memory.raw_calls), 5)
        self.assertEqual([work['fieldCount'] for work in result['rawOccupancy']['readWork']], [68, 6, 6, 6, 86])
        self.assertTrue(all(work['complete'] and work['peekAttempted'] for work in result['rawOccupancy']['readWork']))
        self.assertEqual([work['stage'] for work in result['rawOccupancy']['lifecycle']['readWork']],
                         ['raw-occupancy-lifecycle-before', 'raw-occupancy-lifecycle-after'])
        self.assertEqual([work['fieldCount'] for work in result['rawOccupancy']['lifecycle']['readWork']], [79, 79])

    def test_masked_unready_and_deferred_records_are_collected_before_filtering(self):
        memory = RawMemory()
        base = memory.base
        memory.memory.update({0x100120: 0x10080000, 0x1005C0: 0,
                              0x1009E8: base + 0x400000, 0x1009F0: base + 0x300000,
                              0x30001E: 11, 0x2A171D8: base + 0x120000, 0x2A171E0: base + 0x120000,
                              0x120918: base + 0x200000, 0x1209F0: base + 0x300040, 0x30005E: 0})
        result, _, _ = self.collect(memory=memory)
        witness = result['rawOccupancy']
        self.assertTrue(result['complete'])
        self.assertEqual(result['livingCombatRows'], [])
        self.assertTrue(witness['listedOccupancyComplete'])
        first = witness['lists']['active']['nodes'][0]
        deferred = witness['lists']['deferred']['nodes'][0]
        self.assertEqual((first['flags120'], first['status'], first['recordId']), (0x10080000, 0, 11))
        self.assertTrue(first['readSuccess']['status'] and first['readSuccess']['recordId'])
        self.assertEqual(deferred['recordId'], 0)
        self.assertTrue(deferred['recordIdApplicable'] and deferred['readSuccess']['recordId'])
        null_record = witness['lists']['active']['nodes'][1]
        self.assertEqual(null_record['spawnRecord'], 0)
        self.assertFalse(null_record['recordIdApplicable'] or null_record['readSuccess']['recordId'])
        self.assertNotIn('recordId', null_record)
        for key in ('atomic', 'pendingExclusionComplete', 'controllerIncarnationQualified', 'globalControllerIdCoverageComplete'):
            self.assertFalse(witness[key])
        self.assertNotIn('noListedConflict', witness)

    def test_mixed_width_metadata_alias_is_rejected_before_ambiguous_cli_read(self):
        memory = TypedAddressMemory()
        memory.put_bytes(0x200004, b'\x03\x01')
        # Demonstrate the real transport failure mode: requesting both widths
        # returns one address key, while the separate byte request returns3.
        response = memory('peek', '--rva', '0x200004:u8,0x200004:u16')
        self.assertEqual(response['samples'][0]['0x200004'], 259)
        self.assertEqual(memory('peek', '--rva', '0x200004:u8')['samples'][0]['0x200004'], 3)
        memory.memory[0x1009F0] = memory.base + 0x200004 - 0x1E
        result, _, _ = self.collect(memory=memory)
        witness = result['rawOccupancy']
        self.assertFalse(witness['listedOccupancyComplete'])
        conflict = next(reason for reason in witness['reasons'] if reason['code'] == 'typed-address-conflict')
        self.assertEqual(conflict['address'], memory.base + 0x200004)
        self.assertEqual(conflict['stage'], 'metadata-before')
        self.assertEqual({(field['label'], field['type']) for field in conflict['fields']},
                         {('node0:objectType', 'u8'), ('node1:objectType', 'u8'), ('node0:recordId', 'u16')})
        self.assertFalse(witness['readback']['after']['attempted'])
        self.assertEqual(len(memory.raw_calls), 3)  # Roots and two nodes; no metadata or after read.
        node = witness['lists']['active']['nodes'][0]
        self.assertTrue(node['readSuccess']['spawnRecord'])
        self.assertFalse(node['readSuccess']['recordId'] or node['readSuccess']['objectType'])
        self.assertNotIn('recordId', node)
        self.assertFalse(witness['pendingExclusionComplete'] or witness['atomic'])

    def test_metadata_alias_with_prior_root_or_node_field_is_rejected(self):
        for target, prior_label, prior_kind in ((0x2A171C8, 'activeHead', 'u64'),
                                                (0x100120, 'node0:flags120', 'u32')):
            with self.subTest(target=hex(target)):
                memory = TypedAddressMemory()
                original = memory.memory.get(target, 0)
                memory.put_bytes(target, original.to_bytes(8 if prior_kind == 'u64' else 4, 'little'))
                memory.memory[0x1009F0] = memory.base + target - 0x1E
                result, _, _ = self.collect(memory=memory)
                witness = result['rawOccupancy']
                self.assertFalse(witness['listedOccupancyComplete'])
                conflict = next(reason for reason in witness['reasons'] if reason['code'] == 'typed-address-conflict')
                self.assertEqual(conflict['address'], memory.base + target)
                self.assertEqual({(field['label'], field['type']) for field in conflict['fields']},
                                 {(prior_label, prior_kind), ('node0:recordId', 'u16')})
                self.assertEqual(witness['readback']['before']['values'][prior_label], original)
                self.assertNotIn('node0:recordId', witness['readback']['before']['values'])
                self.assertEqual(len(memory.raw_calls), 3)
                self.assertFalse(witness['readback']['after']['attempted'])

    def test_same_type_descriptor_and_record_aliases_keep_labels_and_legacy_projection(self):
        def memory():
            value = TypedAddressMemory()
            # Both actors share the descriptor; recordId shares namePrefix's u16.
            value.put_bytes(0x200008, b'P_')
            value.memory[0x1009F0] = value.base + 0x200008 - 0x1E
            return value
        old, old_memory, _ = self.collect(memory=memory(), without_raw_occupancy=True)
        result, new_memory, _ = self.collect(memory=memory())
        self.assertEqual(self.projection(old), self.projection(result))
        self.assertEqual([call[0] for call in old_memory.calls],
                         [call[0] for call in new_memory.calls[:len(old_memory.calls)]])
        witness = result['rawOccupancy']
        self.assertTrue(witness['listedOccupancyComplete'])
        for phase in ('before', 'after'):
            values = witness['readback'][phase]['values']
            self.assertEqual([values[label] for label in ('node0:namePrefix', 'node1:namePrefix', 'node0:recordId')],
                             [0x5F50] * 3)
        self.assertTrue(witness['lists']['active']['nodes'][0]['readSuccess']['recordId'])

    def test_conflicting_node_batch_is_stopped_before_its_first_read(self):
        memory = TypedAddressMemory()
        second = 0x1007F8  # second+120 overlaps first+918 with u32 versus u64.
        memory.actor_rvas = (0x100000, second)
        memory.memory.update({0x100A90: second, 0x2A171D0: memory.base + second,
                              second + 0x918: memory.base + 0x200000})
        memory.put_bytes(0x100918, (memory.base + 0x200000).to_bytes(8, 'little'))
        result, _, _ = self.collect(memory=memory)
        witness = result['rawOccupancy']
        self.assertFalse(witness['listedOccupancyComplete'])
        conflict = next(reason for reason in witness['reasons'] if reason['code'] == 'typed-address-conflict')
        self.assertEqual(conflict['stage'], 'node-before')
        self.assertEqual(conflict['address'], memory.base + 0x100918)
        self.assertEqual({(field['label'], field['type']) for field in conflict['fields']},
                         {('node0:objectEntry', 'u64'), ('node1:flags120', 'u32')})
        self.assertEqual(len(memory.raw_calls), 2)  # Roots and first node only.
        first, second_node = witness['lists']['active']['nodes']
        self.assertEqual(first['objectEntry'], memory.base + 0x200000)
        self.assertFalse(any(second_node['readSuccess'].values()))
        self.assertFalse(witness['readback']['after']['attempted'])

    def test_missing_record_id_is_unknown_not_zero_and_does_not_change_old_result(self):
        memory = RawMemory(raw_omit=0x30001E)
        memory.memory[0x1009F0] = memory.base + 0x300000
        result, _, _ = self.collect(memory=memory)
        witness = result['rawOccupancy']; node = witness['lists']['active']['nodes'][0]
        self.assertTrue(result['complete'])
        self.assertFalse(witness['listedOccupancyComplete'])
        self.assertTrue(node['recordIdApplicable'])
        self.assertFalse(node['readSuccess']['recordId'])
        self.assertNotIn('recordId', node)
        self.assertIn('node0:recordId', witness['readback']['before']['unreadFields'])
        self.assertFalse(witness['readback']['after']['attempted'])
        self.assertEqual(witness['readFailures'][0]['stage'], 'raw-occupancy-metadata-before')
        self.assertNotIn('readFailures', result)

    def test_root_cycle_cross_list_and_handle_failures_are_explicit(self):
        cases = {
            'root-tail-null-mismatch': {0x2A171D8: 0, 0x2A171E0: Memory.base + 0x120000},
            'terminal-tail-mismatch': {0x2A171D8: Memory.base + 0x120000, 0x2A171E0: Memory.base + 0x130000,
                                       0x120918: Memory.base + 0x200000},
            'cycle-or-repeated-node': {0x110A90: 0x100000},
            'cross-list-membership': {0x2A171D8: Memory.base + 0x100000, 0x2A171E0: Memory.base + 0x110000},
            'invalid-node-pointer': {0x2A171D8: 1, 0x2A171E0: 1},
        }
        for code, changes in cases.items():
            with self.subTest(code=code):
                memory = RawMemory(); memory.memory.update(changes)
                result, _, _ = self.collect(memory=memory)
                witness = result['rawOccupancy']
                self.assertFalse(witness['listedOccupancyComplete'])
                self.assertIn(code, [reason['code'] for reason in witness['reasons']])
        for bucket in (0, 0xffffffffffffffff, Memory.base + 1):
            with self.subTest(bucket=bucket):
                memory = RawMemory(); memory.memory[0x2B0D720] = bucket
                result, _, _ = self.collect(memory=memory)
                self.assertIn('invalid-handle-bucket', [r['code'] for r in result['rawOccupancy']['reasons']])

    def test_every_identity_root_link_bucket_and_record_field_recheck_can_invalidate(self):
        for rva, changed in ((0x2A171D8, Memory.base + 0x120000), (0x2B0D720, Memory.base + 0x2000000),
                             (0x100A90, 0), (0x100120, 0x80000), (0x100918, 0),
                             (0x1005C0, Memory.base + 0x500000), (0x1009E8, Memory.base + 0x400000),
                             (0x1009F0, 0), (0x30001E, 18), (0x200004, 4)):
            with self.subTest(rva=hex(rva)):
                memory = RawMemory(raw_change=(rva, changed))
                memory.memory.update({0x1009F0: memory.base + 0x300000, 0x30001E: 11})
                result, _, _ = self.collect(memory=memory)
                self.assertTrue(result['complete'])
                witness = result['rawOccupancy']
                self.assertFalse(witness['listedOccupancyComplete'])
                self.assertEqual(len(witness['readback']['changes']), 2 if rva == 0x200004 else 1)
                self.assertTrue(all(change['address'] == memory.base + rva for change in witness['readback']['changes']))
                self.assertEqual(witness['lists']['active']['nodes'][0]['recordId'], 11)

    def test_partial_node_and_failed_recheck_preserve_only_completed_values(self):
        result, _, _ = self.collect(memory=RawMemory(raw_omit=0x1009E8))
        witness = result['rawOccupancy']; first = witness['lists']['active']['nodes'][0]
        self.assertFalse(witness['listedOccupancyComplete'])
        self.assertTrue(first['readSuccess']['status'])
        self.assertFalse(first['readSuccess']['controller'])
        self.assertNotIn('controller', first)
        self.assertFalse(witness['readback']['after']['attempted'])
        result, _, _ = self.collect(memory=RawMemory(raw_failure=True))
        witness = result['rawOccupancy']
        self.assertTrue(result['complete'])
        self.assertFalse(witness['listedOccupancyComplete'])
        self.assertTrue(witness['readback']['before']['complete'])
        self.assertTrue(witness['readback']['after']['attempted'])
        self.assertFalse(witness['readback']['after']['complete'])
        self.assertEqual(witness['readback']['after']['values'], {})
        result, _, _ = self.collect(memory=RawMemory(raw_omit_after=0x2A171E0))
        witness = result['rawOccupancy']
        self.assertFalse(witness['listedOccupancyComplete'])
        self.assertEqual(list(witness['readback']['after']['values']), ['activeHead', 'activeTail', 'deferredHead'])
        self.assertIn('deferredTail', witness['readback']['after']['unreadFields'])
        self.assertEqual(len(witness['lists']['active']['nodes']), 2)

    def test_high_handle_bit_is_not_termination_and_empty_lists_are_checked(self):
        memory = RawMemory(); memory.memory[0x100A90] = 0x80110000
        result, _, _ = self.collect(memory=memory)
        self.assertTrue(result['rawOccupancy']['listedOccupancyComplete'])
        self.assertEqual(len(result['rawOccupancy']['lists']['active']['nodes']), 2)
        memory = RawMemory(); memory.memory.update({0x2A171C8: 0, 0x2A171D0: 0})
        result, _, _ = self.collect(memory=memory)
        self.assertFalse(result['complete'])  # Existing empty primary census remains unavailable.
        self.assertTrue(result['rawOccupancy']['listedOccupancyComplete'])
        self.assertFalse(result['rawOccupancy']['pendingExclusionComplete'])

    def test_cap_exhaustion_and_invalid_nonnull_record_pointer(self):
        memory = RawMemory()
        start = 0x500000
        memory.memory.update({0x2A171D8: memory.base + start, 0x2A171E0: memory.base + start + 254 * 0x1000})
        for n in range(255):
            address = start + n * 0x1000
            memory.memory[address + 0x918] = memory.base + 0x200000
            memory.memory[address + 0xA90] = address + 0x1000 if n < 254 else 0
        result, _, _ = self.collect(memory=memory)
        witness = result['rawOccupancy']
        self.assertFalse(witness['listedOccupancyComplete'])
        self.assertEqual(sum(len(lane['nodes']) for lane in witness['lists'].values()), 256)
        self.assertIn('node-cap-exhausted', [r['code'] for r in witness['reasons']])
        memory = RawMemory(); memory.memory[0x1009F0] = 1
        result, _, _ = self.collect(memory=memory)
        self.assertFalse(result['rawOccupancy']['listedOccupancyComplete'])
        self.assertTrue(result['rawOccupancy']['lists']['active']['nodes'][0]['recordIdApplicable'])
        self.assertIn('invalid-field-pointer', [r['code'] for r in result['rawOccupancy']['reasons']])


if __name__ == '__main__':
    unittest.main()
