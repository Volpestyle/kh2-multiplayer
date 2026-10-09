"""Actual route handler with mocked read-only OS boundary; no native execution."""
import copy,json,struct
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace as NS
from unittest.mock import patch
import native_sync as route,test_native_sync_proof

def run():
    checks=test_native_sync_proof.run()
    safety_cases=[f'safe-{source}-{phase}-{i}-{field}' for source in ('raw','tail','independent') for phase in ('initial','final') for i in (0,1) for field in ('event','context','frozen','inField','menu','blockers')]
    for failure in ('none','host-bootstrap','baseline-reset-after-arrival','baseline-friend-reset','unsafe-start','defaults','warp-refusal','missing-cause','join-replacement','deadline-after-join','review3-join-runtime-host','review3-join-runtime-friend','retained-runtime-host','missing-personal-hashes','timeout','timeout-PID','timeout-target','timeout-from','timeout-frozen','timeout-inField','timeout-menu','timeout-error',*safety_cases,'no-reanchor-host','no-reanchor-friend'):
        with TemporaryDirectory() as temp:
            root=Path(temp);fixture=test_native_sync_proof.synthetic();state={'warped':False,'closed':False};commands=[];handles=[]
            class Failed(Exception):pass
            initial=[]
            for i,role in enumerate(('host','client')):
                log=f'[warp] load complete serial=2 transition=1 room=02/02 door=0 map=4 btl=0 evt=0\n[enemysync] {role} arrived epoch=1 room=02/02 door=0 map=4 btl=0 evt=0\n[load-cause-seal] hostSource=1\n'.encode()
                reset=b'[enemysync] session reset: host epoch and pending target cleared\n'
                if (failure=='host-bootstrap' and i==0) or (failure=='baseline-friend-reset' and i==1):log=log.replace(b'[enemysync] '+role.encode()+b' arrived',reset*3+b'[enemysync] '+role.encode()+b' arrived')
                if failure=='baseline-reset-after-arrival' and i==0:log+=reset
                initial.append(log);(root/f'inject{i}.log').write_bytes(log)
                r=dict(schema=1,identityCurrent=1,stringsComplete=1,attachedPid=100+i,slot=i,worldSlot=i,generationValid=1,authority=2,bridgeOpen=1,pinPresent=1,admitted=1,transportConnected=1,quarantine=0,errors=0,session=fixture['identity']['session'],pinSession=fixture['identity']['session'],pinHostConnection=1,pinSlot=i,hostConnection=1,selfConnection=1+i,roster0=1,roster1=2,roster2=0,worldRoster0=1,worldRoster1=2,worldRoster2=0,generation=2,delivery=1,peerFloor0=1,peerFloor1=1,peerFloor2=0)
                (root/f'runtime_{i}.log').write_text('[runtime-identity] '+' '.join(f'{k}={v}' for k,v in r.items())+'\n',encoding='utf-8')
            inst=[NS(pid=100+i,inject_log=root/f'inject{i}.log') for i in (0,1)]
            def command(name,*args,pid,**kw):
                commands.append((name,pid,args));assert name=='warp' and pid==100
                if failure=='warp-refusal':return dict(ok=False,processId=pid,error='refused')
                state['warped']=True
                for i,k in enumerate(('hostLog','friendLog')):
                    suffix=fixture[k][len(b'baseline\n'):]
                    if failure=='missing-cause' and i==1:suffix=suffix.replace(b'event=issue',b'event=foreign')
                    if failure=='missing-personal-hashes' and i==1:suffix=suffix.replace(b' personal_before=124AA108 personal_after=124AA108',b'')
                    inst[i].inject_log.write_bytes(initial[i]+suffix)
                if failure.startswith('timeout'):
                    ack=dict(ok=False,processId=pid,error='Timed out before the target room loaded',target=dict(world=4,room=0,door=0,map=65535,btl=65535,evt=1),**{'from':dict(world=2,room=2)},gateAtHandOver=dict(frozen=0,inField=1,openMenu=255))
                    if failure=='timeout-PID':ack['processId']=999
                    if failure=='timeout-target':ack['target']['room']=10
                    if failure=='timeout-from':ack['from']['room']=3
                    if failure=='timeout-frozen':ack['gateAtHandOver']['frozen']=1
                    if failure=='timeout-inField':ack['gateAtHandOver']['inField']=0
                    if failure=='timeout-menu':ack['gateAtHandOver']['openMenu']=7
                    if failure=='timeout-error':ack['error']='Warp held by safe-state gate'
                    return ack
                return dict(ok=True,processId=pid)
            def mutation(source,i):
                phase='final' if state.get('servicing') else 'initial'
                prefix=f'safe-{source}-{phase}-{i}-'
                return failure[len(prefix):] if failure.startswith(prefix) else None
            bad=dict(event=3,context=1,frozen=1,inField=0,menu=10,blockers=1)
            def safe(r,c,i):
                fields={f'0x{a:X}':v for a,v in zip((0x717008,0x717009,0x71700A,0x71700C,0x71700E,0x717010),route.START)}
                fields.update({'0xB65210':0,'0x2A11478':0,'0x2A171E8':0,'0x9BA8D0':1,'0x7435D0':255,'0xABB878':0})
                field=mutation('independent',i)
                # Leave the summary safe=True: the handler must inspect fresh raw fields.
                if field:fields[dict(event='0xB65210',context='0x2A11478',frozen='0x2A171E8',inField='0x9BA8D0',menu='0x7435D0',blockers='0xABB878')[field]]=bad[field]
                return dict(safe=failure!='unsafe-start',location=route.START,raw=dict(ok=True,processId=100+i,moduleBase='0x140000000',samples=[fields]))
            def native(reader):
                state[('reads',reader.pid)]=state.get(('reads',reader.pid),0)+1
                row=dict(location=[4,0,0,0,0,1] if state['warped'] else route.START,event=0,context=0,frozen=0,inField=1,menu=255)
                field=mutation('raw',reader.pid-100) or (mutation('tail',reader.pid-100) if state[('reads',reader.pid)]%2==0 else None)
                if not state['warped'] and field and field!='blockers':row[field]=bad[field]
                return row
            class Reader:
                def __init__(self,pid,base):self.pid=pid;self.base=base;self.closed=False;handles.append(self)
                def read(self,a,n):return bytes.fromhex('010000000100' if failure=='defaults' else '000000000100')
                def value(self,a,fmt):
                    assert a==0xABB878 and fmt=='I'
                    return int(not state['warped'] and (mutation('raw',self.pid-100)=='blockers' or (state[('reads',self.pid)]%2==0 and mutation('tail',self.pid-100)=='blockers')))
                def close(self):
                    self.closed=True
                    if failure=='retained-runtime-host' and self.pid==100:
                        path=root/'runtime_0.log';path.write_text(path.read_text().replace(fixture['identity']['session'],'a'*32),encoding='utf-8')
            class Mapping:
                def __init__(self,pid,prefix='world',size=128):self.pid=pid;self.prefix=prefix;self.closed=False;handles.append(self)
                def read(self):
                    b=bytearray(128);struct.pack_into('<II',b,0,0x42574B32,12);struct.pack_into('<I',b,8,self.pid-100);struct.pack_into('<I',b,20,2)
                    struct.pack_into('<QQQ',b,24,1,2,0);struct.pack_into('<I',b,48,2);struct.pack_into('<QQQQ',b,56,1,1,1,0);struct.pack_into('<I',b,116,300+self.pid-100);return bytes(b)
                def close(self):self.closed=True
            class Service:
                failure=None
                def __init__(self,*args):pass
                def start(self):
                    state['servicing']=True
                    if failure.startswith('no-reanchor-'):
                        i=0 if failure.endswith('host') else 1
                        with inst[i].inject_log.open('ab') as out:out.write(f'[warp] load complete serial=3 transition=2 room=02/02 door=0 map=4 btl=0 evt=0\n[enemysync] {"host" if i==0 else "client"} arrived epoch=2 room=02/02 door=0 map=4 btl=0 evt=0\n'.encode())
                def require(self):pass
                def close(self):
                    state['closed']=True
                    if failure in ('review3-join-runtime-host','review3-join-runtime-friend'):
                        i=0 if failure.endswith('host') else 1
                        path=root/f'runtime_{i}.log';path.write_text(path.read_text().replace(fixture['identity']['session'],'a'*32),encoding='utf-8')
                    if failure=='join-replacement':
                        with inst[0].inject_log.open('ab') as out:out.write(b'[warp] load complete serial=4 transition=3 room=04/00 door=0 map=0 btl=0 evt=1\n')
            ctx=NS(inst=lambda i:inst[i],saved={},artifacts=[],run_dir=root,processes=[(f'runtime_{i}',NS(pid=300+i,poll=lambda:None),None) for i in (0,1)])
            def sleep(s):raise Failed('mock finite observation refusal')
            ctx.sleep=sleep;runner=NS(StepFailed=Failed,kh2ctl=command,pc2_require_disk_inventory=lambda x:None)
            import time
            clock=time.monotonic
            def monotonic():return clock()+(1000 if state['closed'] and failure=='deadline-after-join' else 0)
            with patch('memory_start.safe',safe),patch.object(route.native_read,'Reader',Reader),patch.object(route.native_read,'Mapping',Mapping),patch.object(route.pause_scope,'native',native),patch.object(route,'incarnation',lambda r:200+r.pid-100),patch.object(route,'Service',Service),patch.object(route.time,'monotonic',monotonic):
                try:route.observe(runner,ctx)
                except (Failed,ValueError) as e:stopped=str(e)
                else:raise AssertionError('handler must stop PENDING/refuse')
            result=json.loads((root/'native_sync_result.json').read_text())
            success=failure in ('none','timeout','host-bootstrap')
            assert result['pairedFreshHBArrival']==success,(failure,result)
            assert result['status']==('PENDING' if success else 'FAIL')
            assert result['friendWarpCount']==0 and result['noGameplayInput'] and not result['held']
            before_warp=failure in ('unsafe-start','defaults','baseline-reset-after-arrival','baseline-friend-reset') or failure in safety_cases or failure.startswith('no-reanchor-')
            assert len(commands)==(0 if before_warp else 1),failure
            if failure in safety_cases:
                assert result['hostWarpCount']==0 and not result['pairedFreshHBArrival']
                if '-tail-' in failure:assert result['readFailures']
                else:assert result['startAdmissionChecks'][-1]['qualified'] is False
            if failure in ('baseline-reset-after-arrival','baseline-friend-reset'):
                rejected=result['readFailures'][0]
                assert all(k in rejected for k in ('nativeBefore','nativeAfter','injectBeforeHex','injectAfterHex','worldBefore','worldAfter','creationBefore','creationAfter'))
            assert all(h.closed for h in handles)
            if success:assert stopped==route.STOP and result['proof']['location']==[4,0,0,0,0,1]
            else:assert stopped!=route.STOP
            checks.append(dict(name='actual mocked native sync handler '+failure,status='PASS'))
    return checks

if __name__=='__main__':print(json.dumps(dict(status='PASS',liveExecuted=False,checks=run()),indent=2))
