import copy
import unittest
from . import population,summary_acquisition as gap,profile
from .adapter import PacketAdapter
from .test_adapter import Services

SCOPE='generation=2 delivery=1 connection=2 epoch=2 load=4 transition=3 role=2 slot=1 location=5,6,0,1,1,0'
def summary(frame,admitted=1,extra=''):
    return f'[combat-causal] summary {SCOPE} ownerFrame={frame} ownerAvailable=1 admitted={admitted} retired=0 dropped=0 loss=0 pendingNativeLeave=0 {extra}\n'
def admissions(frame):
    return ''.join(f'[combat-causal] receipt {SCOPE} kind=0 qualified=1 reason=0 loss=0 frame={frame} netId={i}\n' for i in range(5))

class Logs(Services):
    def __init__(self):
        super().__init__();self.prefix=summary(3863,0)
    def read_logs(self,*,deadline_ns):
        self.bounded('logs',deadline_ns);return self.prefix,'original-runtime'

class SummaryControls(unittest.TestCase):
    def make(self):
        s=Logs();a=PacketAdapter(s,profile.adapter_contract());a.sample(s.deadline_ns)
        return s,a

    def test_actual_gap_3864_and_3924_blind_spot_without_wait_or_cached_bracket(self):
        for frame in (3864,3924):
            s,a=self.make();baseline=copy.deepcopy(a.last_sample)
            s.prefix+=admissions(frame)+summary(frame)
            s.calls.clear();sample=a.sample(s.deadline_ns)
            self.assertNotIn(gap.SUMMARY,sample['injectAfter'][len(sample['injectBefore']):])
            self.assertGreater(sample['readStartedNs'],baseline['readFinishedNs'])
            self.assertTrue(gap.verify(sample))
            self.assertEqual(sample['summaryAcquisition']['summary']['ownerFrame'],str(frame))
            self.assertFalse(any(k=='wait' for k,v in s.calls))

    def test_same_baseline_summary_cannot_qualify_or_renew(self):
        s,a=self.make();s.prefix+=admissions(3864)+summary(3864)
        a.sample(s.deadline_ns);s.step=100_000_000;s.calls.clear()
        started=s.clock_ns()
        with self.assertRaises(profile.Refused):a.sample(s.deadline_ns)
        self.assertEqual({end for k,end in s.calls if k!='retain'},
                         {started+profile.SAMPLE_BUDGET_NS})

    def test_prefix_scope_identity_and_loss_refuse(self):
        for mutation in ('prefix','scope','identity','loss','runtime','nonadmitted'):
            s,a=self.make();s.prefix+=admissions(3864)+summary(3864)
            row=a.sample(s.deadline_ns);baseline=row['summaryAcquisition']['baseline']
            if mutation=='prefix':row['injectBefore']='replaced'
            elif mutation=='scope':row['injectAfter']+=summary(3865).replace('epoch=2','epoch=3')
            elif mutation=='identity':row['ownedAfter']=dict(row['ownedAfter'],creationTicks=999)
            elif mutation=='loss':row['injectAfter']+=summary(3865).replace('loss=0','loss=1')
            elif mutation=='runtime':row['runtimeBefore']='replaced'
            else:row['injectAfter']+=summary(3865,0)
            if mutation=='nonadmitted':self.assertIsNone(gap.qualify(row,baseline))
            else:
                with self.assertRaises(population.Unqualified):gap.qualify(row,baseline)

    def test_forged_proof_and_missing_original_admission_refuse(self):
        s,a=self.make();s.prefix+=admissions(3864)+summary(3864)
        row=a.sample(s.deadline_ns);row['summaryAcquisition']['summary']['ownerFrame']='9999'
        with self.assertRaises(population.Unqualified):gap.verify(row)
        row['injectAfter']=row['injectAfter'].replace('qualified=1','qualified=0')
        with self.assertRaises(population.Unqualified):gap.qualify(row,row['summaryAcquisition']['baseline'])

    def test_cancellation_still_stops_before_sample(self):
        s,a=self.make();s.cancelled=True
        with self.assertRaises(RuntimeError):a.sample(s.deadline_ns)

if __name__=='__main__':unittest.main()
