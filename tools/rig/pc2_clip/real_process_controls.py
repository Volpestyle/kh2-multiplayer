"""Read-only native identity/absence controls on own Python processes, never KH2."""
import argparse,ctypes,json,os,subprocess,sys
from pathlib import Path
from .adapter import Native,write

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);a=parser.parse_args()
    native=Native();checks=[]
    for module in (False,True):
        h=native.open(os.getpid(),module=module)
        try:
            ident=native.identity(h)
            assert Path(ident['imagePath'])==Path(sys.executable).resolve() and ident['creationTicks']>0
            assert native.k.WaitForSingleObject(h,0)==258
            assert native.absent(dict(pid=os.getpid(),**ident)) is False
            if module:assert native.base(h)>0
            checks.append(dict(case='running self explicit module/limited rights',module=module,ok=True,identity=ident))
        finally:native.close(h)
    p=subprocess.Popen([sys.executable,'-c','import sys;sys.stdin.read()'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
    h=None
    try:
        h=native.open(p.pid);ident=dict(pid=p.pid,**native.identity(h))
        assert Path(ident['imagePath'])==Path(sys.executable).resolve() and native.absent(ident) is False
        assert native.identity(p._handle)['creationTicks']==ident['creationTicks']
        checks.append(dict(case='running original owned child refuses absence',ok=True,identity=ident))
        p.stdin.close();p.wait(timeout=5);assert p.returncode==0
        assert native.creation_ticks(h)==ident['creationTicks'] and native.k.WaitForSingleObject(h,0)==0
        # Hold BOTH original handles throughout. An image query on this exited
        # process is not needed, and may fail with ERROR_GEN_FAILURE31 on Windows.
        assert native.absent(ident) is True
        checks.append(dict(case='exited original child with retained handles is absent',ok=True,exitCode=p.returncode,identity=ident))
        original_image=native.identity
        def forbidden(*args):raise AssertionError('absence queried exited process image')
        native.identity=forbidden
        assert native.absent(ident) is True
        native.identity=original_image
        checks.append(dict(case='actual exited child absence never calls identity/image',ok=True))
    finally:
        if p.stdin and not p.stdin.closed:p.stdin.close()
        p.wait(timeout=5);native.close(h);p._handle.Close()
    write(a.output,dict(ok=True,tests=len(checks),checks=checks,nativeGameExecuted=False,pc2Contact=False,normalOwnedPythonChildExitOnly=True,queriesReadOnly=True))
    print(json.dumps(dict(ok=True,tests=len(checks))))
if __name__=='__main__':main()
