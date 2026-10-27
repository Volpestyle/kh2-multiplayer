"""VUH-2763: tools/rig/movie_twins.py deletes only .mp4 twins of exited PIDs. No game.

    python -B tests/test_movie_twins.py
"""
import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("movie_twins", ROOT / "tools/rig/movie_twins.py")
TWINS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(TWINS)

DEAD = 0x7FFFFFF0  # never a valid Windows PID (PIDs are multiples of 4)


class MovieTwinsTest(unittest.TestCase):
    def setUp(self):
        self.logs = Path(tempfile.mkdtemp(prefix="kh2_twins_"))
        self.addCleanup(lambda: [p.unlink() for p in self.logs.rglob("*") if p.is_file()])

    def write(self, relative, text):
        path = self.logs / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_deletes_movie_twins_and_keeps_saves(self):
        movie = self.write(f"save_sandbox_{DEAD}/STEAM/juefigs/KH2ReSource/zmovie/en/me4.mp4", "twin")
        save = self.write(f"save_sandbox_{DEAD}/Steam/123/KHIIFM_WW.png", "save")
        code, receipt = TWINS.clean(self.logs, [DEAD])
        self.assertEqual(code, 0)
        self.assertFalse(movie.exists())
        self.assertTrue(save.exists())
        self.assertEqual(receipt["deletedBytes"], 4)
        self.assertEqual([entry["path"] for entry in receipt["deleted"]],
                         [str(movie.relative_to(self.logs))])

    def test_refuses_a_running_pid(self):
        movie = self.write(f"save_sandbox_{os.getpid()}/STEAM/x.mp4", "twin")
        code, receipt = TWINS.clean(self.logs, [os.getpid()])
        self.assertEqual(code, 2)
        self.assertTrue(movie.exists())
        self.assertEqual(receipt["deleted"], [])

    def test_missing_sandbox_is_clean(self):
        self.assertEqual(TWINS.clean(self.logs, [DEAD])[0], 0)


if __name__ == "__main__":
    unittest.main()
