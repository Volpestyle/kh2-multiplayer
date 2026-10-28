"""VUH-2808: floor crossing, recovery, canonical identity, and scenario verdict controls."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/scenario"))
import thorn_floor


def observation(y, **changes):
    state = dict(world=2, room=34, inField=True, event=False, playerPos=[81, y, 430], pos=[0, 99999, 0])
    state.update(changes)
    return dict(kind="thorn_observe", t=72.3, state=state)


class FloorTest(unittest.TestCase):
    def setUp(self):
        self.box = tempfile.TemporaryDirectory()
        self.addCleanup(self.box.cleanup)
        self.root = Path(self.box.name)

    def pilot(self, name, rows):
        path = self.root / name / "pilot.jsonl"
        path.parent.mkdir(exist_ok=True)
        path.write_text("".join(json.dumps(r) + "\n" for r in rows), encoding="utf-8")
        return path

    def test_boundary_and_downward_sign(self):
        path = self.pilot("host_thorn", [observation(-5000), observation(0), observation(1000)])
        result = thorn_floor.evaluate([path])
        self.assertEqual(result["status"], "pass")
        self.assertEqual(result["seats"][0]["worst"]["belowFloor"], 1000)

    def test_either_seat_and_recovery_does_not_erase_fall(self):
        for fallen in ("host_thorn", "friend_thorn"):
            paths = [self.pilot(name, [observation(0), observation(1709 if name == fallen else 0), observation(0)])
                     for name in ("host_thorn", "friend_thorn")]
            result = thorn_floor.evaluate(paths)
            self.assertEqual(result["status"], "fail")
            fall = next(s for s in result["seats"] if s["firstFall"])
            self.assertEqual((fall["pilot"], fall["firstFall"]["line"]), (fallen, 2))

    def test_camera_and_other_rooms_and_cinematics_are_excluded(self):
        path = self.pilot("host_thorn", [observation(0), observation(5000, room=3),
                                         observation(5000, world=4), observation(5000, event=True),
                                         observation(5000, inField=False)])
        result = thorn_floor.evaluate([path])
        self.assertEqual((result["status"], result["samples"]), ("pass", 1))

    def test_missing_or_invalid_position_is_unavailable(self):
        for position in (None, [], [0, float("nan"), 0], [0, float("inf"), 0], [0, "1001", 0]):
            path = self.pilot("friend_thorn", [observation(0, playerPos=position)])
            self.assertEqual(thorn_floor.evaluate([path])["status"], "unavailable")
        empty = self.pilot("friend_thorn", [])
        good = self.pilot("host_thorn", [observation(0)])
        self.assertEqual(thorn_floor.evaluate([good, empty])["status"], "unavailable")
        self.assertEqual(thorn_floor.evaluate([])["status"], "unavailable")
        self.assertEqual(thorn_floor.evaluate([self.root / "missing/pilot.jsonl"])["status"], "unavailable")

    def test_truncated_json_is_not_a_pass(self):
        path = self.pilot("friend_thorn", [observation(0)])
        with path.open("a", encoding="utf-8") as stream:
            stream.write('{"state":')
        result = thorn_floor.evaluate([path])
        self.assertEqual((result["status"], result["seats"][0]["unreadable"]), ("unavailable", 1))

    def test_automatic_report_fails_and_keeps_other_failure(self):
        self.pilot("friend_thorn", [observation(1000.01)])
        for status in ("pass", "fail", "crash", "hang"):
            report = dict(scenario="net_twilight_thorn_bots05", status=status)
            if status != "pass":
                report["error"] = "original failure"
            artifacts = []
            thorn_floor.check_run(self.root, report, artifacts)
            self.assertEqual(report["status"], "fail" if status == "pass" else status)
            self.assertEqual(report["thornFloor"]["status"], "fail")
            self.assertIn("thorn_floor.json", artifacts)
            self.assertEqual(json.loads((self.root / artifacts[0]).read_text())["status"], "fail")
            if status != "pass":
                self.assertEqual(report["error"], "original failure")

    def test_unrelated_pilot_does_not_hide_good_thorn_samples(self):
        self.pilot("host_calling", [observation(5000, room=3)])
        self.pilot("host_thorn", [observation(0)])
        report = dict(scenario="net_twilight_thorn_bots05", status="pass")
        thorn_floor.check_run(self.root, report, [])
        self.assertEqual(report["thornFloor"]["status"], "pass")
        self.assertEqual(len(report["thornFloor"]["seats"]), 1)


if __name__ == "__main__":
    unittest.main()
