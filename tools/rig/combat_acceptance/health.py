"""Read-only canonical local-Sora guard; no native calls or writes of its own.

Bindings mirror EntityHook::CaptureHitActor/HitCanonical (2725ff71):
native player == active-list head, ObjEntry84/type0/team1, actor+5C0 status.
No name-only, fixed-slot HP, remote puppet or cached CLI HP authority.
"""
import time,re

NATIVE_PLAYER=0x2A105D0
ACTIVE_HEAD=0x2A171C8
LOW_HP=12                 # Lead50 practice-only: 24->16 continues, 16->8 stops.
MAX_SHADOW_HIT=8
MAX_READ_NS=250_000_000
FLAGS=(('cutscene','0xB65210','i',0),('eventContext','0x2A11478','Q',0),
       ('frozenGroups','0x2A171E8','I',0),('inField','0x9BA8D0','B',1),
       ('openMenu','0x7435D0','B',255),('pauseBlockers','0xABB878','I',0))

class HealthRefusal(RuntimeError):
    def __init__(self,reason,evidence):
        super().__init__(reason);self.evidence=evidence

def integer(v):return int(v,16 if isinstance(v,str) and v.lower().startswith('0x') else 10) if isinstance(v,str) else int(v)

def flags(ram):
    return [{'name':name,'rva':rva,'expected':expected,'actual':ram.value(ram.base+int(rva,16),fmt)} for name,rva,fmt,expected in FLAGS]

def failed_flags(rows):return [r for r in rows if r['actual']!=r['expected']]

class CanonicalHealth:
    def __init__(self):self.bindings={}
    def read(self,ram,session,identity,*,clock=time.perf_counter_ns,stage='health'):
        row={'stage':stage,'session':session,'readStartedNs':clock()}
        def need(ok,reason):
            if not ok:raise HealthRefusal(reason,row)
        def snapshot():
            player=ram.value(ram.base+NATIVE_PLAYER,'Q');head=ram.value(ram.base+ACTIVE_HEAD,'Q')
            need(0x10000<=player<0x800000000000 and player==head,'canonical native player/head unavailable or disagree')
            obj=ram.value(player+0x918,'Q');status=ram.value(player+0x5C0,'Q')
            need(0x10000<=obj<0x800000000000 and 0x10000<=status<0x800000000000,'canonical actor roots unavailable')
            return {'actor':player,'head':head,'objentry':obj,'status':status,
                    'objectId':ram.value(obj,'I'),'objectType':ram.value(obj+4,'B'),
                    'team':ram.value(player+0x4DC,'I'),'hp':ram.value(status,'i'),'maxHp':ram.value(status+4,'i')}
        try:
            row['identityBefore']=identity();need(row['identityBefore']==session,'original owned session changed before health')
            row['before']=snapshot();row['flags']=flags(ram)
            row['cachedSlot']={'hp':ram.value(ram.base+0x2A23598,'i'),'maxHp':ram.value(ram.base+0x2A2359C,'i')}
            row['after']=snapshot();row['identityAfter']=identity();row['readFinishedNs']=clock()
            need(row['identityAfter']==session,'original owned session changed during health')
            need(row['readFinishedNs']>=row['readStartedNs'] and row['readFinishedNs']-row['readStartedNs']<=MAX_READ_NS,'canonical health read stale/invalid chronology')
            need(row['before']==row['after'],'canonical actor/status/HP changed during health read')
            s=row['after'];row['nativeDeath']=s['hp']==0
            need((s['objectId'],s['objectType'],s['team'])==(84,0,1),'canonical local Sora type/id/team mismatch')
            key=tuple(sorted(session.items()));binding={k:s[k] for k in ('actor','objentry','status','objectId','objectType','team')}
            need(key not in self.bindings or self.bindings[key]==binding,'canonical session actor/status replaced; no reanchor')
            need(row['cachedSlot']['maxHp']==s['maxHp'] and s['hp']<=row['cachedSlot']['hp']<=s['maxHp'],'invalid or lower cached HP disagreement')
            row['cachedLagObserved']=row['cachedSlot']['hp']!=s['hp']
            need(0<s['maxHp'] and 0<=s['hp']<=s['maxHp'],'canonical status HP invalid')
            need(s['hp']>LOW_HP,f'canonical low-HP/death refusal {s["hp"]}/{s["maxHp"]}, margin{LOW_HP}')
            row['failedFlags']=failed_flags(row['flags']);need(not row['failedFlags'],'native unsafe flags: '+repr(row['failedFlags']))
            self.bindings.setdefault(key,binding);row['ok']=True;return row
        except Exception as exc:
            row['ok']=False;row['reason']=str(exc)
            row['failedFlags']=failed_flags(row.get('flags',[]))
            if isinstance(exc,HealthRefusal):raise
            raise HealthRefusal('canonical health read unavailable: '+repr(exc),row) from exc

