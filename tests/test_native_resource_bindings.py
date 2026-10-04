"""Production AST and address-keyed CLI controls; no native process is accessed."""
import ast
import copy
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from types import SimpleNamespace

from test_geometry_position_receipts import collector_module, Memory as GeometryMemory

ROOT = Path(__file__).resolve().parents[1]


class Memory:
    base = 0x140000000

    def __init__(self):
        self.bytes, self.calls, self.stage = {}, [], ''
        self.faults = {}
        self.now = 0.0

    def put(self, rva, kind, value):
        size = {'u8': 1, 'u16': 2, 'u32': 4, 'i32': 4, 'u64': 8}[kind]
        self.bytes.update({rva+i: v for i, v in enumerate(value.to_bytes(size, 'little', signed=kind == 'i32'))})

    def __call__(self, *args, **kwargs):
        assert args[:2] == ('peek', '--rva')
        self.calls.append((self.stage, args, kwargs))
        action = self.faults.get(self.stage)
        if action == 'failure':
            raise PermissionError('synthetic checked read failure')
        sample = {}
        for token in args[2].split(','):
            address, kind = token.split(':'); rva = int(address, 0)
            size = {'u8': 1, 'u16': 2, 'u32': 4, 'i32': 4, 'u64': 8}[kind]
            if all(rva+i in self.bytes for i in range(size)):
                value = int.from_bytes(bytes(self.bytes[rva+i] for i in range(size)), 'little', signed=kind == 'i32')
                sample[address] = hex(value) if kind == 'u64' else value
        if callable(action):
            action(sample)
        return {'moduleBase': hex(self.base), 'samples': [sample]}


