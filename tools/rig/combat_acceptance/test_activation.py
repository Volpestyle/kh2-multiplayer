"""Actual activation gate/run branches over retained native brackets.

Only sample acquisition/canonical reads, peer transport and helper creation are
standins. The admission predicate, snapshot, preposition and five-body oracle
are real. Partial populations are explicit synthetic mutations of sample-002.
"""
import copy
import json
from pathlib import Path
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from . import profile, population
from .test_profile import Adapter

FIXTURE=json.loads((Path(__file__).parent/'fixtures/activation-empty.json').read_text(encoding='utf-8'))
SHADOW=dict(address='0x12345000',status='0x12346000',name='M_EX020',
            objectId=302,objectType=4,team=0,hp=20,maxHp=20)

def partial(sample, endpoints):
    result=copy.deepcopy(sample)
    for endpoint in endpoints:result['entities'+endpoint]['actors'].append(copy.deepcopy(SHADOW))
    return result

class ActivationComplete(Exception):pass

class RetainedAdapter(Adapter):
    role='host'
    def __init__(self):
        super().__init__()
        self.empty=copy.deepcopy(FIXTURE['sample'])
        self.owned=copy.deepcopy(self.empty['ownedBefore'])
        self.peer_owned=dict(self.owned,pid=self.owned['pid']+1)
        self.runtime_session=population.records(self.empty['runtimeAfter'],'[runtime-identity] ')[-1]['session']
        self.now=self.empty['readFinishedNs']+1
        self.encounter_started_ns=self.now
        self.deadline_ns=self.now+200_000_000_000
        self.canonical=copy.deepcopy(FIXTURE['canonical'])
        self.peer_endpoints=()
        self.spawns=[]

    def value(self,sample,peer=False):
        row=copy.deepcopy(sample);c=copy.deepcopy(self.canonical)
        original=self.peer_owned if peer else self.owned
        row['ownedBefore']=row['ownedAfter']=original
        c['session']=original
        for key in ('peekBefore','peekAfter','stateBefore','stateAfter',
                    'entitiesBefore','entitiesAfter','avatarObserve'):
            row[key]['processId']=original['pid']
        # Prove the exact retained/synthetic bracket reaches the disputed branch.
        population.activation_snapshot(row,original,self.now)
        return dict(sample=row,canonical=c,role='friend' if peer else 'host')

    def exchange(self,stage,value,end):
        if stage.endswith(':ack'):
            action=value['action']
            if action=='host_activate_0':raise ActivationComplete()
            return dict(role='friend',action=action,output={'ok':True})
        sample=self.empty
        if stage=='host_activate_0:before':sample=partial(sample,self.peer_endpoints)
        return self.value(sample,peer=True)

    def dispatch_once(self,action,argv,gate,end):
        gate()  # Production dispatch must gate before helper creation.
        self.spawns.append(action)
        return {'ok':True}


class ActivationControls(unittest.TestCase):
    def make(self, endpoints=(), final_only=False):
        a=RetainedAdapter();v=profile.Profile(a);activation_reads=[]
        v.settle_start=lambda:None;v.first_dispatch_pair=lambda row:None;v.start_local=SimpleNamespace(observe=lambda *a,**k:None) # Tested in test_start.
        def acquire(label):
            row=a.empty
            if label=='host_activate_0':
                activation_reads.append(label)
                if not final_only or len(activation_reads)>1:
                    row=partial(row,endpoints)
            return a.value(row)
        return a,v,acquire

    def test_predicate_before_after_and_original_receipt(self):
        empty=FIXTURE['sample']
        self.assertFalse(population.activation_requires_admission(empty))
        for endpoints in (('Before',),('After',),('Before','After')):
            self.assertTrue(population.activation_requires_admission(partial(empty,endpoints)))
        for log in ('injectBefore','injectAfter'):
            row=copy.deepcopy(empty);row[log]+='\n[combat-causal] receipt kind=0 loss=0\n'
            self.assertTrue(population.activation_requires_admission(row))

    def test_actual_local_gate_partial_refuses_without_kind_zero(self):
        for endpoints in (('Before',),('After',),('Before','After')):
            a,v,acquire=self.make(endpoints)
            self.assertFalse(any(population.number(r,'kind')==0
                                 for r in population.receipt_rows(a.empty['injectAfter'])))
            with patch.object(v,'sample',side_effect=acquire):
                with self.assertRaises(population.Unqualified):v.gate('host_activate_0')
            self.assertTrue(v.population.retired)
            self.assertEqual(a.spawns,[])

    def test_actual_run_local_and_final_partial_zero_dispatch(self):
        for final_only in (False,True):
            for endpoints in (('Before',),('After',),('Before','After')):
                a,v,acquire=self.make(endpoints,final_only)
                with patch.object(v,'sample',side_effect=acquire),patch.object(v,'canonical',return_value=a.canonical):
                    with self.assertRaises(population.Unqualified):v.run()
                self.assertEqual(a.spawns,[])
                self.assertEqual(v.history,[r[0] for r in profile.ACTIONS[:3]])

    def test_actual_run_peer_partial_zero_dispatch(self):
        for endpoints in (('Before',),('After',),('Before','After')):
            a,v,acquire=self.make();a.peer_endpoints=endpoints
            with patch.object(v,'sample',side_effect=acquire),patch.object(v,'canonical',return_value=a.canonical):
                with self.assertRaises(population.Unqualified):v.run()
            self.assertTrue(v.peer_population.retired)
            self.assertEqual(a.spawns,[])

    def test_both_partial_peers_without_receipts_zero_dispatch(self):
        a,v,acquire=self.make(('Before',));a.peer_endpoints=('After',)
        with patch.object(v,'sample',side_effect=acquire),patch.object(v,'canonical',return_value=a.canonical):
            with self.assertRaises(population.Unqualified):v.run()
        self.assertEqual(a.spawns,[])

    def test_original_anchor_never_reopens_empty_activation(self):
        a,v,acquire=self.make()
        for oracle in (v.population,v.peer_population):
            oracle.anchor=('original',)
            with self.assertRaises(population.Unqualified):
                v.admission_for('host_activate_0',a.empty,oracle,a.now)

    def test_genuinely_empty_gate_and_run_activation_positive(self):
        a,v,acquire=self.make()
        with patch.object(v,'sample',side_effect=acquire),patch.object(v,'canonical',return_value=a.canonical):
            self.assertIsNone(v.gate('host_activate_0')['admission'])
            with self.assertRaises(ActivationComplete):v.run()
        self.assertEqual(a.spawns,['host_activate_0'])

if __name__=='__main__':unittest.main()
