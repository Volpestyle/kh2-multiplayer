"""Production split admission/validation with generated pixels and native leaves mocked.

No processes, games, SSH, native builds or encoder execution. Native controls
must separately qualify the real suspended helper/job and ffmpeg paths.
"""
import argparse
import copy
import json
import math
from pathlib import Path
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from PIL import Image
from . import adapter, metrics, postrun


class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve();self.cancel=threading.Event()
        self.target=dict(pid=10,creationTicks=20,moduleBase=65536)
        self.product=object.__new__(adapter.MediaProduct)
        self.product.seconds=16;self.product.frames=480;self.product.package=self.root
        self.product.snapshot=lambda:[]
        self.product.products={}
        for key in ('clipCli','encoder','verifier','metrics'):
            path=self.root/(key+'.exe');path.write_bytes(b'pinned fake leaf '+key.encode())
            self.product.products[key]=dict(packagePath=str(path),sha256=metrics.sha(path))
        self.driver=self.identity(1,2,'clipCli');self.product.driver=self.driver
        self.product.native=SimpleNamespace(absent=lambda _:True,close=lambda _:None,
            identity=lambda _:dict(creationTicks=112,imagePath=self.product.products['clipCli']['packagePath']),
            resume=lambda proc,identity,check:check(),
            k=SimpleNamespace(CreateEventW=lambda *a:1,SetEvent=lambda _:True))
        self.ctx=SimpleNamespace(run_dir=self.root,pc2_media_cancel=self.cancel,
            pc2_media_targets={0:self.target},pc2_media_products=copy.deepcopy(self.product.products))
        now=time.perf_counter_ns();self.started=now-1_000_000_000
        self.helper=self.identity(50,60,'clipCli')
        self.full_target=dict(**self.target,imagePath='game.exe',imageName='KINGDOM HEARTS II FINAL MIX.exe',
            imageSha256='a'*64,ownedRecordSha256='b'*64,handleHeldThroughFinalization=True)
        self.cap=dict(seconds=16,frameCount=480,expectedRequestSeq=1,doneSeq=1,nativeStatus=0,framesWritten=480,requestedInterval=1,
            width=16,height=16,renderer=1,backbufferFormat=28,gameFpsBefore=30.,gameFpsDuringCapture=30.,
            encodedFps=30.,startedNs=now-900_000_000,finishedNs=now-100_000_000,captureLeaseReleased=True)
        rawdir=self.root/'hb_arrival_peer0_raw';rawdir.mkdir()
        for i in range(480):Image.new('RGB',(16,16),(50+i%100,60,70)).save(rawdir/f'f_{i:05}.bmp')
        (self.root/'hb_arrival_peer0_frames').mkdir()
        release=dict(schema='pc2-capture-release-v1',helper={k:self.helper[k] for k in ('pid','creationTicks')},
            target=self.full_target,seconds=16,frames=480,output=str(rawdir/'f_%05u.bmp'),capture=self.cap)
        adapter.write(self.root/'hb_arrival_peer0_capture.released.json',release)
        raw=dict(ok=True,schema='pc2-native-capture-only-v1',target=self.full_target,capture=self.cap,
            rawPattern=release['output'],releaseJournal=str(self.root/'hb_arrival_peer0_capture.released.json'),
            encoderStarted=False,verifierStarted=False)
        self.stdout=self.root/'hb_arrival_peer0_helper.stdout.log';adapter.write(self.stdout,raw)
        self.stderr=self.root/'hb_arrival_peer0_helper.stderr.log';self.stderr.write_bytes(b'')
        self.capture=postrun.capture_only_receipt(self.product,self.ctx,raw,self.helper,self.started,now,
            self.started,float(time.perf_counter()+80),self.stdout,self.stderr,lambda:None)
        self.originals=dict(game=self.identity(10,20,'clipCli'),runtime=self.identity(11,21,'clipCli'),captureHelper=self.helper)
        self.closure=dict(schema='pc2-postrun-run-closure-v1',originals=self.originals,
            **{k:True for k in ('gameplayComplete','gamesClosed','runtimesClosed','captureHelpersClosed',
                               'descendantsAbsent','protectedFilesEqual','portsClosed','censusComplete')})
    def identity(self,pid,creation,role):
        return dict(pid=pid,creationTicks=creation,parentPid=50,imagePath=self.product.products[role]['packagePath'],
            imageSha256=self.product.products[role]['sha256'],argv=['fixture'])
    def admit(self,**kw):
        return postrun.admit_postrun(self.product,self.ctx,self.capture,self.closure,self.originals,
            kw.get('verify',lambda *a:True),kw.get('deadline',float(time.perf_counter()+590)))
    def test_capture_only_actual_schema_480_and_empty_logs(self):
        self.assertEqual(len(self.capture['rawFrames']),480)
        self.assertEqual(self.capture['rawLogs']['stderr']['bytes'],0)
        self.assertFalse((self.root/'hb_arrival_peer0.mp4').exists())
        self.assertEqual(self.admit(),self.root)
    def test_second_window_requires_original_release_and_closed_helper(self):
        previous=(self.root,self.capture)
        self.assertTrue(postrun.admit_next_capture(self.product,previous,self.target))
        with self.assertRaises(ValueError):postrun.admit_next_capture(self.product,None,self.target)
        self.product.native.absent=lambda _:False
        with self.assertRaises(ValueError):postrun.admit_next_capture(self.product,previous,self.target)
        self.product.native.absent=lambda _:True
        with self.assertRaises(ValueError):postrun.admit_next_capture(self.product,previous,dict(self.target,pid=99))
        (self.root/'hb_arrival_peer0_raw/f_00001.bmp').write_bytes(b'changed')
        with self.assertRaises(ValueError):postrun.admit_next_capture(self.product,previous,self.target)
    def test_all_closure_gates_mandatory_before_spawn(self):
        for key in ('gameplayComplete','gamesClosed','runtimesClosed','captureHelpersClosed',
                    'descendantsAbsent','protectedFilesEqual','portsClosed','censusComplete'):
            self.closure[key]=False
            with self.assertRaises(ValueError):self.admit()
            self.closure[key]=True
        with self.assertRaises(ValueError):self.admit(verify=None)
        with self.assertRaises(ValueError):self.admit(verify=lambda *a:False)
    def test_live_original_or_descendant_refuses(self):
        self.product.native.absent=lambda _:False
        with self.assertRaises(ValueError):self.admit()
        self.product.native.absent=lambda _:True
        self.product.snapshot=lambda:[dict(parent=self.originals['runtime']['pid'])]
        with self.assertRaises(ValueError):self.admit()
    def test_cancel_expired_mixed_clock_or_over600_refuses(self):
        for deadline in (time.perf_counter()-1.,time.perf_counter()+601.,float('nan'),True):
            with self.assertRaises(ValueError):self.admit(deadline=deadline)
        self.cancel.set()
        with self.assertRaises(ValueError):self.admit()
    def test_original_capture_raw_hash_or_admission_mutation_refuses(self):
        path=self.root/self.capture['rawFrames'][0]['path'];path.write_bytes(b'changed')
        with self.assertRaises(ValueError):self.admit()
    def test_wrong_runtime_or_missing_inventory_refuses(self):
        self.originals.pop('runtime')
        with self.assertRaises(ValueError):self.admit()
    def execute(self,cancel_on_resume=False):
        calls=[]
        def popen(argv,**kwargs):
            calls.append(argv);now=time.perf_counter_ns();processes={}
            (self.root/'hb_arrival_peer0.mp4').write_bytes(b'explicitly mocked native encode output')
            for i in range(480):Image.new('RGB',(16,16),(50+i%100,60,70)).save(self.root/f'hb_arrival_peer0_frames/decoded_{i:03}.png')
            for n,role in enumerate(('encoder','verifier')):
                ident=self.identity(200+n,300+n,role);ident['parentPid']=111
                child=dict(identity=ident,startedNs=now,finishedNs=now+1,exitCode=0,exited=True,identityAbsent=True,absenceCheckedNs=now+2)
                adapter.write(self.root/f'hb_arrival_peer0_{role}.identity.json',ident)
                adapter.write(self.root/f'hb_arrival_peer0_{role}.closed.json',child);processes[role]=child
                (self.root/f'hb_arrival_peer0_{role}.stdout.log').write_bytes(b'')
                (self.root/f'hb_arrival_peer0_{role}.stderr.log').write_bytes(b'')
            show='config in time_base: 1/30\n'+''.join(f'n: {i} pts: {i} s:16x16\n' for i in range(480))
            (self.root/'hb_arrival_peer0_verifier.stderr.log').write_text(show)
            def option(key):return argv[argv.index(key)+1]
            result=dict(ok=True,schema='pc2-native-postrun-v1',inventorySha256=option('--inventory-sha256'),
                closureSha256=option('--closure-sha256'),processes=processes)
            kwargs['stdout'].write(json.dumps(result).encode());kwargs['stdout'].flush()
            return SimpleNamespace(pid=111,_handle=5,returncode=0,poll=lambda:0)
        if cancel_on_resume:
            def resume(proc,identity,check):self.cancel.set();check()
            self.product.native.resume=resume
        with patch.object(postrun.subprocess,'Popen',popen):
            result=postrun.postprocess(self.product,self.ctx,self.capture,self.closure,
                original_identities=self.originals,verify_closure=lambda *a:True,
                deadline=float(time.perf_counter()+590))
        self.assertEqual(len(calls),1);self.assertEqual(calls[0][1],'clip-postprocess')
        return result
    def test_actual_postrun_path_full480_schema_metrics_and_empty_logs(self):
        result=self.execute();self.assertTrue(result['ok']);self.assertFalse(result['acceptance'])
        self.assertEqual(result['decode']['decodedFrames'],480)
        manifest=adapter.read(self.root/result['frameManifest']['path'])
        self.assertTrue(metrics.recompute(manifest,self.root,time.perf_counter()+5,self.cancel))
        manifest['rows'][1]['rgbSha256']='0'*64
        with self.assertRaises(ValueError):metrics.recompute(manifest,self.root,time.perf_counter()+5,self.cancel)
    def test_final_resume_cancel_retains_typed_failure_and_originals(self):
        with self.assertRaises(ValueError):self.execute(cancel_on_resume=True)
        failure=adapter.read(self.root/'hb_arrival_peer0_postrun_failure.json')
        self.assertFalse(failure['ok']);self.assertEqual(set(failure['spawnedIdentities']),{'postrunHelper','encoder','verifier'})
        self.assertFalse((self.root/'hb_arrival_peer0_postrun.json').exists())


    def test_fresh_postrun_descendant_census_is_mandatory(self):
        identities={'helper':self.helper}
        self.product.snapshot=lambda:[dict(pid=900,parent=self.helper['pid'],creationTicks=999)]
        with self.assertRaisesRegex(ValueError,'descendant'):postrun.fresh_postrun_closure(self.product,identities)
        self.product.snapshot=lambda:[dict(pid=900,parent=0)]
        with self.assertRaisesRegex(ValueError,'incomplete'):postrun.fresh_postrun_closure(self.product,identities)
        self.product.snapshot=lambda:[]
        self.assertTrue(postrun.fresh_postrun_closure(self.product,identities)['ownedDescendantsAbsent'])


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Controls))
    args.out.write_text(json.dumps(dict(ok=result.wasSuccessful(),tests=result.testsRun,
        errors=len(result.errors),failures=len(result.failures),nativeProcessLeavesMocked=True,
        processCreated=False,pc2Contact=False,nativeBuilt=False),indent=2),encoding='utf-8')
    raise SystemExit(not result.wasSuccessful())

if __name__=='__main__':main()