class Bindings(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        logs = Path(self.temp.name)
        self.log_path = logs / 'kh2coop_inject_123.log'
        self.log_path.write_text('[warp] load complete serial=4 transition=3 room=05/06 door=0 map=1 btl=1 evt=0\n'
            '[enemysync] client arrived epoch=1 room=05/06 door=0 map=1 btl=1 evt=0\n')
        self.memory = m = Memory(); b = m.base
        self.env = env = collector_module(logs)
        tree = ast.parse((ROOT/'tools/scenario/run.py').read_text(encoding='utf-8'))
        helper = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'capture_native_resource_bindings')
        census = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'native_enemy_census_snapshot')
        nested = [n for n in census.body if isinstance(n, ast.FunctionDef) and n.name in
                  ('remaining', 'valid_pointer', 'read', 'header', 'logs', 'raw_lifecycle_read')]
        env.update(base=b, pid=123, deadline=30.0, out={}, kh2ctl=m,
                   time=type('Clock', (), {'monotonic': lambda _: m.now})())
        exec(compile(ast.Module(body=[helper]+nested, type_ignores=[]), '<production binding and readers>', 'exec'), env)
        real_read = env['read']
        def checked_read(fields, result=None, **kwargs):
            m.stage = kwargs.get('stage', 'native-field-batch')
            return real_read(fields, result, **kwargs)
        env['read'] = checked_read
        self.read = checked_read
        self.lifecycle = env['raw_lifecycle_read']
        for rva, kind, value in ((0x2A171C8, 'u64', b+0x100000), (0x2A171D0, 'u64', b+0x100000),
                (0x2A171D8, 'u64', 0), (0x2A171E0, 'u64', 0), (0x2AE6680, 'u64', b+0x2AE5E60),
                (0x2AE6688, 'u32', 1), (0x9BA8D0, 'u8', 1), (0x2A171E8, 'u32', 0),
                (0xB65210, 'i32', 0), (0x2A11478, 'u64', 0), (0x7435D0, 'u8', 255),
                (0x717008, 'u8', 5), (0x717009, 'u8', 6), (0x71700A, 'u8', 0),
                (0x71700C, 'u16', 1), (0x71700E, 'u16', 1), (0x717010, 'u16', 0),
                (0x9BA920, 'u64', b+0x200000), (0x200000, 'u64', b+0x5B2BB0),
                (0x5B2BB8, 'u64', b+0x19C2B0)):
            m.put(rva, kind, value)
        for i in range(64):
            m.put(0x2B0D720+8*i, 'u64', b if i == 0 else 0)
        self.nodes, self.provenance = [], []
        self.add_actor(0)
        self.refresh_census()

    def add_actor(self, index, kind=3):
        m = self.memory; b = m.base
        x, o, s, c, r, bar, model = (0x100000+index*0x1000, 0x300000+index*0x100,
            0x310000+index*0x100, 0x320000+index*0x100, 0x330000+index*0x100,
            0x400000+index*0x1000, 0x500000+index*0x1000)
        node = dict(address=b+x, objectEntry=b+o, status=b+s, controller=b+c, spawnRecord=b+r,
                    nextHandle=0, flags120=0, objectId=302, objectType=4, combatEligible=True, hp=17)
        self.nodes.append(node)
        raw = bytearray(64); struct.pack_into('<I', raw, 0, 302); struct.pack_into('<H', raw, 0x1E, 11+index)
        self.provenance.append(dict(actor=b+x, controller=b+c, pointer=b+r, recordId=11+index,
            objectId=302, ordinaryTableIdentityVerified=True, hex=raw.hex()))
        for off, name in ((0x918, 'objectEntry'), (0x5C0, 'status'), (0x9E8, 'controller'), (0x9F0, 'spawnRecord')):
            m.put(x+off, 'u64', node[name])
        for rva, typ, val in ((x+0x120, 'u32', 0), (x+0xA90, 'u32', 0), (x+0x920, 'u64', b+bar),
                (x+0xA88, 'u64', b+model), (o, 'u32', 302), (o+4, 'u8', 4), (o+8, 'u16', 0x5F4D),
                (r+0x1E, 'u16', 11+index), (bar+4, 'i32', 3),
                (bar+0x10, 'u16', 2), (bar+0x20, 'u16', 4), (bar+0x30, 'u16', 4),
                (bar+0x28, 'u32', 0x80000000 | model), (bar+0x2C, 'u32', 0x94),
                (model+8, 'u64', b+model+0x90), (model+0x90, 'i32', kind)):
            m.put(rva, typ, val)
        vp, target = {3: (0x5B3E00, 0x1C21E0), 2: (0x5B3D40, 0x1C1500), -1: (0x5B3EC0, 0x1C2CA0)}[kind]
        m.put(model, 'u64', b+vp); m.put(vp+0x40, 'u64', b+target)

    def refresh_census(self):
        m = self.memory; b = m.base
        for i, node in enumerate(self.nodes):
            node['nextHandle'] = (self.nodes[i+1]['address']-b) | 0x80000000 if i+1 < len(self.nodes) else 0
            m.put(node['address']-b+0xA90, 'u32', node['nextHandle'])
        m.put(0x2A171C8, 'u64', self.nodes[0]['address'] if self.nodes else 0)
        m.put(0x2A171D0, 'u64', self.nodes[-1]['address'] if self.nodes else 0)
        raw = self.env['capture_raw_native_occupancy'](b, self.read, lifecycle_read=self.lifecycle)
        self.assertTrue(raw['listedOccupancyComplete'])
        self.census = dict(pid=123, moduleBase=hex(b), nodes=copy.deepcopy(self.nodes),
            livingCombatRows=copy.deepcopy(self.nodes), provenance=copy.deepcopy(self.provenance), rawOccupancy=raw,
            after=copy.deepcopy(raw['lifecycle']['after']['native']), logsAfter=copy.deepcopy(raw['lifecycle']['after']['logs']))
        self.census.update({k: True for k in ('complete', 'listComplete', 'classificationComplete',
            'actorIdentityStable', 'lifecycleStable', 'epochStable', 'safeGameplay')})
        m.calls.clear()

    def collect(self, base=None):
        before = copy.deepcopy(self.census)
        result = self.env['capture_native_resource_bindings'](self.memory.base if base is None else base,
            self.read, self.census, lifecycle_read=self.lifecycle)
        self.assertEqual(before, self.census)
        for k in ('executedCallObserved', 'callbackClosureComplete', 'pendingExclusionComplete',
                  'controllerIncarnationQualified', 'atomic', 'creationAuthority'):
            self.assertFalse(result[k])
        return result

    def test_three_exact_class_pairs_first_type4_and_raw_signed_bytes(self):
        self.add_actor(1, 2); self.add_actor(2, -1); self.refresh_census()
        result = self.collect()
        self.assertTrue(result['allocatorPointerBindingComplete'] and result['actorCoverageComplete'])
        self.assertEqual([a['modelClass'] for a in result['actors']], ['ModelSKL', 'ModelBG', 'ModelMulti'])
        for a in result['actors']:
            self.assertEqual(a['barTypes'], [2, 4, 4]); self.assertEqual(a['selectedIndex'], 1)
            self.assertEqual(a['selectedPrefix'], [2, 4]); self.assertTrue(a['modelPointerBindingComplete'])
        self.assertEqual(result['readback']['before']['hex']['actor2:modelKind'], 'ffffffff')
        self.assertEqual(result['readback']['before']['hex'], result['readback']['after']['hex'])
        self.assertEqual(result['readback']['changes'], [])
        stages = [c[0] for c in self.memory.calls]
        self.assertEqual(stages[0], 'resource-binding-lifecycle-before')
        self.assertEqual(stages[-1], 'resource-binding-lifecycle-after')
        self.assertEqual(len(stages), 10)  # Seven staged field batches, one readback, two headers.

    def test_shape_rejections_stop_dependent_reads_but_preserve_allocator(self):
        cases = [(0x400004, 'i32', 0, 'unsupported-bar-count'), (0x400004, 'i32', -1, 'unsupported-bar-count'),
            (0x400004, 'i32', 65, 'unsupported-bar-count'), (0x400028, 'u32', 0, 'unsupported-tagged-entry-shape'),
            (0x400028, 'u32', 0x500000, 'unsupported-tagged-entry-shape'),
            (0x40002C, 'u32', 0x93, 'unsupported-tagged-entry-shape'),
            (0x100A88, 'u64', self.memory.base+0x500008, 'cached-model-mismatch'),
            (0x500008, 'u64', self.memory.base+0x500098, 'model-data-mismatch'),
            (0x500090, 'i32', 7, 'unsupported-model-pair'),
            (0x500000, 'u64', self.memory.base+0x5B2D40, 'unsupported-model-pair'),
            (0x5B3E40, 'u64', self.memory.base+0x1C1500, 'model-target-mismatch')]
        initial = copy.deepcopy(self.memory.bytes)
        for rva, kind, value, code in cases:
            with self.subTest(code=code, value=value):
                self.memory.bytes = copy.deepcopy(initial); self.memory.put(rva, kind, value); self.memory.calls.clear()
                r = self.collect(); a = r['actors'][0]
                self.assertFalse(a['modelPointerBindingComplete'] or r['actorCoverageComplete'])
                self.assertTrue(r['allocatorPointerBindingComplete'])
                self.assertIn(code, [x['code'] for x in a['reasons']])
                if code == 'unsupported-model-pair':
                    self.assertNotIn('actor0:modelTarget', r['readback']['fields'])
        self.memory.bytes = initial
        self.memory.put(0x400020, 'u16', 2); self.memory.put(0x400030, 'u16', 3)
        r = self.collect(); self.assertIn('bar-type4-not-found', [x['code'] for x in r['actors'][0]['reasons']])

    def test_allocator_shape_and_target_are_independent_of_model(self):
        for rva, value in ((0x9BA920, 0), (0x9BA920, self.memory.base+0x200008),
                           (0x200000, self.memory.base+0x5B2D40), (0x5B2BB8, self.memory.base+1)):
            initial = copy.deepcopy(self.memory.bytes)
            with self.subTest(rva=hex(rva), value=value):
                self.memory.put(rva, 'u64', value); r = self.collect()
                self.assertFalse(r['allocatorPointerBindingComplete'])
                self.assertTrue(r['actors'][0]['modelPointerBindingComplete'])
                self.assertTrue(r['allocator']['reasons'])
            self.memory.bytes = initial

    def test_bar_cap64_and_missing_later_type_do_not_accept_only_a_readable_prefix(self):
        self.memory.put(0x400004, 'i32', 64)
        for n in range(64): self.memory.put(0x400010+16*n, 'u16', 4 if n == 1 else 2)
        r = self.collect(); self.assertTrue(r['actors'][0]['modelPointerBindingComplete'])
        self.assertEqual(len(r['actors'][0]['barTypes']), 64)
        stage = next(s for s in r['stages'] if s['stage'] == 'allocator-slot-bar-types')
        self.assertEqual(len(stage['fieldLabels']), 65)  # Allocator slot plus all bounded BAR types.
        self.memory.faults['resource-binding-allocator-slot-bar-types'] = lambda s: s.pop('0x400400')
        r = self.collect(); self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
        self.assertIn('actor0:barType1', r['readback']['before']['hex'])
        self.assertNotIn('actor0:handle', r['readback']['fields'])

    def test_malformed_bucket_and_resource_end_are_supported_shape_failures(self):
        for region in (0, 0xFFFFFFFFFFFFFFFF, self.memory.base+8, 0x800000000000):
            self.memory.put(0x2B0D720, 'u64', region)
            r = self.collect(); self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
            self.assertIn('invalid-handle-region', [x['code'] for x in r['actors'][0]['reasons']])
        self.memory.put(0x2B0D720, 'u64', 0x7FFFFE000000)
        self.memory.put(0x400028, 'u32', 0x81FFFFF8)
        self.memory.put(0x100A88, 'u64', 0x7FFFFFFFFFF8)
        self.refresh_census(); r = self.collect()
        self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
        self.assertIn('unsupported-pointer-span', [x['code'] for x in r['actors'][0]['reasons']])
        self.assertNotIn('actor0:modelVtable', r['readback']['fields'])

    def test_zero_actors_samples_allocator_and_cap_does_not_expand(self):
        self.nodes.clear(); self.provenance.clear(); self.refresh_census()
        r = self.collect(); self.assertTrue(r['allocatorPointerBindingComplete'])
        self.assertEqual(r['actors'], []); self.assertEqual(r['eligibleActors'], 0)
        self.assertIn('no-qualified-actors', [x['code'] for x in r['reasons']])
        self.census.update(complete=False)
        self.census.pop('after'); self.census.pop('logsAfter')  # Real empty primary census stops before these.
        r = self.collect(); self.assertTrue(r['allocatorPointerBindingComplete'])
        self.assertFalse(r['actorCoverageComplete'])
        for i in range(17): self.add_actor(i)
        self.refresh_census(); r = self.collect()
        self.assertEqual((r['eligibleActors'], r['attemptedActors'], r['omittedActors']), (17, 16, 1))
        self.assertFalse(r['actorCoverageComplete'])
        self.assertTrue(all(a['modelPointerBindingComplete'] for a in r['actors']))
        self.assertNotIn('actor16:bar', r['readback']['fields'])

    def test_unqualified_census_raw_and_record_joins_are_not_promoted(self):
        original = copy.deepcopy(self.census)
        changes = [lambda c: c.update(complete=False),
            lambda c: c['rawOccupancy'].update(listedOccupancyComplete=False),
            lambda c: c['rawOccupancy']['lifecycle']['before']['rawReadbackJoin'].update(complete=False),
            lambda c: c['provenance'][0].update(ordinaryTableIdentityVerified=False),
            lambda c: c['rawOccupancy']['lists']['active']['nodes'][0].update(status=0),
            lambda c: c['provenance'][0].update(recordId=99)]
        for change in changes:
            self.census = copy.deepcopy(original); change(self.census); r = self.collect()
            self.assertEqual(r['actors'], []); self.assertFalse(r['actorCoverageComplete'])
        self.census = original

    def test_missing_fields_and_read_exceptions_retain_partial_bytes_and_separate_failures(self):
        self.memory.faults['resource-binding-model-shape'] = lambda sample: sample.pop('0x500008')
        r = self.collect()
        self.assertIn('actor0:modelVtable', r['readback']['before']['hex'])
        self.assertNotIn('actor0:modelData', r['readback']['before']['hex'])
        self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
        self.assertTrue(r['allocatorPointerBindingComplete'])
        self.assertEqual(r['readFailures'][0]['stage'], 'resource-binding-model-shape')
        self.memory.faults = {'resource-binding-selected-entry': 'failure'}
        r = self.collect(); self.assertIn('actor0:barType2', r['readback']['before']['hex'])
        self.assertNotIn('actor0:handle', r['readback']['before']['values'])
        self.assertTrue(r['readback']['after']['complete'])

    def test_frozen_readback_drift_never_chases_new_roots(self):
        self.memory.faults['resource-binding-frozen-address-readback'] = lambda s: s.update({'0x100920': hex(self.memory.base+0x600000)})
        r = self.collect(); self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
        self.assertTrue(r['allocatorPointerBindingComplete'])
        self.assertEqual(r['readback']['changes'][0]['field'], 'actor0:bar')
        self.assertEqual(r['readback']['before']['values']['actor0:bar'], self.memory.base+0x400000)
        self.assertFalse(any('0x600004:' in args[2] for _, args, _ in self.memory.calls))

    def test_after_read_failure_preserves_partial_phase_without_borrowed_values(self):
        self.memory.faults['resource-binding-frozen-address-readback'] = lambda s: s.pop('0x100918')
        r = self.collect()
        self.assertEqual(list(r['readback']['after']['values']), ['allocator:root'])
        self.assertIn('actor0:objectEntry', r['readback']['before']['hex'])
        self.assertNotIn('actor0:objectEntry', r['readback']['after']['hex'])
        self.assertFalse(r['allocatorPointerBindingComplete'] or r['actorCoverageComplete'])
        self.assertEqual(r['readFailures'][0]['stage'], 'resource-binding-frozen-address-readback')

    def test_actual_fresh_bucket_local_and_parent_roots_must_agree(self):
        self.memory.faults['resource-binding-fresh-handle-bucket'] = lambda s: s.update({'0x2B0D720': hex(self.memory.base+0x2000000)})
        r = self.collect(); self.assertFalse(r['lifecycle']['stable'] or r['allocatorPointerBindingComplete'])
        self.assertTrue(any('fresh selected bucket' in x.get('error', '') for x in r['lifecycle']['reasons']))
        self.memory.faults = {'resource-binding-lifecycle-before': lambda s: s.update({'0x2A171C8': '0x0', '0x2A171D0': '0x0'}),
                              'resource-binding-lifecycle-after': lambda s: s.update({'0x2A171C8': '0x0', '0x2A171D0': '0x0'})}
        r = self.collect(); self.assertFalse(r['lifecycle']['stable'])
        self.assertTrue(r['readback']['after']['complete'])

    def test_lifecycle_drift_missing_and_deadline_keep_resource_receipt(self):
        self.memory.faults['resource-binding-lifecycle-after'] = lambda s: self.log_path.write_text(
            self.log_path.read_text().replace('serial=4', 'serial=5'))
        r = self.collect(); self.assertFalse(r['lifecycle']['stable'] or r['actorCoverageComplete'])
        self.assertTrue(r['readback']['after']['complete'])
        self.memory.faults = {'resource-binding-lifecycle-before': 'failure'}
        r = self.collect(); self.assertFalse(r['lifecycle']['stable'])
        self.assertTrue(r['readback']['after']['complete']); self.assertIn('readFailures', r['lifecycle'])
        self.memory.faults = {'resource-binding-model-slot': lambda s: setattr(self.memory, 'now', 31.0)}
        self.memory.calls.clear(); r = self.collect()
        self.assertIn('actor0:modelTarget', r['readback']['before']['values'])
        self.assertFalse(r['readback']['after']['complete'] or r['allocatorPointerBindingComplete'])
        self.assertTrue(r['readback']['after']['requested'])
        self.assertFalse(r['readback']['after']['attempted'])
        self.assertFalse(any(stage.endswith('readback') or stage.endswith('lifecycle-after') for stage, _, _ in self.memory.calls))
        self.assertTrue(any(f['phase'] == 'deadline-check' for f in r['readFailures']))

    def test_strict_typed_values_and_address_ranges_are_not_cast_or_wrapped(self):
        for value in (True, 4.0, -1, 1 << 16):
            with self.subTest(value=value):
                self.memory.faults = {'resource-binding-allocator-slot-bar-types': lambda s, v=value: s.update({'0x400020': v})}
                r = self.collect(); self.assertNotIn('actor0:barType1', r['readback']['before']['hex'])
                self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
        self.memory.faults = {}
        for value in (self.memory.base-8, 0x7FFFFFFFFFFC, self.memory.base+0x400002):
            self.memory.put(0x100920, 'u64', value); r = self.collect()
            self.assertFalse(r['actors'][0]['modelPointerBindingComplete'])
            self.assertIn('unsupported-pointer-span', [x['code'] for x in r['actors'][0]['reasons']])
        self.assertFalse(self.collect(base=True)['allocatorPointerBindingComplete'])

    def test_mixed_width_alias_is_blocked_across_stages_and_same_type_sharing_survives(self):
        # BAR type at B+10 aliases the earlier u64 allocator root.
        self.memory.put(0x100920, 'u64', self.memory.base+0x9BA910)
        self.memory.put(0x9BA914, 'i32', 1)
        r = self.collect()
        conflicts = [x for x in r['reasons'] if x['code'] == 'typed-address-conflict']
        self.assertTrue(conflicts); self.assertEqual(conflicts[0]['address'], self.memory.base+0x9BA920)
        self.assertFalse(r['actors'][0]['modelPointerBindingComplete'] or r['allocatorPointerBindingComplete'])
        for _, args, _ in self.memory.calls:
            types = {}
            for token in args[2].split(','):
                address, kind = token.split(':'); types.setdefault(address, set()).add(kind)
            self.assertTrue(all(len(kinds) == 1 for kinds in types.values()))
        self.memory.put(0x100920, 'u64', self.memory.base+0x400000)
        self.add_actor(1); self.refresh_census()
        self.memory.put(0x101920, 'u64', self.memory.base+0x400000)
        self.memory.put(0x101A88, 'u64', self.memory.base+0x500000)
        r = self.collect(); self.assertTrue(r['actorCoverageComplete'])
        self.assertEqual(r['actors'][0]['model'], r['actors'][1]['model'])
        self.assertIn('actor0:modelKind', r['readback']['fields']); self.assertIn('actor1:modelKind', r['readback']['fields'])


