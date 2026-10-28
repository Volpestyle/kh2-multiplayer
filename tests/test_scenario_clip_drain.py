"""VUH-2912 (vuh2912-hp-05): a run whose step fails before its clip_wait finishes its background clips before the
teardown kills the games, and a failed step's own diagnostics (StepFailed.details) reach its report entry. The real
run_scenario with a fake step and no game.

    python -B -m unittest discover -s tests -p test_scenario_clip_drain.py -v
"""
import importlib.util
import json
import tempfile
import threading
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("clip_drain_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)


class ClipDrainTest(unittest.TestCase):
    def run_fake(self, step_fn):
        with tempfile.TemporaryDirectory() as directory:
            runs, scenario = Path(directory) / "runs", Path(directory) / "fake.json"
            scenario.write_text(json.dumps({"name": "fake", "steps": [{"do": "fake"}, {"do": "never"}]}), encoding="utf-8")
            saved = (RUN.RUNS, dict(RUN.STEPS))
            RUN.RUNS = runs
            RUN.STEPS.update(fake=step_fn, never=lambda ctx, step: self.fail("a step after the failure ran"))
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


if __name__ == "__main__":
    unittest.main()
