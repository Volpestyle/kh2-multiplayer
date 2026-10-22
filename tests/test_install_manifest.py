"""Offline install detector regression; only newly owned .local scratch files."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    "install_manifest", Path(__file__).resolve().parents[1] / "tools/rig/install_manifest.py")
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


class InstallManifestTest(unittest.TestCase):
    def setUp(self):
        manifest.LOCAL.mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="install-manifest-test-", dir=manifest.LOCAL)
        self.base = Path(self.temp.name)
        self.install = self.base / "install"
        self.install.mkdir()
        (self.install / "one.bin").write_bytes(b"original")
        (self.install / "nested").mkdir()
        (self.install / "nested/two.bin").write_bytes(b"two")

    def tearDown(self):
        # Recursive cleanup is confined to this test's freshly owned scratch tree.
        assert self.base.resolve().is_relative_to(manifest.LOCAL.resolve())
        self.temp.cleanup()

    def cli(self, *args):
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            return manifest.main(list(map(str, args)))

    def test_all_files_and_content_match_despite_mtime_change(self):
        before = manifest.capture(self.install)
        os.utime(self.install / "one.bin", (100, 100))
        after = manifest.capture(self.install)
        self.assertEqual([r["path"] for r in before["files"]], ["nested/two.bin", "one.bin"])
        self.assertTrue(manifest.compare(before, after)["matched"])

    def test_same_size_change_addition_and_removal_refuse_continue(self):
        before, after, diff = (self.base / n for n in ("before.json", "after.json", "diff.json"))
        self.assertEqual(self.cli("capture", "--root", self.install, "--out", before), 0)
        (self.install / "one.bin").write_bytes(b"mutated!")
        (self.install / "nested/two.bin").unlink()
        (self.install / "added.bin").write_bytes(b"added")
        self.assertEqual(self.cli("capture", "--root", self.install, "--out", after), 0)
        self.assertEqual(self.cli("diff", "--before", before, "--after", after, "--out", diff), 1)
        result = json.loads(diff.read_text())
        self.assertEqual(result["changed"], ["one.bin"])
        self.assertEqual(result["added"], ["added.bin"])
        self.assertEqual(result["removed"], ["nested/two.bin"])

    def test_output_inside_install_is_never_created(self):
        target = self.install / "must-not-create/subdir/out.json"
        self.assertEqual(self.cli("capture", "--root", self.install, "--out", target), 2)
        self.assertFalse(target.parent.exists())

    def test_before_receipt_cannot_be_overwritten(self):
        target = self.base / "before.json"
        self.assertEqual(self.cli("capture", "--root", self.install, "--out", target), 0)
        original = target.read_bytes()
        (self.install / "one.bin").write_bytes(b"changed")
        self.assertEqual(self.cli("capture", "--root", self.install, "--out", target), 2)
        self.assertEqual(target.read_bytes(), original)

    def test_wrong_root_and_malformed_manifest_refuse(self):
        before, after, diff = (self.base / n for n in ("before.json", "after.json", "diff.json"))
        self.cli("capture", "--root", self.install, "--out", before)
        value = json.loads(before.read_text())
        value["root"] = str(self.base)
        after.write_text(json.dumps(value))
        self.assertEqual(self.cli("diff", "--before", before, "--after", after, "--out", diff), 2)
        self.assertFalse(diff.exists())
        value = json.loads(before.read_text())
        value["files"].append(dict(value["files"][0]))
        value["fileCount"] += 1
        after.write_text(json.dumps(value))
        self.assertEqual(self.cli("diff", "--before", before, "--after", after, "--out", diff), 2)
        self.assertFalse(diff.exists())


if __name__ == "__main__":
    unittest.main()
