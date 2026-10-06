"""Root-only retained relay closure query after exact-run stop/fetch; never stops anything."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require(ok, message):
    if not ok:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('run-id','ready','fetched','output','ssh-exe','ssh-config'):
        parser.add_argument('--'+name, required=True)
    args = parser.parse_args()
    require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_-]{0,63}', args.run_id), 'invalid run ID')
    ready = read(args.ready)
    fetched = Path(args.fetched).resolve()
    require(ready['runId'] == args.run_id and fetched.name == args.run_id, 'wrong fetched/ready run')
    remote_dir = ready['remoteRunDir']
    require(re.fullmatch(r'/[A-Za-z0-9_./-]+', remote_dir) and
            remote_dir.endswith('/runs/'+args.run_id), 'wrong remote run path')
    metadata = read(fetched/'relay.json')
    require(metadata['run'] == args.run_id and metadata['pid'] == ready['pid'] and
            metadata['supervisorPid'] == ready['supervisorPid'], 'relay identity differs')
    output = Path(args.output)
    output.mkdir(exist_ok=False)
    script = Path(__file__).with_name('relay-closure.zsh').read_bytes()
    # Same authorized SSH host, fixed retained read-only script; path grammar excludes shell syntax.
    command = [args.ssh_exe,'-o','BatchMode=yes','-F',args.ssh_config,'-T','mac',
               '/bin/zsh','-l','-s','--',remote_dir]
    proc = subprocess.run(command, input=b'set -e\n'+script, capture_output=True, timeout=20)
    (output/'remote-final-stdout.txt').write_bytes(proc.stdout)
    (output/'remote-final-stderr.txt').write_bytes(proc.stderr)
    require(proc.returncode == 0, 'closure query failed; retain error, do not restart')
    remote = json.loads(proc.stdout)
    (output/'remote-final.json').write_text(json.dumps(remote,indent=2)+'\n',encoding='utf-8')
    local = {f.relative_to(fetched).as_posix():sha(f) for f in fetched.rglob('*') if f.is_file()}
    require(local == remote['hashes'] and not remote['relayPs'] and
            not remote['supervisorPs'] and not remote['socket'], 'relay closure/hash mismatch')
    require(remote['exit']['exitCode'] == 0 and remote['exit']['relayExitCode'] == 0,
            'relay did not close cleanly')
    result = dict(runId=args.run_id, fetched=str(fetched), artifactCount=len(local),
                  allHashesMatch=True, ownedProcessesAbsent=True, port27795Free=True, exit=remote['exit'])
    (output/'relay-final-verification.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
