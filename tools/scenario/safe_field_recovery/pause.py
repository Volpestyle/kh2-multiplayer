"""Test-only fixed Friend1 pause. All control uses the original Popen handle.

Identity gates mirror runtime_lifecycle.pause_owned_friend_runtime; path/argv and
native suspend/resume calls reuse that module. No process opener or PID lookup.

VUH-2640 (Quill r1 on the threshold): the pause must expire the 4000 ms
event-control lease and nothing else, and the relay's 5000 ms peer liveness
counts silence from the last packet it received, not from the suspension. So
the relay's silence at resume is the pre-pause age (unmeasured here) plus the
actual pause plus the first packet's delivery. The budget below is stated, not a
guarantee: a pause outside it is refused here, and evidence.correlate refuses a
proof whose relay log shows a peer timeout. A Python Timer is a scheduled
callback, not a real-time bound, so a late watchdog shows up as a refused
duration, never as a pass.
"""
import ctypes
import subprocess
import threading
import time


LEASE_MS = 4000            # eventhold::HeartbeatTimeoutMs (check.py reads the header)
RELAY_TIMEOUT_MS = 5000    # the relay's default heartbeatTimeoutMs (check.py reads it)
PAUSE_MS = 4200            # requested pause: past the lease by EXPIRY_MARGIN_MS
EXPIRY_MARGIN_MS = 200     # the native view sees the gap pass the lease within a pump
PRE_AGE_BUDGET_MS = 400    # relay silence already elapsed at the suspend (field avatars ~every frame; heartbeat 1 s)
OVERRUN_BUDGET_MS = 200    # measured suspend-to-resume beyond PAUSE_MS
DELIVERY_BUDGET_MS = 100   # resume to the relay receiving the runtime's next packet
WATCHDOG_MS = RELAY_TIMEOUT_MS - PRE_AGE_BUDGET_MS - DELIVERY_BUDGET_MS
assert PAUSE_MS >= LEASE_MS + EXPIRY_MARGIN_MS
assert PRE_AGE_BUDGET_MS + PAUSE_MS + OVERRUN_BUDGET_MS + DELIVERY_BUDGET_MS < RELAY_TIMEOUT_MS
assert PAUSE_MS + OVERRUN_BUDGET_MS <= WATCHDOG_MS


def budget():
    return {"leaseMs": LEASE_MS, "relayTimeoutMs": RELAY_TIMEOUT_MS, "pauseMs": PAUSE_MS,
            "expiryMarginMs": EXPIRY_MARGIN_MS, "preAgeBudgetMs": PRE_AGE_BUDGET_MS,
            "overrunBudgetMs": OVERRUN_BUDGET_MS, "deliveryBudgetMs": DELIVERY_BUDGET_MS,
            "watchdogMs": WATCHDOG_MS, "preAgeMeasured": False}


def validate(lifecycle, processes, instances, runtime, expected, api):
    fail = lifecycle.RuntimePauseError
    if not isinstance(runtime, subprocess.Popen):
        raise fail("original Popen required")
    owned = list(processes)
    if any(not isinstance(row, (tuple, list)) or len(row) != 3 for row in owned):
        raise fail("invalid retained process registry")
    exact = [row for row in owned if row[1] is runtime]
    named = [row for row in owned if row[0] == "runtime_1"]
    if len(exact) != 1 or len(named) != 1 or named[0][1] is not runtime:
        raise fail("runtime must be uniquely registered as runtime_1")
    if len(instances) != 2 or instances[1].index != 1:
        raise fail("exactly two ordered owned games required")
    game_pid = instances[1].pid
    if type(game_pid) is not int or game_pid <= 0 or sum(i.pid == game_pid for i in instances) != 1:
        raise fail("ambiguous Friend1 game binding")
    if runtime.poll() is not None or runtime.pid == game_pid:
        raise fail("runtime exited or aliases game")
    args = runtime.args
    if not isinstance(args, (list, tuple)) or not args or any(not isinstance(a, str) for a in args):
        raise fail("retained argv required")
    expected = lifecycle._path(expected)
    if lifecycle._path(args[0]) != expected:
        raise fail("wrong runtime executable")
    if args.count("--network") != 1 or any(a.startswith("--network=") for a in args):
        raise fail("exact network flag required")
    for flag, value in (("--pid", str(game_pid)), ("--role", "friend1")):
        if args.count(flag) != 1 or lifecycle._flag_values(args, flag) != [value]:
            raise fail("wrong retained game or role argument")
    for _, other, _ in owned:
        if other is runtime or other.poll() is not None:
            continue
        argv = other.args
        if not isinstance(argv, (list, tuple)) or any(not isinstance(a, str) for a in argv):
            raise fail("cannot exclude duplicate writer")
        for value in lifecycle._flag_values(argv, "--pid"):
            try:
                duplicate = int(value) == game_pid
            except (ValueError, TypeError):
                raise fail("invalid other owned game binding")
            if duplicate:
                raise fail("duplicate owned writer")
    handle = getattr(runtime, "_handle", None)
    if handle is None or int(handle) in (0, -1):
        raise fail("original handle absent")
    if api.process_id(handle) != runtime.pid or lifecycle._path(api.executable(handle)) != expected:
        raise fail("retained handle identity differs")
    birth = api.creation(handle)
    if type(birth) is not int or birth <= 0:
        raise fail("creation time absent")
    return handle, {"runtimePid": runtime.pid, "creationFiletime": birth,
                    "executable": expected, "gamePid": game_pid, "slot": 1,
                    "registryName": "runtime_1", "argv": list(args)}


