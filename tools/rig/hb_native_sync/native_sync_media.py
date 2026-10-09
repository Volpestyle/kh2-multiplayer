"""Offline-verifiable source-owned media gates; never accepts a complete run."""
import copy, hashlib, io, json, math, os, stat
from media_clock import clock as time
from pathlib import Path, PurePosixPath

CONTRACT=Path(__file__).with_name('media_contract')
ENDPOINT=[4,10,50,0,0,22]

def need(value,reason):
    if not value:raise ValueError(reason)

def strict_json(data):
    def pairs(rows):
        out={}
        for k,v in rows:
            need(k not in out,'duplicate JSON key');out[k]=v
        return out
    return json.loads(data,object_pairs_hook=pairs,parse_constant=lambda x:(_ for _ in ()).throw(ValueError('nonfinite JSON')))

def canonical(obj):return json.dumps(obj,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False)
def digest(data):return hashlib.sha256(data).hexdigest()

def validate(obj,schema,defs=None):
    """Strict validator for the deliberately small frozen JSON schema vocabulary."""
    if defs is None:defs=schema.get('$defs',{})
    if '$ref' in schema:
        need(schema['$ref'].startswith('#/$defs/'),'nonlocal schema reference')
        return validate(obj,defs[schema['$ref'].split('/')[-1]],defs)
    if 'anyOf' in schema:
        for choice in schema['anyOf']:
            try:validate(obj,choice,defs);return
            except (ValueError,TypeError):pass
        raise ValueError('no schema alternative')
    t=schema.get('type')
    if t:need({'object':type(obj)is dict,'array':type(obj)is list,'integer':type(obj)is int,'number':type(obj)in(int,float) and math.isfinite(obj),'string':type(obj)is str,'boolean':type(obj)is bool,'null':obj is None}[t],'schema type '+t)
    if 'const' in schema:need(type(obj)is type(schema['const']) and obj==schema['const'],'schema const')
    if 'enum' in schema:need(any(type(obj)is type(x) and obj==x for x in schema['enum']),'schema enum')
    if type(obj)in(int,float):
        need(math.isfinite(obj),'nonfinite number')
        for k,op in [('minimum',lambda a,b:a>=b),('maximum',lambda a,b:a<=b),('exclusiveMinimum',lambda a,b:a>b)]:
            if k in schema:need(op(obj,schema[k]),'schema '+k)
    if type(obj)is str:
        import re
        need(len(obj)>=schema.get('minLength',0),'short string')
        if 'pattern'in schema:need(re.search(schema['pattern'],obj) is not None,'schema pattern')
    if type(obj)is dict:
        need(all(k in obj for k in schema.get('required',[])),'missing required field')
        props=schema.get('properties',{})
        if schema.get('additionalProperties')is False:need(set(obj)<=set(props),'extra field')
        for k,v in obj.items():
            if k in props:validate(v,props[k],defs)
    if type(obj)is list:
        need(len(obj)>=schema.get('minItems',0) and len(obj)<=schema.get('maxItems',math.inf),'array length')
        for v in obj:validate(v,schema.get('items',{}),defs)

def schema(name):return strict_json((CONTRACT/name).read_bytes())

def check_clock(ctx,deadline):
    need(math.isfinite(deadline) and time.monotonic()<deadline,'media deadline expired')
    need(not ctx.pc2_media_cancel.is_set(),'media cancelled')

def regular(path):
    path=Path(path)
    need(path.is_absolute(),'absolute artifact/product path required')
    # Reject reparse/symlink components, including junctions, before resolution.
    for part in (path,*path.parents):
        st=part.lstat()
        need(not stat.S_ISLNK(st.st_mode) and not getattr(st,'st_file_attributes',0)&0x400,'reparse path refused')
    need(path.is_file() and path.resolve()==path,'regular normalized file required')
    return path

