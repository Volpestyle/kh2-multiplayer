"""Delete SaveGuard movie twins (VUH-2763) of exited game PIDs; print a JSON receipt for closure.json.

clean --logs build/rig/logs --pid 1234 [--pid 5678]
SaveGuard copies a shipped movie into <logs>/save_sandbox_<pid>/ when the game rewrites its header.
Those twins are copies of copyrighted assets: they stay local, are never published, and are removed
at closure. Only .mp4 files are deleted; save twins in the same sandbox are kept.
Exit 0: cleaned (or none); 1: a twin could not be deleted; 2: refused (PID still running, link, bad path).
"""
from __future__ import annotations

import argparse
import ctypes
import json
from pathlib import Path
import stat
import sys

STILL_ACTIVE = 259


def running(pid: int) -> bool:
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.restype = ctypes.c_void_p
    handle = kernel.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    try:
        code = ctypes.c_ulong()
        return not kernel.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code)) or code.value == STILL_ACTIVE
    finally:
        kernel.CloseHandle(ctypes.c_void_p(handle))


def clean(logs: Path, pids: list[int]) -> tuple[int, dict]:
    receipt = {"logs": str(logs), "sandboxes": [], "deleted": [], "failed": [], "refused": []}
    for pid in pids:
        sandbox = logs / f"save_sandbox_{pid}"
        if running(pid):
            receipt["refused"].append(f"pid {pid} is still running")
            continue
        if not sandbox.exists():
            continue
        receipt["sandboxes"].append(sandbox.name)
        for path in sorted(sandbox.rglob("*")):
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & 0x400:
                receipt["refused"].append(f"reparse point: {path}")
                continue
            if not stat.S_ISREG(info.st_mode) or path.suffix.lower() != ".mp4":
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
    code, receipt = clean(args.logs.resolve(), args.pid)
    print(json.dumps(receipt, indent=2))
    return code


if __name__ == "__main__":
    sys.exit(main())
