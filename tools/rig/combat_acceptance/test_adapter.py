"""Offline production projection controls; no processes, native reads or network."""
import copy
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from . import profile, population
from .adapter import PacketAdapter, DriverCalls


class Services:
    role = 'friend'
    owned = {'pid':7,'creationTicks':8}
    deadline_ns = 100_000_000_000

    def __init__(self):
        self.now = 1
        self.step = 10_000_000
        self.calls = []
        self.driver = SimpleNamespace()
        self.logs = 0
        self.summary = True
        self.cancelled = False
        self.stamp = True

    def clock_ns(self): return self.now
    def check_stop_and_ownership(self, original):
        if self.cancelled: raise RuntimeError('cancelled')
        assert original == self.owned
    def bounded(self, name, end):
        self.calls.append((name,end)); self.now += self.step
    def read_logs(self, *, deadline_ns):
        self.bounded('logs',deadline_ns); self.logs += 1
        return ('before'+('[combat-causal] summary fresh' if self.logs>1 and self.summary else ''),'runtime')
    def read_owned(self, *, deadline_ns):
        self.bounded('owned',deadline_ns); return self.owned.copy()
    def native(self, name, *, deadline_ns):
        self.bounded(name,deadline_ns); return {'native':name}
    def wait_tick(self, *, deadline_ns): self.bounded('wait',deadline_ns)
    def retain(self, name, value): self.calls.append(('retain',name))
    def prepare_media(self, action, *, deadline_ns): self.calls.append(('media',action))
    def media_pre_spawn(self, action): self.calls.append(('media-sample',action))
    def check_cancel_memory_only(self):
        if self.cancelled: raise RuntimeError('cancelled at spawn')
    def call_input(self, argv, *, deadline_ns):
        self.calls.append(('call',argv))
        self.driver.diagnostic_before_child()
        if self.stamp: self.driver.diagnostic_stamp()
        self.calls.append(('spawn',argv))
        return {'ok':True}


class Controls(unittest.TestCase):
    def make(self):
        s=Services(); return s,PacketAdapter(s,profile.adapter_contract())

    def test_stale_five_seconds_or_eight_actions_refuse_before_io(self):
        for kind in ('budget','actions','extra'):
            s=Services(); contract=profile.adapter_contract()
            if kind=='budget':contract['sampleBudgetNs']=5_000_000_000
            elif kind=='actions':contract['actions']=[[a,r,list(v)] for a,r,v in population.ACTIONS]
            else:contract['unexpected']=True
            with self.assertRaises(profile.Refused):PacketAdapter(s,contract)
            self.assertEqual(s.calls,[])

    def test_source_values_are_consumed_not_copied(self):
        with patch.object(profile,'SAMPLE_BUDGET_NS',123_000_000):
            s,a=self.make(); s.step=1
            a.sample(s.deadline_ns)
            deadlines=[end for name,end in s.calls if name!='retain']
            self.assertEqual(set(deadlines),{1+profile.SAMPLE_BUDGET_NS})
        with patch.object(profile,'ACTIONS',(('new-source-action','friend',('input',)),)):
            s,a=self.make(); a.dispatch_once('new-source-action',('input',),lambda:None,s.deadline_ns)
            a.action_acked('new-source-action',{'ok':True})
            self.assertEqual(a.history,['new-source-action'])

    def test_all_native_children_and_wait_share_original_cap(self):
        s,a=self.make(); end=100_000_000
        s.step=1
        row=a.sample(end)
        self.assertEqual({v for k,v in s.calls if k!='retain'},{end})
        self.assertEqual([k for k,v in s.calls if k not in ('logs','owned')],
            ['peekBefore','stateBefore','entitiesBefore','avatarObserve','stateAfter','entitiesAfter','peekAfter'])
        self.assertLess(row['operationFinishedNs'],end)

    def test_late_child_is_refused_without_another_child(self):
        s,a=self.make(); s.step=profile.SAMPLE_BUDGET_NS+1
        with self.assertRaises(profile.Refused):a.sample(s.deadline_ns)
        self.assertEqual([c[0] for c in s.calls],['logs','retain'])

    def test_empty_sample_does_not_wait_for_summary(self):
        s,a=self.make(); s.summary=False; s.step=100_000_000
        a.sample(s.deadline_ns)
        self.assertFalse(any(k=='wait' for k,v in s.calls))
        self.assertEqual({v for k,v in s.calls if k!='retain'},{1+profile.SAMPLE_BUDGET_NS})

    def test_both_roles_exact_source_six_and_no_seventh(self):
        for role in ('host','friend'):
            s,a=self.make(); s.role=role
            for action,owner,argv in profile.ACTIONS:
                if role==owner:
                    result=a.dispatch_once(action,argv,lambda:s.calls.append(('native-gate',action)),s.deadline_ns)
                else:result={'ok':True}
                a.action_acked(action,result)
            self.assertEqual(a.history,[r[0] for r in profile.ACTIONS])
            self.assertEqual([v for k,v in s.calls if k=='spawn'],
                             [argv for _,owner,argv in profile.ACTIONS if owner==role])
            with self.assertRaises(profile.Refused):a.dispatch_once('host_hit',profile.CROSS,lambda:None,s.deadline_ns)
            for i,(name,_) in enumerate(s.calls):
                if name=='spawn':self.assertEqual([r[0] for r in s.calls[i-2:i]],['native-gate','media-sample'])

    def test_changed_role_argv_order_or_ack_refuse(self):
        for action,argv in [('host_activate_0',profile.MOVE),('friend_position_0',profile.CROSS)]:
            s,a=self.make()
            with self.assertRaises(profile.Refused):a.dispatch_once(action,argv,lambda:None,s.deadline_ns)
            self.assertEqual(s.calls,[])
        s,a=self.make()
        with self.assertRaises(profile.Refused):a.action_acked('friend_position_0',{'ok':True})

    def test_gate_failure_spends_dispatch_clears_hooks_and_no_spawn(self):
        s,a=self.make()
        def refuse():raise RuntimeError('native refusal')
        with self.assertRaises(RuntimeError):a.dispatch_once('friend_position_0',profile.MOVE,refuse,s.deadline_ns)
        with self.assertRaises(profile.Refused):a.dispatch_once('friend_position_0',profile.MOVE,lambda:None,s.deadline_ns)
        self.assertFalse(any(k=='spawn' for k,v in s.calls))
        self.assertTrue(all(getattr(s.driver,k) is None for k in ('diagnostic_dispatch','diagnostic_before_child','diagnostic_stamp')))

    def test_cancel_at_media_spawn_refuses(self):
        s,a=self.make()
        s.media_pre_spawn=lambda action:setattr(s,'cancelled',True)
        with self.assertRaises(RuntimeError):a.dispatch_once('friend_position_0',profile.MOVE,lambda:None,s.deadline_ns)
        self.assertFalse(any(k=='spawn' for k,v in s.calls))

    def test_missing_actual_stamp_is_not_ack(self):
        s,a=self.make(); s.stamp=False
        with self.assertRaises(profile.Refused):a.dispatch_once('friend_position_0',profile.MOVE,lambda:None,s.deadline_ns)
        self.assertEqual(a.history,[])

    def test_contract_drift_after_admission_refuses(self):
        s,a=self.make()
        with patch.object(profile,'SAMPLE_BUDGET_NS',1):
            with self.assertRaises(profile.Refused):a.sample(s.deadline_ns)
        self.assertEqual(s.calls,[])


