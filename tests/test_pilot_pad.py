"""Route pilot background input (VUH-2340): offline, against a private mailbox name."""
import ctypes
import importlib.util
import mmap
import os
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("route_pilot", ROOT / "tools/rig/route_pilot/pilot.py")
PILOT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PILOT)


class FakeGame:
    def __init__(self, pid):
        self.pid = pid


def slot0(pid):
    view = mmap.mmap(-1, PILOT.MAILBOX_SIZE, tagname=f"Local\\kh2coop_input_{pid}", access=mmap.ACCESS_READ)
    try:
        header = struct.unpack_from("<III", view, 0)
        seq, lx, ly, rx, ry, buttons, raw = struct.unpack_from("<IffffIH", view, PILOT.SLOT0)
        return header, seq, lx, ly, raw
    finally:
        view.close()


class PadInputTest(unittest.TestCase):
    def setUp(self):
        # A name no game uses: 0x7FFFxxxx is never a live PID here.
        self.pid = 0x7FFF0000 + (os.getpid() & 0xFFFF)
        self.pad = PILOT.PadInput(FakeGame(self.pid), PILOT.KEYS)

    def test_new_mailbox_header_names_this_writer(self):
        header, *_ = slot0(self.pid)
        self.assertEqual(header, (PILOT.MAILBOX_MAGIC, PILOT.MAILBOX_VERSION, os.getpid()))

    def test_actions_become_pad_state_and_release_clears_it(self):
        self.pad.hold({"up", "attack"})
        _, seq, lx, ly, raw = slot0(self.pid)
        self.assertEqual((seq % 2, lx, ly, raw), (0, 0.0, 1.0, 0x4000))
        self.pad.hold({"up", "right"})
        _, _, lx, ly, raw = slot0(self.pid)
        self.assertAlmostEqual(lx, ly)
        self.assertAlmostEqual(lx * lx + ly * ly, 1.0, places=5)
        self.assertEqual(raw, 0)
        self.pad.hold({"DOWN", "react"})  # raw arrow = D-pad, unlike the "down" stick action
        self.assertEqual(slot0(self.pid)[4], 0x0040 | 0x1000)
        self.pad.release()
        self.assertEqual(slot0(self.pid)[2:], (0.0, 0.0, 0))

    def test_unchanged_hold_publishes_no_new_frame(self):
        self.pad.hold({"attack"})
        seq = slot0(self.pid)[1]
        self.pad.hold({"attack"})
        self.assertEqual(slot0(self.pid)[1], seq)

    def test_unmapped_key_is_refused(self):
        with self.assertRaises(ValueError):
            self.pad.hold({"MOUSE_LEFT"})

    def test_never_needs_focus(self):
        self.assertTrue(self.pad.focused())
        self.assertIsNone(self.pad.unfocused_since)

    def test_live_writer_keeps_ownership_of_an_existing_mailbox(self):
        # The second opener finds this process (alive) as the writer and leaves it.
        other = PILOT.PadInput(FakeGame(self.pid), PILOT.KEYS)
        other.hold({"jump"})
        header, *_ = slot0(self.pid)
        self.assertEqual(header[2], os.getpid())
        self.assertEqual(slot0(self.pid)[4], 0x2000)

    def test_dead_writer_is_replaced(self):
        ctypes.memmove(self.pad.view + 8, struct.pack("<I", 0x7FFFFFF0), 4)  # no such process
        PILOT.PadInput(FakeGame(self.pid), PILOT.KEYS)
        self.assertEqual(slot0(self.pid)[0][2], os.getpid())


if __name__ == "__main__":
    unittest.main()
