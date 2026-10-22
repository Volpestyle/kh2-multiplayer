"""VUH-2617 desk stop: tools/rig/desk-scenario.ps1 refuses every run while
build/rig/vuh2617-fileio-desk-stop.json exists in the main checkout (or the
running checkout), whatever it says. Runs a copy of the script in throwaway
checkouts; nothing here can start a game.

    python -B -m unittest discover -s tests -p test_desk_stop.py -v
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "rig" / "desk-scenario.ps1"
STOP = Path("build") / "rig" / "vuh2617-fileio-desk-stop.json"


def git(cwd, *args):
    subprocess.run(["git", "-C", str(cwd), *args], check=True, capture_output=True)


def stage(checkout):
    """Copy the script into a checkout with one scenario file; return the script path."""
    rig = checkout / "tools" / "rig"
    rig.mkdir(parents=True, exist_ok=True)
    shutil.copy2(SCRIPT, rig / SCRIPT.name)
    (checkout / "scenario.json").write_text('{"steps": []}', encoding="utf-8")
    return rig / SCRIPT.name


def run(script, scenario="scenario.json"):
    out = subprocess.run(["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass",
                          "-File", str(script), "-Scenario", scenario, "-Tag", "stop-test"],
                         capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
    return out.stdout + out.stderr


def stop_at(checkout, text='{"pid": 4242}'):
    path = checkout / STOP
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


@unittest.skipUnless(os.name == "nt" and shutil.which("powershell") and shutil.which("git"), "Windows PowerShell and git")
class DeskStopTest(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="desk-stop-"))
        self.main = self.tmp / "main"
        self.main.mkdir()
        git(self.main, "init", "-q")
        git(self.main, "-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "--allow-empty", "-m", "base")
        self.worktree = self.main / ".local" / "wt"
        git(self.main, "worktree", "add", "-q", "--detach", str(self.worktree))
        self.script = stage(self.worktree)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def assertRefused(self, output, stop, root):
        self.assertIn("desk stopped:", output)
        self.assertIn(Path(stop).name, output)
        # Refused before anything else: no operator log, no runner output.
        self.assertFalse((root / "build" / "rig" / "operator-logs").exists(), output)

    def test_main_checkout_stop_refuses_a_worktree_run(self):
        stop = stop_at(self.main)
        self.assertRefused(run(self.script), stop, self.worktree)

    def test_main_checkout_stop_refuses_a_worktree_outside_local(self):
        elsewhere = self.tmp / "elsewhere"
        git(self.main, "worktree", "add", "-q", "--detach", str(elsewhere))
        script = stage(elsewhere)
        stop = stop_at(self.main)
        self.assertRefused(run(script), stop, elsewhere)

    def test_own_checkout_stop_refuses(self):
        stop = stop_at(self.worktree)
        self.assertRefused(run(self.script), stop, self.worktree)

    def test_any_content_refuses(self):
        for text in ("", "not json", '{"pid": 0}'):
            with self.subTest(text=text):
                stop = stop_at(self.main, text)
                self.assertRefused(run(self.script), stop, self.worktree)

    def test_refused_even_for_an_unknown_scenario(self):
        stop_at(self.main)
        self.assertIn("desk stopped:", run(self.script, "missing.json"))

    def test_without_git_a_local_worktree_still_finds_the_main_stop(self):
        plain = self.tmp / "plain"
        script = stage(plain / ".local" / "wt")
        stop = stop_at(plain)
        self.assertRefused(run(script), stop, plain / ".local" / "wt")

    def test_no_stop_file_passes_the_check(self):
        # An unknown scenario stops at the next check, so no run can start.
        output = run(self.script, "missing.json")
        self.assertNotIn("desk stopped:", output)
        self.assertIn("no scenario 'missing.json'", output)


if __name__ == "__main__":
    unittest.main()
