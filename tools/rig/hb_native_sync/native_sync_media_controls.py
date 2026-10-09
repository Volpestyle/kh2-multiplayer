"""Real source media/handler paths with only OS/game/product boundaries mocked."""
import copy,hashlib,json,os,struct
from media_clock import clock as time
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace as NS
from unittest.mock import patch
import native_sync_media as media,native_sync_acceptance as handler

def put(ctx,name,data):
    p=ctx.run_dir/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
    return dict(path=name,bytes=len(data),sha256=media.digest(data))

def make_context(root):
    products={}
    for role in ('clipCli','encoder','verifier','metrics'):
        p=root/(role+'.test');p.write_bytes(role.encode());products[role]=dict(packagePath=str(p.resolve()),sha256=media.digest(role.encode()))
    ctx=NS(run_dir=root,saved={},artifacts=[],pc2_media_products=products)
    r=NS(pc2_media_products=products,pc2_owned_clip=lambda *a:None)
    media.commit(r,ctx,{i:dict(pid=100+i,creationTicks=200+i,moduleBase=0x140000000)for i in (0,1)},time.monotonic()+180)
    return ctx,r

def clip(ctx,i,out,deadline):
    from PIL import Image
    begun=time.monotonic_ns();products=copy.deepcopy(ctx.pc2_media_products)
    target=dict(**ctx.pc2_media_targets[i],imagePath='C:/test/KINGDOM HEARTS II FINAL MIX.exe',imageName='KINGDOM HEARTS II FINAL MIX.exe',imageSha256='a'*64,ownedRecordSha256='b'*64,handleHeldThroughFinalization=True)
    processes={}
    for j,role in enumerate(('helper','encoder','verifier')):
        product=products['clipCli'if role=='helper'else role]
        processes[role]=dict(identity=dict(pid=10000+i*10+j,creationTicks=500+i*10+j,parentPid=os.getpid()if role=='helper'else 10000+i*10,imagePath=product['packagePath'],imageSha256=product['sha256'],argv=[product['packagePath'],str(out)]),startedNs=begun+1,finishedNs=begun+2,absenceCheckedNs=begun+3,exitCode=0,exited=True,identityAbsent=True)
    cap=dict(expectedRequestSeq=1,doneSeq=1,nativeStatus=0,framesWritten=90,requestedInterval=2,width=16,height=16,renderer=12,backbufferFormat=87,gameFpsBefore=60,gameFpsDuringCapture=60,encodedFps=30,startedNs=begun+1,finishedNs=begun+2,captureLeaseReleased=True)
    video=put(ctx,out.name,b'offline mocked encoder bytes; never a real clip')
    rows=[];distinct=set();first=None;maximum=0
    for n in range(90):
        im=Image.new('RGB',(16,16),(70+n,90,110));p=ctx.run_dir/f'hb_arrival_peer{i}_frames/decoded_{n:03}.png';p.parent.mkdir(exist_ok=True);im.save(p)
        a=put(ctx,p.relative_to(ctx.run_dir).as_posix(),p.read_bytes());rgbsha=media.digest(im.tobytes());distinct.add(rgbsha)
        if first is None:first=im.copy()
        black,changed=media.frame_metrics(im,first);maximum=max(maximum,changed)
        rows.append(dict(index=n,pts=n,png=a,rgbSha256=rgbsha,blackFraction=black,changedPixels=changed))
    manifest=dict(schema='pc2-decoded-frames-v1',instance=i,videoSha256=video['sha256'],width=16,height=16,timeBase=dict(numerator=1,denominator=30),metricsAlgorithmSha256=products['metrics']['sha256'],rows=rows)
    fm=put(ctx,f'hb_arrival_peer{i}_frames.json',json.dumps(manifest).encode());sheet=put(ctx,f'hb_arrival_peer{i}_sheet.png',(ctx.run_dir/rows[0]['png']['path']).read_bytes())
    dec=dict(artifactSha256=video['sha256'],decodedAllFrames=True,decodeErrors=0,decodedFrames=90,width=16,height=16,durationSeconds=3,frameDigestManifest=fm,contactSheet=sheet,metricsAlgorithmSha256=products['metrics']['sha256'],blackFrames=0,nonblackDistinctFrames=len(distinct),motionQualified=True,maximumChangedPixels=maximum,motionChannelDelta=16,motionMinimumChangedPixels=64,blackLumaThreshold=16,blackPixelFractionThreshold=.99)
    logs={role:{stream:put(ctx,f'{i}_{role}_{stream}.txt',b'')for stream in ('stdout','stderr')}for role in processes}
    native=put(ctx,f'hb_arrival_peer{i}_raw.json',json.dumps(dict(target=target,capture=cap,processes=processes)).encode())
    finished=time.monotonic_ns()
    return dict(schema='pc2-owned-clip-v1',ok=True,acceptance=False,request=dict(instance=i,output=out.name,seconds=3,requestedFps=30),target=target,products=products,startedNs=begun,finishedNs=finished,deadlineMonotonicNs=int(deadline*1e9),capture=cap,artifact=video,decode=dec,processes=processes,closure=dict(allStartedIdentitiesRetained=True,helpersClosed=True,ownedDescendantsAbsent=True,checkedNs=finished,remainingOwnedIdentities=[]),rawCliReceipt=native,rawLogs=logs,driver=dict(pid=os.getpid(),creationTicks=100,parentPid=1,imagePath='python.exe',imageSha256='c'*64,argv=['python.exe']))

