"""Internal PC2 media adapter. Injects only the agreed media context and hook."""
import copy
import ctypes
from ctypes import wintypes as w
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import time
import uuid

from PIL import Image

from . import metrics


def read(path):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise ValueError('duplicate native JSON key')
            out[key] = value
        return out
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=pairs,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON')))


def write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, allow_nan=False)+'\n', encoding='utf-8')


class ThreadEntry(ctypes.Structure):
    _fields_=[('size',w.DWORD),('usage',w.DWORD),('tid',w.DWORD),('owner',w.DWORD),
              ('base',w.LONG),('delta',w.LONG),('flags',w.DWORD)]


class Native:
    def __init__(self):
        self.k = ctypes.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'OpenProcess': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'CloseHandle': ([w.HANDLE], w.BOOL),
            'GetProcessTimes': ([w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4, w.BOOL),
            'QueryFullProcessImageNameW': ([w.HANDLE,w.DWORD,w.LPWSTR,ctypes.POINTER(w.DWORD)], w.BOOL),
            'K32EnumProcessModules': ([w.HANDLE,ctypes.POINTER(w.HMODULE),w.DWORD,ctypes.POINTER(w.DWORD)], w.BOOL),
            'CreateEventW': ([ctypes.c_void_p,w.BOOL,w.BOOL,w.LPCWSTR], w.HANDLE),
            'SetEvent': ([w.HANDLE], w.BOOL),
            'WaitForSingleObject': ([w.HANDLE,w.DWORD], w.DWORD),
            'GetExitCodeProcess': ([w.HANDLE,ctypes.POINTER(w.DWORD)], w.BOOL),
            'CreateToolhelp32Snapshot': ([w.DWORD,w.DWORD], w.HANDLE),
            'Thread32First': ([w.HANDLE,ctypes.POINTER(ThreadEntry)],w.BOOL),
            'Thread32Next': ([w.HANDLE,ctypes.POINTER(ThreadEntry)],w.BOOL),
            'OpenThread': ([w.DWORD,w.BOOL,w.DWORD],w.HANDLE),
            'GetProcessIdOfThread': ([w.HANDLE],w.DWORD),
            'GetThreadTimes': ([w.HANDLE]+[ctypes.POINTER(w.FILETIME)]*4,w.BOOL),
            'ResumeThread': ([w.HANDLE],w.DWORD),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k,name); fn.argtypes=args; fn.restype=result

    def close(self, handle):
        if handle:
            self.k.CloseHandle(handle)

    def open(self, pid, *, module=False):
        handle = self.k.OpenProcess((0x1410 if module else 0x1000)|0x100000,False,pid)
        if not handle:
            raise OSError(ctypes.get_last_error(), 'media identity process unavailable')
        return handle

    def creation_ticks(self, handle):
        files=[w.FILETIME() for _ in range(4)]
        if not self.k.GetProcessTimes(handle,*[ctypes.byref(x) for x in files]):
            raise OSError(ctypes.get_last_error(), 'media process creation unavailable')
        return (files[0].dwHighDateTime<<32)|files[0].dwLowDateTime

    def identity(self, handle):
        created=self.creation_ticks(handle)
        name=ctypes.create_unicode_buffer(32768); size=w.DWORD(len(name))
        if not self.k.QueryFullProcessImageNameW(handle,0,name,ctypes.byref(size)):
            raise OSError(ctypes.get_last_error(), 'media process image unavailable')
        return dict(creationTicks=created,imagePath=str(Path(name.value).resolve()))

    def base(self, handle):
        module=w.HMODULE(); needed=w.DWORD()
        if not self.k.K32EnumProcessModules(handle,ctypes.byref(module),ctypes.sizeof(module),ctypes.byref(needed)):
            raise OSError('media target module unavailable')
        return int(module.value)

    def absent(self, identity):
        try:
            handle=self.open(identity['pid'])
        except OSError as error:
            if error.errno==87:  # ERROR_INVALID_PARAMETER: this PID no longer exists.
                return True
            raise
        try:
            # An exited process object may still have open handles. Its creation
            # time and signaled state remain queryable, but its image may not.
            # PID reuse also proves the original incarnation absent. Never turn
            # access/query/wait uncertainty into a closure claim.
            if self.creation_ticks(handle)!=identity['creationTicks']:
                return True
            state=self.k.WaitForSingleObject(handle,0)
            if state==0:return True  # WAIT_OBJECT_0: original process exited.
            if state==258:return False  # WAIT_TIMEOUT: original process alive.
            raise OSError(ctypes.get_last_error(),f'media absence wait unavailable (result={state})')
        finally:
            self.close(handle)

    def resume(self, proc, admitted, admission_check):
        """Resume only the unique original primary thread, after journal and final source admission."""
        def original_alive():
            if (self.identity(proc._handle)!=admitted or
                    self.k.WaitForSingleObject(proc._handle,0)!=258):
                raise OSError('suspended helper original identity changed')
        original_alive()
        census=self.k.CreateToolhelp32Snapshot(4,0)  # TH32CS_SNAPTHREAD, read-only.
        if census in (None,ctypes.c_void_p(-1).value):raise OSError('helper thread census unavailable')
        try:
            row=ThreadEntry();row.size=ctypes.sizeof(row);threads=[]
            if not self.k.Thread32First(census,ctypes.byref(row)):raise OSError('helper thread census failed')
            while True:
                if row.owner==proc.pid:threads.append(row.tid)
                if not self.k.Thread32Next(census,ctypes.byref(row)):
                    if ctypes.get_last_error()!=18:raise OSError('helper thread census incomplete')
                    break
        finally:self.close(census)
        if len(threads)!=1:raise OSError('one original suspended helper thread required')
        thread=self.k.OpenThread(0x42,False,threads[0])  # query + suspend/resume
        if not thread:raise OSError('original helper thread unavailable')
        try:
            files=[w.FILETIME() for _ in range(4)]
            if (self.k.GetProcessIdOfThread(thread)!=proc.pid or
                    not self.k.GetThreadTimes(thread,*[ctypes.byref(x) for x in files]) or
                    ((files[0].dwHighDateTime<<32)|files[0].dwLowDateTime)<admitted['creationTicks']):
                raise OSError('helper thread original owner/creation refused')
            original_alive()
            admission_check()  # Last admission after all census/identity reads; no intervening query.
            if self.k.ResumeThread(thread)!=1:raise OSError('helper thread resume refused')
        finally:self.close(thread)


