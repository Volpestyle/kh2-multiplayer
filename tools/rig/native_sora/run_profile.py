"""Default-off GoA native Sora visual profile. No package or injected-code change."""
import argparse, ctypes, hashlib, importlib.util, json, math, mmap, os, re, socket, struct, subprocess, sys, threading, time
from pathlib import Path
sys.dont_write_bytecode=True
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
from party_leaf import PartyLeaf, Refused, ROW_RVA, TARGET_RVA, ORIGINAL
from native_read import ReadOnly, sha

def save(path,data): Path(path).write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')
def tree(path):
    path=Path(path)
    return {str(f.relative_to(path)):sha(f) for f in sorted(path.rglob('*')) if f.is_file()} if path.exists() else {}
def sandbox_inventory(logs): return {p.name:tree(p) for p in sorted(logs.glob('save_sandbox_*')) if p.is_dir()}
def load_runner():
    spec=importlib.util.spec_from_file_location('native_sora_canonical',ROOT/'tools/scenario/run.py')
    r=importlib.util.module_from_spec(spec);sys.modules[spec.name]=r;spec.loader.exec_module(r);return r

class Profile:
    def __init__(self,r,products,deadline):
        self.r,self.products,self.deadline=r,products,deadline
        self.raw=r.kh2ctl;self.ctx=None;self.ram=None;self.hostram=None;self.index=1;self.leaf=None;self.helpers=[]
        self.receipt_lock=threading.Lock()
        self.rows=[];self.killed=set();self.cleaning=False;self.launches=0
    def cli(self,*args,pid=None,check=True,timeout=120,env=None):
        if args[0]=='kill' and pid in self.killed:return {'ok':True,'alreadyClosedByProfile':True}
        if not self.cleaning:self.check()
        if args[0]=='launch':
            if self.launches>=2:raise Refused('two launches only')
            self.launches+=1
            args=(*args,'--dll',self.products['dll']['path'])
            env={k:v for k,v in os.environ.items() if not k.startswith('KH2COOP_')}
            env.update({'KH2COOP_LOG_DIR':str(self.r.LOGS),'KH2COOP_PUPPET_TRACE':'1'})
        if args[0] in ('poke','hit','kill-enemy','resync','progress'):
            raise Refused('writer/control outside declared profile')
        budget=20 if self.cleaning else max(.01,min(timeout,self.deadline-time.monotonic()))
        cmd=[str(self.r.KH2CTL),*map(str,args)]+(['--pid',str(pid)] if pid is not None else [])
        done=subprocess.run(cmd,capture_output=True,text=True,stdin=subprocess.DEVNULL,timeout=budget,env=env)
        try:data=json.loads(done.stdout.strip().splitlines()[-1])
        except Exception:data={'ok':False,'error':'unparsed CLI result'}
        self.emit('cli',{'argv':cmd,'exit':done.returncode,'stdout':done.stdout,'stderr':done.stderr,'parsed':data})
        if check and (done.returncode!=0 or data.get('ok') is not True):raise self.r.StepFailed(str(data))
        if not self.cleaning:self.check()
        return data
    def emit(self,phase,data):
        with self.receipt_lock:
            row={'phase':phase,'monotonic':time.monotonic(),'data':data};self.rows.append(row)
            if self.ctx:
                with (self.ctx.run_dir/'party-receipts.jsonl').open('a',encoding='utf-8') as f:f.write(json.dumps(row)+'\n')

    def check(self):
        if time.monotonic()>self.deadline:raise Refused('nonrenewable540s experimental deadline')
        if self.ctx:self.ctx.check_all()

    def poke(self,rva,value):
        if rva!=TARGET_RVA or value not in (0,1):raise Refused('exact u8 leaf boundary')
        self.ram.identity()
        if not self.cleaning:self.check()
        cmd=[str(self.r.KH2CTL),'poke','--pid',str(self.ctx.inst(self.index).pid),'--rva',hex(TARGET_RVA),'--type','u8','--value',str(value)]
        start=time.monotonic()
        try:
            done=subprocess.run(cmd,capture_output=True,text=True,stdin=subprocess.DEVNULL,timeout=5)
            self.emit('poke_raw',{'argv':cmd,'start':start,'end':time.monotonic(),'exit':done.returncode,'stdout':done.stdout,'stderr':done.stderr})
            result=json.loads(done.stdout.strip().splitlines()[-1])
            if done.returncode!=0:result['ok']=False
            if not self.cleaning:self.check()
            return result
        except BaseException as e:
            self.emit('poke_failed',{'argv':cmd,'error':repr(e),'start':start,'end':time.monotonic()});raise

    def log(self):return self.ctx.inst(self.index).inject_log.read_text(errors='replace')

    def safe(self):
        self.check();b=self.ram.base;read=self.ram.value
        fields=[(0x717008,'B'),(0x717009,'B'),(0x71700A,'B'),(0x71700C,'H'),(0x71700E,'H'),(0x717010,'H')]
        loc=[read(b+off,fmt) for off,fmt in fields]
        g=[read(b+off,fmt) for off,fmt in [(0x9BA8D0,'B'),(0x7435D0,'B'),(0x2A171E8,'i'),(0xB65210,'i'),(0x2A11478,'Q')]]
        self.emit('safety',{'location':loc,'gameplay':g,'frame':self.ctx.inst(self.index).live_frame()})
        if loc!=[4,26,0,0,0,0] or g[0]==0 or g[1:]!=[255,0,0,0]:raise Refused('unsafe/non-GoA context')
        if loc!=[read(b+off,fmt) for off,fmt in fields]:raise Refused('location read changed')
        return loc

    def actors(self,expect_clone):
        self.safe();self.ram.identity();b=self.ram.base
        head=self.ram.value(b+0x2A171C8,'Q')
        data=self.cli('entities',pid=self.ctx.inst(self.index).pid,timeout=5)
        rows=data.get('actors',[]);addresses=[int(a['address'],16) for a in rows]
        if not rows or len(addresses)!=len(set(addresses)) or addresses[0]!=head or [data.get('world'),data.get('room')]!=[4,26]:raise Refused('native active-list/local-head ambiguity')
        if head!=self.ram.value(b+0x2A171C8,'Q'):raise Refused('native list head changed')
        local=rows[0]
        if local['name']!='P_EX100' or local['objectType']!=0 or local['hp']<=0:raise Refused('local Sora identity/HP')
        clones=[a for a in rows[1:] if a.get('objectType')==0]
        if len(clones)!=(1 if expect_clone else 0):raise Refused('expected unique clone absent/ambiguous')
        identities=[]
        for actor in [local]+clones:
            address=int(actor['address'],16);obj=self.ram.value(address+0x918,'Q')
            raw=self.ram.read(obj,0x48)
            name=raw[8:40].split(b'\0')[0].decode('ascii',errors='strict')
            mset=raw[40:72].split(b'\0')[0].decode('ascii',errors='strict')
            if raw[4]!=0 or name!='P_EX100' or actor['name']!=name or int.from_bytes(raw[:4],'little')!=actor['objectId'] or actor['hp']<=0:raise Refused('native model/type/HP mismatch')
            status=self.ram.value(address+0x5C0,'Q')
            if status!=int(actor['status'],16) or list(struct.unpack('<ii',self.ram.read(status,8)))!=[actor['hp'],actor['maxHp']]:raise Refused('native status/HP alias changed')
            if obj!=self.ram.value(address+0x918,'Q') or status!=self.ram.value(address+0x5C0,'Q'):raise Refused('objentry/status changed')
            if not all(math.isfinite(v) for v in actor['position'].values()):raise Refused('nonfinite actor position')
            identities.append({'actor':actor,'objentry':hex(obj),'model':name,'mset':mset,'raw':raw.hex()})
        self.safe()
        self.emit('native_identity',{'head':hex(head),'identities':identities,'friendPointers':[hex(self.ram.value(b+x,'Q')) for x in (0x2A239B0,0x2A239B8)]})
        return identities

    def helper(self,name,cmd):
        out=(self.ctx.run_dir/(name+'.stdout')).open('w');err=(self.ctx.run_dir/(name+'.stderr')).open('w')
        try:proc=subprocess.Popen(cmd,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
        except BaseException:out.close();err.close();raise
        item=(name,proc,out,err,cmd);self.helpers.append(item);self.emit('helper_start',{'name':name,'argv':cmd,'pid':proc.pid});return proc

    def collect(self,item,timeout):
        name,proc,out,err,cmd=item
        proc.wait(timeout=timeout);out.flush();err.flush()
        text=(self.ctx.run_dir/(name+'.stdout')).read_text();stderr=(self.ctx.run_dir/(name+'.stderr')).read_text()
        data=json.loads(text.strip().splitlines()[-1])
        self.emit('helper_done',{'name':name,'argv':cmd,'exit':proc.returncode,'stdout':text,'stderr':stderr,'parsed':data})
        if proc.returncode!=0 or data.get('ok') is not True:raise Refused(name+' failed')
        return data
    def observe(self,index):
        cmd=[self.products['avatarctl']['path'],'observe','--pid',str(self.ctx.inst(index).pid),'--samples','1']
        done=subprocess.run(cmd,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=5)
        data=json.loads(done.stdout.strip().splitlines()[-1]);self.emit('bridge_observe',{'argv':cmd,'exit':done.returncode,'data':data})
        row=data['samples'][0];p=row['puppets'][0]['value'];w=row['worldBefore']
        if not row['identityStable'] or not row['localReadAvailable'] or not p or not p['active'] or not p['provenanceMatchesWorld'] or w['localSlot']!=index or p['pose']['ownerSlot']!=1-index or not p['pose']['finite']:
            raise Refused('fresh network pose/provenance missing')
        if [p['pose']['worldId'],p['pose']['roomId']]!=[4,26]:raise Refused('pose outside GoA')
        return row
    def run(self,ctx,step):
        self.ctx=ctx;error=None;result={};rec=None;pid=ctx.inst(1).pid
        try:
            self.ram=ReadOnly(pid,self.r.RIG/'owned.txt',self.products['game']['sha256'],self.products['dll']['sha256'])
            self.emit('owned_identity',self.ram.identity())
            self.hostram=ReadOnly(ctx.inst(0).pid,self.r.RIG/'owned.txt',self.products['game']['sha256'],self.products['dll']['sha256'])
            for inst in ctx.instances:
                log=inst.inject_log.read_text(errors='replace')
                if 'Save guard installed: writes under' not in log or 'Initialization complete' not in log or re.search(r'\[saveguard\].*(redirected|blocked)',log,re.I):raise Refused('guard/init/save attempt')
            self.actors(False)
            self.leaf=PartyLeaf(lambda:self.ram.read(self.ram.base+ROW_RVA,4),self.ram.identity,self.poke,self.emit,self.log)
            self.safe();self.leaf.replace()
            count=self.log().count('[warp] load complete')
            self.r.STEPS['warp'](ctx,{'instance':1,'world':4,'room':26,'door':0,'map':0,'btl':0,'evt':0})
            if self.log().count('[warp] load complete')!=count+1:raise Refused('one replacement reload required')
            identities=self.actors(True);clone=identities[1];address=int(clone['actor']['address'],16)
            def identity_key(rows):return [(x['actor']['address'],x['actor']['status'],x['actor']['objectId'],x['objentry'],x['mset']) for x in rows]
            original=identity_key(identities)
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sock:
                sock.bind(('127.0.0.1',27794)) # no foreign listener is stopped
            self.r.STEPS['relay'](ctx,{'port':27794,'args':['--bind','127.0.0.1']})
            for i,role in [(0,'player'),(1,'friend1')]:
                self.r.STEPS['runtime'](ctx,{'instance':i,'role':role,'peerId':f'SoraVisual{i}',
                    'args':['--tick-ms','16','--max-ticks','6500']})
            runtimes=[(n,p,l) for n,p,l in ctx.processes if n.startswith('runtime_')]
            # Driver acknowledgement, not helper-start or publication timing, starts the interval.
            def bound():
                tail=self.log()[self.log().rfind('[warp] load complete'):]
                values=re.findall(r'\[puppet 0\] frame \d+ motion [^\r\n]* actor=(?:0x)?([0-9A-Fa-f]+)',tail)
                return bool(values) and int(values[-1],16)==address and '[ptrace]' in tail and '[enemysync] client arrived' in tail
            self.r.wait_for(ctx,bound,'actual native clone driver binding',15,.1)
            identities=self.actors(True);clone=identities[1];address=int(clone['actor']['address'],16);original=identity_key(identities)
            self.observe(0);self.observe(1)
            interval_log_offset=len(self.log())
            baseline_release=self.log().count('Puppet 0 released')
            start=time.monotonic();self.emit('qualified_interval_start',{'clone':clone,'seconds':60})
            rec=self.r.Recorder(ctx,[0,1]);ctx.saved['native_tracks']=rec
            for i in (0,1):
                self.helper(f'clip_{i}',[str(self.r.KH2CTL),'clip','--pid',str(ctx.inst(i).pid),'--seconds','10','--fps','30','--out',str(ctx.run_dir/f'native_sora_side{i}.mp4')])
                self.helper(f'avatar_{i}',[self.products['avatarctl']['path'],'record','--pid',str(ctx.inst(i).pid),'--seconds','65','--out',str(ctx.run_dir/f'avatar_{i}.csv')])
            cycles=0;samples=[]
            while time.monotonic()-start<60:
                self.check()
                if any(p.poll() is not None for _,p,_ in runtimes):raise Refused('runtime exited before60s')
                # Short paired horizontal motions; no portals/menu/magic or target damage commands.
                for axis in (1,-1):self.r.STEPS['input'](ctx,{'instance':0,'lx':axis,'ms':250})
                ctx.sleep(.25)
                self.cli('player-press','--button','cross','--duration-ms','150',pid=ctx.inst(0).pid)
                ctx.sleep(1.0)
                self.cli('player-press','--button','circle','--duration-ms','120',pid=ctx.inst(0).pid)
                ctx.sleep(1.0)
                a=self.actors(True)
                clientram=self.ram
                try:
                    self.ram=self.hostram;self.index=0;self.actors(False)
                finally:self.ram=clientram;self.index=1
                if identity_key(a)!=original:raise Refused('native clone/local binding changed')
                if self.ram.read(self.ram.base+ROW_RVA,4)!=bytes([0,0,2,18]):raise Refused('native party row changed')
                bridge=self.observe(1)
                samples.append({'at':time.monotonic(),'frame':ctx.inst(1).live_frame(),'actors':a,'bridge':bridge})
                if self.log().count('Puppet 0 released')!=baseline_release:raise Refused('puppet released inside60s')
                cycles+=1
            seconds=time.monotonic()-start
            self.r.STEPS['record_stop'](ctx,{'as':'native_tracks'});rec=None
            save(ctx.run_dir/'network-native-samples.json',samples)
            for item in self.helpers:self.collect(item,35)
            tail=self.log()[interval_log_offset:]
            if '[warp] load complete' in tail:raise Refused('room lifetime changed during interval')
            bindings=re.findall(r'\[puppet 0\] frame \d+ motion [^\r\n]* actor=(?:0x)?([0-9A-Fa-f]+)',tail)
            if not bindings or any(int(x,16)!=address for x in bindings):raise Refused('wrong clone driver target')
            traces=re.findall(r'\[ptrace\] f=(\d+) t=(\d+) game=\(([^)]+)\) pose=\(([^)]+)\) motion=(\d+)',tail)
            motions=sorted({int(x[4]) for x in traces})
            if len(traces)<60 or not {2,3,151}.issubset(motions):raise Refused('run/jump/attack motion diversity unexposed')
            # Canonical lagged owner/native metric, filtered to the proved clone (not best-name fallback).
            track=ctx.saved['native_tracks'];saved_rows=track.rows
            track.rows=[row for row in saved_rows if row[1]==0 or int(row[4],16)==address or row[4]==row[3]]
            metric=self.r.puppet_error(track,0,1)
            track.rows=saved_rows
            self.emit('native_follow_metric',metric)
            if metric.get('mean',1e9)>=50:raise Refused('native clone lagged owner error>=50')
            # Natural bounded exit is observed, never inferred from ticks*16 as wall time.
            stop_deadline=time.monotonic()+150
            while any(p.poll() is None for _,p,_ in runtimes):
                if time.monotonic()>=stop_deadline:raise Refused('natural runtime shutdown deadline')
                ctx.sleep(.25)
            for name,proc,log in runtimes:
                log.flush();text=(ctx.run_dir/(name+'.log')).read_text(errors='replace')
                self.emit('runtime_exit',{'name':name,'exit':proc.returncode,'shutdown':'[Runtime] Shutdown' in text})
                if proc.returncode!=0 or '[Runtime] Shutdown' not in text:raise Refused('runtime naturalexit qualification')
            self.r.wait_for(ctx,lambda:self.log().count('Puppet 0 released')>baseline_release,'native puppet released after runtime exit',5,.1)
            result={'seconds':seconds,'cycles':cycles,'clone':clone,'motionIds':motions,'ptraceCount':len(traces),'nativeFollow':metric,'runtimeNaturalExit':True,'visualReview':'required','sharedStatusGate':identities[0]['actor']['status']==clone['actor']['status']}
        except BaseException as e:error=e;self.emit('failure',{'error':repr(e)})
        finally:
            self.cleaning=True;errors=[]
            if rec:
                try:self.r.STEPS['record_stop'](ctx,{'as':'native_tracks'})
                except BaseException as e:errors.append('recorder '+repr(e))
            # Stop only registered network helpers before touching the party byte.
            for name,proc,log in reversed(ctx.processes):
                try:
                    forced=proc.poll() is None
                    if forced:proc.terminate();proc.wait(timeout=5)
                    self.emit('network_cleanup',{'name':name,'pid':proc.pid,'exit':proc.returncode,'forced':forced})
                    if forced and name.startswith('runtime_'):errors.append(name+' forced shutdown')
                except BaseException as e:errors.append(repr(e))
            for name,proc,out,err,cmd in reversed(self.helpers):
                try:
                    if proc.poll() is None:proc.wait(timeout=35)
                    self.emit('helper_cleanup',{'name':name,'pid':proc.pid,'exit':proc.returncode})
                except BaseException as e:
                    errors.append(repr(e))
                    if proc.poll() is None:proc.kill();proc.wait(timeout=5)
                finally:out.close();err.close()
            try:
                if self.leaf:result['restore']=self.leaf.restore()
            except BaseException as e:errors.append('restore '+repr(e))
            # Restore -> immediate canonical kill, no further reload/play.
            for i in (1,0):
                try:
                    inst=ctx.inst(i);data=self.cli('kill',pid=inst.pid,check=False)
                    if not data.get('ok'):errors.append('canonical kill '+str(i))
                    else:self.killed.add(inst.pid);inst.expect_exit=True
                except BaseException as e:errors.append('kill '+repr(e))
            if self.ram:self.ram.close()
            if self.hostram:self.hostram.close()
            for i in (0,1):
                (ctx.run_dir/f'native-complete-{i}.log').write_text(ctx.inst(i).inject_log.read_text(errors='replace'))
            save(ctx.run_dir/'profile-result.json',{'result':result,'error':repr(error) if error else None,'cleanupErrors':errors})
            if errors and error is None:error=Refused('; '.join(errors))
        if error:raise self.r.StepFailed(str(error))
        return result

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--check',action='store_true');ap.add_argument('--native-sora-visual',action='store_true')
    ap.add_argument('--packet',type=Path,required=True);ap.add_argument('--review',type=Path);ap.add_argument('--foreign-inventory',type=Path)
    args=ap.parse_args();packet=args.packet.resolve();data=json.loads((packet/'pins.json').read_text())
    for row in data['files']:
        if sha(row['path'])!=row['sha256']:raise Refused('changed pin '+row['path'])
    r=load_runner();r.STEPS['native_sora_visual']=lambda c,s:None
    fixture=HERE/'scenario.json';r.validate_scenario(json.loads(fixture.read_text()))
    if args.check:print(json.dumps({'ok':True,'scope':'offline pins/schema; no live acceptance'}));return 0
    if not args.native_sora_visual or not args.review or not args.foreign_inventory:ap.error('default OFF; exact review and foreign inventory required')
    review=json.loads(args.review.read_text())
    if review.get('status')!='ADOPT' or review.get('pinsSha256')!=sha(packet/'pins.json'):raise Refused('lead adoption differs')
    paths=json.loads(args.foreign_inventory.read_text())['files']
    if not paths or any(not Path(x).is_absolute() for x in paths):raise Refused('explicit foreign file inventory required')
    foreign_before={x:sha(x) for x in paths};saves=r.hash_saves()
    if len(saves)!=4:raise Refused('exact four save files required')
    before=sandbox_inventory(r.LOGS);session=ctypes.c_ulong()
    if not ctypes.windll.kernel32.ProcessIdToSessionId(os.getpid(),ctypes.byref(session)) or session.value!=1:raise Refused('Session1 only')
    with (packet/'attempt-spent.json').open('x') as f:json.dump({'at':time.time(),'pinsSha256':sha(packet/'pins.json')},f)
    p=Profile(r,data['products'],time.monotonic()+540)
    r.KH2CTL=Path(data['products']['kh2ctl']['path']);r.RUNTIME=Path(data['products']['runtime']['path']);r.SERVER=Path(data['products']['server']['path']);r.AVATARCTL=Path(data['products']['avatarctl']['path'])
    if r.kh2ctl('instances')['instances']:raise Refused('requires empty rig')
    r.kh2ctl=p.cli;r.STEPS['native_sora_visual']=p.run
    sys.argv=[str(ROOT/'tools/scenario/run.py'),str(fixture)];code=1
    # Explicit process-only environment; inherited opt-ins cannot change this profile.
    oldenv={k:v for k,v in os.environ.items() if k.startswith('KH2COOP_')}
    for k in oldenv:del os.environ[k]
    try:code=r.main()
    finally:
        for k,v in oldenv.items():os.environ[k]=v
        after=r.hash_saves();foreign_after={x:sha(x) for x in paths};sand_after=sandbox_inventory(r.LOGS)
        logs=[]
        if p.ctx:
            for inst in p.ctx.instances:
                text=inst.inject_log.read_text(errors='replace')
                logs.append({'pid':inst.pid,'saveGuard':'Save guard installed: writes under' in text,'saveAttempts':re.findall(r'\[saveguard\].*(?:redirected|blocked)[^\r\n]*',text,re.I)})
        closed={'canonicalExit':code,'savesBefore':saves,'savesAfter':after,'foreignBefore':foreign_before,'foreignAfter':foreign_after,'sandboxBefore':before,'sandboxAfter':sand_after,'logs':logs,'runDir':str(p.ctx.run_dir) if p.ctx else None}
        closed['safetyPass']=len(logs)==2 and all(x['saveGuard'] and not x['saveAttempts'] for x in logs) and after==saves and foreign_before==foreign_after and {k:v for k,v in before.items() if v}=={k:v for k,v in sand_after.items() if v}
        save(packet/'closure.json',closed)
    return code if closed['safetyPass'] else 1
if __name__=='__main__':raise SystemExit(main())
