"""Windows handles for only this launcher's selected game and owned helpers.

No API is called at import. The game handle is QUERY|SYNCHRONIZE, never terminate.
Each helper has a private hidden console and belongs to a kill-on-close job.
"""
import ctypes as c
from ctypes import wintypes as w
import os
import subprocess
import time
import math
import weakref


class BasicLimit(c.Structure):
    _fields_ = [('process_time', c.c_longlong), ('job_time', c.c_longlong), ('flags', w.DWORD),
                ('min_ws', c.c_size_t), ('max_ws', c.c_size_t), ('active_limit', w.DWORD),
                ('affinity', c.c_size_t), ('priority', w.DWORD), ('scheduling', w.DWORD)]


class IoCounters(c.Structure):
    _fields_ = [(name, c.c_ulonglong) for name in ('read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]


class ExtendedLimit(c.Structure):
    _fields_ = [('basic', BasicLimit), ('io', IoCounters), ('process_memory', c.c_size_t),
                ('job_memory', c.c_size_t), ('peak_process', c.c_size_t), ('peak_job', c.c_size_t)]


class StartupInfo(c.Structure):
    _fields_ = [('cb', w.DWORD), ('reserved', w.LPWSTR), ('desktop', w.LPWSTR), ('title', w.LPWSTR),
                *[(name, w.DWORD) for name in ('x', 'y', 'x_size', 'y_size', 'x_chars', 'y_chars', 'fill', 'flags')],
                ('show', w.WORD), ('reserved_size', w.WORD), ('reserved_bytes', c.POINTER(w.BYTE)),
                ('stdin', w.HANDLE), ('stdout', w.HANDLE), ('stderr', w.HANDLE)]


class StartupInfoEx(c.Structure):
    _fields_ = [('startup', StartupInfo), ('attributes', c.c_void_p)]


class ProcessInfo(c.Structure):
    _fields_ = [('process', w.HANDLE), ('thread', w.HANDLE), ('pid', w.DWORD), ('tid', w.DWORD)]


class OwnedProcess:
    """The small Popen-compatible surface used by Session, with a retained handle."""
    def __init__(self, win, handle, pid, argv):
        self.win, self._handle, self.pid, self.args = win, handle, pid, argv
        self.returncode = None
        self._finalizer = weakref.finalize(self, win.close, handle)

    def poll(self):
        if self.returncode is None:
            result = self.win.k.WaitForSingleObject(self._handle, 0)
            if result == 0:
                code = w.DWORD()
                if not self.win.k.GetExitCodeProcess(self._handle, c.byref(code)):
                    raise c.WinError(c.get_last_error())
                self.returncode = code.value
            elif result != 258:
                raise c.WinError(c.get_last_error())
        return self.returncode

    def wait(self, timeout=None):
        if self.poll() is None:
            milliseconds = 0xFFFFFFFF if timeout is None else min(0xFFFFFFFE, max(0, math.ceil(timeout * 1000)))
            result = self.win.k.WaitForSingleObject(self._handle, milliseconds)
            if result == 258:
                raise subprocess.TimeoutExpired(self.args, timeout)
            if result != 0:
                raise c.WinError(c.get_last_error())
        return self.poll()

    def terminate(self):
        if self.poll() is None and not self.win.k.TerminateProcess(self._handle, 1):
            error = c.get_last_error()
            if self.poll() is None:
                raise c.WinError(error)

    kill = terminate


class Windows:
    def __init__(self):
        self.k = c.WinDLL('kernel32', use_last_error=True)
        signatures = {
            'OpenProcess': ([w.DWORD, w.BOOL, w.DWORD], w.HANDLE),
            'CloseHandle': ([w.HANDLE], w.BOOL),
            'WaitForSingleObject': ([w.HANDLE, w.DWORD], w.DWORD),
            'QueryFullProcessImageNameW': ([w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)], w.BOOL),
            'ProcessIdToSessionId': ([w.DWORD, c.POINTER(w.DWORD)], w.BOOL),
            'CreateJobObjectW': ([c.c_void_p, w.LPCWSTR], w.HANDLE),
            'SetInformationJobObject': ([w.HANDLE, c.c_int, c.c_void_p, w.DWORD], w.BOOL),
            'GetCurrentProcess': ([], w.HANDLE),
            'GetHandleInformation': ([w.HANDLE, c.POINTER(w.DWORD)], w.BOOL),
            'DuplicateHandle': ([w.HANDLE, w.HANDLE, w.HANDLE, c.POINTER(w.HANDLE), w.DWORD, w.BOOL, w.DWORD], w.BOOL),
            'InitializeProcThreadAttributeList': ([c.c_void_p, w.DWORD, w.DWORD, c.POINTER(c.c_size_t)], w.BOOL),
            'UpdateProcThreadAttribute': ([c.c_void_p, w.DWORD, c.c_size_t, c.c_void_p, c.c_size_t, c.c_void_p, c.c_void_p], w.BOOL),
            'DeleteProcThreadAttributeList': ([c.c_void_p], None),
            'CreateProcessW': ([w.LPCWSTR, w.LPWSTR, c.c_void_p, c.c_void_p, w.BOOL, w.DWORD,
                                c.c_void_p, w.LPCWSTR, c.POINTER(StartupInfoEx), c.POINTER(ProcessInfo)], w.BOOL),
            'GetExitCodeProcess': ([w.HANDLE, c.POINTER(w.DWORD)], w.BOOL),
            'TerminateProcess': ([w.HANDLE, w.UINT], w.BOOL),
            'IsProcessInJob': ([w.HANDLE, w.HANDLE, c.POINTER(w.BOOL)], w.BOOL),
            'CreateMutexW': ([c.c_void_p, w.BOOL, w.LPCWSTR], w.HANDLE),
            'OpenFileMappingW': ([w.DWORD, w.BOOL, w.LPCWSTR], w.HANDLE),
            'FreeConsole': ([], w.BOOL), 'AttachConsole': ([w.DWORD], w.BOOL),
            'GetConsoleWindow': ([], w.HWND),
            'GetConsoleProcessList': ([c.POINTER(w.DWORD), w.DWORD], w.DWORD),
            'SetConsoleCtrlHandler': ([c.c_void_p, w.BOOL], w.BOOL),
            'GenerateConsoleCtrlEvent': ([w.DWORD, w.DWORD], w.BOOL),
        }
        for name, (args, result) in signatures.items():
            fn = getattr(self.k, name); fn.argtypes = args; fn.restype = result

    def session(self):
        value = w.DWORD()
        if not self.k.ProcessIdToSessionId(os.getpid(), c.byref(value)):
            raise c.WinError(c.get_last_error())
        return value.value

    def open_query(self, pid):
        handle = self.k.OpenProcess(0x100000 | 0x1000, False, pid)  # SYNCHRONIZE | QUERY_LIMITED_INFORMATION
        if not handle:
            raise c.WinError(c.get_last_error())
        return handle

    def alive(self, handle):
        return self.k.WaitForSingleObject(handle, 0) == 258

    def close(self, handle):
        if handle:
            self.k.CloseHandle(handle)

    def selected_game(self, pid):
        handle = self.open_query(pid)
        try:
            name = c.create_unicode_buffer(32768); size = w.DWORD(len(name))
            if not self.k.QueryFullProcessImageNameW(handle, 0, name, c.byref(size)):
                raise c.WinError(c.get_last_error())
            if os.path.basename(name.value).lower() != 'kingdom hearts ii final mix.exe' or not self.alive(handle):
                raise ValueError('Selected PID is not a running KH2 game.')
            mapping = self.k.OpenFileMappingW(4, False, f'Local\\kh2coop_avatar_{pid}')
            if not mapping:
                raise ValueError('Game has no AvatarBridge. Prepare/inject it through kh2ctl first.')
            self.close(mapping)
            return handle
        except Exception:
            self.close(handle)
            raise

    def mutex(self, pid):
        handle = self.k.CreateMutexW(None, False, f'Local\\kh2coop_dev_launcher_{pid}')
        error = c.get_last_error()
        if not handle or error == 183:
            self.close(handle)
            raise ValueError('Another launcher already owns a runtime for this PID.')
        return handle

    def job(self):
        handle = self.k.CreateJobObjectW(None, None)
        if not handle:
            raise c.WinError(c.get_last_error())
        limit = ExtendedLimit(); limit.basic.flags = 0x2000  # KILL_ON_JOB_CLOSE
        if not self.k.SetInformationJobObject(handle, 9, c.byref(limit), c.sizeof(limit)):
            self.close(handle)
            raise c.WinError(c.get_last_error())
        return handle

    def spawn(self, job, argv, cwd, output):
        import msvcrt
        argv = [os.fspath(arg) for arg in argv]
        if not argv or not os.path.isabs(argv[0]) or any('\0' in arg for arg in argv):
            raise ValueError('Helper requires an absolute executable and NUL-free argv.')
        if not job:
            raise ValueError('An owned job is required before helper creation.')
        flags = w.DWORD()
        if not self.k.GetHandleInformation(job, c.byref(flags)):
            raise c.WinError(c.get_last_error())
        if flags.value & 1:
            raise ValueError('The kill-on-close job handle must not be inheritable.')
        size = c.c_size_t()
        if self.k.InitializeProcThreadAttributeList(None, 2, 0, c.byref(size)) or c.get_last_error() != 122:
            raise c.WinError(c.get_last_error())
        storage = c.create_string_buffer(size.value)
        if not self.k.InitializeProcThreadAttributeList(storage, 2, 0, c.byref(size)):
            raise c.WinError(c.get_last_error())
        inherited = []
        pi = ProcessInfo()
        try:
            # Only private duplicates of NUL/input and the log/output are
            # inherited. The job and returned process/thread handles are not.
            with open(os.devnull, 'rb') as null:
                current = self.k.GetCurrentProcess()
                for file in (null, output):
                    duplicate = w.HANDLE()
                    if not self.k.DuplicateHandle(current, msvcrt.get_osfhandle(file.fileno()),
                                                  current, c.byref(duplicate), 0, True, 2):
                        raise c.WinError(c.get_last_error())
                    inherited.append(duplicate.value)
            handles = (w.HANDLE * len(inherited))(*inherited)
            jobs = (w.HANDLE * 1)(job)
            # PROC_THREAD_ATTRIBUTE_HANDLE_LIST / JOB_LIST (Windows 10+).
            for key, value in ((0x00020002, handles), (0x0002000D, jobs)):
                if not self.k.UpdateProcThreadAttribute(storage, 0, key, c.cast(value, c.c_void_p),
                                                       c.sizeof(value), None, None):
                    raise c.WinError(c.get_last_error())
            startup = StartupInfoEx()
            startup.startup.cb = c.sizeof(startup)
            startup.startup.flags = 0x101  # USESHOWWINDOW | USESTDHANDLES
            startup.startup.show = 0  # private hidden console
            startup.startup.stdin, startup.startup.stdout, startup.startup.stderr = inherited[0], inherited[1], inherited[1]
            startup.attributes = c.cast(storage, c.c_void_p)
            command = c.create_unicode_buffer(subprocess.list2cmdline(argv))
            if not self.k.CreateProcessW(argv[0], command, None, None, True,
                                         0x00080010, None, os.fspath(cwd), c.byref(startup), c.byref(pi)):
                raise c.WinError(c.get_last_error())
            # JOB_LIST establishes membership before any child instruction.
            # Failure never retries without the attribute or assigns afterward.
            proc = OwnedProcess(self, pi.process, pi.pid, argv)
            pi.process = None  # ownership moved to proc's finalizer
            return proc
        finally:
            self.k.DeleteProcThreadAttributeList(storage)
            for handle in inherited:
                self.close(handle)
            self.close(pi.thread)
            if pi.process:
                self.k.TerminateProcess(pi.process, 1)  # only a just-created owned child on wrapping failure
                self.close(pi.process)

    def stop(self, proc):
        if proc.poll() is not None:
            return {'exitCode': proc.returncode, 'forced': False, 'signalSent': False}
        # This process created the target's PRIVATE console. Verify membership
        # before sending Ctrl+C; never signal a shared terminal or foreign PID.
        had_console = bool(self.k.GetConsoleWindow())
        self.k.FreeConsole()
        sent = False
        try:
            if self.k.AttachConsole(proc.pid):
                ids = (w.DWORD * 8)()
                count = self.k.GetConsoleProcessList(ids, 8)
                if 0 < count <= 8 and set(ids[:count]) <= {os.getpid(), proc.pid}:
                    if self.k.SetConsoleCtrlHandler(None, True):
                        sent = bool(self.k.GenerateConsoleCtrlEvent(0, 0))
                        time.sleep(0.05)
        finally:
            self.k.FreeConsole()
            self.k.SetConsoleCtrlHandler(None, False)
            if had_console:
                self.k.AttachConsole(0xFFFFFFFF)  # reattach only this launcher's parent console
        forced = False
        try:
            proc.wait(timeout=3 if sent else 0)
        except subprocess.TimeoutExpired:
            forced = True
            proc.terminate()  # retained creation handle; only our child
            proc.wait(timeout=3)
        return {'exitCode': proc.returncode, 'forced': forced, 'signalSent': sent}
