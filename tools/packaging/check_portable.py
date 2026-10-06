"""Offline portable CLI refusal checks. Never invokes launch, instances or kill."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile


def check(cli, output):
    rows=[]
    with tempfile.TemporaryDirectory(prefix='kh2-package-check-') as temp:
        root=Path(temp); (root/'bin').mkdir();exe=root/'bin/kh2ctl.exe';shutil.copyfile(cli,exe)
        def run(label,args,cwd,success,contains):
            r=subprocess.run([str(exe),*args],cwd=cwd,stdin=subprocess.DEVNULL,capture_output=True,
                             text=True,timeout=8,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            assert (r.returncode==0)==success,(label,r.stdout,r.stderr)
            assert contains in r.stdout+r.stderr,(label,r.stdout,r.stderr)
            assert not (root/'build').exists(),label+' wrote rig state'
            rows.append({'case':label,'argv':args,'exitCode':r.returncode,'stdout':r.stdout,'stderr':r.stderr})
        run('no-marker',['help'],root,False,'package folder')
        (root/'KH2COOP-PACKAGE').write_text('wrong\n');(root/'package.json').write_text('{}')
        run('wrong-marker',['help'],root,False,'package folder')
        (root/'KH2COOP-PACKAGE').write_text('kh2coop-friend-package-v1\n')
        run('wrong-cwd',['help'],root/'bin',False,'package folder')
        run('help',['help'],root,True,'Internal commands: launch, instances, kill, overlay on|off --pid N, help.')
        for command in ('restart','boot-load-save','inject','poke','player-input','world-resync','capture'):
            run('blocked-'+command,[command],root,False,'Command unavailable')
        for args in ([],['on'],['off'],['toggle','--pid','42'],['ON','--pid','42'],
                     ['--pid','42','on'],['on','--pid'],['on','--pid','42','extra'],
                     ['off','--pid','42','--pid','43']):
            run('overlay-shape-'+str(args),['overlay',*args],root,False,'Portable overlay takes')
        for pid in ('0','-1','+42','0x2a','42x','4294967296',' 42','42 '):
            run('overlay-pid-'+pid,['overlay','on','--pid',pid],root,False,'positive decimal PID')
        # No owned.txt: these exact valid forms refuse before opening a process
        # or channel. No discovery or live game operation is performed.
        for value in ('on','off'):
            run('overlay-unowned-'+value,['overlay',value,'--pid','42'],root,False,"package's owned game")
    result={'ok':True,'scope':'CLI usage and refusal paths only; no game discovery, launch, memory or network', 'cases':rows}
    Path(output).write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({'ok':True,'checks':len(rows)}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',type=Path,required=True);p.add_argument('--output',required=True)
    a=p.parse_args();check(a.cli,a.output)
