"""A-02 native brackets, explicit time/pose mutations; no native/process calls."""
import copy,json,unittest
from pathlib import Path
from unittest.mock import patch
from . import start,population,profile,preposition,health
from .test_profile import Adapter

FIXTURE=json.loads((Path(__file__).parent/'fixtures/start-a02.json').read_text())
def value(role,offset=0,drift=0):
    r=copy.deepcopy(FIXTURE[role]);s=r['sample']
    for key in ('readStartedNs','readFinishedNs'):s[key]+=offset
    for side in ('Before','After'):
        s['state'+side]['actors']['player']['position']['x']+=drift
        for a in s['entities'+side]['actors']:
            if int(a['address'],16)==r['canonical']['after']['actor']:a['position']['x']+=drift
    return r

def settle(role):
    r=value(role);session=population.records(r['sample']['runtimeAfter'],'[runtime-identity] ')[-1]['session']
    return start.Settle(role,r['canonical']['session'],session)

class A(Adapter):
    role='friend'
    def __init__(self,*,drift_round=None,final_drift=False):
        super().__init__();self.local=value('friend');self.peer=value('host')
        self.owned=self.local['canonical']['session'];self.peer_owned=self.peer['canonical']['session']
        self.runtime_session=population.records(self.local['sample']['runtimeAfter'],'[runtime-identity] ')[-1]['session']
        self.now=self.local['sample']['readFinishedNs'];self.encounter_started_ns=self.now
        self.deadline_ns=self.now+200_000_000_000;self.drift_round=drift_round;self.final_drift=final_drift
        self.spawns=[];self.round=0;self.final_reads=0
    def acquire(self,label,deadline_ns=None):
        if label.startswith('start-settle-'):
            self.round=int(label.rsplit('-',1)[1]);offset=self.round*600_000_000
            self.now=self.local['sample']['readFinishedNs']+offset
            return value('friend',offset,2 if self.drift_round==self.round else 0)
        self.final_reads+=1;offset=1_800_000_000+self.final_reads*600_000_000
        self.now=self.local['sample']['readFinishedNs']+offset
        return value('friend',offset,2 if self.final_drift and self.final_reads>=2 else 0)
    def exchange(self,stage,row,end):
        if stage.startswith('start-settle-'):return value('host',self.round*600_000_000)
        if stage=='start-first-dispatch':return value('host',2_400_000_000)
        if stage.endswith(':before'):return value('host',1_800_000_000)
        raise StopIteration('first input recorded')
    def dispatch_once(self,action,argv,gate,end):
        gate();self.spawns.append(action);return {'ok':True}

class Controls(unittest.TestCase):
    def test_retained_both_corridors_stable_and_final_positive(self):
        for role in ('host','friend'):
            gate=settle(role)
            self.assertFalse(gate.observe(value(role))['ready'])
            self.assertTrue(gate.observe(value(role,600_000_000))['ready'])
            self.assertTrue(gate.observe(value(role,1_200_000_000),final=True)['ready'])
    def test_actual_a01_retained_friend_drift_holds(self):
        r=json.loads((Path(__file__).parent/'fixtures/start-a01-drift.json').read_text())['value']
        session=population.records(r['sample']['runtimeAfter'],'[runtime-identity] ')[-1]['session']
        with self.assertRaises(preposition.Refused):start.Settle('friend',r['canonical']['session'],session).observe(r)
    def test_observed_drift_never_reanchors(self):
        for role in ('host','friend'):
            gate=settle(role);gate.observe(value(role));anchor=copy.deepcopy(gate.anchor)
            with self.assertRaises(preposition.Refused):gate.observe(value(role,600_000_000,2))
            self.assertEqual(gate.anchor,anchor)
    def test_original_root_world_runtime_and_b_recipe_refuse(self):
        def root(r):r['sample']['entitiesAfter']['actors'][0]['status']='0x12345678'
        def world(r):r['sample']['avatarObserve']['samples'][0]['worldAfter']['generation']+=1
        def runtime(r):r['sample']['runtimeAfter']=r['sample']['runtimeAfter'].replace('quarantine=0','quarantine=1')
        def b(r):
            for side in ('Before','After'):
                for a in r['sample']['entities'+side]['actors']:
                    if a['name']=='P_EX020':a.update(name='P_EX100',objectId=84,objectType=0,team=1)
        for mutate in (root,world,runtime,b):
            gate=settle('friend');gate.observe(value('friend'));row=value('friend',600_000_000);mutate(row)
            with self.assertRaises((preposition.Refused,population.Unqualified,health.HealthRefusal)):gate.observe(row)
    def test_stale_not_settled_and_late_enemy_refuse(self):
        gate=settle('friend');gate.observe(value('friend'))
        with self.assertRaises(preposition.Refused):gate.observe(value('friend'))
        gate=settle('friend')
        with self.assertRaises(preposition.Refused):gate.observe(value('friend'),final=True)
        gate=settle('friend');gate.observe(value('friend'));gate.observe(value('friend',600_000_000))
        row=value('friend',1_200_000_000);row['sample']['injectAfter']+='\n[combat-causal] receipt kind=0 loss=0'
        with self.assertRaises(preposition.Refused):gate.observe(row,final=True)
    def run_profile(self,a):
        v=profile.Profile(a)
        with patch.object(v,'sample',side_effect=a.acquire),patch.object(v,'canonical',return_value=a.local['canonical']):return v.run()
    def test_actual_run_settle_drift_zero_dispatch(self):
        a=A(drift_round=1)
        with self.assertRaises(preposition.Refused):self.run_profile(a)
        self.assertEqual(a.spawns,[])
    def test_actual_run_fresh_final_drift_zero_dispatch(self):
        a=A(final_drift=True)
        with self.assertRaises(preposition.Refused):self.run_profile(a)
        self.assertEqual(a.spawns,[])
    def test_actual_run_empty_stable_first_input_positive(self):
        a=A()
        with self.assertRaises(StopIteration):self.run_profile(a)
        self.assertEqual(a.spawns,['friend_position_0'])
    def test_actual_settle_expiry_and_stop_zero_dispatch(self):
        for stop in (False,True):
            a=A();v=profile.Profile(a)
            if stop:a.check_stop_and_ownership=lambda owned:(_ for _ in ()).throw(RuntimeError('cancel'))
            else:a.now=v.end
            with patch.object(v,'sample',side_effect=a.acquire):
                with self.assertRaises((RuntimeError,profile.Refused)):v.run()
            self.assertEqual(a.spawns,[])
if __name__=='__main__':unittest.main()
