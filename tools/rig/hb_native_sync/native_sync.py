"""One host request; passive observation of the product-owned follower path."""
import ctypes,hashlib,json,re,threading,time
import native_read,pause_scope,servicing
from native_log_format import LOAD,ARRIVAL,LIFECYCLE,RESET,_location,_identities

START=[2,2,0,4,0,0]
REQUEST=dict(instance=0,world=4,room=0,door=0,map=65535,btl=65535,evt=1)
STOP='native sync paired fresh HB arrival retained PENDING; no successor input'
SCHEMA='tt-hb-native-sync-v1'

def complete(data):
    # A writer may be halfway through its last line. Preserve only its complete prefix.
    return data[:data.rfind(b'\n')+1]

def scope(data,role):
    text=data.decode('utf-8');loads=list(LOAD.finditer(text));arrivals=list(ARRIVAL.finditer(text));edges=list(LIFECYCLE.finditer(text))
    if not loads or not arrivals or not edges:raise ValueError('completed native scope missing')
    l,a=loads[-1],arrivals[-1]
    if a.start()<l.end() or a['role']!=role or _location(l)!=_location(a):
        raise ValueError('native scope not a completed arrival')
    tail=[e for e in edges if e.start()>l.start()]
    # Runtime bootstrap can reset the HOST epoch after its offline setup load.
    # Admit only the observed first TT02 host epoch1, with resets before arrival.
    # Pin the complete lifecycle digest so no later reset can reanchor admission.
    if tail and not (role=='host' and _location(l)==START and int(a['epoch'])==1
        and len([x for x in arrivals if x['role']=='host'])==1
        and all(RESET.match(text,e.start()) and e.end()<a.start() for e in tail)):
        raise ValueError('native scope not a completed arrival')
    values=[int(l['serial']),int(l['transition']),int(a['epoch'])]
    if min(values)<=0:raise ValueError('native completed scope identity missing')
    lifecycle=[line for line in text.splitlines() if LIFECYCLE.search(line) or ARRIVAL.search(line)]
    return dict(loadSerial=values[0],transitionSerial=values[1],epoch=values[2],location=_location(l),bootstrapResetCount=len(tail),lifecycleSha256=hashlib.sha256(('\n'.join(lifecycle)+'\n').encode('utf-8')).hexdigest())

def flags(n):return dict(eventState=n['event'],eventContext=n['context'],frozen=n['frozen'],inField=n['inField'],openMenu=n['menu'],pauseBlockers=n['pauseBlockers'])

def raw_native(reader):
    out=pause_scope.native(reader)
    out['pauseBlockers']=reader.value(0xABB878,'I')
    return out

def require_safe_start(row):
    expected=dict(eventState=0,eventContext=0,frozen=0,inField=1,openMenu=255,pauseBlockers=0)
    if row['location']!=START or any(type(row['flags'].get(k)) is not int or row['flags'][k]!=v for k,v in expected.items()):
        raise ValueError('explicit current full safe START required')

def require_independent_start(gate,pid,base):
    reply=gate['raw'];module=reply.get('moduleBase');module=int(module,0) if isinstance(module,str) else module
    if gate.get('safe') is not True or gate.get('location')!=START or reply.get('ok') is not True or reply.get('processId')!=pid or module!=base:
        raise ValueError('fresh independent safe START identity required')
    raw=reply['samples'][0]
    expected={r:v for r,v in zip((0x717008,0x717009,0x71700A,0x71700C,0x71700E,0x717010),START)}
    expected.update({0xB65210:0,0x2A11478:0,0x2A171E8:0,0x9BA8D0:1,0x7435D0:255,0xABB878:0})
    for rva,wanted in expected.items():
        value=raw.get(f'0x{rva:X}')
        if rva==0x2A11478 and isinstance(value,str):value=int(value,0)
        if type(value) is not int or value!=wanted:raise ValueError('fresh independent safe START fields required')

def world(h):
    return dict(generation=h['generation'],deliverySerial=h['delivery'],authorityMode=h['authority'],localSlot=h['slot'],connectionIds=h['roster'],peerDeliverySerials=h['peerDelivery'],writerPid=h['writerPid'])

