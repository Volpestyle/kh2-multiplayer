"""File/JSON-only gates. Caller alone owns reads, clocks, dispatch and closure."""
import copy,hashlib,re

LOCATION=[5,6,0,1,1,0]
SCOPE=('generation','delivery','sessionSalt','connection','host','peer','hostDelivery','peerDelivery','epoch','load','transition','role','slot','location')
ROOTS=('netId','objectId','type','actor','objentry','status','controller','record','maxHp')
NOW=[('0x717008','u8'),('0x717009','u8'),('0x71700A','u8'),('0x71700C','u16'),('0x71700E','u16'),('0x717010','u16')]
SAFE=[('0xB65210','i32',0),('0x2A11478','u64',0),('0x2A171E8','u32',0),('0x9BA8D0','u8',1),('0x7435D0','u8',255),('0xABB878','u32',0)]
PEEK_SPEC=','.join(f'{r}:{t}' for r,t in NOW)+','+','.join(f'{r}:{t}' for r,t,_ in SAFE)
MOVE=('player-input','--ly','1','--duration-ms','900')
CROSS=('player-press','--button','cross','--duration-ms','80')
ACTIONS=[(f'host_activate_{i}','host',MOVE) for i in range(3)]+[(f'friend_position_{i}','friend',MOVE) for i in range(3)]+[('host_hit','host',CROSS),('friend_claim','friend',CROSS)]

class Unqualified(RuntimeError):pass
def need(value,reason):
    if not value:raise Unqualified(reason)
def integer(value):
    if type(value) is int:return value
    need(isinstance(value,str),'integer missing')
    return int(value,16 if value.lower().startswith('0x') else 10)
def fields(line):return dict(re.findall(r'(\w+)=([^\s]*)',line))
def records(text,prefix):return [fields(line.split(prefix,1)[1]) for line in text.splitlines() if prefix in line]
def receipt_rows(text):return records(text,'[combat-causal] receipt ')
def number(row,key):return integer(row[key])
def scope(row):return tuple(row[k] for k in SCOPE)
def roots(row):return tuple(row[k] for k in ROOTS)
def latest_load(text,admitted=None,original_session=None):
    pattern=r'\[warp\] load complete serial=(\d+) transition=(\d+) room=([0-9A-Fa-f]+)/([0-9A-Fa-f]+) door=(\d+) map=(\d+) btl=(\d+) evt=(\d+)'
    hits=list(re.finditer(pattern,text));need(hits,'native completed load missing')
    hit=hits[-1];v=hit.groups();result=[int(v[0]),int(v[1]),int(v[2],16),int(v[3],16),*map(int,v[4:])]
    # A completed native load may be followed by its own arrival receipt.
    # Authenticate that exact existing cause; never re-anchor to a later cause.
    suffix=records(text[hit.end():],'[load-cause] ')
    prior=records(text[:hit.start()],'[load-cause] ')
    loads=[r for r in prior if r.get('event')=='load' and number(r,'load')==result[0] and number(r,'transition')==result[1]]
    if loads:
        need(len(loads)==1,'completed load cause ambiguous')
        completed=loads[0]
        need(not any(r.get('event')=='arrival' and r.get('cause')==completed.get('cause') and r.get('load')==completed.get('load') for r in prior),'arrival before completed load')
    if suffix:
        need(len(suffix)==1 and len(loads)==1,'new load cause after completed baseline')
        arrival=suffix[0];completed=loads[0]
        need(arrival.get('event')=='arrival' and completed.get('origin')=='ordinary_host_room' and number(completed,'cause')>0 and number(completed,'complete')==1 and number(completed,'available')==1,'matching completed ordinary arrival required')
        need(set(arrival)==set(completed) and all(arrival[k]==completed[k] for k in completed if k not in ('seq','event')) and number(arrival,'seq')==number(completed,'seq')+1,'arrival differs from original completed cause/load')
        need(completed['room']==f'{result[2]:02X}/{result[3]:02X}' and [number(completed,k) for k in ('door','map','btl','evt')]==result[4:],'completed cause native tuple differs')
        if admitted is not None:
            need(completed['session']==original_session and all(number(completed,k)==number(admitted,k) for k in ('generation','delivery','host','epoch','load','transition')) and number(completed,'target')==number(admitted,'connection') and number(completed,'targetDelivery')==number(admitted,'delivery'),'arrival cause differs from original admitted scope')
    return result
