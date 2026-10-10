"""Offline contract controls. No native execution, games or PC2 contact."""
import argparse,copy,json,unittest
from pathlib import Path
from .release import phase_deadline,validate_release

class Controls(unittest.TestCase):
    def fixture(self,seconds=16):
        helper=dict(pid=42,creationTicks=123,imagePath='fixture-only')
        target=dict(pid=7,creationTicks=456,moduleBase=65536)
        cap=dict(expectedRequestSeq=4,doneSeq=4,nativeStatus=0,framesWritten=seconds*30,
                 startedNs=1_000_000_000,finishedNs=(seconds+1)*1_000_000_000,captureLeaseReleased=True)
        row=dict(schema='pc2-capture-release-v1',helper={k:helper[k] for k in ('pid','creationTicks')},
                 target=target,seconds=seconds,frames=seconds*30,output='absolute-fixture/f_%05u.bmp',capture=cap)
        kw=dict(helper=helper,target=target,seconds=seconds,raw_pattern=row['output'],request=4,
                worker_started_ns=500_000_000,now_ns=30_000_000_000,
                sample=dict(request=4,completeFrames=seconds*30,recording=False,output=row['output']))
        return row,kw
    def test_only_two_modes_and_original_join_cap(self):
        self.assertEqual(phase_deadline(seconds=3,started=10.,join_deadline=600.),55.)
        self.assertEqual(phase_deadline(seconds=16,started=10.,join_deadline=600.),100.)
        self.assertEqual(phase_deadline(seconds=16,started=500.,join_deadline=600.),540.)
        for seconds in (True,3.,12,17):
            with self.assertRaises(ValueError):phase_deadline(seconds=seconds,started=10.,join_deadline=600.)
        with self.assertRaises(ValueError):phase_deadline(seconds=16,started=540.,join_deadline=600.)
    def test_original_helper_target_request_path_and_types(self):
        for mode in (3,16):
            row,kw=self.fixture(mode);self.assertIs(validate_release(row,**kw),row)
            for path,value in ((('helper','creationTicks'),124),(('helper','pid'),True),(('target','pid'),8),(('capture','doneSeq'),3),(('capture','nativeStatus'),True),(('output',),'wrong'),(('frames',),90 if mode==16 else 480)):
                forged=copy.deepcopy(row);obj=forged
                for key in path[:-1]:obj=obj[key]
                obj[path[-1]]=value
                with self.assertRaises(ValueError):validate_release(forged,**kw)
    def test_missing_early_draining_release_refused(self):
        row,kw=self.fixture()
        for key in ('helper','capture','output'):
            bad=copy.deepcopy(row);del bad[key]
            with self.assertRaises((KeyError,ValueError)):validate_release(bad,**kw)
        for key,value in (('completeFrames',479),('recording',True),('request',3),('output','wrong')):
            bad=copy.deepcopy(kw);bad['sample'][key]=value
            with self.assertRaises(ValueError):validate_release(row,**bad)
        row['capture']['captureLeaseReleased']=False
        with self.assertRaises(ValueError):validate_release(row,**kw)
    def test_acquisition_and_clock_bounds_refused(self):
        for key,value in (('startedNs',1),('finishedNs',31_000_000_000),('finishedNs',25_000_000_000)):
            row,kw=self.fixture();row['capture'][key]=value
            with self.assertRaises(ValueError):validate_release(row,**kw)
    def test_measured_reserve_local_view_count_and_separate_system_floor(self):
        import tempfile,hashlib
        from unittest.mock import patch
        from .capacity import admit
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);p=root/'measurement.json'
            p.write_text(json.dumps(dict(ok=True,fullDecodeVerified=True,width=1920,height=1080,frames=480,seconds=16,fps=30,peakScratchBytes=8*1024**3)))
            pin=hashlib.sha256(p.read_bytes()).hexdigest()
            def usage(path):return type('Usage',(),dict(free=(20 if str(path)=='C:/' else 16)*1024**3))()
            with patch('shutil.disk_usage',side_effect=usage), patch.object(Path,'anchor',property(lambda p: 'D:/'+'' if p==root else 'C:/')):
                result=admit(root,measurement_path=p,measurement_sha256=pin,retained_clips=1)
                self.assertEqual(result['mediaVolumeFloorBytes'],1024**3)
                with self.assertRaises(ValueError):admit(root,measurement_path=p,measurement_sha256=pin,retained_clips=2)
            with patch('shutil.disk_usage',return_value=type('Usage',(),dict(free=14*1024**3))()):
                with self.assertRaises(ValueError):admit(root,measurement_path=p,measurement_sha256=pin,retained_clips=1)
            p.write_text('{}')
            with self.assertRaises(ValueError):admit(root,measurement_path=p,measurement_sha256=pin,retained_clips=1)

    def test_native_release_order_and_busy_acquire_remain(self):
        # Static ordering control, NOT a native/destructor behavioral proof.
        source=(Path(__file__).parents[2]/'kh2ctl/src/owned_clip_v2.hpp').read_text()
        destruction=source.index('// This point is AFTER CaptureLease destruction')
        publish=source.index('std::filesystem::rename(releaseTemporary, releasePath)')
        encode=source.index('const auto encoder = Child(encode')
        self.assertLess(destruction,publish);self.assertLess(publish,encode)
        self.assertIn('lease.Acquire(pid) != kh2coop::CaptureLeaseStatus::Acquired',source)
        self.assertIn('bound.Check(); std::filesystem::rename(releaseTemporary, releasePath); bound.Check();',source)

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);args=ap.parse_args()
    r=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Controls))
    args.out.write_text(json.dumps(dict(ok=r.wasSuccessful(),tests=r.testsRun,nativeBuilt=False,nativeRun=False,pc2Contact=False,coverageAuthority=False),indent=2))
    raise SystemExit(0 if r.wasSuccessful() else 1)
if __name__=='__main__':main()
