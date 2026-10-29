#!/usr/bin/env python3
"""VUH-2914 selective, read-only installed-byte capture. See docs/AXEL_HAZARDS.md.

No network, processes, saves, package writes or remastered-asset extraction.
Default is a plan; capture requires explicit paths and separate PC authorization.
Package layout/encryption follows OpenKH at the commit pinned in the input plan.
The key table is read from a hash-checked local OpenKH source, never vendored.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import zlib

REPO = Path(__file__).resolve().parents[2]
PLAN = Path(__file__).with_name("axel_inventory_plan.json")
MAX_ASSET = 128 * 1024 * 1024
MAX_TOTAL = 512 * 1024 * 1024
ATTACK_IDS = {427, 429, 432, 434, 436, 503}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def span(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"out-of-range slice {offset:#x}+{size:#x}/{len(data):#x}")
    return data[offset:offset + size]


def load_keys(path, expected):
    source = path.read_bytes()
    if sha(source) != expected:
        raise ValueError("OpenKH encryption reference hash mismatch")
    tables = []
    for name, size in (("MasterKey", 256), ("ScrambleKey", 40)):
        match = re.search(r"\b" + name + r"\s*=\s*new byte\[\]\s*\{([^}]+)\}", source.decode())
        if not match:
            raise ValueError(f"missing OpenKH {name}")
        table = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", match[1]))
        if len(table) != size:
            raise ValueError(f"invalid OpenKH {name} length")
        tables.append(table)
    return tables


def crypt(data, seed, tables):
    master, scramble = tables
    key = bytearray(176)
    key[:16] = bytes(value if value else i for i, value in enumerate(seed))
    for i in range(40):
        frame = key[12 + 4*i:16 + 4*i]
        if i % 4 == 0:
            frame = bytes(master[frame[(j + 1) % 4]] ^ scramble[i + j] for j in range(4))
        for j in range(4):
            key[16 + 4*i + j] = key[4*i + j] ^ frame[j]
    result = bytearray(data)
    for i in range(min(len(data), 256)):
        for turn in range(10, -1, -1):
            result[i] ^= key[16*turn + i % 16]
    return bytes(result)


def hed_rows(data):
    if len(data) % 32 or not data or len(data) > 16 * 1024 * 1024:
        raise ValueError("invalid HED length")
    return list(struct.iter_unpack("<16sQii", data))


def read_original(pkg, row, tables):
    _, offset, record_size, actual_size = row
    file_size = pkg.seek(0, 2)
    if record_size < 16 or offset + record_size > file_size:
        raise ValueError("HED record exceeds package")
    pkg.seek(offset)
    seed = pkg.read(16)
    length, count, compressed, _ = struct.unpack("<4i", seed)
    if not 0 < length <= MAX_ASSET or not 0 <= count <= 4096 or compressed < -2:
        raise ValueError("invalid original HD asset header")
    payload_size = compressed if compressed >= 0 else length
    relative = 16 + count * 48
    if payload_size <= 0 or payload_size > MAX_ASSET or relative + payload_size > record_size:
        raise ValueError("original payload exceeds HED record")
    pkg.seek(offset + relative)
    packed = pkg.read(payload_size)
    if len(packed) != payload_size:
        raise ValueError("truncated original payload")
    if compressed > -2:
        packed = crypt(packed, seed, tables)
    if compressed >= 0:
        decoder = zlib.decompressobj(-15)
        data = decoder.decompress(packed[2:], length + 1)
        if not decoder.eof or decoder.unconsumed_tail:
            raise ValueError("incomplete or oversized deflate stream")
    else:
        data = packed
    if len(data) != length:
        raise ValueError("original decompressed length mismatch")
    return data, {"pkgRecordOffset": offset, "pkgRecordLength": record_size,
                  "hedActualLength": actual_size, "originalHeaderHex": seed.hex(),
                  "originalPayloadOffset": offset + relative, "storedLength": payload_size,
                  "remasteredCountNotExtracted": count}


def bar_rows(data):
    if span(data, 0, 4) != b"BAR\x01":
        raise ValueError("not a BAR archive")
    count = struct.unpack_from("<I", span(data, 4, 4))[0]
    table = span(data, 16, count * 16)
    rows = []
    for i, (kind, index, name, offset, length) in enumerate(struct.iter_unpack("<HH4sII", table)):
        content = span(data, offset, length)
        rows.append({"entry": i, "type": kind, "index": index,
                     "nameHex": name.hex(), "name": name.rstrip(b"\0").decode("ascii", "replace"),
                     "offset": offset, "length": length, "sha256": sha(content)})
    return rows


def atkp_rows(data, bar):
    """Capture row identities/ranges only; no speculative field/ABI interpretation."""
    result = []
    for entry in bar:
        if entry["name"] != "atkp":
            continue
        content = span(data, entry["offset"], entry["length"])
        count = struct.unpack_from("<I", span(content, 4, 4))[0]
        if not count or (len(content) - 8) % count:
            raise ValueError("unexpected ATKP row layout")
        stride = (len(content) - 8) // count
        if stride < 4:
            raise ValueError("short ATKP row")
        for i in range(count):
            row = span(content, 8 + i*stride, stride)
            attack, sub = struct.unpack_from("<HH", row)
            if attack in ATTACK_IDS:
                result.append({"id": attack, "subId": sub, "row": i,
                               "offset": entry["offset"] + 8 + i*stride,
                               "length": stride, "sha256": sha(row)})
    return result


def pe_traps(data, traps):
    pe = struct.unpack_from("<I", span(data, 0x3c, 4))[0]
    if span(data, pe, 4) != b"PE\0\0":
        raise ValueError("not a PE")
    machine, count = struct.unpack_from("<HH", span(data, pe + 4, 4))
    optional_size = struct.unpack_from("<H", span(data, pe + 20, 2))[0]
    if machine != 0x8664 or struct.unpack_from("<H", span(data, pe + 24, 2))[0] != 0x20b:
        raise ValueError("not an AMD64 PE+")
    image_base = struct.unpack_from("<Q", span(data, pe + 48, 8))[0]
    sections = []
    for i in range(count):
        row = span(data, pe + 24 + optional_size + 40*i, 40)
        virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<4I", row, 8)
        sections.append((rva, raw_size, raw_offset))

    def file_offset(rva, size):
        for start, raw_size, raw in sections:
            if start <= rva and rva + size <= start + raw_size:
                offset = raw + rva - start
                span(data, offset, size)
                return offset
        raise ValueError(f"RVA {rva:#x} has no file-backed range")

    rows = []
    for table, index in traps:
        pointer_offset = file_offset(0x753490 + 8*table, 8)
        table_va = struct.unpack_from("<Q", data, pointer_offset)[0]
        entry_rva = table_va - image_base + 16*index
        entry_offset = file_offset(entry_rva, 16)
        fn, flags = struct.unpack_from("<QI", data, entry_offset)
        fn_rva = fn - image_base
        fn_offset = file_offset(fn_rva, 0x100)
        rows.append({"table": table, "index": index, "entryRva": entry_rva,
                     "entryFileOffset": entry_offset, "functionRva": fn_rva,
                     "functionFileOffset": fn_offset, "flags": flags,
                     "first256Sha256": sha(span(data, fn_offset, 0x100))})
    return rows


def output_path(path, repo=REPO, install=None):
    local = repo.resolve() / ".local"
    if local.is_symlink():
        raise ValueError(".local must not be a symlink")
    target = path.resolve()
    if not target.is_relative_to(local) or target == local or path.exists():
        raise ValueError("output must be a new directory strictly below this worktree's .local")
    if install and (target.is_relative_to(install.resolve()) or install.resolve().is_relative_to(target)):
        raise ValueError("output overlaps install")
    return target


def capture(install, out, key_source, plan):
    out = output_path(out, install=install)
    if shutil.disk_usage(REPO).free < 20 * 1024**3 + MAX_TOTAL:
        raise ValueError("less than 20 GiB plus 512 MiB capture headroom")
    tables = load_keys(key_source, plan["openkhEncryptionSha256"])
    pe_path = install / plan["pe"]["path"]
    if pe_path.stat().st_size != plan["pe"]["bytes"]:
        raise ValueError("installed PE length differs from retained baseline")
    pe = pe_path.read_bytes()
    if sha(pe) != plan["pe"]["sha256"]:
        raise ValueError("installed PE differs from retained baseline; stop for qualification")
    native = pe_traps(pe, plan["traps"])
    wanted = {hashlib.md5(name.encode("utf-8")).digest(): name for name in plan["assets"]}
    matches = {}
    heds = []
    for relative, expected in plan["hedSha256"].items():
        path = install / relative
        if path.stat().st_size > 16 * 1024 * 1024:
            raise ValueError("oversized HED")
        data = path.read_bytes()
        if sha(data) != expected:
            raise ValueError(f"HED differs from retained baseline: {relative}")
        heds.append({"path": relative, "sha256": sha(data)})
        for i, row in enumerate(hed_rows(data)):
            if row[0] in wanted:
                name = wanted[row[0]]
                if name in matches:
                    raise ValueError(f"duplicate package match: {name}")
                matches[name] = (path.with_suffix(".pkg"), row, i)
    if set(matches) != set(plan["assets"]):
        raise ValueError(f"missing assets: {sorted(set(plan['assets']) - set(matches))}")
    out.mkdir(parents=True, exist_ok=False)
    (out / "originals").mkdir()
    (out / "ai").mkdir()
    (out / "native").mkdir()
    (out / "native" / "kh2fm.exe").write_bytes(pe)
    result = {"schema": 1, "complete": False, "qualification": "UNQUALIFIED; capture is not ABI proof",
              "pe": plan["pe"], "hed": heds, "nativeTrapEntries": native,
              "nativeLifecycleRvas": [0x3CF830, 0x3CFB10], "assets": [], "openkhCommands": []}
    total = len(pe)
    try:
        for name in plan["assets"]:
            pkg, row, hed_index = matches[name]
            with pkg.open("rb") as stream:
                data, metadata = read_original(stream, row, tables)
            total += len(data)
            if total * 2 > MAX_TOTAL:  # reserve room for extracted AI and later unpacking
                raise ValueError("capture exceeds 512 MiB budget including unpack headroom")
            digest = sha(data)
            expected = plan["assets"][name]
            if expected and not (digest.startswith(expected[0]) and digest.endswith(expected[1])):
                raise ValueError(f"asset differs from prior prefix/suffix constraint: {name}")
            target = out / "originals" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            # Objentry is a standalone table, not a BAR; retain it for OpenKH.
            bar = [] if name == "00objentry.bin" else bar_rows(data)
            asset = {"path": name, "sha256": digest, "bytes": len(data), "bar": bar,
                     "pkg": str(pkg), "hedRowOffset": hed_index * 32,
                     "constraint": "partial prior hash" if expected else "new full hash, no prior pin",
                     **metadata}
            if name == "00battle.bin":
                asset["selectedAtkpRows"] = atkp_rows(data, bar)
            result["assets"].append(asset)
            if bar:
                result["openkhCommands"].append(["OpenKh.Command.Bar.exe", "unpack", str(target),
                                                 "-o", str(out / "unpacked" / name)])
            for entry in bar:
                if entry["type"] == 7:  # OpenKH Bar.EntryType.AiBinary
                    ai = out / "ai" / (name.replace("/", "__") + f"__{entry['entry']}.bdx")
                    ai.write_bytes(span(data, entry["offset"], entry["length"]))
                    labels = []
                    if name in ("obj/B_EX110.mdlx", "obj/B_EX110_SKIRMISH.mdlx"):
                        for label in plan["axelPublicLabels"]:
                            labels.extend(["-l", str(label)])
                    result["openkhCommands"].append(["OpenKh.Command.Bdxio.exe", "decode", "-b",
                                                     *labels, str(ai), str(ai.with_suffix(".bdscript"))])
        result["complete"] = True
    finally:
        (out / "capture.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--key-source", type=Path)
    args = parser.parse_args()
    plan = json.loads(PLAN.read_text())
    if not any((args.install, args.out, args.key_source)):
        print(json.dumps(plan, indent=2))
        return
    if not all((args.install, args.out, args.key_source)):
        parser.error("capture requires --install, --out and --key-source")
    try:
        result = capture(args.install, args.out, args.key_source, plan)
    except (OSError, ValueError, struct.error, zlib.error) as error:
        parser.exit(1, f"Capture refused/failed: {error}\n")
    print(f"Captured {len(result['assets'])} originals; local manifest: {args.out / 'capture.json'}")


if __name__ == "__main__":
    main()
