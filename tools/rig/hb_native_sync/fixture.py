"""Accepted PC2 host-native-warp/product-follower fixture (source9 projection).

Only the acceptance04 dispatcher is installed. Execution requires the reviewed
PC2 adapter, sealed products, original consent/session binding and owned closure.
This module supplies offline --validate only; it never launches a game directly.
"""
import argparse,hashlib,importlib.util,json,sys,time
from pathlib import Path
import native_sync_acceptance,native_sync_media
import archive_format,memory_start,observer,packet_products

START=[2,2,0,4,0,0]

def require_disk_inventory(runner,disk):
    target=getattr(runner,'pc2_require_disk_inventory',None)
    if target is not None:target(disk)
    elif len(disk)!=4:raise runner.StepFailed('expected exactly four canonical disk-save files')


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def verify(packet):
    pins=json.loads((packet/'pins.json').read_text(encoding='utf-8'))
    files=pins['files'];files=files.items() if isinstance(files,dict) else ((r['path'],r['sha256']) for r in files)
    for name,digest in files:
        p=(packet/name).resolve()
        if not p.is_relative_to(packet) or sha(p)!=digest:raise ValueError('sealed input changed: '+name)
    for row in pins['externalPins']:
        if sha(Path(row['path']))!=row['sha256']:raise ValueError('external input changed: '+row['path'])


def load_runner(root):
    spec=importlib.util.spec_from_file_location('worldmap_fixture_runner',root/'tools/scenario/run.py')
    runner=importlib.util.module_from_spec(spec);sys.modules[spec.name]=runner;spec.loader.exec_module(runner);return runner


