"""Governed Release build + ctest of one exact commit for bld, in PC1's neutral-path worktree (VUH-2339).

The dev kh2ctl compiles its source directory in (KH2COOP_SOURCE_DIR) and keeps owned.txt, logs and the
staged DLL under it, so bld's checkout sits at the same C:/kh2rig/wt and these products are copied there
unchanged. Builds stay on PC1: the PC1 desk owner queues build-job.ps1 between live runs. Like the bots05
build it refuses while a live run holds the canonical rig.lock and holds the canonical ctest.lock.

    python -B C:/kh2rig/ops/build.py <full commit sha>

Writes C:/kh2rig/products/<commit>/ with each product at its build-relative path and products.json
(commit and sha256 of every product the bld live run uses).
"""
from pathlib import Path
import hashlib, json, shutil, subprocess, sys

ROOT = Path('C:/kh2rig/wt')
DEPS = Path('C:/kh2rig/deps')  # FetchContent sources copied from a PC1 build (enet v1.3.17, minhook)
OUT = Path('C:/kh2rig/products')
CANONICAL = Path('C:/Users/volpe/repos/kh2-multiplayer/build/rig')
PRODUCTS = ['build/inject/staging/kh2coop_inject.dll', 'build/Release/kh2coop_preinject_guard.dll',
            'build/Release/kh2coop_server.exe', 'build/Release/kh2coop_runtime_scaffold.exe',
            'build/tools/kh2ctl/Release/kh2ctl.exe']


def git(*args):
    result = subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise SystemExit(f'git {" ".join(args)} exited {result.returncode}: {result.stderr.strip()}')
    return result.stdout.strip()


def tree(expect):
    head = git('rev-parse', 'HEAD')
    dirty = [l for l in git('status', '--porcelain', '--untracked-files=no').splitlines() if l]
    if head != expect or dirty:
        raise SystemExit(f'checkout is not clean exact {expect}: head={head} dirty={dirty}')
    return head


def main():
    if len(sys.argv) != 2 or len(sys.argv[1]) != 40:
        raise SystemExit('usage: build.py <full commit sha>')
    expect = sys.argv[1]
    if (OUT / expect / 'products.json').exists():
        raise SystemExit(f'{expect} already built; products are kept, not replaced')
    if (CANONICAL / 'rig.lock').exists():
        raise SystemExit('live lane occupied; build deferred')
    head = tree(expect)
    sys.path.insert(0, str(ROOT / 'tools/scenario'))
    import run as runner
    runner.LOCK = CANONICAL / 'ctest.lock'
    lock = runner.RigLock()
    error = lock.acquire()
    if error:
        raise SystemExit(error)
    try:
        commands = []
        if not (ROOT / 'build/CMakeCache.txt').exists():
            commands.append(['cmake', '-S', str(ROOT), '-B', str(ROOT / 'build'), '-G', 'Visual Studio 18 2026',
                             f'-DFETCHCONTENT_SOURCE_DIR_ENET={DEPS / "enet-src"}',
                             f'-DFETCHCONTENT_SOURCE_DIR_MINHOOK={DEPS / "minhook-src"}'])
        commands += [['cmake', '--build', 'build', '--config', 'Release', '--', '/m:2'],
                     ['ctest', '--test-dir', 'build', '-C', 'Release', '--output-on-failure']]
        for command in commands:
            print(command, flush=True)
            result = subprocess.run(command, cwd=ROOT)
            if result.returncode:
                raise SystemExit(result.returncode)
        tree(expect)
        staging = OUT / (expect + '.partial')
        shutil.rmtree(staging, ignore_errors=True)
        rows = []
        for p in PRODUCTS:
            target = staging / p
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / p, target)
            rows.append(dict(path=p, sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
            if rows[-1]['sha256'] != hashlib.sha256((ROOT / p).read_bytes()).hexdigest():
                raise SystemExit(f'product changed while copying: {p}')
        (staging / 'products.json').write_text(json.dumps(dict(commit=head, products=rows), indent=2) + '\n',
                                               encoding='utf-8')
        staging.rename(OUT / expect)
        print('products', json.dumps(rows, indent=1), flush=True)
    finally:
        lock.release()


if __name__ == '__main__':
    main()
