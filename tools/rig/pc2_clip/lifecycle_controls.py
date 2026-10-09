"""Actual adapter lifecycle controls; native/process leaves are intercepted.

Encoder/verifier receipts, empty log bytes and all90 PNGs come unchanged from
retained real generated-frame control. Target/capture leaves are explicitly
synthetic because no game capture has been authorized for this product.
"""
import copy
import ctypes
import json
import os
from pathlib import Path
import shutil
import threading
from types import SimpleNamespace
from unittest.mock import patch

from . import adapter,metrics


RETAINED=Path(__file__).resolve().parents[2]/'evidence/native-controls/encode_decode'


def run(test,case):
    root=test.root;product=object.__new__(adapter.MediaProduct)
    class StepFailed(Exception):pass
    product.runner=SimpleNamespace(StepFailed=StepFailed);product.package=root
    product.game_sha256='a'*64
    product.products={}
    for role in ('clipCli','encoder','verifier','metrics'):
        path=root/(role+'.exe');path.write_bytes(b'fake-native-leaf')
        product.products[role]=dict(packagePath=str(path),sha256=metrics.sha(path))
    proc=SimpleNamespace(pid=999,_handle=2,returncode=None,resumed=False,terminated=False)
    clock=SimpleNamespace(ns=10_000_000_000)
    def ns():clock.ns+=1000;return clock.ns
    fake_time=SimpleNamespace(perf_counter_ns=ns,perf_counter=lambda:ns()/1e9,
                              sleep=lambda _:setattr(clock,'ns',clock.ns+2_000_000_000))
    ctx=SimpleNamespace(run_dir=root,pc2_media_cancel=threading.Event(),
                        pc2_media_targets={0:dict(pid=10,creationTicks=100,moduleBase=65536)},
                        inst=lambda _:SimpleNamespace(pid=10))
    product.publish(ctx)
    raw=None
    if case=='success':
        retained=RETAINED
        raw=adapter.read(retained/'native-control.json')
        test.assertTrue(raw['offlineGeneratedPixels']);test.assertFalse(raw['nativeGameExecuted'])
        proc.pid=raw['processes']['encoder']['identity']['parentPid']
        test.assertEqual(proc.pid,raw['processes']['verifier']['identity']['parentPid'])
        first=raw['processes']['encoder']['startedNs'];last=raw['processes']['verifier']['absenceCheckedNs']
        clock.ns=first-1_000_000
        for role in ('encoder','verifier'):
            ident=raw['processes'][role]['identity']
            product.products[role]=dict(packagePath=ident['imagePath'],sha256=ident['imageSha256'])
        product.driver=dict(pid=os.getpid(),parentPid=1,creationTicks=1,imagePath=str(root/'python.exe'),imageSha256='b'*64,argv=['python','offline-control'])
        target=dict(pid=10,creationTicks=100,moduleBase=65536,imagePath=str(root/'game.exe'),
                    imageName='KINGDOM HEARTS II FINAL MIX.exe',imageSha256=product.game_sha256,
                    ownedRecordSha256='c'*64,handleHeldThroughFinalization=True)
        capture=dict(expectedRequestSeq=1,doneSeq=1,nativeStatus=0,framesWritten=90,requestedInterval=1,
                     width=320,height=180,renderer=1,backbufferFormat=28,gameFpsBefore=30.0,
                     gameFpsDuringCapture=30.0,encodedFps=30.0,startedNs=first-500_000,
                     finishedNs=first-100_000,captureLeaseReleased=True)
        native=dict(ok=True,target=target,capture=capture,processes=copy.deepcopy(raw['processes']))
        for role in ('encoder','verifier'):
            for suffix in ('stdout.log','stderr.log','identity.json','closed.json'):
                shutil.copyfile(retained/f'hb_arrival_peer0_{role}.{suffix}',root/f'hb_arrival_peer0_{role}.{suffix}')
        shutil.copyfile(retained/'hb_arrival_peer0.mp4',root/'hb_arrival_peer0.mp4')
        shutil.copytree(retained/'hb_arrival_peer0_frames',root/'hb_arrival_peer0_frames')
        # The fixture itself retains original raw timestamps/argv/identities.
        # Only native target/capture and process execution/hash leaves are mocked.
        def populate():
            (root/'hb_arrival_peer0_helper.stdout.log').write_text(json.dumps(native),encoding='utf-8')
            proc.returncode=0;clock.ns=last+1000
        deadline=(last+30_000_000_000)/1e9
        output=root/'hb_arrival_peer0.mp4'
        # Admission requires fresh absent output; mocked execution publishes it.
        saved_video=output.read_bytes();output.unlink()
    else:
        deadline=100.0
        def populate():
            if case=='empty_stdout':proc.returncode=1
            else:ctx.pc2_media_cancel.set()
    def identity(handle):
        if handle==2 and case=='identity_failure':raise OSError('helper identity query failed after create')
        return dict(creationTicks=1000 if handle==2 else 100,
                    imagePath=product.products['clipCli']['packagePath'] if handle==2 else str(root/'game.exe'))
    def resume(p,admitted,admission_check):
        test.assertTrue((root/'hb_arrival_peer0_helper.identity.json').is_file())
        test.assertEqual(adapter.read(root/'hb_arrival_peer0_helper.identity.json')['creationTicks'],admitted['creationTicks'])
        admission_check()
        proc.resumed=True
        if case=='success':(root/'hb_arrival_peer0.mp4').write_bytes(saved_video)
        populate()
    def terminate():
        proc.terminated=True
        if case=='terminate_failure':raise OSError('retained handle termination failed')
        if case!='wait_timeout':proc.returncode=125
    def wait(**kwargs):
        if proc.returncode is None:raise adapter.subprocess.TimeoutExpired('fake-helper',kwargs.get('timeout'))
        return proc.returncode
    proc.poll=lambda:proc.returncode;proc.terminate=terminate;proc.wait=wait
    product.native=SimpleNamespace(open=lambda *a,**k:1,identity=identity,base=lambda _:65536,
        close=lambda _:None,resume=resume,absent=lambda ident:proc.returncode is not None,
        k=SimpleNamespace(CreateEventW=lambda *a:3,SetEvent=lambda _:True,WaitForSingleObject=lambda *a:258))
    boundary=case.startswith(('cancel_in_', 'deadline_in_', 'replace_in_'))
    resume_calls=[]
    if boundary:
        def mutate():
            if case.startswith('cancel_'):ctx.pc2_media_cancel.set()
            elif case.startswith('deadline_'):clock.ns=101_000_000_000
            else:ctx.pc2_media_targets[0]['creationTicks']+=1
        actual=object.__new__(adapter.Native)
        actual.close=lambda _:None
        actual.identity=identity
        configure_resume(actual,proc,resume_calls,
                         mutate if case.endswith('census') else lambda:None,
                         mutate if case.endswith('identity') else lambda:None)
        product.native.resume=actual.resume
    ctx.pc2_media_products=copy.deepcopy(product.products)
    original_write=adapter.write
    def write(path,value):
        if case=='journal_failure' and Path(path).name.endswith('helper.identity.json'):
            raise OSError('helper journal failed before resume')
        return original_write(path,value)
    real_sha=metrics.sha
    def sha(path):
        for role in ('encoder','verifier'):
            row=product.products[role]
            if case=='success' and Path(path)==Path(row['packagePath']):return row['sha256']
        return real_sha(path)
    def popen(*args,**kwargs):
        test.assertTrue(kwargs['creationflags']&4,'helper must be created suspended')
        return proc
    with patch.object(adapter.subprocess,'Popen',side_effect=popen),patch.object(adapter,'write',side_effect=write),\
         patch.object(adapter,'time',fake_time),patch.object(metrics,'time',fake_time),patch.object(metrics,'sha',side_effect=sha):
        if case=='success':
            result=product.clip(ctx,0,root/'hb_arrival_peer0.mp4',float(deadline))
            test.assertTrue(result['ok']);test.assertFalse(result['acceptance'])
            test.assertEqual(result['processes']['encoder'],raw['processes']['encoder'])
            test.assertEqual(result['processes']['verifier'],raw['processes']['verifier'])
            for role,stream in (('encoder','stdout'),('encoder','stderr'),('verifier','stdout')):
                test.assertEqual(result['rawLogs'][role][stream]['bytes'],0)
            test.assertEqual(result['decode']['decodedFrames'],90)
            test.assertTrue(proc.resumed)
            return
        with test.assertRaises(StepFailed):product.clip(ctx,0,root/'hb_arrival_peer0.mp4',float(deadline))
    failure=adapter.read(root/'hb_arrival_peer0_clip_failure.json')
    test.assertFalse(failure['ok']);test.assertFalse(failure['acceptance'])
    if boundary or case in ('identity_failure','journal_failure','terminate_failure','wait_timeout'):
        test.assertFalse(failure['closure']['ownedDescendantsAbsent'])
        test.assertIn('UNKNOWN',failure['error'])
    else:test.assertTrue(failure['closure']['ownedDescendantsAbsent'])
    if case=='empty_stdout':
        test.assertIsNone(failure['rawCliReceipt'])
        test.assertEqual((root/'hb_arrival_peer0_helper.stdout.log').read_bytes(),b'')
    if case in ('identity_failure','journal_failure'):test.assertFalse(proc.resumed)
    if case=='journal_failure':test.assertEqual(len(failure['spawnedIdentities']),1)
    if case in ('terminate_failure','wait_timeout'):test.assertIsNone(proc.returncode)
    if case=='cleanup_recovery':test.assertTrue(proc.terminated)
    if boundary:
        test.assertEqual(resume_calls,[])
        test.assertFalse(proc.resumed)
        test.assertTrue(proc.terminated)
        test.assertEqual(proc.returncode,125)
        test.assertEqual(len(failure['spawnedIdentities']),1)
        test.assertEqual(failure['spawnedIdentities'][0]['pid'],proc.pid)
        test.assertIsNone(failure['rawCliReceipt'])
        test.assertIn('cancel' if case.startswith('cancel_') else 'deadline' if case.startswith('deadline_') else 'target replaced',failure['error'])



