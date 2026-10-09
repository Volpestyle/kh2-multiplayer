"""Read-only two-peer segment for an existing canonical runner-owned route.

No launch, attach, memory write or input command. A standalone run is not supplied
until a post-wardrobe sandbox start and host UI selection are qualified.
"""
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import time
import evidence

FIELDS='0x717008:u8,0x717009:u8,0x71700A:u8,0x71700C:u16,0x71700E:u16,0x717010:u16,0xB65210:i32,0x2A11478:u64,0x2A171E8:u32,0x9BA8D0:u8,0x7435D0:u8'


def install(runner):
    def sample(ctx,index):
        inst=ctx.inst(index)
        if inst.inject_log is None:
            raise runner.StepFailed('world-map observation needs owned launch log registration')
        if not any(name==f'runtime_{index}' and proc.poll() is None for name,proc,_ in ctx.processes):
            raise runner.StepFailed('world-map observation needs owned live runtime')
        started=time.monotonic_ns()
        before=runner.kh2ctl('peek','--rva',FIELDS,pid=inst.pid,timeout=5)
        runtime=(ctx.run_dir/f'runtime_{index}.log').read_bytes()
        inject=inst.inject_log.read_bytes()
        after=runner.kh2ctl('peek','--rva',FIELDS,pid=inst.pid,timeout=5)
        row=dict(instance=index,pid=inst.pid,startedNs=started,finishedNs=time.monotonic_ns(),
            peekBefore=before,peek=after,runtimeLogHex=runtime.hex(),injectLogHex=inject.hex())
        # Strict bracket check is retained as a problem, never an absence claim.
        return row

    def collect_pair(ctx):
        if len(ctx.instances)!=2:
            raise runner.StepFailed('world-map slice requires exactly two owned games')
        with ThreadPoolExecutor(max_workers=2) as pool:
            return [f.result() for f in [pool.submit(sample,ctx,i) for i in (0,1)]]

    def retain(ctx,trace):
        path=ctx.run_dir/'worldmap_trace.json'
        path.write_text(json.dumps(trace,indent=2)+'\n',encoding='utf-8')
        if path.name not in ctx.artifacts: ctx.artifacts.append(path.name)
        result=evidence.evaluate(trace)
        output=ctx.run_dir/'worldmap_result.json'
        output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        if output.name not in ctx.artifacts: ctx.artifacts.append(output.name)
        ctx.saved['worldmap_trace']=trace
        ctx.saved['worldmap_result']=result
        return result

    def baseline(ctx,step):
        trace={'schema':1,'samples':[collect_pair(ctx)]}
        retain(ctx,trace)
        for row in trace['samples'][0]:
            observed=evidence.native(row)
            if observed['location'][0]!=2 or not observed['safeField']:
                raise runner.StepFailed('world-map baseline must be fresh safe TT on both games')
        return {'status':'PENDING','baseline':'retained; route/save provenance still required'}

    runner.STEPS.update(wm_baseline=baseline)