def summary(text):
    rows=records(text,'[combat-causal] summary ');need(rows,'observer summary missing')
    r=rows[-1]
    need(all(number(r,k)==v for k,v in [('admitted',1),('retired',0),('dropped',0),('loss',0)]),'observer unavailable/retired/lost')
    return r

def activation_snapshot(sample,owned,now_ns):
    """Pre-population gate; never grants an attack or typed admission."""
    need(sample['ownedBefore']==owned==sample['ownedAfter'],'original owned identity changed')
    # Lead decision: age is recorded evidence validity, never a dispatch veto.
    # Chronology and the bounded native-read operation remain hard checks.
    need(now_ns>=sample['readFinishedNs'] and 0<sample['readFinishedNs']-sample['readStartedNs']<=5_000_000_000,'native read chronology/operation budget invalid')
    for key in ['peekBefore','peekAfter']:
        data=sample[key];need(data.get('ok') is True and data['processId']==owned['pid'] and integer(data['moduleBase'])==integer(owned['moduleBase']),'peek original PID/module mismatch')
        need(len(data['samples'])==1,'one native sample required');row=data['samples'][0]
        need([integer(row[r]) for r,_ in NOW]==LOCATION,'full current native tuple changed')
        need(all(integer(row[r])==v for r,_,v in SAFE),'native event/context/frozen/field/menu/pause unsafe')
    for key in ['stateBefore','stateAfter']:
        data=sample[key];room=data['room']
        need(data.get('ok') is True and data.get('attached') is True and data['processId']==owned['pid'] and data.get('atTitleOrLoading') is False,'state original PID unavailable')
        need([room[k] for k in ['worldId','roomId','mapProgram','battleProgram','eventProgram']]==[5,6,1,1,0] and room['inTransition'] is False and room['inCutscene'] is False,'current state scope unsafe')
        need(data['actors']['player']['hp']>0,'local player unavailable/dead')
    for key in ['entitiesBefore','entitiesAfter']:
        data=sample[key];need(data.get('ok') is True and data['processId']==owned['pid'] and [data['world'],data['room']]==[5,6],'native entity scope mismatch')
        enemies=[r for r in data['actors'] if r['objectType']==4 or r['name'].startswith(('M_','B_'))]
        need(len(enemies)<=5 and all(r['objectId']==302 and r['objectType']==4 and r['hp']>0 for r in enemies),'unknown/replaced/dead enemy family')
    for key in ['injectBefore','injectAfter']:
        text=sample[key]
        need(not any(number(r,'kind')==11 or number(r,'loss') for r in receipt_rows(text)),'activation observer retired/lost')
        summaries=records(text,'[combat-causal] summary ')
        need(not summaries or all(number(summaries[-1],k)==0 for k in ['retired','dropped','loss']),'activation observer summary retired/lost')
    return True

def activation_requires_admission(sample):
    return bool([r for r in receipt_rows(sample['injectAfter']) if number(r,'kind')==0])

def peer_freshness(sent_ns,received_ns,now_ns):
    """All THREE timestamps are original local QPC, not remote clock values.

    Caller validates run/token/nonce/role and proof sampled after challenge.
    Full RTT is a conservative bound; never infer one-way transit.
    """
    need(all(type(x) is int for x in [sent_ns,received_ns,now_ns]) and 0<sent_ns<=received_ns<=now_ns,'peer challenge chronology invalid')
    return {'sshRttNs':received_ns-sent_ns,'peerProofAgeUpperNs':now_ns-sent_ns,
            'within250ms':now_ns-sent_ns<=250_000_000,'dispatchAllowedByTiming':True}

