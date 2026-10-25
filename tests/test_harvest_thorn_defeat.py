"""Offline qualification controls: no game, desk, network or real ffmpeg job."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("harvest_thorn_defeat", ROOT / "tools/scenario/harvest_thorn_defeat.py")
H = importlib.util.module_from_spec(spec)
spec.loader.exec_module(H)
END = "[mission-mirror] host end trap=mission_dead_boss arg=4294967295\n"
ENTRY = "[thorn-defeat] 315 entry path=native context=AA\n"


class HarvestTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.run = self.root / "run"
        self.run.mkdir()
        self.write(self.run / "report.json", dict(instances=[101, 102], status="pass"))
        self.host = self.run / "kh2coop_inject_101.log"
        self.friend = self.run / "kh2coop_inject_102.log"
        self.host.write_text("TT34_MS304.bar\n" + END + ENTRY)
        self.friend.write_text(ENTRY)

    def write(self, path, data):
        path.write_text(json.dumps(data), encoding="utf-8")

    def harvest(self, name="output", **kwargs):
        return H.harvest(self.run, self.root / name, **kwargs)

    def review(self):
        self.harvest("inventory")
        data = H.load(self.root / "inventory/review-template.json")
        for name in H.VISUAL:
            data[name] = dict(status="PASS", evidence="Retained reviewed frames/report assertion")
        data["timing"].update(samePc=True, precedingFramesNotDefeat=True,
                              host=dict(receipt="host.mp4.receipt.json", firstVisibleFrame=2),
                              friend=dict(receipt="friend.mp4.receipt.json", firstVisibleFrame=2))
        path = self.root / "review.json"
        self.write(path, data)
        return path

    def captures(self, offset=0):
        for name, pid, shift in (("host", 101, 0), ("friend", 102, offset)):
            sidecar = self.run / f"{name}.mp4.capture-time.jsonl"
            rows = [dict(schema=1, frame=i, qpcBegin=1000 + shift + 33*i,
                         qpcEnd=1002 + shift + 33*i, present=10 + i*2, qpcFrequency=1000) for i in range(5)]
            sidecar.write_text("\n".join(json.dumps(row) for row in rows))
            self.write(self.run / f"{name}.mp4.receipt.json",
                       dict(ok=True, frames=5, processId=pid, gameFpsBefore=60, gameFpsDuringCapture=60,
                            captureSeconds=.165, clipFps=30, clockDomain="PC1",
                            timingMethod="native-Present-QPC-bounds-v1", captureTiming=sidecar.name))

    def closure(self):
        path = self.root / "closure"
        path.mkdir()
        self.write(path / "closure.json", dict(run="run", fullInstallDiffPass=True, protectedSavesUnchanged=True,
                                              ownedProcessesGone=True, relayClosed=True))
        self.write(path / "install-diff.json", dict(matched=True, beforeCount=541, afterCount=541,
                                                   added=[], removed=[], changed=[]))
        return path

    def test_wipe_is_unqualified_not_a_zero_start_failure(self):
        self.host.write_text("TT34_MS304.bar\n[downed] full party down\n")
        self.friend.write_text("")
        result = self.harvest()
        self.assertEqual(result["status"], "UNQUALIFIED")
        self.assertFalse(result["checks"]["native"]["counts"]["zeroStartsQualificationFailure"])

    def test_kill_without_end16_or_local315_is_failure(self):
        self.friend.write_text("[thorn-defeat] host315 generic replay refused; no qualified owner epoch=3\n")
        result = self.harvest()
        self.assertEqual(result["status"], "FAIL")
        self.assertTrue(result["checks"]["native"]["counts"]["noEnd16OrLocal315"])

    def test_replay_plus_native_is_failure(self):
        self.friend.write_text(ENTRY + "[thorn-defeat] 315 entry path=replay context=BB\n")
        self.assertEqual(self.harvest()["status"], "FAIL")

    def test_missing_end16_with_one_local_native315_keeps_native_path(self):
        result = self.harvest()
        self.assertEqual(result["checks"]["native"]["status"], "PASS")
        self.assertEqual(result["status"], "UNQUALIFIED")  # no pixels/progression/closure yet

    def test_partial_native_context_is_failure(self):
        self.friend.write_text(ENTRY + "[thorn-defeat] 315 context ok=0 before=AA after=BB\n")
        self.assertEqual(self.harvest()["status"], "FAIL")

    def test_complete_review_uses_real_qpc_comparison(self):
        self.captures(offset=10)
        review = self.review()
        with patch.object(H, "decode_video", return_value=dict(strictDecode="PASS")):
            result = self.harvest("final", review_path=review, closure=self.closure(), decode=True)
        self.assertEqual(result["status"], "PASS")
        self.assertLess(result["checks"]["timing"]["bounds"]["absoluteSkewUpperBoundMs"], 1000)

    def test_definite_visible_delay_fails(self):
        self.captures(offset=1500)
        review = self.review()
        with patch.object(H, "decode_video", return_value=dict(strictDecode="PASS")):
            result = self.harvest("final", review_path=review, closure=self.closure(), decode=True)
        self.assertEqual(result["checks"]["timing"]["status"], "FAIL")
        self.assertEqual(result["status"], "FAIL")

    def test_corrupt_video_cannot_certify_timing(self):
        self.captures()
        review = self.review()
        with patch.object(H, "decode_video", side_effect=ValueError("corrupt decode")):
            result = self.harvest("final", review_path=review, closure=self.closure(), decode=True)
        self.assertEqual(result["status"], "UNQUALIFIED")

    def test_zero_exit_with_frame_count_decoder_errors_is_refused(self):
        self.captures()
        (self.run / "host.mp4").write_bytes(b"controlled decode stub")
        replies = [SimpleNamespace(stderr=b"", stdout=b""),
                   SimpleNamespace(stderr=b"[h264] error while decoding", stdout=b'{"streams":[{"nb_read_frames":"5"}]}')]
        with patch.object(H.subprocess, "run", side_effect=replies):
            with self.assertRaisesRegex(ValueError, "ffprobe reported decode errors"):
                H.decode_video(self.run / "host.mp4.receipt.json", 5, 101)

    def test_wrong_seat_clip_is_refused_before_any_decoder(self):
        self.captures()
        (self.run / "host.mp4").write_bytes(b"controlled decode stub")
        with patch.object(H.subprocess, "run") as decoder:
            with self.assertRaisesRegex(ValueError, "wrong seat"):
                H.decode_video(self.run / "host.mp4.receipt.json", 5, 102)
        decoder.assert_not_called()

    def test_review_binds_full_logs_and_capture_receipts(self):
        self.captures()
        review = self.review()
        self.friend.write_text(ENTRY + ENTRY)
        with self.assertRaisesRegex(ValueError, "stale"):
            self.harvest("final", review_path=review)

    def test_changed_capture_sidecar_invalidates_review(self):
        self.captures()
        review = self.review()
        with (self.run / "host.mp4.capture-time.jsonl").open("a") as stream:
            stream.write("\n{}")
        with self.assertRaisesRegex(ValueError, "stale"):
            self.harvest("final", review_path=review)

    def test_wrong_run_closure_is_unqualified(self):
        closure = self.closure()
        data = H.load(closure / "closure.json")
        data["run"] = "different-run"
        self.write(closure / "closure.json", data)
        self.assertEqual(self.harvest(closure=closure)["checks"]["closure"]["status"], "UNQUALIFIED")

    def test_inputs_and_existing_output_remain_untouched(self):
        before = {p.name: p.read_bytes() for p in self.run.iterdir()}
        with self.assertRaises(ValueError):
            H.harvest(self.run, self.run / "forbidden")
        self.harvest()
        output = self.root / "output/receipt.json"
        original = output.read_bytes()
        with self.assertRaises(ValueError):
            self.harvest()
        self.assertEqual(original, output.read_bytes())
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.run.iterdir()})


if __name__ == "__main__":
    unittest.main()
