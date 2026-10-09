"""Offline controls: generated RGB fixtures, no game/native process or socket use."""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest

from PIL import Image

from . import adapter, metrics, lifecycle_controls


class Controls(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='kh2-clip-control-')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve()
        self.cancel=threading.Event()

    def manifest(self):
        directory=self.root/'hb_arrival_peer0_frames';directory.mkdir()
        base=Image.new('RGB',(16,16),(50,50,50))
        rows=[]
        for index in range(90):
            image=base.copy()
            if index:
                image.paste((100,50,50),(0,0,8,8))
            path=directory/f'decoded_{index:03}.png';image.save(path)
            rows.append(dict(index=index,pts=index,png=metrics.artifact(self.root,path),
                             **metrics.frame_metrics(image,base)))
        return dict(schema='pc2-decoded-frames-v1',instance=0,videoSha256='a'*64,
                    width=16,height=16,timeBase=dict(numerator=1,denominator=30),
                    metricsAlgorithmSha256=metrics.sha(Path(metrics.__file__)),rows=rows)

    def test_metric_integer_luma(self):
        image=Image.new('RGB',(4,1));image.putdata([(16,16,16),(17,17,17),(255,0,0),(0,0,255)])
        self.assertEqual(metrics.frame_metrics(image,image)['blackFraction'],.25)

    def test_metric_delta_threshold(self):
        base=Image.new('RGB',(2,1),(50,50,50));image=base.copy()
        image.putdata([(65,50,50),(50,66,50)])
        self.assertEqual(metrics.frame_metrics(image,base)['changedPixels'],1)

    def test_metric_rgb_hash(self):
        image=Image.new('RGB',(2,1),(50,50,50))
        self.assertEqual(metrics.frame_metrics(image,image)['rgbSha256'],hashlib.sha256(image.tobytes()).hexdigest())

    def test_metric_mode_refusal(self):
        with self.assertRaises(ValueError):metrics.frame_metrics(Image.new('L',(2,2)),Image.new('RGB',(2,2)))

    def test_complete_ninety_manifest(self):
        manifest=self.manifest()
        adapter.validate(manifest,adapter.read(Path(adapter.__file__).with_name('pc2-decoded-frames-v1.schema.json')))
        self.assertTrue(metrics.recompute(manifest,self.root,time.perf_counter()+10,self.cancel))
        summary=metrics.summarize(manifest,30)
        self.assertEqual((summary['decodedFrames'],summary['durationSeconds'],summary['blackFrames']), (90,3.0,0))
        self.assertTrue(summary['motionQualified'])

    def test_frame_mutants(self):
        original=self.manifest()
        mutations=[lambda m:m['rows'].pop(),lambda m:m['rows'][1].update(index=True),
                   lambda m:m['rows'][1].update(pts=0),lambda m:m['rows'][1].update(changedPixels=65),
                   lambda m:m['rows'][1].update(blackFraction=.1),lambda m:m['rows'][1].update(rgbSha256='b'*64),
                   lambda m:m['rows'][1]['png'].update(sha256='c'*64),lambda m:m['rows'][1]['png'].update(bytes=1),
                   lambda m:m['rows'][1]['png'].update(path='../escape.png'),lambda m:m.update(width=17)]
        for mutate in mutations:
            with self.subTest(mutation=mutations.index(mutate)):
                manifest=copy.deepcopy(original);mutate(manifest)
                with self.assertRaises(ValueError):metrics.recompute(manifest,self.root,time.perf_counter()+10,self.cancel)

    def test_static_refuses_motion(self):
        manifest=self.manifest()
        for row in manifest['rows']:row.update(rgbSha256='a'*64,changedPixels=0)
        self.assertFalse(metrics.summarize(manifest,30)['motionQualified'])

    def test_black_count(self):
        manifest=self.manifest();manifest['rows'][2]['blackFraction']=.99
        self.assertEqual(metrics.summarize(manifest,30)['blackFrames'],1)

    def test_duration_mismatch(self):
        manifest=self.manifest();manifest['rows'][-1]['pts']=100
        with self.assertRaises(ValueError):metrics.summarize(manifest,30)

    def test_decoder_real_showinfo_shape(self):
        log='[Parsed_showinfo_1] config in time_base: 1/15360, frame_rate: 30/1\n'
        log+='\n'.join(f'[Parsed_showinfo_1] n: {i:3} pts: {i*512:6} pts_time:0 fmt:rgb24 s:320x180 i:P' for i in range(90))
        tb,rows=metrics.decoder_rows(log)
        self.assertEqual(tb,dict(numerator=1,denominator=15360));self.assertEqual(rows[-1],(89,45568,320,180))
        with self.assertRaises(ValueError):metrics.decoder_rows(log+'\n'+log)

    def test_decoder_truncated_refuses(self):
        with self.assertRaises(ValueError):metrics.decoder_rows('config in time_base: 1/30\nn:0 pts:0 s:16x16')

    def test_cancel_and_deadline(self):
        self.cancel.set()
        with self.assertRaises(ValueError):metrics.ensure(time.perf_counter()+10,self.cancel)
        self.cancel.clear()
        with self.assertRaises(ValueError):metrics.ensure(time.perf_counter()-1,self.cancel)

    def test_media_uses_explicit_qpc_clock(self):
        old,old_ns=time.monotonic,time.monotonic_ns
        def forbidden():raise AssertionError('GetTickCount64 media clock used')
        time.monotonic=time.monotonic_ns=forbidden
        try:
            metrics.ensure(time.perf_counter()+10,self.cancel)
            product=self.product();ctx=SimpleNamespace(run_dir=self.root,pc2_media_cancel=self.cancel)
            product.publish(ctx);self.cancel.set()
            with self.assertRaises(product.runner.StepFailed):
                product.clip(ctx,0,self.root/'hb_arrival_peer0.mp4',time.perf_counter()+10)
            failure=adapter.read(self.root/'hb_arrival_peer0_clip_failure.json')
            self.assertLess(failure['startedNs'],failure['finishedNs'])
            self.assertEqual(failure['deadlineMonotonicNs']>failure['finishedNs'],True)
        finally:time.monotonic,time.monotonic_ns=old,old_ns

    def test_json_duplicate_and_nonfinite(self):
        path=self.root/'raw.json'
        for text in ('{"ok":true,"ok":false}','{"x":NaN}','{"x":Infinity}'):
            path.write_text(text,encoding='utf-8')
            with self.assertRaises(ValueError):adapter.read(path)

    def test_regular_file_paths(self):
        path=self.root/'okay.png';path.write_bytes(b'x')
        self.assertEqual(metrics.file(self.root,'okay.png'),path)
        for name in ('../okay.png','./okay.png',str(path),'missing.png','foo//bar'):
            with self.assertRaises(ValueError):metrics.file(self.root,name)

    def test_strict_schema_integer(self):
        schema={'$defs':{},'type':'object','additionalProperties':False,'properties':{'n':{'type':'integer'}},'required':['n']}
        adapter.validate({'n':1},schema)
        for value in ({'n':True},{'n':1.0},{'n':1,'extra':2},{}):
            with self.assertRaises(ValueError):adapter.validate(value,schema)

    def product(self):
        product=object.__new__(adapter.MediaProduct)
        class StepFailed(Exception):pass
        product.runner=SimpleNamespace(STEPS={'wm_route':lambda ctx,step:ctx.pc2_media_products},StepFailed=StepFailed)
        product.products={key:dict(packagePath=str(self.root/(key+'.exe')),sha256='a'*64)
                          for key in ('clipCli','encoder','verifier','metrics')}
        product.native=SimpleNamespace(close=lambda _:None)
        return product

    def test_actual_install_publishes_before_source(self):
        product=self.product();product.install();ctx=SimpleNamespace()
        self.assertEqual(product.runner.STEPS['wm_route'](ctx,{}),product.products)
        self.assertEqual(product.runner.pc2_owned_clip,product.clip)
        ctx.pc2_media_products['metrics']['sha256']='b'*64
        with self.assertRaises(ValueError):product.runner.STEPS['wm_route'](ctx,{})
        self.assertEqual(product.products['metrics']['sha256'],'a'*64)

    def test_actual_hook_typed_cancel_failure(self):
        product=self.product();ctx=SimpleNamespace(run_dir=self.root,pc2_media_cancel=self.cancel)
        product.publish(ctx);self.cancel.set()
        with self.assertRaises(product.runner.StepFailed):product.clip(ctx,0,self.root/'hb_arrival_peer0.mp4',time.perf_counter()+10)
        failure=adapter.read(self.root/'hb_arrival_peer0_clip_failure.json')
        self.assertFalse(failure['ok']);self.assertFalse(failure['acceptance']);self.assertEqual(failure['spawnedIdentities'],[])
        self.assertEqual(failure['stage'],'admission');self.assertTrue(failure['closure']['ownedDescendantsAbsent'])

    def test_actual_hook_api_type_refusals(self):
        product=self.product();ctx=SimpleNamespace(run_dir=self.root)
        for instance,deadline in ((True,float(time.perf_counter()+10)),(2,float(time.perf_counter()+10)),(0,1),(0,float('nan'))):
            with self.assertRaises(product.runner.StepFailed):product.clip(ctx,instance,self.root/'hb_arrival_peer0.mp4',deadline)
        self.assertEqual(list(self.root.iterdir()),[])

    def test_actual_success_retained_empty_native_logs(self):lifecycle_controls.run(self,'success')
    def test_actual_empty_stdout_failure_receipt(self):lifecycle_controls.run(self,'empty_stdout')
    def test_actual_identity_failure_unknown_closure(self):lifecycle_controls.run(self,'identity_failure')
    def test_actual_journal_failure_unknown_closure(self):lifecycle_controls.run(self,'journal_failure')
    def test_actual_terminate_failure_unknown_closure(self):lifecycle_controls.run(self,'terminate_failure')
    def test_actual_wait_timeout_unknown_closure(self):lifecycle_controls.run(self,'wait_timeout')
    def test_actual_positive_cleanup_recovery(self):lifecycle_controls.run(self,'cleanup_recovery')

    def test_actual_suspended_thread_resume_boundaries(self):
        for mode in ('valid','wrong_owner','old_thread','dead_process'):
            with self.subTest(mode=mode):lifecycle_controls.resume_control(self,mode)

    def test_cancel_during_thread_census_never_resumes(self):lifecycle_controls.run(self,'cancel_in_census')
    def test_cancel_during_final_identity_never_resumes(self):lifecycle_controls.run(self,'cancel_in_identity')
    def test_deadline_during_thread_census_never_resumes(self):lifecycle_controls.run(self,'deadline_in_census')
    def test_deadline_during_final_identity_never_resumes(self):lifecycle_controls.run(self,'deadline_in_identity')
    def test_target_replaced_during_final_identity_never_resumes(self):lifecycle_controls.run(self,'replace_in_identity')

    def absence_native(self, *, created=100, state=0, open_error=None, time_error=None):
        import ctypes
        native=object.__new__(adapter.Native);closed=[]
        def open_(pid,**kw):
            if open_error:raise OSError(open_error,'query refused')
            return 44
        def times(handle,*ptrs):
            if time_error:ctypes.set_last_error(time_error);return False
            ptrs[0]._obj.dwLowDateTime=created;return True
        def forbidden_image(*args):raise AssertionError('absence must never query image')
        native.open=open_;native.close=lambda h:closed.append(h)
        native.k=SimpleNamespace(GetProcessTimes=times,WaitForSingleObject=lambda *a:state,QueryFullProcessImageNameW=forbidden_image)
        return native,closed

    def test_absence_original_exited_without_image_query(self):
        n,c=self.absence_native();self.assertTrue(n.absent(dict(pid=9,creationTicks=100)));self.assertEqual(c,[44])

    def test_absence_original_alive_refuses_without_image_query(self):
        n,c=self.absence_native(state=258);self.assertFalse(n.absent(dict(pid=9,creationTicks=100)));self.assertEqual(c,[44])

    def test_absence_reused_pid_different_creation(self):
        n,c=self.absence_native(created=101,state=258);self.assertTrue(n.absent(dict(pid=9,creationTicks=100)));self.assertEqual(c,[44])

    def test_absence_missing_pid_vs_access_denied(self):
        n,c=self.absence_native(open_error=87);self.assertTrue(n.absent(dict(pid=9,creationTicks=100)));self.assertEqual(c,[])
        n,c=self.absence_native(open_error=5)
        with self.assertRaises(OSError)as caught:n.absent(dict(pid=9,creationTicks=100))
        self.assertEqual(caught.exception.errno,5);self.assertEqual(c,[])

    def test_absence_creation_failure_preserves_win32_error(self):
        n,c=self.absence_native(time_error=5)
        with self.assertRaises(OSError)as caught:n.absent(dict(pid=9,creationTicks=100))
        self.assertEqual(caught.exception.errno,5);self.assertEqual(c,[44])

    def test_absence_wait_failure_or_unknown_never_closed(self):
        import ctypes
        for state in (0xffffffff,128):
            n,c=self.absence_native(state=state);ctypes.set_last_error(6)
            with self.assertRaises(OSError)as caught:n.absent(dict(pid=9,creationTicks=100))
            self.assertEqual(caught.exception.errno,6);self.assertIn(str(state),str(caught.exception));self.assertEqual(c,[44])

    def test_identity_image_failure_preserves_win32_error(self):
        import ctypes
        n,c=self.absence_native()
        def image(*a):ctypes.set_last_error(31);return False
        n.k.QueryFullProcessImageNameW=image
        with self.assertRaises(OSError)as caught:n.identity(44)
        self.assertEqual(caught.exception.errno,31);self.assertIn('media process image unavailable',str(caught.exception))

    def test_module_query_rights_explicit_limited_information(self):
        n=object.__new__(adapter.Native);calls=[]
        n.k=SimpleNamespace(OpenProcess=lambda mask,inherit,pid:calls.append((mask,inherit,pid))or 44)
        self.assertEqual(n.open(9,module=True),44);self.assertEqual(calls,[(0x101410,False,9)])

    def test_frozen_schemas_unchanged(self):
        for name,value in {'pc2-owned-clip-v1':'e3d8c4d7ee949aae43d0447755e2e44db1b834e06692c27b1bd36c6cbaf002fc',
                           'pc2-owned-clip-failure-v1':'27df82c90169fa103f3f4005b3861ea8b18cb53b1ac89acf474a72d11a98bff1',
                           'pc2-decoded-frames-v1':'b06a43ab43bccccfdd6a58eb061df6cf68a1426a9aaeea4af71a3ee6665f6adc'}.items():
            self.assertEqual(metrics.sha(Path(__file__).with_name(name+'.schema.json')),value)


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--retained-native',type=Path);args=parser.parse_args()
    if args.retained_native:lifecycle_controls.RETAINED=args.retained_native.resolve()
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(Controls)
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    adapter.write(args.output,dict(ok=result.wasSuccessful(),tests=result.testsRun,failures=len(result.failures),errors=len(result.errors),
                                  nativeGameExecuted=False,pc2Contact=False,fixture='generated RGB / synthetic native log grammar',
                                  sourceSha256={p.name:metrics.sha(p) for p in Path(__file__).parent.glob('*.py')}))
    raise SystemExit(0 if result.wasSuccessful() else 1)

if __name__=='__main__':main()
