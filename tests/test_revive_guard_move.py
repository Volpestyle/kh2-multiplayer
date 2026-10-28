"""VUH-2912 (run vuh2912-hp-02): ReviveGuard's per-tick revive move. While a native RC owns Triangle (Axel's Burst
Frontier offer), the reviver steps sideways out of the line through the nearest enemy instead of waiting in it.
No game.

    python -B -m unittest discover -s tests -p test_revive_guard_move.py -v
"""
import importlib.util
import math
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("revive_guard_runner", ROOT / "tools/scenario/run.py")
RUN = importlib.util.module_from_spec(spec)
spec.loader.exec_module(RUN)
move = RUN.ReviveGuard.revive_move
REACH = RUN.ReviveGuard.REACH
AXEL = {"x": 0.0, "z": 0.0}
BURST_FRONTIER = 444


class ReviveMoveTest(unittest.TestCase):
    def test_walks_to_the_downed_seat_until_in_reach(self):
        kind, dx, dz = move((0, 0, 500), (0, 0, 800), 0, [AXEL], REACH)
        self.assertEqual(kind, "walk")
        self.assertEqual((dx, dz), (0, 300))

    def test_holds_to_revive_in_reach_with_triangle_free(self):
        self.assertEqual(move((0, 0, 500), (0, 0, 560), 0, [AXEL], REACH), ("hold",))

    def test_a_native_rc_in_reach_steps_aside_off_the_line_through_axel(self):
        # The reviver at 500 in front of Axel (on Burst Frontier's line); the downed seat beside it.
        kind, dx, dz = move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, [AXEL], REACH)
        self.assertEqual(kind, "aside")
        radial = (0.0, 500.0)
        self.assertAlmostEqual(dx * radial[0] + dz * radial[1], 0.0)  # perpendicular to Axel -> reviver
        self.assertGreater(math.hypot(dx, dz), 0)

    def test_aside_uses_the_nearest_living_enemy(self):
        far = {"x": 0.0, "z": 5000.0}
        kind, dx, dz = move((300, 0, 0), (300, 0, 50), BURST_FRONTIER, [far, AXEL], REACH)
        self.assertEqual(kind, "aside")
        self.assertAlmostEqual(dx * 300 + dz * 0, 0.0)  # perpendicular to the nearer Axel, not the far enemy

    def test_a_native_rc_with_no_enemy_listed_waits(self):
        self.assertEqual(move((0, 0, 500), (0, 0, 560), BURST_FRONTIER, [], REACH), ("wait",))

    def test_a_native_rc_out_of_reach_still_walks_in(self):
        self.assertEqual(move((0, 0, 0), (0, 0, 400), BURST_FRONTIER, [AXEL], REACH)[0], "walk")


if __name__ == "__main__":
    unittest.main()
