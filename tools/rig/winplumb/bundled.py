"""No-game worker runtime admission; exact ZIP Python group, no install/PATH fallback."""
import argparse,ctypes,hashlib,json,os,subprocess,sys,zipfile
from pathlib import Path,PurePosixPath

PYTHON_SHA='5f7b89a612c9b8af1d6456cdfcd1dbe5ca630849e79aebced9bee9a6694952ec'
DLL_SHA='0817a2a657a24c0d5fbb60df56960f42fc66b3039d522ec952dab83e2d869364'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def _process_identity(proc,argv):
    k=ctypes.WinDLL('kernel32',use_last_error=True)
    class FT(ctypes.Structure):_fields_=[('lo',ctypes.c_uint32),('hi',ctypes.c_uint32)]
    k.GetProcessTimes.argtypes=[ctypes.c_void_p]+[ctypes.POINTER(FT)]*4
    ts=[FT() for _ in range(4)]
    if not k.GetProcessTimes(proc._handle,*[ctypes.byref(v) for v in ts]):
        raise ctypes.WinError(ctypes.get_last_error())
    return dict(pid=proc.pid,creationTicks=ts[0].hi<<32|ts[0].lo,image=argv[0],argv=argv)

def run_import(argv,root):
    with (root/'import.stdout.log').open('xb') as out,(root/'import.stderr.log').open('xb') as err:
        proc=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=out,stderr=err,
            env={**os.environ,'PYTHONUTF8':'1','PYTHONUNBUFFERED':'1'},creationflags=subprocess.CREATE_NO_WINDOW)
        # Every operation after creation is inside the retained-handle cleanup
        # scope, including identity lookup and the first journal write.
        try:
            identity=_process_identity(proc,argv)
            (root/'import-owned.json').write_text(json.dumps(identity,indent=2),encoding='utf-8')
            rc=proc.wait(timeout=20)
        finally:
            if proc.poll() is None:proc.terminate();proc.wait(timeout=2)
    return identity,rc
def prepare(packet,root):
    packet=packet.resolve();root=root.resolve()
    assert root.is_absolute() and not root.is_relative_to(packet),'runtime must be outside packet'
    pins=json.loads((packet/'pins.json').read_text(encoding='utf-8-sig'))
    expected={r['path']:r['sha256'] for r in pins['files']}
    assert len(expected)==len(pins['files'])
    archive=packet/'KH2-Co-op.zip'
    assert sha(archive)==expected['KH2-Co-op.zip']==pins['packageSha256']
    root.mkdir(parents=True,exist_ok=False)
    group={}
    with zipfile.ZipFile(archive) as z:
        assert len(z.namelist())==len(set(z.namelist()))
        manifest=json.loads(z.read('KH2-Co-op/package.json').decode('utf-8-sig'))['files']
        group={n:v['sha256'] if isinstance(v,dict) else v for n,v in manifest.items() if n.startswith('python/')}
        assert group['python/python.exe']==PYTHON_SHA and group['python/python311.dll']==DLL_SHA
        actual={n.removeprefix('KH2-Co-op/') for n in z.namelist() if n.startswith('KH2-Co-op/python/') and not n.endswith('/')}
        assert actual==set(group),'complete Python group required'
        for name,h in group.items():
            rel=PurePosixPath(name);assert not rel.is_absolute() and '..' not in rel.parts and ':' not in name and '\\' not in name
            data=z.read('KH2-Co-op/'+name);assert hashlib.sha256(data).hexdigest()==h,name
            out=root/Path(*rel.parts);out.parent.mkdir(parents=True,exist_ok=True)
            with out.open('xb') as f:f.write(data)
    assert all(sha(root/n)==h for n,h in group.items())
    # Prove the exact ABI-dependent module selected by driver.setup(), using
    # the newly extracted interpreter and complete independently pinned deps.
    deps=packet/'dependencies'
    dependency_files={n:h for n,h in expected.items() if n.startswith('dependencies/')}
    assert dependency_files and all(sha(packet/n)==h for n,h in dependency_files.items())
    code="import sys,json;sys.path.insert(0,sys.argv[1]);import PIL.Image,PIL._imaging;assert sys.version_info[:2]==(3,11);print(json.dumps({'version':list(sys.version_info[:3]),'executable':sys.executable,'pillow':PIL.Image.__version__,'imaging':PIL._imaging.__file__}))"
    argv=[str(root/'python/python.exe'),'-u','-B','-c',code,str(deps)]
    identity,rc=run_import(argv,root)
    assert rc==0,(root/'import.stderr.log').read_text(encoding='utf-8',errors='replace')
    check=json.loads((root/'import.stdout.log').read_text(encoding='utf-8-sig'))
    assert Path(check['imaging']).resolve()==(deps/'PIL/_imaging.cp311-win_amd64.pyd').resolve()
    result=dict(ok=True,python=str(root/'python/python.exe'),pythonSha256=PYTHON_SHA,
        zipSha256=sha(archive),packetPinsSha256=sha(packet/'pins.json'),group=group,
        dependencyFiles=dependency_files,importControl=check,importIdentity=identity,importExited=True)
    (root/'runtime-admission.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('--packet',type=Path,required=True);ap.add_argument('--root',type=Path,required=True);a=ap.parse_args()
    r=prepare(a.packet,a.root);print(json.dumps({k:r[k] for k in ('ok','pythonSha256','packetPinsSha256','importExited')}))
