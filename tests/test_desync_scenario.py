"""Prepared artifact linkage only: never constructs Context or invokes kh2ctl."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("USERPROFILE", str(Path.home()))
SPEC = importlib.util.spec_from_file_location("desync_scenario_runner", ROOT / "tools/scenario/run.py")
RUNNER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RUNNER)


class DesyncLinkageTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.no_cli = patch.object(RUNNER, "kh2ctl", side_effect=AssertionError("linkage must not execute CLI"))
        self.no_cli.start()
        self.addCleanup(self.no_cli.stop)

    def manifest(self, number, status):
        path = self.root / "desync-reports" / ("a" * 32) / str(number) / "manifest.json"
        path.parent.mkdir(parents=True)
        path.write_text(json.dumps({"schemaVersion": 1, "key": {"sessionId": "a" * 32, "reportId": number},
                                    "collectionStatus": status}), encoding="utf-8")
        return path

    def test_complete_and_partial_preserve_original_and_gameplay_status(self):
        good = self.manifest(1, "complete")
        bad = self.manifest(2, "partial")
        raw = bad.read_bytes()
        (bad.parent / "peer_1_2_3.png").write_bytes(b"partial raw screenshot bytes")
        result = RUNNER.link_desync_reports(self.root)
        self.assertEqual(result["collectionStatus"], "partial")
        self.assertEqual([r["collectionStatus"] for r in result["reports"]], ["complete", "partial"])
        self.assertEqual(bad.read_bytes(), raw)
        self.assertTrue(good.exists())
        report = {"scenario": "synthetic linkage", "attempt": 1, "status": "pass", "started": "test",
                  "steps": [], "desyncCollection": result}
        rendered = RUNNER.render_md(report)
        self.assertIn("PASS", rendered)
        self.assertIn("**PARTIAL**", rendered)
        self.assertIn(result["reports"][1]["manifest"], rendered)

    def test_complete_only_and_no_reports_are_distinct(self):
        self.assertEqual(RUNNER.link_desync_reports(self.root)["collectionStatus"], "none")
        self.assertEqual(RUNNER.link_desync_reports(self.root)["filesystemEvidence"], "unavailable")
        self.manifest(1, "complete")
        self.assertEqual(RUNNER.link_desync_reports(self.root)["collectionStatus"], "complete")

    def test_collecting_malformed_unknown_are_not_complete(self):
        self.manifest(1, "collecting")
        malformed = self.manifest(2, "complete")
        malformed.write_text("{truncated", encoding="utf-8")
        self.manifest(3, "invented")
        result = RUNNER.link_desync_reports(self.root)
        self.assertEqual(result["collectionStatus"], "partial")
        self.assertEqual(len(result["reports"]), 3)
        self.assertTrue(all(r["collectionStatus"] == "partial" and r.get("error") for r in result["reports"]))
        self.assertEqual(malformed.read_text(), "{truncated")

    def test_output_without_manifest_is_partial(self):
        path = self.root / "desync-reports" / "interrupted-write"
        path.mkdir(parents=True)
        (path / "raw.bin").write_bytes(b"retained")
        result = RUNNER.link_desync_reports(self.root)
        self.assertEqual(result["collectionStatus"], "partial")
        self.assertTrue(result["errors"])

    def summary(self, name="suppression-summary.json", **changes):
        path = self.root / "desync-reports" / ("a" * 32) / name
        path.parent.mkdir(parents=True, exist_ok=True)
        data = {"schemaVersion": 1, "artifactType": "desync-suppression-summary", "collectionStatus": "skipped",
                "sessionId": "a" * 32, "revision": 1,
                "counters": dict(cadence=1, quota=0, active=0, finishing=0, witnessOverflow=0, lostTriggers=0, storageErrors=0)}
        data.update(changes)
        path.write_text(json.dumps(data), encoding="utf-8")
        return path

    def test_suppression_only_is_skipped_evidence_not_complete_report(self):
        path = self.summary()
        before = path.read_bytes()
        result = RUNNER.link_desync_reports(self.root)
        self.assertEqual(result["collectionStatus"], "skipped")
        self.assertEqual(result["reports"], [])
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["suppressionSummaries"][0]["counters"]["cadence"], 1)
        self.assertEqual(path.read_bytes(), before)

    def test_complete_report_plus_suppressed_triggers_remains_visibly_partial(self):
        self.manifest(1, "complete")
        self.summary()
        result = RUNNER.link_desync_reports(self.root)
        self.assertEqual(result["collectionStatus"], "partial")
        self.assertEqual(result["reports"][0]["collectionStatus"], "complete")
        rendered = RUNNER.render_md({"scenario": "synthetic", "attempt": 1, "status": "pass", "started": "test",
                                     "steps": [], "desyncCollection": result})
        self.assertIn("**PARTIAL**", rendered)
        self.assertIn("skipped triggers; no collected peer report", rendered)
        self.assertIn("suppression-summary.json", rendered)

    def test_pending_and_malformed_suppression_evidence_is_preserved(self):
        pending = self.summary("suppression-summary.pending.json")
        malformed = self.summary()
        malformed.write_text("{truncated", encoding="utf-8")
        result = RUNNER.link_desync_reports(self.root)
        self.assertEqual(result["collectionStatus"], "partial")
        self.assertEqual(len(result["suppressionSummaries"]), 2)
        self.assertTrue(all(row.get("error") for row in result["suppressionSummaries"]))
        self.assertTrue(pending.exists())
        self.assertEqual(malformed.read_text(), "{truncated")
        self.summary(collectionStatus="complete")
        self.assertEqual(RUNNER.link_desync_reports(self.root)["collectionStatus"], "partial")
        pending.unlink()
        self.summary()
        self.manifest(1, "complete")
        temporary = malformed.with_name("suppression-summary.tmp")
        temporary.write_text("unfinished", encoding="utf-8")
        self.assertEqual(RUNNER.link_desync_reports(self.root)["collectionStatus"], "partial")
        self.assertTrue(temporary.exists())

    def test_owned_child_options_use_run_paths_and_exact_launch_log(self):
        instance = RUNNER.Instance(0, 123)
        instance.inject_log = (self.root / "registered.log").resolve()
        class Ctx:
            run_dir = self.root
            relay_port = "7782"
            def __init__(self):
                self.processes = []
            def inst(self, index):
                return instance
            def sleep(self, seconds):
                pass
        class Proc:
            pid = 456
            returncode = None
            def poll(self):
                return None
        commands = []
        def start(ctx, name, command, env=None):
            commands.append(command)
            proc = Proc()
            proc.args = command
            ctx.processes.append((name, proc, None))
            return proc
        ctx = Ctx()
        with patch.object(RUNNER, "start_process", side_effect=start), patch.object(RUNNER, "wait_for"):
            RUNNER.step_relay(ctx, {})
            RUNNER.step_runtime(ctx, {"role": "client"})
        self.assertEqual(commands[0][-2:], ["--desync-dir", str((self.root / "desync-reports").resolve())])
        self.assertEqual(commands[1][-4:], ["--desync-dir", str((self.root / "desync-local" / "peer_0").resolve()),
                                           "--inject-log", str(instance.inject_log)])

    def runtime_context(self, processes=()):
        instance = RUNNER.Instance(0, 123)
        return SimpleNamespace(run_dir=self.root, relay_port="7782", processes=list(processes),
                               inst=lambda index: instance)

    def test_runtime_duplicate_registered_name_refused_before_spawn(self):
        # Even an exited same-name process owns its original recovery log.
        proc = SimpleNamespace(args=["runtime", "--pid", "123"], poll=lambda: 0)
        ctx = self.runtime_context([("runtime_0", proc, None)])
        with patch.object(RUNNER, "start_process") as spawn:
            with self.assertRaisesRegex(RUNNER.StepFailed, "already has an owned process/log"):
                RUNNER.step_runtime(ctx, {"role": "player"})
        spawn.assert_not_called()

    def test_runtime_existing_log_preserved_without_spawn_or_truncation(self):
        path = self.root / "runtime_0.log"
        original = b"original identity and recovery evidence\r\n"
        path.write_bytes(original)
        with patch.object(RUNNER, "start_process") as spawn:
            with self.assertRaisesRegex(RUNNER.StepFailed, "already has an owned process/log"):
                RUNNER.step_runtime(self.runtime_context(), {"role": "player"})
        spawn.assert_not_called()
        self.assertEqual(path.read_bytes(), original)

    def test_runtime_same_pid_owned_writer_refused_before_spawn(self):
        proc = SimpleNamespace(args=["runtime", "--pid", "123"], poll=lambda: None)
        ctx = self.runtime_context([("runtime_2", proc, None)])
        with patch.object(RUNNER, "start_process") as spawn:
            with self.assertRaisesRegex(RUNNER.StepFailed, "already has a live runtime writer"):
                RUNNER.step_runtime(ctx, {"role": "player"})
        spawn.assert_not_called()
        self.assertFalse((self.root / "runtime_0.log").exists())

    def test_launch_retains_exact_registered_log(self):
        exact = self.root / "registered" / "inject.log"
        class Ctx:
            instances = []
        ctx = Ctx()
        with patch.object(RUNNER, "kh2ctl", return_value={"processId": 123, "log": str(exact)}):
            RUNNER.step_launch(ctx, {"mute": False})
        self.assertEqual(ctx.instances[0].inject_log, exact.resolve())


if __name__ == "__main__":
    unittest.main()
