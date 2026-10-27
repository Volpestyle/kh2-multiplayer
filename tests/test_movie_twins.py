"""VUH-2775: tools/rig/movie_twins.py deletes only install-asset twins of exited PIDs, and fails closed. No game.

    python -B tests/test_movie_twins.py
"""
import _winapi
import importlib.util
import os
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("movie_twins", ROOT / "tools/rig/movie_twins.py")
TWINS = importlib.util.module_from_spec(spec)
spec.loader.exec_module(TWINS)

DEAD = 0x7FFFFFF0  # never a valid Windows PID (PIDs are multiples of 4)
MOVIE = "install-assets/zmovie/en/me4.mp4"


class MovieTwinsTest(unittest.TestCase):
    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="kh2_twins_"))
        self.addCleanup(self.remove, self.root)
        self.logs = self.root / "logs"
        self.logs.mkdir()

    def remove(self, root):
        # Unlink junctions first so rmtree never walks into their targets.
        for directory, dirs, _ in os.walk(root):
            for name in dirs:
                path = Path(directory) / name
                if TWINS.linked(path):
                    os.rmdir(path)
        shutil.rmtree(root, ignore_errors=True)

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def test_deletes_asset_twins_and_keeps_saves(self):
        sandbox = self.logs / f"save_sandbox_{DEAD}"
        movie = self.write(sandbox / MOVIE, "twin")
        save = self.write(sandbox / "Steam/123/KHIIFM_WW.png", "save")
        code, receipt = TWINS.clean(self.logs, [DEAD])
        self.assertEqual(code, 0)
        self.assertFalse(movie.exists())
        self.assertTrue(save.exists())
        self.assertEqual(receipt["deletedBytes"], 4)
        self.assertEqual([entry["path"] for entry in receipt["deleted"]], [str(movie.relative_to(self.logs))])

    def test_refuses_a_running_pid(self):
        movie = self.write(self.logs / f"save_sandbox_{os.getpid()}" / MOVIE, "twin")
        code, receipt = TWINS.clean(self.logs, [os.getpid()])
        self.assertEqual(code, 2)
        self.assertTrue(movie.exists())

    def test_unreadable_process_counts_as_running(self):
        kernel = mock.Mock()
        kernel.OpenProcess.return_value = None
        with mock.patch.object(TWINS, "kernel32", return_value=kernel), \
                mock.patch.object(TWINS.ctypes, "get_last_error", return_value=5):  # access denied
            self.assertTrue(TWINS.running(DEAD))
        with mock.patch.object(TWINS, "kernel32", return_value=kernel), \
                mock.patch.object(TWINS.ctypes, "get_last_error", return_value=TWINS.ERROR_INVALID_PARAMETER):
            self.assertFalse(TWINS.running(DEAD))

    def test_refuses_junctioned_sandbox_paths(self):
        outside = self.write(self.root / "outside/zmovie/en/me4.mp4", "not a twin")
        # The sandbox, its install-assets root, and a directory inside the twin tree, each a junction outside.
        for n, tail in enumerate(("", "/install-assets", "/install-assets/zmovie")):
            pid = DEAD + 4 * n
            link = f"save_sandbox_{pid}{tail}"
            with self.subTest(link):
                path = self.logs / link
                path.parent.mkdir(parents=True, exist_ok=True)
                target = self.root / "outside/zmovie" if link.endswith("zmovie") else self.root / "outside"
                _winapi.CreateJunction(str(target), str(path))
                code, receipt = TWINS.clean(self.logs, [pid])
                self.assertEqual(code, 2)
                self.assertTrue(outside.exists())
                self.assertEqual(receipt["deleted"], [])

    def test_refuses_a_symlinked_sandbox_root(self):
        # Rhea's repro (ddc42716): a symlinked sandbox root once deleted an outside .mp4 and exited 0.
        outside = self.write(self.root / "outside/install-assets/zmovie/en/me4.mp4", "not a twin")
        try:
            os.symlink(self.root / "outside", self.logs / f"save_sandbox_{DEAD}", target_is_directory=True)
        except OSError:
            self.skipTest("this account can't create symlinks (the junction cases still run)")
        self.addCleanup(os.rmdir, self.logs / f"save_sandbox_{DEAD}")
        code, receipt = TWINS.clean(self.logs, [DEAD])
        self.assertEqual(code, 2)
        self.assertTrue(outside.exists())
        self.assertEqual(receipt["deleted"], [])

    def test_refuses_a_linked_ancestor_of_the_logs_root(self):
        # Rhea's repro (eb389de1): linked-parent -> outside, clean(linked-parent/logs) deleted outside twins.
        outside = self.write(self.root / f"outside/logs/save_sandbox_{DEAD}" / MOVIE, "not ours")
        for kind in ("junction", "symlink"):
            parent = self.root / f"linked-parent-{kind}"
            with self.subTest(kind):
                if kind == "junction":
                    _winapi.CreateJunction(str(self.root / "outside"), str(parent))
                else:
                    try:
                        os.symlink(self.root / "outside", parent, target_is_directory=True)
                    except OSError:
                        self.skipTest("this account can't create symlinks")
                self.addCleanup(os.rmdir, parent)
                code, receipt = TWINS.clean(parent / "logs", [DEAD])
                self.assertEqual(code, 2)
                self.assertTrue(outside.exists())
                self.assertEqual(receipt["deleted"], [])

    def test_refuses_a_logs_root_outside_the_allowed_roots(self):
        with mock.patch.object(TWINS, "allowed_logs", return_value=False):
            self.assertEqual(TWINS.clean(self.logs, [DEAD])[0], 2)
        logs_link = self.root / "logs-link"
        _winapi.CreateJunction(str(self.logs), str(logs_link))
        self.assertEqual(TWINS.clean(logs_link, [DEAD])[0], 2)

    def test_missing_sandbox_is_clean(self):
        self.assertEqual(TWINS.clean(self.logs, [DEAD])[0], 0)


if __name__ == "__main__":
    unittest.main()
