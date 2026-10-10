"""16s capture receipt and closure-gated, owned post-run processing.

No launch, input or game/capture-channel access in the post-run path. The
caller supplies independently retained run identities and its closure verifier.
"""
import copy
import ctypes
import json
import math
import os
from pathlib import Path
import subprocess
import time
import uuid

from PIL import Image
from . import metrics
from .adapter import read, write, validate


def pinned_artifact(root, row):
    path = metrics.file(root, row['path'])
    if metrics.artifact(root, path) != row:
        raise ValueError('original artifact bytes changed')
    return path


def admit_next_capture(product, previous, target):
    """A completed worker alone never authorizes another capture."""
    if previous is None:raise ValueError('prior capture closure UNKNOWN')
    root,receipt=previous
    validate(receipt,read(Path(__file__).with_name('pc2-owned-capture-v1.schema.json')))
    if read(root/f"hb_arrival_peer{receipt['instance']}_capture.json")!=receipt:
        raise ValueError('prior canonical capture changed')
    if any(receipt['target'][k]!=v for k,v in target.items()):
        raise ValueError('original capture target changed')
    if not product.native.absent(receipt['helper']['identity']):
        raise ValueError('prior capture helper remains')
    release=read(pinned_artifact(root,receipt['releaseJournal']))
    if (release['capture']!=receipt['capture'] or
            release['helper']!={k:receipt['helper']['identity'][k] for k in ('pid','creationTicks')} or
            release['target']!=receipt['target'] or release['seconds']!=16 or release['frames']!=480 or
            release['capture'].get('captureLeaseReleased') is not True):
        raise ValueError('original post-destructor release differs')
    for row in receipt['rawFrames']:
        pinned_artifact(root,{k:row[k] for k in ('path','bytes','sha256')})
    return True


def capture_only_receipt(product, ctx, raw, helper, begun, ended, started,
                         deadline, stdout, stderr, ensure):
    root = Path(ctx.run_dir).resolve()
    stem = 'hb_arrival_peer0' if stdout.name.startswith('hb_arrival_peer0_') else 'hb_arrival_peer1'
    instance = 0 if stem.endswith('0') else 1
    cap = raw['capture']
    if (raw.get('schema') != 'pc2-native-capture-only-v1'
            or raw.get('encoderStarted') is not False or raw.get('verifierStarted') is not False
            or type(cap['seconds']) is not int or cap['seconds']!=16 or type(cap['frameCount']) is not int or cap['frameCount']!=480
            or cap['framesWritten'] != 480 or cap['doneSeq'] != cap['expectedRequestSeq']
            or cap['nativeStatus'] != 0 or cap['captureLeaseReleased'] is not True
            or not started <= cap['startedNs'] < cap['finishedNs'] <= ended
            or ended-started > 23_000_000_000 or not product.native.absent(helper)):
        raise ValueError('capture-only completion/23s helper closure refused')
    raw_dir = root/(stem+'_raw')
    pattern = str(raw_dir/'f_%05u.bmp')
    release_path = root/(stem+'_capture.released.json')
    release = read(release_path)
    if (raw['rawPattern'] != pattern or raw['releaseJournal'] != str(release_path)
            or release['helper'] != {k: helper[k] for k in ('pid','creationTicks')}
            or release['target'] != raw['target'] or release['capture'] != cap
            or release['seconds'] != 16 or release['frames'] != 480 or release['output'] != pattern):
        raise ValueError('actual post-destructor release journal differs')
    if set(p.name for p in raw_dir.iterdir()) != {f'f_{i:05}.bmp' for i in range(480)}:
        raise ValueError('exact captured raw inventory required')
    rows = []
    for index in range(480):
        ensure()
        path = metrics.file(root, f'{stem}_raw/f_{index:05}.bmp')
        with Image.open(path) as image:
            if image.format != 'BMP' or image.size != (cap['width'],cap['height']):
                raise ValueError('raw frame dimensions/format differ')
        rows.append(dict(index=index, **metrics.artifact(root,path)))
        ensure()
    helper_closed = dict(identity=helper,startedNs=begun,finishedNs=ended,exitCode=0,
                         exited=True,identityAbsent=True,absenceCheckedNs=time.perf_counter_ns())
    receipt = dict(schema='pc2-owned-capture-v1',status='CAPTURED_READY_FOR_CLOSURE',
        acceptance=False,instance=instance,seconds=16,frames=480,
        target=raw['target'],products=copy.deepcopy(product.products),driver=copy.deepcopy(product.driver),
        startedNs=started,finishedNs=time.perf_counter_ns(),
        nativeDeadlineNs=min(math.floor(deadline*1e9),started+23_000_000_000),
        phaseDeadlineNs=math.floor(deadline*1e9),capture=cap,helper=helper_closed,
        rawFrames=rows,releaseJournal=metrics.artifact(root,release_path),
        rawCliReceipt=metrics.artifact(root,stdout),
        rawLogs=dict(stdout=metrics.artifact(root,stdout),stderr=metrics.artifact(root,stderr)))
    validate(receipt,read(Path(__file__).with_name('pc2-owned-capture-v1.schema.json')))
    ensure(); write(root/(stem+'_capture.json'),receipt); ensure()
    return receipt


