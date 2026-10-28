"""VUH-2912 (vuh2912-hp-05): a run whose step fails before its clip_wait finishes its background clips before the
teardown kills the games, and a failed step's own diagnostics (StepFailed.details) reach its report entry. The real
run_scenario with a fake step and no game. Ione 2990c3d8: a failure while clip_wait waits leaves its clips to that
drain, and a clip_wait with "require" fails the run unless every clip finished as a finalized MP4.

    python -B -m unittest discover -s tests -p test_scenario_clip_drain.py -v
"""
import importlib.util
import json
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("clip_drain_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)


class ClipDrainTest(unittest.TestCase):
    def run_fake(self, step_fn, fails=True):
        with tempfile.TemporaryDirectory() as directory:
            runs, scenario = Path(directory) / "runs", Path(directory) / "fake.json"
            scenario.write_text(json.dumps({"name": "fake", "steps": [{"do": "fake"}, {"do": "never"}]}), encoding="utf-8")
            saved = (RUN.RUNS, dict(RUN.STEPS))
            RUN.RUNS = runs
            RUN.STEPS.update(fake=step_fn, never=lambda ctx, step: self.fail("a step after the failure ran") if fails else {})
            try:
                report = RUN.run_scenario(scenario, 1)
                files = sorted(p.name for p in next(runs.iterdir()).iterdir())
            finally:
                RUN.RUNS = saved[0]
                RUN.STEPS.clear()
                RUN.STEPS.update(saved[1])
            return report, files

    @staticmethod
    def encoder(ctx, name, seconds, finished):
        """A background clip whose encoder writes its file after `seconds`, as kh2ctl clip does at the end."""
        out, result = ctx.run_dir / name, {}

        def encode():
            time.sleep(seconds)
            out.write_bytes(b"moov")
            result.update(ok=True)
            finished.append(time.monotonic())

        thread = threading.Thread(target=encode, daemon=True)
        thread.start()
        ctx.saved.setdefault("_background_clips", []).append((thread, out, result))

    def test_a_failed_step_drains_its_clips_and_keeps_its_details(self):
        finished = []

        def fake(ctx, step):
            self.encoder(ctx, "downs_0.mp4", 0.3, finished)
            self.encoder(ctx, "downs_1.mp4", 0.4, finished)
            error = RUN.StepFailed("owner_downs: HARNESS FAILURE")
            error.details = {"owner_downs": {"guards": [[{"result": "timeout", "holds": 2}], []]}}
            raise error

        report, files = self.run_fake(fake)
        self.assertEqual(report["status"], "fail")
        self.assertEqual([s["do"] for s in report["steps"]], ["fake"])
        self.assertEqual(report["steps"][0]["details"], {"owner_downs": {"guards": [[{"result": "timeout", "holds": 2}], []]}})
        self.assertEqual(len(finished), 2)  # both encoders finished before the run returned
        self.assertEqual([c["path"] for c in report["clipDrain"]["clips"]], ["downs_0.mp4", "downs_1.mp4"])
        self.assertTrue(all(c["ok"] for c in report["clipDrain"]["clips"]))
        self.assertEqual(report["clipDrain"]["unfinished"], [])
        self.assertIn("downs_0.mp4", files)
        self.assertIn("downs_0.mp4", report["artifacts"])

    def test_a_failure_without_details_or_clips_reports_as_before(self):
        def fake(ctx, step):
            raise RUN.StepFailed("plain")

        report, _ = self.run_fake(fake)
        self.assertNotIn("details", report["steps"][0])
        self.assertNotIn("clipDrain", report)

    def test_the_drain_is_bounded(self):
        release = threading.Event()
        thread = threading.Thread(target=release.wait, daemon=True)
        thread.start()
        with tempfile.TemporaryDirectory() as directory:
            ctx = type("Ctx", (), {"artifacts": [], "run_dir": Path(directory)})()
            t0 = time.monotonic()
            drained = RUN.drain_clips(ctx, [(thread, Path(directory) / "stuck_0.mp4", {})], seconds=0.2)
            self.assertLess(time.monotonic() - t0, 2)
        release.set()
        self.assertEqual(drained, {"clips": [], "unfinished": ["stuck_0.mp4"]})

    def test_a_failure_while_waiting_leaves_the_clips_to_the_drain(self):
        finished = []

        def fake(ctx, step):
            self.encoder(ctx, "round_host_0.mp4", 0.3, finished)
            self.encoder(ctx, "round_friend_1.mp4", 0.4, finished)

            def exited(seconds):  # the instance check inside ctx.sleep finds a dead game mid-wait
                raise RUN.StepFailed("instance 1 exited")

            ctx.sleep = exited
            RUN.step_clip_wait(ctx, {"require": True})

        report, files = self.run_fake(fake)
        self.assertEqual(report["steps"][0]["error"], "instance 1 exited")
        self.assertEqual(len(finished), 2)  # both encoders finished before the run returned
        self.assertEqual([c["path"] for c in report["clipDrain"]["clips"]], ["round_host_0.mp4", "round_friend_1.mp4"])
        self.assertTrue(all(c["ok"] for c in report["clipDrain"]["clips"]))
        self.assertEqual(report["clipDrain"]["unfinished"], [])
        self.assertIn("round_friend_1.mp4", files)
        self.assertIn("round_friend_1.mp4", report["artifacts"])


def box(kind: bytes, payload: bytes = b"") -> bytes:
    return (8 + len(payload)).to_bytes(4, "big") + kind + payload


FINALIZED = box(b"ftyp", b"isom") + box(b"mdat", b"x" * 64) + box(b"moov", box(b"mvhd"))
CUT = box(b"ftyp", b"isom") + (8 + 4096).to_bytes(4, "big") + b"mdat" + b"x" * 64  # killed while writing mdat


class RequiredClipsTest(unittest.TestCase):
    """Ione 2990c3d8 (2): the acceptance pair's clip_wait fails the run unless both clips finished as finalized MP4s."""

    def collect(self, clips, step):
        """clips: (name, result, file bytes or None), already finished; the real run_scenario then runs clip_wait."""
        def fake(ctx, _):
            for name, result, content in clips:
                if content is not None:
                    (ctx.run_dir / name).write_bytes(content)
                thread = threading.Thread(target=lambda: None)
                thread.start()
                thread.join()
                ctx.saved.setdefault("_background_clips", []).append((thread, ctx.run_dir / name, dict(result)))
            RUN.step_clip_wait(ctx, step)

        return ClipDrainTest.run_fake(self, fake, fails=False)

    def assert_evidence_failure(self, report, failed):
        self.assertEqual(report["status"], "fail")
        self.assertEqual([s["do"] for s in report["steps"]], ["fake"])  # nothing after the failure ran
        self.assertIn("EVIDENCE FAILURE (clips)", report["steps"][0]["error"])
        self.assertIn(repr(failed), report["steps"][0]["error"])
        self.assertEqual(len(report["steps"][0]["details"]["clips"]), 2)  # every receipt is kept
        self.assertNotIn("clipDrain", report)  # collected, not left to the teardown

    def test_a_finalized_pair_passes(self):
        report, _ = self.collect([("host_0.mp4", {"ok": True}, FINALIZED), ("friend_1.mp4", {"ok": True}, FINALIZED)],
                                 {"require": True})
        self.assertEqual(report["status"], "pass", report)
        self.assertTrue({"host_0.mp4", "friend_1.mp4"} <= set(report["artifacts"]))

    def test_a_failed_pair_without_files_fails_the_run(self):
        failed = {"ok": False, "error": "encoder failed"}
        report, _ = self.collect([("host_0.mp4", failed, None), ("friend_1.mp4", failed, None)], {"require": True})
        self.assert_evidence_failure(report, ["host_0.mp4", "friend_1.mp4"])
        self.assertEqual(report["steps"][0]["details"]["clips"][0]["error"], "encoder failed")

    def test_one_failed_seat_fails_the_run(self):
        report, _ = self.collect([("host_0.mp4", {"ok": True}, FINALIZED),
                                  ("friend_1.mp4", {"ok": False, "error": "encoder failed"}, None)], {"require": True})
        self.assert_evidence_failure(report, ["friend_1.mp4"])

    def test_a_failed_encoder_with_a_finalized_file_fails_the_run(self):
        report, _ = self.collect([("host_0.mp4", {"ok": True}, FINALIZED),
                                  ("friend_1.mp4", {"ok": False, "error": "exit 1"}, FINALIZED)], {"require": True})
        self.assert_evidence_failure(report, ["friend_1.mp4"])

    def test_a_missing_file_reported_ok_fails_the_run(self):
        report, _ = self.collect([("host_0.mp4", {"ok": True}, FINALIZED), ("friend_1.mp4", {"ok": True}, None)],
                                 {"require": True})
        self.assert_evidence_failure(report, ["friend_1.mp4"])

    def test_an_unfinalized_file_fails_the_run(self):
        report, _ = self.collect([("host_0.mp4", {"ok": True}, CUT), ("friend_1.mp4", {"ok": True}, FINALIZED)],
                                 {"require": True})
        self.assert_evidence_failure(report, ["host_0.mp4"])
        self.assertEqual([c["finalized"] for c in report["steps"][0]["details"]["clips"]], [False, True])

    def test_a_worker_exception_is_a_failed_receipt_that_fails_the_run(self):
        def fake(ctx, _):
            for name in ("host_0.mp4", "friend_1.mp4"):
                result = {}
                with mock.patch.object(RUN, "kh2ctl", side_effect=subprocess.TimeoutExpired("kh2ctl clip", 180)):
                    thread = threading.Thread(target=RUN.film_into, args=(result, "clip", "--out", name),
                                              kwargs={"pid": 100, "timeout": 180})
                    thread.start()
                    thread.join()
                ctx.saved.setdefault("_background_clips", []).append((thread, ctx.run_dir / name, result))
            RUN.step_clip_wait(ctx, {"require": True})

        report, _ = ClipDrainTest.run_fake(self, fake)
        self.assert_evidence_failure(report, ["host_0.mp4", "friend_1.mp4"])
        receipt = report["steps"][0]["details"]["clips"][0]
        self.assertFalse(receipt["ok"])
        self.assertTrue(receipt["error"].startswith("TimeoutExpired: "), receipt)

    def test_no_clips_fails_a_required_wait(self):
        report, _ = self.collect([], {"require": True})
        self.assertEqual(report["status"], "fail")
        self.assertIn("no clips", report["steps"][0]["error"])

    def test_without_require_a_failed_pair_is_only_its_receipts(self):  # diagnostic clips elsewhere are unchanged
        failed = {"ok": False, "error": "encoder failed"}
        report, _ = self.collect([("host_0.mp4", failed, None), ("friend_1.mp4", failed, None)], {})
        self.assertEqual(report["status"], "pass", report)

    def test_mp4_finalized_reads_top_level_boxes(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "x.mp4"
            large = (1).to_bytes(4, "big") + b"mdat" + (16 + 8).to_bytes(8, "big") + b"y" * 8  # a 64-bit size
            moov = box(b"moov", box(b"mvhd", b"z" * 92))
            for content, finalized in [(FINALIZED, True), (box(b"moov") + box(b"mdat"), True), (CUT, False),
                                       (box(b"ftyp") + large + box(b"moov"), True), (b"", False),
                                       (box(b"ftyp") + (0).to_bytes(4, "big") + b"mdat", False),
                                       # Ione 1afb82ac NOTE: a moov whose header claims more than the file holds
                                       (box(b"ftyp") + moov[:40], False), (box(b"ftyp") + moov[:8], False),
                                       (box(b"ftyp") + moov, True),
                                       (box(b"ftyp") + (0).to_bytes(4, "big") + b"moov" + b"w" * 16, True),
                                       (box(b"ftyp") + (1).to_bytes(4, "big") + b"moov" + (16).to_bytes(4, "big"), False),
                                       (box(b"ftyp") + (4).to_bytes(4, "big") + b"moov", False)]:
                path.write_bytes(content)
                self.assertEqual(RUN.mp4_finalized(path), finalized, content[:40])
            self.assertFalse(RUN.mp4_finalized(Path(directory) / "missing.mp4"))


if __name__ == "__main__":
    unittest.main()