def artifact(ctx,row,deadline,empty=False):
    defs=schema('pc2-owned-clip-v1.schema.proposed.json')['$defs']
    validate(row,defs['rawArtifact' if empty else 'artifact'],defs)
    rel=PurePosixPath(row['path'])
    need(not rel.is_absolute() and '\\' not in row['path'] and ':' not in row['path'] and '..'not in rel.parts and str(rel)==row['path'],'unsafe run-relative artifact')
    check_clock(ctx,deadline);p=regular(ctx.run_dir.resolve()/str(rel))
    data=p.read_bytes();check_clock(ctx,deadline)
    need(len(data)==row['bytes'] and digest(data)==row['sha256'],'artifact bytes/hash mismatch')
    check_clock(ctx,deadline)
    if row['path']not in ctx.artifacts:ctx.artifacts.append(row['path'])
    return data

def require_hooks(runner,ctx=None):
    from PIL import Image,ImageMath
    need(callable(getattr(ImageMath,'lambda_eval',None)),'reviewed Pillow integer metrics prerequisite missing')
    need(callable(getattr(runner,'pc2_owned_clip',None)),'reviewed package-owned clip hook required before boot/warp')
    # Adapter publishes products before boot, on runner; ctx publication is checked
    # again at original native admission. No native execution in this check.
    products=getattr(ctx,'pc2_media_products',None) if ctx is not None else getattr(runner,'pc2_media_products',None)
    if products is None:products=getattr(runner,'pc2_media_products',None)
    defs=schema('pc2-owned-clip-v1.schema.proposed.json')['$defs'];validate(products,defs['products'],defs)
    for row in products.values():need(digest(regular(row['packagePath']).read_bytes())==row['sha256'],'pinned media product mismatch')
    return copy.deepcopy(products)

def commit(runner,ctx,targets,deadline):
    products=require_hooks(runner,ctx)
    need(type(targets)is dict and set(targets)=={0,1},'paired original targets required')
    for row in targets.values():need(set(row)=={'pid','creationTicks','moduleBase'} and all(type(v)is int and v>0 for v in row.values()),'initial native identity malformed')
    ctx.pc2_media_targets=copy.deepcopy(targets)
    ctx.pc2_media_products=copy.deepcopy(products)
    import threading
    ctx.pc2_media_cancel=threading.Event()
    check_clock(ctx,deadline)
    # Private source copy prevents a hook replacing or editing public ctx fields.
    ctx._story_media_commit=(copy.deepcopy(products),copy.deepcopy(targets),ctx.pc2_media_cancel)
    return products

def original_admission(ctx):
    products,targets,cancel=ctx._story_media_commit
    need(ctx.pc2_media_cancel is cancel and ctx.pc2_media_products==products and ctx.pc2_media_targets==targets,'original media admission replaced')
    return products,targets

def committed(ctx,deadline):
    pair=original_admission(ctx);check_clock(ctx,deadline);return pair

def require_original_endpoint(anchor,checked,floors):
    need(checked['location']==ENDPOINT and anchor['location']==ENDPOINT,'unsupported HB endpoint')
    for key in anchor:
        if key not in ('current','runtimePrefixes'):need(canonical(checked[key])==canonical(anchor[key]),'original causal anchor replaced: '+key)
    for i in (0,1):
        old=anchor['current'].get(i,anchor['current'].get(str(i)));row=checked['current'].get(i,checked['current'].get(str(i)))
        for key in ('pid','creationTicks','moduleBase','nativeScope','scopeAfter','worldBefore','worldAfter'):
            need(row[key]==old[key],'original endpoint identity/scope replaced: '+key)
        safe=dict(eventState=0,eventContext=0,frozen=0,inField=1,openMenu=255,pauseBlockers=0)
        for flags in (row['flags'],row['flagsAfter']):need(all(type(flags.get(k))is int and flags[k]==v for k,v in safe.items()),'unsafe media endpoint flags')
        for raw in row['rawReads']:
            n=raw['native'];need(type(n.get('pauseBlockers'))is int and n['pauseBlockers']==0,'unsafe independent ABB878 bookend')
        if floors is not None:
            need(row['startedNs']>floors[i] and row['finishedNs']>=row['startedNs'],'media bracket read floor did not advance')
    if floors is not None:
        for i in (0,1):floors[i]=checked['current'].get(i,checked['current'].get(str(i)))['finishedNs']

