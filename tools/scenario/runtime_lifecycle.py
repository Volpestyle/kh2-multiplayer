"""Bounded interruption of one launch-owned Friend1 runtime; never a PID opener.

The retirement callback is a nonblocking poll. It must join fresh relay departure
evidence to the current same-session survivor rosters, and mark identityComplete.
This helper proves safe lifecycle handling, not native reconnect acceptance.
"""
from __future__ import annotations

import ctypes
import math
import os
from pathlib import Path
import subprocess
import threading
import time


class RuntimePauseError(RuntimeError):
    """Failure with a JSON-serializable lifecycle receipt attached."""


class OwnedHandleAPI:
    """All native calls use the supplied original Popen handle only."""

    def __init__(self):
        if os.name != "nt":
            raise RuntimePauseError("runtime suspension requires Windows")
        self._kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        self._nt = ctypes.WinDLL("ntdll", use_last_error=True)
        handle = ctypes.c_void_p
        self._pid = self._kernel.GetProcessId
        self._pid.argtypes = [handle]
        self._pid.restype = ctypes.c_uint32
        self._image = self._kernel.QueryFullProcessImageNameW
        self._image.argtypes = [handle, ctypes.c_uint32, ctypes.c_wchar_p,
                               ctypes.POINTER(ctypes.c_uint32)]
        self._image.restype = ctypes.c_int32
        self._suspend = self._nt.NtSuspendProcess
        self._resume = self._nt.NtResumeProcess
        for call in (self._suspend, self._resume):
            call.argtypes = [handle]
            # NTSTATUS is signed 32-bit, independent of Python/host pointer size.
            call.restype = ctypes.c_int32

    def process_id(self, handle):
        result = self._pid(handle)
        if not result:
            raise ctypes.WinError(ctypes.get_last_error())
        return result

    def executable(self, handle):
        size = ctypes.c_uint32(32768)
        value = ctypes.create_unicode_buffer(size.value)
        if not self._image(handle, 0, value, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        return value.value

    def suspend(self, handle):
        return self._suspend(handle)

    def resume(self, handle):
        return self._resume(handle)


def _path(value):
    return os.path.normcase(str(Path(value).resolve(strict=True)))


def _flag_values(args, flag):
    values = []
    for index, arg in enumerate(args):
        if arg == flag:
            values.append(args[index + 1] if index + 1 < len(args) else None)
        elif arg.startswith(flag + "="):
            values.append(arg.split("=", 1)[1])
    return values


def _status(value):
    if type(value) is not int:
        raise RuntimePauseError("native API did not return integer NTSTATUS")
    return ctypes.c_int32(value).value


def pause_owned_friend_runtime(
        processes, instances, runtime, game_pid, friend_index, expected_executable,
        wait_retired, check_all, timeout_seconds=20, *, relay_offset,
        expected_connection_id, _api=None, _clock=time.monotonic, _sleep=time.sleep):
    """Suspend/resume the exact owned Popen, retaining its automatic recovery pin.

    Only Friend1/index 1 is supported. ``processes`` is Context.processes;
    ``instances`` is Context.instances. No name/PID lookup or handle opening is
    permitted. ``wait_retired(relay_offset)`` returns None, {retired: False}, or a complete proof:
    {retired: True, identityComplete: True, slot: 1, connectionId: expected,
    byteOffset: >= relay_offset, ...}. Callback exceptions and cancellation are
    re-raised with .receipt after recovery. ``check_all`` must check all original
    games, runtimes, and relay without blocking. Tests may inject a handle API.

    A wall-clock watchdog independently resumes on callback overrun. Scheduling
    and native API availability still limit any user-mode wall-clock guarantee.
    Resume failure causes bounded retries then cleanup of this runtime only;
    that is terminal/pin-lost and can never produce a successful result.
    """
    receipt = {"status": "validating", "completed": False, "resumed": False,
               "pinPreserved": False, "runtimePid": getattr(runtime, "pid", None),
               "gamePid": game_pid, "slot": friend_index,
               "expectedConnectionId": expected_connection_id,
               "relayOffset": relay_offset, "retirement": None,
               "suspendStatus": None, "resumeAttempts": [],
               "suspendAttempted": False, "deadlineExpired": False,
               "terminal": False, "cleanup": []}
    timer = None
    lock = threading.RLock()
    recovery_done = False
    handle = None
    api = None

    def fail(message):
        raise RuntimePauseError(message)

    def recover():
        nonlocal recovery_done
        with lock:
            if recovery_done or not receipt["suspendAttempted"]:
                return
            # A failed or interrupted suspend call may have partially acted.
            # Always attempt the matching resume before considering cleanup.
            for attempt in range(3):
                row = {"attempt": attempt + 1}
                receipt["resumeAttempts"].append(row)
                try:
                    status = _status(api.resume(handle))
                    row["status"] = status
                    if status >= 0:
                        receipt["resumed"] = True
                        recovery_done = True
                        try:
                            receipt["pinPreserved"] = runtime.poll() is None
                        except BaseException as error:
                            row["livenessError"] = f"{type(error).__name__}: {error}"
                        return
                except BaseException as error:
                    row["error"] = f"{type(error).__name__}: {error}"
            receipt.update(terminal=True, pinPreserved=False, status="cleanup_required")
            # Popen terminate/wait use its original retained handle on Windows.
            # Refuse even owned cleanup if somebody replaced that handle.
            if getattr(runtime, "_handle", None) is not handle:
                receipt["cleanup"].append({"error": "original Popen handle replaced"})
                receipt["status"] = "cleanup_unresolved"
                recovery_done = True
                return
            for attempt in range(2):
                row = {"attempt": attempt + 1}
                receipt["cleanup"].append(row)
                try:
                    if runtime.poll() is None:
                        runtime.terminate()
                        row["terminated"] = True
                    row["returncode"] = runtime.wait(timeout=1)
                    receipt["status"] = "terminated"
                    recovery_done = True
                    return
                except BaseException as error:
                    row["error"] = f"{type(error).__name__}: {error}"
            receipt["status"] = "cleanup_unresolved"
            recovery_done = True

    def watchdog():
        with lock:
            if recovery_done:
                return
            receipt["deadlineExpired"] = True
        recover()

    try:
        if (type(timeout_seconds) not in (int, float) or not math.isfinite(timeout_seconds)
                or not 1 <= timeout_seconds <= 20):
            fail("timeout_seconds must be finite and in [1, 20]")
        if type(friend_index) is not int or friend_index != 1:
            fail("only owned Friend1/index 1 may be paused")
        if type(game_pid) is not int or game_pid <= 0:
            fail("game_pid must be a positive integer")
        if type(relay_offset) is not int or relay_offset < 0:
            fail("relay_offset must be a nonnegative integer")
        if type(expected_connection_id) is not int or expected_connection_id <= 0:
            fail("expected_connection_id must be a positive integer")
        if not callable(wait_retired) or not callable(check_all):
            fail("retirement and liveness callbacks are required")
        if not isinstance(runtime, subprocess.Popen):
            fail("runtime must be the original subprocess.Popen")
        owned = list(processes)
        if any(not isinstance(row, (tuple, list)) or len(row) != 3 for row in owned):
            fail("invalid process ownership registry")
        exact = [row for row in owned if row[1] is runtime]
        named = [row for row in owned if row[0] == "runtime_1"]
        if len(exact) != 1 or len(named) != 1 or named[0][1] is not runtime:
            fail("runtime must be uniquely registered as runtime_1")
        if len(instances) <= 1:
            fail("Friend1 instance is absent from owned registry")
        inst = instances[1]
        if (getattr(inst, "index", None) != 1 or getattr(inst, "pid", None) != game_pid
                or sum(getattr(i, "pid", None) == game_pid for i in instances) != 1
                or sum(getattr(i, "index", None) == 1 for i in instances) != 1):
            fail("game PID does not uniquely match owned Friend1 instance")
        if runtime.poll() is not None or runtime.pid == game_pid:
            fail("owned runtime is exited or aliases the game")
        args = runtime.args
        if (not isinstance(args, (list, tuple)) or not args
                or any(not isinstance(a, str) for a in args)):
            fail("original runtime argv list is required")
        expected = _path(expected_executable)
        if _path(args[0]) != expected:
            fail("runtime argv executable differs from expected executable")
        if args.count("--network") != 1 or any(a.startswith("--network=") for a in args):
            fail("runtime requires exactly one --network flag")
        for flag, value in (("--pid", str(game_pid)), ("--role", "friend1")):
            if args.count(flag) != 1 or _flag_values(args, flag) != [value]:
                fail(f"runtime requires exactly one {flag} {value}")
        for name, proc, _ in owned:
            if proc is runtime or proc.poll() is not None:
                continue
            other = getattr(proc, "args", None)
            if not isinstance(other, (list, tuple)) or any(not isinstance(a, str) for a in other):
                fail(f"cannot exclude duplicate writer in owned process {name}")
            for value in _flag_values(other, "--pid"):
                try:
                    duplicate = int(value) == game_pid
                except (TypeError, ValueError):
                    fail(f"cannot parse owned process {name} --pid")
                if duplicate:
                    fail("another live owned process writes the same game PID")
        handle = getattr(runtime, "_handle", None)
        if handle is None or int(handle) in (0, -1):
            fail("original runtime has no valid owned process handle")
        api = _api if _api is not None else OwnedHandleAPI()
        if api.process_id(handle) != runtime.pid or _path(api.executable(handle)) != expected:
            fail("owned handle process identity differs from expected runtime")
        check_all()
        if runtime.poll() is not None or runtime._handle is not handle:
            fail("owned runtime changed before suspension")
        started = _clock()
        deadline = started + timeout_seconds
        receipt.update(status="suspending", timeoutSeconds=timeout_seconds,
                       executable=expected)
        with lock:
            # Arm before making a call whose outcome could become uncertain.
            timer = threading.Timer(timeout_seconds, watchdog)
            timer.daemon = True
            timer.start()
            receipt["suspendAttempted"] = True
            receipt["suspendStatus"] = _status(api.suspend(handle))
            if receipt["suspendStatus"] < 0:
                fail(f"NtSuspendProcess failed: {receipt['suspendStatus']}")
            receipt["status"] = "suspended"
        while True:
            if _clock() >= deadline or receipt["deadlineExpired"]:
                receipt["deadlineExpired"] = True
                fail("retirement deadline expired")
            check_all()
            if _clock() >= deadline or receipt["deadlineExpired"]:
                receipt["deadlineExpired"] = True
                fail("liveness callback exceeded retirement deadline")
            proof = wait_retired(relay_offset)
            if _clock() >= deadline or receipt["deadlineExpired"]:
                receipt["deadlineExpired"] = True
                fail("retirement callback exceeded retirement deadline")
            if proof is not None and not (isinstance(proof, dict) and proof.get("retired") is False):
                receipt["retirement"] = proof
                if (not isinstance(proof, dict) or proof.get("retired") is not True
                        or proof.get("identityComplete") is not True
                        or type(proof.get("slot")) is not int or proof["slot"] != 1
                        or type(proof.get("connectionId")) is not int
                        or proof["connectionId"] != expected_connection_id
                        or type(proof.get("byteOffset")) is not int
                        or proof["byteOffset"] < relay_offset):
                    fail("retirement proof is incomplete or has wrong identity/boundary")
                break
            _sleep(min(0.05, max(0, deadline - _clock())))
        recover()
        if receipt["deadlineExpired"]:
            fail("retirement deadline expired during recovery")
        if not receipt["resumed"] or not receipt["pinPreserved"] or receipt["terminal"]:
            fail("runtime could not be resumed alive with its original recovery pin")
        check_all()
        if runtime.poll() is not None or runtime._handle is not handle:
            receipt["pinPreserved"] = False
            fail("owned runtime changed after resume")
        receipt.update(status="resumed", completed=True, elapsedSeconds=_clock() - started)
        return receipt
    except BaseException as error:
        recover()
        if not receipt["terminal"]:
            receipt["status"] = "failed"
        receipt["error"] = f"{type(error).__name__}: {error}"
        receipt["completed"] = False
        error.receipt = receipt
        raise
    finally:
        recover()
        if timer is not None:
            timer.cancel()
