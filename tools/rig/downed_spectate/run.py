"""Sealed VUH-1819 fixture. --check is offline; --execute requires exact adoption."""
import argparse, ctypes, hashlib, importlib.util, json, mmap, os, pathlib, re, shutil, subprocess, sys, time

def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def load(path,name):
    s=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(s)
    sys.modules[name]=m; s.loader.exec_module(m); return m
def write(p,d): p.write_text(json.dumps(d,indent=2)+'\n')

def pointer_value(value):
    if isinstance(value,bool) or not isinstance(value,(int,str)):raise ValueError('invalid pointer type')
    if isinstance(value,str):
        if not re.fullmatch(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)',value):raise ValueError('invalid pointer text')
        value=int(value,16 if value.lower().startswith('0x') else 10)
    if not 0<=value<2**64:raise ValueError('pointer outside u64')
    return value

def check_cli_root(binary,root):
    roots={m.group().decode('ascii') for m in re.finditer(rb'[A-Za-z]:[/\\][^\x00]{0,200}?kh2-multiplayer[^\x00]{0,200}',binary)}
    if len(roots)!=1 or pathlib.Path(next(iter(roots))).resolve()!=root.resolve():
        raise RuntimeError('kh2ctl source root differs from canonical runner: '+repr(sorted(roots)))
    return str(root.resolve())

class Camera(ctypes.Structure):
    _fields_=[(n,ctypes.c_uint32) for n in ('magic','version','sequence','installed','frame','active','slot','cycles')]+\
        [(n,ctypes.c_uint64) for n in ('actorDuring','actorBefore','actorAfter','localActor','episode')]+\
        [(n,ctypes.c_uint32) for n in ('calls','overrides','generation','transition','load','mode','released','aimSuppressed')]

def camera_snap(mapping):
    for _ in range(100):
        first=int.from_bytes(mapping[8:12],'little')
        if first & 1: continue
        c=Camera.from_buffer_copy(mapping[:104])
        if first==int.from_bytes(mapping[8:12],'little')==c.sequence and first:
            return {n:getattr(c,n) for n,_ in Camera._fields_}
    raise RuntimeError('camera receipt unavailable/torn')

def judge_target(c,slot,actor,body,down):
    return (c['installed']==1 and c['active']==1 and c['slot']==slot and c['mode']==0 and
            c['actorDuring']==actor and c['actorBefore']==body and c['actorAfter']==body and
            c['localActor']==body and c['episode']==down['episode'] and c['generation']>0 and
            down['state']=='downed' and down['hp']==0 and down['flags9B8']&4 and
            down['deadAction']==1 and down['controllerOff']==1 and not down['gameOverTask'])

def observe_avatar(executable,pid,error_type=RuntimeError):
    p=subprocess.run([str(executable),'observe','--pid',str(pid),'--samples','1'],
        capture_output=True,text=True,timeout=5,creationflags=subprocess.CREATE_NO_WINDOW)
    if p.returncode:raise error_type('avatar observer failed')
    return json.loads(p.stdout)['samples'][0]