def install(runner,packet,*,platform=None):
    if platform is not None:
        if platform.machine!='PC2' or not callable(platform.inventory) or not callable(getattr(runner,'pc2_require_disk_inventory',None)):
            raise ValueError('complete PC2 inventory contract required')
        runner.hash_saves=platform.inventory
    packet_products.install(runner,packet)
    observer.install(runner);memory_start.install_launch_receipts(runner)
    source=archive_format.entries((packet/'save_sandbox_prepared/source-copy.png').read_bytes())[4][3]
    def retain(ctx,name,data):
        ctx.saved[name]=data;(ctx.run_dir/(name+'.json')).write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
        if name+'.json' not in ctx.artifacts:ctx.artifacts.append(name+'.json')
        return data
    def preflight(ctx,step):
        verify(packet)
        if json.loads((packet/'execution.json').read_text()).get('hostWarpAcceptanceSchema'):native_sync_media.require_hooks(runner,ctx)
        disk=runner.hash_saves()
        require_disk_inventory(runner,disk)
        retain(ctx,'worldmap_disk_before',disk)
        return {'status':'PENDING','noSaveInput':True,'startMethod':'loaded-copy checked in-memory diff and existing warp'}
    def setup(ctx,step):return memory_start.apply(runner,ctx,packet,source,START)
    def ready(ctx,step):
        end=time.monotonic()+20;last=None
        while time.monotonic()<end:
            last=runner.transition_evidence(ctx,[0,1],-1)
            if last.get('ready') and all(memory_start.safe(runner,ctx,i)['safe'] for i in (0,1)):
                return retain(ctx,'worldmap_bootstrap',last)
            ctx.sleep(.25)
        retain(ctx,'worldmap_bootstrap',last);raise runner.StepFailed('two-peer native bootstrap not ready; no map input')
    def route(ctx,step):
        if step.get('scope')=='native-arrival-acceptance':return native_sync_acceptance.observe(runner,ctx)
        raise runner.StepFailed('only accepted native-arrival-acceptance scope is supported')
    def finish(ctx,step):
        import native_sync_proof
        from media_clock import clock as acceptance_clock
        result=ctx.saved.get('hb_host_warp_acceptance',{})
        try:
            if result.get('status')!='READY_FOR_CLOSURE' or result.get('acceptance')is not False or result.get('mediaQualified')is not True or result.get('pairedFreshHBArrival')is not True or result.get('hostWarpCount')!=1 or result.get('friendWarpCount')!=0:
                raise runner.StepFailed('source acceptance media/proof not ready for closure')
            if native_sync_media.digest(native_sync_media.canonical(result).encode())!=ctx._story_acceptance_terminal:raise runner.StepFailed('immutable source terminal changed')
            current=result['proof']['current'];ident=result['identity']
            checked=native_sync_proof.verify(dict(request=[4,0,0,65535,65535,1],baselines=result['baselines'],identity=ident,current=current,hostLog=(ctx.run_dir/'native_sync_inject_0.log').read_bytes(),friendLog=(ctx.run_dir/'native_sync_inject_1.log').read_bytes(),runtimeLogs={i:(ctx.run_dir/f'native_sync_runtime_{i}.log').read_bytes() for i in (0,1)}))
            native_sync_media.require_original_endpoint(result['arrivalProof'],checked,None)
            if native_sync_media.canonical(checked)!=native_sync_media.canonical(result['proof']):raise runner.StepFailed('retained whole causal proof changed')
            native_sync_media.finalize(runner,ctx,result,ctx._story_total_deadline)
            disk=runner.hash_saves();require_disk_inventory(runner,disk)
            if disk!=ctx.saved['worldmap_disk_before']:raise runner.StepFailed('complete canonical inventory changed before closure')
            result['sourceDiskUnchanged']=True
            result['media']['sourceFinishedNs']=acceptance_clock.monotonic_ns()
            native_sync_media.history(result['media'],result['handlerDeadlineMonotonicNs'],terminal=True)
            retain(ctx,'native_sync_result',result)
            deadline=native_sync_media.validation_deadline(ctx,result['media'],ctx._story_total_deadline)
            if acceptance_clock.monotonic()>=deadline:raise runner.StepFailed('deadline after final source inventory/artifact processing')
            return dict(status='READY_FOR_CLOSURE',acceptance=False,independentGlobalClosureRequired=True)
        except Exception as error:
            result.update(status='FAIL',acceptance=False,stopped=str(error));retain(ctx,'native_sync_result',result);raise
        finally:
            if hasattr(ctx,'pc2_media_cancel'):ctx.pc2_media_cancel.set()
    runner.STEPS.update(wm_preflight=preflight,wm_memory_start=setup,wm_ready=ready,wm_route=route,wm_acceptance_finish=finish)
def validate(root,packet,scenario):
    runner=load_runner(root);install(runner,packet);data=json.loads(scenario.read_text(encoding='utf-8'));runner.validate_scenario(data)
    acceptance=json.loads((packet/'execution.json').read_text(encoding='utf-8')).get('hostWarpAcceptanceSchema')
    expected=['wm_preflight','boot','boot','wm_memory_start','relay','runtime','runtime','wm_ready','wm_baseline','progress_snapshot','wm_route']
    assert [s['do'] for s in data['steps']]==expected+(['wm_acceptance_finish'] if acceptance else [])
    if json.loads((packet/'execution.json').read_text(encoding='utf-8')).get('nativeSyncSchema'):
        assert data['steps'][-2 if acceptance else -1]==dict(do='wm_route',scope='native-arrival-acceptance' if acceptance else 'native-arrival-sync')
        assert [(s['instance'],s['role']) for s in data['steps'] if s['do']=='runtime']==[(0,'player'),(1,'friend1')]
    return dict(status='PENDING',structure='PASS',executable=True,liveExecuted=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ('root','packet','scenario'):parser.add_argument('--'+key,type=Path,required=True)
    parser.add_argument('--validate',action='store_true',required=True)
    args=parser.parse_args()
    print(json.dumps(validate(args.root.resolve(),args.packet.resolve(),args.scenario.resolve())))
