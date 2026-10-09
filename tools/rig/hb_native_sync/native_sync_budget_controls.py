"""Actual acquisition/validation paths with deterministic QPC and no native work."""
import copy,json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace as NS
from unittest.mock import patch
import native_sync_media as m
from native_sync_media_controls import make_context,clip

def run(packet):
    checks=[]
    def passed(name):checks.append(dict(name=name,status='PASS'))
    for failure in ('none','late-hook','late-png','acquisition-retain','cancel-acquisition','late-validation','cancel-validation','late-join','late-finalize','total-cap'):
        with TemporaryDirectory()as tmp:
            ctx,r=make_context(Path(tmp).resolve());now=[1000.0];phase=[False];checkpoints=[]
            def seconds():now[0]+=.000001;return now[0]
            def ns():return int(seconds()*1e9)
            def hook(c,i,out,deadline):
                row=clip(c,i,out,deadline);now[0]+=17
                if failure=='late-hook'and i==1:now[0]=deadline
                return row
            def capture(*a,pid,**kw):
                from PIL import Image
                p=Path(a[2]);Image.new('RGB',(16,16),(110,120,140)).save(p)
                if failure=='late-png':now[0]=ctx._story_media_deadline
                return dict(ok=True,processId=pid,path=str(p),width=16,height=16,renderer=12,backbufferFormat=87,expectedRequestSeq=2,doneSeq=2,nativeStatus=0,framesWritten=1)
            r.pc2_owned_clip=hook;r.kh2ctl=capture
            original=m.receipt_artifacts
            def replay(c,row,deadline,recompute=True):
                # Prove all four artifacts exist BEFORE first full frame replay.
                assert all((c.run_dir/f'hb_arrival_peer{i}.{suffix}').exists()for i in(0,1)for suffix in('mp4','png'))
                if recompute:
                    phase[0]=True;now[0]+=13
                    if failure=='late-validation':now[0]=deadline
                    if failure=='cancel-validation':c.pc2_media_cancel.set()
                return original(c,row,deadline,recompute)
            def checkpoint(label):
                checkpoints.append(label)
                if label=='paired-media-complete'and failure=='cancel-acquisition':ctx.pc2_media_cancel.set()
            write=Path.write_text
            def retain(p,*a,**kw):
                value=write(p,*a,**kw)
                if failure=='acquisition-retain'and p.name=='native_sync_media.json'and not phase[0]:now[0]=ctx._story_media_deadline
                return value
            service=NS(require=lambda:None)
            with patch.object(m.time,'monotonic',seconds),patch.object(m.time,'monotonic_ns',ns),patch.object(m.time,'perf_counter_ns',ns),patch.object(m,'receipt_artifacts',replay),patch.object(Path,'write_text',retain):
                total=1055 if failure=='total-cap'else 1180
                request_times=[ns(),ns(),ns()]
                try:
                    media=m.observe(r,ctx,{},checkpoint,service,total)
                    result=dict(media=media,status='READY_FOR_CLOSURE',acceptance=False,mediaQualified=True,readHandlesClosed=True,serviceFailure=None,handlerStartedNs=int((total-180)*1e9),handlerDeadlineMonotonicNs=int(total*1e9),mediaProducts=ctx.pc2_media_products,mediaTargets=ctx.pc2_media_targets,sourceDiskUnchanged=True)
                    result.update(requestPreparedNs=request_times[0],requestStartedNs=request_times[1],requestFinishedNs=request_times[2],requestDispatched=True)
                    if failure=='late-join':now[0]=media['validationDeadlineNs']/1e9
                    if failure=='late-finalize':
                        old=m.artifact
                        def late(*a,**kw):
                            value=old(*a,**kw);now[0]=media['validationDeadlineNs']/1e9;return value
                        with patch.object(m,'artifact',late):m.finalize(r,ctx,result,total)
                    else:m.finalize(r,ctx,result,total)
                    media['sourceFinishedNs']=ns();m.history(media,int(total*1e9),terminal=True)
                    assert failure=='none','mutant accepted '+failure
                    assert media['validationFinishedNs']>media['acquisitionDeadlineNs'] and phase[0]
                    assert len(checkpoints)==10 and checkpoints[-1]=='paired-media-validated'
                    # Fresh verification time must authenticate original history first.
                    m.replay_media(ctx.run_dir,result,ctx.pc2_media_products,ctx.pc2_media_targets,seconds()+45,media['clips'][0]['driver'])
                    passed('retained03 timing: two17s hooks + two13s source replays past A45 inside V30/H180; full fresh replay')
                    for boundary in ('handler equals prepare','request completes at acquisition'):
                        good=copy.deepcopy(result)
                        if boundary=='handler equals prepare':good.update(handlerStartedNs=good['requestPreparedNs'],handlerDeadlineMonotonicNs=good['requestPreparedNs']+180_000_000_000)
                        else:good['requestFinishedNs']=media['acquisitionStartedNs']
                        assert m.replay_media(ctx.run_dir,good,ctx.pc2_media_products,ctx.pc2_media_targets,seconds()+45,media['clips'][0]['driver'])['qualified']is True
                        passed('full original outer chronology coherent boundary '+boundary)
                    # Actual review3 repro: move ONLY H/start together; all media
                    # bytes, phase stamps and receipt deadlines remain untouched.
                    outer_mutants={
                        'review3 entire media before handler':lambda d:d.update(handlerStartedNs=media['sourceFinishedNs']+1_000_000_000,handlerDeadlineMonotonicNs=media['sourceFinishedNs']+181_000_000_000),
                        'shifted H/start after acquisition':lambda d:d.update(handlerStartedNs=media['acquisitionStartedNs']+1,handlerDeadlineMonotonicNs=media['acquisitionStartedNs']+180_000_000_001),
                        'shifted H/start after request prepare':lambda d:d.update(handlerStartedNs=d['requestPreparedNs']+1,handlerDeadlineMonotonicNs=d['requestPreparedNs']+180_000_000_001),
                        'request before handler':lambda d:d.update(requestPreparedNs=d['handlerStartedNs']-1),
                        'request prepare after issue':lambda d:d.update(requestPreparedNs=d['requestStartedNs']+1),
                        'request complete before issue':lambda d:d.update(requestFinishedNs=d['requestStartedNs']),
                        'request after acquisition':lambda d:d.update(requestFinishedNs=media['acquisitionStartedNs']+1),
                        'missing request dispatch':lambda d:d.update(requestDispatched=False),
                        'bool request timestamp':lambda d:d.update(requestStartedNs=True),
                        'clip start before handler':lambda d:d['media']['clips'][0].update(startedNs=d['handlerStartedNs']-1),
                        'clip deadline after H':lambda d:d['media']['clips'][0].update(deadlineMonotonicNs=d['handlerDeadlineMonotonicNs']+1),
                        'clip process start before handler':lambda d:d['media']['clips'][0]['processes']['helper'].update(startedNs=d['handlerStartedNs']-1),
                        'capture native time after H':lambda d:d['media']['clips'][1]['capture'].update(finishedNs=d['handlerDeadlineMonotonicNs']+1),
                        'A before handler':lambda d:d['media'].update(acquisitionDeadlineNs=d['handlerStartedNs']-1),
                        'V after H':lambda d:d['media'].update(validationDeadlineNs=d['handlerDeadlineMonotonicNs']+1),
                    }
                    for label,mutate in outer_mutants.items():
                        bad=copy.deepcopy(result);mutate(bad)
                        # Require rejection BEFORE any fresh product/artifact replay.
                        with patch.object(m,'require_hooks',side_effect=AssertionError('fresh replay before outer history')):
                            try:m.replay_media(ctx.run_dir,bad,ctx.pc2_media_products,ctx.pc2_media_targets,seconds()+45,media['clips'][0]['driver'])
                            except ValueError:passed('full replay rejects outer chronology '+label)
                            else:raise AssertionError(label)
                    mutants={
                        'alias replaced':lambda d:d.update(deadlineMonotonicNs=d['validationDeadlineNs']),
                        'A extended':lambda d:d.update(acquisitionDeadlineNs=d['acquisitionDeadlineNs']+1_000_000_000,deadlineMonotonicNs=d['acquisitionDeadlineNs']+1_000_000_000),
                        'V renewed':lambda d:d.update(validationDeadlineNs=d['validationDeadlineNs']+1_000_000_000),
                        'late validation':lambda d:d.update(validationFinishedNs=d['validationDeadlineNs']),
                        'late terminal':lambda d:d.update(sourceFinishedNs=d['validationDeadlineNs']),
                        'late PNG':lambda d:d['captures'][1].update(finishedNs=d['acquisitionDeadlineNs']),
                        'late return':lambda d:d['clipCalls'][1].update(returnedNs=d['acquisitionDeadlineNs']),
                        'missing phase':lambda d:d.pop('validationStartedNs'),
                        'bool timestamp':lambda d:d.update(validationFinishedNs=True),
                        'old envelope':lambda d:d.update(schema='hb-arrival-media-v1'),
                        'mixed clock':lambda d:d.update(validationStartedNs=d['validationStartedNs']+100_000_000_000),
                    }
                    for label,mutate in mutants.items():
                        bad=copy.deepcopy(result);mutate(bad['media'])
                        try:m.replay_media(ctx.run_dir,bad,ctx.pc2_media_products,ctx.pc2_media_targets,seconds()+45,media['clips'][0]['driver'])
                        except ValueError:passed('fresh replay refuses original phase history '+label)
                        else:raise AssertionError(label)
                    ctx._story_media_validation=tuple([0]*5)
                    try:m.validation_deadline(ctx,media,total)
                    except ValueError:passed('live private deadline commitment refuses replacement')
                    else:raise AssertionError('private commitment')
                except ValueError:
                    assert failure!='none'
                    passed('actual source two-phase refusal '+failure)
    old=packet/'history/acceptance03/native_sync_media.json'
    if old.exists():
        partial=json.loads(old.read_text(encoding='utf-8-sig'))
        timing=json.loads((old.parent/'timing.json').read_text(encoding='utf-8-sig'))
        assert len(partial['clips'])==2 and len(partial['captures'])==1 and partial['qualified']is False
        assert partial['clipCalls'][1]['returnedNs']==timing['clipCalls'][1]['returnedNs']<partial['deadlineMonotonicNs']
        try:m.history(partial,timing['handlerDeadlineNs'],terminal=True)
        except ValueError:passed('exact retained acceptance03 partial history stays FAIL despite successful native clips')
        else:raise AssertionError('historical FAIL promoted')
    return checks
