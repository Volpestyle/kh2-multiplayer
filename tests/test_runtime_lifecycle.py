"""Injected lifecycle controls only. No native API or game process is executed."""
import importlib.util
from pathlib import Path
import subprocess
import threading
import time
import types
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "runtime_lifecycle", ROOT / "tools/scenario/runtime_lifecycle.py")
CONTROL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CONTROL)


class FakeProcess(subprocess.Popen):
    def __init__(self, pid=101, args=None):
        self.pid = pid
        self.args = args or [str(Path(__file__).resolve()), "--network", "--pid", "201",
                             "--role", "friend1"]
        self._handle = 100000 + pid
        self.returncode = None
        self.terminations = 0
        self.waits = []
        self._child_created = False

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminations += 1
        self.returncode = -1

    def wait(self, timeout=None):
        self.waits.append(timeout)
        if self.returncode is None:
            raise subprocess.TimeoutExpired("fake", timeout)
        return self.returncode


class FakeAPI:
    def __init__(self, proc):
        self.proc = proc
        self.pid = proc.pid
        self.image = proc.args[0]
        self.suspend_status = 0
        self.resume_statuses = [0]
        self.calls = []
        self.resumed = threading.Event()

    def process_id(self, handle):
        self.calls.append(("pid", handle))
        return self.pid

    def executable(self, handle):
        self.calls.append(("image", handle))
        return self.image

    def suspend(self, handle):
        self.calls.append(("suspend", handle))
        if isinstance(self.suspend_status, BaseException):
            raise self.suspend_status
        return self.suspend_status

    def resume(self, handle):
        self.calls.append(("resume", handle))
        value = self.resume_statuses.pop(0) if len(self.resume_statuses) > 1 else self.resume_statuses[0]
        if isinstance(value, BaseException):
            raise value
        if value == 0:
            self.resumed.set()
        return value