class Integration(unittest.TestCase):
    def run_collector(self, option=None, *, late_failure=False):
        class CollectorMemory(GeometryMemory):
            def __init__(self):
                super().__init__()
                self.header_count = 0; self.now = 0.0
                self.memory.update({0x9BA920: self.base+0x200000, 0x200000: self.base+0x5B2BB0,
                                    0x5B2BB8: self.base+0x19C2B0})
            def __call__(self, *args, **kwargs):
                if args[0] == 'peek' and args[2].startswith('0x2A171C8:u64,0x2A171D0:u64,0x2AE6680:u64,'):
                    self.header_count += 1
                    if self.header_count == 6:
                        self.now = 25.0
                result = super().__call__(*args, **kwargs)
                if late_failure and self.header_count >= 7:
                    raise PermissionError('synthetic binding-only failure')
                return result
        memory = CollectorMemory()
        with tempfile.TemporaryDirectory() as temp:
            logs = Path(temp)
            value = 2166136261
            for byte in struct.pack('<II', 0x3145484B, 0):
                value = ((value ^ byte) * 16777619) & 0xffffffff
            (logs/'kh2coop_inject_123.log').write_text(
                '[warp] load complete serial=10 transition=10 room=05/06 door=0 map=1 btl=1 evt=0\n'
                '[enemysync] host arrived epoch=10 room=05/06 door=0 map=1 btl=1 evt=0\n'
                f'[statehash] role=host epoch=10 frame=100 room=05/06 door=0 map=1 btl=1 evt=0 enemies={value:08X} progress=12345678 count=0 unmatched=0 observed=0\n')
            env = collector_module(logs)
            tree = ast.parse((ROOT/'tools/scenario/run.py').read_text(encoding='utf-8'))
            helper = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'capture_native_resource_bindings')
            exec(compile(ast.Module(body=[helper], type_ignores=[]), '<production binding helper>', 'exec'), env)
            env.update(kh2ctl=memory, time=SimpleNamespace(monotonic=lambda: memory.now))
            kwargs = {} if option is None else {'resource_bindings': option}
            result = env['native_enemy_census_snapshot'](SimpleNamespace(inst=lambda index: SimpleNamespace(pid=123)), 0, **kwargs)
        return result, memory

    def test_default_false_is_unchanged_and_opt_in_appends_shared_deadline_reads(self):
        default, default_memory = self.run_collector()
        disabled, disabled_memory = self.run_collector(False)
        enabled, enabled_memory = self.run_collector(True)
        self.assertNotIn('resourceBindings', default)
        self.assertEqual(default, disabled)
        self.assertEqual(default_memory.calls, disabled_memory.calls)
        self.assertEqual(len(default_memory.calls), 24)
        self.assertTrue(default['complete'] and default['causeContext']['complete'])
        self.assertEqual({k:v for k,v in enabled.items() if k != 'resourceBindings'}, default)
        self.assertEqual(enabled_memory.calls[:24], default_memory.calls)
        self.assertGreater(len(enabled_memory.calls), 24)
        self.assertTrue(all(0 < call[1]['timeout'] <= 5.0 for call in enabled_memory.calls[24:]))
        self.assertTrue(all(call[0][0] == 'peek' for call in enabled_memory.calls[24:]))
        self.assertEqual(enabled['resourceBindings']['deadlineOwner'], 'supplied census read/lifecycle readers')

    def test_opt_in_failure_is_retained_without_changing_parent_verdict(self):
        disabled, old_memory = self.run_collector(False, late_failure=True)
        enabled, new_memory = self.run_collector(True, late_failure=True)
        self.assertEqual({k:v for k,v in enabled.items() if k != 'resourceBindings'}, disabled)
        self.assertEqual(new_memory.calls[:len(old_memory.calls)], old_memory.calls)
        self.assertFalse(enabled['resourceBindings']['allocatorPointerBindingComplete'])
        self.assertFalse(enabled['resourceBindings']['actorCoverageComplete'])
        self.assertTrue(enabled['resourceBindings']['lifecycle']['reasons'])
        self.assertTrue(enabled['complete'] and enabled['rawOccupancy']['listedOccupancyComplete'])

    def step_environment(self):
        tree = ast.parse((ROOT/'tools/scenario/run.py').read_text(encoding='utf-8'))
        nodes = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in
                 ('step_native_enemy_census', 'validate_scenario')]
        steps = next(n for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'STEPS' for t in n.targets))
        env = dict(Context=object, StepFailed=RuntimeError, ThreadPoolExecutor=ThreadPoolExecutor, json=json,
                   STEPS={k.value: None for k in steps.value.keys}, native_geometry_comparisons=lambda peers: [])
        for node in tree.body:
            if (isinstance(node, ast.Expr) and isinstance(node.value, ast.Call)
                    and isinstance(node.value.func, ast.Attribute) and node.value.func.attr == 'update'
                    and isinstance(node.value.func.value, ast.Name) and node.value.func.value.id == 'STEPS'):
                env['STEPS'].update({k.value: None for k in node.value.args[0].keys})
        exec(compile(ast.Module(body=nodes, type_ignores=[]), '<production step and validation>', 'exec'), env)
        return env

    def test_step_threads_option_and_validation_rejects_nonbool_before_collection(self):
        env = self.step_environment()
        calls = []
        def capture(ctx, index, timeout, **kwargs):
            calls.append((index, timeout, kwargs))
            return {'complete': True, 'causeContext': {'complete': True}, 'comparison': {'nativeLivingCount': 0}}
        env['native_enemy_census_snapshot'] = capture
        with tempfile.TemporaryDirectory() as temp:
            ctx = SimpleNamespace(instances=[object()], sleep=lambda seconds: None, check_all=lambda: None,
                                  saved={}, artifacts=[], run_dir=Path(temp))
            for option in (False, True):
                env['step_native_enemy_census'](ctx, {'resourceBindings': option, 'samples': 1, 'as': str(option)})
                self.assertEqual(calls[-1], (0, 30, {'resource_bindings': option}))
            for option in (None, 0, 1, 'true', [], {}):
                with self.subTest(option=option):
                    scenario = {'steps': [{'do': 'boot'}, {'do': 'native_enemy_census', 'resourceBindings': option}]}
                    with self.assertRaises(ValueError): env['validate_scenario'](scenario)
                    before = len(calls)
                    with self.assertRaises(RuntimeError): env['step_native_enemy_census'](ctx, scenario['steps'][1])
                    self.assertEqual(len(calls), before)
            for step in ({'do': 'native_enemy_census'}, {'do': 'native_enemy_census', 'resourceBindings': False},
                         {'do': 'native_enemy_census', 'resourceBindings': True}):
                env['validate_scenario']({'steps': [{'do': 'boot'}, step]})

    def test_new_fixture_is_only_first_six_census_opt_ins_and_name_change(self):
        old_path = ROOT/'tools/scenario/scenarios/net_reconnect_shadows_outside_endpoints_trace.json'
        new_path = old_path.with_name('net_reconnect_shadows_resource_bindings_trace.json')
        self.assertEqual(hashlib.sha256(old_path.read_bytes()).hexdigest().upper(),
                         '10B4DBA17C11A75BA2CF2AE7B49FB45FE955AA0BC1DD6CDF43041BBB4B127ED1')
        old, new = (json.loads(p.read_text(encoding='utf-8')) for p in (old_path, new_path))
        self.assertEqual(len(old['steps']), 282); self.assertEqual(len(new['steps']), 282)
        selected = [i for i,s in enumerate(new['steps']) if 'resourceBindings' in s]
        self.assertEqual(selected, [42, 64, 76, 93, 104, 108])
        self.assertEqual(selected, [i for i,s in enumerate(old['steps']) if s['do'] == 'native_enemy_census'][:6])
        for i in selected:
            self.assertIs(new['steps'][i].pop('resourceBindings'), True)
        self.assertEqual(new.pop('name'), 'net_reconnect_shadows_resource_bindings_trace')
        old.pop('name'); self.assertEqual(new, old)
        self.step_environment()['validate_scenario'](json.loads(new_path.read_text()))


if __name__ == '__main__':
    unittest.main()
