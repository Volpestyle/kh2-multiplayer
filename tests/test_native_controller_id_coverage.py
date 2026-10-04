"""Actual production helper/read adapters with byte-backed address-only CLI replies."""
import ast
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest

from test_geometry_position_receipts import collector_module, Memory as GeometryMemory
from test_native_resource_bindings import Memory
import test_native_resource_bindings as binding_tests

ROOT = Path(__file__).resolve().parents[1]
UNSET = object()


def load_helper(env):
    tree = ast.parse((ROOT/'tools/scenario/run.py').read_text(encoding='utf-8'))
    node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'capture_native_controller_id_coverage')
    env['hashlib'] = hashlib
    exec(compile(ast.Module(body=[node],type_ignores=[]), '<production controller ID helper>', 'exec'),env)
    return tree


class ControllerRecords(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        logs = Path(self.temp.name)
        self.log = logs/'kh2coop_inject_123.log'
        self.log.write_text('[warp] load complete serial=4 transition=3 room=05/06 door=0 map=1 btl=1 evt=0\n'
                            '[enemysync] client arrived epoch=1 room=05/06 door=0 map=1 btl=1 evt=0\n')
        self.m = m = Memory(); b = m.base
        self.env = e = collector_module(logs); tree = load_helper(e)
        census = next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='native_enemy_census_snapshot')
        nested = [n for n in census.body if isinstance(n,ast.FunctionDef) and n.name in
                  ('remaining','valid_pointer','read','header','logs','raw_lifecycle_read')]
        e.update(base=b,pid=123,deadline=30.0,out={},kh2ctl=m,time=SimpleNamespace(monotonic=lambda:m.now))
        exec(compile(ast.Module(body=nested,type_ignores=[]),'<production checked readers>','exec'),e)
        actual=e['read']
        def read(fields,result=None,**kw):
            m.stage=kw.get('stage','native-field-batch')
            return actual(fields,result,**kw)
        e['read']=self.read=read; self.lifecycle=e['raw_lifecycle_read']
        for rva,kind,value in ((0x2A171C8,'u64',b+0x100000),(0x2A171D0,'u64',b+0x100000),
            (0x2A171D8,'u64',b+0x110000),(0x2A171E0,'u64',b+0x110000),
            (0x2AE6680,'u64',b+0x2AE5E60),(0x2AE6688,'u32',1),(0x9BA8D0,'u8',1),(0x2A171E8,'u32',0),
            (0xB65210,'i32',0),(0x2A11478,'u64',0),(0x7435D0,'u8',255),
            (0x717008,'u8',5),(0x717009,'u8',6),(0x71700A,'u8',0),(0x71700C,'u16',1),
            (0x71700E,'u16',1),(0x717010,'u16',0),(0x2A10418,'i32',10)):
            m.put(rva,kind,value)
        for i in range(64): m.put(0x2B0D720+8*i,'u64',b if i==0 else 0)
        self.defs=[]
        counts=[3,3,0,0,0,3,5,5,3,4]; groups=[808476525]*2+[808542061]*2+[825319277,808935277]+[808476514]*4
        for i,n in enumerate(counts):
            c,h=0x300000+i*0x100,0x400000+i*0x1000+(4 if i%2 else 0)
            start=h+44; ht=1 if i<6 else 2; hid=[1,4,13,14,15,17,30,31,32,33][i]
            d=dict(tableIndex=i,key=groups[i],flags=0,pointer=b+c,groupKey=groups[i],header=b+h,
                   spawnArray=b+start,regionArray=b+start+n*64,headerFields=dict(type=ht,headerId=hid,spawnCount=n,regionCount=0))
            self.defs.append(d)
            for rva,kind,value in ((0x2A10010+16*i,'u32',groups[i]),(0x2A10014+16*i,'u32',0),
                (0x2A10018+16*i,'u64',b+c),(c,'u32',groups[i]),(c+4,'u32',2),(c+8,'u64',b+h),
                (c+0x30,'u64',b+start),(c+0x38,'u64',b+start+n*64)):
                m.put(rva,kind,value)
            header=bytearray(44); header[0]=ht;struct.pack_into('<HH',header,2,hid,n)
            m.bytes.update({h+j:v for j,v in enumerate(header)})
            for j in range(n):
                record=bytearray(64); struct.pack_into('<I',record,0,302 if i>=6 else 321)
                struct.pack_into('<H',record,30,i*10+j+1);record[28]=2
                m.bytes.update({start+64*j+k:v for k,v in enumerate(record)})
        for address,di in ((0x100000,0),(0x110000,6)):
            o=address+0x10000 if address==0x110000 else 0x200000
            for rva,kind,value in ((address+0xA90,'u32',0),(address+0x120,'u32',0x10080000 if di==6 else 0),
                (address+0x918,'u64',b+o),(address+0x5C0,'u64',0),(address+0x9E8,'u64',self.defs[di]['pointer']),
                (address+0x9F0,'u64',self.defs[di]['spawnArray']),(o,'u32',321 if di==0 else 302),
                (o+4,'u8',10 if di==0 else 4),(o+8,'u16',0)):
                m.put(rva,kind,value)
        self.refresh()

    def refresh(self):
        raw=self.env['capture_raw_native_occupancy'](self.m.base,self.read,lifecycle_read=self.lifecycle)
        self.assertTrue(raw['listedOccupancyComplete'])
        self.census=dict(pid=123,moduleBase=hex(self.m.base),rawOccupancy=raw,
            causeContext=dict(complete=True,tableStable=True,lifecycleStable=True,
                              before={'controllerCount':len(self.defs)},after={'controllerCount':len(self.defs)},controllers=copy.deepcopy(self.defs)))
        self.census.update({k:True for k in ('complete','listComplete','classificationComplete','actorIdentityStable','lifecycleStable','epochStable','safeGameplay')})
        for phase in ('before','after'):
            native=copy.deepcopy(raw['lifecycle'][phase]['native']);logs=copy.deepcopy(raw['lifecycle'][phase]['logs'])
            self.census[phase]=native;self.census['logs'+phase.title()]=logs
            self.census['causeContext']['native'+phase.title()]=copy.deepcopy(native)
            self.census['causeContext']['logs'+phase.title()]=copy.deepcopy(logs)
        self.m.calls.clear()

    def collect(self,lifecycle=None):
        old=copy.deepcopy(self.census)
        r=self.env['capture_native_controller_id_coverage'](self.m.base,self.read,self.census,lifecycle_read=lifecycle or self.lifecycle)
        self.assertEqual(old,self.census)
        for k in ('globalControllerIdCoverageComplete','controllerIncarnationQualified','pendingExclusionComplete','atomic','creationAuthority','executedCallObserved'):
            self.assertIs(r[k],False)
        for _,args,_ in self.m.calls:
            types={}
            for token in args[2].split(','):
                addr,kind=token.split(':');types.setdefault(addr,set()).add(kind)
            self.assertTrue(all(len(v)==1 for v in types.values()))
        return r

    def test_full_ten_definitions_and_status_null_deferred_joins(self):
        r=self.collect()
        self.assertTrue(r['sampledSupportedCoverageComplete']); self.assertIs(r['noIdConflictInSampledScope'],True)
        self.assertEqual(r['declaredLogicalRecordCount'],26)
        self.assertEqual([len(d['records']) for d in r['definitions']],[3,3,0,0,0,3,5,5,3,4])
        self.assertEqual([d['headerFields']['headerId'] for d in r['definitions'] if d['key']==808476514],[30,31,32,33])
        self.assertEqual([x['list'] for x in r['references']],['active','deferred'])
        self.assertTrue(all(x['fullBytesComplete'] for d in r['definitions'] for x in d['records']))
        self.assertTrue(any(x['pointer']%8==4 for d in r['definitions'] for x in d['records']))

    def test_unreferenced_duplicate_is_retained_with_complete_enumeration(self):
        self.m.put(self.defs[7]['spawnArray']-self.m.base+30,'u16',61)
        r=self.collect();self.assertTrue(r['sampledSupportedCoverageComplete'])
        self.assertIs(r['noIdConflictInSampledScope'],False)
        self.assertEqual(r['idConflicts'][0]['recordId'],61)
        self.assertEqual([x['tableIndex'] for x in r['idConflicts'][0]['occurrences']],[6,7])

    def test_zero_actors_and_partial_parent_keep_independent_table_bytes(self):
        for rva in (0x2A171C8,0x2A171D0,0x2A171D8,0x2A171E0): self.m.put(rva,'u64',0)
        self.refresh();r=self.collect();self.assertTrue(r['sampledSupportedCoverageComplete']);self.assertEqual(r['references'],[])
        self.census['rawOccupancy']['listedOccupancyComplete']=False
        r=self.collect();self.assertTrue(r['ordinaryRecordBytesComplete']);self.assertFalse(r['listedReferenceJoinComplete'])
        self.assertIsNone(r['noIdConflictInSampledScope']);self.assertTrue(r['parentReasons'])
        self.census['rawOccupancy']=None;r=self.collect();self.assertTrue(r['ordinaryRecordBytesComplete'])
        self.census.pop('rawOccupancy');r=self.collect();self.assertTrue(r['ordinaryRecordBytesComplete'])
        self.assertFalse(r['sampledSupportedCoverageComplete'])

    def test_null_record_zero_and_high_bit_ids_are_distinct(self):
        self.m.put(0x1009E8,'u64',0);self.m.put(0x1009F0,'u64',0);self.refresh()
        self.m.put(self.defs[0]['spawnArray']-self.m.base+30,'u16',0)
        self.m.put(self.defs[1]['spawnArray']-self.m.base+30,'u16',0x8001)
        r=self.collect();self.assertTrue(r['sampledSupportedCoverageComplete'])
        self.assertEqual(r['references'][0]['status'],'null-record');self.assertIsNone(r['references'][0]['recordId'])
        self.assertEqual(r['definitions'][0]['records'][0]['recordId'],0)
        self.assertEqual(r['definitions'][1]['records'][0]['recordId'],0x8001)
        self.assertEqual(r['definitions'][0]['records'][0]['lookupQueryId'],0x70)
        self.assertEqual(r['definitions'][1]['records'][0]['recordIdLimitations'],['high-bit-cache-signedness-unqualified'])

    def test_alternate_unknown_type_and_bad_layout_are_not_empty_coverage(self):
        for mode in ('alternate','type','layout'):
            with self.subTest(mode=mode):
                old=copy.deepcopy(self.m.bytes)
                if mode=='alternate': self.m.put(0x2A10014+16*7,'u32',1)
                if mode=='type': self.m.put(self.defs[7]['header']-self.m.base,'u8',5)
                if mode=='layout': self.m.put(self.defs[7]['pointer']-self.m.base+0x38,'u64',self.defs[7]['regionArray']+1)
                r=self.collect();self.assertTrue(r['tableInventoryComplete']);self.assertFalse(r['ordinaryRecordBytesComplete'])
                self.assertIsNone(r['noIdConflictInSampledScope']);self.m.bytes=old

    def test_table_alias_preserves_logical_memberships_and_same_type_reads(self):
        self.m.put(0x2A10018+16*7,'u64',self.defs[6]['pointer'])
        self.defs[7]=dict(self.defs[6],tableIndex=7);self.refresh()
        r=self.collect();self.assertTrue(r['ordinaryRecordBytesComplete']);self.assertFalse(r['listedReferenceJoinComplete'])
        self.assertEqual(r['definitionAliases'][0]['tableIndices'],[6,7])
        self.assertEqual(len(r['references'][1]['matches']),2);self.assertIs(r['noIdConflictInSampledScope'],False)

    def test_mixed_width_alias_across_stages_is_blocked(self):
        # Header's first u32 would collide with the earlier raw u64 active root.
        d=self.defs[7];self.m.put(d['pointer']-self.m.base+8,'u64',self.m.base+0x2A171C8)
        r=self.collect();self.assertFalse(r['ordinaryRecordBytesComplete'])
        conflicts=[x for x in r['reasons'] if x['code']=='typed-address-conflict']
        self.assertTrue(conflicts);self.assertEqual(conflicts[0]['address'],self.m.base+0x2A171C8)

    def test_record_byte_drift_and_overlap_inconsistency_remain_partial(self):
        rva=self.defs[7]['spawnArray']-self.m.base
        self.m.faults['controller-id-all-after']=lambda s:s.update({hex(rva).upper().replace('0X','0x'):hex(999)})
        r=self.collect();self.assertFalse(r['ordinaryRecordBytesComplete'])
        row=r['definitions'][7]['records'][0]
        self.assertIn('beforeHex',row);self.assertIn('afterHex',row);self.assertNotIn('sha256',row)
        self.m.faults.clear()
        # Change only full bytes for the independently sampled raw ID overlap.
        address=self.defs[0]['spawnArray']-self.m.base+24
        self.m.faults['controller-id-records-before']=lambda s:s.update({f'0x{address:X}':hex(0x2222<<48)})
        r=self.collect();self.assertFalse(r['sampledSupportedCoverageComplete'])
        self.assertIn('overlapping-byte-mismatch',[x['code'] for x in r['reasons']])

    def test_partial_final_batch_retains_complete_initial_record_bytes(self):
        n=[0]
        def fault(sample):
            n[0]+=1
            if n[0]==2: sample.pop(next(iter(sample)))
        self.m.faults['controller-id-all-after']=fault
        r=self.collect();self.assertFalse(r['ordinaryRecordBytesComplete'])
        self.assertIn('beforeHex',r['definitions'][0]['records'][0])
        self.assertTrue(r['readback']['after']['values']);self.assertTrue(r['readFailures'])

    def test_lifecycle_missing_failure_serial_tuple_and_unused_bucket_mismatches(self):
        for phase in ('before','after'):
            for mode in ('failure','missing','serial','tuple','bucket','root'):
                with self.subTest(phase=phase,mode=mode):
                    def reader(sample,stage,evidence):
                        self.lifecycle(sample,stage,evidence)
                        if not stage.endswith(phase): return
                        if mode=='failure': raise PermissionError('synthetic lifecycle failure')
                        if mode=='missing': sample['scope']=None
                        if mode=='serial': sample['scope']['loadSerial']+=1
                        if mode=='tuple': sample['native']['location'][1]+=1
                        if mode=='bucket': sample['native']['regions'][63]=self.m.base
                        if mode=='root': sample['native']['head']=0
                    r=self.collect(reader);self.assertFalse(r['sampledSupportedCoverageComplete'])
                    self.assertIn('beforeHex',r['definitions'][0]['records'][0])
                    self.assertIsNone(r['noIdConflictInSampledScope'])

    def test_parent_table_and_raw_value_contradictions_do_not_qualify(self):
        for mode in ('table','raw','bucket','cause','legacy','missing'):
            with self.subTest(mode=mode):
                old=copy.deepcopy(self.census)
                if mode=='cause': self.census['causeContext']['nativeBefore']['regions'][63]=self.m.base
                if mode=='legacy': self.census['after']['location'][1]+=1
                if mode=='missing': self.census.pop('logsAfter')
                if mode=='table': self.census['causeContext']['controllers'][7]['headerFields']['headerId']=999
                if mode=='raw': self.census['rawOccupancy']['lists']['active']['nodes'][0]['controller']=0
                if mode=='bucket': self.census['rawOccupancy']['lifecycle']['before']['native']['regions'][63]=self.m.base
                r=self.collect();self.assertTrue(r['ordinaryRecordBytesComplete']);self.assertFalse(r['sampledSupportedCoverageComplete'])
                self.assertTrue(r['parentReasons']);self.census=old

    def test_below_module_unaligned_or_orphan_record_stays_unknown(self):
        for pointer in (self.m.base-64,self.defs[0]['spawnArray']+1,self.m.base+0x800000):
            with self.subTest(pointer=pointer):
                self.m.put(0x1009F0,'u64',pointer)
                # Preserve existing parent: changed reference cannot be chased as current.
                r=self.collect();self.assertFalse(r['sampledSupportedCoverageComplete'])
                self.assertTrue(r['references'][0]['reasons']);self.assertIsNone(r['noIdConflictInSampledScope'])
        self.m.put(self.defs[7]['pointer']-self.m.base+8,'u64',self.m.base-44)
        r=self.collect();self.assertFalse(r['ordinaryRecordBytesComplete'])
        self.assertTrue(all(int(t.split(':')[0],0)>=0 for _,args,_ in self.m.calls for t in args[2].split(',')))

    def test_count_and_record_caps_never_truncate_to_complete(self):
        for count in (-1,65):
            self.m.put(0x2A10418,'i32',count);r=self.collect()
            self.assertFalse(r['tableInventoryComplete']);self.assertEqual(r['table'],[])
        self.m.put(0x2A10418,'i32',10)
        self.m.put(self.defs[7]['header']-self.m.base+4,'u16',257)
        r=self.collect();d=r['definitions'][7]
        self.assertEqual(d['omittedRecordIndices'],[0,257]);self.assertFalse(d['fullBytesComplete'])
        self.assertFalse(r['ordinaryRecordBytesComplete'])

    def test_total_logical_cap_retains_first_1024_records_and_marks_omission(self):
        self.m.put(0x2A10418,'i32',5)
        for i in range(5):
            d=self.defs[i];h=0xA00000+i*0x10000;start=h+44
            for rva,val in ((d['pointer']-self.m.base+8,self.m.base+h),
                            (d['pointer']-self.m.base+0x30,self.m.base+start),
                            (d['pointer']-self.m.base+0x38,self.m.base+start+256*64)):
                self.m.put(rva,'u64',val)
            header=bytearray(44);header[0]=1;struct.pack_into('<HH',header,2,i+1,256)
            self.m.bytes.update({h+k:v for k,v in enumerate(header)})
            for j in range(256):
                record=bytearray(64);struct.pack_into('<H',record,30,i*256+j+1)
                self.m.bytes.update({start+j*64+k:v for k,v in enumerate(record)})
        r=self.collect();self.assertEqual(r['declaredLogicalRecordCount'],1280)
        self.assertEqual([len(d['records']) for d in r['definitions']],[256,256,256,256,0])
        self.assertEqual(r['definitions'][4]['omittedRecordIndices'],[0,256])
        self.assertTrue(all(d['fullBytesComplete'] for d in r['definitions'][:4]))
        self.assertFalse(r['ordinaryRecordBytesComplete']);self.assertIsNone(r['noIdConflictInSampledScope'])

    def test_aggregate_raw_cap_does_not_prevent_independent_table_receipt(self):
        self.census['rawOccupancy']['lists']['active']['nodes']=[dict(address=self.m.base+0x800000+i*0x1000,
            objectEntry=0,spawnRecord=0) for i in range(257)]
        r=self.collect();self.assertEqual(len(r['references']),256)
        self.assertTrue(r['ordinaryRecordBytesComplete']);self.assertFalse(r['listedReferenceJoinComplete'])
        self.assertTrue(any('cap' in x.get('error','') for x in r['parentReasons']))

    def test_width_failure_and_unread_nonnull_id_remain_unknown(self):
        key=f"0x{self.defs[7]['spawnArray']-self.m.base:X}"
        self.m.faults['controller-id-records-before']=lambda sample:sample.update({key:hex(1<<64)})
        r=self.collect();self.assertFalse(r['ordinaryRecordBytesComplete'])
        self.assertIn('invalid-typed-value',[x['code'] for x in r['reasons']])
        self.assertNotIn('beforeHex',r['definitions'][7]['records'][0]);self.assertIsNone(r['noIdConflictInSampledScope'])
        self.m.faults.clear()
        key=f"0x{self.defs[0]['spawnArray']-self.m.base+30:X}"
        self.m.faults['controller-id-roots-raw-before']=lambda sample:sample.pop(key)
        r=self.collect();self.assertTrue(r['ordinaryRecordBytesComplete']);self.assertFalse(r['listedReferenceJoinComplete'])
        self.assertIsNone(r['references'][0]['recordId']);self.assertFalse(r['references'][0]['recordIdReadSuccess'])

    def test_deadline_exhaustion_has_no_new_budget_or_retry(self):
        def exhaust(sample): self.m.now=30.0
        self.m.faults['controller-id-headers-before']=exhaust
        r=self.collect();self.assertFalse(r['ordinaryRecordBytesComplete'])
        self.assertTrue(r['readFailures']);self.assertEqual(r['lifecycle']['status'],'partial')
        self.assertFalse(any(stage=='controller-id-records-before' for stage,_,_ in self.m.calls))
        self.assertTrue(all(0<kw['timeout']<=30 for _,_,kw in self.m.calls))