def incarnation(reader):
    from ctypes import wintypes as W
    times=[W.FILETIME() for _ in range(4)]
    reader.k.GetProcessTimes.argtypes=[W.HANDLE,*([ctypes.POINTER(W.FILETIME)]*4)]
    reader.k.GetProcessTimes.restype=W.BOOL
    if not reader.k.GetProcessTimes(reader.handle,*[ctypes.byref(t) for t in times]):raise OSError('owned process creation read failed')
    return (times[0].dwHighDateTime<<32)|times[0].dwLowDateTime

def identity(raw,i,pid):
    rows=_identities(raw.decode('utf-8'));r=rows[-1] if rows else {}
    for k,v in dict(schema=1,identityCurrent=1,stringsComplete=1,attachedPid=pid,slot=i,worldSlot=i,generationValid=1,authority=2,bridgeOpen=1,pinPresent=1,admitted=1,transportConnected=1,quarantine=0,errors=0).items():
        if r.get(k)!=v:raise ValueError('runtime admission '+k)
    roster=[r.get('roster'+str(j)) for j in range(3)]
    if not all(type(x)is int and x>0 for x in roster[:2]) or roster[2]!=0:raise ValueError('exact two-member roster required')
    if roster!=[r.get('worldRoster'+str(j)) for j in range(3)] or r['selfConnection']!=roster[i] or r['hostConnection']!=roster[0]:raise ValueError('runtime roster identity')
    if r['pinSession']!=r['session'] or r['pinHostConnection']!=roster[0] or r['pinSlot']!=i or not re.fullmatch('[0-9a-f]{32}',r['session']):raise ValueError('original runtime pin')
    return r

def binding(h,r,i,proc):
    if any(h[k]!=v for k,v in dict(slot=i,writerPid=proc.pid,authority=2,generation=r['generation'],delivery=r['delivery'],roster=[r['roster0'],r['roster1'],0],peerDelivery=[r['peerFloor0'],r['peerFloor1'],r['peerFloor2']]).items()):raise ValueError('runtime/world binding mismatch')

class Service:
    """50ms read-only lease/Present sampling; room epochs may advance, session may not."""
    def __init__(self,ctx,maps,captures,anchors):
        self.ctx=ctx;self.maps=maps;self.captures=captures;self.anchors=anchors;self.stop_event=threading.Event();self.failure=None;self.latest=0;self.thread=None
    def run(self):
        trackers=[servicing.PresentTracker(),servicing.PresentTracker()];last=time.monotonic()
        try:
            with (self.ctx.run_dir/'native_sync_service.jsonl').open('x',encoding='utf-8') as f:
                while not self.stop_event.is_set():
                    now=time.monotonic();row=dict(monotonic=now,gapMs=(now-last)*1000,peers=[])
                    if row['gapMs']>250:raise ValueError('service observation gap >250ms')
                    for i,m in enumerate(self.maps):
                        proc=next(p for n,p,_ in self.ctx.processes if n==f'runtime_{i}')
                        if proc.poll() is not None:raise ValueError('owned runtime exited')
                        h=servicing.header(m.read());anchor=self.anchors[i]
                        if any(h[k]!=anchor[k] for k in h if k!='heartbeat'):raise ValueError('original world binding replaced')
                        if ((ctypes.windll.kernel32.GetTickCount()-h['heartbeat'])&0xffffffff)>5000:raise ValueError('runtime lease expired')
                        count,age=trackers[i].sample(self.captures[i].read(),now)
                        row['peers'].append(dict(header=h,presentCount=count,presentAge=age))
                    f.write(json.dumps(row)+'\n');f.flush();self.latest=time.monotonic();last=now;self.stop_event.wait(.05)
        except Exception as e:self.failure=str(e)
    def require(self):
        if self.failure or time.monotonic()-self.latest>.25:raise ValueError('native sync service: '+str(self.failure or 'sample stale'))
    def start(self):
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start();end=time.monotonic()+2
        while not self.latest and not self.failure and time.monotonic()<end:time.sleep(.01)
        self.require()
    def close(self):
        self.stop_event.set()
        if self.thread:self.thread.join(2)
        if self.thread and self.thread.is_alive():raise ValueError('service worker did not close')

