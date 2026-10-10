"""Actual Windows/PS5/no-console controls; no network, KH2 or build.

For SSH, copy this whole package to a fresh owned directory and run this same
entrypoint once there. Every child is owned, bounded and reaped.
"""
import argparse,ctypes,hashlib,json,os,subprocess,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent))
from winplumb.text import read_text,read_json,write_json,encoded_command,checked_text,powershell_argv
from winplumb.atomic import replacement_script
from winplumb.desktop import overlay_off_argv,worker_argv

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--out',type=Path,required=True);ap.add_argument('--packet',type=Path);a=ap.parse_args()
 root=a.out.resolve();root.mkdir(parents=True,exist_ok=False)
 module=Path(__file__).resolve().parent;rows=[]
 def record(name,**fields):rows.append(dict(name=name,**fields))
 def q(value):return "'"+str(value).replace("'","''")+"'"
 def ps(name,code,flags=subprocess.CREATE_NO_WINDOW):
  start=time.perf_counter();si=subprocess.STARTUPINFO();si.dwFlags=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
  with (root/(name+'.stdout.log')).open('xb') as out,(root/(name+'.stderr.log')).open('xb') as err:
   proc=subprocess.Popen(powershell_argv('-EncodedCommand',encoded_command(code)),stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=flags,startupinfo=si)
   try:rc=proc.wait(timeout=12)
   finally:
    if proc.poll() is None:proc.terminate();proc.wait(timeout=2)
  record(name,exit=rc,closed=True,pid=proc.pid,elapsedSeconds=time.perf_counter()-start)
  return rc,(root/(name+'.stdout.log')).read_bytes()
 # BOM-tolerant Python parameter/JSON readers and BOM-free Unicode output.
 payload={'message':'日本語 ✓ é'}
 (root/'bom.json').write_bytes(b'\xef\xbb\xbf'+json.dumps(payload,ensure_ascii=False).encode())
 assert read_json(root/'bom.json')==payload
 write_json(root/'plain.json',payload);assert not (root/'plain.json').read_bytes().startswith(b'\xef\xbb\xbf')
 for bad in [b'\xef\xbb\xbf\xef\xbb\xbfparam()', 'ï»¿param()', '‹¯¨param()']:
  try:checked_text(bad)
  except ValueError:pass
  else:raise AssertionError('malformed BOM accepted')
 record('python-utf8-bom-json',passed=True)
 (root/'parameter.ps1').write_bytes(b'\xef\xbb\xbf'+"param([string]$Word);@{word=$Word}|ConvertTo-Json -Compress".encode())
 profile="$ErrorActionPreference='Stop';. "+q(module/'utf8.ps1')+';'
 for name,flags in [('ps5-no-console',subprocess.CREATE_NO_WINDOW),('ps5-console',subprocess.CREATE_NEW_CONSOLE)]:
  code=profile+"$p=Read-WinPlumbJson "+q(root/'bom.json')+";Write-WinPlumbText "+q(root/(name+'.json'))+" ($p|ConvertTo-Json -Compress);& ([scriptblock]::Create((Read-WinPlumbText "+q(root/'parameter.ps1')+"))) -Word '日本語';@{ps=$PSVersionTable.PSVersion.Major;input=[Console]::InputEncoding.CodePage;output=[Console]::OutputEncoding.CodePage;preamble=[Console]::OutputEncoding.GetPreamble().Length;console=[WinPlumbConsole]::GetConsoleCP();py=$env:PYTHONUTF8;unbuffered=$env:PYTHONUNBUFFERED}|ConvertTo-Json -Compress"
  rc,data=ps(name,code,flags);assert rc==0,data
  parsed=[json.loads(line) for line in data.decode('utf-8-sig').splitlines() if line.startswith('{')]
  assert parsed[0]['word']=='日本語' and parsed[-1]['ps']==5 and parsed[-1]['input']==parsed[-1]['output']==65001 and parsed[-1]['preamble']==0 and parsed[-1]['py']==parsed[-1]['unbuffered']=='1'
  if flags==subprocess.CREATE_NEW_CONSOLE:assert parsed[-1]['console']==65001
  assert read_json(root/(name+'.json'))==payload and not (root/(name+'.json')).read_bytes().startswith(b'\xef\xbb\xbf')
 # Restrictive inherited process policy must not block a module child entry.
 child=profile+"if((Get-ExecutionPolicy) -ne 'Bypass'){throw 'child policy'};Write-Output 'MODULE_POLICY_OK'"
 child_command='& '+ ' '.join(q(v) for v in powershell_argv('-EncodedCommand',encoded_command(child)))
 restrictive="Set-ExecutionPolicy -Scope Process -ExecutionPolicy Restricted -Force;$ErrorActionPreference='Stop';if((Get-ExecutionPolicy) -ne 'Restricted'){throw 'parent policy'};"+child_command+";if($LASTEXITCODE -ne 0){throw 'child failed'};if((Get-ExecutionPolicy) -ne 'Restricted'){throw 'parent policy changed'}"
 rc,data=ps('restricted-parent-module-child',restrictive);assert rc==0 and b'MODULE_POLICY_OK' in data
 for index,bad in enumerate([b'\xef\xbb\xbf\xef\xbb\xbfparam()', 'ï»¿param()'.encode(), '‹¯¨param()'.encode()]):
  file=root/('bad-bom-'+str(index)+'.ps1');file.write_bytes(bad)
  assert ps('ps5-bom-refusal-'+str(index),profile+'Read-WinPlumbText '+q(file))[0]!=0
 # Actual Move then Replace; unique predecessor retained, collisions refused.
 target=root/'heartbeat.json';src=root/'first.part';src.write_bytes(b'first')
 assert ps('publish-first',replacement_script(str(src),str(target)))[0]==0 and target.read_bytes()==b'first'
 src=root/'second.part';src.write_bytes(b'second');backup=root/'predecessor.json'
 assert ps('publish-repeat',replacement_script(str(src),str(target),str(backup)))[0]==0 and target.read_bytes()==b'second' and backup.read_bytes()==b'first'
 src=root/'collision.part';src.write_bytes(b'third')
 assert ps('publish-collision',replacement_script(str(src),str(target),str(backup)))[0]!=0 and src.read_bytes()==b'third' and target.read_bytes()==b'second' and backup.read_bytes()==b'first'
 assert ps('publish-missing-source',replacement_script(str(root/'missing.part'),str(target)))[0]!=0 and target.read_bytes()==b'second'
 # Genuine ERROR_SHARING_VIOLATION: release only after actual retry observation.
 k=ctypes.WinDLL('kernel32',use_last_error=True)
 k.CreateFileW.argtypes=[ctypes.c_wchar_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_void_p];k.CreateFileW.restype=ctypes.c_void_p;k.CloseHandle.argtypes=[ctypes.c_void_p]
 for name,release in [('sharing-recovery',True),('sharing-timeout',False)]:
  src=root/(name+'.part');src.write_bytes(b'shared');journal=root/(name+'.retry.log')
  handle=k.CreateFileW(str(src),0x80000000,3,None,3,0x80,None);assert handle not in (None,ctypes.c_void_p(-1).value)
  proc=None
  try:
   code=replacement_script(str(src),str(target),retry_journal=str(journal))
   with (root/(name+'.stdout.log')).open('xb') as out,(root/(name+'.stderr.log')).open('xb') as err:
    proc=subprocess.Popen(powershell_argv('-EncodedCommand',encoded_command(code)),stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
    end=time.perf_counter()+5
    while not journal.exists() and proc.poll() is None and time.perf_counter()<end:time.sleep(.005)
    assert journal.exists(),'real sharing retry not observed'
    if release:assert k.CloseHandle(handle);handle=None
    rc=proc.wait(timeout=5)
   codes=[int(x) for x in journal.read_text(encoding='utf-8').splitlines()];assert codes and set(codes)<={32,33}
   assert (rc==0)==release
   if release:assert target.read_bytes()==b'shared' and not src.exists()
   else:assert src.read_bytes()==b'shared'
   record(name,exit=rc,closed=True,pid=proc.pid,retryCodes=codes)
  finally:
   if handle:k.CloseHandle(handle)
   if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=2)
 # No-console actual invalid handles, Pythonw None stdio. No pipe substitute.
 pythonw=Path(sys.executable).with_name('pythonw.exe');assert pythonw.is_file()
 for name,exe,mode in [('pythonw-none',pythonw,'none'),('python-invalid',Path(sys.executable),'invalid')]:
  dest=root/name;dest.mkdir();si=subprocess.STARTUPINFO();si.dwFlags=subprocess.STARTF_USESTDHANDLES|subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0;si.hStdInput=si.hStdOutput=si.hStdError=0
  proc=subprocess.Popen([str(exe),'-u','-B',str(module/'control_worker.py'),str(dest),mode],startupinfo=si,creationflags=subprocess.CREATE_NO_WINDOW)
  try:rc=proc.wait(timeout=12)
  finally:
   if proc.poll() is None:proc.terminate();proc.wait(timeout=2)
  raw=(dest/'worker.raw.log').read_bytes();assert rc==1 and all(marker in raw for marker in [b'WAITING_AFTER_FILE',b'FILE_HANDLER_ONLY',b'CHILD_STDERR_FULL','UNICODE_✓'.encode(),b'RuntimeError: FINAL_TRACEBACK_MARKER']) and b'lost sys.stderr' not in raw
  assert read_json(dest/'waiting.json')=={'pid':proc.pid,'recovered':True}
  record(name,pid=proc.pid,closed=True,exit=rc,sha256=hashlib.sha256(raw).hexdigest())
 # Routing and interpreter binding perform no game/process operation.
 assert overlay_off_argv(root,123)==[str(root/'bin/kh2ctl.exe'),'overlay','off','--pid','123']
 hashed=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
 assert worker_argv(Path(sys.executable),module/'control_worker.py',python_sha256=hashed(Path(sys.executable)),sha256=hashed)[0]==sys.executable
 try:worker_argv(Path(sys.executable),module/'control_worker.py',python_sha256='0'*64,sha256=hashed)
 except ValueError:pass
 else:raise AssertionError('wrong interpreter hash admitted')
 record('desktop-overlay-command-binding',passed=True)
 # Real retained child must be reaped even before its first identity/journal.
 from winplumb import bundled
 from unittest.mock import patch
 original_popen=subprocess.Popen;original_write=Path.write_text
 for failure in ('identity','journal'):
  dest=root/('import-failure-'+failure);dest.mkdir();created=[]
  def tracked(*args,**kw):
   proc=original_popen(*args,**kw);created.append(proc);return proc
  def broken_write(self,*args,**kw):
   if self.name=='import-owned.json':raise OSError('injected journal refusal')
   return original_write(self,*args,**kw)
  try:
   with patch.object(bundled.subprocess,'Popen',tracked):
    if failure=='identity':
     with patch.object(bundled,'_process_identity',side_effect=OSError('injected identity refusal')):
      bundled.run_import([sys.executable,'-c','import time;time.sleep(15)'],dest)
    else:
     with patch.object(Path,'write_text',broken_write):
      bundled.run_import([sys.executable,'-c','import time;time.sleep(15)'],dest)
  except OSError:pass
  else:raise AssertionError('injected admission failure not raised')
  assert len(created)==1 and created[0].poll() is not None
  record('import-'+failure+'-failure-cleanup',pid=created[0].pid,closed=True,exit=created[0].returncode)
 if a.packet:
  # Exact real bundled runtime/Pillow+PS5 desktop path, not a mocked Python.
  from winplumb.bundled import prepare
  packet=a.packet.resolve();runtime=root/'worker-runtime'
  admission=prepare(packet,runtime)
  worker=root/'desktop-worker.py'
  worker.write_text("import os,sys\nfrom pathlib import Path\nsys.path.insert(0,"+repr(str(module.parent))+ ")\nfrom winplumb.bootstrap import bootstrap,configure_logging\nroot=Path(os.environ['PC2_OPERATOR_LOG_DIR']);root.mkdir(exist_ok=False)\nlog,invalid=bootstrap(root/'worker.log');configure_logging(root/'worker.log')\nimport json,logging\nsys.path.insert(0,"+repr(str(packet/'dependencies'))+")\nimport PIL.Image,PIL._imaging\nassert sys.version_info[:2]==(3,11)\nassert PIL.Image.new('RGB',(1,1),(1,2,3)).getpixel((0,0))==(1,2,3)\nlogging.info('BUNDLED_DESKTOP_OK')\n(root/'worker.json').write_text(json.dumps({'pid':os.getpid(),'version':list(sys.version_info[:3]),'executable':sys.executable}),encoding='utf-8')\n",encoding='utf-8')
  argv=powershell_argv('-File',module/'desktop.ps1','-Worker',worker,'-OperatorLogDir',root/'desktop-operator','-RuntimeRoot',runtime,'-Packet',packet)
  with (root/'desktop.stdout.log').open('xb') as out,(root/'desktop.stderr.log').open('xb') as err:
   proc=subprocess.Popen(argv,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
   try:rc=proc.wait(timeout=25)
   finally:
    if proc.poll() is None:proc.terminate();proc.wait(timeout=2)
  assert rc==0,(root/'desktop.stderr.log').read_bytes()
  wr=read_json(root/'desktop-operator/worker.json');assert wr['version'][:2]==[3,11] and Path(wr['executable']).resolve()==Path(admission['python']).resolve()
  assert b'BUNDLED_DESKTOP_OK' in (root/'desktop-operator/worker.log').read_bytes()
  record('actual-bundled311-Pillow-PS5-desktop',passed=True,entryPid=proc.pid,closed=True,workerPid=wr['pid'])
 result={'status':'PASS','controls':rows,'count':len(rows),'allChildrenExited':True,'gameLaunch':False,'network':False}
 write_json(root/'result.json',result);print(json.dumps({'status':'PASS','count':len(rows),'allChildrenExited':True}));return 0

if __name__=='__main__':raise SystemExit(main())
