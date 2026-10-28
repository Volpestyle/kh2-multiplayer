#!/usr/bin/env python3
"""Compile actual owner-camera detours against adapter memory/native stand-ins.

Run through clankie heavy. --root may be an exported baseline; --ione-r3 reuses
Ione's saved r3 mains without modifying her evidence. No game or PC is accessed.
"""
import argparse
import json
from pathlib import Path
import subprocess


def section(src, start, end):
    i = src.index(start)
    return src[i:src.index(end, i)]


def generate(root, main):
    prefix = (root / 'tests/AxelReactionAdapterTest.cpp').read_text().split('int main()')[0]
    if 'static uintptr_t readFault=0;' not in prefix:
        prefix = prefix.replace('static unsigned reads=0,vmLookups=0;',
                                'static unsigned reads=0,vmLookups=0;\nstatic uintptr_t readFault=0;')
        prefix = prefix.replace('    ++reads;\n', '    ++reads;\n    if(at==readFault)return false;\n')
    if 'static bool gameThread=true;' not in prefix:
        prefix = prefix.replace('static bool SmGameThread() noexcept { return true; }',
                                'static bool gameThread=true;\nstatic bool SmGameThread() noexcept { return gameThread; }')
    if 'if(handle && handle==decodeFault)return 0;' not in prefix:
        prefix = prefix.replace('static std::uint32_t decodeFault=0;', '')
        prefix = prefix.replace('static uintptr_t SmDecode(std::uint32_t handle) {',
                                'static std::uint32_t decodeFault=0;\nstatic uintptr_t SmDecode(std::uint32_t handle) { if(handle && handle==decodeFault)return 0;')
    owner = (root / 'inject/src/ScriptActorOwner.inl').read_text()
    mirror = (root / 'inject/src/ScriptMirror.inl').read_text()
    copy = section(mirror, 'static bool SmCopyArgs(const std::uint64_t* args', '\n}\n') + '\n}\n'
    # Models the caught copy fault; deliberately leaves the remainder of the actual reader intact.
    copy = copy.replace('noexcept {', 'noexcept {\n    if(copyFault)return false;')
    detours = section(owner, 'static void __fastcall SmOwnerCameraGlobal(std::uint64_t* args) {',
                      '// The native watch a deferred Burst watch')
    stubs = r'''
static bool copyFault=false;
static unsigned reviewNativeOut=0,reviewNativeIn=0,reviewNativeReset=0,reviewNativeStart=0,reviewNativeWatch=0;
static void ReviewNativeWhiteOut(std::uint64_t*) { ++reviewNativeOut; }
static void ReviewNativeWhiteIn(std::uint64_t*) { ++reviewNativeIn; }
static void ReviewNativeReset(std::uint64_t*) { ++reviewNativeReset; }
static void ReviewNativeStart(std::uint64_t*) { ++reviewNativeStart; }
static void ReviewNativeWatch(std::uint64_t*) { ++reviewNativeWatch; }
static void ReviewNativeThread(std::uint64_t* args) { args[0]=R::Tagged(R::kTagAddress,0x71); }
static struct { uintptr_t original; }
    g_smAxelWhiteOutSlot{reinterpret_cast<uintptr_t>(&ReviewNativeWhiteOut)},
    g_smAxelWhiteInSlot{reinterpret_cast<uintptr_t>(&ReviewNativeWhiteIn)},
    g_smAxelResetSlot{reinterpret_cast<uintptr_t>(&ReviewNativeReset)},
    g_smAxelCameraSlot{reinterpret_cast<uintptr_t>(&ReviewNativeStart)},
    g_smAxelWatchSlot{reinterpret_cast<uintptr_t>(&ReviewNativeWatch)},
    g_smAxelThreadSlot{reinterpret_cast<uintptr_t>(&ReviewNativeThread)};
'''
    return prefix + stubs + copy + detours + main.replace('reviewDecodeFault', 'decodeFault')


def run(root, out, mains):
    out.mkdir(parents=True, exist_ok=True)
    results = []
    for name, main in mains:
        src = out / (name + '.cpp')
        src.write_text(generate(root, main))
        command = ['clang++', '-std=c++20', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                   '-Wno-unused-function', '-Wno-unused-const-variable', '-I', str(root/'inject/src'),
                   '-I', str(root/'common/include'), str(src), str(root/'common/src/Codec.cpp'),
                   str(root/'common/src/Protocol.cpp'), '-o', str(out/name)]
        b = subprocess.run(command, capture_output=True, text=True)
        (out/(name+'-build.log')).write_text(b.stdout+b.stderr)
        if b.returncode:
            print(b.stderr)
            results.append({'name': name, 'build': b.returncode})
            continue
        p = subprocess.run([str((out/name).resolve())], capture_output=True, text=True)
        (out/(name+'.log')).write_text(p.stdout+p.stderr)
        rows = p.stdout.splitlines()
        results.append({'name': name, 'exit': p.returncode,
                        'pass': sum(x.startswith('PASS') for x in rows),
                        'fail': [x for x in rows if x.startswith('FAIL')]})
        print(json.dumps(results[-1]), flush=True)
    (out/'checks.json').write_text(json.dumps(results, indent=2)+'\n')
    return all(r.get('exit') == 0 for r in results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--ione-r3', type=Path)
    args = parser.parse_args()
    mains = [('read-sites', Path(__file__).with_name('AxelCameraReadProbe.inl').read_text())]
    if args.ione_r3:
        for name in ('identity-read-counterexamples', 'pending-retirement-counterexamples'):
            src = (args.ione_r3/(name+'.cpp')).read_text()
            mains.append((name, src[src.index('int main() {'):]))
    return 0 if run(args.root.resolve(), args.out.resolve(), mains) else 1


if __name__ == '__main__':
    raise SystemExit(main())
