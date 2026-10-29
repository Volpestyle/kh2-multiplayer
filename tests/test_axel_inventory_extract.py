"""Synthetic controls only: no installed game data or PC access."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
from types import SimpleNamespace
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

SPEC = importlib.util.spec_from_file_location(
    "axel_inventory_extract", Path(__file__).parents[1] / "tools/rig/axel_inventory_extract.py")
extract = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(extract)
TABLES = (bytes(range(256)), bytes(range(40)))


def key_source():
    return "\n".join(f"{name} = new byte[] {{" + ",".join(f"0x{x:02x}" for x in table) + "};"
                     for name, table in zip(("MasterKey", "ScrambleKey"), TABLES)).encode()


def original(data, compressed=-2, remastered=0):
    payload = zlib.compress(data) if compressed == 0 else data
    seed = struct.pack("<4i", len(data), remastered, len(payload) if compressed == 0 else compressed, 0)
    if compressed > -2:
        payload = extract.crypt(payload, seed, TABLES)
    record = seed + bytes(remastered * 48) + payload
    return record, (bytes(16), 0, len(record), len(data))


def bar(name=b"test", kind=7, data=b"synthetic ai"):
    return b"BAR\x01" + struct.pack("<I", 1) + bytes(8) + struct.pack("<HH4sII", kind, 0, name, 32, len(data)) + data


def pe():
    data = bytearray(0x3400)
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 0xf0)
    struct.pack_into("<H", data, 0x98, 0x20b)
    base = 0x140000000
    struct.pack_into("<Q", data, 0xb0, base)
    struct.pack_into("<4I", data, 0x190, 0x3000, 0x753000, 0x3000, 0x400)
    struct.pack_into("<Q", data, 0x898, base + 0x754000)  # table1 pointer
    struct.pack_into("<QI", data, 0x1db0, base + 0x755000, 7)  # trap1/155
    return bytes(data)


class ExtractTest(unittest.TestCase):
    def test_original_plain_encrypted_and_compressed(self):
        for mode in (-2, -1, 0):
            for count in (0, 2):
                with self.subTest(mode=mode, remastered=count):
                    expected = b"synthetic original " * 30
                    record, row = original(expected, mode, count)
                    data, metadata = extract.read_original(io.BytesIO(record), row, TABLES)
                    self.assertEqual(data, expected)
                    self.assertEqual(metadata["originalPayloadOffset"], 16 + count * 48)

    def test_package_boundaries_reject(self):
        record, row = original(b"abc")
        cases = [(record, (*row[:2], len(record) + 1, 3)),
                 (record, (*row[:2], 16, 3)),
                 (struct.pack("<4i", 129 * 1024**2, 0, -2, 0), (bytes(16), 0, 16, 0)),
                 (struct.pack("<4i", 3, -1, -2, 0) + b"abc", row)]
        for data, bad_row in cases:
            with self.subTest(data=data[:16]), self.assertRaises(ValueError):
                extract.read_original(io.BytesIO(data), bad_row, TABLES)

    def test_decompression_limits_and_corruption_reject(self):
        record, row = original(b"x" * 1000, 0)
        too_small = struct.pack("<i", 10) + record[4:]
        # Re-encrypt for the changed header, keeping a valid oversized stream.
        payload = extract.crypt(record[16:], record[:16], TABLES)
        too_small = too_small[:16] + extract.crypt(payload, too_small[:16], TABLES)
        with self.assertRaises(ValueError):
            extract.read_original(io.BytesIO(too_small), row, TABLES)
        # Corrupt the deflate block, rather than its unused zlib checksum.
        corrupt = payload[:2] + b"\x07" + payload[3:]
        corrupt = record[:16] + extract.crypt(corrupt, record[:16], TABLES)
        with self.assertRaises((ValueError, zlib.error)):
            extract.read_original(io.BytesIO(corrupt), row, TABLES)

    def test_hed_rows_and_invalid_lengths(self):
        data = struct.pack("<16sQii", hashlib.md5(b"obj/B_EX110.mdlx").digest(), 123, 45, 67)
        self.assertEqual(extract.hed_rows(data)[0][1:], (123, 45, 67))
        for bad in (b"", data[:-1], data + b"x"):
            with self.assertRaises(ValueError):
                extract.hed_rows(bad)

    def test_bar_ranges_and_hashes(self):
        rows = extract.bar_rows(bar())
        self.assertEqual((rows[0]["offset"], rows[0]["type"]), (32, 7))
        self.assertEqual(rows[0]["sha256"], extract.sha(b"synthetic ai"))
        for data in (b"bad", bar()[:-1], b"BAR\x01" + struct.pack("<I", 999) + bytes(8)):
            with self.assertRaises(ValueError):
                extract.bar_rows(data)

    @staticmethod
    def atkp(rows, trailer=b""):
        """OpenKH 7a3b945c Atkp.cs rows: SubId u16, Id u16, then 44 further bytes (48 in all)."""
        return struct.pack("<II", 6, len(rows)) + b"".join(
            struct.pack("<HH", sub, attack) + bytes([attack & 0xFF]) * 44 for sub, attack in rows) + trailer

    def test_selected_atkp_rows(self):
        data = bar(b"atkp", 0, self.atkp([(0, 427), (2, 503), (0, 1)]))
        rows, tables = extract.atkp_rows(data, extract.bar_rows(data))
        self.assertEqual([(r["id"], r["subId"], r["row"]) for r in rows], [(427, 0, 0), (503, 2, 1)])
        self.assertEqual([(r["offset"], r["length"]) for r in rows], [(40, 48), (88, 48)])
        self.assertEqual(tables[0]["trailer"]["length"], 0)

    def test_atkp_selects_by_id_not_sub_id(self):
        """SubId comes first (vuh2914-installed-01): a row whose SubId is 503 is not attack 503."""
        data = bar(b"atkp", 0, self.atkp([(503, 7), (1, 503)]))
        rows, _ = extract.atkp_rows(data, extract.bar_rows(data))
        self.assertEqual([(r["id"], r["subId"], r["row"]) for r in rows], [(503, 1, 1)])

    def test_installed_atkp_layout_with_its_277_byte_trailer(self):
        """The installed shape: version 6, 2713 rows of 48 bytes, then 277 bytes the table doesn't describe.
        main 6b4f7a5 refused it ("unexpected ATKP row layout")."""
        rows = [(0, 1 + i % 400) for i in range(2713)]
        rows[100], rows[2712] = (3, 427), (0, 503)
        trailer = bytes(range(1, 251)) + bytes(27)
        content = self.atkp(rows, trailer)
        self.assertEqual(len(content), 130509)
        data = bar(b"atkp", 0, content)
        selected, tables = extract.atkp_rows(data, extract.bar_rows(data))
        self.assertEqual([(r["id"], r["subId"], r["row"]) for r in selected], [(427, 3, 100), (503, 0, 2712)])
        self.assertEqual(tables, [{"entry": 0, "version": 6, "count": 2713, "rowBytes": 48,
                                   "trailer": {"offset": 32 + 8 + 2713 * 48, "length": 277,
                                               "sha256": hashlib.sha256(trailer).hexdigest()}}])

    def test_atkp_rows_past_the_entry_still_refuse(self):
        data = bar(b"atkp", 0, self.atkp([(0, 427)])[:-1])
        with self.assertRaisesRegex(ValueError, "unexpected ATKP row layout"):
            extract.atkp_rows(data, extract.bar_rows(data))

    def test_native_pe_rva_mapping(self):
        rows = extract.pe_traps(pe(), [[1, 155]])
        self.assertEqual(rows[0]["functionRva"], 0x755000)
        self.assertEqual(rows[0]["functionFileOffset"], 0x2400)
        self.assertEqual(rows[0]["entryFileOffset"], 0x1db0)
        with self.assertRaises(ValueError):
            extract.pe_traps(pe(), [[9, 155]])

    def test_key_source_pin_and_parse(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "reference.cs"
            path.write_bytes(key_source())
            self.assertEqual(extract.load_keys(path, extract.sha(key_source())), list(TABLES))
            with self.assertRaises(ValueError):
                extract.load_keys(path, "0" * 64)

    def test_output_containment_overwrite_and_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            local = repo / ".local"
            local.mkdir()
            expected = local / "new"
            self.assertEqual(extract.output_path(expected, repo), expected.resolve())
            for bad in (repo / "outside", local, local / ".." / "escape"):
                with self.assertRaises(ValueError):
                    extract.output_path(bad, repo)
            expected.mkdir()
            with self.assertRaises(ValueError):
                extract.output_path(expected, repo)
            (local / "escape").symlink_to(repo, target_is_directory=True)
            with self.assertRaises(ValueError):
                extract.output_path(local / "escape" / "new", repo)

    def test_capture_unchanged_inputs_and_complete_manifest(self):
        # Owned ignored directory; no installed files, PC, network or processes.
        local = extract.REPO / ".local"
        local.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=local) as tmp:
            root = Path(tmp)
            install = root / "fake-install"
            install.mkdir()
            source = root / "keys.cs"
            source.write_bytes(key_source())
            (install / "fake.exe").write_bytes(pe())
            assets = {"obj/B_EX110.mdlx": bar(), "00objentry.bin": b"fake objentry"}
            pkg = bytearray()
            hed = bytearray()
            for name, data in assets.items():
                record, row = original(data)
                hed.extend(struct.pack("<16sQii", hashlib.md5(name.encode()).digest(), len(pkg), row[2], row[3]))
                pkg.extend(record)
            (install / "fake.hed").write_bytes(hed)
            (install / "fake.pkg").write_bytes(pkg)
            plan = {"pe": {"path": "fake.exe", "bytes": len(pe()), "sha256": extract.sha(pe())},
                    "openkhEncryptionSha256": extract.sha(key_source()), "traps": [[1, 155]],
                    "hedSha256": {"fake.hed": extract.sha(hed)},
                    "assets": dict.fromkeys(assets), "axelPublicLabels": [12313]}
            before = {p.name: p.read_bytes() for p in install.iterdir()}
            with patch.object(extract.shutil, "disk_usage", return_value=SimpleNamespace(free=100 * 1024**3)):
                result = extract.capture(install, root / "capture", source, plan)
                with self.assertRaises(ValueError):
                    extract.capture(install, root / "capture", source, plan)
            self.assertTrue(result["complete"])
            self.assertEqual(len(result["assets"]), 2)
            self.assertIn("-l", result["openkhCommands"][1])
            self.assertTrue(json.loads((root / "capture/capture.json").read_text())["complete"])
            self.assertEqual(before, {p.name: p.read_bytes() for p in install.iterdir()})
            for name, data in assets.items():
                self.assertEqual((root / "capture/originals" / name).read_bytes(), data)


if __name__ == "__main__":
    unittest.main()
