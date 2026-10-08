"""Windows offline control: hidden runner polls, child/console counts and reaping.

Uses only disposable Python helpers; never launches, attaches to, or queries KH2.
"""
import argparse
import ast
import ctypes as c
from ctypes import wintypes as w
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
from unittest.mock import patch


def snapshot():
    class Entry(c.Structure):
        _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD), ('heap', c.c_size_t),
                    ('module', w.DWORD), ('threads', w.DWORD), ('parent', w.DWORD),
                    ('priority', w.LONG), ('flags', w.DWORD), ('name', w.WCHAR * 260)]
    k = c.WinDLL('kernel32', use_last_error=True)
    k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]; k.CreateToolhelp32Snapshot.restype = w.HANDLE
    k.Process32FirstW.argtypes = k.Process32NextW.argtypes = [w.HANDLE, c.POINTER(Entry)]
    k.CloseHandle.argtypes = [w.HANDLE]
    handle = k.CreateToolhelp32Snapshot(2, 0)
    if handle == c.c_void_p(-1).value: raise c.WinError(c.get_last_error())
    rows = []
    try:
        entry = Entry(); entry.size = c.sizeof(entry)
        more = k.Process32FirstW(handle, c.byref(entry))
        while more:
            rows.append({'pid': entry.pid, 'parent': entry.parent, 'name': entry.name.lower()})
            more = k.Process32NextW(handle, c.byref(entry))
    finally: k.CloseHandle(handle)
    return rows


def alive(pid):
    k = c.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
    k.WaitForSingleObject.argtypes = [w.HANDLE, w.DWORD]; k.CloseHandle.argtypes = [w.HANDLE]
    handle = k.OpenProcess(0x100000, False, pid)
    if not handle: return False
    try: return k.WaitForSingleObject(handle, 0) == 258
    finally: k.CloseHandle(handle)


def run():
    if os.name != 'nt': raise RuntimeError('This control requires Windows process/console APIs.')
    spec = importlib.util.spec_from_file_location('console_runner', Path(__file__).with_name('run.py'))
    runner = importlib.util.module_from_spec(spec); sys.modules[spec.name] = runner; spec.loader.exec_module(runner)
    checks = []
    def check(name, value):
        checks.append({'name': name, 'ok': bool(value)})
        if not value: raise AssertionError(name)
    source = Path(__file__).with_name('run.py').read_text()
    calls = [node for node in ast.walk(ast.parse(source)) if isinstance(node, ast.Call)
             and isinstance(node.func, ast.Attribute) and isinstance(node.func.value, ast.Name)
             and node.func.value.id == 'subprocess' and node.func.attr in ('run', 'Popen', 'call', 'check_call', 'check_output')]
    check('every direct runner child spawn explicitly suppresses console allocation',
          len(calls) == 4 and all(any(kw.arg == 'creationflags' and 'CREATE_NO_WINDOW' in ast.dump(kw.value)
                                     for kw in call.keywords) for call in calls))
    flags = []
    def fake_run(*args, **kw):
        flags.append(kw.get('creationflags', 0)); return SimpleNamespace(stdout='{"ok":true}', stderr='')
    with patch.object(runner.subprocess, 'run', fake_run):
        runner.kh2ctl('peek', '--rva', '0:u64', pid=1)
        ctx = runner.Context(Path('.')); ctx.instances = [SimpleNamespace(pid=1)]
        try: ctx.namespace()['bridge']()
        finally: ctx.close()
    check('canonical kh2ctl and avatar polls explicitly set CREATE_NO_WINDOW', flags == [subprocess.CREATE_NO_WINDOW] * 2)
    runner.KH2CTL = Path(sys.executable)
    code = 'import ctypes,json,os;print(json.dumps({"ok":True,"pid":os.getpid(),"console":int(ctypes.windll.kernel32.GetConsoleWindow())}))'
    before = snapshot(); tracked = []
    for _ in range(12):
        sample = runner.kh2ctl('-c', code, timeout=5); tracked.append(sample['pid'])
        check('poll has no console and is reaped', sample['console'] == 0 and not alive(sample['pid']))
    with tempfile.TemporaryDirectory() as temp:
        receipt = Path(temp) / 'timeout.json'
        timeout_code = ('import ctypes,json,os,time;from pathlib import Path;'
                        'Path(' + repr(str(receipt)) + ').write_text(json.dumps({"pid":os.getpid(),"console":int(ctypes.windll.kernel32.GetConsoleWindow())}));time.sleep(30)')
        try: runner.kh2ctl('-c', timeout_code, timeout=1)
        except subprocess.TimeoutExpired: pass
        else: raise AssertionError('timeout child did not time out')
        sample = json.loads(receipt.read_text()); tracked.append(sample['pid'])
        check('timed-out poll is killed and reaped without a console', sample['console'] == 0 and not alive(sample['pid']))
        order = []
        class Process:
            def poll(self): return None
            def terminate(self): order.append('terminate')
            def wait(self, timeout=None):
                order.append('wait')
                if timeout is not None: raise subprocess.TimeoutExpired('owned fake helper', timeout)
                return 0
            def kill(self): order.append('kill')
        ctx = SimpleNamespace(_stop=threading.Event(), saved={}, processes=[('owned', Process(), SimpleNamespace(close=lambda: order.append('log-close')))])
        started = threading.Event()
        def active_poll():
            started.set(); result = runner.kh2ctl('-c', 'import time;' + code.replace('import ctypes,json,os;', 'import ctypes,json,os;time.sleep(0.2);'), timeout=5)
            tracked.append(result['pid']); order.append('poll-reaped')
        ctx._protector = threading.Thread(target=active_poll); ctx._protector.start(); started.wait()
        runner.Context.close(ctx)
        check('cleanup joins in-flight protector poll before owned helper teardown', not ctx._protector.is_alive() and order == ['poll-reaped', 'terminate', 'wait', 'kill', 'wait', 'log-close'])
    after = snapshot(); ids = set(tracked)
    owned_children = [x for x in after if x['pid'] in ids]
    owned_consoles = [x for x in after if x['name'] in ('conhost.exe', 'openconsole.exe') and x['parent'] in ids]
    check('no retained poll children or child console processes', not owned_children and not owned_consoles)
    return {'ok': True, 'checks': checks, 'pollChildren': len(tracked),
            'remainingOwnedChildren': len(owned_children), 'remainingOwnedConsoleProcesses': len(owned_consoles),
            'consoleProcessesBefore': sum(x['name'] in ('conhost.exe', 'openconsole.exe') for x in before),
            'consoleProcessesAfter': sum(x['name'] in ('conhost.exe', 'openconsole.exe') for x in after),
            'processesBefore': len(before), 'processesAfter': len(after),
            'scope': 'Python helper controls only; no game; global counts observational, owned leftovers fail'}


if __name__ == '__main__':
    ap = argparse.ArgumentParser(); ap.add_argument('--out', type=Path); args = ap.parse_args()
    result = run()
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True); args.out.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))