def admit_postrun(product, ctx, capture, closure, originals, verify_closure, deadline):
    """All checks precede ANY native spawn. No boolean/default verifier fallback."""
    now = time.perf_counter()
    if (type(deadline) is not float or not math.isfinite(deadline)
            or not now < deadline <= now+600 or product.seconds != 16):
        raise ValueError('new bounded <=600s post-run QPC deadline required')
    metrics.ensure(deadline,ctx.pc2_media_cancel)
    if set(originals) != {'game','runtime','captureHelper'}:
        raise ValueError('complete ORIGINAL game/runtime/capture-helper inventory required')
    root = Path(ctx.run_dir).resolve(); instance = capture['instance']
    canonical = read(root/f'hb_arrival_peer{instance}_capture.json')
    validate(capture,read(Path(__file__).with_name('pc2-owned-capture-v1.schema.json')))
    if canonical != capture or capture['products'] != product.products or capture['driver'] != product.driver:
        raise ValueError('original capture/package/driver binding differs')
    if (any(capture['target'][k] != v for k,v in ctx.pc2_media_targets[instance].items())
            or any(originals['game'][k] != capture['target'][k] for k in ('pid','creationTicks'))
            or originals['captureHelper'] != capture['helper']['identity']):
        raise ValueError('original run identities differ from admitted capture')
    keys=[(i['pid'],i['creationTicks']) for i in originals.values()]
    if len(keys)!=len(set(keys)):
        raise ValueError('duplicate original closure identities')
    if not callable(verify_closure) or verify_closure(copy.deepcopy(closure),copy.deepcopy(originals)) is not True:
        raise ValueError('independent gameplay/run closure verifier refused')
    # The verifier owns save/app-ID/ports/whole-run closure. Independently require
    # an explicit complete report and recheck every original native identity.
    if (closure.get('schema')!='pc2-postrun-run-closure-v1'
            or any(closure.get(k) is not True for k in ('gameplayComplete','gamesClosed',
                'runtimesClosed','captureHelpersClosed','descendantsAbsent','protectedFilesEqual',
                'portsClosed','censusComplete')) or closure.get('originals')!=originals):
        raise ValueError('run closure incomplete/disagrees')
    for identity in originals.values():
        metrics.ensure(deadline,ctx.pc2_media_cancel)
        if not product.native.absent(identity):raise ValueError('original run identity still alive')
    census=product.snapshot()
    if any(row.get('parent') in {i['pid'] for i in originals.values()} for row in census):
        raise ValueError('original run descendant remains or parent is ambiguous')
    for row in capture['rawFrames']:
        metrics.ensure(deadline,ctx.pc2_media_cancel);pinned_artifact(root,{k:row[k] for k in ('path','bytes','sha256')})
    for row in (capture['releaseJournal'],capture['rawCliReceipt'],*capture['rawLogs'].values()):
        pinned_artifact(root,row)
    metrics.ensure(deadline,ctx.pc2_media_cancel)
    return root



def fresh_postrun_closure(product, identities):
    """Fresh complete native census; no name-based absence or kill inference."""
    rows=product.snapshot()
    if not isinstance(rows,list) or any(not isinstance(row,dict) or
            type(row.get('pid')) is not int or type(row.get('parent')) is not int or
            type(row.get('creationTicks')) is not int for row in rows):
        raise ValueError('post-run census incomplete')
    keys={(i['pid'],i['creationTicks']) for i in identities.values()}
    parents={i['pid'] for i in identities.values()}
    if any((row['pid'],row['creationTicks']) in keys or row['parent'] in parents for row in rows):
        raise ValueError('post-run original or descendant remains')
    if any(not product.native.absent(i) for i in identities.values()):
        raise ValueError('post-run original identity remains')
    return dict(ownedDescendantsAbsent=True,remainingOwnedIdentities=[],checkedNs=time.perf_counter_ns())