def observe(runner,ctx):
    import memory_start,native_sync_proof
    result=dict(schema=SCHEMA,status='FAIL',acceptance=False,pairedFreshHBArrival=False,hostWarpCount=0,friendWarpCount=0,noGameplayInput=True,held=False,scope='synthetic host transition/follower sync; map UI unfinished')
    readers=[];maps=[];caps=[];service=None;start=time.monotonic();trace=[];proof_input=None
    def runtime_logs():return {i:complete((ctx.run_dir/f'runtime_{i}.log').read_bytes()) for i in (0,1)}
    def retain():
        for name,obj in [('native_sync_result',result),('native_sync_trace',trace)]:
            (ctx.run_dir/(name+'.json')).write_text(json.dumps(obj,indent=2)+'\n',encoding='utf-8')
            if name+'.json' not in ctx.artifacts:ctx.artifacts.append(name+'.json')
    def read(i):
        t=time.perf_counter_ns();r=readers[i];receipt=dict(instance=i,pid=r.pid,moduleBase=r.base,startedNs=t)
        try:
            creation=incarnation(r);receipt['creationBefore']=creation
            h=world(servicing.header(maps[i].read()));receipt['worldBefore']=h
            before=raw_native(r);receipt['nativeBefore']=before
            log=complete(ctx.inst(i).inject_log.read_bytes());receipt['injectBeforeHex']=log.hex()
            after=raw_native(r);receipt['nativeAfter']=after
            endlog=complete(ctx.inst(i).inject_log.read_bytes());receipt['injectAfterHex']=endlog.hex()
            h2=world(servicing.header(maps[i].read()));receipt['worldAfter']=h2
            creation2=incarnation(r);receipt['creationAfter']=creation2
            # Parse only after retaining BOTH raw bookends, including failures.
            s=scope(log,'host' if i==0 else 'client');s2=scope(endlog,'host' if i==0 else 'client')
            if before!=after or s!=s2 or s['location']!=before['location']:raise ValueError('native transition/bookend not yet stable')
            if h!=h2 or creation!=creation2:raise ValueError('world/process bracket changed')
            raw=[dict(pid=r.pid,moduleBase=r.base,creationTicks=c,native=n) for c,n in ((creation,before),(creation2,after))]
            return dict(instance=i,pid=r.pid,creationTicks=creation,moduleBase=r.base,location=before['location'],locationAfter=after['location'],flags=flags(before),flagsAfter=flags(after),nativeScope=s,scopeAfter=s2,coherent=True,rawReads=raw,worldBefore=h,worldAfter=h2,startedNs=t,finishedNs=time.perf_counter_ns()),endlog
        except (ValueError,OSError) as error:
            receipt.update(error=str(error),finishedNs=time.perf_counter_ns())
            failures=result.setdefault('readFailures',[])
            if len(failures)<32:failures.append(receipt)
            else:result['omittedReadFailureDetails']=result.get('omittedReadFailureDetails',0)+1
            raise
    def admit_start(i,anchor=None):
        before,_=read(i)
        gate=memory_start.safe(runner,ctx,i)
        after,log=read(i)
        check=dict(instance=i,phase='initial' if anchor is None else 'pre-warp',before=before,independentSafe=gate,after=after,qualified=False)
        result.setdefault('startAdmissionChecks',[]).append(check)
        require_safe_start(before);require_independent_start(gate,readers[i].pid,readers[i].base);require_safe_start(after)
        if before['nativeScope']!=after['nativeScope']:raise ValueError('START scope replaced across independent safe read')
        if anchor is not None and any(before[k]!=anchor[k] or after[k]!=anchor[k] for k in ('nativeScope','pid','creationTicks','moduleBase')):
            raise ValueError('START original admission replaced; no reanchor')
        check['qualified']=True
        return after,log
    try:
        if not callable(getattr(runner,'pc2_require_disk_inventory',None)):raise runner.StepFailed('native sync requires PC2 adapter')
        gates=[memory_start.safe(runner,ctx,i) for i in (0,1)]
        if not all(g['safe'] and g['location']==START for g in gates):raise runner.StepFailed('unpaused safe paired TT START required')
        for i,g in enumerate(gates):
            base=g['raw']['moduleBase'];base=int(base,0) if isinstance(base,str) else base
            readers.append(native_read.Reader(ctx.inst(i).pid,base));maps.append(native_read.Mapping(ctx.inst(i).pid));caps.append(native_read.Mapping(ctx.inst(i).pid,'capture',56))
        runtime_initial=runtime_logs()
        ids=[identity(runtime_initial[i],i,ctx.inst(i).pid) for i in (0,1)]
        if ids[0]['session']!=ids[1]['session']:raise ValueError('cross-peer session mismatch')
        headers=[servicing.header(m.read()) for m in maps]
        for i in (0,1):binding(headers[i],ids[i],i,next(p for n,p,_ in ctx.processes if n==f'runtime_{i}'))
        initial=[admit_start(i) for i in (0,1)]
        # Read the WHOLE default program words, including unedited high bytes.
        defaults=[r.read(r.base+memory_start.SAVE+0x610,6).hex() for r in readers]
        result['hb00DefaultWords']=defaults
        if defaults!=['000000000100']*2:raise ValueError('HB00 live defaults not qualified')
        baselines={name:dict(offset=len(log),prefixSha256=hashlib.sha256(log).hexdigest(),load=row['nativeScope']['loadSerial'],transition=row['nativeScope']['transitionSerial'],epoch=row['nativeScope']['epoch'],readFinishedNs=row['finishedNs']) for name,(row,log) in zip(('host','friend'),initial)}
        if baselines['host']['epoch']!=baselines['friend']['epoch']:raise ValueError('paired START baseline epochs differ')
        for i,name in enumerate(('host','friend')):
            baselines[name].update(runtimeOffset=len(runtime_initial[i]),runtimePrefixSha256=hashlib.sha256(runtime_initial[i]).hexdigest())
        causes=re.findall(rb'\[load-cause\][^\n]*hostSource=(\d+)',initial[1][1]);floor=int(causes[-1]) if causes else 0
        ident=dict(session=ids[0]['session'],host=ids[0]['hostConnection'],friend=ids[1]['selfConnection'],generation=ids[1]['generation'],delivery=ids[1]['delivery'],targetDelivery=ids[1]['delivery'],hostSourceFloor=floor,peerDeliverySerials=headers[1]['peerDelivery'],owned={i:{k:initial[i][0][k] for k in ('pid','creationTicks','moduleBase')} for i in (0,1)})
        result.update(baselines=baselines,identity=ident,request=REQUEST,initial=[x[0] for x in initial])
        native_sync_proof.runtime_pin(dict(identity=ident,baselines=baselines,runtimeLogs=runtime_logs()))
        service=Service(ctx,maps,caps,headers);service.start();service.require()
        # Last current admission before the ONLY host warp. No friend pause/release.
        for i in (0,1):
            admit_start(i,initial[i][0])
        native_sync_proof.runtime_pin(dict(identity=ident,baselines=baselines,runtimeLogs=runtime_logs()))
        result['hostWarpCount']=1;result['requestStartedNs']=time.perf_counter_ns();retain()
        ack=runner.kh2ctl('warp','--world','4','--room','0','--door','0','--map','65535','--btl','65535','--evt','1',pid=ctx.inst(0).pid,timeout=60,check=False)
        result.update(requestReceipt=ack,requestFinishedNs=time.perf_counter_ns());retain()
        # The CLI's room-settle heuristic may miss HB00 if its authored event
        # redirects to HB10. This exact post-handover timeout grants observation
        # only; raw native causal proof is still mandatory for the terminal.
        timeout_after_handover=(ack.get('error')=='Timed out before the target room loaded' and ack.get('target')==dict(world=4,room=0,door=0,map=65535,btl=65535,evt=1) and ack.get('from')==dict(world=2,room=2) and ack.get('gateAtHandOver',{}).get('frozen')==0 and ack.get('gateAtHandOver',{}).get('inField')==1 and ack.get('gateAtHandOver',{}).get('openMenu')==255)
        if ack.get('processId')!=ctx.inst(0).pid or not (ack.get('ok') is True or timeout_after_handover):raise runner.StepFailed('owned host warp handover missing; no retry')
        while time.monotonic()-start<180:
            service.require();entry=dict(elapsed=time.monotonic()-start)
            try:
                pair=[read(i) for i in (0,1)];entry['current']=[p[0] for p in pair]
                fresh=pair[0][1][baselines['host']['offset']:].decode('utf-8')
                loads=list(LOAD.finditer(fresh))
                if not loads or _location(loads[0])!=[4,0,0,0,0,1]:raise ValueError('first completed host HB00 entry not qualified')
                result['firstHostHB00Load']=dict(line=loads[0].group(),loadSerial=int(loads[0]['serial']),transitionSerial=int(loads[0]['transition']))
                hs={i:dict(before=servicing.header(maps[i].read()),after=servicing.header(maps[i].read())) for i in (0,1)}
                for i in (0,1):
                    nowid=identity(complete((ctx.run_dir/f'runtime_{i}.log').read_bytes()),i,ctx.inst(i).pid)
                    if any(nowid[k]!=ids[i][k] for k in ('session','generation','delivery','hostConnection','selfConnection','roster0','roster1','roster2')):raise runner.StepFailed('runtime pin replaced')
                proof=native_sync_proof.verify(dict(request=[4,0,0,65535,65535,1],hostLog=pair[0][1],friendLog=pair[1][1],baselines=baselines,identity=ident,current={i:pair[i][0] for i in (0,1)},runtimeLogs=runtime_logs(),worldHeaders=hs))
                service.require()
                service.close()
                final=[read(i) for i in (0,1)]
                proof_input=dict(request=[4,0,0,65535,65535,1],hostLog=final[0][1],friendLog=final[1][1],baselines=baselines,identity=ident,current={i:final[i][0] for i in (0,1)},runtimeLogs=runtime_logs(),worldHeaders=hs)
                try:proof=native_sync_proof.verify(proof_input)
                except ValueError as error:raise runner.StepFailed('post-join native sync proof refused: '+str(error)) from error
                if service.failure:raise runner.StepFailed(service.failure)
                if time.monotonic()-start>=180:raise runner.StepFailed('native sync deadline after proof')
                result.update(status='PENDING',pairedFreshHBArrival=True,proof=proof,stopped=STOP);trace.append(entry);retain();raise runner.StepFailed(STOP)
            except ValueError as e:entry['unqualified']=str(e)
            trace.append(entry);retain();ctx.sleep(.1)
        raise runner.StepFailed('native sync arrival timeout; no fallback or further input')
    except Exception as e:
        result['stopped']=str(e);raise
    finally:
        if service:
            try:service.close()
            except Exception as error:
                result.update(pairedFreshHBArrival=False,cleanupFailure=str(error))
            finally:result['serviceFailure']=service.failure
            if 'native_sync_service.jsonl' not in ctx.artifacts:ctx.artifacts.append('native_sync_service.jsonl')
        for x in readers+maps+caps:
            try:x.close()
            except Exception as error:result.update(pairedFreshHBArrival=False,cleanupFailure=str(error))
        result['readHandlesClosed']='cleanupFailure' not in result
        for i in range(len(readers)):
            for kind,path in [('inject',ctx.inst(i).inject_log),('runtime',ctx.run_dir/f'runtime_{i}.log')]:
                name=f'native_sync_{kind}_{i}.log';(ctx.run_dir/name).write_bytes(complete(path.read_bytes()))
                if name not in ctx.artifacts:ctx.artifacts.append(name)
        # Verify the EXACT retained terminal prefixes after cleanup/worker join.
        # New runtime identity rows cannot contradict an earlier qualified proof.
        if result['pairedFreshHBArrival']:
            try:
                proof_input.update(hostLog=(ctx.run_dir/'native_sync_inject_0.log').read_bytes(),friendLog=(ctx.run_dir/'native_sync_inject_1.log').read_bytes(),runtimeLogs={i:(ctx.run_dir/f'native_sync_runtime_{i}.log').read_bytes() for i in (0,1)})
                result['proof']=native_sync_proof.verify(proof_input)
            except ValueError as error:result.update(pairedFreshHBArrival=False,cleanupFailure='retained terminal proof: '+str(error))
        result['elapsedSeconds']=time.monotonic()-start
        if result['pairedFreshHBArrival'] and result['elapsedSeconds']>=180:
            result.update(pairedFreshHBArrival=False,cleanupFailure='180s deadline after retained log processing')
        if 'cleanupFailure' in result:result.update(status='FAIL',stopped='native sync cleanup refused: '+result['cleanupFailure'])
        retain()
        if result['pairedFreshHBArrival'] and time.monotonic()-start>=180:
            result.update(status='FAIL',pairedFreshHBArrival=False,cleanupFailure='180s deadline after artifact serialization',stopped='native sync cleanup refused: 180s deadline after artifact serialization');retain()
        if 'cleanupFailure' in result:raise runner.StepFailed('native sync cleanup refused: '+result['cleanupFailure'])
