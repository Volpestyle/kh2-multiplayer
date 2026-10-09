"""Actual approved setup handler with mocked reads/writes; never opens a PID."""
import copy,json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch
import memory_start


def run(packet,source,start):
    changes=json.loads((packet/'memory-start.json').read_text(encoding='utf-8'))['changes'];checks=[]
    for valid in (False,True):
        reply=dict(ok=True,processId=77,saveGuardBeforeResume=True,preinject=dict(ackObserved=valid,resumePrevCount=1))
        r=SimpleNamespace(kh2ctl=lambda *a,**kw:copy.deepcopy(reply))
        memory_start.install_launch_receipts(r);received=r.kh2ctl('launch')
        assert received['ok']==valid and bool(r.memory_start_launch_receipts)==valid
        checks.append(dict(name='launch receipt '+('accepts exact pre-resume ACK' if valid else 'rejects absent ACK without hiding owned PID'),status='PASS'))
    for failure in ('none','second-source','guard','unsafe','changed-byte','write-readback'):
        with TemporaryDirectory(prefix='memory-start-control-') as d:
            out=Path(d);buffers=[bytearray(source),bytearray(source)];locations=[list(source[12:15])+[0,0,20] for i in (0,1)];writes=[];warps=[]
            if failure=='second-source':buffers[1][0x3580]^=1
            for i in (0,1):(out/f'inject{i}.log').write_text(f'[saveguard] ack signalled pid={100+i} qpc=1 tickMs=1\n',encoding='utf-8')
            ctx=SimpleNamespace(saved={},artifacts=[],run_dir=out,inst=lambda i:SimpleNamespace(pid=100+i,inject_log=out/f'inject{i}.log'))
            class Failed(Exception):pass
            def command(name,*args,pid,**kw):
                i=pid-100
                if name=='poke':
                    rva=int(args[1],0);value=int(args[5]);writes.append((i,rva,value))
                    if failure!='write-readback':buffers[i][rva-memory_start.SAVE]=value
                    return {'ok':True}
                assert name=='peek'
                raw={}
                for f in args[1].split(','):
                    key,kind=f.split(':');rva=int(key,0)
                    if rva>=memory_start.SAVE and rva<memory_start.SAVE+len(source):raw[key]=buffers[i][rva-memory_start.SAVE]
                    else:
                        fields={r:v for r,v in zip((0x717008,0x717009,0x71700A,0x71700C,0x71700E,0x717010),locations[i])}
                        fields.update({0xB65210:1 if failure=='unsafe' else 0,0x2A11478:0,0x2A171E8:0,0x9BA8D0:1,0x7435D0:255,0xABB878:0})
                        raw[key]=fields[rva]
                if failure=='changed-byte' and len(raw)==1:raw[next(iter(raw))]^=1
                return dict(ok=True,processId=pid,moduleBase='0x140000000',samples=[raw])
            def warp(c,s):
                i=s['instance'];locations[i]=[s[k] for k in ('world','room','door','map','btl','evt')];warps.append(i)
            runner=SimpleNamespace(StepFailed=Failed,kh2ctl=command,step_warp=warp,memory_start_launch_receipts={} if failure=='guard' else {100:{},101:{}})
            class Reader:
                def __init__(self,pid,base):self.i=pid-100
                def read(self,*args):return bytes(buffers[self.i])
                def close(self):pass
            with patch.object(memory_start.native_read,'Reader',Reader):
                try:memory_start.apply(runner,ctx,packet,source,start)
                except (Failed,ValueError):
                    assert failure!='none'
                else:assert failure=='none'
            if failure in ('second-source','guard','unsafe','changed-byte'):assert not writes and not warps
            if failure=='write-readback':assert len(writes)==1 and not warps
            if failure=='none':
                assert warps==[0,1] and len(writes)==2*len(changes)
                assert all(buffers[i][r['offset']]==r['after'] for i in (0,1) for r in changes)
            checks.append(dict(name='actual memory setup '+failure,status='PASS'))
    return checks