def run(packet):
    checks=[]
    def passed(name):checks.append(dict(name=name,status='PASS'))
    from PIL import Image
    import media_clock,time as python_time
    assert media_clock.clock.monotonic is python_time.perf_counter and media_clock.clock.monotonic_ns is python_time.perf_counter_ns
    assert python_time.monotonic is not media_clock.clock.monotonic
    passed('source6 QPC clock only; global monotonic and source5 clocks unchanged')
    colors=[(i,(i*17)%256,(i*31)%256)for i in range(256)]
    actual=Image.new('RGB',(16,16));actual.putdata(colors);reference=Image.new('RGB',(16,16),(16,16,16))
    expected=(sum(((77*r+150*g+29*b)>>8)<=16 for r,g,b in colors)/256,sum(max(abs(v-16)for v in rgb)>=16 for rgb in colors))
    assert media.frame_metrics(actual,reference)==expected
    passed('Pillow C integer luma/delta independently equals scalar reference including threshold edges')
    with TemporaryDirectory()as tmp:
        root=Path(tmp).resolve();ctx,r=make_context(root);deadline=time.monotonic()+45;row=clip(ctx,0,root/'hb_arrival_peer0.mp4',deadline)
        media.receipt(ctx,row,0,deadline);passed('full original 90-frame native/raw/log/metrics receipt')
        mutants={
            'wrong PID':lambda x:x['target'].update(pid=999),
            'wrong creation':lambda x:x['target'].update(creationTicks=999),
            'wrong module':lambda x:x['target'].update(moduleBase=99999),
            'wrong product':lambda x:x['products']['encoder'].update(sha256='d'*64),
            'wrong peer':lambda x:x['request'].update(instance=1),
            'boolean integer':lambda x:x['capture'].update(nativeStatus=False),
            'stale completion':lambda x:x['capture'].update(doneSeq=2),
            'unfinished lease':lambda x:x['capture'].update(captureLeaseReleased=False),
            'short frames':lambda x:x['capture'].update(framesWritten=89),
            'native refusal':lambda x:x['capture'].update(nativeStatus=1),
            'forged FPS':lambda x:x['capture'].update(encodedFps=31),
            'deadline extension':lambda x:x.update(deadlineMonotonicNs=x['deadlineMonotonicNs']+1),
            'late completion':lambda x:x.update(finishedNs=x['deadlineMonotonicNs']),
            'foreign driver':lambda x:x['driver'].update(pid=999),
            'foreign encoder':lambda x:x['processes']['encoder']['identity'].update(imageSha256='d'*64),
            'foreign helper parent':lambda x:x['processes']['helper']['identity'].update(parentPid=999),
            'reused child':lambda x:x['processes']['verifier'].update(identity=copy.deepcopy(x['processes']['encoder']['identity'])),
            'encoder unclosed':lambda x:x['processes']['encoder'].update(identityAbsent=False),
            'encoder exit':lambda x:x['processes']['encoder'].update(exitCode=1),
            'cleanup late':lambda x:x['closure'].update(checkedNs=x['finishedNs']+1),
            'remaining descendant':lambda x:x['closure'].update(remainingOwnedIdentities=[x['driver']]),
            'decoded dimensions':lambda x:x['decode'].update(width=17),
            'black video':lambda x:x['decode'].update(blackFrames=1),
            'static video':lambda x:x['decode'].update(maximumChangedPixels=0),
            'forged aggregate':lambda x:x['decode'].update(nonblackDistinctFrames=2),
            'wrong duration':lambda x:x['decode'].update(durationSeconds=4),
            'swapped video digest':lambda x:x['decode'].update(artifactSha256='d'*64),
            'unsafe path':lambda x:x['artifact'].update(path='../hb_arrival_peer0.mp4'),
            'missing raw log':lambda x:x['rawLogs']['encoder']['stderr'].update(path='absent.txt'),
            'missing native receipt':lambda x:x.pop('rawCliReceipt'),
        }
        for name,mutate in mutants.items():
            candidate=copy.deepcopy(row);mutate(candidate)
            try:media.receipt(ctx,candidate,0,deadline)
            except (ValueError,OSError,KeyError):passed('receipt refuses '+name)
            else:raise AssertionError(name)
        for name,path in [('video',(root/row['artifact']['path'])),('lossless decoded frame',root/'hb_arrival_peer0_frames/decoded_089.png'),('raw CLI receipt',root/row['rawCliReceipt']['path']),('empty log',root/row['rawLogs']['helper']['stderr']['path'])]:
            old=path.read_bytes();path.write_bytes(old+b'changed')
            try:media.receipt(ctx,row,0,deadline)
            except ValueError:passed('actual retained bytes mutation '+name)
            else:raise AssertionError(name)
            path.write_bytes(old)
        for name,mutate in [('index',lambda m:m['rows'][1].update(index=0)),('PTS',lambda m:m['rows'][1].update(pts=0)),('RGB hash',lambda m:m['rows'][1].update(rgbSha256='d'*64)),('black metric',lambda m:m['rows'][1].update(blackFraction=.5)),('motion metric',lambda m:m['rows'][1].update(changedPixels=12)),('swapped manifest',lambda m:m.update(instance=1))]:
            candidate=copy.deepcopy(row);path=root/row['decode']['frameDigestManifest']['path'];original=path.read_bytes();manifest=json.loads(original);mutate(manifest);candidate['decode']['frameDigestManifest']=put(ctx,path.name,json.dumps(manifest).encode())
            try:media.receipt(ctx,candidate,0,deadline)
            except ValueError:passed('full lossless manifest refuses '+name)
            else:raise AssertionError(name)
            path.write_bytes(original)
        with patch.object(media.time,'monotonic',return_value=deadline+1):
            try:media.receipt(ctx,row,0,deadline)
            except ValueError:passed('deadline after artifact/product operations refuses')
            else:raise AssertionError('deadline')
        # A hash that completes after the deadline must not qualify, even
        # though admission and every earlier operation happened in time.
        original_digest=media.digest;now=[time.monotonic()]
        def late_hash(data):
            value=original_digest(data)
            if data==b'offline mocked encoder bytes; never a real clip':now[0]=deadline+1
            return value
        with patch.object(media,'digest',late_hash),patch.object(media.time,'monotonic',side_effect=lambda:now[0]):
            try:media.receipt(ctx,row,0,deadline)
            except ValueError:passed('retained artifact hash finishes late; exact post-processing refusal')
            else:raise AssertionError('late hash qualified')
        original=ctx.pc2_media_cancel
        import threading
        ctx.pc2_media_cancel=threading.Event()
        try:media.receipt(ctx,row,0,deadline)
        except ValueError:passed('original cancellation Event replacement refuses')
        else:raise AssertionError('event')
        ctx.pc2_media_cancel=original;original.set()
        try:media.receipt(ctx,row,0,deadline)
        except ValueError:passed('source cancellation refuses')
        else:raise AssertionError('cancel')
    # Exact successful retained05 causal proof, not a fabricated live PASS.
    import native_sync_proof
    history=packet/'history/hbsync05';retained=json.loads((history/'native_sync_result.json').read_text())
    proof=retained['proof'];proof['current']={int(i):v for i,v in proof['current'].items()};ident=proof['identity'];ident['owned']={int(i):v for i,v in ident['owned'].items()}
    e=dict(request=[4,0,0,65535,65535,1],baselines=retained['baselines'],identity=ident,current=proof['current'],hostLog=(history/'native_sync_inject_0.log').read_bytes(),friendLog=(history/'native_sync_inject_1.log').read_bytes(),runtimeLogs={i:(history/f'native_sync_runtime_{i}.log').read_bytes()for i in (0,1)})
    need=media.need;actual=native_sync_proof.verify(e);need(media.canonical(actual)==media.canonical(proof),'retained05 whole proof equality');media.require_original_endpoint(actual,actual,None);passed('retained hb-sync05 exact whole source5 proof and endpoint')
    for i in (0,1):
        for field in ('eventState','eventContext','frozen','inField','openMenu','pauseBlockers'):
            for after in ('flags','flagsAfter'):
                bad=copy.deepcopy(actual);bad['current'][i][after][field]=1 if field not in ('inField','openMenu')else (0 if field=='inField'else 7)
                try:media.require_original_endpoint(actual,bad,None)
                except ValueError:passed(f'endpoint peer{i} {after} {field}')
                else:raise AssertionError(field)
        for field in ('pid','creationTicks','moduleBase','nativeScope','worldAfter'):
            bad=copy.deepcopy(actual);bad['current'][i][field]=None
            try:media.require_original_endpoint(actual,bad,None)
            except ValueError:passed(f'endpoint peer{i} original {field}')
            else:raise AssertionError(field)
    # Actual observer with retained raw logs and only native leaves mocked.
    for failure in ('none','unsafe-after-clip','scope-after-clip','raw-runtime-after-clip','clip-refusal','join-replacement','deadline-after-join','unsafe-start-initial','unsafe-start-final','finish-inventory','finish-terminal','finish-late','finish-retain-expiry','finish-retain-cancel','validation-join-expiry','unsafe-after-png','cause-superseded-after-clip','mixed-clock-past','mixed-clock-future','deadline-before-warp','cancel-before-warp','boundary-before-warp','budget-before-warp'):
        with TemporaryDirectory()as temp:
            root=Path(temp).resolve();ctx,r=make_context(root);state=dict(warped=False,closed=False,mutated=False);commands=[];handles=[];ids=ident['owned']
            for i,label in enumerate(('host','friend')):
                b=retained['baselines'][label];(root/f'inject{i}.log').write_bytes(e['hostLog'if i==0 else'friendLog'][:b['offset']]);(root/f'runtime_{i}.log').write_bytes(e['runtimeLogs'][i][:b['runtimeOffset']])
            inst=[NS(pid=ids[i]['pid'],inject_log=root/f'inject{i}.log')for i in (0,1)];index={v.pid:i for i,v in enumerate(inst)}
            class Failed(Exception):pass
            def command(name,*args,pid,**kw):
                commands.append((name,pid));i=index[pid]
                if name=='warp':
                    if failure=='budget-before-warp':need(0<kw['timeout']<=ctx._story_total_deadline-time.monotonic()+.001 and kw['timeout']<60,'warp timeout capped to original remaining budget')
                    need(i==0 and sum(n=='warp'for n,p in commands)==1,'only one host warp')
                    state['warped']=True
                    for j in (0,1):inst[j].inject_log.write_bytes(e['hostLog'if j==0 else'friendLog']);(root/f'runtime_{j}.log').write_bytes(e['runtimeLogs'][j])
                    return dict(ok=True,processId=pid)
                need(name=='capture','no gameplay input');out=Path(args[1]);from PIL import Image
                Image.new('RGB',(16,16),(100,120,140)).save(out)
                if failure=='unsafe-after-png':state['pngDone']=True
                return dict(ok=True,processId=pid,path=str(out),width=16,height=16,renderer=12,backbufferFormat=87,expectedRequestSeq=2,doneSeq=2,nativeStatus=0,framesWritten=1)
            def native(reader):
                return dict(location=media.ENDPOINT if state['warped']else handler.START,event=3 if (state['mutated']and failure=='unsafe-after-clip') or (state.get('pngDone')and failure=='unsafe-after-png') or (not state['warped'] and (failure=='unsafe-start-initial' or failure=='unsafe-start-final'and state.get('servicing')))else 0,context=0,frozen=0,inField=1,menu=255)
            class Reader:
                def __init__(self,pid,base):self.pid=pid;self.base=base;self.closed=False;handles.append(self)
                def read(self,a,n):return bytes.fromhex('000000000100')
                def value(self,a,f):return 0
                def close(self):self.closed=True
            class Mapping:
                def __init__(self,pid,*args):self.pid=pid;self.closed=False;handles.append(self)
                def read(self):
                    i=index[self.pid];h=proof['current'][i]['worldBefore'];b=bytearray(128);struct.pack_into('<II',b,0,0x42574B32,12);struct.pack_into('<I',b,8,i);struct.pack_into('<I',b,20,2);struct.pack_into('<QQQ',b,24,*h['connectionIds']);struct.pack_into('<I',b,48,h['generation']);struct.pack_into('<QQQQ',b,56,h['deliverySerial'],*h['peerDeliverySerials']);struct.pack_into('<I',b,116,h['writerPid']);return bytes(b)
                def close(self):self.closed=True
            class Service:
                failure=None
                def __init__(self,*args):pass
                def start(self):state['servicing']=True
                def require(self):pass
                def close(self):
                    state['closed']=True
                    if failure=='join-replacement':
                        with inst[0].inject_log.open('ab')as f:f.write(b'[warp] load complete serial=99 transition=99 room=04/0A door=50 map=0 btl=0 evt=22\n')
            def safe(r,c,i):
                fields={f'0x{a:X}':v for a,v in zip((0x717008,0x717009,0x71700A,0x71700C,0x71700E,0x717010),handler.START)};fields.update({'0xB65210':0,'0x2A11478':0,'0x2A171E8':0,'0x9BA8D0':1,'0x7435D0':255,'0xABB878':0})
                return dict(safe=True,location=handler.START,raw=dict(ok=True,processId=inst[i].pid,moduleBase=hex(ids[i]['moduleBase']),samples=[fields]))
            def hook(c,i,out,deadline):
                if failure=='clip-refusal':raise Failed('owned clip failed after bounded cleanup')
                row=clip(c,i,out,deadline);state['mutated']=True
                if failure in ('mixed-clock-past','mixed-clock-future'):
                    shift=-1000000000 if failure=='mixed-clock-past'else 1000000000
                    row['startedNs']+=shift;row['finishedNs']+=shift
                if failure=='scope-after-clip':
                    with inst[0].inject_log.open('ab')as f:f.write(b'[warp] load complete serial=99 transition=99 room=04/0A door=50 map=0 btl=0 evt=22\n')
                if failure=='cause-superseded-after-clip':
                    with inst[1].inject_log.open('ab')as f:f.write(b'[load-cause] event=superseded seq=99 cause=99\n')
                if failure=='raw-runtime-after-clip':
                    p=root/'runtime_0.log';p.write_bytes(p.read_bytes().replace(ident['session'].encode(),b'a'*32))
                return row
            ctx.inst=lambda i:inst[i];ctx.processes=[(f'runtime_{i}',NS(pid=proof['current'][i]['worldBefore']['writerPid'],poll=lambda:None),None)for i in (0,1)];ctx.sleep=lambda s:(_ for _ in ()).throw(Failed('finite mock refusal'));r.StepFailed=Failed;r.pc2_require_disk_inventory=lambda x:None;r.kh2ctl=command;r.pc2_owned_clip=hook
            clock=time.monotonic;clock_ns=time.monotonic_ns;original_dumps=json.dumps
            def slow_serialization(obj,*args,**kw):
                text=original_dumps(obj,*args,**kw)
                if type(obj)is dict and obj.get('hostWarpCount')==1 and 'requestReceipt'not in obj:
                    if failure in ('deadline-before-warp','boundary-before-warp'):state['expiredBeforeWarp']=True
                    if failure=='cancel-before-warp':ctx.pc2_media_cancel.set();state['cancelledBeforeWarp']=True
                    if failure=='budget-before-warp':state['budgetBeforeWarp']=True
                return text
            def monotonic():
                if state.get('expiredBeforeWarp'):return ctx._story_total_deadline+(0 if failure=='boundary-before-warp'else 1000)
                if state['closed']and failure=='validation-join-expiry':return ctx._story_media_validation[3]/1e9
                return clock()+(125 if state.get('budgetBeforeWarp')else 0)+(1000 if state['closed']and failure=='deadline-after-join'else 0)
            def monotonic_ns():return clock_ns()+(125000000000 if state.get('budgetBeforeWarp')else 0)
            with patch('memory_start.safe',safe),patch.object(handler.native_read,'Reader',Reader),patch.object(handler.native_read,'Mapping',Mapping),patch.object(handler.pause_scope,'native',native),patch.object(handler,'incarnation',lambda reader:ids[index[reader.pid]]['creationTicks']),patch.object(handler,'Service',Service),patch.object(handler.time,'monotonic',monotonic),patch.object(handler.time,'monotonic_ns',monotonic_ns),patch.object(handler.time,'perf_counter_ns',monotonic_ns),patch.object(handler.json,'dumps',slow_serialization):
                try:result=handler.observe(r,ctx)
                except (ValueError,Failed):result=ctx.saved.get('hb_host_warp_acceptance',{})
                if failure in ('none','finish-inventory','finish-terminal','finish-late','finish-retain-expiry','finish-retain-cancel','budget-before-warp'):need(result.get('status')=='READY_FOR_CLOSURE' and result['mediaQualified']and not result['acceptance'],'actual handler positive media boundary')
                else:
                    need(result.get('status')=='FAIL','actual handler mutant refused '+failure)
                    if failure.startswith('mixed-clock-'):need('mixed clip clock'in result['stopped'],'mixed clock caught at actual call window')
            need(sum(n=='warp'for n,p in commands)==(0 if failure.startswith('unsafe-start-')or failure in ('deadline-before-warp','boundary-before-warp','cancel-before-warp')else 1) and not any(n not in ('warp','capture')for n,p in commands),'no repeat or gameplay input');need(all(x.closed for x in handles),'read handles closed');passed('actual acceptance handler '+failure)
            if failure in ('deadline-before-warp','boundary-before-warp','cancel-before-warp'):
                need(result['hostWarpCount']==0 and result['requestDispatched']is False and not commands,'post-serialization refusal sends ZERO commands')
                need(state.get('expiredBeforeWarp')or state.get('cancelledBeforeWarp'),'control actually crossed serialization boundary')
                passed('review3 actual serializer boundary refusal ZERO warp '+failure)
            if failure in ('none','finish-inventory','finish-terminal','finish-late','finish-retain-expiry','finish-retain-cancel'):
                import fixture
                r.STEPS={};disk={'native'+str(i):'a'*64 for i in range(7)};r.hash_saves=lambda:disk;r.pc2_require_disk_inventory=lambda d:media.need(len(d)==7,'complete seven required')
                fixture.install(r,packet);ctx.saved['worldmap_disk_before']=copy.deepcopy(disk)
                if failure=='finish-inventory':disk['native0']='b'*64
                if failure=='finish-terminal':result['hostWarpCount']=2
                finalclock=time.monotonic
                finish_expired=[failure=='finish-late'];original_retain=Path.write_text
                def finish_retain(*a,**kw):
                    value=original_retain(*a,**kw)
                    if failure=='finish-retain-expiry':finish_expired[0]=True
                    if failure=='finish-retain-cancel':ctx.pc2_media_cancel.set()
                    return value
                with patch.object(media.time,'monotonic',side_effect=lambda:result['media']['validationDeadlineNs']/1e9 if finish_expired[0]else finalclock()),patch.object(Path,'write_text',finish_retain):
                    try:finish=r.STEPS['wm_acceptance_finish'](ctx,{})
                    except (ValueError,Failed):media.need(failure!='none','positive source finish refused')
                    else:media.need(failure=='none' and finish['acceptance']is False and finish['independentGlobalClosureRequired'],'no source acceptance promotion')
                passed('actual final source inventory/immutable/media deadline '+failure)
                if failure=='none':
                    reread=media.replay_media(root,json.loads(json.dumps(result)),ctx.pc2_media_products,ctx.pc2_media_targets,time.monotonic()+45,result['media']['clips'][0]['driver'])
                    media.need(reread['qualified']is True and reread['acceptance']is False,'post-closure read-only media replay cannot accept run');passed('independent post-closure media replay preserves original deadline and recomputes full frames')
    return checks