class DriverBoundaryControls(unittest.TestCase):
    def make(self):
        calls=[]
        class Reclaim:
            def child(self,argv,cwd=None):
                calls.append(('child',argv,d.deadline));return b'{"ok":true}'
        reclaim=Reclaim()
        d=SimpleNamespace(deadline=1000.,reclaim=reclaim,package=__import__('pathlib').Path('package'))
        d.call=lambda *argv:calls.append(('driver',argv,d.deadline)) or {'ok':True}
        phase=SimpleNamespace(child=reclaim.child)
        diagnostic=SimpleNamespace(raw=lambda d:('raw','runtime'),owned=lambda d,base:{'moduleBase':base})
        leaf=DriverCalls(d,phase,diagnostic)
        leaf.owned={'pid':7,'moduleBase':'0x1000'}
        leaf.clock_ns=lambda:10_000_000_000
        return leaf,d,phase,calls

    def test_real_driver_and_avatar_leaf_capped_without_epoch_mix(self):
        leaf,d,phase,calls=self.make()
        with patch('tools.rig.combat_acceptance.adapter.time.monotonic',return_value=100.):
            for name in ('peekBefore','stateAfter','entitiesBefore','avatarObserve'):
                leaf.native(name,deadline_ns=12_000_000_000)
            leaf.call_input(profile.MOVE,deadline_ns=12_000_000_000)
        self.assertTrue(all(row[2]==102. for row in calls))
        self.assertEqual(calls[0][1],('peek','--rva',population.PEEK_SPEC))
        self.assertEqual(calls[3][0],'child')
        self.assertEqual(d.deadline,1000.)

    def test_fixed_timeout_phase_fallback_refused(self):
        leaf,d,phase,calls=self.make();phase.child=lambda *a:None
        with self.assertRaises(profile.Refused):DriverCalls(d,phase,leaf.diagnostic)
        self.assertEqual(calls,[])

    def test_expired_or_overrun_restores_original_no_followup(self):
        leaf,d,phase,calls=self.make()
        with self.assertRaises(profile.Refused):leaf.native('stateBefore',deadline_ns=1)
        self.assertEqual(calls,[])
        d.call=lambda *a:setattr(leaf,'clock_ns',lambda:13_000_000_000) or {'ok':True}
        with patch('tools.rig.combat_acceptance.adapter.time.monotonic',return_value=100.):
            with self.assertRaises(profile.Refused):leaf.native('stateBefore',deadline_ns=12_000_000_000)
        self.assertEqual(d.deadline,1000.)

    def test_original_run_budget_never_renewed(self):
        leaf,d,phase,calls=self.make();d.deadline=101.
        with patch('tools.rig.combat_acceptance.adapter.time.monotonic',return_value=100.):
            leaf.native('stateBefore',deadline_ns=99_000_000_000)
        self.assertEqual(calls[0][2],101.)
        with patch('tools.rig.combat_acceptance.adapter.time.monotonic',return_value=101.):
            with self.assertRaises(profile.Refused):leaf.native('stateBefore',deadline_ns=99_000_000_000)
        self.assertEqual(len(calls),1)

if __name__=='__main__':unittest.main()