class FakeClock:
    def __init__(self):
        self.now = 10.0

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class LifecycleTest(unittest.TestCase):
    def setUp(self):
        self.proc = FakeProcess()
        self.api = FakeAPI(self.proc)
        self.clock = FakeClock()
        self.processes = [("runtime_1", self.proc, object())]
        self.instances = [types.SimpleNamespace(index=0, pid=200),
                          types.SimpleNamespace(index=1, pid=201),
                          types.SimpleNamespace(index=2, pid=202)]
        self.proof = {"retired": True, "identityComplete": True, "slot": 1,
                      "connectionId": 42, "byteOffset": 123,
                      "source": "fresh relay departure joined to survivor rosters"}

    def run_pause(self, **changes):
        args = dict(processes=self.processes, instances=self.instances,
                    runtime=self.proc, game_pid=201, friend_index=1,
                    expected_executable=Path(__file__).resolve(),
                    wait_retired=lambda offset: self.proof, check_all=lambda: None,
                    timeout_seconds=1, relay_offset=123, expected_connection_id=42,
                    _api=self.api, _clock=self.clock, _sleep=self.clock.sleep)
        args.update(changes)
        return CONTROL.pause_owned_friend_runtime(**args)

    def assert_failure(self, **changes):
        with self.assertRaises(CONTROL.RuntimePauseError) as caught:
            self.run_pause(**changes)
        self.assertFalse(caught.exception.receipt["completed"])
        return caught.exception.receipt

    def assert_no_suspend(self):
        self.assertNotIn("suspend", [call[0] for call in self.api.calls])

    def test_success_same_original_handle_balanced(self):
        result = self.run_pause()
        self.assertTrue(result["completed"])
        self.assertTrue(result["resumed"])
        self.assertTrue(result["pinPreserved"])
        self.assertEqual([name for name, _ in self.api.calls], ["pid", "image", "suspend", "resume"])
        self.assertTrue(all(handle is self.proc._handle for _, handle in self.api.calls))
        self.assertEqual(self.proc.terminations, 0)

    def test_not_popen(self):
        self.assert_failure(runtime=types.SimpleNamespace(pid=101))
        self.assert_no_suspend()

    def test_wrong_owner_or_duplicate_registration(self):
        for rows in ([], [("runtime_0", self.proc, None)],
                     self.processes * 2,
                     self.processes + [("runtime_1", FakeProcess(102), None)],
                     self.processes + [("alias", self.proc, None)]):
            with self.subTest(rows=len(rows)):
                self.assert_failure(processes=rows)
                self.assert_no_suspend()

    def test_wrong_instance_and_duplicate_game(self):
        self.assert_failure(friend_index=0)
        self.assert_failure(game_pid=200)
        self.instances.append(types.SimpleNamespace(index=3, pid=201))
        self.assert_failure()
        self.assert_no_suspend()

    def test_exited_runtime(self):
        self.proc.returncode = 0
        self.assert_failure()
        self.assert_no_suspend()

    def test_argument_rejections(self):
        original = self.proc.args[:]
        for suffix in (["--pid", "201"], ["--pid=201"], ["--network"],
                       ["--role", "friend2"], ["--network=true"]):
            with self.subTest(suffix=suffix):
                self.proc.args = original + suffix
                self.assert_failure()
                self.assert_no_suspend()
        self.proc.args = "uninspectable command line"
        self.assert_failure()
        self.assert_no_suspend()

    def test_duplicate_live_writer_and_dead_writer(self):
        other = FakeProcess(102, [self.proc.args[0], "--pid=201"])
        self.processes.append(("runtime_2", other, None))
        self.assert_failure()
        self.assert_no_suspend()
        other.returncode = 0
        self.assertTrue(self.run_pause()["completed"])

    def test_handle_pid_and_image_identity(self):
        self.api.pid = 201
        self.assert_failure()
        self.api.pid = 101
        self.api.image = str(ROOT / "AGENTS.md")
        self.assert_failure()
        self.assert_no_suspend()

    def test_pre_suspend_liveness_failure_does_not_touch_runtime(self):
        def failed():
            raise ValueError("host exited")
        with self.assertRaises(ValueError) as caught:
            self.run_pause(check_all=failed)
        self.assertFalse(caught.exception.receipt["suspendAttempted"])
        self.assert_no_suspend()

    def test_liveness_poll_failure_after_resume_does_not_resume_twice(self):
        original = self.api.resume
        def resume(handle):
            status = original(handle)
            def failed_poll():
                raise OSError("poll failed")
            self.proc.poll = failed_poll
            return status
        self.api.resume = resume
        receipt = self.assert_failure()
        self.assertTrue(receipt["resumed"])
        self.assertFalse(receipt["pinPreserved"])
        self.assertEqual(len(receipt["resumeAttempts"]), 1)

    def test_invalid_timeouts(self):
        for seconds in (0, 0.9, 20.01, float("nan"), float("inf"), True):
            with self.subTest(seconds=seconds):
                self.assert_failure(timeout_seconds=seconds)
                self.assert_no_suspend()

    def test_unsigned_failure_status_is_normalized_and_resumed(self):
        self.api.suspend_status = 0xC0000001
        receipt = self.assert_failure()
        self.assertLess(receipt["suspendStatus"], 0)
        self.assertTrue(receipt["resumed"])
        self.assertEqual(self.proc.terminations, 0)

    def test_suspend_uncertainty_resumes_and_preserves_exception(self):
        error = KeyboardInterrupt("suspend interrupted")
        self.api.suspend_status = error
        with self.assertRaises(KeyboardInterrupt) as caught:
            self.run_pause()
        self.assertIs(caught.exception, error)
        self.assertTrue(error.receipt["resumed"])

    def test_callback_baseexception_resumes(self):
        error = SystemExit("cancel")
        def callback(_):
            raise error
        with self.assertRaises(SystemExit) as caught:
            self.run_pause(wait_retired=callback)
        self.assertIs(caught.exception, error)
        self.assertTrue(error.receipt["resumed"])

    def test_check_failure_after_suspend_resumes(self):
        calls = []
        def check():
            calls.append(1)
            if len(calls) == 2:
                raise ValueError("survivor exited")
        with self.assertRaises(ValueError) as caught:
            self.run_pause(check_all=check)
        self.assertTrue(caught.exception.receipt["resumed"])

    def test_pending_none_or_false_times_out_and_resumes(self):
        for pending in (None, {"retired": False, "problems": ["waiting"]}):
            with self.subTest(pending=pending):
                receipt = self.assert_failure(wait_retired=lambda _: pending)
                self.assertTrue(receipt["deadlineExpired"])
                self.assertTrue(receipt["resumed"])

    def test_callback_deadline_cannot_accept_late_proof(self):
        def slow(_):
            self.clock.now += 2
            return self.proof
        receipt = self.assert_failure(wait_retired=slow)
        self.assertTrue(receipt["resumed"])
        self.assertIsNone(receipt["retirement"])

    def test_invalid_identity_proofs_fail_closed(self):
        for patch in ({"identityComplete": False}, {"slot": 0}, {"connectionId": 43},
                      {"byteOffset": 122}, {"slot": True}, {"retired": 1}):
            with self.subTest(patch=patch):
                proof = dict(self.proof, **patch)
                receipt = self.assert_failure(wait_retired=lambda _: proof)
                self.assertTrue(receipt["resumed"])

    def test_resume_retry_then_success(self):
        self.api.resume_statuses = [-1, OSError("retry"), 0]
        receipt = self.run_pause()
        self.assertTrue(receipt["completed"])
        self.assertEqual(len(receipt["resumeAttempts"]), 3)

    def test_resume_failure_terminates_only_original_owned_runtime(self):
        other = FakeProcess(102, [self.proc.args[0], "--pid", "202"])
        self.processes.append(("runtime_2", other, None))
        self.api.resume_statuses = [-1]
        receipt = self.assert_failure()
        self.assertEqual(receipt["status"], "terminated")
        self.assertTrue(receipt["terminal"])
        self.assertFalse(receipt["pinPreserved"])
        self.assertEqual(len(receipt["resumeAttempts"]), 3)
        self.assertEqual(self.proc.terminations, 1)
        self.assertEqual(self.proc.waits, [1])
        self.assertEqual(other.terminations, 0)

    def test_cleanup_failure_is_explicit_never_success(self):
        self.api.resume_statuses = [-1]
        def denied():
            raise PermissionError("denied")
        self.proc.terminate = denied
        receipt = self.assert_failure()
        self.assertEqual(receipt["status"], "cleanup_unresolved")
        self.assertEqual(len(receipt["cleanup"]), 2)

    def test_watchdog_resumes_during_blocked_callback(self):
        def blocked(_):
            self.assertTrue(self.api.resumed.wait(2.5))
            return self.proof
        before = time.monotonic()
        receipt = self.assert_failure(wait_retired=blocked, _clock=time.monotonic,
                                      _sleep=time.sleep)
        self.assertLess(time.monotonic() - before, 2.5)
        self.assertTrue(receipt["deadlineExpired"])
        self.assertTrue(receipt["resumed"])
        self.assertEqual(len(receipt["resumeAttempts"]), 1)


if __name__ == "__main__":
    unittest.main()