def configure_resume(native,proc,calls,census_read=lambda:None,identity_read=lambda:None,mode='valid'):
    """Only WinAPI leaves are intercepted; use the production Native.resume."""
    original_identity=native.identity
    identity_count=0
    def identity(handle):
        nonlocal identity_count
        identity_count+=1
        if identity_count==2:identity_read()
        return original_identity(handle)
    native.identity=identity
    def first(handle,ptr):
        ptr._obj.tid=7;ptr._obj.owner=proc.pid;return True
    def next_(handle,ptr):census_read();ctypes.set_last_error(18);return False
    def times(handle,*ptrs):ptrs[0]._obj.dwLowDateTime=1000 if mode!='old_thread' else 99;return True
    def resume(handle):calls.append(('resume',handle));return 1
    native.k=SimpleNamespace(CreateToolhelp32Snapshot=lambda *a:3,Thread32First=first,Thread32Next=next_,
        OpenThread=lambda *a:4,GetProcessIdOfThread=lambda _:proc.pid if mode!='wrong_owner' else proc.pid+1,
        GetThreadTimes=times,ResumeThread=resume,WaitForSingleObject=lambda *a:258 if mode!='dead_process' else 0)


def resume_control(test,mode):
    native=object.__new__(adapter.Native);calls=[];expected=dict(creationTicks=100,imagePath='helper.exe')
    native.identity=lambda _:expected.copy();native.close=lambda h:calls.append(('close',h))
    proc=SimpleNamespace(pid=999,_handle=2)
    configure_resume(native,proc,calls,mode=mode)
    def admission():calls.append(('admission',None))
    if mode=='valid':
        native.resume(proc,expected,admission)
        test.assertEqual(calls[-3:],[('admission',None),('resume',4),('close',4)])
    else:
        with test.assertRaises(OSError):native.resume(proc,expected,admission)
        test.assertNotIn(('resume',4),calls)
        test.assertNotIn(('admission',None),calls)