def frame_metrics(rgb,first):
    from PIL import ImageMath,ImageChops
    r,g,b=rgb.split()
    # Integer I mode avoids Pillow's rounded L conversion and overflow in L mode.
    lum=ImageMath.lambda_eval(lambda a:(a['r']*77+a['g']*150+a['b']*29)>>8,r=r.convert('I'),g=g.convert('I'),b=b.convert('I')).convert('L')
    black=sum(lum.histogram()[:17])/(rgb.width*rgb.height)
    delta=ImageChops.difference(rgb,first);r,g,b=delta.split();maximum=ImageChops.lighter(ImageChops.lighter(r,g),b)
    changed=sum(maximum.histogram()[16:])
    return black,changed

def frames(ctx,receipt,deadline):
    from PIL import Image
    d=receipt['decode'];cap=receipt['capture'];i=receipt['request']['instance']
    manifest=strict_json(artifact(ctx,d['frameDigestManifest'],deadline));validate(manifest,schema('pc2-decoded-frames-v1.schema.proposed.json'))
    need(manifest['instance']==i and manifest['videoSha256']==receipt['artifact']['sha256'],'frame manifest swapped')
    need(manifest['width']==cap['width'] and manifest['height']==cap['height'] and manifest['metricsAlgorithmSha256']==receipt['products']['metrics']['sha256'],'manifest dimensions/metrics mismatch')
    first=None;previous=-1;distinct=set();black_frames=0;maximum=0
    for index,row in enumerate(manifest['rows']):
        need(row['index']==index and row['pts']>previous,'frame order/PTS invalid');previous=row['pts']
        need(row['png']['path']==f'hb_arrival_peer{i}_frames/decoded_{index:03}.png','frame path/index mismatch')
        data=artifact(ctx,row['png'],deadline)
        with Image.open(io.BytesIO(data)) as im:
            need(im.format=='PNG' and im.mode=='RGB' and im.size==(cap['width'],cap['height']),'lossless RGB dimensions/format required')
            rgb=im.copy()
        check_clock(ctx,deadline);raw_sha=digest(rgb.tobytes());check_clock(ctx,deadline)
        need(raw_sha==row['rgbSha256'],'decoded RGB hash mismatch')
        if first is None:first=rgb.copy()
        black,changed=frame_metrics(rgb,first);check_clock(ctx,deadline)
        need(row['blackFraction']==black and row['changedPixels']==changed,'frame metric forged')
        if black>=.99:black_frames+=1
        else:distinct.add(raw_sha)
        maximum=max(maximum,changed)
    duration=(manifest['rows'][-1]['pts']-manifest['rows'][0]['pts'])*manifest['timeBase']['numerator']/manifest['timeBase']['denominator']+1/cap['encodedFps']
    need(abs(duration-90/cap['encodedFps'])<=1/cap['encodedFps'] and abs(d['durationSeconds']-duration)<=1e-9,'decoded duration mismatch')
    need(black_frames==0 and len(distinct)>=2 and maximum>=64,'black/static clip refused')
    need(d['blackFrames']==black_frames and d['nonblackDistinctFrames']==len(distinct) and d['maximumChangedPixels']==maximum and d['motionQualified']is True,'aggregate metrics forged')
    artifact(ctx,d['contactSheet'],deadline)
    return manifest

