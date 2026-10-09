"""Synthetic native-cause controls only. Never claims a live HB arrival.

The ordinary-host-room line layout is copied from retained TT worldmap03
199604.log queue/issue/load/arrival seq3..6, generation2/delivery1/hostSource2.
Targets below are explicitly relabeled HB synthetic data, not live evidence.
"""
import copy,hashlib
import native_sync_proof as proof

def synthetic():
 target=[4,0,0,0,0,1];session='5ce362a89df37877466d59e7330c2009';prefix=b'baseline\n'
 host=prefix+b'Warp: 02/02 -> 04/00 door 0 map FFFF btl FFFF evt 0001 (waited 0 frames)\n[warp] load complete serial=3 transition=2 room=04/00 door=0 map=0 btl=0 evt=1\n[enemysync] host arrived epoch=2 room=04/00 door=0 map=0 btl=0 evt=1\n'
 lines=[]
 for n,event in enumerate(('queue','issue','load','arrival')):
  lines.append(f'[load-cause] seq={3+n} complete=1 event={event} cause=3 origin=ordinary_host_room available=1 generation=2 delivery=1 hostSource=2 session={session} host=1 request=0 target=2 targetDelivery=1 phase=255 cut=0 snapshot=- loadBefore=2 load={2 if n<2 else 3} transition={1 if n==0 else 2} issueTransition={0 if n==0 else 2} epoch=2 targetRoom=04/00 targetDoor=0 targetMap=0 targetBtl=0 targetEvt=1 room=04/00 door=0 map=0 btl=0 evt=1')
  if n==0:lines.append('[progresssync] apply version=1 spans=0 bytes=0 hash=2CCAEEE7 personal_before=124AA108 personal_after=124AA108 personal_unchanged=1')
  if n==2:lines.append('[warp] load complete serial=3 transition=2 room=04/00 door=0 map=0 btl=0 evt=1')
 lines.append('[enemysync] client arrived epoch=2 room=04/00 door=0 map=0 btl=0 evt=1')
 friend=prefix+('\n'.join(lines)+'\n').encode()
 identity=dict(session=session,host=1,friend=2,generation=2,delivery=1,hostSourceFloor=1,targetDelivery=1,peerDeliverySerials=[1,1,0],owned={i:dict(pid=100+i,creationTicks=200+i,moduleBase='0x140000000') for i in (0,1)})
 current={}
 for i in (0,1):
  flags=dict(eventState=0,eventContext=0,frozen=0,inField=1,openMenu=255);scope=dict(loadSerial=3,transitionSerial=2,epoch=2,location=target)
  header=dict(generation=2,deliverySerial=1,authorityMode=2,localSlot=i,connectionIds=[1,2,0],peerDeliverySerials=[1,1,0])
  current[i]=dict(instance=i,**identity['owned'][i],coherent=True,startedNs=11,finishedNs=12,location=target,locationAfter=target,flags=flags,flagsAfter=copy.deepcopy(flags),nativeScope=scope,scopeAfter=copy.deepcopy(scope),worldBefore=header,worldAfter=copy.deepcopy(header),rawReads=[dict(pid=100+i,creationTicks=200+i,moduleBase='0x140000000',native=dict(location=target,event=0,context=0,frozen=0,inField=1,menu=255)) for _ in (0,1)])
 runtimes={};baselines={}
 for i,label in enumerate(('host','friend')):
  r=dict(schema=1,identityCurrent=1,stringsComplete=1,attachedPid=100+i,slot=i,worldSlot=i,generationValid=1,authority=2,bridgeOpen=1,pinPresent=1,admitted=1,transportConnected=1,quarantine=0,errors=0,session=session,pinSession=session,pinHostConnection=1,pinSlot=i,hostConnection=1,selfConnection=1+i,roster0=1,roster1=2,roster2=0,worldRoster0=1,worldRoster1=2,worldRoster2=0,generation=2,delivery=1,peerFloor0=1,peerFloor1=1,peerFloor2=0)
  runtimes[i]=('[runtime-identity] '+' '.join(f'{k}={v}' for k,v in r.items())+'\n').encode()
  baselines[label]=dict(offset=len(prefix),prefixSha256=hashlib.sha256(prefix).hexdigest(),load=2,transition=1,epoch=1,readFinishedNs=10,runtimeOffset=len(runtimes[i]),runtimePrefixSha256=hashlib.sha256(runtimes[i]).hexdigest())
 return dict(hostLog=host,friendLog=friend,request=[4,0,0,65535,65535,1],baselines=baselines,runtimeLogs=runtimes,identity=identity,current=current,synthetic=True)

