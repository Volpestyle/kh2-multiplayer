"""Review3 hidden-history adversaries through real PacketAdapter + Admission.

Synthetic logs/five native Shadows over retained sample-002. All acquisition,
summary proof and complete five-body gates are real; native I/O leaves are data.
"""
import copy,unittest
from .test_activation import FIXTURE
from . import population as p,summary_acquisition as gap,profile
from .adapter import PacketAdapter

SCOPE='generation=2 delivery=1 sessionSalt=1 connection=2 host=1 peer=2 hostDelivery=1 peerDelivery=1 epoch=2 load=4 transition=3 role=2 slot=1 location=5,6,0,1,1,0'
def summary(frame):
    return f'[combat-causal] summary {SCOPE} ownerFrame={frame} ownerAvailable=1 admitted=1 retired=0 dropped=0 loss=0 pendingNativeLeave=0\n'

def native():
    s=copy.deepcopy(FIXTURE['sample']);admits=''
    for i in range(1,6):
        actor=0x12345000+i*0x1000;status=actor+0x100;obj=actor+0x200
        admits+=f'[combat-causal] receipt {SCOPE} serial={i} kind=0 qualified=1 reason=0 loss=0 frame=9 netId={i} objectId=302 type=4 actor={actor:x} status={status:x} objentry={obj:x} controller={actor+0x300:x} record={actor+0x400:x} targetHp=20 maxHp=20\n'
        for key in ('entitiesBefore','entitiesAfter'):
            s[key]['actors'].append(dict(address=hex(actor),status=hex(status),name='M_EX020',objectId=302,objectType=4,hp=20,maxHp=20))
    log='[warp] load complete serial=4 transition=3 room=05/06 door=0 map=1 btl=1 evt=0\n[enemysync] client arrived epoch=2 room=05/06 door=0 map=1 btl=1 evt=0\n'+admits
    return s,log

class Data:
    deadline_ns=100_000_000_000
    def __init__(self):
        self.s,self.prefix=native();self.owned=self.s['ownedBefore'];self.now=1
        self.before=self.prefix+summary(9);self.after=self.before+summary(10)
        self.calls=0;self.retained=[]
        self.runtime='[combat-causal-runtime] sessionHex=616263646566 generation=2 delivery=1 slot=1 self=2 host=1\n'
    def clock_ns(self):self.now+=1_000_000;return self.now
    def check_stop_and_ownership(self,owned):assert owned==self.owned
    def read_logs(self,*,deadline_ns):
        self.calls+=1;return (self.before if self.calls%2 else self.after),self.runtime
    def read_owned(self,*,deadline_ns):return copy.deepcopy(self.owned)
    def native(self,name,*,deadline_ns):return copy.deepcopy(self.s[name])
    def wait_tick(self,*,deadline_ns):self.now+=100_000_000
    def retain(self,name,value):self.retained.append((name,value))

class Controls(unittest.TestCase):
    def attempt(self,suffix,path):
        d=Data();a=PacketAdapter(d,profile.adapter_contract())
        if path!='direct':
            baseline=a.sample(d.deadline_ns)
            # Complete actual five-body positive before testing later history.
            self.assertTrue(p.Admission('control','friend',d.owned,'abcdef').check(baseline,d.now)['qualifiedPopulation'])
            base=baseline['injectAfter']
        else:base=d.prefix+summary(10)
        if path=='gap':d.before=d.after=base+suffix
        elif path=='split':
            # Hidden row lies in gap; clean last row is within native bracket.
            pieces=suffix.splitlines(keepends=True);d.before=base+''.join(pieces[:-1]);d.after=d.before+pieces[-1]
        else:d.before=base;d.after=base+suffix
        d.calls=0
        return d,a,base
    def test_clean_positive_gap_direct_and_split(self):
        for path in ('gap','direct','split'):
            d,a,base=self.attempt(summary(11)+summary(12),path)
            row=a.sample(d.deadline_ns)
            self.assertTrue(p.Admission('control','friend',d.owned,'abcdef').check(row,d.now)['qualifiedPopulation'])
    def test_foreign_middle_and_stale_tail_refuse_every_path(self):
        tails=(summary(11).replace('generation=2','generation=999')+summary(12),
               summary(11)+summary(9).replace('generation=2','generation=999'))
        for tail in tails:
            for path in ('gap','direct','split'):
                d,a,base=self.attempt(tail,path)
                with self.assertRaises(p.Unqualified):a.sample(d.deadline_ns)
    def test_duplicate_conflicting_and_reordered_refuse_every_path(self):
        tails=(summary(11)*2,summary(11)*2+summary(12),summary(10),
               summary(11)+summary(11).replace('pendingNativeLeave=0','pendingNativeLeave=1'),
               summary(12)+summary(11),summary(11)+summary(9))
        for tail in tails:
            for path in ('gap','direct','split'):
                d,a,base=self.attempt(tail,path)
                with self.assertRaises(p.Unqualified):a.sample(d.deadline_ns)
    def test_full_admission_cannot_bypass_direct_suffix_verification(self):
        for tail in (summary(11).replace('generation=2','generation=999')+summary(12),
                     summary(11)+summary(9).replace('generation=2','generation=999'),
                     summary(11)*2,summary(12)+summary(11)):
            d,a,base=self.attempt(tail,'direct')
            row=copy.deepcopy(d.s)
            row.update(injectBefore=d.before,injectAfter=d.after,runtimeBefore=d.runtime,runtimeAfter=d.runtime,
                       ownedBefore=d.owned,ownedAfter=d.owned,readStartedNs=1,readFinishedNs=2)
            with self.assertRaises(p.Unqualified):p.Admission('control','friend',d.owned,'abcdef').check(row,2)
    def test_full_admission_replays_entire_gap_even_with_old_style_proof(self):
        for tail in (summary(11).replace('generation=2','generation=999')+summary(12),
                     summary(11)+summary(9).replace('generation=2','generation=999'),
                     summary(11)*2,summary(12)+summary(11)):
            d,a,base=self.attempt(summary(11),'gap');row=a.sample(d.deadline_ns)
            row['injectBefore']=row['injectAfter']=base+tail
            # Reproduce the previous last-newer-row proof selection exactly.
            row['summaryAcquisition']['summary']=[r for r in p.records(tail,gap.SUMMARY) if p.number(r,'ownerFrame')>10][-1]
            with self.assertRaises(p.Unqualified):p.Admission('control','friend',d.owned,'abcdef').check(row,d.now)
    def test_duplicate_alone_cannot_manufacture_gap_proof(self):
        d,a,base=self.attempt(summary(10),'gap')
        with self.assertRaises(p.Unqualified):a.sample(d.deadline_ns)
if __name__=='__main__':unittest.main()