def verify(packet):
    seal=json.loads((packet/'seal.json').read_text()); spec=json.loads((packet/'packet.json').read_text())
    for name,digest in seal['files'].items():
        path=(packet/name).resolve()
        if not path.is_relative_to(packet) or sha(path)!=digest: raise RuntimeError('packet pin mismatch: '+name)
    for name,digest in spec['dependencies'].items():
        if sha(pathlib.Path(spec['repositoryRoot'])/name)!=digest: raise RuntimeError('dependency pin mismatch: '+name)
    if sha(spec['gamePath'])!=spec['gameSha256']:raise RuntimeError('game executable pin mismatch')
    return spec

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('--packet',type=pathlib.Path,required=True)
    ap.add_argument('--check',action='store_true'); ap.add_argument('--execute',action='store_true')
    ap.add_argument('--dry-run-handoff',action='store_true'); ap.add_argument('--protected-saves-baseline',type=pathlib.Path)
    a=ap.parse_args(); packet=a.packet.resolve(); spec=verify(packet); root=pathlib.Path(spec['repositoryRoot'])
    sys.path.insert(0,str(root/'tools/scenario'))
    r=load(root/'tools/scenario/run.py','spectate_canonical_runner')
    h=load(packet/'party-save-helpers.py','spectate_setup_helpers')
    d=load(packet/'downed-path.py','spectate_existing_downed_path')
    guard=load(packet/'saveguard-judge.py','spectate_saveguard_judge')
    for key,attr in [('kh2ctl','KH2CTL'),('runtime','RUNTIME'),('server','SERVER'),('avatarctl','AVATARCTL')]:
        setattr(r,attr,packet/'products'/spec['products'][key]['name'])
    check_cli_root(r.KH2CTL.read_bytes(),root)
    if r.RIG.resolve()!=(root/'build/rig').resolve() or r.LOGS.resolve()!=(root/'build/rig/logs').resolve() or r.LOCK.resolve()!=(root/'build/rig/rig.lock').resolve():
        raise RuntimeError('canonical runner rig state differs')
    r.RUNS=packet/'output'/'scenarios'
    launches=[]; helpers=[]; manifests=[]; samples=[]
    native_cli=r.kh2ctl; native_start=r.start_process
    def start(*args,**kwargs):
        p=native_start(*args,**kwargs); helpers.append(p); return p
    r.start_process=start
    def cli(*args,**kw):
        if args[0]=='launch':
            if len(launches)>=2: raise r.StepFailed('exactly two launches allowed')
            args=(args[0],'--dll',str(packet/'products'/spec['products']['dll']['name']),*args[1:])
        result=native_cli(*args,**kw)
        if args[0]=='launch': launches.append(result)
        out=packet/'output'
        if out.exists():
            with (out/'control.jsonl').open('a') as f: f.write(json.dumps(dict(args=args,pid=kw.get('pid'),result=result))+'\n')
        return result
    r.kh2ctl=cli
    r.foreign_kh2=lambda:[x['processId'] for x in cli('instances')['instances']]
    # Reuse the native menu3 ordering from the VUH-1504 fixture.
    old_boot,old_press=r.STEPS['boot'],r.press
    def boot(ctx,step):
        n=0
        def press(inst,button):
            nonlocal n
            if button=='cross':
                n+=1
                if n==2:
                    r.wait_for(ctx,lambda:ctx.namespace()['peek'](0x7435D0,'u8',inst.index)==3,'native menu3',30,.1)
                    return cli('player-press','--button','cross','--duration-ms','500',pid=inst.pid)
            return old_press(inst,button)
        r.press=press
        try:return old_boot(ctx,step)
        finally:r.press=old_press
    r.STEPS['boot']=boot
    def observe(ctx,i):
        return observe_avatar(r.AVATARCTL,ctx.inst(i).pid,r.StepFailed)
    def log(ctx,i):return ctx.inst(i).inject_log.read_text(errors='replace')
    def setup(ctx,step):
        offsets=[len(log(ctx,i)) for i in range(2)]; loads=[log(ctx,i).count('[warp] load complete') for i in range(2)]
        r.wait_for(ctx,lambda:all('plan=one-clone' in log(ctx,i) for i in range(2)),'one-clone plan admitted',45,.25)
        r.STEPS['warp'](ctx,dict(instance=0,world=4,room=26,door=0,map=0,btl=0,evt=0))
        def ready():
            for i in range(2):
                if log(ctx,i).count('[warp] load complete')<=loads[i] or not h.goa_applied(log(ctx,i)[offsets[i]:],i):return False
                if not h.goa_party_gate(ctx.entities(i)['actors'],i):return False
                s=observe(ctx,i); p=s['puppets'][0]['value']
                if not s['complete'] or not s['identityStable'] or not p or not p['active'] or not p['provenanceMatchesWorld']:return False
                if s['worldBefore']['localSlot']!=i or p['pose']['ownerSlot']!=1-i:return False
            return True
        r.wait_for(ctx,ready,'fresh GoA kit-aware one-clone on both games',60,.25)
        cli('player-input','--lx','1','--ly','0','--duration-ms','600',pid=ctx.inst(1).pid)
        ctx.sleep(.5) # distinct target views, beyond the unchanged avatar render delay
        if '[spectate] configure installed=1' not in log(ctx,0) or '[spectate]' in log(ctx,1):
            raise r.StepFailed('candidate install/default-off control failed')
        write(ctx.run_dir/'setup.json',dict(actors=[ctx.entities(i) for i in range(2)],world=[observe(ctx,i) for i in range(2)]))
        return dict(ok=True,room=[4,26],hostKit=84,friendKit=90)
    r.STEPS['spectate_setup']=setup
    def probe(ctx,step):
        pid=ctx.inst(0).pid
        actors=ctx.entities(0)['actors']
        body=[x for x in actors if x.get('objectId')==84 and x.get('team')==1]
        peer=[x for x in actors if x.get('objectId')==90 and x.get('team')==0]
        if len(body)!=1 or len(peer)!=1: raise r.StepFailed('unique body/kit-matched puppet required')
        body=int(body[0]['address'],16); peer=int(peer[0]['address'],16)
        down=d.open_channel(pid)
        cam=mmap.mmap(-1,104,tagname=f'Local\\kh2coop_spectate_{pid}',access=mmap.ACCESS_READ)
        try:
            c=camera_snap(cam)
            if c['magic']!=0x53504543 or c['version']!=1 or c['active']:raise r.StepFailed('camera channel layout/alive negative control')
            killed=d.command(ctx,r,down,d.CMD_KILL)
            if killed['result']!='ok' or killed['state']!='downed':raise r.StepFailed('native VUH1504 kill did not hold downed')
            cli('player-input','--lx','0','--ly','0','--duration-ms','150',pid=pid) # neutral R3 before first cycle
            def target(slot,actor,name):
                first=camera_snap(cam)['calls']
                r.wait_for(ctx,lambda:camera_snap(cam)['calls']>first and judge_target(camera_snap(cam),slot,actor,body,d.snap(down)),name,10,.05)
                before=camera_snap(cam); dead=d.snap(down)
                r.STEPS['capture'](ctx,dict(instance=0,name=name))
                after=camera_snap(cam)
                if not judge_target(after,slot,actor,body,d.snap(down)) or before['cycles']!=after['cycles']:raise r.StepFailed('target changed across screenshot')
                raw_value=ctx.namespace()['peek'](0x718CB0,'u64',0)
                sample=dict(name=name,before=before,after=after,downed=dead,rawCameraValue=raw_value,
                            rawCameraValueType=type(raw_value).__name__)
                samples.append(sample)
                try:raw=pointer_value(raw_value);sample['rawCameraPointer']=raw
                except ValueError as e:sample['pointerParseError']=str(e);raise
                finally:write(ctx.run_dir/'spectate-samples.json',samples)
                # Scoped native receipt is the authority; an out-of-call peek
                # may catch either phase, but must never see another actor.
                if raw not in (body,actor):raise r.StepFailed('unexpected raw camera pointer')
                return after
            first=target(1,peer,'spectate_teammate_roxas')
            cli('player-press','--button','r3','--duration-ms','700',pid=pid)
            own=target(0,body,'spectate_own_downed_body')
            if own['cycles']!=first['cycles']+1:raise r.StepFailed('held R3 cycled more than once')
            cli('player-press','--button','r3','--duration-ms','150',pid=pid)
            again=target(1,peer,'spectate_teammate_again')
            if again['cycles']!=own['cycles']+1:raise r.StepFailed('R3 did not rearm')
            revived=d.command(ctx,r,down,d.CMD_REVIVE)
            if revived['result']!='ok' or revived['state']!='ready' or revived['hp']!=d.revive_target(revived['maxHp']):
                raise r.StepFailed('native revive failed')
            before=camera_snap(cam)
            r.wait_for(ctx,lambda:camera_snap(cam)['calls']>before['calls'] and not camera_snap(cam)['active'],'camera release after revive',10,.05)
            released=camera_snap(cam)
            raw_release_value=ctx.namespace()['peek'](0x718CB0,'u64',0)
            raw_release=pointer_value(raw_release_value)
            write(ctx.run_dir/'spectate-release.json',dict(receipt=released,rawCameraValue=raw_release_value,rawCameraPointer=raw_release,body=body))
            if released['slot']!=255 or released['actorAfter']!=body or raw_release!=body:
                raise r.StepFailed('camera pointer did not return to native owner')
            r.STEPS['capture'](ctx,dict(instance=0,name='spectate_revived_release'))
            write(ctx.run_dir/'spectate-result.json',dict(status='PASS',samples=samples,revived=revived,released=released,
                limit='two-player GoA kit-aware clone plus own body; second remote, live disconnect/downed-target and retry not exercised'))
            return dict(ok=True,targets=[1,0,1],release=True)
        finally:cam.close();down.close()
    r.STEPS['spectate_probe']=probe
    scenario=packet/'scenario.json'; r.validate_scenario(json.loads(scenario.read_text()))
    if a.dry_run_handoff:
        print(json.dumps(h.validate_save_baseline(a.protected_saves_baseline,r.SAVE_DIR,r.hash_saves())));return 0
    if a.check and not a.execute:print(json.dumps(dict(ok=True,scope='offline pins/schema')));return 0
    if not a.execute:raise RuntimeError('select --check or --execute')
    adopt=json.loads((packet/'adopt.json').read_text(encoding='utf-8-sig'))
    if adopt.get('verdict')!='ADOPT' or adopt.get('sealSha256')!=sha(packet/'seal.json'):raise RuntimeError('exact lead adoption required')
    if (root/'build/rig/rig.lock').exists() or (root/'build/rig/ctest.lock').exists():raise RuntimeError('rig/ctest busy')
    sid=ctypes.c_ulong()
    if not ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(),ctypes.byref(sid)) or sid.value!=ctypes.windll.kernel32.WTSGetActiveConsoleSessionId() or not sid.value:
        raise RuntimeError('active desktop required')
    h.validate_save_baseline(a.protected_saves_baseline,r.SAVE_DIR,r.hash_saves())
    output=packet/'output';output.mkdir(exist_ok=False)
    saved=dict(os.environ)
    for k in list(os.environ):
        if k.startswith('KH2COOP_') or k=='KH2_GAME_DIR':del os.environ[k]
    os.environ.update(spec['commonEnv'])
    original_hash=r.hash_saves
    def hashes():
        if (root/'build/rig/ctest.lock').exists():raise RuntimeError('test lock appeared under live lock')
        v=original_hash();h.validate_save_baseline(a.protected_saves_baseline,r.SAVE_DIR,v)
        write(output/('saves-before.json' if not manifests else 'saves-after.json'),v);manifests.append(v);return v
    r.hash_saves=hashes
    oldargv=sys.argv;sys.argv=[str(root/'tools/scenario/run.py'),str(scenario)]
    try:result=r.main()
    finally:sys.argv=oldargv;os.environ.clear();os.environ.update(saved)
    gates=[]
    for launch in launches:
        pid=launch['processId'];own=pathlib.Path(launch.get('launcherLog',''))
        if not own.is_file() or own.resolve().parent!=r.LOGS.resolve():gates.append(dict(ok=False));continue
        text=(r.LOGS/f'kh2coop_inject_{pid}.log').read_text(errors='replace')
        (output/f'inject-{pid}.log').write_text(text);shutil.copyfile(own,output/own.name)
        gates.append(guard.judge(launch,text,own.read_text()))
    remaining={x['processId'] for x in cli('instances')['instances']}
    closed=all(x['processId'] not in remaining for x in launches) and all(p.poll() is not None for p in helpers)
    equal=len(manifests)==2 and manifests[0]==manifests[1]
    if len(gates)!=2 or not all(x['ok'] for x in gates) or not closed or not equal:result=1
    write(output/'closure.json',dict(exitCode=result,launchGates=gates,ownedClosed=closed,savesUnchanged=equal))
    return result
if __name__=='__main__':raise SystemExit(main())