def postprocess(product, ctx, capture, closure, *, original_identities, verify_closure, deadline):
    """One owned post-run helper; cancellation/failure never discards originals.

    The source/run owner must retain the original Event beyond game closure.
    This job has its own deadline; it cannot extend gameplay or restore a target.
    """
    root=admit_postrun(product,ctx,capture,closure,original_identities,verify_closure,deadline)
    stem=f"hb_arrival_peer{capture['instance']}";started=time.perf_counter_ns()
    deadline_ns=math.floor(deadline*1e9);proc=None;event=None;helper=None;uncertain=False
    original_event=ctx.pc2_media_cancel
    stdout=root/(stem+'_postrun_helper.stdout.log');stderr=root/(stem+'_postrun_helper.stderr.log')
    def ensure():
        metrics.ensure(deadline,original_event)
        if ctx.pc2_media_cancel is not original_event:raise ValueError('original post-run cancellation Event replaced')
        if ctx.pc2_media_products!=product.products:raise ValueError('post-run products changed')
    def artifact(path):
        ensure();row=metrics.artifact(root,path);ensure();return row
    try:
        product.publish(ctx);ensure()
        closure_path=root/'postrun-closure.json'
        with closure_path.open('x',encoding='utf-8') as f:json.dump(closure,f,indent=2)
        inventory=root/(stem+'_raw.tsv')
        with inventory.open('x',encoding='ascii',newline='\n') as f:
            for row in capture['rawFrames']:f.write(f"{Path(row['path']).name} {row['bytes']} {row['sha256']}\n")
        name='Local\\kh2coop_clip_cancel_'+str(os.getpid())+'_'+uuid.uuid4().hex
        event=product.native.k.CreateEventW(None,True,False,name)
        if not event:raise OSError('post-run cancellation event unavailable')
        output=root/(stem+'.mp4')
        argv=[product.products['clipCli']['packagePath'],'clip-postprocess','--out',str(output),
            '--raw-dir',str(root/(stem+'_raw')),'--inventory',str(inventory),
            '--inventory-sha256',metrics.sha(inventory),'--closure',str(closure_path),
            '--closure-sha256',metrics.sha(closure_path),'--deadline-ns',str(deadline_ns),
            '--cancel-event',name,'--tool-sha256',product.products['encoder']['sha256'],
            '--encoded-fps',repr(capture['capture']['encodedFps'])]
        for entry in product.products.values():
            if metrics.sha(Path(entry['packagePath']))!=entry['sha256']:raise ValueError('post-run product bytes changed')
        with stdout.open('xb') as out,stderr.open('xb') as err:
            ensure();proc=subprocess.Popen(argv,cwd=product.package,env=dict(os.environ),stdin=subprocess.DEVNULL,
                stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW|0x4)
            admitted=product.native.identity(proc._handle)
            helper=dict(pid=proc.pid,parentPid=os.getpid(),**admitted,
                imageSha256=product.products['clipCli']['sha256'],argv=argv)
            if Path(helper['imagePath'])!=Path(product.products['clipCli']['packagePath']):raise ValueError('post-run helper image differs')
            write(root/(stem+'_postrun_helper.identity.json'),helper)
            product.native.resume(proc,admitted,ensure)
            while proc.poll() is None:ensure();time.sleep(.02)
        ended=time.perf_counter_ns();ensure()
        if proc.returncode!=0:raise ValueError('post-run native helper nonzero exit')
        raw=read(stdout)
        if (raw.get('schema')!='pc2-native-postrun-v1' or raw.get('ok') is not True
                or raw['inventorySha256']!=metrics.sha(inventory) or raw['closureSha256']!=metrics.sha(closure_path)):
            raise ValueError('post-run native receipt binding differs')
        processes=copy.deepcopy(raw['processes'])
        for role in ('encoder','verifier'):
            child=processes[role];ident=child['identity']
            if (ident!=read(root/f'{stem}_{role}.identity.json') or child!=read(root/f'{stem}_{role}.closed.json')
                    or ident['parentPid']!=proc.pid or ident['imagePath']!=product.products[role]['packagePath']
                    or ident['imageSha256']!=product.products[role]['sha256'] or not product.native.absent(ident)):
                raise ValueError('post-run child original identity/closure differs')
        if not product.native.absent(helper):raise ValueError('post-run helper alive')
        processes['postrunHelper']=dict(identity=helper,startedNs=started,finishedNs=ended,exitCode=0,
            exited=True,identityAbsent=True,absenceCheckedNs=time.perf_counter_ns())
        tb,pts=metrics.decoder_rows((root/f'{stem}_verifier.stderr.log').read_text(encoding='utf-8'),count=480)
        cap=capture['capture'];manifest=dict(schema='pc2-decoded-frames-v2',seconds=16,frameCount=480,instance=capture['instance'],
            videoSha256=artifact(output)['sha256'],width=cap['width'],height=cap['height'],timeBase=tb,
            metricsAlgorithmSha256=product.products['metrics']['sha256'],rows=[])
        baseline=None
        for index,(_,pts_value,width,height) in enumerate(pts):
            ensure();path=root/f'{stem}_frames/decoded_{index:03}.png'
            with Image.open(path) as source:
                if source.format!='PNG' or source.mode!='RGB' or source.size!=(cap['width'],cap['height']) or source.size!=(width,height):
                    raise ValueError('full decoded frame format/dimensions differ')
                image=source.copy()
            if baseline is None:baseline=image.copy()
            manifest['rows'].append(dict(index=index,pts=pts_value,png=artifact(path),**metrics.frame_metrics(image,baseline)))
        if len(list((root/(stem+'_frames')).iterdir()))!=480:raise ValueError('extra/missing decoded frames')
        validate(manifest,read(Path(__file__).with_name('pc2-decoded-frames-v2.schema.json')))
        summary=metrics.summarize(manifest,cap['encodedFps'])
        if summary['blackFrames'] or not summary['motionQualified']:raise ValueError('black/static-only media unqualified')
        manifest_path=root/(stem+'_frames.json');write(manifest_path,manifest)
        # Recheck raw/closure and all identities after validation; no new budget.
        admit_postrun(product,ctx,capture,closure,original_identities,verify_closure,deadline)
        if any(not product.native.absent(p['identity']) for p in processes.values()):raise ValueError('post-run original remains')
        final_closure=fresh_postrun_closure(product,{role:p['identity'] for role,p in processes.items()})
        ensure()
        result=dict(schema='pc2-owned-postrun-v1',ok=True,acceptance=False,
            captureReceipt=artifact(root/(stem+'_capture.json')),runClosure=artifact(closure_path),
            rawInventory=artifact(inventory),products=copy.deepcopy(product.products),driver=copy.deepcopy(product.driver),
            startedNs=started,finishedNs=time.perf_counter_ns(),deadlineMonotonicNs=deadline_ns,
            artifact=artifact(output),frameManifest=artifact(manifest_path),decode=summary,processes=processes,
            rawCliReceipt=artifact(stdout),rawLogs={role:{stream:artifact(root/f'{stem}_{role}.{stream}.log')
                for stream in ('stdout','stderr')} for role in ('encoder','verifier')},
            helperLogs=dict(stdout=artifact(stdout),stderr=artifact(stderr)),
            closure=final_closure)
        validate(result,read(Path(__file__).with_name('pc2-owned-postrun-v1.schema.json')))
        ensure();write(root/(stem+'_postrun.json'),result);ensure();return result
    except BaseException as error:
        if event:product.native.k.SetEvent(event)
        if proc is not None and proc.poll() is None:
            try:proc.terminate();proc.wait(timeout=max(0,min(2,deadline-time.perf_counter())))
            except (OSError,subprocess.TimeoutExpired):uncertain=True
        originals={}
        if helper is not None:originals['postrunHelper']=helper
        for role in ('encoder','verifier'):
            path=root/f'{stem}_{role}.identity.json'
            if path.exists():
                try:originals[role]=read(path)
                except BaseException:uncertain=True
        if proc is not None and helper is None:uncertain=True
        remaining=[]
        for identity in originals.values():
            try:
                if not product.native.absent(identity):remaining.append(identity)
            except OSError:uncertain=True;remaining.append(identity)
        try:fresh_postrun_closure(product,originals)
        except (OSError,ValueError):uncertain=True
        retained=[name for name in (stem+'_capture.json',stem+'_raw.tsv','postrun-closure.json',
            stdout.name,stderr.name,stem+'_encoder.stdout.log',stem+'_encoder.stderr.log',
            stem+'_verifier.stdout.log',stem+'_verifier.stderr.log') if (root/name).is_file()]
        failure=dict(schema='pc2-owned-postrun-failure-v1',ok=False,acceptance=False,error=repr(error),
            startedNs=started,finishedNs=time.perf_counter_ns(),deadlineMonotonicNs=deadline_ns,
            spawnedIdentities=originals,retainedPaths=retained,closure=dict(ownedDescendantsAbsent=not uncertain and not remaining,
                remainingOwnedIdentities=remaining),originalCapture=capture,originalRunClosure=closure)
        validate(failure,read(Path(__file__).with_name('pc2-owned-postrun-failure-v1.schema.json')))
        write(root/(stem+'_postrun_failure.json'),failure)
        raise
    finally:
        product.native.close(event)
