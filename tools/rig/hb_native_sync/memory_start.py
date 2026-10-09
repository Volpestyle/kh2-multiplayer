"""Approved finite in-memory SAVE setup, only after a loaded-copy check.

No archive read-redirection assumption, no save command or disk-save write.
The canonical runner retains protected disk hashes at closure.
"""
import hashlib,json,time
from pathlib import Path
import native_read

SAVE=0x9A98B0
SAFE='0x717008:u8,0x717009:u8,0x71700A:u8,0x71700C:u16,0x71700E:u16,0x717010:u16,0xB65210:i32,0x2A11478:u64,0x2A171E8:u32,0x9BA8D0:u8,0x7435D0:u8,0xABB878:u32'


def install_launch_receipts(runner):
    original=runner.kh2ctl;receipts={}
    def call(command,*args,**kwargs):
        result=original(command,*args,**kwargs)
        if command=='launch' and result.get('ok') is True:
            p=result.get('preinject',{})
            if result.get('saveGuardBeforeResume') is not True or p.get('ackObserved') is not True or p.get('resumePrevCount')!=1:
                # The canonical runner still registers this returned owned PID
                # for cleanup, then refuses boot instead of orphaning it.
                result=dict(result,ok=False,error='pre-resume guard proof missing')
            else:receipts[result['processId']]=result
        return result
    runner.kh2ctl=call;runner.memory_start_launch_receipts=receipts


def safe(runner,ctx,i):
    reply=runner.kh2ctl('peek','--rva',SAFE,pid=ctx.inst(i).pid,timeout=5)
    if reply.get('ok') is not True or reply.get('processId')!=ctx.inst(i).pid:raise runner.StepFailed('native PID/result mismatch')
    raw=reply['samples'][0]
    loc=[raw[f'0x{x:X}'] for x in (0x717008,0x717009,0x71700A,0x71700C,0x71700E,0x717010)]
    context=raw['0x2A11478'];context=int(context,0) if isinstance(context,str) else context
    is_safe=(raw['0xB65210']==0 and context==0 and raw['0x2A171E8']==0 and raw['0x9BA8D0']!=0 and raw['0x7435D0']==255 and raw['0xABB878']==0 and loc[0] not in (15,255))
    return dict(location=loc,safe=is_safe,raw=reply,finishedNs=time.monotonic_ns())


def loaded_check(live,source,changes):
    # Strong content checks, not a claim of filename/container provenance.
    ranges=((0,12),(0x24F0,0x3526),(0x3580,0x36C0))
    if any(live[a:b]!=source[a:b] for a,b in ranges):raise ValueError('loaded source header/kit/inventory mismatch')
    if any(live[r['offset']]!=r['before'] for r in changes):raise ValueError('loaded source patch bytes mismatch')
    return dict(sourcePayloadSha256=hashlib.sha256(source).hexdigest(),
                checkedRanges=[dict(begin=a,end=b,sha256=hashlib.sha256(live[a:b]).hexdigest()) for a,b in ranges],
                patchBytesChecked=len(changes),scope='content qualification; untested live; source-copy is explicit')


def apply(runner,ctx,packet,source,start):
    config=json.loads((packet/'memory-start.json').read_text(encoding='utf-8'))
    changes=config['changes'];rows=[]
    if not changes or any(type(r.get(k)) is not int for r in changes for k in ('offset','before','after')):
        raise runner.StepFailed('finite setup diff missing or malformed')
    if len({r['offset'] for r in changes})!=len(changes) or any(not 16<=r['offset']<0x24F0 or not 0<=r['before']<=255 or not 0<=r['after']<=255 or source[r['offset']]!=r['before'] for r in changes):
        raise runner.StepFailed('setup diff escaped progression/place region or source')
    ctx.saved['memory_start']=rows
    def retain():
        (ctx.run_dir/'memory_start.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
        if 'memory_start.json' not in ctx.artifacts:ctx.artifacts.append('memory_start.json')
    # Qualify BOTH before the first write. No partial fallback or memory repair.
    for i in (0,1):
        inst=ctx.inst(i);gate=safe(runner,ctx,i)
        row=dict(instance=i,pid=inst.pid,gate=gate,applied=[]);rows.append(row);retain()
        if inst.pid not in runner.memory_start_launch_receipts:raise runner.StepFailed('exact launch ACK/resume receipt absent')
        row['launchReceipt']=runner.memory_start_launch_receipts[inst.pid];retain()
        if not gate['safe'] or gate['location'][:3]!=list(source[12:15]):raise runner.StepFailed('loaded source location/safety mismatch')
        if inst.inject_log is None or f'[saveguard] ack signalled pid={inst.pid} ' not in inst.inject_log.read_text(encoding='utf-8',errors='replace'):
            raise runner.StepFailed('pre-resume saveguard ACK missing')
        base=gate['raw']['moduleBase'];base=int(base,0) if isinstance(base,str) else base
        reader=native_read.Reader(inst.pid,base)
        try:row['loadedCopyCheck']=loaded_check(reader.read(base+SAVE,len(source)),source,changes)
        except Exception as error:
            row['refused']=str(error);raise
        finally:reader.close();retain()
    try:
        for i in (0,1):
            for r in changes:
                gate=safe(runner,ctx,i)
                if not gate['safe'] or gate['location'][:3]!=list(source[12:15]):raise runner.StepFailed('state changed before setup byte; stop')
                key=f"0x{SAVE+r['offset']:X}"
                before=runner.kh2ctl('peek','--rva',key+':u8',pid=ctx.inst(i).pid)['samples'][0][key]
                if before!=r['before']:raise runner.StepFailed('setup byte changed after source check')
                runner.kh2ctl('poke','--rva',key,'--type','u8','--value',str(r['after']),pid=ctx.inst(i).pid)
                after=runner.kh2ctl('peek','--rva',key+':u8',pid=ctx.inst(i).pid)['samples'][0][key]
                rows[i]['applied'].append(dict(**r,readback=after))
                if after!=r['after']:raise runner.StepFailed('setup byte readback mismatch')
            runner.step_warp(ctx,dict(instance=i,world=start[0],room=start[1],door=start[2],map=start[3],btl=start[4],evt=start[5]))
            current=safe(runner,ctx,i)
            if not current['safe'] or current['location']!=start:raise runner.StepFailed('derived start warp did not qualify')
            rows[i]['qualified']=current
    finally:
        retain()
    return rows
