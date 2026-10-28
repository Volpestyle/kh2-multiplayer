"""Compare protected saves with a nonempty SHA-256 baseline and retained copies.

Read-only on saves and copies. Emits one JSON object and exits 1 on any mismatch
or invalid input. Used by the VUH-2808 desk closure instead of PowerShell array
comparison (which wrapped the four baseline rows into one object).
"""
from __future__ import annotations

import argparse
import filecmp
import hashlib
import json
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify(baseline: Path, save_root: Path, copies: Path) -> dict:
    try:
        before = json.loads(baseline.read_text(encoding="utf-8-sig"))
        if not isinstance(before, list) or not before:
            raise ValueError("baseline must be a nonempty array of save records")
        save_root = save_root.resolve(strict=True)
        expected, rows = set(), []
        for row in before:
            if not isinstance(row, dict) or not isinstance(row.get("path"), str):
                raise ValueError("baseline record needs a path")
            digest = row.get("sha256")
            if (not isinstance(digest, str) or len(digest) != 64
                    or any(c not in "0123456789abcdef" for c in digest)):
                raise ValueError("baseline record needs a lowercase SHA-256")
            path = Path(row["path"]).resolve(strict=True)
            relative = path.relative_to(save_root)
            if path in expected:
                raise ValueError("duplicate baseline save")
            expected.add(path)
            retained = copies / relative
            current_hash, retained_hash = sha256(path), sha256(retained)
            # Discard filecmp's process cache: every verification reads the files.
            filecmp.clear_cache()
            rows.append(dict(path=str(path), expected=digest, current=current_hash,
                             retained=retained_hash,
                             baselineMatches=current_hash == retained_hash == digest,
                             byteIdentical=filecmp.cmp(path, retained, shallow=False)))
        actual = {p.resolve() for p in save_root.rglob("*") if p.is_file()}
        extra, missing = sorted(map(str, actual - expected)), sorted(map(str, expected - actual))
        changed = [row for row in rows if not row["baselineMatches"] or not row["byteIdentical"]]
        return {"pass": not (changed or extra or missing), "saveCount": len(rows),
                "saveRows": rows, "changed": changed, "extra": extra, "missing": missing}
    except (OSError, ValueError, TypeError) as error:
        return {"pass": False, "error": str(error)}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--save-root", type=Path, required=True)
    parser.add_argument("--copies", type=Path, required=True)
    args = parser.parse_args(argv)
    result = verify(args.baseline, args.save_root, args.copies)
    print(json.dumps(result, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