def _crosscheck_sample(sample,canonical):
    """Every recorded source must agree; no fixed-slot/state majority vote."""
    current=canonical['after'];errors=[]
    if current['hp']<=LOW_HP:raise HealthRefusal('canonical low-HP/death refusal',canonical)
    canonical['cachedStateLagObserved']=[]
    for side in ('Before','After'):
        state=sample['state'+side];entities=sample['entities'+side]
        if entities.get('ok') is not True or entities.get('processId')!=canonical['session']['pid']:errors.append('entities'+side+': original session unavailable/mismatch')
        rows=[r for r in entities['actors'] if integer(r['address'])==current['actor']]
        if len(rows)!=1:errors.append('entities'+side+': unique canonical actor unavailable');continue
        actor=rows[0]
        if (integer(actor['status']),actor['objectId'],actor['objectType'],actor['team'],actor['hp'],actor['maxHp'])!=(current['status'],84,0,1,current['hp'],current['maxHp']):errors.append('entities'+side+': canonical identity/status/HP disagreement')
        player=state['actors']['player']
        # State exposes HP but no maxHp; do not invent a field or use its slot actorId as a pointer.
        if player['hp']!=current['hp']:
            if current['hp']<player['hp']<=current['maxHp']:canonical['cachedStateLagObserved'].append({'side':side,'hp':player['hp']})
            else:errors.append('state'+side+': invalid or lower cached HP disagreement')
        if player.get('downed') is not False:errors.append('state'+side+': downed flag true/unavailable')
    if errors:raise HealthRefusal('; '.join(errors),{'sourceDisagreements':errors,'canonical':canonical,'sample':sample})

def crosscheck_sample(sample,canonical):
    try:return _crosscheck_sample(sample,canonical)
    except HealthRefusal:raise
    except Exception as exc:raise HealthRefusal('health source fields unavailable: '+repr(exc),{'sourceDisagreements':[repr(exc)],'canonical':canonical}) from exc

