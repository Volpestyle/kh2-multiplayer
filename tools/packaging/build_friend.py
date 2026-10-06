"""Build a private friend ZIP from explicit installed inputs; no downloads/builds/live calls.

Output and evidence directories must be new. Local absolute provenance stays in
evidence; only portable role names and file hashes enter the ZIP.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/launcher'))
from friend_package import SUPPORTED_GAME, digest

FORBIDDEN_PARTS = {'site-packages', '__pycache__', 'ensurepip', 'idlelib', 'test', 'tests', 'demos', '.local', '.git', 'pip'}
FORBIDDEN_SUFFIXES = {'.pyc', '.pyo', '.pdb', '.lib', '.exp', '.obj', '.log'}
TEXT_SUFFIXES = {'.py', '.txt', '.md', '.rst', '.cmd', '.bat', '.ps1', '.json', '.ini', '.cfg', '.xml', '.html', '.htm', '.tcl', '.terms', '._pth'}
# Only the personal-host wording check exempts these bundled attributions.
# The full private-name/repository-path scan still covers their entire contents.
ATTRIBUTION_FILES = {
    'licenses/cpython-and-bundled-components.txt', 'licenses/tcl-tk-license.terms',
    'licenses/msvc-redist.txt', 'licenses/msvc-thirdpartynotices.txt',
    'licenses/enet.txt', 'licenses/minhook.txt',
    'python/lib/gettext.py', 'python/lib/profile.py', 'python/lib/pstats.py',
    'python/lib/turtledemo/fractalcurves.py', 'python/lib/xmlrpc/client.py',
    'python/lib/xml/parsers/__init__.py',
}


def allowed(path):
    return not (set(p.casefold() for p in path.parts) & FORBIDDEN_PARTS or path.suffix.casefold() in FORBIDDEN_SUFFIXES
                or path.name.startswith(('_test', '_ctypes_test')))


def installed_files(directory):
    # Prune site-packages before traversal, not after enumerating its contents.
    for parent, dirs, files in os.walk(directory):
        dirs[:] = sorted(d for d in dirs if allowed(Path(d)))
        for name in sorted(files):
            if allowed(Path(name)):yield Path(parent)/name


def leakage(name, data, repo=ROOT):
    # bytes.lower handles ASCII case in ASCII, UTF-8 and UTF-16 code units.
    forbidden = ('volpe', 'kh2-multiplayer', str(repo), str(repo).replace('\\', '/'))
    haystack = data.lower()
    for word in forbidden:
        for encoding in ('utf-8', 'utf-16-le', 'utf-16-be'):
            if word.casefold().encode(encoding) in haystack:
                raise ValueError(f'Private path/name leaked in {name}')
    if not allowed(Path(name)):
        raise ValueError(f'Forbidden package entry: {name}')


def user_text_leakage(relative_name, data):
    name = relative_name.replace('\\', '/').casefold()
    if name in ATTRIBUTION_FILES or Path(name).suffix not in TEXT_SUFFIXES:
        return
    if any('james'.encode(encoding) in data.lower()
           for encoding in ('utf-8', 'utf-16-le', 'utf-16-be')):
        raise ValueError(f'Personal host name in user-facing text: {relative_name}')


def scan_zip(path):
    entries = 0; pe = 0
    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            if info.is_dir():continue
            data = archive.read(info)
            leakage(info.filename, info.filename.encode('utf-8'))
            leakage(info.filename, data)
            # build() writes exactly one output-directory prefix into the ZIP.
            user_text_leakage(info.filename.partition('/')[2], data)
            entries += 1; pe += int(data.startswith(b'MZ'))
    return {'decompressedEntries':entries, 'peFiles':pe, 'privatePathLeaks':0, 'personalHostTextLeaks':0,
            'checks':['case-insensitive ASCII/UTF8/UTF16LE/UTF16BE', 'entry names and full decompressed contents',
                      'no site-packages/cache/pyc/developer logs/configs',
                      'case-insensitive James in text; explicit license/attribution exemptions only for this check']}


def crt_closure(products, crt, dumpbin, evidence):
    available={p.name.casefold():p for p in crt.glob('*.dll')}
    pending=list(products); visited=set(); needed=set(); rows=[]
    while pending:
        source=pending.pop(0).resolve()
        if source in visited:continue
        visited.add(source)
        result=subprocess.run([str(dumpbin),'/dependents',str(source)],capture_output=True,
                              text=True,errors='replace',timeout=15,check=True)
        imports=sorted(set(s.casefold() for s in re.findall(r'^\s+([\w.-]+\.dll)\s*$',result.stdout,re.MULTILINE|re.IGNORECASE)))
        if not imports:raise ValueError(f'No dependency evidence for {source.name}')
        rows.append({'source':str(source),'sha256':digest(source),'imports':imports,'stdout':result.stdout})
        for name in imports:
            if name in available and name not in needed:
                needed.add(name);pending.append(available[name])
            elif name.startswith(('msvcp','vcruntime','concrt','vccorlib')) and name not in available:
                raise ValueError(f'Missing imported CRT dependency: {name}')
    receipt={'dumpbin':str(dumpbin),'dumpbinSHA256':digest(dumpbin),'retainedBinCrtDlls':sorted(needed),
             'omittedBinCrtDlls':sorted(set(available)-needed),'imports':rows,
             'scope':'Direct product imports and transitive CRT imports; Windows API/UCRT remain OS dependencies. Python remains separate.'}
    (evidence/'crt-imports.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
    return [available[name] for name in sorted(needed)]


def build(args):
    output, evidence = Path(args.output).resolve(), Path(args.evidence).resolve()
    output.mkdir(parents=True, exist_ok=False); evidence.mkdir(parents=True, exist_ok=False)
    python = Path(args.python_home).resolve(); crt = Path(args.crt).resolve()
    sources = {}; products = {}; selected = json.loads((ROOT/'tools/packaging/friend-products.json').read_text())
    def copy(source, dest):
        source = Path(source).resolve(); target = output/dest
        if not source.is_file():raise ValueError(f'Missing installed input: {source}')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
        sources[dest] = {'source':str(source), 'sha256':digest(source)}
    for role, spec in selected['products'].items():
        source = ROOT/spec['path']
        if digest(source) != spec['sha256']:raise ValueError(f'Selected {role} hash mismatch')
        dest = 'bin/'+source.name; copy(source, dest); products[role]=dest
    cli = Path(args.cli).resolve()
    if digest(cli) != args.cli_sha256:raise ValueError('Private portable CLI hash mismatch')
    copy(cli, 'bin/kh2ctl.exe'); products['cli']='bin/kh2ctl.exe'
    for name in ('friend.py','friend_package.py','launcher.py','plan.py','windows_owned.py'):
        copy(ROOT/'tools/launcher'/name, 'tools/launcher/'+name)
    for name in ('python.exe','pythonw.exe','python3.dll','python311.dll','vcruntime140.dll','vcruntime140_1.dll'):
        copy(python/name, 'python/'+name)
    for tree in ('Lib','DLLs'):
        for source in installed_files(python/tree):
            relative = source.relative_to(python)
            if source.is_file() and allowed(relative):copy(source, 'python/'+relative.as_posix())
    for tree in ('tcl8.6','tk8.6','tcl8','dde1.4','reg1.3'):
        for source in installed_files(python/'tcl'/tree):
            relative=source.relative_to(python)
            if source.is_file() and allowed(relative):copy(source, 'python/'+relative.as_posix())
    for source in crt_closure([output/name for name in products.values()],crt,Path(args.dumpbin).resolve(),evidence):
        copy(source, 'bin/'+source.name)
    copy(python/'LICENSE.txt','licenses/CPython-and-bundled-components.txt')
    copy(python/'tcl/tk8.6/license.terms','licenses/Tcl-Tk-license.terms')
    copy(Path(args.vs_licenses)/'Redist.txt','licenses/MSVC-Redist.txt')
    copy(Path(args.vs_licenses)/'ThirdPartyNotices.txt','licenses/MSVC-ThirdPartyNotices.txt')
    copy(ROOT/'build/_deps/enet-src/LICENSE','licenses/ENet.txt')
    copy(ROOT/'build/_deps/minhook-src/LICENSE.txt','licenses/MinHook.txt')
    copy(ROOT/'docs/FRIEND_PLAYTEST.md','READ ME FIRST.txt')
    (output/'python/python311._pth').write_text('.\nLib\nDLLs\n../tools/launcher\n',encoding='ascii')
    (output/'KH2COOP-PACKAGE').write_text('kh2coop-friend-package-v1\n',encoding='ascii')
    (output/'Start KH2 Co-op.cmd').write_text('@echo off\ncd /d "%~dp0"\n"%~dp0python\\pythonw.exe" -I -B "%~dp0tools\\launcher\\friend.py"\n',encoding='ascii')
    files={p.relative_to(output).as_posix():digest(p) for p in sorted(output.rglob('*')) if p.is_file()}
    manifest={'schema':1,'name':'KH2 Co-op private friend preview','avatarBridgeVersion':3,'protocol':10,
              'gameBuild':'1.0.0.10-steam-global','content':'none','mod':'none',
              'supportedGame':{'sha256':SUPPORTED_GAME,'fileVersion':'1.0.0.2','edition':'Steam Global'},
              'products':products,'files':files}
    (output/'package.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    archive=output.with_suffix('.zip')
    if archive.exists():raise ValueError('ZIP already exists; choose a new output')
    with zipfile.ZipFile(archive,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for source in sorted(output.rglob('*')):
            if source.is_file():z.write(source,output.name+'/'+source.relative_to(output).as_posix())
    scan=scan_zip(archive)
    (evidence/'provenance.json').write_text(json.dumps({'inputs':sources,'files':files,'cliBuildInputs':str(args.cli),
        'note':'Local-only provenance; never placed in ZIP.'},indent=2),encoding='utf-8')
    result={'zip':str(archive),'sha256':digest(archive),'bytes':archive.stat().st_size,'scan':scan,
            'scope':'Offline bundle; no live startup or save/connection acceptance.'}
    (evidence/'package-result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('output','evidence','python-home','crt','dumpbin','vs-licenses','cli','cli-sha256'):p.add_argument('--'+name,required=True)
    build(p.parse_args())
