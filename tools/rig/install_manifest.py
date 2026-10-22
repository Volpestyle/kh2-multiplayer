"""Read-only install SHA256 inventory; write receipts under this checkout's .local.

capture --root INSTALL --out .local/RUN/before.json
diff --before BEFORE --after AFTER --out .local/RUN/diff.json
Exit 0: captured/matched; 1: mismatch (stop further runs); 2: refused/error.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import stat
import sys

LOCAL = Path(__file__).resolve().parents[2] / ".local"


def ordinary(path: Path):
    info = path.lstat()
    if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
        raise ValueError(f"reparse point refused: {path}")
    return info


def files(root: Path):
    """Walk every file; refuse links and unknown file types rather than skipping."""
    if not stat.S_ISDIR(ordinary(root).st_mode):
        raise ValueError("install root must be a directory")
    pending, found = [root], []
    while pending:
        for path in sorted(pending.pop().iterdir()):
            info = ordinary(path)
            if stat.S_ISDIR(info.st_mode):
                pending.append(path)
            elif stat.S_ISREG(info.st_mode):
                found.append(path)
            else:
                raise ValueError(f"non-file entry refused: {path}")
    return sorted(found)


def capture(root: Path) -> dict:
    root = Path(os.path.abspath(root))
    paths, rows = files(root), []
    for path in paths:
        before = ordinary(path)
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        after = ordinary(path)
        signature = lambda s: (s.st_ino, s.st_size, s.st_mtime_ns)
        if signature(before) != signature(after):
            raise ValueError(f"file changed while hashing: {path}")
        rows.append(dict(path=path.relative_to(root).as_posix(), bytes=after.st_size, sha256=digest.hexdigest()))
    if paths != files(root):
        raise ValueError("install file set changed while hashing")
    if not rows:
        raise ValueError("empty install refused")
    return dict(schema=1, root=str(root), capturedUtc=dt.datetime.now(dt.timezone.utc).isoformat(),
                fileCount=len(rows), files=rows)


def read_manifest(path: Path) -> dict:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if (not isinstance(value, dict) or value.get("schema") != 1
            or not isinstance(value.get("root"), str) or not Path(value["root"]).is_absolute()
            or not isinstance(value.get("files"), list) or not value["files"]
            or value.get("fileCount") != len(value["files"])):
        raise ValueError("invalid install manifest")
    seen = set()
    for row in value["files"]:
        if not isinstance(row, dict):
            raise ValueError("invalid install file row")
        name, size, digest = row.get("path"), row.get("bytes"), row.get("sha256")
        if (not isinstance(name, str) or not name or "\\" in name or ":" in name
                or name.startswith("/") or any(p in ("", ".", "..") for p in name.split("/"))
                or name.casefold() in seen or type(size) is not int or size < 0
                or not isinstance(digest, str) or len(digest) != 64
                or any(c not in "0123456789abcdef" for c in digest)):
            raise ValueError("invalid/duplicate install file row")
        seen.add(name.casefold())
    return value


def compare(before: dict, after: dict) -> dict:
    if os.path.normcase(before["root"]) != os.path.normcase(after["root"]):
        raise ValueError("install roots differ")
    a, b = ({r["path"]: (r["bytes"], r["sha256"]) for r in m["files"]} for m in (before, after))
    added, removed = sorted(b.keys() - a.keys()), sorted(a.keys() - b.keys())
    changed = sorted(p for p in a.keys() & b.keys() if a[p] != b[p])
    return dict(schema=1, root=before["root"], matched=not (added or removed or changed),
                beforeCount=len(a), afterCount=len(b), added=added, removed=removed, changed=changed)


def write_local(path: Path, value: dict, install: Path):
    path = path.absolute()
    resolved = path.resolve()
    if not resolved.is_relative_to(LOCAL.resolve()) or resolved.is_relative_to(install.resolve()):
        raise ValueError("output must be under this checkout's .local and outside the install")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("x", encoding="utf-8") as stream:  # never replace a before/after receipt
        json.dump(value, stream, indent=2)
        stream.write("\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    take = modes.add_parser("capture")
    take.add_argument("--root", type=Path, required=True)
    take.add_argument("--out", type=Path, required=True)
    diff = modes.add_parser("diff")
    diff.add_argument("--before", type=Path, required=True)
    diff.add_argument("--after", type=Path, required=True)
    diff.add_argument("--out", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.mode == "capture":
            root = Path(os.path.abspath(args.root))
            # Reject an in-install output before reading anything or creating directories.
            if args.out.resolve().is_relative_to(root.resolve()):
                raise ValueError("output inside install refused")
            value = capture(root)
            write_local(args.out, value, root)
            print(f"Captured {value['fileCount']} files: {args.out}")
            return 0
        before, after = read_manifest(args.before), read_manifest(args.after)
        result = compare(before, after)
        write_local(args.out, result, Path(before["root"]))
        if not result["matched"]:
            print(f"STOP: install mismatch; no further runs. Report {args.out} to the lead.", file=sys.stderr)
            return 1
        print(f"Install content unchanged ({result['afterCount']} files): {args.out}")
        return 0
    except (OSError, ValueError, TypeError, KeyError) as error:
        print(f"Install manifest refused: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
