"""PC2 (bld) sealed single-attempt lane for VUH-2338/VUH-2464, adapted from boot-smoke07's smoke.py.

Runs from a fresh copy of the packet payload, which is its own portable package root: the
portable kh2ctl (launch/instances/kill) and kh2ctl_diagnostic (pad input, capture, peek, state,
entities, mute, dump) run with that root as cwd, so ownership, logs and frames stay inside it. A clip
is a burst of in-renderer captures encoded by the payload's ffmpeg (about 7 fps on PC1), not 30 fps video. Input is the DLL's pad mailbox only; nothing is foregrounded
and no OS keyboard/mouse input is sent. Never saves; refuses unless the save, app-ID and game
inventory is identical before and after.
"""
import argparse, datetime as dt, hashlib, importlib.util, json, os, re, subprocess, sys, threading, time, traceback
from pathlib import Path

sys.stderr = sys.stdout  # one stream: the desk job tees stdout, and PowerShell 5 must never see stderr
F = Path(__file__).resolve().parent
sys.path[:0] = [str(F / 'dependencies'), str(F / 'tools/rig/pc2_session')]
from system_sessions import snapshot  # noqa: E402
from binding import active_console_session, require_session  # noqa: E402

GAME = Path(r'C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-')
PORTABLE = ('launch', 'kill', 'instances')
DIAGNOSTIC = ('mute', 'capture', 'player-press', 'player-input', 'peek', 'state', 'entities', 'dump')


def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def read(p): return json.loads(Path(p).read_text(encoding='utf-8-sig'))
def write(p, v): Path(p).write_text(json.dumps(v, indent=2) + '\n', encoding='utf-8')


def inventory(account):
    """Account B's save files (consent covers only that account; steam_autocloud.vdf is the Steam
    client's, as in run.py STEAM_OWNED), the game EXE and its app-ID file."""
    rows = {p.name: sha(p) for p in sorted(account.iterdir()) if p.is_file() and p.name != 'steam_autocloud.vdf'}
    rows['<game>/KINGDOM HEARTS II FINAL MIX.exe'] = sha(GAME / 'KINGDOM HEARTS II FINAL MIX.exe')
    app = GAME / 'steam_appid.txt'
    rows['<game>/steam_appid.txt'] = sha(app) if app.exists() else None
    return rows


