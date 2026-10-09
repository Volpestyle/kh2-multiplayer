"""Pure supplied-evidence causal proof. No I/O, native calls or binary equivalence.

verify(evidence): hostLog/friendLog bytes; baselines host/friend with offset,
prefixSha256,load,transition; identity session/host/friend/generation/delivery/
hostSourceFloor/targetDelivery, peerDeliverySerials and owned {0,1: pid,creationTicks,
moduleBase}; current {0,1: coherent native scalar rows with flagsAfter/rawReads,
nativeScope and worldBefore/worldAfter}; request six-field tuple.
Only bounded raw suffix evidence is accepted. Synthetic fixtures remain synthetic.
"""
import hashlib,re
from native_log_format import _identities,LIFECYCLE

class Refused(ValueError):pass

def need(ok,why):
 if not ok:raise Refused(why)

def integer(value):
 if isinstance(value,str):return int(value,0)
 need(type(value) is int,'integer required');return value

def suffix(data,base):
 need(type(data) is bytes,'raw log bytes required');offset=base['offset']
 need(type(offset) is int and 0<=offset<=len(data) and (offset==0 or data[offset-1:offset]==b'\n'),'baseline byte boundary invalid')
 need(hashlib.sha256(data[:offset]).hexdigest()==base['prefixSha256'],'baseline log prefix replaced')
 need(data.endswith(b'\n'),'incomplete final native log line')
 return [(offset+m.start(),m.group().decode('utf-8')) for m in re.finditer(rb'[^\n]+',data[offset:])]

LOAD=re.compile(r'\[warp\] load complete serial=(\d+) transition=(\d+) room=([0-9A-Fa-f]{2})/([0-9A-Fa-f]{2}) door=(\d+) map=(\d+) btl=(\d+) evt=(\d+)')
ARRIVE=re.compile(r'\[enemysync\] (host|client) arrived epoch=(\d+) room=([0-9A-Fa-f]{2})/([0-9A-Fa-f]{2}) door=(\d+) map=(\d+) btl=(\d+) evt=(\d+)')
WARP=re.compile(r'Warp: [0-9A-Fa-f]{2}/[0-9A-Fa-f]{2} -> ([0-9A-Fa-f]{2})/([0-9A-Fa-f]{2}) door (\d+) map ([0-9A-Fa-f]+) btl ([0-9A-Fa-f]+) evt ([0-9A-Fa-f]+)')

def location(match,start):
 return [int(match[start],16),int(match[start+1],16),*[int(match[j]) for j in range(start+2,start+6)]]

def events(lines,pattern):
 return [(offset,line,pattern.search(line)) for offset,line in lines if pattern.search(line)]

def kv(line):
 # Strip only the CR belonging to a native CRLF line. Raw log bytes/offsets and
 # authenticated prefix hashes are never normalized. Embedded CR/LF refuses.
 if line.endswith('\r'):line=line[:-1]
 need('\r' not in line and '\n' not in line,'embedded native token line ending')
 pairs=re.findall(r'(\w+)=([^ ]+)',line)
 need(len(pairs)==len(dict(pairs)),'duplicate native token')
 return dict(pairs)

def runtime_pin(e):
 receipts={}
 for i,label in enumerate(('host','friend')):
  data=e['runtimeLogs'][i];base=e['baselines'][label];offset=base['runtimeOffset']
  need(type(data) is bytes and data.endswith(b'\n'),'complete runtime prefix required')
  need(type(offset) is int and 0<offset<=len(data) and data[offset-1:offset]==b'\n','runtime baseline boundary')
  need(hashlib.sha256(data[:offset]).hexdigest()==base['runtimePrefixSha256'],'runtime admission prefix replaced')
  initial=_identities(data[:offset].decode('utf-8'));need(bool(initial),'runtime baseline identity absent')
  rows=initial[-1:]+_identities(data[offset:].decode('utf-8'));ident=e['identity']
  roster=[ident['host'],ident['friend'],0]
  expected=dict(schema=1,identityCurrent=1,stringsComplete=1,attachedPid=ident['owned'][i]['pid'],slot=i,worldSlot=i,generationValid=1,authority=2,bridgeOpen=1,pinPresent=1,admitted=1,transportConnected=1,quarantine=0,errors=0,session=ident['session'],pinSession=ident['session'],pinHostConnection=ident['host'],pinSlot=i,hostConnection=ident['host'],selfConnection=roster[i],generation=ident['generation'],delivery=ident['delivery'])
  expected.update({f'{prefix}{j}':v for prefix in ('roster','worldRoster') for j,v in enumerate(roster)})
  expected.update({f'peerFloor{j}':v for j,v in enumerate(ident['peerDeliverySerials'])})
  for row in rows:need(all(row.get(k)==v for k,v in expected.items()),'original runtime admission replaced')
  receipts[i]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),identity=rows[-1])
 return receipts