def receipt_fields(ctx,row,i,deadline_ns,expected_driver=None):
    validate(row,schema('pc2-owned-clip-v1.schema.proposed.json'))
    products,targets=original_admission(ctx);target=targets[i]
    need(row['products']==products and all(row['target'][k]==v for k,v in target.items()),'clip original target/products mismatch')
    need(row['request']==dict(instance=i,output=f'hb_arrival_peer{i}.mp4',seconds=3,requestedFps=30),'clip request swapped')
    need(row['artifact']['path']==row['request']['output'],'clip artifact path mismatch')
    need(row['deadlineMonotonicNs']==deadline_ns,'clip deadline changed')
    cap=row['capture'];d=row['decode'];close=row['closure']
    need(row['startedNs']<=cap['startedNs']<cap['finishedNs']<=row['finishedNs']<row['deadlineMonotonicNs'],'clip completion/deadline chronology')
    need(cap['expectedRequestSeq']==cap['doneSeq'] and cap['encodedFps']==cap['gameFpsBefore']/cap['requestedInterval'],'native completion/fps mismatch')
    need((d['width'],d['height'])==(cap['width'],cap['height']) and d['artifactSha256']==row['artifact']['sha256'] and d['metricsAlgorithmSha256']==products['metrics']['sha256'],'decode provenance mismatch')
    driver=row['driver']
    if expected_driver is None:need(driver['pid']==os.getpid(),'foreign media driver')
    else:
        validate(expected_driver,schema('pc2-owned-clip-v1.schema.proposed.json')['$defs']['processIdentity'])
        need(driver==expected_driver,'original independently admitted media driver changed')
    ids=[]
    for role in ('helper','encoder','verifier'):
        proc=row['processes'][role];ident=proc['identity'];ids.append((ident['pid'],ident['creationTicks']))
        wanted=products['clipCli' if role=='helper' else role]
        need(ident['imageSha256']==wanted['sha256'] and Path(ident['imagePath'])==Path(wanted['packagePath']),'foreign media process image')
        need(ident['parentPid']==(driver['pid'] if role=='helper' else row['processes']['helper']['identity']['pid']),'media process parent changed')
        need(row['startedNs']<=proc['startedNs']<proc['finishedNs']<=proc['absenceCheckedNs']<=close['checkedNs']<=row['finishedNs'],'media process completion chronology')
    need(len(set(ids))==3 and all(pid not in (driver['pid'],*tuple(x['pid']for x in targets.values())) for pid,_ in ids),'media process identity reused')
    return row

def receipt_artifacts(ctx,row,deadline,recompute=True):
    cap=row['capture']
    native=strict_json(artifact(ctx,row['rawCliReceipt'],deadline))
    need(type(native)is dict and native.get('target')==row['target'] and native.get('capture')==cap,'raw CLI target/native completion mismatch')
    for role in ('encoder','verifier'):need(native.get('processes',{}).get(role)==row['processes'][role],'raw CLI child receipt mismatch')
    for logs in row['rawLogs'].values():
        for log in logs.values():artifact(ctx,log,deadline,empty=True)
    artifact(ctx,row['artifact'],deadline)
    if recompute:frames(ctx,row,deadline)
    return row

def receipt(ctx,row,i,deadline,recompute=True,receipt_deadline_ns=None):
    committed(ctx,deadline);receipt_fields(ctx,row,i,math.floor(deadline*1e9) if receipt_deadline_ns is None else receipt_deadline_ns)
    return receipt_artifacts(ctx,row,deadline,recompute)

def capture(runner,ctx,i,deadline,clip):
    from PIL import Image
    committed(ctx,deadline);out=ctx.run_dir/f'hb_arrival_peer{i}.png';need(not out.exists(),'arrival capture already exists')
    started=time.monotonic_ns()
    reply=runner.kh2ctl('capture','--out',str(out),pid=ctx.pc2_media_targets[i]['pid'],timeout=min(5,deadline-time.monotonic()),check=False)
    committed(ctx,deadline)
    need(reply.get('ok')is True and type(reply.get('processId'))is int and reply['processId']==ctx.pc2_media_targets[i]['pid'] and Path(reply.get('path','')).resolve()==out.resolve(),'capture owned PID/path mismatch')
    for k,v in dict(nativeStatus=0,framesWritten=1).items():need(type(reply.get(k))is int and reply[k]==v,'capture completion mismatch')
    need(type(reply.get('expectedRequestSeq'))is int and reply['expectedRequestSeq']>0 and reply['doneSeq']==reply['expectedRequestSeq'] and reply['expectedRequestSeq']>clip['capture']['doneSeq'],'stale capture completion')
    need(all(type(reply.get(k))is int and reply[k]==clip['capture'][k] for k in ('width','height','renderer','backbufferFormat')),'capture renderer/dimensions changed')
    data=regular(out.resolve()).read_bytes();committed(ctx,deadline)
    with Image.open(io.BytesIO(data))as im:
        need(im.format=='PNG' and im.size==(reply['width'],reply['height']),'arrival PNG invalid')
        rgb=im.convert('RGB');black,_=frame_metrics(rgb,rgb)
        need(black<.99,'black arrival PNG')
    committed(ctx,deadline)
    a=dict(path=out.name,bytes=len(data),sha256=digest(data));artifact(ctx,a,deadline)
    return dict(instance=i,target=copy.deepcopy(ctx.pc2_media_targets[i]),startedNs=started,finishedNs=time.monotonic_ns(),receipt=reply,artifact=a)

