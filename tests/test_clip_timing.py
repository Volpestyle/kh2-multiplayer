"""QPC frame evidence: no game, rendering, encoding, or input."""
import importlib.util
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TIMING = load("clip_timing", "tools/scenario/clip_timing.py")
RUN = load("clip_timing_runner", "tools/scenario/run.py")


class TimingTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)

    def capture(self, seat, offset=0, spacing=33, **changes):
        path = self.path / f"seat{seat}.mp4.receipt.json"
        rows = [dict(schema=1, frame=i, qpcBegin=1000+offset+i*spacing,
                     qpcEnd=1002+offset+i*spacing, present=100+i*2, qpcFrequency=1000)
                for i in range(10)]
        timing = path.with_name(f"seat{seat}.mp4.capture-time.jsonl")
        timing.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
        receipt = dict(ok=True, timingMethod="native-Present-QPC-bounds-v1", processId=100+seat,
                       frames=10, gameFpsBefore=60, gameFpsDuringCapture=55, captureSeconds=0.33,
                       clipFps=30, clockDomain="same-pc", captureTiming=str(timing))
        receipt.update(changes)
        path.write_text(json.dumps(receipt), encoding="utf-8")
        return path, timing, rows

    def test_different_encoded_rates_do_not_change_wall_time_bounds(self):
        h, _, _ = self.capture(0, clipFps=27)
        f, _, _ = self.capture(1, offset=200, clipFps=31)
        result = TIMING.compare(TIMING.onset(h, 4), TIMING.onset(f, 4), same_pc=True)
        self.assertEqual(result["timing"], "PASS")
        self.assertEqual(result["signedSkewBoundsMs"], [165, 235])

    def test_real_delay_fails_despite_same_frame_number(self):
        h, _, _ = self.capture(0)
        f, _, _ = self.capture(1, offset=1500)
        self.assertEqual(TIMING.compare(TIMING.onset(h, 4), TIMING.onset(f, 4), same_pc=True)["timing"], "FAIL")

    def test_wide_capture_gap_cannot_pass_on_its_average(self):
        h, _, _ = self.capture(0, spacing=1000)
        f, _, _ = self.capture(1, spacing=1000)
        self.assertEqual(TIMING.compare(TIMING.onset(h, 4), TIMING.onset(f, 4), same_pc=True)["timing"], "UNQUALIFIED")

    def test_incomplete_duplicate_reversed_or_mixed_clock_receipts_refuse(self):
        for kind in ("missing", "duplicate", "reversed", "clock"):
            with self.subTest(kind=kind):
                path, timing, rows = self.capture(0)
                if kind == "missing": rows.pop()
                if kind == "duplicate": rows[5] = rows[4]
                if kind == "reversed": rows[5]["qpcEnd"] = rows[5]["qpcBegin"] - 1
                if kind == "clock": rows[5]["qpcFrequency"] = 2000
                timing.write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
                with self.assertRaises(ValueError): TIMING.onset(path, 4)

    def test_first_capture_frame_has_no_onset_lower_bound(self):
        h, _, _ = self.capture(0)
        with self.assertRaises(ValueError): TIMING.onset(h, 0)

    def test_exported_windows_receipt_resolves_adjacent_sidecar(self):
        h, timing, _ = self.capture(0)
        receipt = json.loads(h.read_text())
        receipt["captureTiming"] = "C:\\retained-run\\" + timing.name
        h.write_text(json.dumps(receipt), encoding="utf-8")
        self.assertEqual(TIMING.onset(h, 4)["firstVisibleFrame"], 4)

    def test_cross_pc_and_same_seat_are_rejected(self):
        h, _, _ = self.capture(0)
        f, _, _ = self.capture(1, clockDomain="another-pc")
        with self.assertRaises(ValueError):
            TIMING.compare(TIMING.onset(h, 4), TIMING.onset(f, 4), same_pc=True)
        with self.assertRaises(ValueError):
            TIMING.compare(TIMING.onset(h, 4), TIMING.onset(h, 4), same_pc=True)

    def test_scenario_keeps_full_receipt_and_sidecars(self):
        path, timing, _ = self.capture(0)
        out = self.path / "seat0.mp4"
        result = json.loads(path.read_text())
        ctx = SimpleNamespace(artifacts=[])
        receipt = RUN.clip_receipt(ctx, out, result)
        self.assertEqual(receipt["gameFpsDuringCapture"], 55)
        self.assertEqual(receipt["captureTiming"], str(timing))
        self.assertEqual(set(ctx.artifacts), {timing.name, path.name})


if __name__ == "__main__":
    unittest.main()
