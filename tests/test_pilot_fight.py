"""Route pilot fight rules (VUH-2340): Potion timing and the pad guard, offline."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("route_pilot_fight", ROOT / "tools/rig/route_pilot/pilot.py")
PILOT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PILOT)

HEAL = {"below": 0.5, "max": 3}


def due(hp, used=0, missed=0, ready_at=0.0, now=10.0, drop=0, heal=HEAL, max_hp=22):
    return PILOT.heal_due(hp, max_hp, heal, used, missed, ready_at, now, drop)


class HealDueTest(unittest.TestCase):
    def test_threshold(self):
        self.assertTrue(due(10))
        self.assertFalse(due(11))  # 11 of 22 is not below half
        self.assertFalse(due(0))  # dead: Game Over handles it
        self.assertFalse(due(10, heal=None))

    def test_damage_in_progress_heals_earlier(self):
        # 14 HP with 8 lost in the last 2 s: half the combo is anticipated (14 - 4 < 11).
        self.assertTrue(due(14, drop=8))
        self.assertFalse(due(14, drop=4))
        self.assertTrue(due(14, drop=4, heal={**HEAL, "lead": 1.0}))

    def test_only_used_potions_count_toward_max(self):
        self.assertTrue(due(10, used=2, missed=3))
        self.assertFalse(due(10, used=3))
        self.assertFalse(due(10, missed=4))  # menu misses stop at their own cap
        self.assertTrue(due(10, missed=4, heal={**HEAL, "missMax": 6}))

    def test_cooldown(self):
        self.assertFalse(due(10, ready_at=11.0, now=10.0))
        self.assertTrue(due(10, ready_at=10.0, now=10.0))


class GuardMappingTest(unittest.TestCase):
    def test_guard_is_square_on_the_pad(self):
        self.assertEqual(PILOT.KEYS["guard"], "SQUARE")
        self.assertEqual(PILOT.PAD["SQUARE"], 0x8000)

    def test_keyboard_has_no_square(self):
        with self.assertRaises(ValueError):
            PILOT.Keyboard.code("SQUARE")


if __name__ == "__main__":
    unittest.main()