def observe(runner,ctx,anchor,checkpoint,service,total_deadline):
    started=time.monotonic();deadline=min(total_deadline,started+45)
    ctx._story_media_deadline=deadline
    result=dict(schema='hb-arrival-media-v2',qualified=False,acceptance=False,deadlineMonotonicNs=math.floor(deadline*1e9),acquisitionDeadlineNs=math.floor(deadline*1e9),acquisitionStartedNs=math.floor(started*1e9),clips=[],captures=[],clipCalls=[],startedNs=math.floor(started*1e9))
    # Snapshot every verified auxiliary digest once; finalization rehashes them
    # after worker join. Full PNG decode/metrics runs before the initial decision.
    try:
        for i in (0,1):
            committed(ctx,deadline);service.require();checkpoint(f'peer{i}-before-clip')
            out=ctx.run_dir/f'hb_arrival_peer{i}.mp4';need(not out.exists(),'clip output already exists')
            begun=time.monotonic_ns();row=copy.deepcopy(runner.pc2_owned_clip(ctx,i,out,deadline));returned=time.monotonic_ns();committed(ctx,deadline)
            result['clipCalls'].append(dict(instance=i,startedNs=begun,returnedNs=returned))
            need(begun<=row['startedNs']<row['finishedNs']<=returned,'mixed clip clock or operation timestamps outside source call')
            result['clips'].append(row);receipt_fields(ctx,row,i,result['deadlineMonotonicNs']);committed(ctx,deadline);checkpoint(f'peer{i}-after-clip');service.require()
            checkpoint(f'peer{i}-before-png');shot=capture(runner,ctx,i,deadline,row);checkpoint(f'peer{i}-after-png');service.require();result['captures'].append(shot)
        checkpoint('paired-media-complete');committed(ctx,deadline)
        result['acquisitionFinishedNs']=time.monotonic_ns()
        # Retention is part of acquisition. Never renew an expired acquisition.
        (ctx.run_dir/'native_sync_media.json').write_text(canonical(result)+'\n',encoding='utf-8');committed(ctx,deadline)
        validation_start=time.monotonic();committed(ctx,deadline)
        validation_deadline=min(total_deadline,validation_start+30)
        result.update(validationStartedNs=math.floor(validation_start*1e9),validationDeadlineNs=math.floor(validation_deadline*1e9))
        ctx._story_media_validation=(result['acquisitionStartedNs'],result['acquisitionDeadlineNs'],result['validationStartedNs'],result['validationDeadlineNs'],math.floor(total_deadline*1e9))
        deadline=validation_deadline
        for i,row in enumerate(result['clips']):
            receipt(ctx,row,i,deadline,receipt_deadline_ns=result['acquisitionDeadlineNs']);service.require()
        checkpoint('paired-media-validated');committed(ctx,deadline)
        result['validationFinishedNs']=time.monotonic_ns()
        result.update(qualified=True,finishedNs=time.monotonic_ns());return result
    except Exception as error:
        result.update(qualified=False,error=str(error));ctx.pc2_media_cancel.set();raise
    finally:
        (ctx.run_dir/'native_sync_media.json').write_text(canonical(result)+'\n',encoding='utf-8')
        if 'native_sync_media.json'not in ctx.artifacts:ctx.artifacts.append('native_sync_media.json')
        if result['qualified']:committed(ctx,deadline)