def api_with_metadata(lifecycle):
    # Extend the existing retained-handle API with metadata queries only.
    class API(lifecycle.OwnedHandleAPI):
        def __init__(self):
            super().__init__()
            self._times = self._kernel.GetProcessTimes
            self._times.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_uint64)] * 4
            self._times.restype = ctypes.c_int32
            self._tick = self._kernel.GetTickCount64
            self._tick.argtypes = []
            self._tick.restype = ctypes.c_uint64

        def creation(self, handle):
            fields = [ctypes.c_uint64() for _ in range(4)]
            if not self._times(handle, *(ctypes.byref(v) for v in fields)):
                raise ctypes.WinError(ctypes.get_last_error())
            return fields[0].value

        def tick(self):
            return self._tick()
    return API()


def pause(lifecycle, processes, instances, runtime, expected, check_all, *, api=None,
          clock=time.perf_counter, sleep=time.sleep, timer_factory=threading.Timer):
    receipt = {"kind": "friend_heartbeat_expiry", "requestedPauseMs": PAUSE_MS,
               "watchdogMs": WATCHDOG_MS, "budget": budget(), "suspendAttempted": False, "suspendStatus": None,
               "resumed": False, "completed": False, "terminal": False,
               "resumeAttempts": [], "cleanup": [], "watchdogFired": False}
    lock = threading.RLock()
    timer = None
    recovered = False
    handle = None
    primary = None
    api = api or api_with_metadata(lifecycle)

    def recover(origin):
        nonlocal recovered
        with lock:
            if recovered or not receipt["suspendAttempted"]:
                return
            if origin == "watchdog":
                receipt["watchdogFired"] = True
            for attempt in range(3):
                row = {"origin": origin, "attempt": attempt + 1}
                receipt["resumeAttempts"].append(row)
                try:
                    row["status"] = lifecycle._status(api.resume(handle))
                    if row["status"] >= 0:
                        recovered = True
                        receipt["resumed"] = True
                        try:
                            receipt.update(resumeQpcSeconds=clock(), resumeTickMs=api.tick())
                        except BaseException as error:
                            receipt["terminal"] = True
                            row["metadataError"] = f"{type(error).__name__}: {error}"
                        return
                except BaseException as error:
                    row["error"] = f"{type(error).__name__}: {error}"
            receipt["terminal"] = True
            # As in runtime_lifecycle: terminal resume failure retires only the
            # same retained Popen. No replacement handle, process scan or game kill.
            if getattr(runtime, "_handle", None) is handle:
                try:
                    if runtime.poll() is None:
                        runtime.terminate()
                    receipt["cleanup"].append({"returncode": runtime.wait(timeout=2)})
                except BaseException as error:
                    receipt["cleanup"].append({"error": f"{type(error).__name__}: {error}"})
            recovered = True

    try:
        handle, identity = validate(lifecycle, processes, instances, runtime, expected, api)
        receipt["identity"] = identity
        check_all()
        if runtime.poll() is not None or runtime._handle is not handle:
            raise lifecycle.RuntimePauseError("runtime changed before pause")
        receipt.update(suspendQpcSeconds=clock(), suspendTickMs=api.tick())
        timer = timer_factory(WATCHDOG_MS / 1000, lambda: recover("watchdog"))
        timer.daemon = True
        with lock:
            # Arm under the same guard as attempt publication and suspend.
            # A timer that fires during scheduling preemption waits until the
            # attempt is known, rather than consuming its one shot too early.
            timer.start()
            receipt["suspendAttempted"] = True
            receipt["suspendStatus"] = lifecycle._status(api.suspend(handle))
            if receipt["suspendStatus"] < 0:
                raise lifecycle.RuntimePauseError("native suspend failed")
            receipt["suspendReturnedQpcSeconds"] = clock()
        until = receipt["suspendReturnedQpcSeconds"] + PAUSE_MS / 1000
        while clock() < until and not recovered:
            check_all()
            sleep(min(0.025, max(0, until - clock())))
        recover("elapsed")
        if receipt["terminal"] or not receipt["resumed"] or receipt["watchdogFired"]:
            raise lifecycle.RuntimePauseError("pause overrun or terminal resume failure")
        receipt["durationMs"] = 1000 * (receipt["resumeQpcSeconds"] - receipt["suspendQpcSeconds"])
        receipt["confirmedPauseMs"] = 1000 * (receipt["resumeQpcSeconds"] - receipt["suspendReturnedQpcSeconds"])
        if not PAUSE_MS <= receipt["confirmedPauseMs"] or receipt["durationMs"] > PAUSE_MS + OVERRUN_BUDGET_MS:
            raise lifecycle.RuntimePauseError("pause outside qualified duration (budget)")
        checked_handle, after = validate(lifecycle, processes, instances, runtime, expected, api)
        if checked_handle is not handle or after != identity:
            raise lifecycle.RuntimePauseError("retained runtime identity changed")
        check_all()
        receipt["completed"] = True
    except BaseException as error:
        primary = error
    finally:
        recover("finally")
        if timer is not None:
            timer.cancel()
            timer.join(timeout=4)
            if timer.is_alive() and primary is None:
                primary = lifecycle.RuntimePauseError("resume watchdog did not retire")
        if "resumeQpcSeconds" in receipt and "durationMs" not in receipt:
            receipt["durationMs"] = 1000 * (receipt["resumeQpcSeconds"] - receipt["suspendQpcSeconds"])
    if primary is not None:
        receipt["completed"] = False
        receipt["error"] = f"{type(primary).__name__}: {primary}"
        primary.receipt = receipt
        raise primary
    return receipt
