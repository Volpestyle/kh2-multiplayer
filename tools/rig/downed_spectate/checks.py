"""Offline fixture verdict controls; no process, mapping or rig access."""
import importlib.util, pathlib, json
p=pathlib.Path(__file__).parent/'run.py';s=importlib.util.spec_from_file_location('spectate_fixture',p)
m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
c=dict(installed=1,active=1,slot=1,mode=0,actorDuring=200,actorBefore=100,actorAfter=100,
       localActor=100,episode=42,generation=3)
d=dict(episode=42,state='downed',hp=0,flags9B8=4,deadAction=1,controllerOff=1,gameOverTask=0)
checks=0
def check(x):
 global checks
 checks+=1
 if not x:raise AssertionError('fixture control '+str(checks))
check(m.ctypes.sizeof(m.Camera)==104)
check(m.judge_target(c,1,200,100,d))
for key,value in dict(installed=0,active=0,slot=0,mode=1,actorDuring=100,actorBefore=200,
                      actorAfter=200,localActor=200,episode=43,generation=0).items():
 check(not m.judge_target(dict(c,**{key:value}),1,200,100,d))
for key,value in dict(state='ready',hp=1,flags9B8=0,deadAction=0,controllerOff=0,gameOverTask=1,episode=43).items():
 check(not m.judge_target(c,1,200,100,dict(d,**{key:value})))
check(m.judge_target(dict(c,slot=0,actorDuring=100),0,100,100,d))
raw=bytearray(104);raw[8:12]=(2).to_bytes(4,'little');check(m.camera_snap(raw)['sequence']==2)
for seq in (0,1,3):
 raw[8:12]=seq.to_bytes(4,'little')
 try:m.camera_snap(raw)
 except RuntimeError:check(True)
 else:check(False)
from unittest.mock import patch
from types import SimpleNamespace
with patch.object(m.subprocess,'CREATE_NO_WINDOW',0x08000000,create=True), patch.object(m.subprocess,'run') as spawn:
 spawn.return_value=SimpleNamespace(returncode=0,stdout=json.dumps(dict(samples=[dict(ok=True)])))
 check(m.observe_avatar('observer.exe',42)==dict(ok=True))
 args,kw=spawn.call_args
 check(args[0]==['observer.exe','observe','--pid','42','--samples','1'])
 check(kw==dict(capture_output=True,text=True,timeout=5,creationflags=0x08000000))
 spawn.return_value=SimpleNamespace(returncode=1,stdout='')
 try:m.observe_avatar('observer.exe',42)
 except RuntimeError:check(True)
 else:check(False)
 class StepFailed(Exception):pass
 try:m.observe_avatar('observer.exe',42,StepFailed)
 except StepFailed:check(True)
 else:check(False)
for value in (100,'100','0x64','0X64'):
 check(m.pointer_value(value)==100)
for value in (-1,2**64,True,1.5,None,'','xyz','0xGG',' 0x64','1.0'):
 try:m.pointer_value(value)
 except ValueError:check(True)
 else:check(False)
# Reproduce the retained live failure: CLI u64 is a hex string of the local body.
body=0x7FF6930EC700;peer=0x7FF6930C1040;raw='0x7FF6930EC700'
check(raw not in (body,peer))
check(m.pointer_value(raw) in (body,peer))
check(m.pointer_value('0x7FF6930C1040') in (body,peer))
check(m.pointer_value('0x7FF693000000') not in (body,peer))
check(m.pointer_value(raw)==body) # release uses the same normalization
root=pathlib.Path('C:/Users/volpe/repos/kh2-multiplayer')
check(m.check_cli_root(b'C:/Users/volpe/repos/kh2-multiplayer\0',root)==str(root.resolve()))
for binary in (b'C:/Users/volpe/repos/kh2-multiplayer/.local/wt-spectate\0',b'no root',b'C:/Users/volpe/repos/kh2-multiplayer\0D:/kh2-multiplayer\0'):
 try:m.check_cli_root(binary,root)
 except RuntimeError:check(True)
 else:check(False)
print(json.dumps(dict(status='PASS',controls=checks)))
