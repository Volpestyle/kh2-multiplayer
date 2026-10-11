"""A-only observed corridor settle. No movement, fixed wait or re-anchoring.

Two fresh brackets over >=250ms, within an original 3s/encounter cap. Stability
means observed endpoints only; no continuous renderer/native-physics claim.
"""
import copy
import math
from . import population,health,preposition

SETTLE_BUDGET_NS=3_000_000_000
STABILITY_NS=250_000_000
MAX_DRIFT=1.0
TARGETS={'host':(0.,-260.,1950.),'friend':(-64.36,-260.,1950.)}
RADIUS=30.

def need(ok,reason):
    if not ok:raise preposition.Refused('HOLD A start: '+reason)

def pose(row):
    value=tuple(float(row[k]) for k in ('x','y','z'))
    need(all(map(math.isfinite,value)),'nonfinite pose')
    return value

class Settle:
    def __init__(self,role,owned,session):
        self.role=role;self.owned=copy.deepcopy(owned);self.session=session
        self.anchor=None;self.first_ns=None;self.last_ns=None;self.count=0;self.ready=False

    def observe(self,value,*,final=False):
        sample=value['sample'];canonical=value['canonical']
        need(value['role']==self.role,'peer role changed')
        population.activation_snapshot(sample,self.owned,sample['readFinishedNs'])
        health.crosscheck_sample(sample,canonical)
        need(canonical['session']==self.owned and canonical['after']['hp']>health.LOW_HP,'canonical original/HP unavailable')
        if self.role=='friend':preposition.proven_a_start(sample,canonical)
        need(not population.activation_requires_admission(sample),'enemy/admission before first input')
        need(self.last_ns is None or sample['readStartedNs']>=self.last_ns,'cached/reordered settle bracket')
        if final:need(self.ready,'settle not complete before first dispatch')
        obs=sample['avatarObserve'];need(obs['ok'] is True and obs['readOnly'] is True and obs['processId']==self.owned['pid'],'original read-only observer unavailable')
        need(len(obs['samples'])==1,'one world bracket required')
        view=obs['samples'][0];world=view['worldBefore']
        need(view['complete'] is True and view['identityStable'] is True and world==view['worldAfter'],'world changed')
        slot=0 if self.role=='host' else 1;other='friend' if self.role=='host' else 'host'
        need(world['localSlot']==slot and world['connectionIds'][slot]>0 and world['connectionIds'][1-slot]>0,'original two-peer roster unavailable')
        runtime=population.records(sample['runtimeAfter'],'[runtime-identity] ')
        need(bool(runtime),'authenticated runtime missing');runtime=runtime[-1]
        need(runtime['session']==runtime['pinSession']==self.session and
             all(runtime[k]=='1' for k in ('identityCurrent','admitted','transportConnected','bridgeOpen')) and runtime['quarantine']=='0',
             'authenticated session lost/changed')
        need(int(runtime['generation'])==world['generation'] and int(runtime['delivery'])==world['deliverySerial'] and
             int(runtime['slot'])==slot and [int(runtime[f'roster{i}']) for i in range(3)]==world['connectionIds'],'runtime/world roster mismatch')
        roots={};positions={}
        for side in ('Before','After'):
            actors=sample['entities'+side]['actors']
            local=[a for a in actors if int(a['address'],16)==canonical['after']['actor']]
            puppet=[a for a in actors if (a['objectId'],a['objectType'],a['team'])==(92,1,0) and a['name']=='P_EX020']
            need(len(local)==len(puppet)==1,'A requires original Sora and authenticated Donald body, not B two-Sora recipe')
            for key,actor,target in (('local',local[0],TARGETS[self.role]),('puppet',puppet[0],TARGETS[other])):
                roots[key+side]=tuple(actor[k] for k in ('address','status','objectId','objectType','team'))
                positions[key+side]=pose(actor['position'])
                need(math.dist(positions[key+side],target)<=RADIUS,'native body outside original corridor')
            player=sample['state'+side]['actors']['player']
            yaw=float(player['rotationY'])
            need(math.isfinite(yaw) and abs(math.atan2(math.sin(yaw-math.pi),math.cos(yaw-math.pi)))<=.05,'A start yaw outside original corridor')
            positions['state'+side]=pose(player['position'])
            need(math.dist(positions['state'+side],TARGETS[self.role])<=RADIUS,'cached state outside original corridor')
        for key in ('local','puppet'):
            need(roots[key+'Before']==roots[key+'After'],'native root changed within bracket')
        puppets=[r['value'] for r in view['puppets'] if r['readAvailable'] and r['value']['active']==1]
        need(len(puppets)==1,'one active original peer puppet required');puppet=puppets[0];provenance=puppet['provenance']
        need(puppet['provenanceMatchesWorld'] is True and provenance['generation']==world['generation'] and
             provenance['localSlot']==slot and provenance['ownerConnectionId']==world['connectionIds'][1-slot] and
             provenance['localConnectionId']==world['connectionIds'][slot] and provenance['hostConnectionId']==world['connectionIds'][0],
             'puppet provenance differs from original world')
        stream=puppet['pose'];need(stream['finite'] is True and stream['ownerSlot']==1-slot and
             [stream['worldId'],stream['roomId']]==[5,6],'peer stream scope unavailable')
        positions['stream']=tuple(float(x) for x in stream['position'])
        need(len(positions['stream'])==3 and all(map(math.isfinite,positions['stream'])) and
             math.dist(positions['stream'],TARGETS[other])<=RADIUS,'peer stream outside original corridor')
        load=population.latest_load(sample['injectAfter'])
        need(population.latest_load(sample['injectBefore'])==load,'native load changed within bracket')
        binding=dict(roots=roots,world=world,load=load)
        if self.anchor is None:
            self.anchor=copy.deepcopy(dict(binding=binding,positions=positions))
            self.first_ns=sample['readStartedNs']
        need(binding==self.anchor['binding'],'original roots/world/load replaced; no reanchor')
        # State and canonical entity agree at each endpoint; native puppet and
        # authenticated stream stay in the same observed A corridor.
        for side in ('Before','After'):
            need(math.dist(positions['state'+side],positions['local'+side])<=MAX_DRIFT,'state/native pose disagree')
            need(math.dist(positions['puppet'+side],positions['stream'])<=MAX_DRIFT,'native puppet/owner stream disagree')
        for key,value_pose in positions.items():
            reference=self.anchor['positions']['localBefore' if key.startswith(('local','state')) else 'puppetBefore']
            need(math.dist(value_pose,reference)<=MAX_DRIFT,'observed drift; zero input, no reanchor')
        self.last_ns=sample['readFinishedNs'];self.count+=1
        self.ready=self.count>=2 and self.last_ns-self.first_ns>=STABILITY_NS
        return dict(role=self.role,ready=self.ready,count=self.count,firstReadNs=self.first_ns,
                    lastReadNs=self.last_ns,original=self.anchor,observed=positions,final=final,
                    observationOnly=True,inputs=0)
