"""Assemble one sealed-PENDING PC2 lane packet (VUH-2338 / VUH-2464) from portable products.

usage: build_packet.py NAME SCOPE SCENARIO BUDGET_SECONDS
Products come from build-pc2/ (README.md). The packet dir build/rig/pc2-lanes/NAME must be new. Its
payload is a portable package root: copied to a fresh folder on bld by desktop-lane.ps1 and run there
with the copied CPython 3.11 (-I).
"""
import hashlib, json, os, shutil, subprocess, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
WT = HERE.parents[2]
PY = Path(os.environ['LOCALAPPDATA']) / 'Programs/Python/Python311'
FFMPEG = Path(os.environ['LOCALAPPDATA']) / 'Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-8.0.1-full_build/bin/ffmpeg.exe'
BP = WT / 'build-pc2'
SKIP_DIRS = {'site-packages', '__pycache__', 'ensurepip', 'idlelib', 'test', 'tests', 'demos', 'pip', 'tkinter', 'turtledemo', 'Doc', 'Tools', 'Scripts', 'include', 'libs', 'tcl'}
SKIP_SUFFIX = {'.pyc', '.pyo', '.pdb', '.lib', '.exp', '.obj', '.log'}


def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def main(name, scope, scenario, budget):
    packet = WT / 'build/rig/pc2-lanes' / name
    packet.mkdir(parents=True, exist_ok=False)
    pay = packet / 'payload'
    head = subprocess.check_output(['git', '-C', str(WT), 'rev-parse', 'HEAD'], text=True).strip()
    products = {'bin/kh2ctl.exe': BP / 'tools/kh2ctl/Release/kh2ctl.exe',
                'bin/kh2ctl_diagnostic.exe': BP / 'tools/kh2ctl/Release/kh2ctl_diagnostic.exe',
                'bin/ffmpeg.exe': FFMPEG,
                'build/inject/staging/kh2coop_inject.dll': BP / 'inject/staging/kh2coop_inject.dll',
                'build/Release/kh2coop_runtime_scaffold.exe': BP / 'Release/kh2coop_runtime_scaffold.exe',
                'lane.py': HERE / 'lane.py', 'scenario.json': WT / scenario}
    for rel, src in products.items():
        copy(src, pay / rel)
    for rel in [p for p in (WT / 'tools/scenario').glob('*.py')] + list((WT / 'tools/rig/pc2_session').glob('*.py')) + \
            list((WT / 'tools/launcher').glob('*.py')) + [WT / 'tools/rig/route_pilot/pilot.py'] + list((WT / 'tools/rig/route_pilot/routes').glob('prologue_*.json')):
        copy(rel, pay / rel.relative_to(WT))
    for parent, dirs, names in os.walk(PY):  # prune before descending (site-packages is large)
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for n in names:
            p = Path(parent) / n
            if p.suffix not in SKIP_SUFFIX:
                copy(p, pay / 'python' / p.relative_to(PY))
    shutil.copytree(PY / 'Lib/site-packages/PIL', pay / 'dependencies/PIL', ignore=shutil.ignore_patterns('__pycache__', '*.pyc'))
    (pay / 'KH2COOP-PACKAGE').write_text('kh2coop-friend-package-v1\n', encoding='ascii')
    write = lambda p, v: p.write_text(json.dumps(v, indent=2) + '\n', encoding='utf-8')  # noqa: E731
    write(pay / 'package.json', {'schema': 1, 'name': 'VUH-2464 PC2 rig lane (not a friend package)',
                                 'sourceRevision': head, 'protocol': 26})
    files = [{'path': p.relative_to(pay).as_posix(), 'sha256': sha(p)} for p in sorted(pay.rglob('*')) if p.is_file()]
    write(pay / 'lane.json', {'name': name, 'scope': scope, 'sourceRevision': head, 'budgetSeconds': budget,
                              'account': '76561199097703456',
                              'accountSaveSha256': '069eeb520f280b989ea3993d7b94d2ca4ceaf348103599254288b1e34e087291',
                              'gameSha256': '9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed',
                              'buildid': '15194255', 'payloadFiles': files})
    copy(HERE / 'desktop-lane.ps1', packet / 'desktop-lane.ps1')
    pins = [{'path': p.relative_to(packet).as_posix(), 'sha256': sha(p)} for p in sorted(packet.rglob('*')) if p.is_file()]
    write(packet / 'pins.json', {'status': 'PENDING', 'packet': name, 'sourceRevision': head, 'files': pins})
    write(packet / 'lead-review.json', {'status': 'PENDING', 'pinsSha256': sha(packet / 'pins.json')})
    print(json.dumps({'packet': str(packet), 'files': len(pins), 'pinsSha256': sha(packet / 'pins.json'),
                      'bytes': sum(p.stat().st_size for p in packet.rglob('*') if p.is_file())}))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]))
