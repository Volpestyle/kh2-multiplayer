"""VUH-2866: the scenario step downed_kill against a stand-in DLL side of the DownedSpike
fixture channel (Local\\kh2coop_downed_<pid>, inject/src/DownedSpikeState.hpp v7). No game.

    python -B -m unittest discover -s tests -p test_downed_kill_step.py -v
"""
import importlib.util
import mmap
import os
import struct
import threading
import time
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("downed_kill_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)


class FakeDll:
    """Creates the mapping as the DLL does and answers one request with `result`."""

    def __init__(self, pid, result=1, answer=True, magic=0x4E574448, version=7):
        self.map = mmap.mmap(-1, 360, tagname=f"Local\\kh2coop_downed_{pid}")
        struct.pack_into("<IIii", self.map, 0, magic, version, 0, 0)
        self.result, self.answer, self.seen = result, answer, []
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.serve, daemon=True)
        self.thread.start()

    def serve(self):
        while not self.stop.is_set():
            request, done, command = struct.unpack_from("<iiI", self.map, 8)
            if request != done and self.answer:
                self.seen.append(command)
                struct.pack_into("<i", self.map, 20, self.result)
                struct.pack_into("<i", self.map, 12, request)
            time.sleep(0.005)

    def close(self):
        self.stop.set()
        self.thread.join()
        self.map.close()


def context(pid):
    inst = SimpleNamespace(index=1, pid=pid)
    return SimpleNamespace(inst=lambda index: inst, check_all=lambda: None)


class DownedKillStepTest(unittest.TestCase):
    pid = 0x7FFF0000 + os.getpid() % 0xFFFF  # never a real KH2 pid in this test process

    def fake(self, **kw):
        dll = FakeDll(self.pid, **kw)
        self.addCleanup(dll.close)
        return dll

    def test_kill_sends_command_one_and_reports_ok(self):
        dll = self.fake()
        out = RUN.step_downed_kill(context(self.pid), {"instance": 1})
        self.assertEqual(out["result"], "Ok")
        self.assertEqual(out["seq"], 1)
        self.assertEqual(dll.seen, [1])

    def test_refusal_fails_unless_not_required(self):
        self.fake(result=6)  # WrongState: already downed
        with self.assertRaises(RUN.StepFailed):
            RUN.step_downed_kill(context(self.pid), {"instance": 1})
        out = RUN.step_downed_kill(context(self.pid), {"instance": 1, "require": False})
        self.assertEqual(out["result"], "WrongState")

    def test_missing_channel_is_never_created(self):
        with self.assertRaises(RUN.StepFailed) as raised:
            RUN.step_downed_kill(context(self.pid + 1), {"instance": 1})
        self.assertIn("KH2COOP_DOWNED_SPIKE_FIXTURE=1", str(raised.exception))

    def test_wrong_layout_is_refused(self):
        self.fake(version=6)
        with self.assertRaises(RUN.StepFailed):
            RUN.step_downed_kill(context(self.pid), {"instance": 1})

    def test_unanswered_request_times_out(self):
        self.fake(answer=False)
        with self.assertRaises(RUN.StepFailed):
            RUN.step_downed_kill(context(self.pid), {"instance": 1, "timeoutMs": 200})


if __name__ == "__main__":
    unittest.main()