def run():
 rows=[]
 def check(name,ok):assert ok,name;rows.append(dict(name=name,status='PASS',scope='SYNTHETIC relabeled retained TT line shape, no live HB proof'))
 good=synthetic();result=proof.verify(good);check('synthetic HB ordinary causal chain validates PENDING only',result['pairedNativeArrivalQualified'] and result['acceptance'] is False and result['hostSource']==2)
 chain=copy.deepcopy(good);target=[4,10,50,0,0,0]
 chain['hostLog']=chain['hostLog'].replace(b'[enemysync] host arrived epoch=2 room=04/00 door=0 map=0 btl=0 evt=1',b'[warp] load complete serial=4 transition=3 room=04/0A door=50 map=0 btl=0 evt=0\n[enemysync] host arrived epoch=2 room=04/0A door=50 map=0 btl=0 evt=0')
 chain['friendLog']=chain['friendLog'].replace(b'Room=04/00 targetDoor=0 targetMap=0 targetBtl=0 targetEvt=1',b'Room=04/0A targetDoor=50 targetMap=0 targetBtl=0 targetEvt=0').replace(b'room=04/00 door=0 map=0 btl=0 evt=1',b'room=04/0A door=50 map=0 btl=0 evt=0')
 for i in (0,1):
  row=chain['current'][i];row['location']=list(target);row['locationAfter']=list(target)
  for key in ('nativeScope','scopeAfter'):
   row[key]['location']=list(target)
   if i==0:row[key].update(loadSerial=4,transitionSerial=3)
  for read in row['rawReads']:read['native']['location']=list(target)
 result=proof.verify(chain);check('synthetic authored chain retains first HB00 and later actual paired HB10 without HB00 pairing claim',result['location']==target and len(result['hostLoads'])==2)
 mutations={
  'post-admission-host-reset':lambda e:e.update(hostLog=e['hostLog'].replace(b'[enemysync] host arrived',b'[enemysync] session reset: host epoch and pending target cleared\n[enemysync] host arrived')),
  'post-admission-friend-reset':lambda e:e.update(friendLog=e['friendLog']+b'[enemysync] session reset: host epoch and pending target cleared\n'),
  'review3-friend-epoch-equal':lambda e:e['baselines']['friend'].update(epoch=2),
  'review3-friend-epoch-regressed':lambda e:e['baselines']['friend'].update(epoch=3),
  'baseline-epochs-differ':lambda e:e['baselines']['host'].update(epoch=2),
  'baseline-epoch-bool':lambda e:e['baselines']['friend'].update(epoch=True),
  'review3-both-personal-hashes-missing':lambda e:e.update(friendLog=e['friendLog'].replace(b' personal_before=124AA108 personal_after=124AA108',b'')),
  'personal-before-only':lambda e:e.update(friendLog=e['friendLog'].replace(b' personal_after=124AA108',b'')),
  'personal-after-only':lambda e:e.update(friendLog=e['friendLog'].replace(b' personal_before=124AA108',b'')),
  'personal-both-short':lambda e:e.update(friendLog=e['friendLog'].replace(b'124AA108',b'124')),
  'personal-both-long':lambda e:e.update(friendLog=e['friendLog'].replace(b'124AA108',b'124AA1080')),
  'personal-both-nonhex':lambda e:e.update(friendLog=e['friendLog'].replace(b'124AA108',b'124AA10Z')),
  'runtime-session-replaced':lambda e:e['runtimeLogs'].update({0:e['runtimeLogs'][0]+e['runtimeLogs'][0].replace(e['identity']['session'].encode(),b'a'*32)}),
  'runtime-session-restored-after-replacement':lambda e:e['runtimeLogs'].update({1:e['runtimeLogs'][1]+e['runtimeLogs'][1].replace(e['identity']['session'].encode(),b'a'*32)+e['runtimeLogs'][1]}),
  'runtime-prefix-replaced':lambda e:e['runtimeLogs'].update({0:e['runtimeLogs'][0].replace(e['identity']['session'].encode(),b'a'*32)}),
  'foreign-session':lambda e:e.update(friendLog=e['friendLog'].replace(b'session=5ce',b'session=ace')),
  'foreign-host':lambda e:e.update(friendLog=e['friendLog'].replace(b'host=1 ',b'host=9 ')),
  'foreign-target':lambda e:e.update(friendLog=e['friendLog'].replace(b'target=2 ',b'target=9 ')),
  'stale-source':lambda e:e['identity'].update(hostSourceFloor=2),
  'generation':lambda e:e.update(friendLog=e['friendLog'].replace(b'generation=2',b'generation=3')),
  'delivery':lambda e:e.update(friendLog=e['friendLog'].replace(b'delivery=1',b'delivery=2')),
  'targetDelivery':lambda e:e.update(friendLog=e['friendLog'].replace(b'targetDelivery=1',b'targetDelivery=2')),
  'cause-replacement':lambda e:e.update(friendLog=e['friendLog'].replace(b'event=issue cause=3',b'event=issue cause=8')),
  'resync':lambda e:e.update(friendLog=e['friendLog'].replace(b'origin=ordinary_host_room',b'origin=resync_bootstrap')),
  'missing-progress':lambda e:e.update(friendLog=b'\n'.join(x for x in e['friendLog'].split(b'\n') if not x.startswith(b'[progresssync]'))),
  'personal-change':lambda e:e.update(friendLog=e['friendLog'].replace(b'personal_after=124AA108',b'personal_after=124AA109')),
  'unattributed':lambda e:e.update(friendLog=e['friendLog'].replace(b'available=1',b'available=0')),
  'load-gap':lambda e:e.update(friendLog=e['friendLog'].replace(b'load=3',b'load=4')),
  'cause-gap':lambda e:e.update(friendLog=e['friendLog'].replace(b'seq=5 ',b'seq=9 ')),
  'missing-arrival':lambda e:e.update(friendLog=b'\n'.join(x for x in e['friendLog'].split(b'\n') if not x.startswith(b'[enemysync]'))),
  'friend-warp':lambda e:e.update(friendLog=e['friendLog']+b'Warp: 02/02 -> 04/00 door 0 map FFFF btl FFFF evt 0001\n'),
  'suffix-truncated':lambda e:e.update(friendLog=e['friendLog'][:-1]),
  'prefix-replaced':lambda e:e.update(friendLog=b'X'+e['friendLog'][1:]),
  'unsafe-after':lambda e:e['current'][1]['flagsAfter'].update(openMenu=7),
  'unsafe-both':lambda e:(e['current'][1]['flags'].update(openMenu=7),e['current'][1]['flagsAfter'].update(openMenu=7)),
  'scope-after':lambda e:e['current'][1]['scopeAfter'].update(loadSerial=99),
  'stale-read-floor':lambda e:e['current'][1].update(startedNs=10),
  'PID':lambda e:e['current'][1].update(pid=999),
  'creation':lambda e:e['current'][1].update(creationTicks=999),
  'module-bookend':lambda e:e['current'][1]['rawReads'][1].update(moduleBase='0x150000000'),
  'malformed-rawReads':lambda e:e['current'][1]['rawReads'][1].pop('native'),
  'rawReads-unsafe':lambda e:e['current'][1]['rawReads'][1]['native'].update(menu=7),
  'rawReads-creation':lambda e:e['current'][1]['rawReads'][1].update(creationTicks=999),
  'first-host-map-default':lambda e:e.update(hostLog=e['hostLog'].replace(b'map=0 btl=0 evt=1',b'map=1 btl=0 evt=1')),
  'header-change':lambda e:e['current'][1]['worldAfter'].update(generation=3),
  'header-foreign':lambda e:(e['current'][1]['worldBefore'].update(connectionIds=[1,9,0]),e['current'][1]['worldAfter'].update(connectionIds=[1,9,0])),
  'host-stale-load':lambda e:e.update(hostLog=e['hostLog'].replace(b'serial=3',b'serial=2')),
  'host-wrong-request':lambda e:e.update(hostLog=e['hostLog'].replace(b'evt 0001',b'evt 0000')),
  'host-no-arrival':lambda e:e.update(hostLog=b'\n'.join(x for x in e['hostLog'].split(b'\n') if not x.startswith(b'[enemysync]')))
 }
 for name,mutate in mutations.items():
  e=copy.deepcopy(good);mutate(e)
  try:proof.verify(e)
  except (proof.Refused,KeyError,ValueError):check('synthetic causal proof refuses '+name,True)
  else:raise AssertionError(name+' unexpectedly qualified')
 return rows

if __name__=='__main__':
 import json
 print(json.dumps(dict(status='PASS',liveExecuted=False,controls=run()),indent=2))