def cause_location(row,target=False):
 prefix='target' if target else '';room=row['targetRoom' if target else 'room'].split('/')
 return [int(x,16) for x in room]+[int(row[k]) for k in (('targetDoor','targetMap','targetBtl','targetEvt') if target else ('door','map','btl','evt'))]

def baseline_retirement(e,causes):
 """Authenticate exactly one closing row of the completed ORIGINAL TT baseline.

 Never discard arbitrary historical rows or select a replacement HB cause.
 The complete raw prefix was authenticated by suffix() before this call.
 """
 if not causes or causes[0][2].get('event')!='superseded':return causes,None
 retired=causes[0];row=retired[2];base=e['baselines']['friend'];ident=e['identity'];start=[2,2,0,4,0,0]
 prefix=e['friendLog'][:base['offset']]
 lines=[(m.start(),m.group().decode('utf-8')) for m in re.finditer(rb'[^\n]+',prefix)]
 records=[(off,line,kv(line)) for off,line in lines if line.startswith('[load-cause] ')]
 old=records[-4:]
 need(len(old)==4 and [x[2].get('event') for x in old]==['queue','issue','load','arrival'],'completed baseline cause history required')
 first=old[0][2];lb=base['load'];tr=base['transition'];epoch=base['epoch']
 need(type(lb) is int and lb>0 and type(tr) is int and tr>0,'completed baseline load/transition required')
 need([int(x[2]['seq']) for x in old]==list(range(int(first['seq']),int(first['seq'])+4)),'baseline cause sequence gap')
 need(int(first['seq'])>0 and int(first['cause'])==int(first['seq']),'baseline cause identity missing')
 keys=('cause','generation','delivery','hostSource','session','host','target','targetDelivery','loadBefore','epoch')
 expected=dict(generation=ident['generation'],delivery=ident['delivery'],hostSource=ident['hostSourceFloor'],host=ident['host'],target=ident['friend'],targetDelivery=ident['targetDelivery'],epoch=epoch,loadBefore=lb-1)
 for n,(_,_,oldrow) in enumerate(old):
  need(all(oldrow.get(k)==first.get(k) for k in keys),'baseline cause binding replaced')
  need(oldrow.get('complete')=='1' and oldrow.get('available')=='1' and oldrow.get('origin')=='ordinary_host_room','baseline ordinary cause incomplete')
  need(oldrow.get('session')==ident['session'] and oldrow.get('request')=='0' and oldrow.get('phase')=='255' and oldrow.get('cut')=='0' and oldrow.get('snapshot')=='-','baseline cause foreign/resync binding')
  need(all(int(oldrow[k])==v for k,v in expected.items()) and int(oldrow['hostSource'])>0,'baseline cause does not match original admission')
  need(cause_location(oldrow,True)==start and cause_location(oldrow)==start,'baseline cause full START tuple mismatch')
  need(int(oldrow['load'])==(lb-1 if n<2 else lb) and int(oldrow['transition'])==(tr-1 if n==0 else tr) and int(oldrow['issueTransition'])==(0 if n==0 else tr),'baseline cause load/transition history mismatch')
 tail=[x for x in lines if x[0]>old[0][0]]
 loads=events(tail,LOAD);arrivals=events(tail,ARRIVE)
 need(len(loads)==1 and int(loads[0][2][1])==lb and int(loads[0][2][2])==tr and location(loads[0][2],3)==start,'baseline completed native load absent')
 need(old[2][0]<loads[0][0]<old[3][0],'baseline native load causal order')
 edges=events(tail,LIFECYCLE)
 need(bool(edges) and edges[-1][0]==loads[0][0] and not events(tail,WARP),'baseline native lifecycle not completed')
 need(len(arrivals)==1 and arrivals[0][2][1]=='client' and int(arrivals[0][2][2])==epoch and location(arrivals[0][2],3)==start and arrivals[0][0]>old[3][0],'baseline completed product arrival absent')
 need(not any('[enemysync] session reset:' in line or '[progresssync] apply failed' in line or 'personal_unchanged=0' in line for _,line in tail),'baseline completion failure/reset retained')
 applies=[(off,line,kv(line)) for off,line in tail if line.startswith('[progresssync] apply ') and old[0][0]<off<old[1][0]]
 need(len(applies)==1,'baseline shared progress apply absent')
 progress=applies[0][2]
 need(int(progress['version'])>0 and re.fullmatch('[0-9A-Fa-f]{8}',progress.get('hash','')) is not None and re.fullmatch('[0-9A-Fa-f]{8}',progress.get('personal_before','')) is not None and progress.get('personal_before')==progress.get('personal_after') and progress.get('personal_unchanged')=='1','baseline personal integrity incomplete')
 # Only event/sequence differ from the completed baseline arrival record.
 need({k:v for k,v in row.items() if k not in ('event','seq')}=={k:v for k,v in old[-1][2].items() if k not in ('event','seq')},'retirement does not close completed baseline cause')
 need(int(row['seq'])==int(old[-1][2]['seq'])+1,'baseline retirement sequence gap')
 need(len(causes)==5 and causes[1][2].get('event')=='queue' and int(causes[1][2]['seq'])==int(row['seq'])+1 and int(causes[1][2]['cause'])>int(row['cause']),'single baseline retirement must immediately precede new cause')
 return causes[1:],dict(row=retired[:2],baselineCause=[x[:2] for x in old],baselineNativeLoad=loads[0][:2],baselineArrival=arrivals[0][:2],baselineSharedProgressApply=applies[0][:2])