def validate(value, schema):
    """Small strict validator for this frozen schema; no optional package dependency."""
    definitions=schema['$defs']
    def check(v,s):
        if '$ref' in s:
            return check(v,definitions[s['$ref'].split('/')[-1]])
        if 'anyOf' in s:
            for branch in s['anyOf']:
                try:
                    check(v,branch); return
                except (ValueError,TypeError,KeyError):
                    pass
            raise ValueError('no typed receipt union matched')
        if 'const' in s and (type(v) is not type(s['const']) or v!=s['const']):
            raise ValueError('typed receipt constant mismatch')
        if 'enum' in s and not any(type(v) is type(x) and v==x for x in s['enum']):
            raise ValueError('typed receipt enum mismatch')
        kind=s.get('type')
        types={'object':dict,'array':list,'string':str,'integer':int,'boolean':bool,'null':type(None)}
        if kind=='number':
            if type(v) not in (int,float) or not math.isfinite(v): raise ValueError('finite receipt number required')
        elif kind and type(v) is not types[kind]:
            raise ValueError('exact receipt type required: '+kind)
        if type(v) in (int,float):
            if 'minimum' in s and v<s['minimum']:raise ValueError('receipt below minimum')
            if 'maximum' in s and v>s['maximum']:raise ValueError('receipt above maximum')
            if 'exclusiveMinimum' in s and v<=s['exclusiveMinimum']:raise ValueError('receipt below exclusive minimum')
        if type(v) is dict:
            if not set(s.get('required',[]))<=set(v):raise ValueError('missing receipt field')
            if s.get('additionalProperties') is False and set(v)-set(s['properties']):raise ValueError('extra receipt field')
            for k,item in v.items():
                if k in s.get('properties',{}):check(item,s['properties'][k])
        if type(v) is list:
            if len(v)<s.get('minItems',0) or len(v)>s.get('maxItems',math.inf):raise ValueError('receipt array length')
            for item in v:check(item,s.get('items',{}))
        if type(v) is str:
            import re
            if len(v)<s.get('minLength',0) or ('pattern' in s and not re.fullmatch(s['pattern'],v)):
                raise ValueError('receipt string shape')
    check(value,schema)


