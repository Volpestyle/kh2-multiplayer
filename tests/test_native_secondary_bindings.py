"""Actual opt-in production collector/readers over owned byte-backed CLI replies."""
import ast
import copy
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest

import test_native_resource_bindings as resource_tests
from test_geometry_position_receipts import collector_module, Memory as GeometryMemory

ROOT = Path(__file__).resolve().parents[1]


class Secondary(unittest.TestCase):
    def setUp(self):
        resource_tests.Bindings.setUp(self)
        self.m = self.memory
        self.base = self.m.base
        self.T, self.B, self.W, self.I = 0x900000, 0x800000, 0x600000, 0x700000
        self.original_cli = self.env['kh2ctl']
        self.identity = 123
        def cli(*args, **kw):
            result = self.original_cli(*args, **kw)
            result['processId'] = self.identity
            return result
        self.env['kh2ctl'] = cli
        tree = ast.parse((ROOT/'tools/scenario/run.py').read_text(encoding='utf-8'))
        fn = next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='capture_native_secondary_bindings')
        exec(compile(ast.Module(body=[fn],type_ignores=[]),'<actual secondary helper>','exec'),self.env)
        self.census={'pid':123,'moduleBase':hex(self.base),'nodes':[], 'rawOccupancy':{'listedOccupancyComplete':False}}
        for rva,value in ((0x2A25030,self.T),(0x2A25038,0),(0x2A25040,0),(0x2AE5E50,self.B),
                          (0x2AE5A38,self.W),(0x2A25370,self.I)):
            self.m.put(rva,'u64',self.base+value if value else 0)
        self.object_rows([(84,2,1,0),(302,1001,0,11)])
        self.raw(self.B,struct.pack('<IiII',0x01524142,2,0,0))
        self.descriptor(0,b'went',self.W,512)
        self.descriptor(1,b'item',self.I,128)
        self.raw(self.W,bytes(512)); self.m.put(self.W+4,'u32',16)
        self.m.put(self.W+4*19,'u32',302)
        self.raw(self.I,bytes(128)); self.m.put(self.I+4,'i32',3)
        self.item(0,10,3); self.item(1,10,9); self.item(2,20,4)
        self.equip(2,10,10)

    def add_actor(self,index,kind=3):
        return resource_tests.Bindings.add_actor(self,index,kind)

    def refresh_census(self):
        return resource_tests.Bindings.refresh_census(self)

    def raw(self,rva,data):
        self.m.bytes.update({rva+i:v for i,v in enumerate(data)})

    def object_rows(self,rows,root=None):
        root=self.T if root is None else root
        self.raw(root,struct.pack('<Ii',3,len(rows)))
        for i,(oid,selector,group,form) in enumerate(rows):
            raw=bytearray(96); struct.pack_into('<I',raw,0,oid); raw[4]=4
            struct.pack_into('<HH',raw,0x4C,selector,group); raw[0x57]=form&255
            self.raw(root+8+96*i,raw)

    def descriptor(self,index,name,pointer,length,typ=2):
        self.raw(self.B+16+16*index,struct.pack('<HH4sII',typ,0,name,pointer|0x80000000,length))

    def item(self,index,item_id,ordinal):
        raw=bytearray(24); struct.pack_into('<H',raw,0,item_id); struct.pack_into('<H',raw,4,ordinal)
        self.raw(self.I+8+24*index,raw)

    def equip(self,selector,a,b):
        mapped={14:1,15:6}.get(selector,selector)
        rva=0x9ABDA0+(mapped-1)*0x114
        self.raw(rva,struct.pack('<HHQ',a,b,0))

    def capture(self):
        return self.env['capture_native_secondary_bindings'](self.base,self.read,self.census,lifecycle_read=self.lifecycle)

    def test_complete_current_selection_first_item_and_full_bytes(self):
        r=self.capture()
        self.assertTrue(r['inventoryComplete']); self.assertTrue(r['sampledBindingsStable'],r['reasons'])
        self.assertTrue(r['declaredExtentBindingComplete']); self.assertTrue(r['items']['complete'])
        self.assertEqual([b['rawOutput'] for b in r['bindings']],[302,302,0,0])
        self.assertEqual(r['bindings'][0]['itemMatches'],[0,1]); self.assertEqual(r['bindings'][0]['ordinal'],3)
        self.assertEqual(len(bytes.fromhex(r['selectedEntries'][0]['hex'])),96)
        self.assertEqual(len(bytes.fromhex(r['bindings'][0]['itemHex'])),24)
        self.assertFalse(r['effectiveObjectLookupQualified'])
        self.assertTrue(all(not r[k] for k in ('mayCreate','atomic','creationAuthority','creatorExclusive','replacementArgumentDomainComplete')))
        self.assertEqual(r['bindings'][0]['offsetBeforeLookup'],16)
        self.assertEqual(r['bindings'][0]['offsetAfterLookup'],16)
        stages=[o['stage'] for o in r['observations']]
        self.assertIn('post-lookup',stages); self.assertIn('readback',stages)

    def test_all_three_tables_aliases_duplicates_and_unsorted_retained(self):
        self.m.put(0x2A25038,'u64',self.base+self.T)
        self.m.put(0x2A25040,'u64',self.base+0x910000)
        self.object_rows([(302,0,0,0),(84,2,1,0),(84,2,1,0)],0x910000)
        r=self.capture(); self.assertTrue(r['inventoryComplete'])
        self.assertEqual(len(r['selectedEntries'][0]['aliases']),2)
        self.assertEqual(r['tables'][2]['duplicateIds'],[84]); self.assertFalse(r['tables'][2]['unsignedSorted'])
        self.assertFalse(r['effectiveObjectLookupQualified'])
        self.assertEqual(r['selectedPhysicalCount'],5)

    def test_census_reference_must_be_exact_rooted_physical_row(self):
        self.object_rows([(100,2,0,0)])
        self.census['nodes']=[{'objectEntry':self.base+self.T+9}]
        self.assertEqual(self.capture()['selectedPhysicalCount'],0)
        self.census['nodes']=[{'objectEntry':self.base+self.T+8}]
        self.assertEqual(self.capture()['selectedPhysicalCount'],1)

    def test_distinct_early_zero_reasons(self):
        self.object_rows([(302,1001,0,11),(84,6000,1,0),(85,1,1,0),(86,2,1,0),(87,3,2,0)])
        self.equip(1,0,0); self.equip(2,0,0); self.equip(3,10,10)
        self.m.put(self.W+8,'u32',0)
        statuses=[b['status'] for b in self.capture()['bindings']]
        for status in ('group-zero','selector-null','form-null','equipment-zero','row-offset-zero'):
            self.assertIn(status,statuses)

    def test_selector_remaps_and_signed_form_boundaries(self):
        for selector,form,status in ((14,1,'sampled-output'),(14,10,'sampled-output'),
                                    (14,0,'form-null'),(1,11,'form-null'),(1,-1,'form-null'),(15,0,'sampled-output')):
            with self.subTest(selector=selector,form=form):
                self.object_rows([(84,selector,1,form)]); self.equip(selector,10,10)
                if 1<=form<=10: self.raw(0x9ABDA0+0xE04+(form-1)*0x38,struct.pack('<Q',10))
                r=self.capture(); self.assertEqual(r['bindings'][1]['status'],status)
                if selector==15: self.assertEqual(r['bindings'][0]['equipmentAddress'],self.base+0x9ABDA0+5*0x114)

    def test_missing_item_is_fault_not_zero_and_later_duplicate_not_chosen(self):
        self.equip(2,99,10)
        r=self.capture(); self.assertEqual(r['bindings'][0]['status'],'would-fault-missing-item')
        self.assertNotIn('rawOutput',r['bindings'][0]); self.assertEqual(r['bindings'][1]['itemIndex'],0)

    def test_raw_outputs_zero_highbit_unmasked(self):
        for v in (0,302,0x8000012E,0xFFFFFFFF):
            self.m.put(self.W+76,'u32',v)
            self.assertEqual(self.capture()['bindings'][0]['rawOutput'],v)

    def test_first_invalid_descriptor_never_falls_back(self):
        self.m.put(self.B+4,'i32',3)
        self.descriptor(0,b'went',self.W+4,512)
        self.descriptor(2,b'went',self.W,512)
        r=self.capture(); self.assertEqual(r['bar']['went']['matches'],[0,2])
        self.assertEqual(r['bar']['went']['descriptorIndex'],0)
        self.assertFalse(r['declaredExtentBindingComplete']); self.assertFalse(r['currentSelectionComplete'])

    def test_handle_highbit_not_required_and_bucket_join_required(self):
        self.m.put(self.B+24,'u32',self.W)
        self.assertTrue(self.capture()['declaredExtentBindingComplete'])
        self.m.put(0x2B0D720,'u64',self.base+1)
        self.assertFalse(self.capture()['declaredExtentBindingComplete'])

    def test_item_extent_containment_and_exact_legacy_malformed_count(self):
        self.assertTrue(self.capture()['items']['complete'])
        self.raw(self.I,bytes.fromhex('010007000000001e'))
        r=self.capture(); self.assertEqual(r['items']['count'],503316480)
        self.assertFalse(r['items']['complete']); self.assertFalse(r['currentSelectionComplete'])
        self.assertIn('item-count-cap-or-extent',[x['code'] for x in r['reasons']])

    def test_went_divisibility_and_output_extent(self):
        self.m.put(self.B+28,'u32',79)
        self.assertFalse(self.capture()['bar']['went']['complete'])
        self.m.put(self.B+28,'u32',80)
        self.assertEqual(self.capture()['bindings'][0]['rawOutput'],302)
        self.m.put(self.B+28,'u32',76)
        self.assertEqual(self.capture()['bindings'][0]['status'],'output-outside-qualified-extent')

    def test_offset_addition_does_not_wrap_u32(self):
        self.m.put(self.W+4,'u32',0xFFFFFFFF)
        r=self.capture(); b=r['bindings'][0]
        self.assertEqual(b['outputIndex'],0x100000002)
        self.assertEqual(b['status'],'output-outside-qualified-extent'); self.assertNotIn('rawOutput',b)

    def test_null_below_module_and_caps_retain_partial(self):
        self.m.put(0x2A25030,'u64',self.base-8)
        r=self.capture(); self.assertFalse(r['inventoryComplete']); self.assertEqual(r['tables'][0]['failure'],'root-unavailable')
        self.m.put(0x2A25030,'u64',self.base+self.T); self.m.put(self.T+4,'i32',8193)
        r=self.capture(); self.assertEqual(r['tables'][0]['count'],8193); self.assertFalse(r['inventoryComplete'])
        self.m.put(self.T+4,'i32',2); self.m.put(self.B+4,'i32',257)
        r=self.capture(); self.assertEqual(r['bar']['count'],257); self.assertEqual(r['bindings'][2]['status'],'group-zero')

    def test_selection_cap_is_not_complete_prefix(self):
        self.object_rows([(300+i,2,1,0) for i in range(129)])
        r=self.capture(); self.assertEqual(r['selectedPhysicalCount'],129)
        self.assertEqual(len(r['bindings']),256); self.assertFalse(r['currentSelectionComplete'])

    def test_w_reload_and_offset_reload_drift_do_not_refresh_baseline(self):
        def change(sample):
            key='0x2AE5A38'
            if key in sample: sample[key]=hex(self.base+self.W+4)
        self.m.faults['secondary-post-lookup']=change
        r=self.capture(); self.assertEqual(r['bindings'][0]['status'],'went-reload-drift')
        self.assertEqual(r['roots']['went'],self.base+self.W)
        self.assertTrue(r['readbackChanges']); self.assertFalse(r['sampledBindingsStable'])
        def offset(sample):
            key=f'0x{self.W+4:X}'
            if key in sample: sample[key]=17
        self.m.faults['secondary-post-lookup']=offset
        r=self.capture(); self.assertEqual(r['bindings'][0]['offsetBeforeLookup'],16)
        self.assertEqual(r['bindings'][0]['offsetAfterLookup'],17)
        self.assertEqual(r['bindings'][0]['status'],'offset-reload-drift')

    def test_dependency_readback_drift_keeps_first_raw_bytes(self):
        def drift(sample):
            key=f'0x{self.T+8:X}'
            if key in sample: sample[key]=hex(85)
        self.m.faults['secondary-readback']=drift
        r=self.capture(); self.assertEqual(r['selectedEntries'][0]['objectId'],84)
        self.assertTrue(r['readbackChanges']); self.assertFalse(r['sampledBindingsStable'])

    def test_unaligned_full_content_and_overlapping_conflicts(self):
        self.m.put(0x2A25030,'u64',self.base+self.T+4)
        self.object_rows([(84,2,1,0)],self.T+4)
        self.assertTrue(self.capture()['sampledBindingsStable'])
        # Two equipment words overlap by six bytes; a contradictory second
        # response must not erase the first byte-backed observation.
        def conflict(sample):
            k=f'0x{0x9ABDA0+0x114+2:X}'
            if k in sample: sample[k]=hex(11)
        self.m.faults['secondary-initial']=conflict
        r=self.capture(); self.assertTrue(any(c['kind']=='overlapping-bytes' for c in r['conflicts']))
        self.assertFalse(r['currentSelectionComplete'])

    def test_type_alias_conflict_cannot_masquerade_stable(self):
        # Mapping group cell aliases the u64 object ID word exactly.
        self.m.put(0x2AE5A38,'u64',self.base+self.T+4)
        self.descriptor(0,b'went',self.T+4,512)
        r=self.capture(); self.assertTrue(any(c['kind']=='typed-address' for c in r['conflicts']))
        self.assertFalse(r['sampledBindingsStable'])

    def test_identity_before_merge_content_and_lifecycle(self):
        self.identity=456
        r=self.capture(); self.assertFalse(r['inventoryComplete']); self.assertFalse(r['lifecycle']['before']['available'])
        self.assertTrue(all(not o['values'] for o in r['observations']))
        self.assertTrue(all(f['phase']=='peek-response-identity' for f in r['readFailures']))

    def test_identity_metadata_bool_or_wrong_base_rejected(self):
        self.identity=True
        self.assertFalse(self.capture()['sampledBindingsStable'])
        self.identity=123
        old=self.env['kh2ctl']
        def wrong(*a,**kw):
            r=old(*a,**kw); r['moduleBase']=hex(self.base+0x1000); return r
        self.env['kh2ctl']=wrong
        r=self.capture(); self.assertFalse(r['lifecycle']['before']['available']); self.assertFalse(r['inventoryComplete'])

    def test_final_failure_and_deadline_no_retry_keep_early_zero(self):
        self.m.faults['secondary-readback']='failure'
        r=self.capture(); self.assertEqual(r['bindings'][2]['status'],'group-zero')
        self.assertFalse(r['readbackStable']); self.assertTrue(r['readFailures'])
        self.m.now=31
        before=len(self.m.calls); r=self.capture()
        self.assertEqual(len(self.m.calls),before); self.assertFalse(r['sampledBindingsStable'])
        self.assertTrue(all(w['peekAttempted'] is False for w in r['readWork']))

    def test_partial_batch_retains_completed_rows_and_unknowns(self):
        self.object_rows([(300+i,2,1,0) for i in range(100)])
        old=self.env['kh2ctl']; count=0
        def fail(*a,**kw):
            nonlocal count
            if self.m.stage=='secondary-initial' and len(a[2].split(','))==128:
                count+=1
                if count==2: raise OSError('later batch failed')
            return old(*a,**kw)
        self.env['kh2ctl']=fail
        r=self.capture(); self.assertTrue(r['readFailures'])
        self.assertFalse(r['readbackStable'])
        self.assertTrue(any(o['values'] for o in r['observations']))

    def test_failed_initial_field_is_not_retried_by_full_record_extension(self):
        old=self.env['kh2ctl']; root_word=f'0x{self.T+8:X}'
        reads=0
        def fail(*a,**kw):
            nonlocal reads
            if self.m.stage=='secondary-initial' and root_word+':u64' in a[2].split(','):
                reads+=1
                raise OSError('no retry of initial discovery')
            return old(*a,**kw)
        self.env['kh2ctl']=fail
        r=self.capture()
        self.assertEqual(reads,1); self.assertFalse(r['inventoryComplete'])
        self.assertFalse(r['sampledBindingsStable'])

    def test_full_record_extension_reuses_first_discovery_word(self):
        r=self.capture(); key=f'{self.base+self.T+8:X}:u64'
        initial=[o for o in r['observations'] if o['stage']=='initial' and key in o['values']]
        self.assertEqual(len(initial),1)
        self.assertEqual(r['selectedEntries'][0]['hex'][:8], '54000000')
        self.assertEqual(r['tables'][0]['rows'][0]['idWord'], int.from_bytes(bytes.fromhex(r['selectedEntries'][0]['hex'][:16]),'little'))

    def test_lifecycle_after_wrong_pid_is_retained_and_not_merged(self):
        old=self.env['kh2ctl']
        def wrong(*a,**kw):
            r=old(*a,**kw)
            if self.m.stage=='secondary-lifecycle-after': r['processId']=456
            return r
        self.env['kh2ctl']=wrong
        r=self.capture()
        self.assertTrue(r['currentSelectionComplete']); self.assertFalse(r['sampledBindingsStable'])
        self.assertEqual(r['lifecycle']['after']['native'],{})
        self.assertEqual(r['readWork'][-1]['responseIdentity']['processId'],456)

    def test_invalid_width_bool_partial_and_parent_identity(self):
        def invalid(sample):
            key=f'0x{self.T+8:X}'
            if key in sample: sample[key]=True
        self.m.faults['secondary-initial']=invalid
        r=self.capture(); self.assertFalse(r['currentSelectionComplete'])
        self.assertIn('invalid-typed-value',[x['code'] for x in r['reasons']])
        self.m.faults.clear(); self.census['moduleBase']='0x1'
        r=self.capture(); self.assertTrue(r['currentSelectionComplete'])
        self.assertFalse(r['parentIdentityAvailable']); self.assertFalse(r['sampledBindingsStable'])

    def test_negative_counts_and_aggregate_cap_are_explicit(self):
        self.m.put(self.I+4,'i32',-1)
        r=self.capture(); self.assertEqual(r['items']['count'],-1)
        self.assertTrue(r['items']['complete']); self.assertEqual(r['bindings'][0]['status'],'would-fault-missing-item')
        self.m.put(0x2A25038,'u64',self.base+0x910000)
        self.raw(0x910000,struct.pack('<Ii',3,8191))
        r=self.capture(); self.assertFalse(r['inventoryComplete'])
        self.assertEqual(r['tables'][1]['failure'],'count-cap-or-span')
        self.assertEqual(r['tables'][0]['rows'][0]['objectId'],84)

    def test_consumed_bucket_disagrees_with_stable_lifecycle_endpoints(self):
        for stage in ('secondary-lifecycle-before','secondary-lifecycle-after'):
            self.m.faults[stage]=lambda samples:samples.__setitem__('0x2B0D720',hex(self.base+0x2000000))
        r=self.capture()
        self.assertTrue(r['lifecycle']['stable']); self.assertTrue(r['readbackStable'])
        self.assertTrue(r['currentSelectionComplete'])
        self.assertFalse(r['localBindingsStable']); self.assertFalse(r['sampledBindingsStable'])
        join=r['consumedBucketJoin']; self.assertFalse(join['complete']); self.assertTrue(join['knownDisagreement'])
        self.assertEqual([e['bindingName'] for e in join['entries']],['went','item'])
        for e in join['entries']:
            self.assertTrue(e['complete']); self.assertFalse(e['matches'])
            self.assertEqual(e['samples']['binding'],self.base)
            self.assertEqual(e['samples']['readback'],self.base)
            self.assertEqual(e['samples']['lifecycleBefore'],self.base+0x2000000)
            self.assertEqual(e['samples']['lifecycleAfter'],self.base+0x2000000)

    def test_present_parent_scope_mismatch_denies_combined_stable(self):
        self.census['rawOccupancy']['lifecycle']={'after':{'scope':{'epoch':99,'location':[1,2,3,4,5,6]}}}
        r=self.capture()
        self.assertTrue(r['parentScopeJoin']['available']); self.assertFalse(r['parentScopeJoin']['matches'])
        self.assertTrue(r['localBindingsStable']); self.assertFalse(r['sampledBindingsStable'])
        self.assertEqual(r['sampledScope'],'parent-conflict')
        self.assertIn('available-parent-scope-disagreement',[x['code'] for x in r['reasons']])

    def test_parent_unavailable_local_only_and_consistent_parent_join(self):
        r=self.capture()
        self.assertTrue(r['localBindingsStable']); self.assertTrue(r['sampledBindingsStable'])
        self.assertEqual(r['sampledScope'],'local-only-parent-unavailable')
        self.assertFalse(r['parentScopeJoin']['available']); self.assertTrue(r['consumedBucketJoin']['complete'])
        self.census['rawOccupancy']['lifecycle']={'after':{'scope':copy.deepcopy(r['lifecycle']['after']['scope'])}}
        r=self.capture()
        self.assertTrue(r['sampledBindingsStable']); self.assertEqual(r['sampledScope'],'parent-joined')
        self.assertTrue(r['parentScopeJoin']['matches'])

    def test_bucket_readback_disagreement_and_missing_endpoint_retained(self):
        self.m.faults['secondary-readback']=lambda samples:samples.__setitem__('0x2B0D720',hex(self.base+0x2000000))
        r=self.capture(); self.assertTrue(r['consumedBucketJoin']['knownDisagreement'])
        self.assertFalse(r['sampledBindingsStable'])
        self.assertEqual(r['consumedBucketJoin']['entries'][0]['samples']['readback'],self.base+0x2000000)
        self.m.faults.clear(); self.m.faults['secondary-lifecycle-after']='failure'
        r=self.capture(); self.assertFalse(r['consumedBucketJoin']['complete'])
        self.assertFalse(r['localBindingsStable']); self.assertFalse(r['sampledBindingsStable'])
        e=r['consumedBucketJoin']['entries'][0]
        self.assertIsNone(e['samples']['lifecycleAfter']); self.assertFalse(e['available']['lifecycleAfter'])
        self.assertFalse(e['knownDisagreement'])

    def test_unconsumed_bucket_is_not_fabricated_binding_dependency(self):
        for stage in ('secondary-lifecycle-before','secondary-lifecycle-after'):
            self.m.faults[stage]=lambda samples:samples.__setitem__('0x2B0D728',hex(self.base+0x2000000))
        r=self.capture(); self.assertTrue(r['sampledBindingsStable'])
        self.assertEqual([e['bucket'] for e in r['consumedBucketJoin']['entries']],[0,0])