class Integration(unittest.TestCase):
    def run_collector(self,option=UNSET,*,resource=False,late_failure=False):
        class Fake(GeometryMemory):
            def __init__(self): super().__init__();self.headers=0;self.now=0.0
            def __call__(self,*args,**kw):
                if args[0]=='peek' and args[2].startswith('0x2A171C8:u64,0x2A171D0:u64,0x2AE6680:u64,'):
                    self.headers+=1
                    if self.headers==6:self.now=25.0
                result=super().__call__(*args,**kw)
                if late_failure and self.headers>=7: raise PermissionError("synthetic new-diagnostic failure")
                return result
        m=Fake()
        with tempfile.TemporaryDirectory() as temp:
            logs=Path(temp)
            (logs/'kh2coop_inject_123.log').write_text('[warp] load complete serial=4 transition=3 room=05/06 door=0 map=1 btl=1 evt=0\n'
                '[enemysync] client arrived epoch=1 room=05/06 door=0 map=1 btl=1 evt=0\n')
            e=collector_module(logs);tree=load_helper(e)
            resource_helper=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='capture_native_resource_bindings')
            exec(compile(ast.Module(body=[resource_helper],type_ignores=[]),'<production resource helper>','exec'),e)
            e.update(kh2ctl=m,time=SimpleNamespace(monotonic=lambda:m.now))
            kw={} if option is UNSET else {'controller_id_coverage':option}
            r=e['native_enemy_census_snapshot'](SimpleNamespace(inst=lambda i:SimpleNamespace(pid=123)),0,resource_bindings=resource,**kw)
        return r,m

    def test_default_disabled_projection_schedule_and_shared_remaining_deadline(self):
        old,m0=self.run_collector();disabled,m1=self.run_collector(False);enabled,m2=self.run_collector(True)
        self.assertEqual(old,disabled);self.assertEqual(m0.calls,m1.calls)
        self.assertNotIn('controllerIdCoverage',old)
        self.assertEqual({k:v for k,v in enabled.items() if k!='controllerIdCoverage'},old)
        self.assertEqual(m2.calls[:len(m0.calls)],m0.calls)
        self.assertTrue(all(0<kw['timeout']<=5 for _,kw in m2.calls[len(m0.calls):]))
        self.assertIn('controllerIdCoverage',enabled)

    def test_existing_resource_option_order_and_new_failure_preserve_projection(self):
        old,m0=self.run_collector(resource=True);new,m1=self.run_collector(True,resource=True)
        self.assertEqual({k:v for k,v in new.items() if k!='controllerIdCoverage'},old)
        self.assertEqual(m1.calls[:len(m0.calls)],m0.calls)
        old,m0=self.run_collector(late_failure=True);new,m1=self.run_collector(True,late_failure=True)
        self.assertEqual({k:v for k,v in new.items() if k!='controllerIdCoverage'},old)
        self.assertFalse(new['controllerIdCoverage']['sampledSupportedCoverageComplete'])
        self.assertTrue(new['controllerIdCoverage']['reasons'])

    def test_strict_boolean_step_direct_collector_and_validation(self):
        e=binding_tests.Integration.step_environment(self);calls=[]
        e['native_enemy_census_snapshot']=lambda ctx,index,timeout,**kw:(calls.append(kw) or {'complete':True,'causeContext':{'complete':True},'comparison':{'nativeLivingCount':0}})
        with tempfile.TemporaryDirectory() as temp:
            ctx=SimpleNamespace(instances=[object()],sleep=lambda _:None,check_all=lambda:None,saved={},artifacts=[],run_dir=Path(temp))
            e['step_native_enemy_census'](ctx,{'controllerIdCoverage':True,'resourceBindings':True,'samples':1})
            self.assertEqual(calls,[{'resource_bindings':True,'controller_id_coverage':True}])
            for value in (None,0,1,'true',[],{}):
                step={'do':'native_enemy_census','controllerIdCoverage':value}
                with self.assertRaises(ValueError):e['validate_scenario']({'steps':[{'do':'boot'},step]})
                with self.assertRaises(RuntimeError):e['step_native_enemy_census'](ctx,step)
                with self.assertRaises(RuntimeError):self.run_collector(value)
            self.assertEqual(len(calls),1)

    def test_fixture_is_exact_resource_parent_with_six_added_options(self):
        directory=ROOT/'tools/scenario/scenarios';old=directory/'net_reconnect_shadows_resource_bindings_trace.json'
        self.assertEqual(hashlib.sha256(old.read_bytes()).hexdigest().upper(),'4F5D7D963D73064CA581A7B509EAAABFCF9747901966FA270A2EBCE50867714A')
        previous=json.loads(old.read_text());new=json.loads((directory/'net_reconnect_shadows_controller_records_phase_trace.json').read_text())
        self.assertEqual(new.pop('name'),'net_reconnect_shadows_controller_records_phase_trace');previous.pop('name')
        chosen=[i for i,s in enumerate(new['steps']) if 'controllerIdCoverage' in s]
        self.assertEqual(chosen,[42,64,76,93,104,108]);self.assertEqual(len(new['steps']),282)
        for i in chosen:self.assertIs(new['steps'][i].pop('controllerIdCoverage'),True)
        self.assertEqual(new,previous)
        binding_tests.Integration.step_environment(self)['validate_scenario'](json.loads((directory/'net_reconnect_shadows_controller_records_phase_trace.json').read_text()))


if __name__=='__main__': unittest.main()