def dispatch_timing(action_id,role,sent_ns,received_ns,dispatch_ns,read_started_ns,read_finished_ns):
    need((action_id,role) in [('host_setup_warp','host')]+[(i,r) for i,r,_ in ACTIONS],'unknown timed action')
    peer=peer_freshness(sent_ns,received_ns,dispatch_ns)
    need(type(read_started_ns) is int and type(read_finished_ns) is int and 0<read_started_ns<=read_finished_ns<=dispatch_ns,'native read timing chronology invalid')
    return dict(peer,actionId=action_id,role=role,sentNs=sent_ns,receivedNs=received_ns,
                dispatchNs=dispatch_ns,readStartedNs=read_started_ns,readFinishedNs=read_finished_ns,
                localStateAgeNs=dispatch_ns-read_finished_ns,localBracketAgeNs=dispatch_ns-read_started_ns,
                qualifiedTiming=peer['within250ms'] and dispatch_ns-read_finished_ns<=250_000_000)

class Admission:
    def __init__(self,run_id,role,owned,session_hex):
        need(role in ('host','friend') and bool(run_id),'invalid role/run')
        need(set(owned)=={'pid','creationTicks','moduleBase','imageSha256'} and owned['pid']>0 and owned['creationTicks']>0,'original owned admission missing')
        need(bool(re.fullmatch(r'(?:[0-9a-f]{2})+',session_hex)),'exact authenticated session hex required')
        self.run_id,self.role,self.owned,self.session=run_id,role,copy.deepcopy(owned),session_hex
        # Original SessionState text -> lowercase ASCII-byte hex, once.
        # Do not decode/re-anchor ledger values or normalize their spelling.
        self.runtime_session_hex=self.session.encode('ascii').hex()
        self.anchor=None;self.targets=None;self.world=None;self.last_log='';self.last_runtime='';self.retired=False
    def check(self,sample,now_ns):
        try:return self._check(sample,now_ns)
        except (Unqualified,KeyError,ValueError,TypeError,IndexError) as exc:
            self.retired=True
            raise Unqualified(str(exc)) from exc
    def _check(self,s,now_ns):
        need(not self.retired,'oracle permanently retired; no reanchor')
        activation_snapshot(s,self.owned,now_ns)
        before,after=s['injectBefore'],s['injectAfter'];rb,ra=s['runtimeBefore'],s['runtimeAfter']
        need(after.startswith(before) and before.startswith(self.last_log) and ra.startswith(rb) and rb.startswith(self.last_runtime),'raw log prefix replaced/truncated')
        need('[combat-causal] summary ' in after[len(before):],'fresh owner-frame mapping summary missing')
        summary(after)
        need(not any(number(r,'kind')==11 or number(r,'loss') for r in receipt_rows(after)),'observer retired/lost')
        load=latest_load(after);need(latest_load(before)==load and load[2:]==LOCATION,'native load bookend changed')
        admitted=[r for r in receipt_rows(after) if number(r,'kind')==0]
        need(len(admitted)==5 and all(number(r,'qualified')==1 and number(r,'reason')==0 for r in admitted),'original five-target native admission missing')
        anchor=scope(admitted[0]);need(all(scope(r)==anchor for r in admitted),'admission scope differs')
        latest_load(before,admitted[0],self.session);latest_load(after,admitted[0],self.session)
        floor=min(number(r,'serial') for r in admitted)
        need(all(scope(r)==anchor for r in receipt_rows(after) if number(r,'serial')>=floor),'post-admission producer scope replaced')
        slot=0 if self.role=='host' else 1
        need(number(admitted[0],'slot')==slot and number(admitted[0],'role')==slot+1 and admitted[0]['location']=='5,6,0,1,1,0','native role/tuple mismatch')
        need([number(admitted[0],'load'),number(admitted[0],'transition')]==load[:2],'native admission lifecycle mismatch')
        arrivals=re.findall(r'\[enemysync\] (?:host|client) arrived epoch=(\d+) room=([0-9A-Fa-f]+)/([0-9A-Fa-f]+) door=(\d+) map=(\d+) btl=(\d+) evt=(\d+)',after)
        need(arrivals and int(arrivals[-1][0])==number(admitted[0],'epoch') and [int(arrivals[-1][1],16),int(arrivals[-1][2],16),*map(int,arrivals[-1][3:])]==LOCATION,'native arrived epoch/tuple missing or replaced')
        targets={number(r,'netId'):r for r in admitted}
        need(len(targets)==5 and all(k>0 and number(r,'objectId')==302 and number(r,'type')==4 and 0<number(r,'targetHp')<=number(r,'maxHp') for k,r in targets.items()),'five live typed original targets required')
        need(all(all(int(r[k],16)>0 for k in ['actor','objentry','status','controller','record']) for r in targets.values()),'checked original native roots missing')
        for key in ['entitiesBefore','entitiesAfter']:
            rows=[r for r in s[key]['actors'] if r['objectType']==4 or r['name'].startswith(('M_','B_'))]
            need(len(rows)==5,'living population no longer five')
            by_actor={integer(r['address']):r for r in rows};need(len(by_actor)==5,'aliased native target')
            for r in targets.values():
                actor=by_actor.get(int(r['actor'],16));need(actor is not None,'original actor disappeared')
                need(integer(actor['status'])==int(r['status'],16) and actor['objectId']==302 and actor['objectType']==4 and actor['maxHp']==number(r,'maxHp') and actor['hp']>0,'original typed actor/status/maxHP changed')
        obs=s['avatarObserve'];need(obs.get('ok') is True and obs['processId']==self.owned['pid'] and obs.get('readOnly') is True and len(obs['samples'])==1,'read-only world observation required; peek is insufficient')
        row=obs['samples'][0];need(row['complete'] is True and row['identityStable'] is True and row['worldBefore']==row['worldAfter'],'world read bookends unstable')
        world=row['worldBefore'];r=admitted[0]
        need(world['localSlot']==slot and world['generation']==number(r,'generation') and world['deliverySerial']==number(r,'delivery') and world['connectionIds']==[number(r,'host'),number(r,'peer'),0] and world['peerDeliverySerials'][:2]==[number(r,'hostDelivery'),number(r,'peerDelivery')],'current bridge original scope mismatch')
        local=row['local'];puppet=row['puppets'][0]['value']
        need(row['localReadAvailable'] is True and local['finite'] is True and [local['worldId'],local['roomId']]==[5,6] and local['hp']>0,'native local avatar unavailable')
        need(row['puppets'][0]['readAvailable'] is True and puppet['active']==1 and puppet['provenanceMatchesWorld'] is True and puppet['pose']['ownerSlot']==1-slot and [puppet['pose']['worldId'],puppet['pose']['roomId']]==[5,6],'paired current puppet unavailable')
        runtime=records(ra,'[combat-causal-runtime] ');need(runtime and all('loss' not in r for r in runtime),'runtime diagnostic missing/lost')
        need(all(r['sessionHex']==self.runtime_session_hex for r in runtime),'runtime original session replaced')
        need(all(number(r,'generation')==world['generation'] and number(r,'delivery')==world['deliverySerial'] and number(r,'slot')==slot and number(r,'self')==world['connectionIds'][slot] and number(r,'host')==world['connectionIds'][0] for r in runtime),'runtime current original world binding mismatch')
        if self.anchor is not None:
            need(anchor==self.anchor and world==self.world and {k:roots(v) for k,v in targets.items()}=={k:roots(v) for k,v in self.targets.items()},'immutable original scope/mapping replaced')
        else:self.anchor,self.world,self.targets=anchor,copy.deepcopy(world),copy.deepcopy(targets)
        self.last_log,self.last_runtime=after,ra
        return {'runId':self.run_id,'role':self.role,'qualifiedPopulation':True,'scope':dict(zip(SCOPE,anchor)),'targets':copy.deepcopy(targets),'sessionHex':self.session,'fullCombatQualified':False}