def main():
    ap = argparse.ArgumentParser()
    for n in ('consent', 'go', 'evidence'):
        ap.add_argument('--' + n, type=Path)
    ap.add_argument('--control', action='store_true', help='offline setup control: stops before consent, lock or launch')
    ap.add_argument('--rehearsal', action='store_true',
                    help='PC1 live rehearsal: the same lane, without the bld identity, consent, GO and seal checks; '
                         'lock and evidence stay under --evidence, inventory covers the local Steam account')
    a = ap.parse_args()
    lane = read(F / 'lane.json')
    for row in lane['payloadFiles']:
        p = F / row['path']
        assert p.resolve().is_relative_to(F) and sha(p) == row['sha256'], 'Unpacked file mismatch: ' + row['path']
    if a.control:
        from PIL import Image  # noqa: F401 - the boot step's title detection
        spec = importlib.util.spec_from_file_location('pc2_runner', F / 'tools/scenario/run.py')
        r = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = r
        spec.loader.exec_module(r)
        scenario = read(F / 'scenario.json')
        r.validate_scenario(scenario)
        if scenario.get('playerDefaults'):  # run_scenario imports the launcher's player policy
            sys.path.insert(0, str(F / 'tools/launcher'))
            import friend_package
            assert friend_package.child_environment(F)['KH2COOP_ORDINARY_LIFECYCLE_EXPERIMENTAL'] == '0'
        out = {}
        for exe, args in (('kh2ctl.exe', ['instances']), ('kh2ctl_diagnostic.exe', ['instances']),
                          ('ffmpeg.exe', ['-version'])):
            run = subprocess.run([str(F / 'bin' / exe), *args], cwd=F, capture_output=True, text=True, errors='replace',
                                 creationflags=subprocess.CREATE_NO_WINDOW)
            out[exe] = (run.returncode, (run.stdout or run.stderr).strip().splitlines()[-1][:160] if (run.stdout or run.stderr).strip() else '')
        print(json.dumps({'control': 'ok', 'steps': len(scenario['steps']), 'session': active_console_session(),
                          'python': sys.version.split()[0], 'products': out}, indent=1))
        return 0
    home = Path(os.environ['USERPROFILE']).resolve()
    saves = home / 'OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX'
    if a.rehearsal:
        assert a.evidence, 'Explicit evidence path required'
        go = {'lane': lane['name'], 'runId': 'rehearsal', 'pinsSha256': 'rehearsal', 'consentSha256': 'none'}
        c = {'session': active_console_session()}
        (account,) = [d for d in (saves / 'Steam').iterdir() if d.is_dir()]
    else:
        assert a.consent and a.go and a.evidence, 'Explicit consent, GO and evidence paths required'
        go, c = read(a.go), read(a.consent)
        today = dt.datetime.now(dt.timezone.utc).date().isoformat()
        assert go['lane'] == lane['name'] and re.fullmatch(r'[A-Za-z0-9-]{8,64}', go['runId'])
        assert sha(a.consent) == go['consentSha256'], 'Consent bytes differ from GO'
        assert c['schema'] == 'pc2-consent/1' and c['utcDate'] == today, 'Fresh UTC consent required'
        assert c['host'].casefold() == os.environ['COMPUTERNAME'].casefold() and c['user'].casefold() == os.environ['USERNAME'].casefold()
        assert any(s.startswith(lane['scope'] + ':') for s in c['scope']), 'Lane outside consent scope'
        account = saves / 'Steam' / lane['account']
        assert sha(account / 'KHIIFM_WW.png') == lane['accountSaveSha256'], 'Account-B save differs from Rook 2026-10-18'
    assert sha(GAME / 'KINGDOM HEARTS II FINAL MIX.exe') == lane['gameSha256'], 'Game EXE differs'
    manifest = (GAME.parents[1] / 'appmanifest_2552430.acf').read_text(encoding='utf-8')
    assert re.search(r'"buildid"\s+"' + lane['buildid'] + '"', manifest), 'Steam build differs'
    assert not os.environ.get('KH2_GAME_DIR'), 'Default Steam launch only'
    assert not [k for k in os.environ if k.upper().startswith('KH2COOP_')], 'Inherited KH2COOP_* variables refuse'
    legacy = home / 'dev/kh2-multiplayer/build/rig/rig.lock'
    assert not legacy.exists(), 'Legacy checkout lock held; no stale takeover'
    me = require_session(c['session'], snapshot(), os.getpid(), active_console_session())
    evidence = a.evidence.resolve()
    assert a.rehearsal or evidence.is_relative_to(home / 'lead-outbox')
    evidence.mkdir(parents=True, exist_ok=False)
    lock = (evidence / 'kh2-rig/rig.lock') if a.rehearsal else home / 'lead-outbox/kh2-rig/rig.lock'
    budget = time.monotonic() + lane['budgetSeconds']
    owned, actions, result = {}, [], {'lane': lane['name'], 'runId': go['runId'], 'status': 'FAIL',
                                      'pinsSha256': go['pinsSha256'], 'consentSha256': go['consentSha256']}
    held = closed = before = False
    journal = threading.Lock()  # background clips dispatch from their own thread
    token = json.dumps({'schemaVersion': 1, 'machine': 'PC2', 'pid': os.getpid(), 'creationTicks': me['creationTicks'],
                        'runId': go['runId'], 'packetPinsSha256': go['pinsSha256'], 'lane': lane['name'],
                        'sinceUtc': dt.datetime.now(dt.timezone.utc).isoformat()}).encode('utf-8')

    def dispatch(*args, pid=None, check=True, timeout=120, env=None):
        command = args[0]
        if command not in ('kill', 'instances'):
            assert time.monotonic() < budget, 'Lane budget exhausted'
        if command == 'clip':
            data = clip(args, pid)
            with journal:
                actions.append({'command': 'clip', 'pid': pid, 'ok': data.get('ok'), 'frames': data.get('frames')})
                write(evidence / 'commands.json', actions)
            if check and not data.get('ok'):
                raise r.StepFailed(f"kh2ctl clip: {data.get('error', data)}")
            return data
        exe = 'kh2ctl.exe' if command in PORTABLE else 'kh2ctl_diagnostic.exe' if command in DIAGNOSTIC else None
        assert exe, 'Command outside the lane boundary: ' + command
        argv = [str(F / 'bin' / exe), *map(str, args)]
        if pid is not None:
            argv += ['--pid', str(pid)]
        if command == 'launch':
            require_session(c['session'], snapshot(), os.getpid(), active_console_session())
            argv += ['--dll', str(F / 'build/inject/staging/kh2coop_inject.dll')]
        if pid in owned:
            current = [x for x in snapshot() if x['pid'] == pid]
            assert not current or current[0]['creationTicks'] == owned[pid], 'PID identity changed'
        child_env = {**os.environ, 'PYTHONUTF8': '1', 'PATH': str(F / 'bin') + os.pathsep + os.environ.get('PATH', ''),
                     **(env or {})}
        out = subprocess.run(argv, cwd=F, capture_output=True, text=True, encoding='utf-8', errors='replace',
                             env=child_env, timeout=timeout, creationflags=subprocess.CREATE_NO_WINDOW)
        text = out.stdout.strip()
        try:
            data = json.loads(text.splitlines()[-1]) if text else {}
        except json.JSONDecodeError:
            data = {'ok': False, 'error': text or out.stderr.strip()}
        with journal:
            actions.append({'command': command, 'pid': pid, 'ok': data.get('ok'), 'returncode': out.returncode,
                        **({} if data.get('ok') else {'error': str(data.get('error'))[:200]})})
            write(evidence / 'commands.json', actions)
        if command == 'launch' and data.get('processId'):
            found = [x for x in snapshot() if x['pid'] == data['processId']]
            owned[data['processId']] = found[0]['creationTicks'] if found else 0
        if check and not data.get('ok', False):
            raise r.StepFailed(f"kh2ctl {' '.join(map(str, args))}: {data.get('error', data)}")
        return data

    def clip(args, pid):
        """`kh2ctl clip` stand-in: a burst of in-renderer captures (kh2ctl_diagnostic) for the requested
        seconds, encoded by the payload's ffmpeg at the achieved rate. Same reply shape as kh2ctl clip."""
        opts = dict(zip(args[1::2], args[2::2]))
        seconds, out = float(opts.get('--seconds', 5)), Path(opts['--out'])
        # Frames go to a short path under the payload root: the game writes each PNG itself, and a deep
        # evidence path passed MAX_PATH in the PC1 rehearsal (261 chars), failing every capture.
        frames = F / 'build/rig/clips' / f'c{time.monotonic_ns()}'
        frames.mkdir(parents=True)
        t0, n, errors = time.monotonic(), 0, set()
        while time.monotonic() - t0 < seconds:
            data = dispatch('capture', '--out', str(frames / f'f_{n:05d}.png'), pid=pid, check=False)
            if data.get('ok'):
                n += 1
            else:
                errors.add(str(data.get('error'))[:200])
        elapsed = time.monotonic() - t0
        if not n:
            return {'ok': False, 'error': 'no frame captured: ' + '; '.join(sorted(errors))}
        fps = n / elapsed
        enc = subprocess.run([str(F / 'bin/ffmpeg.exe'), '-nostdin', '-y', '-loglevel', 'error', '-framerate', f'{fps:.3f}', '-i',
                              str(frames / 'f_%05d.png'), '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-crf', '20', str(out)],
                             stdin=subprocess.DEVNULL, capture_output=True, text=True, errors='replace', timeout=120,
                             creationflags=subprocess.CREATE_NO_WINDOW)
        if enc.returncode:
            return {'ok': False, 'error': 'ffmpeg: ' + enc.stderr[-300:]}
        for f in frames.iterdir():
            f.unlink()
        frames.rmdir()
        return {'ok': True, 'processId': pid, 'path': str(out), 'frames': n, 'seconds': round(elapsed, 2),
                'fpsDuring': round(fps, 2), 'clipFps': round(fps, 2), 'source': 'capture-burst'}

    spec = importlib.util.spec_from_file_location('pc2_runner', F / 'tools/scenario/run.py')
    r = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = r
    spec.loader.exec_module(r)
    try:
        lock.parent.mkdir(parents=True, exist_ok=True)
        fd = os.open(lock, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        held = True
        with os.fdopen(fd, 'wb') as stream:
            stream.write(token)
        assert not legacy.exists(), 'Legacy checkout lock appeared'
        spent = lock.parent / 'spent' / go['pinsSha256']
        spent.parent.mkdir(exist_ok=True)
        with spent.open('xb') as stream:
            stream.write(token)
        # (A PC1 rehearsal shares the PC with its host's game; ownership stays exact either way.)
        assert a.rehearsal or not dispatch('instances')['instances'], 'Existing KH2 refuses; no process touched'
        write(evidence / 'inventory-before.json', inventory(account))
        before = True
        r.RUNS = evidence / 'scenarios'
        os.environ['KH2_PILOT_KH2CTL'] = str(F / 'bin/kh2ctl_diagnostic.exe')  # the route pilot's entities/capture
        r.kh2ctl = dispatch
        runtime = r.STEPS['runtime']

        def remote_runtime(ctx, step):  # the friend's relay is on the host PC: no local relay step sets the port
            if 'port' in step:
                ctx.relay_port = str(step['port'])
            return runtime(ctx, step)
        r.STEPS['runtime'] = remote_runtime
        report = r.run_scenario(F / 'scenario.json', 1)
        result['report'] = str(Path(report['dir']).relative_to(evidence)) if 'dir' in report else None
        result['scenarioStatus'] = report['status']
        assert report['status'] == 'pass', report.get('error')
        result['status'] = 'PASS'
    except Exception as error:
        result['error'] = str(error)
        (evidence / 'failure.txt').write_text(traceback.format_exc(), encoding='utf-8')
    finally:
        if held:
            try:
                registry = F / 'build/rig/owned.txt'
                if registry.exists():
                    for line in registry.read_text(encoding='utf-8').splitlines():
                        pid, created = map(int, line.split()[:2])
                        assert pid > 0 and created > 0, 'Incomplete package ownership record'
                        if owned.get(pid):
                            assert owned[pid] == created, 'Ownership record changed'
                        owned[pid] = created
                for pid, created in owned.items():
                    current = [x for x in snapshot() if x['pid'] == pid]
                    if current:
                        assert created and current[0]['creationTicks'] == created, 'Unverified owned identity; no kill'
                        dispatch('kill', pid=pid, check=False)
                end = time.monotonic() + 15
                alive = lambda: any(x['pid'] in owned and x['creationTicks'] == owned[x['pid']] for x in snapshot())  # noqa: E731
                while alive() and time.monotonic() < end:
                    time.sleep(.2)
                closed = not alive()
                assert closed, 'Owned game survived closure'
                if before:
                    write(evidence / 'inventory-after.json', inventory(account))
                    assert read(evidence / 'inventory-before.json') == read(evidence / 'inventory-after.json'), 'Save/app-ID/game inventory changed'
                    result['inventoryUnchanged'] = True
                result['ownedClosed'] = True
            except Exception as error:
                result.update(status='FAIL', closureError=str(error))
            finally:
                if closed and lock.exists() and lock.read_bytes() == token:
                    lock.unlink()
        write(evidence / 'result.json', result)
    print(json.dumps({'status': result['status'], 'evidence': str(evidence)}))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