def _verify(e):
 identity=e['identity'];owned=identity['owned'];session=identity['session']
 need(isinstance(session,str) and re.fullmatch('[0-9a-f]{32}',session) is not None,'session identity invalid')
 for k in ('host','friend','generation','delivery','targetDelivery'):need(type(identity[k]) is int and identity[k]>0,'positive identity '+k+' required')
 need(type(identity['hostSourceFloor']) is int and identity['hostSourceFloor']>=0,'host source floor required')
 need(identity['host']!=identity['friend'] and identity['delivery']==identity['targetDelivery'],'admitted target delivery mismatch')
 for label in ('host','friend'):
  need(type(e['baselines'][label]['epoch']) is int and e['baselines'][label]['epoch']>0,'positive baseline epoch required')
 need(e['baselines']['host']['epoch']==e['baselines']['friend']['epoch'],'paired admitted baseline epochs differ')
 runtime=runtime_pin(e)
 host=suffix(e['hostLog'],e['baselines']['host']);friend=suffix(e['friendLog'],e['baselines']['friend'])
 need(not any('[enemysync] session reset:' in line for _,line in host+friend),'post-admission native session reset refused')
 warps=events(host,WARP);need(len(warps)==1,'exactly one fresh host Warp required')
 request=e['request'];need(type(request) is list and len(request)==6 and request[:3]==[4,0,0] and request[5]==1,'finite HB00 event1 request required')
 w=warps[0][2];need([int(w[1],16),int(w[2],16),int(w[3]),int(w[4],16),int(w[5],16),int(w[6],16)]==request,'host request mismatch')
 loads=events(host,LOAD);arrivals=events(host,ARRIVE)
 need(loads and arrivals and loads[0][0]>warps[0][0],'fresh host load/arrival absent')
 previous=e['baselines']['host'];priorload=previous['load'];priortransition=previous['transition']
 for offset,line,m in loads:
  need(int(m[1])==priorload+1 and int(m[2])==priortransition+1,'host load sequence gap/replacement')
  need(location(m,3)[0]==4,'host load left HB scope');priorload=int(m[1]);priortransition=int(m[2])
 firstloc=location(loads[0][2],3);need(firstloc==[4,0,0,0,0,1],'initial host exact resolved HB00event1 load absent')
 for j in (3,4):need(request[j]==65535 or firstloc[j]==request[j],'resolved host program mismatch')
 ah=[x for x in arrivals if x[2][1]=='host'];need(len(ah)==1,'exactly one fresh product host arrival required')
 ha=ah[0];hl=loads[-1];target=location(ha[2],3);epoch=int(ha[2][2])
 need(ha[0]>hl[0] and target==location(hl[2],3) and epoch>previous.get('epoch',0),'host arrived not final fresh load/epoch')
 need(epoch>e['baselines']['friend']['epoch'],'friend arrival epoch not fresh beyond own baseline')
 need(not events(friend,WARP),'friend direct Warp forbidden')
 causes=[(off,line,kv(line)) for off,line in friend if line.startswith('[load-cause] ')]
 causes,retirement=baseline_retirement(e,causes)
 need(len(causes)==4 and [r[2].get('event') for r in causes]==['queue','issue','load','arrival'],'complete sole ordinary cause sequence required')
 nums=('cause','generation','delivery','hostSource','host','target','targetDelivery','loadBefore','epoch')
 first=causes[0][2]
 for _,_,row in causes:
  need(row.get('complete')=='1' and row.get('available')=='1' and row.get('origin')=='ordinary_host_room','unattributed/resync/partial cause refused')
  need(row.get('session')==session and all(row.get(k)==first.get(k) for k in nums),'foreign or replaced cause binding')
  need(row.get('request')=='0' and row.get('phase')=='255' and row.get('cut')=='0' and row.get('snapshot')=='-','resync cause refused')
  expected=dict(generation=identity['generation'],delivery=identity['delivery'],host=identity['host'],target=identity['friend'],targetDelivery=identity['targetDelivery'],epoch=epoch)
  need(all(int(row[k])==value for k,value in expected.items()) and int(row['hostSource'])>identity['hostSourceFloor'] and int(row['cause'])>0,'admitted cause identity/epoch mismatch')
  need(cause_location(row,True)==target,'cause target tuple mismatch')
 need([int(r[2]['seq']) for r in causes]==list(range(int(first['seq']),int(first['seq'])+4)),'load cause sequence gap')
 baseline=e['baselines']['friend'];lb=baseline['load'];tr=baseline['transition'];need(int(first['loadBefore'])==lb,'stale friend load baseline')
 for n,(_,_,row) in enumerate(causes):
  need(int(row['load'])==(lb if n<2 else lb+1),'friend load must advance exactly once')
  need(int(row['transition'])==(tr if n==0 else tr+1),'friend transition must advance exactly once')
  need(int(row['issueTransition'])==(0 if n==0 else tr+1),'friend native issue transition mismatch')
  if n>=2:need(cause_location(row)==target,'friend loaded tuple mismatch')
 fl=events(friend,LOAD);fa=events(friend,ARRIVE)
 need(len(fl)==1 and int(fl[0][2][1])==lb+1 and int(fl[0][2][2])==tr+1 and location(fl[0][2],3)==target,'friend fresh completed native load absent')
 need(causes[2][0]<fl[0][0]<causes[3][0],'native load outside causal order')
 need(len(fa)==1 and fa[0][2][1]=='client' and int(fa[0][2][2])==epoch and location(fa[0][2],3)==target and fa[0][0]>causes[3][0],'friend matched product arrival absent')
 applies=[(off,line,kv(line)) for off,line in friend if line.startswith('[progresssync] apply ') and causes[0][0]<off<causes[1][0]]
 need(len(applies)==1,'one shared progress apply before issue required');progress=applies[0][2]
 for key in ('personal_before','personal_after'):
  need(isinstance(progress.get(key),str) and re.fullmatch('[0-9A-Fa-f]{8}',progress[key]) is not None,'complete personal integrity hash required: '+key)
 need(int(progress['version'])>0 and progress.get('personal_unchanged')=='1' and progress.get('personal_before')==progress.get('personal_after') and re.fullmatch('[0-9A-Fa-f]{8}',progress.get('hash','')) is not None,'shared apply/personal integrity refused')
 retired_offset=retirement['row'][0] if retirement is not None else None
 need(not any('[progresssync] apply failed' in line or 'personal_unchanged=0' in line or ('event=superseded' in line and off!=retired_offset) for off,line in friend),'progress/cause failure retained')
 for i,load,transition in ((0,priorload,priortransition),(1,lb+1,tr+1)):
  row=e['current'][i];owner=owned[i]
  need(row.get('pid')==owner['pid'] and row.get('instance')==i and row.get('creationTicks')==owner['creationTicks'] and integer(row.get('moduleBase'))==integer(owner['moduleBase']),'current owned process identity mismatch')
  need(row.get('coherent') is True and row['finishedNs']>=row['startedNs']>e['baselines']['host' if i==0 else 'friend']['readFinishedNs'],'current bracket/floor invalid')
  fields=('location','eventState','eventContext','frozen','inField','openMenu')
  flags=row['flags'];need(flags==row['flagsAfter'] and row['location']==row['locationAfter'],'current native after-read changed')
  need(row['location']==target and tuple(flags[k] for k in ('eventState','eventContext','frozen','inField','openMenu'))==(0,0,0,1,255),'current safe paired tuple absent')
  scope=row['nativeScope'];need(scope==row['scopeAfter'] and all(scope.get(k)==v for k,v in dict(loadSerial=load,transitionSerial=transition,epoch=epoch,location=target).items()),'current native scope replaced')
  need(row['worldBefore']==row['worldAfter'],'current world header changed');header=row['worldBefore']
  expected=dict(generation=identity['generation'],deliverySerial=identity['delivery'],authorityMode=2,localSlot=i,connectionIds=[identity['host'],identity['friend'],0],peerDeliverySerials=identity['peerDeliverySerials'])
  need(all(header.get(k)==v for k,v in expected.items()),'current world header identity mismatch')
  raw=row.get('rawReads',[]);need(len(raw)==2,'two raw native handle reads required')
  for read in raw:
   need(read.get('pid')==owner['pid'] and read.get('creationTicks')==owner['creationTicks'] and integer(read.get('moduleBase'))==integer(owner['moduleBase']),'native handle identity bookend replaced')
   native=read['native'];need(native.get('location')==target and tuple(native.get(k) for k in ('event','context','frozen','inField','menu'))==(0,0,0,1,255),'native raw state bookend unsafe/malformed')

 return dict(schema='hb-native-sync-causal-proof-v1',status='PENDING',acceptance=False,pairedNativeArrivalQualified=True,location=target,epoch=epoch,hostSource=int(first['hostSource']),identity=identity,runtimePrefixes=runtime,hostRequest=warps[0][:2],hostLoads=[x[:2] for x in loads],hostArrival=ha[:2],friendBaselineRetirement=retirement,friendCause=[x[:2] for x in causes],friendLoad=fl[0][:2],friendArrival=fa[0][:2],sharedProgressApply=applies[0][:2],current=e['current'],scope='supplied raw suffix causal verification only; native binary equivalence and natural map travel unqualified')


def verify(evidence):
 """Return detailed causal proof or Refused; never performs I/O/native work."""
 try:return _verify(evidence)
 except Refused:raise
 except (KeyError,TypeError,ValueError,IndexError,UnicodeError,OverflowError) as error:
  raise Refused('malformed native causal evidence: '+str(error)) from error