def _host_temporal_chain(event,trace,receipt,sample,actor):
    """Host equivalent of the client-only native Witness predicate.

    This proves only a reason to reacquire HP, never incoming-hit acceptance.
    NativeHitTrace::Witness intentionally reports zero for host slot0.
    """
    def need(ok):
        if not ok:raise ValueError('host temporal chain incomplete')
    def one(tag,**keys):
        rows=[f for line,f in trace if '[hittrace] '+tag+' ' in line and all(f.get(k)==str(v) for k,v in keys.items())]
        need(len(rows)==1);return rows[0]
    def same(left,right,keys):return all(k in left and left[k]==right.get(k) for k in keys.split())
    need(event.get('witness')=='0')
    need(all(event.get(k)==v for k,v in dict(ownerThread='1',callerAvailable='1',callerRva='3D613C',parent='0',depth='1',coverageMask='7',lossSerial='0',takeCalls='1',statCalls='1',childCount='2').items()))
    need(int(event['coverageSerial'])>0)
    contexts=[one('context',phase=i) for i in (0,1)]
    c,ca=contexts
    need(same(c,ca,'frame generation epoch transitionSerial loadSerial connectionId hostConnectionId role slot location'))
    need(all(x.get('available')=='1' and x.get('readMask')=='127' for x in contexts))
    need(c['role']=='1' and c['slot']=='0' and c['connectionId']==c['hostConnectionId'])
    need(all(int(c[k])>0 for k in ('generation','epoch','loadSerial','connectionId','hostConnectionId')))
    need(receipt['role']=='1' and receipt['slot']=='0' and receipt['connection']==c['connectionId'] and receipt['host']==c['hostConnectionId'])
    observe=sample['avatarObserve'];need(observe['ok'] is True and observe['processId']==sample['ownedBefore']['pid'])
    worlds=observe['samples'][0];need(worlds['identityStable'] is True and worlds['complete'] is True and worlds['worldBefore']==worlds['worldAfter'])
    world=worlds['worldAfter'];need(world['localSlot']==0 and world['generation']==int(c['generation']) and world['connectionIds'][0]==int(c['connectionId']))
    victims=[one('actor',phase=i,subject=0) for i in (0,1)];sources=[one('actor',phase=i,subject=1) for i in (0,1)];inputs=[one('input',phase=i) for i in (0,1)]
    metadata='actor objentry status objectId type team namePrefix maxHp'
    need(same(victims[0],victims[1],metadata) and same(sources[0],sources[1],metadata))
    need(all(x.get('readMask')=='511' for x in victims+sources))
    src=sources[0];need(src['type']=='4' and int(src['objectId'])>0 and src['namePrefix']!='5F46' and all(int(x['hp'])>0 and int(x['maxHp'])>0 for x in sources))
    h,ha=inputs;need(same(h,ha,'hit attack owner attackHandle atkpHandle ownerHandle attackId kind stat'))
    need(all(x.get('readMask')=='255' and all(x.get(k)=='0' for k in ('syncDrop','manualFilterOn','manualDrop')) for x in inputs))
    need(all(int(h[k],16 if k in ('hit','attack','owner') else 10)>0 for k in ('hit','attack','owner','attackHandle','atkpHandle','ownerHandle')))
    need(h['owner']==src['actor'] and int(h['owner'],16)!=actor['actor'] and h['stat']=='0' and int(h['damage'])>0 and h['kind'] not in ('5','6'))
    need(int(h['flags'])&2==0 and int(ha['flags'])&2!=0)
    need(all(int(x[k],16)==actor['actor'] for x in inputs for k in ('canonicalPlayer','head','tracked')))
    children=[f for line,f in trace if '[hittrace] child ' in line];need(len(children)==2)
    take=one('child',kind=1);stat=one('child',kind=2)
    need(take['index']=='0' and stat['index']=='1' and take['takeSeq']=='0' and stat['takeSeq']==take['childSeq'])
    need(int(event['seq'])<int(take['childSeq'])<int(stat['childSeq']) and take['delta']==stat['delta'])
    need(int(stat['result'])==actor['hp'] and actor['hp']==max(0,int(victims[0]['hp'])+int(stat['delta'])))
    for child in children:
        need(child['matching']=='1' and child['returned']=='1' and child['unwound']=='0' and child['applySeq']==event['seq'] and int(child['actor'],16)==actor['actor'] and child['stat']=='0' and int(child['delta'])<0)
        for phase,hp in ((0,int(victims[0]['hp'])),(1,actor['hp'])):
            observed=one('actor',phase=phase,subject=int(child['index'])+2)
            need(same(victims[0],observed,metadata) and observed['readMask']=='511' and int(observed['hp'])==hp)
    return True

