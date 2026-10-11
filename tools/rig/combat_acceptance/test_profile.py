import unittest
from types import SimpleNamespace
from unittest.mock import patch
from . import profile as p, health
class Adapter:
    role='friend';protocol=17;environment=p.ENVIRONMENT.copy()
    owned={'pid':10,'creationTicks':100,'moduleBase':'0x100000','imageSha256':'a'*64}
    peer_owned={'pid':20,'creationTicks':200,'moduleBase':'0x100000','imageSha256':'a'*64}
    runtime_session='abcdef';run_id='offline';encounter_started_ns=1;deadline_ns=200_000_000_000
    def __init__(self):self.now=1;self.calls=[];self.ram=None
    def clock_ns(self):return self.now
    def check_stop_and_ownership(self,owned):assert owned==self.owned
    def retain(self,*args):self.calls.append(('retain',args[0]))
    def exchange(self,stage,value,end):
        self.calls.append(('exchange',stage))
        if stage.endswith(':ack'):
            action=value['action'];role=next(r for a,r,_ in p.ACTIONS if a==action)
            return dict(role='host',action=action,output={'ok':True} if role=='host' else None)
        return dict(role='host',sample={'ownedBefore':self.peer_owned,'ownedAfter':self.peer_owned,'readFinishedNs':10},canonical={'session':self.peer_owned},admission={'role':'host'})
    def begin_paired_evidence(self,end):self.calls.append(('media',end))
    def dispatch_once(self,action,argv,gate,end):
        gate();self.calls.append(('spawn',action));return {'ok':True}
    def passive_observe(self,end):self.calls.append(('passive',end))
    def original_identity(self):return self.owned
class Controls(unittest.TestCase):
    def test_default_off(self):
        a=Adapter();a.environment=dict(p.ENVIRONMENT,KH2COOP_ORDINARY_LIFECYCLE_EXPERIMENTAL='1')
        with self.assertRaises(p.Refused):p.Profile(a)
    def test_no_extra_reorder_or_argv(self):
        history=[]
        for action,role,argv in p.ACTIONS:p.authorize(action,role,argv,history);history.append(action)
        for args in [('host_hit','host',p.CROSS,history),('host_activate_0','host',p.MOVE,[]),('friend_position_0','friend',p.CROSS,[])]:
            with self.assertRaises(p.Refused):p.authorize(*args)
    def test_actual_run_exact_six_final_health(self):
        a=Adapter();v=p.Profile(a);move=SimpleNamespace(ack=lambda a:None,mark_activation_issuing=lambda:None)
        def gate(action):
            v.moves=move;a.calls.append(('gate',action));return dict(sample={},canonical={},role='friend',admission={'role':'friend'})
        start=dict(sample={},canonical={},role='friend')
        with patch.object(v,'sample',return_value=start),patch.object(v,'gate',side_effect=gate),patch.object(v,'canonical',side_effect=lambda:a.calls.append(('canonical',None))),patch.object(p.preposition,'proven_a_start'),patch.object(p.population,'activation_snapshot'),patch.object(p.population,'activation_requires_admission',return_value=True),patch.object(v.peer_population,'check',return_value={'role':'host'}),patch.object(p.population,'paired'),patch.object(health,'crosscheck_sample'):
            result=v.run()
        self.assertEqual(result['actions'],[r[0] for r in p.ACTIONS]);self.assertFalse(result['acceptance'])
        self.assertEqual([x[1] for x in a.calls if x[0]=='spawn'],['friend_position_0','friend_position_1','friend_position_2','friend_claim'])
        for i,row in enumerate(a.calls):
            if row[0]=='spawn':self.assertEqual(a.calls[i-1][0],'canonical')
    def test_expiry_no_spawn(self):
        a=Adapter();v=p.Profile(a);a.now=v.end
        with self.assertRaises(p.Refused):v.canonical()
    def test_straddle_reacquires_once(self):
        a=Adapter();v=p.Profile(a);a.sample=lambda end:{'injectAfter':'[ordinary-lifecycle] experimental=0'};a.inject_log=lambda:'original'
        with patch.object(v,'canonical',return_value={}),patch.object(p.population,'activation_snapshot'),patch.object(health,'crosscheck_sample',side_effect=[health.HealthRefusal('straddle',{}),None]),patch.object(health,'intervening_hit',return_value={'event':3}):v.sample('hit')
        self.assertIn(('retain','health-reacquisition'),a.calls)
        self.assertFalse(any(x[0]=='spawn' for x in a.calls))
    def test_persistent_or_unexplained_refuses(self):
        for witness in (None,{'event':3}):
            a=Adapter();v=p.Profile(a);a.sample=lambda end:{'injectAfter':'[ordinary-lifecycle] experimental=0'};a.inject_log=lambda:'original'
            with patch.object(v,'canonical',return_value={}),patch.object(p.population,'activation_snapshot'),patch.object(health,'crosscheck_sample',side_effect=health.HealthRefusal('skew',{})),patch.object(health,'intervening_hit',return_value=witness):
                with self.assertRaises(health.HealthRefusal):v.sample('skew')
if __name__=='__main__':unittest.main()