class MediaProduct:
    def __init__(self, runner, package, manifest, game_sha256, snapshot):
        self.runner=runner; self.package=Path(package).resolve(); self.snapshot=snapshot
        self.native=Native(); self.manifest=copy.deepcopy(manifest); self.game_sha256=game_sha256
        self.products={}
        for role in ('clipCli','encoder','verifier','metrics'):
            row=manifest['products'][role]
            path=metrics.file(self.package,row['packagePath']).resolve()
            if metrics.sha(path)!=row['sha256']:
                raise ValueError('independently pinned media product mismatch')
            self.products[role]=dict(packagePath=str(path),sha256=row['sha256'])
        if self.products['encoder'] != self.products['verifier']:
            raise ValueError('reviewed product uses one pinned ffmpeg for encode and full decode')
        if Path(self.products['encoder']['packagePath']) != self.package/'bin/ffmpeg.exe':
            raise ValueError('exact package encoder/verifier location required')
        # Bind current driver once, before the sole source warp or any helper spawn.
        rows=self.snapshot(); driver=next(x for x in rows if x['pid']==os.getpid())
        handle=self.native.open(os.getpid())
        try:
            identity=self.native.identity(handle)
            self.driver=dict(pid=os.getpid(),parentPid=driver['parent'],**identity,
                             imageSha256=metrics.sha(Path(identity['imagePath'])),argv=[__import__('sys').executable,*__import__('sys').argv])
        finally:
            self.native.close(handle)

    def publish(self, ctx):
        if hasattr(ctx,'pc2_media_products'):
            if ctx.pc2_media_products != self.products:raise ValueError('original media products replaced')
        else:
            ctx.pc2_media_products=copy.deepcopy(self.products)

    def install(self):
        original=self.runner.STEPS['wm_route']
        def route(ctx, step):
            self.publish(ctx)
            return original(ctx,step)
        self.runner.STEPS['wm_route']=route
        self.runner.pc2_owned_clip=self.clip

    def clip(self,ctx,instance,output,deadline):
        # Reject malformed API calls before creating any artifact or native handle.
        # A typed failure is meaningful only for one of the two admitted peers.
        if type(instance) is not int or instance not in (0,1):
            raise self.runner.StepFailed('owned clip requires exact instance 0 or 1')
        if type(deadline) is not float or not math.isfinite(deadline) or deadline<=0:
            raise self.runner.StepFailed('owned clip requires finite positive float deadline')
        started=time.perf_counter_ns(); output=Path(output); root=Path(ctx.run_dir).resolve()
        proc=None; game=None; event=None; stage='admission'; raw=None
        helper_attempted=False;helper_admitted=False;helper_exit_proven=False;cleanup_uncertain=False
        cancel=getattr(ctx,'pc2_media_cancel',None); original_event=cancel; original=None
        deadline_ns=math.floor(deadline*1e9) if type(deadline) is float and math.isfinite(deadline) else 1
        stem=f'hb_arrival_peer{instance}'; helpers={}; native_target=None
        def ensure():
            if cancel is None or not callable(getattr(cancel,'is_set',None)):
                raise ValueError('original source media cancellation Event required')
            metrics.ensure(deadline,cancel)
            if ctx.pc2_media_products!=self.products:raise ValueError('original media product binding replaced')
            if ctx.pc2_media_cancel is not original_event:raise ValueError('original source cancellation Event replaced')
            if original is not None and ctx.pc2_media_targets[instance]!=original:raise ValueError('original source media target replaced')
        def art(path):
            ensure(); value=metrics.artifact(root,path); ensure(); return value
        def logs():
            return {role:{stream:art(root/f'{stem}_{role}.{stream}.log') for stream in ('stdout','stderr')}
                    for role in ('helper','encoder','verifier')}
        try:
            ensure()
            if type(instance) is not int or instance not in (0,1) or type(deadline) is not float or not math.isfinite(deadline):
                raise ValueError('strict clip instance/deadline required')
            if output!=root/f'{stem}.mp4' or output.exists():raise ValueError('exact fresh clip destination required')
            original=copy.deepcopy(ctx.pc2_media_targets[instance])
            if set(original)!={'pid','creationTicks','moduleBase'} or any(type(v) is not int or v<=0 for v in original.values()) or original['pid']!=ctx.inst(instance).pid:
                raise ValueError('original source media target required')
            # Freeze source Event and admission rather than accepting a replacement during polling.
            game=self.native.open(original['pid'],module=True)
            admitted=self.native.identity(game)
            if admitted['creationTicks']!=original['creationTicks'] or self.native.base(game)!=original['moduleBase']:
                raise ValueError('original owned game creation/module changed')
            for product in self.products.values():
                if metrics.sha(Path(product['packagePath']))!=product['sha256']:raise ValueError('pinned media tool changed')
                ensure()
            name='Local\\kh2coop_clip_cancel_'+str(os.getpid())+'_'+uuid.uuid4().hex
            event=self.native.k.CreateEventW(None,True,False,name)
            if not event:raise OSError('media cancellation event creation failed')
            argv=[self.products['clipCli']['packagePath'],'clip','--pid',str(original['pid']),
                  '--out',str(output),'--seconds','3','--fps','30','--expected-creation',str(original['creationTicks']),
                  '--expected-module',str(original['moduleBase']),'--deadline-ns',str(deadline_ns),
                  '--cancel-event',name,'--tool-sha256',self.products['encoder']['sha256'],
                  '--game-sha256',self.game_sha256]
            stdout=root/f'{stem}_helper.stdout.log'; stderr=root/f'{stem}_helper.stderr.log'
            stage='capture'; ensure()
            with stdout.open('xb') as out,stderr.open('xb') as err:
                helper_started=time.perf_counter_ns()
                helper_attempted=True
                proc=subprocess.Popen(argv,cwd=self.package,env=dict(os.environ),stdin=subprocess.DEVNULL,
                                      stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW|0x4)
                identity=self.native.identity(proc._handle)
                helper=dict(pid=proc.pid,parentPid=os.getpid(),**identity,imageSha256=self.products['clipCli']['sha256'],argv=argv)
                if Path(helper['imagePath'])!=Path(self.products['clipCli']['packagePath']):raise ValueError('clip helper image changed')
                helpers['helper']=helper
                write(root/f'{stem}_helper.identity.json',helper)
                ensure();self.native.resume(proc,identity,ensure);helper_admitted=True
                while proc.poll() is None:
                    ensure()
                    if ctx.pc2_media_cancel is not original_event or ctx.pc2_media_targets[instance]!=original:
                        raise ValueError('original source media admission/cancel Event replaced')
                    # Native child identity is persisted before resume; retain it even on cancellation.
                    for role in ('encoder','verifier'):
                        path=root/f'{stem}_{role}.identity.json'
                        if path.exists() and role not in helpers:helpers[role]=read(path)
                    time.sleep(.02)
                helper_finished=time.perf_counter_ns()
                helper_exit_proven=True
            ensure(); raw=read(stdout)
            if proc.returncode!=0 or raw.get('ok') is not True:raise ValueError('native clip failure: '+str(raw.get('error',proc.returncode)))
            native_target=raw['target']
            if any(native_target[k]!=v for k,v in original.items()) or Path(native_target['imagePath'])!=Path(admitted['imagePath']) or native_target['imageSha256']!=self.game_sha256:
                raise ValueError('native raw target differs from original admission')
            if self.native.identity(game)!=admitted or self.native.base(game)!=original['moduleBase'] or self.native.k.WaitForSingleObject(game,0)!=258:
                raise ValueError('original held game changed during media')
            processes=copy.deepcopy(raw['processes'])
            for role in ('encoder','verifier'):
                child=processes[role]; ident=child['identity']; helpers[role]=ident
                if ident!=read(root/f'{stem}_{role}.identity.json') or child!=read(root/f'{stem}_{role}.closed.json') or not self.native.absent(ident):
                    raise ValueError('native child receipt/closure mismatch')
                if ident['parentPid']!=proc.pid or ident['imageSha256']!=self.products[role]['sha256'] or Path(ident['imagePath'])!=Path(self.products[role]['packagePath']):
                    raise ValueError('pinned owned media child binding mismatch')
            if not self.native.absent(helper):raise ValueError('clip helper identity remains')
            checked=time.perf_counter_ns()
            processes['helper']=dict(identity=helper,startedNs=helper_started,finishedNs=helper_finished,
                                     exitCode=0,exited=True,identityAbsent=True,absenceCheckedNs=checked)
            stage='decode'; timebase,pts=metrics.decoder_rows((root/f'{stem}_verifier.stderr.log').read_text(encoding='utf-8'))
            capture=raw['capture']; width,height=capture['width'],capture['height']
            frames=sorted((root/f'{stem}_frames').glob('*.png'))
            if len(frames)!=90:raise ValueError('exact ninety retained decoded PNGs required')
            manifest=dict(schema='pc2-decoded-frames-v1',instance=instance,videoSha256=art(output)['sha256'],
                          width=width,height=height,timeBase=timebase,metricsAlgorithmSha256=self.products['metrics']['sha256'],rows=[])
            baseline=None; sheet=Image.new('RGB',(320*10,180*9))
            for index,(_,pts_value,w0,h0) in enumerate(pts):
                ensure(); path=root/f'{stem}_frames/decoded_{index:03}.png'
                if (w0,h0)!=(width,height):raise ValueError('native/decode dimensions differ')
                with Image.open(path) as source:
                    if source.format!='PNG' or source.mode!='RGB' or source.size!=(width,height):raise ValueError('lossless RGB decoder output invalid')
                    image=source.copy()
                if baseline is None:baseline=image.copy()
                row=dict(index=index,pts=pts_value,png=art(path),**metrics.frame_metrics(image,baseline));ensure()
                manifest['rows'].append(row);thumb=image.copy();thumb.thumbnail((320,180));sheet.paste(thumb,((index%10)*320,(index//10)*180))
            validate(manifest,read(Path(__file__).with_name('pc2-decoded-frames-v1.schema.json')))
            manifest_path=root/f'{stem}_frames.json';write(manifest_path,manifest)
            sheet_path=root/f'{stem}_contact.png';sheet.save(sheet_path);ensure()
            summary=metrics.summarize(manifest,capture['encodedFps'])
            if summary['blackFrames'] or not summary['motionQualified']:raise ValueError('black/static-only clip refused')
            decode=dict(summary,artifactSha256=manifest['videoSha256'],frameDigestManifest=art(manifest_path),
                        contactSheet=art(sheet_path),metricsAlgorithmSha256=self.products['metrics']['sha256'])
            stage='hash'; video=art(output)
            if video['sha256']!=manifest['videoSha256']:raise ValueError('finalized video bytes changed')
            if self.native.identity(game)!=admitted or self.native.base(game)!=original['moduleBase'] or self.native.k.WaitForSingleObject(game,0)!=258:
                raise ValueError('original held game changed during metric finalization')
            remaining=[i for i in helpers.values() if not self.native.absent(i)]
            if remaining:raise ValueError('owned media process still present')
            closure=dict(allStartedIdentitiesRetained=True,helpersClosed=True,ownedDescendantsAbsent=True,
                         checkedNs=time.perf_counter_ns(),remainingOwnedIdentities=[])
            result=dict(schema='pc2-owned-clip-v1',ok=True,request=dict(instance=instance,output=output.name,seconds=3,requestedFps=30),
                        target=native_target,products=copy.deepcopy(self.products),driver=copy.deepcopy(self.driver),
                        startedNs=started,finishedNs=time.perf_counter_ns(),deadlineMonotonicNs=deadline_ns,
                        capture=capture,artifact=video,decode=decode,processes=processes,closure=closure,
                        rawCliReceipt=art(stdout),rawLogs=logs(),acceptance=False)
            result['finishedNs']=time.perf_counter_ns(); ensure()
            validate(result,read(Path(__file__).with_name('pc2-owned-clip-v1.schema.json')))
            write(root/f'{stem}_clip.json',result);ensure();return result
        except Exception as error:
            primary_error=str(error)
            # Signal native cancellation first; native KILL_ON_JOB_CLOSE contains only its own descendants.
            if event:self.native.k.SetEvent(event)
            if proc and proc.poll() is None:
                stop_at=min(deadline_ns,time.perf_counter_ns()+1_000_000_000)
                while proc.poll() is None and time.perf_counter_ns()<stop_at:time.sleep(.02)
                if proc.poll() is None:
                    try:proc.terminate()
                    except OSError:cleanup_uncertain=True
                remaining=max(0,(deadline_ns-time.perf_counter_ns())/1e9)
                try:proc.wait(timeout=min(remaining,1))
                except (subprocess.TimeoutExpired,OSError):cleanup_uncertain=True
            if proc is not None:helper_exit_proven=proc.poll() is not None
            helper_unknown=helper_attempted and (not helper_admitted or not helper_exit_proven or cleanup_uncertain)
            # Retain every native identity persisted before child resume; no speculative process-name cleanup.
            recovery_failed=False
            def recovery_read(path):
                nonlocal recovery_failed
                try:return read(path)
                except (OSError,ValueError):
                    recovery_failed=True
                    return {}
            for role in ('helper','encoder','verifier'):
                path=root/f'{stem}_{role}.identity.json'
                if path.is_file():
                    recovered=recovery_read(path)
                    if recovered:helpers[role]=recovered
            observations=[];remaining=[]
            for role,identity in helpers.items():
                try:absent=self.native.absent(identity)
                except OSError:absent=False
                if not absent:remaining.append(identity)
                path=root/f'{stem}_{role}.closed.json'
                closed=recovery_read(path) if path.is_file() else {}
                observations.append(dict(role=role,identity=identity,startedNs=closed.get('startedNs',started),
                                         finishedNs=closed.get('finishedNs'),exitCode=closed.get('exitCode'),
                                         exited=absent,identityAbsent=absent,absenceCheckedNs=time.perf_counter_ns()))
            def partial_art(path):return metrics.artifact(root,path) if path.is_file() else None
            streams={role:{stream:partial_art(root/f'{stem}_{role}.{stream}.log') for stream in ('stdout','stderr')}
                     for role in ('helper','encoder','verifier')}
            failure_schema=read(Path(__file__).with_name('pc2-owned-clip-failure-v1.schema.json'))
            target_keys=failure_schema['$defs']['failure']['properties']['target']['anyOf'][0]['properties']
            failure=dict(schema='pc2-owned-clip-failure-v1',ok=False,acceptance=False,instance=instance,
                         target={k:v for k,v in native_target.items() if k in target_keys} if native_target else None,
                         stage=stage,error=primary_error+('; recovery journal unreadable' if recovery_failed else '')+('; helper descendants UNKNOWN: identity/journal/positive exit proof incomplete' if helper_unknown else ''),startedNs=started,finishedNs=time.perf_counter_ns(),deadlineMonotonicNs=deadline_ns,
                         spawnedIdentities=list(helpers.values()),processes=observations,
                         closure=dict(checkedNs=time.perf_counter_ns(),ownedDescendantsAbsent=not remaining and not recovery_failed and not helper_unknown,remainingOwnedIdentities=remaining),
                         artifact=output.name if output.is_file() else None,
                         rawCliReceipt=(lambda a:a if a and a['bytes']>0 else None)(partial_art(root/f'{stem}_helper.stdout.log')),
                         rawLogs=streams if all(v is not None for logs0 in streams.values() for v in logs0.values()) else None)
            validate(failure,failure_schema);write(root/f'{stem}_clip_failure.json',failure)
            raise self.runner.StepFailed('owned clip '+stage+' failed: '+str(error)) from error
        finally:
            self.native.close(event);self.native.close(game)
