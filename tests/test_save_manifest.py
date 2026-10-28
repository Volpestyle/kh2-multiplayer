"""VUH-2808: real file/JSON controls for the protected-save closure."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location(
    "save_manifest", Path(__file__).resolve().parents[1] / "tools/rig/save_manifest.py")
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class SaveManifestTest(unittest.TestCase):
    def fixture(self, folder, count):
        root, copies = folder / "saves", folder / "copies"
        root.mkdir(); copies.mkdir()
        rows = []
        for i in range(count):
            data = f"protected save {i}\n".encode()
            path = root / f"save{i}.dat"
            path.write_bytes(data)
            (copies / path.name).write_bytes(data)
            rows.append(dict(path=str(path), sha256=hashlib.sha256(data).hexdigest()))
        manifest = folder / "before.json"
        manifest.write_text(json.dumps(rows), encoding="utf-8-sig")
        return root, copies, manifest

    def invoke(self, manifest, root, copies):
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = checker.main(["--baseline", str(manifest), "--save-root", str(root),
                                 "--copies", str(copies)])
        return code, json.loads(output.getvalue())

    def test_identical_one_and_four_pass_with_exact_count(self):
        for count in (1, 4):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as temp:
                root, copies, manifest = self.fixture(Path(temp), count)
                code, result = self.invoke(manifest, root, copies)
                self.assertEqual(code, 0)
                self.assertTrue(result["pass"])
                self.assertEqual(result["saveCount"], count)
                self.assertTrue(all(r["byteIdentical"] for r in result["saveRows"]))

    def test_one_modified_save_fails_for_one_and_four(self):
        for count in (1, 4):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as temp:
                root, copies, manifest = self.fixture(Path(temp), count)
                path = root / "save0.dat"
                path.write_bytes(path.read_bytes().replace(b"protected", b"modified!"))
                code, result = self.invoke(manifest, root, copies)
                self.assertEqual(code, 1)
                self.assertFalse(result["pass"])
                self.assertEqual(len(result["changed"]), 1)
                self.assertFalse(result["changed"][0]["byteIdentical"])

    def test_modified_copy_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            root, copies, manifest = self.fixture(Path(temp), 4)
            (copies / "save0.dat").write_bytes(b"changed retained copy")
            self.assertEqual(self.invoke(manifest, root, copies)[0], 1)

    def test_empty_baseline_fails_even_with_empty_save_set(self):
        with tempfile.TemporaryDirectory() as temp:
            root, copies, manifest = self.fixture(Path(temp), 0)
            code, result = self.invoke(manifest, root, copies)
            self.assertEqual(code, 1)
            self.assertFalse(result["pass"])

    def test_wrapped_array_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            root, copies, manifest = self.fixture(Path(temp), 4)
            rows = json.loads(manifest.read_text(encoding="utf-8-sig"))
            for bad in ({"value": rows, "Count": 4}, [rows]):
                with self.subTest(shape=type(bad).__name__):
                    manifest.write_text(json.dumps(bad))
                    self.assertEqual(self.invoke(manifest, root, copies)[0], 1)

    def test_missing_or_extra_save_fails(self):
        for missing in (True, False):
            with self.subTest(missing=missing), tempfile.TemporaryDirectory() as temp:
                root, copies, manifest = self.fixture(Path(temp), 4)
                if missing:
                    (root / "save0.dat").unlink()
                else:
                    (root / "extra.dat").write_bytes(b"extra save")
                self.assertEqual(self.invoke(manifest, root, copies)[0], 1)


if __name__ == "__main__":
    unittest.main()