def history(media,handler_deadline_ns,terminal=False):
    need(media.get('schema')=='hb-arrival-media-v2','two-phase media history required')
    names=('startedNs','acquisitionStartedNs','acquisitionDeadlineNs','deadlineMonotonicNs','acquisitionFinishedNs','validationStartedNs','validationDeadlineNs','validationFinishedNs','finishedNs')
    need(type(handler_deadline_ns)is int and all(type(media.get(k))is int and media[k]>0 for k in names),'typed QPC phase history required')
    s,a,f,v,d,z=(media[k]for k in ('acquisitionStartedNs','acquisitionDeadlineNs','acquisitionFinishedNs','validationStartedNs','validationDeadlineNs','validationFinishedNs'))
    need(media['startedNs']==s and media['deadlineMonotonicNs']==a,'original acquisition deadline alias changed')
    # At most two ns of float/QPC conversion rounding; never a temporal grace.
    need(abs(a-min(handler_deadline_ns,s+45_000_000_000))<=2 and abs(d-min(handler_deadline_ns,v+30_000_000_000))<=2,'phase deadline formula changed')
    need(s<f<=v<a and v<z<=media['finishedNs']<d<=handler_deadline_ns,'original phase chronology invalid')
    need(len(media['clips'])==len(media['captures'])==len(media['clipCalls'])==2,'paired acquisition history required')
    for i,(clip,shot,call)in enumerate(zip(media['clips'],media['captures'],media['clipCalls'])):
        need(call['instance']==i and all(type(call.get(k))is int for k in ('startedNs','returnedNs')),'typed clip call history required')
        need(s<=call['startedNs']<=clip['startedNs']<clip['finishedNs']<=call['returnedNs']<=shot['startedNs']<shot['finishedNs']<=f<a,'acquisition outside original deadline')
        if i:need(media['captures'][i-1]['finishedNs']<=call['startedNs'],'same ordered acquisition history changed')
    if 'finalizedNs'in media:need(type(media['finalizedNs'])is int and media['finishedNs']<=media['finalizedNs']<d,'late original finalization')
    if terminal:need(type(media.get('sourceFinishedNs'))is int and media['finalizedNs']<=media['sourceFinishedNs']<d,'original terminal validation completion missing/late')
    return d/1e9

def validation_deadline(ctx,media,total_deadline):
    deadline=history(media,math.floor(total_deadline*1e9))
    need(ctx._story_media_validation==(media['acquisitionStartedNs'],media['acquisitionDeadlineNs'],media['validationStartedNs'],media['validationDeadlineNs'],math.floor(total_deadline*1e9)),'original committed phase history replaced')
    committed(ctx,deadline);return deadline

def outer_history(result,terminal=False):
    """Bind phase history to original handler and sole retained warp request."""
    names=('handlerStartedNs','handlerDeadlineMonotonicNs','requestPreparedNs','requestStartedNs','requestFinishedNs')
    need(all(type(result.get(k))is int and result[k]>0 for k in names),'typed original handler/request history required')
    start,limit,prepared,issued,completed=(result[k]for k in names)
    need(abs(limit-(start+180_000_000_000))<=2,'original handler180 history invalid')
    need(result.get('requestDispatched')is True,'original sole request dispatch missing')
    need(start<=prepared<=issued<completed<=result['media']['acquisitionStartedNs'],'original handler/request/acquisition chronology invalid')
    def within_handler(value):
        if type(value)is dict:
            for key,child in value.items():
                if key.endswith('Ns'):need(type(child)is int and start<=child<=limit,'media timestamp outside original handler: '+key)
                else:within_handler(child)
        elif type(value)is list:
            for child in value:within_handler(child)
    within_handler(result['media'])
    return history(result['media'],limit,terminal=terminal)

