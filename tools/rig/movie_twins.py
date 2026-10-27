"""Delete SaveGuard install-asset twins (VUH-2775) of exited game PIDs; print a JSON receipt for closure.json.

clean --logs build/rig/logs --pid 1234 [--pid 5678]
When the game rewrites an installed KH2ReSource file (the cipher movies' header), SaveGuard gives it a copy in
<logs>/save_sandbox_<pid>/install-assets/. Those twins are copies of copyrighted assets: they stay local, are
never published, and are removed at closure. Save twins elsewhere in the sandbox are kept.
Fails closed: a PID whose state can't be read counts as running; a logs root outside this checkout's
build/rig/logs or the temp directory, or any link/junction on the way to or inside a twin tree, is refused.
Exit 0: cleaned (or none); 1: a twin could not be deleted; 2: refused.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import stat
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
STILL_ACTIVE = 259
ERROR_INVALID_PARAMETER = 87  # OpenProcess on a PID that doesn't exist


def kernel32():
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    return kernel


def running(pid: int) -> bool:
    """True unless the process is known to be gone."""
    kernel = kernel32()
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return ctypes.get_last_error() != ERROR_INVALID_PARAMETER
    try:
        code = ctypes.c_ulong()
        return not kernel.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code)) or code.value == STILL_ACTIVE
    finally:
        kernel.CloseHandle(ctypes.c_void_p(handle))


def linked(path: Path) -> bool:
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def allowed_logs(logs: Path) -> bool:
    real = logs.resolve()
    roots = [(ROOT / "build/rig/logs").resolve(), Path(tempfile.gettempdir()).resolve()]
    return any(real == root or root in real.parents for root in roots)


def clean(logs: Path, pids: list[int]) -> tuple[int, dict]:
    receipt = {"logs": str(logs), "sandboxes": [], "deleted": [], "failed": [], "refused": []}

    def refuse(reason: str) -> tuple[int, dict]:
        receipt["refused"].append(reason)
        receipt["deletedBytes"] = 0
        return 2, receipt

    if not logs.is_dir() or not allowed_logs(logs):
        return refuse(f"logs root is missing or outside build/rig/logs and the temp dir: {logs}")
    # The logs root and every ancestor, as spelled, before anything is resolved or walked.
    for part in (logs, *logs.parents):
        if part.exists() and linked(part):
            return refuse(f"link in the logs path: {part}")
    for pid in pids:
        if running(pid):
            receipt["refused"].append(f"pid {pid} may still be running")
            continue
        sandbox = logs / f"save_sandbox_{pid}"
        twins = sandbox / "install-assets"
        if not sandbox.exists():
            continue
        if linked(sandbox) or (twins.exists() and linked(twins)):
            receipt["refused"].append(f"link in sandbox path: {twins}")
            continue
        if not twins.exists():
            continue
        receipt["sandboxes"].append(sandbox.name)
        real = twins.resolve()
        for directory, dirs, files in os.walk(twins, topdown=True):
            here = Path(directory)
            for name in list(dirs):
                if linked(here / name):
                    receipt["refused"].append(f"link inside twin tree: {here / name}")
                    dirs.remove(name)
            for name in files:
                path = here / name
                info = path.lstat()
                if linked(path) or not stat.S_ISREG(info.st_mode) or real not in path.resolve().parents:
                    receipt["refused"].append(f"not a plain file inside the twin tree: {path}")
                    continue
                entry = {"path": str(path.relative_to(logs)), "bytes": info.st_size}
                try:
                    path.unlink()
                    receipt["deleted"].append(entry)
                except OSError as error:
                    receipt["failed"].append({**entry, "error": str(error)})
    receipt["deletedBytes"] = sum(entry["bytes"] for entry in receipt["deleted"])
    return (2 if receipt["refused"] else 1 if receipt["failed"] else 0), receipt


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    cmd = sub.add_parser("clean")
    cmd.add_argument("--logs", type=Path, required=True)
    cmd.add_argument("--pid", type=int, action="append", required=True)
    args = parser.parse_args()
    code, receipt = clean(args.logs.absolute(), args.pid)
    print(json.dumps(receipt, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