def paired(host,friend):
    need(host['runId']==friend['runId'] and host['role']=='host' and friend['role']=='friend' and host['sessionHex']==friend['sessionHex'],'paired run/role/session mismatch')
    h,f=host['scope'],friend['scope']
    need(all(h[k]==f[k] for k in ['host','peer','epoch','location']),'paired roster/epoch/tuple mismatch')
    def typed(row):return {int(k):(number(v,'objectId'),number(v,'type'),number(v,'maxHp')) for k,v in row['targets'].items()}
    need(typed(host)==typed(friend),'cross-peer typed net mapping mismatch')
    return True

def authorize(action_id,role,argv,acked_ids):
    """Additional argv/order gate; caller still must check current BOTH scopes."""
    index=len(acked_ids);need(index<len(ACTIONS),'no additional diagnostic input')
    need(acked_ids==[r[0] for r in ACTIONS[:index]],'action history replaced/out of order')
    need((action_id,role,tuple(argv))==ACTIONS[index],'unexpected diagnostic action/role/argv')
    return True

def classify(join_qualified,safety_closed,actions,logs,refusal=None,timings=None,setup_timing=None):
    result={'schema':'causal-rev2-diagnostic-terminal-v1','acceptance':False,'causalGraphQualified':False,'fullCombat':'BLOCKED','actions':list(actions),'reason':refusal,'independentGraphReplayRequired':True}
    need(set(logs)=={'hostInject','friendInject','hostRuntime','friendRuntime'},'complete raw-log roles required')
    result['logs']={k:{'sha256':hashlib.sha256(v.encode('utf-8')).hexdigest(),'bytes':len(v.encode('utf-8'))} for k,v in logs.items()}
    if safety_closed is not True:result['terminal']='FAIL_SAFETY';return result
    if join_qualified is not True:result['terminal']='FAIL_JOIN';return result
    result['terminal']='UNQUALIFIED'
    if actions:
        try:
            need(timings is not None and len(timings)==len(actions),'per-dispatch raw timing missing')
            recomputed=[]
            for index,row in enumerate(timings):
                need(row['actionId']==actions[index],'timing action history differs')
                recomputed.append(dispatch_timing(row['actionId'],row['role'],row['sentNs'],row['receivedNs'],row['dispatchNs'],row['readStartedNs'],row['readFinishedNs']))
            result['dispatchTimings']=recomputed
            need(setup_timing is not None and setup_timing['actionId']=='host_setup_warp' and setup_timing['role']=='host','original setup-warp timing missing')
            r=setup_timing
            result['setupTiming']=dispatch_timing(r['actionId'],r['role'],r['sentNs'],r['receivedNs'],r['dispatchNs'],r['readStartedNs'],r['readFinishedNs'])
            result['timingQualified']=result['setupTiming']['qualifiedTiming'] and all(row['qualifiedTiming'] for row in recomputed)
        except (Unqualified,KeyError,ValueError,TypeError) as exc:
            result['timingQualified']=False;result['timingReason']=str(exc)
        if not result['timingQualified']:
            result['terminal']='CAUSAL-EVIDENCE-UNQUALIFIED-TIMING'
            result['reason']=refusal or result.get('timingReason') or 'one or more dispatches exceeded250ms; raw ledger retained for auditor'
            return result
    if refusal:return result
    if actions!=[r[0] for r in ACTIONS]:result['reason']='compatibility only or incomplete diagnostic actions';return result
    for key in ['hostInject','friendInject']:
        try:summary(logs[key])
        except (Unqualified,KeyError,ValueError):result['reason']='observer summary unavailable/retired/lost';return result
        if any(number(r,'kind')==11 or number(r,'loss') for r in receipt_rows(logs[key])):
            result['reason']='observer retirement/loss receipt';return result
    required={'hostInject':{0,1,3,4},'friendInject':{0,1,2,5,6}}
    result['candidateKinds']={k:sorted({number(r,'kind') for r in receipt_rows(logs[k]) if number(r,'qualified')==1 and number(r,'reason')==0 and number(r,'loss')==0}) for k in required}
    if any(not kinds.issubset(set(result['candidateKinds'][k])) for k,kinds in required.items()) or any(not records(logs[k],'[combat-causal-runtime] ') for k in ['hostRuntime','friendRuntime']):
        result['reason']='missing raw host-hit/friend-claim/application/normal HP candidate nodes';return result
    result['terminal']='PENDING_DIAGNOSTIC_REPLAY';result['reason']='candidate records only; complete independent raw native/wire/association replay required'
    return result
