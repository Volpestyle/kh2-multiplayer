"""Source6 host warp/follower acceptance candidate; media and closure gates."""
import copy,ctypes,hashlib,json,re,threading
from media_clock import clock as time
import native_read,pause_scope,servicing,native_sync
from native_log_format import LOAD,ARRIVAL,LIFECYCLE,RESET,_location,_identities
START=native_sync.START
REQUEST=native_sync.REQUEST
scope=native_sync.scope
flags=native_sync.flags
raw_native=native_sync.raw_native
require_safe_start=native_sync.require_safe_start
require_independent_start=native_sync.require_independent_start
world=native_sync.world
incarnation=native_sync.incarnation
identity=native_sync.identity
binding=native_sync.binding
complete=native_sync.complete
class Service(native_sync.Service):
    def run(self):
        try:super().run()
        finally:
            if self.failure and hasattr(self.ctx,'pc2_media_cancel'):self.ctx.pc2_media_cancel.set()

SCHEMA='tt-hb-host-warp-follow-acceptance-v1'
READY='host warp/product follower safe HB arrival media ready for independent closure'
ENDPOINT=[4,10,50,0,0,22]

def observe(runner,ctx):
    import memory_start,native_sync_proof,native_sync_media
    result=dict(schema=SCHEMA,status='FAIL',acceptance=False,pairedFreshHBArrival=False,hostWarpCount=0,friendWarpCount=0,noGameplayInput=True,held=False,scope='bounded host native warp/product follower safe HB arrival; natural travel unfinished',mediaQualified=False,handlerDeadlineMonotonicNs=int((time.monotonic()+180)*1e9))
    readers=[];maps=[];caps=[];service=None;start=time.monotonic();trace=[];proof_input=None
    result['handlerDeadlineMonotonicNs']=int((start+180)*1e9)
    result['handlerStartedNs']=int(start*1e9)
    ctx._story_total_deadline=start+180
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
        native_sync_media.require_hooks(runner,ctx)
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
        native_sync_media.commit(runner,ctx,ident['owned'],start+180)
        result['mediaTargets']=copy.deepcopy(ctx.pc2_media_targets)
        result['mediaProducts']=copy.deepcopy(ctx.pc2_media_products)
        service=Service(ctx,maps,caps,headers);service.start();service.require()
        # Last current admission before the ONLY host warp. No friend pause/release.
        for i in (0,1):
            admit_start(i,initial[i][0])
        native_sync_proof.runtime_pin(dict(identity=ident,baselines=baselines,runtimeLogs=runtime_logs()))
        native_sync_media.committed(ctx,start+180)
        result.update(hostWarpCount=1,requestPreparedNs=time.perf_counter_ns(),requestDispatched=False);retain()
        # Serialization can cross the deadline or observe cancellation. No I/O
        # or retention follows this final admission before the sole dispatch.
        try:
            native_sync_media.committed(ctx,start+180);service.require()
            remaining=start+180-time.monotonic()
            if remaining<=0 or ctx.pc2_media_cancel.is_set():raise runner.StepFailed('original deadline/cancellation before host warp dispatch')
        except Exception:
            result['hostWarpCount']=0;raise
        result.update(requestStartedNs=time.perf_counter_ns(),requestDispatched=True)
        ack=runner.kh2ctl('warp','--world','4','--room','0','--door','0','--map','65535','--btl','65535','--evt','1',pid=ctx.inst(0).pid,timeout=min(60,remaining),check=False)
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
                if proof['location']!=ENDPOINT:raise runner.StepFailed('unsupported safe HB endpoint; no acceptance media')
                anchor=copy.deepcopy(proof)
                result['arrivalProof']=anchor;result['pairedFreshHBArrival']=True
                proof_input=dict(request=[4,0,0,65535,65535,1],hostLog=pair[0][1],friendLog=pair[1][1],baselines=baselines,identity=ident,current={i:pair[i][0] for i in (0,1)},runtimeLogs=runtime_logs(),worldHeaders=hs)
                floors={i:pair[i][0]['finishedNs'] for i in (0,1)}
                def media_checkpoint(label):
                    nonlocal proof_input
                    service.require()
                    current_pair=[read(i) for i in (0,1)]
                    headers={i:dict(before=servicing.header(maps[i].read()),after=servicing.header(maps[i].read())) for i in (0,1)}
                    candidate=dict(request=[4,0,0,65535,65535,1],hostLog=current_pair[0][1],friendLog=current_pair[1][1],baselines=baselines,identity=ident,current={i:current_pair[i][0] for i in (0,1)},runtimeLogs=runtime_logs(),worldHeaders=headers)
                    checked=native_sync_proof.verify(candidate)
                    native_sync_media.require_original_endpoint(anchor,checked,floors)
                    service.require();proof_input=candidate
                    result.setdefault('mediaCheckpoints',[]).append(dict(label=label,proof=checked))
                    return checked
                result['mediaAttempted']=True;retain()
                result['media']=native_sync_media.observe(runner,ctx,anchor,media_checkpoint,service,start+180)
                result['mediaQualified']=True
                service.require()
                service.close()
                final=[read(i) for i in (0,1)]
                hs={i:dict(before=servicing.header(maps[i].read()),after=servicing.header(maps[i].read())) for i in (0,1)}
                proof_input=dict(request=[4,0,0,65535,65535,1],hostLog=final[0][1],friendLog=final[1][1],baselines=baselines,identity=ident,current={i:final[i][0] for i in (0,1)},runtimeLogs=runtime_logs(),worldHeaders=hs)
                try:proof=native_sync_proof.verify(proof_input)
                except ValueError as error:raise runner.StepFailed('post-join native sync proof refused: '+str(error)) from error
                native_sync_media.require_original_endpoint(anchor,proof,floors)
                if service.failure:raise runner.StepFailed(service.failure)
                if time.monotonic()-start>=180:raise runner.StepFailed('native sync deadline after proof')
                result.update(status='READY_FOR_CLOSURE',pairedFreshHBArrival=True,proof=proof,stopped=READY);trace.append(entry);retain();return result
            except ValueError as e:
                if result.get('mediaAttempted'):raise runner.StepFailed('acceptance media/proof refused; no retry: '+str(e)) from e
                entry['unqualified']=str(e)
            trace.append(entry);retain();ctx.sleep(.1)
        raise runner.StepFailed('native sync arrival timeout; no fallback or further input')
    except Exception as e:
        if hasattr(ctx,'pc2_media_cancel'):ctx.pc2_media_cancel.set()
        result.update(status='FAIL',stopped=str(e));raise
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
                if result.get('mediaQualified'):native_sync_media.require_original_endpoint(anchor,result['proof'],None)
            except ValueError as error:result.update(pairedFreshHBArrival=False,cleanupFailure='retained terminal proof: '+str(error))
        result['elapsedSeconds']=time.monotonic()-start
        if result['pairedFreshHBArrival'] and result['elapsedSeconds']>=180:
            result.update(pairedFreshHBArrival=False,cleanupFailure='180s deadline after retained log processing')
        if 'cleanupFailure' in result:result.update(status='FAIL',stopped='native sync cleanup refused: '+result['cleanupFailure'])
        if result.get('mediaQualified'):
            try:native_sync_media.finalize(runner,ctx,result,start+180)
            except Exception as error:result.update(mediaQualified=False,status='FAIL',cleanupFailure='final media: '+str(error),stopped='native sync cleanup refused: final media: '+str(error))
        ctx.saved['hb_host_warp_acceptance']=result
        retain()
        final_deadline=min(start+180,result.get('media',{}).get('validationDeadlineNs',int((start+180)*1e9))/1e9)
        if result['pairedFreshHBArrival'] and time.monotonic()>=final_deadline:
            result.update(status='FAIL',pairedFreshHBArrival=False,cleanupFailure='original media/total deadline after artifact serialization',stopped='native sync cleanup refused: original media/total deadline after artifact serialization');retain()
        if result['status']=='READY_FOR_CLOSURE':ctx._story_acceptance_terminal=native_sync_media.digest(native_sync_media.canonical(result).encode())
        elif hasattr(ctx,'pc2_media_cancel'):ctx.pc2_media_cancel.set()
        if result['status']=='READY_FOR_CLOSURE' and time.monotonic()>=final_deadline:
            result.update(status='FAIL',cleanupFailure='deadline after final immutable terminal processing',stopped='deadline after final immutable terminal processing');retain();ctx.pc2_media_cancel.set()
        if 'cleanupFailure' in result:raise runner.StepFailed('native sync cleanup refused: '+result['cleanupFailure'])