class Integration(unittest.TestCase):
    def test_step_boolean_disabled_shape_and_enabled_keyword(self):
        env=resource_tests.Integration.step_environment(self); calls=[]
        def capture(*a,**kw):
            calls.append(kw); return {'complete':True,'causeContext':{'complete':True},'comparison':{'nativeLivingCount':0}}
        env['native_enemy_census_snapshot']=capture
        with tempfile.TemporaryDirectory() as t:
            ctx=SimpleNamespace(instances=[object()],sleep=lambda _:None,check_all=lambda:None,saved={},artifacts=[],run_dir=Path(t))
            for v in (False,True):
                env['step_native_enemy_census'](ctx,{'samples':1,'secondaryBindings':v})
                self.assertEqual(calls[-1],{'resource_bindings':False,**({'secondary_bindings':True} if v else {})})
            for v in (None,0,1,{},[],'true'):
                with self.assertRaises(RuntimeError): env['step_native_enemy_census'](ctx,{'secondaryBindings':v})
                with self.assertRaises(ValueError): env['validate_scenario']({'steps':[{'do':'boot'},{'do':'native_enemy_census','secondaryBindings':v}]})

    def test_snapshot_disabled_output_and_read_schedule_unchanged(self):
        # Actual snapshot with isolated receipt helper injected only for opt-in.
        with tempfile.TemporaryDirectory() as t:
            logs=Path(t); (logs/'kh2coop_inject_123.log').write_text('')
            results=[]; memories=[]
            for kw in ({},{'secondary_bindings':False},{'secondary_bindings':True}):
                env=collector_module(logs); m=GeometryMemory(); env['kh2ctl']=m
                env['time']=SimpleNamespace(monotonic=lambda:0)
                env['capture_native_secondary_bindings']=lambda *a,**k: {'readOnlySentinel':True}
                results.append(env['native_enemy_census_snapshot'](SimpleNamespace(inst=lambda _:SimpleNamespace(pid=123)),0,**kw))
                memories.append(m.calls)
            self.assertEqual(results[0],results[1]); self.assertEqual(memories[0],memories[1])
            self.assertEqual({k:v for k,v in results[2].items() if k!='secondaryBindings'},results[0])
            self.assertEqual(results[2]['secondaryBindings'],{'readOnlySentinel':True})


if __name__=='__main__': unittest.main()
