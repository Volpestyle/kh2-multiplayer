"""Commands only. The caller uses its existing owned job/journal/STOP launcher."""
from pathlib import Path

def worker_argv(python,worker,*,python_sha256,sha256):
    python=Path(python);worker=Path(worker)
    if not python.is_absolute() or not worker.is_absolute():raise ValueError('explicit absolute interpreter and worker required')
    if not python.is_file() or not worker.is_file():raise ValueError('interpreter/worker missing')
    if sha256(python)!=python_sha256:raise ValueError('pinned interpreter mismatch')
    entry="import runpy,sys;from pathlib import Path;w=Path(sys.argv[1]).resolve();sys.path.insert(0,str(w.parent));sys.argv=[str(w)];runpy.run_path(str(w),run_name='__main__')"
    return [str(python),'-u','-B','-c',entry,str(worker)]

def overlay_off_argv(package,original_pid):
    # Internal diagnostic intentionally refuses overlay; use portable lifecycle.
    # This function never adopts/opens a PID. Caller must recheck its original
    # retained PID+creation/image handle and native ownership before dispatch.
    if type(original_pid) is not int or original_pid<=0:raise ValueError('original positive PID required')
    package=Path(package)
    if not package.is_absolute():raise ValueError('absolute package required')
    return [str(package/'bin/kh2ctl.exe'),'overlay','off','--pid',str(original_pid)]
