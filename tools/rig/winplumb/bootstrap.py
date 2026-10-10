"""Same-process UTF-8 logging; replace console-backed Python streams entirely."""
import ctypes
import io
import os
import sys
from pathlib import Path

_original_streams = None

def valid_stream(stream):
    if stream is None:
        return False
    try:
        fd = stream.fileno()
        os.fstat(fd)
        if os.name == 'nt':
            import msvcrt
            kernel = ctypes.WinDLL('kernel32', use_last_error=True)
            kernel.GetFileType.argtypes = [ctypes.c_void_p]
            kernel.GetFileType.restype = ctypes.c_ulong
            ctypes.set_last_error(0)
            kind = kernel.GetFileType(msvcrt.get_osfhandle(fd))
            if kind == 0 and ctypes.get_last_error():
                return False
        return True
    except (OSError, ValueError, AttributeError):
        return False

def bootstrap(path, *, packet_root=None):
    """Call before importing logging or worker modules; never use packet paths."""
    path = Path(path)
    if not path.is_absolute():
        raise ValueError('absolute owned operator-log path required')
    if packet_root is not None and path.resolve().is_relative_to(Path(packet_root).resolve()):
        raise ValueError('operator log must be outside packet')
    os.environ['PYTHONUNBUFFERED'] = '1'
    os.environ['PYTHONUTF8'] = '1'
    # Even valid desktop handles are redirected for complete operator evidence.
    # This covers the mandatory None/invalid-handle branch without a console write.
    invalid = not valid_stream(sys.stdout) or not valid_stream(sys.stderr)
    log = redirect(path)
    return log, invalid

def configure_logging(path):
    # Deliberately imported only AFTER bootstrap has replaced invalid stdio.
    import logging
    root = logging.getLogger()
    for handler in list(root.handlers):
        root.removeHandler(handler)
        handler.close()
    handler = logging.FileHandler(path, mode='a', encoding='utf-8', delay=True)
    # Use the owned file-backed stream's shared file position. A separately
    # opened Windows append handle can race/overlap raw inherited child writes.
    handler.stream = sys.stderr
    root.addHandler(handler)
    root.setLevel(logging.INFO)
    logging.lastResort = handler
    return handler

def child_stdio(log):
    import subprocess
    return dict(stdin=subprocess.DEVNULL, stdout=log, stderr=log)

def redirect(path):
    global _original_streams
    if _original_streams is not None:
        raise RuntimeError('worker logging already initialized')
    # Both raw fd writes and the separate FileHandler append atomically at EOF;
    # ordinary xb offsets could overwrite the handler's appended messages.
    fd = os.open(os.fspath(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_APPEND | getattr(os, 'O_BINARY', 0), 0o600)
    log = os.fdopen(fd, 'wb', buffering=0)
    # Keep console wrappers alive without using or flushing them again. Their
    # WindowsConsoleIO objects can retain invalid handles after dup2/FreeConsole.
    _original_streams = (sys.stdout, sys.stderr, sys.__stdout__, sys.__stderr__)
    os.dup2(log.fileno(), 1, inheritable=True)
    os.dup2(log.fileno(), 2, inheritable=True)
    if os.name == 'nt':
        import msvcrt
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.SetStdHandle.argtypes = [ctypes.c_ulong, ctypes.c_void_p]
        kernel.SetStdHandle.restype = ctypes.c_int
        for number, descriptor in ((-11, 1), (-12, 2)):
            if not kernel.SetStdHandle(number & 0xffffffff, msvcrt.get_osfhandle(descriptor)):
                raise ctypes.WinError(ctypes.get_last_error())
    def stream(descriptor):
        return io.TextIOWrapper(io.FileIO(os.dup(descriptor), 'wb', closefd=True),
                                encoding='utf-8', errors='backslashreplace',
                                line_buffering=True, write_through=True)
    sys.stdout = sys.__stdout__ = stream(1)
    sys.stderr = sys.__stderr__ = stream(2)
    return log