def finalize(runner,ctx,result,total_deadline):
    outer_history(result)
    media=result['media'];deadline=validation_deadline(ctx,media,total_deadline)
    need(result['status']=='READY_FOR_CLOSURE' and result['readHandlesClosed'] and not result.get('serviceFailure'),'source cleanup not ready')
    need(media['qualified']is True and len(media['clips'])==len(media['captures'])==2,'paired media incomplete')
    committed(ctx,deadline)
    for i,(clip,shot)in enumerate(zip(media['clips'],media['captures'])):
        receipt(ctx,clip,i,deadline,recompute=False,receipt_deadline_ns=media['acquisitionDeadlineNs'])
        # Recheck ALL exact decoded PNG digests, not just manifest/video.
        manifest=strict_json(artifact(ctx,clip['decode']['frameDigestManifest'],deadline))
        for row in manifest['rows']:artifact(ctx,row['png'],deadline)
        artifact(ctx,clip['decode']['contactSheet'],deadline)
        need(shot['instance']==i and shot['target']==ctx.pc2_media_targets[i],'paired capture identity changed')
        artifact(ctx,shot['artifact'],deadline)
    require_hooks(runner,ctx);committed(ctx,deadline)
    media['finalizedNs']=time.monotonic_ns();need(media['finalizedNs']<media['validationDeadlineNs'],'late media finalization')


def replay_media(run_dir,result,expected_products,expected_targets,verification_deadline,expected_driver):
    """Read-only post-closure recheck; never changes the original run deadline.

    expected_* MUST come from combined pinned manifest/original launch admission,
    not result fields. expected_driver is the independently retained original
    driver identity, not the current replay process or receipt self-report.
    Caller separately verifies raw source5 proof and global
    process/port/lock/inventory closure. Return is engineering media proof only.
    """
    from types import SimpleNamespace
    import threading
    need(result['status']=='READY_FOR_CLOSURE' and result['acceptance']is False and result['mediaQualified']is True and result['sourceDiskUnchanged']is True,'source not ready for independent closure')
    targets=result['mediaTargets'];need(set(targets) in ({0,1},{'0','1'}),'retained paired target keys malformed')
    need(result['mediaProducts']==expected_products and {int(k):v for k,v in targets.items()}==expected_targets,'combined original products/targets mismatch')
    ctx=SimpleNamespace(run_dir=Path(run_dir).resolve(),artifacts=[],pc2_media_products=copy.deepcopy(expected_products),pc2_media_targets=copy.deepcopy(expected_targets),pc2_media_cancel=threading.Event())
    ctx._story_media_commit=(copy.deepcopy(expected_products),copy.deepcopy(expected_targets),ctx.pc2_media_cancel)
    media=result['media'];need(media['qualified']is True and len(media['clips'])==len(media['captures'])==2,'paired media required')
    outer_history(result,terminal=True)
    require_hooks(type('ReadOnlyHook',(),{'pc2_owned_clip':staticmethod(lambda *a:None)})(),ctx);committed(ctx,verification_deadline)
    need(len(media['clipCalls'])==2,'paired source QPC call windows required')
    for i,(clip,shot)in enumerate(zip(media['clips'],media['captures'])):
        window=media['clipCalls'][i];need(window['instance']==i and window['startedNs']<=clip['startedNs']<clip['finishedNs']<=window['returnedNs']<=shot['startedNs'],'mixed clock/source call window invalid')
        receipt_fields(ctx,clip,i,media['deadlineMonotonicNs'],expected_driver);receipt_artifacts(ctx,clip,verification_deadline)
        need(shot['instance']==i and shot['target']==expected_targets[i] and shot['artifact']['path']==f'hb_arrival_peer{i}.png','paired PNG provenance mismatch')
        need(clip['finishedNs']<shot['startedNs']<shot['finishedNs']<=media['acquisitionFinishedNs'],'clip/PNG chronology invalid')
        artifact(ctx,shot['artifact'],verification_deadline)
    committed(ctx,verification_deadline)
    return dict(schema='hb-arrival-media-replay-v2',qualified=True,acceptance=False,originalDeadlineMonotonicNs=media['deadlineMonotonicNs'],originalValidationDeadlineNs=media['validationDeadlineNs'],originalValidationFinishedNs=media['sourceFinishedNs'],verifiedArtifactCount=len(ctx.artifacts))