def intervening_hit(sample,canonical,log):
    """Certify only temporal HP skew. Never permits dispatch; caller reacquires."""
    try:
        a=canonical['after'];need=lambda ok: (_ for _ in ()).throw(ValueError()) if not ok else None
        need(canonical['ok'] is True and canonical['before']==a and a['hp']>LOW_HP)
        need(canonical['identityBefore']==canonical['identityAfter']==canonical['session']==sample['ownedBefore']==sample['ownedAfter'])
        need(not canonical.get('failedFlags') and all(x['actual']==x['expected'] for x in canonical['flags']))
        need((a['objectId'],a['objectType'],a['team'])==(84,0,1))
        need(0<=canonical['readFinishedNs']-canonical['readStartedNs']<=MAX_READ_NS)
        old=None;straddled=False
        for side in ('Before','After'):
            e=sample['entities'+side];need(e['ok'] is True and e['processId']==canonical['session']['pid'])
            rows=[x for x in e['actors'] if integer(x['address'])==a['actor']];need(len(rows)==1);x=rows[0]
            need((integer(x['status']),x['objectId'],x['objectType'],x['team'],x['maxHp'])==(a['status'],84,0,1,a['maxHp']))
            if side=='Before':
                need(a['hp']<x['hp']<=a['maxHp']);old=x['hp']
            else:
                need(x['hp'] in (old,a['hp']));straddled=x['hp']==a['hp']
            player=sample['state'+side]['actors']['player'];need(player['downed'] is False and a['hp']<=player['hp']<=a['maxHp'])
        need(log.startswith(sample['injectAfter']))
        lines=log.splitlines();parsed=[(line,dict(re.findall(r'(\w+)=([^\s]*)',line))) for line in lines]
        for line,r in parsed:
            if '[combat-causal] receipt ' not in line or r.get('kind')!='1' or r.get('beforeHp')!=str(old) or r.get('afterHp')!=str(a['hp']):continue
            qpc=int(r['qpc']);seq=r['event']
            # The hit may follow both entity reads but precede the final peek.
            # A complete typed chain grants only a fresh whole-bracket retry.
            lower=sample['readStartedNs']
            if not lower<qpc<canonical['readStartedNs']:continue
            trace=[(l,f) for l,f in parsed if '[hittrace]' in l and f.get('seq')==seq]
            event=[f for l,f in trace if '[hittrace] event ' in l]
            if len(event)!=1:continue
            need(all(event[0][k]=='1' for k in ('returned','coverageStable','contextStable','metadataStable','lossStable')) and all(event[0][k]=='0' for k in ('unwound','nested','overflow')))
            if r.get('role')=='1':need(_host_temporal_chain(event[0],trace,r,sample,a))
            else:need(r.get('role')=='2' and event[0]['witness']=='1')
            for phase,hp in (('0',old),('1',a['hp'])):
                actors=[f for l,f in trace if '[hittrace] actor ' in l and f.get('phase')==phase and f.get('subject')=='0'];need(len(actors)==1);f=actors[0]
                need((int(f['actor'],16),int(f['status'],16),int(f['objentry'],16),int(f['objectId']),int(f['type']),int(f['team']),int(f['hp']),int(f['maxHp']))==(a['actor'],a['status'],a['objentry'],84,0,1,hp,a['maxHp']))
                inputs=[f for l,f in trace if '[hittrace] input ' in l and f.get('phase')==phase];need(len(inputs)==1)
                need(int(inputs[0]['canonicalPlayer'],16)==int(inputs[0]['head'],16)==a['actor'])
                contexts=[f for l,f in trace if '[hittrace] context ' in l and f.get('phase')==phase];need(len(contexts)==1);f=contexts[0]
                need(f['frame']==r['frame'] and f['epoch']==r['epoch'] and f['loadSerial']==r['load'] and f['generation']==r['generation'] and f['available']=='1')
            return {'event':seq,'frame':r['frame'],'qpcNs':qpc,'oldHp':old,'newHp':a['hp'],'actor':a['actor'],'status':a['status'],'bracketStraddled':straddled,'decision':'REACQUIRE_REQUIRED'}
        return None
    except (KeyError,ValueError,TypeError,IndexError):return None
