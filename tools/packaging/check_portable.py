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
        run('help',['help'],root,True,'Internal commands: launch, instances, kill, help.')
        for command in ('restart','boot-load-save','inject','poke','player-input','world-resync','capture'):
            run('blocked-'+command,[command],root,False,'Command unavailable')
    result={'ok':True,'scope':'CLI usage and refusal paths only; no game discovery, launch, memory or network', 'cases':rows}
    Path(output).write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps({'ok':True,'checks':len(rows)}))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cli',type=Path,required=True);p.add_argument('--output',required=True)
    a=p.parse_args();check(a.cli,a.output)
