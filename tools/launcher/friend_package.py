"""Portable friend package checks and canonical, retained game ownership.

Importing this file starts nothing. No game memory or direct game termination API.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import re

from plan import steam_id
from steam_flow import AppIdFile, broker_error

GAME_NAME = 'KINGDOM HEARTS II FINAL MIX.exe'
SUPPORTED_GAME = '9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed'


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def verify_package(root):
    root = Path(root).resolve()
    manifest = json.loads((root / 'package.json').read_text(encoding='utf-8'))
    if manifest.get('schema') != 1 or manifest.get('avatarBridgeVersion') != 3:
        raise ValueError('This package is incomplete or has incompatible components. Unpack a fresh copy.')
    if manifest.get('defaultTransport','enet') not in ('steam','enet'):
        raise ValueError('This package has an unknown connection mode. Unpack a fresh copy.')
    for name, expected in manifest['files'].items():
        path = (root / name).resolve()
        if not path.is_relative_to(root) or not path.is_file() or digest(path) != expected:
            raise ValueError(f'Package file is missing or changed: {name}. Unpack a fresh copy; do not mix versions.')
    required = {'cli', 'dll', 'runtime', 'server', 'avatarctl'}
    if set(manifest['products']) != required:
        raise ValueError('Package product roles are incomplete.')
    for role, name in manifest['products'].items():
        if name not in manifest['files'] or not (root / name).is_file():
            raise ValueError(f'Missing verified {role} product.')
    if manifest['supportedGame']['sha256'] != SUPPORTED_GAME:
        raise ValueError('Unsupported game allowlist in this package.')
    return manifest


def verify_game(game_dir, package_root):
    directory = Path(game_dir).expanduser().resolve()
    root = Path(package_root).resolve()
    if root == directory or root.is_relative_to(directory):
        raise ValueError('Unpack this package beside the game folder, not inside it.')
    exe = directory / GAME_NAME
    if not exe.is_file():
        raise ValueError(f'Choose the folder containing {GAME_NAME}.')
    if digest(exe) != SUPPORTED_GAME:
        raise ValueError('This KH2 build is not supported. This preview requires the verified Steam Global EXE. '
                         'Epic and different Steam patches are refused; no injection was attempted.')
    return exe


def child_environment(root, *, steam_broker=False):
    # Do not inherit a developer's test/trace/write-policy switches into a friend game.
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('KH2COOP_')}
    if type(steam_broker) is not bool:
        raise ValueError('Steam broker opt-in must be explicit.')
    if steam_broker:
        env['KH2COOP_STEAM_BROKER'] = '1'
    env['SteamAppId'] = env['SteamGameId'] = '2552430'
    return env


class CanonicalFailure(ValueError):
    def __init__(self, message, receipt):
        super().__init__(message)
        self.receipt = receipt


def canonical(root, cli, args, *, timeout, run=subprocess.run, steam_broker=False):
    if steam_broker and (not args or args[0] != 'launch'):
        raise ValueError('Steam broker opt-in is only for an owned game launch.')
    result = run([str(cli), *args], cwd=str(root), env=child_environment(root, steam_broker=steam_broker),
                 stdin=subprocess.DEVNULL, capture_output=True, text=True, encoding='utf-8',
                 errors='replace', timeout=timeout, shell=False,
                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    try:
        receipt = json.loads(result.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError) as error:
        raise ValueError('Game helper did not return a receipt. See the launcher log; do not launch another copy.') from error
    if result.returncode or not receipt.get('ok'):
        raise CanonicalFailure(receipt.get('error') or '; '.join(receipt.get('errors', [])) or result.stdout or result.stderr, receipt)
    return receipt


def attest_guard(root, pid, receipt):
    if not receipt.get('hooksInstalled') or receipt.get('errors'):
        raise ValueError('Game hooks were not confirmed. Connection refused.')
    expected = Path(root) / 'build/rig/logs' / f'kh2coop_inject_{pid}.log'
    if Path(receipt['log']).resolve() != expected.resolve():
        raise ValueError('Game log is outside this package. Connection refused.')
    text = expected.read_text(encoding='utf-8', errors='replace')
    if 'Save guard installed' not in text or 'save guard incomplete' in text or 'ERROR' in text:
        raise ValueError('Save protection was not confirmed. Connection refused; close this game.')
    return expected


class GameOwner:
    """Only the exact successful canonical launch is eligible for canonical kill.

    A retained query handle prevents trusting PID alone. kh2ctl independently
    checks its (PID, creation-time) ownership file. It remains the sole killer.
    """
    def __init__(self, root, manifest, win, command=canonical):
        self.root, self.manifest, self.win, self.command = Path(root).resolve(), manifest, win, command
        self.pid = None
        self.handle = None
        self.ready = False
        self.receipt = None
        self.unresolved_launch = False
        self.overlay_auto_session = None
        self.overlay_enabled = None
        self.transport = None
        self.launch_wall_ns = 0
        self.steam_identity = ''
        self.appid_file = None
        self.launch_directory = None

    def product(self, name):
        return self.root / self.manifest['products'][name]

    def launch(self, game_dir, receipt_dir, *, transport='enet', appid_consent=False):
        if transport not in ('enet', 'steam'):
            raise ValueError('Choose ENet / relay or Steam (beta).')
        if self.unresolved_launch:
            raise ValueError('A previous start has no verified completion. Use Exit & close game before reopening the launcher; do not start another copy. If the game is still open, exit it normally; do not save.')
        if self.handle and self.win.alive(self.handle):
            raise ValueError('This launcher already owns a game. Close it before starting another.')
        exe = verify_game(game_dir, self.root)  # must precede every launch/injection
        verify_package(self.root)
        receipt_dir = Path(receipt_dir); receipt_dir.mkdir(parents=True, exist_ok=False)
        self.launch_directory = receipt_dir
        appid = None
        if transport == 'steam':
            self.appid_file = AppIdFile(exe.parent / 'steam_appid.txt')
            appid = self.appid_file.prepare(appid_consent)
        argv = ['launch', '--game-dir', str(exe.parent), '--dll', str(self.product('dll'))]
        self.launch_wall_ns = time.time_ns()
        self.transport = transport
        self.steam_identity = ''
        (receipt_dir / 'launch-request.json').write_text(json.dumps({'argv':argv,'gameSHA256':SUPPORTED_GAME,
            'startedNs':time.perf_counter_ns(), 'startedWallNs':self.launch_wall_ns,
            'transport':transport, 'steamBroker':transport == 'steam', 'steamAppIdFile':appid}, indent=2), encoding='utf-8')
        self.unresolved_launch = True
        launch_failure = None
        try:
            opt_in = {'steam_broker':True} if transport == 'steam' else {}
            receipt = self.command(self.root, self.product('cli'), argv, timeout=105, **opt_in)
        except Exception as error:
            (receipt_dir / 'launch-error.json').write_text(json.dumps({'error':str(error),
                'ownershipUnresolved':True,'steamAppIdFile':appid,
                'note':'No guessed PID is adopted or killed. Any newly created app-ID file is retained until owned game closure is confirmed.'},indent=2),encoding='utf-8')
            # InjectAndReport returns the exact launched PID even when a hook
            # failed. Preserve that receipt so this owned unsafe game is closed.
            receipt = error.receipt if isinstance(error, CanonicalFailure) else {}
            if (receipt.get('command') != 'launch' or type(receipt.get('processId')) is not int
                    or not 0 < receipt['processId'] < 2**32):
                raise
            launch_failure = error
        self.pid = int(receipt['processId'])
        self.receipt = receipt
        cleanup = None
        try:
            self.handle = self.win.open_query(self.pid)
            if launch_failure:raise launch_failure
            if not self.win.alive(self.handle):
                raise ValueError('Game exited before it could be prepared.')
            attest_guard(self.root, self.pid, receipt)
            rows = self.command(self.root, self.product('cli'), ['instances'], timeout=8)['instances']
            if not any(r['processId']==self.pid and r.get('owned') is True for r in rows):
                raise ValueError('Canonical ownership was not confirmed.')
            self.ready = True
            self.unresolved_launch = False
            self.overlay_auto_session = None
            self.overlay_enabled = None  # a new attested game starts a new HUD preference
        except Exception as error:
            # An injected game without confirmed protection must not be left
            # playable silently. Canonical kill still checks retained ownership.
            try:
                if not self.handle:
                    raise ValueError('No retained game handle; ownership could not be confirmed')
                cleanup = self.close_game()
            except Exception as stop_error:
                cleanup = {'error':str(stop_error),'closureVerified':False}
                if isinstance(stop_error, CanonicalFailure):cleanup['receipt']=stop_error.receipt
                detail = str(stop_error)
                warning = 'If the game is still open, exit it normally; do not save.'
                if warning not in detail:detail += '. ' + warning
                raise ValueError(f'{error} Closure failed: {detail}') from error
            raise ValueError(f'{error} The owned game was closed; connection was not started.') from error
        finally:
            (receipt_dir / 'launch-result.json').write_text(json.dumps({'receipt':receipt,
                'ready':self.ready,'cleanup':cleanup,'finishedNs':time.perf_counter_ns()}, indent=2), encoding='utf-8')
        return receipt

    def read_steam_identity(self, *, read=None):
        """Read only the broker log for this attested game. No IPC/API attachment."""
        if (self.transport != 'steam' or not self.ready or self.unresolved_launch or
                not self.handle or not self.win.alive(self.handle)):
            raise ValueError('Start an owned game in Steam (beta) mode first.')
        if self.steam_identity:
            return self.steam_identity
        path = self.root / 'build/rig/logs' / f'steam-broker_{self.pid}.log'
        # The broker opens wx: an old PID log prevents creation, never qualifies.
        text, created_ns = (read or read_shared_prefix)(path)
        if created_ns < self.launch_wall_ns:
            raise ValueError('Broker receipt predates this owned game. Restart with a fresh package log path.')
        identity = parse_broker_identity(text)
        if identity:
            self.steam_identity = identity
        return identity

    def connection_identity(self, transport):
        if not self.ready or not self.handle or not self.win.alive(self.handle):
            raise ValueError('Start and prepare your game here first.')
        if transport != self.transport:
            raise ValueError('Connection mode changed. Close this owned game before starting in the new mode.')
        if transport == 'steam':
            identity = self.read_steam_identity()
            if not identity:
                raise ValueError('Steam broker is not ready. Wait for the game Steam session; matching broker DLL/runtime are required.')
            return identity
        return ''

    def set_overlay(self, enabled, session, connection_line, *, automatic=False):
        """One mod-channel flag request, only for this attested, connected game.

        The session lock excludes its timer/stop; the UI busy gate excludes
        Exit and other button work. A receipt confirms the flag, not pixels.
        """
        if type(enabled) is not bool:
            raise ValueError('HUD choice must be on or off.')
        with session.lock:
            if automatic:
                if self.overlay_enabled is not None or self.overlay_auto_session is session:
                    return None
                self.overlay_auto_session = session  # a failure is not auto-retried
            argv = ['overlay', 'on' if enabled else 'off', '--pid', str(self.pid)]
            record = {'argv':argv, 'automatic':automatic, 'startedNs':time.perf_counter_ns(),
                      'connectionLine':connection_line, 'confirmed':False}
            try:
                if (not self.ready or self.unresolved_launch or not self.handle or
                        not self.win.alive(self.handle) or session.plan['options']['pid'] != self.pid or
                        not session.started.is_set() or session.stopping.is_set() or session.done.is_set() or
                        not connection_line.startswith('[Runtime] Network: SessionState session=') or
                        any(p.poll() is not None for p in session.processes.values()) or
                        'runtime' not in session.processes):
                    raise ValueError('HUD requires this launcher\'s prepared game and a connected session.')
                verify_package(self.root)
                attest_guard(self.root, self.pid, self.receipt)
                receipt = self.command(self.root, self.product('cli'), argv, timeout=8)
                record['receipt'] = receipt
                if (receipt.get('ok') is not True or type(receipt.get('processId')) is not int or
                        receipt['processId'] != self.pid or receipt.get('overlay') is not enabled):
                    raise ValueError('HUD helper receipt did not match the owned game and requested flag.')
                self.overlay_enabled = enabled
                record['confirmed'] = True
                return receipt
            except Exception as error:
                record['error'] = str(error)
                if isinstance(error, CanonicalFailure):record['receipt'] = error.receipt
                raise
            finally:
                record['finishedNs'] = time.perf_counter_ns()
                with (session.path / 'overlay.jsonl').open('a', encoding='utf-8') as stream:
                    stream.write(json.dumps(record) + '\n')

    def close_game(self):
        self.ready = False
        if not self.handle:
            return {'killed': [], 'note': 'No retained game ownership.'}
        wait_code = self.win.k.WaitForSingleObject(self.handle, 0)
        if wait_code == 258:  # WAIT_TIMEOUT: retained game has not exited
            result = self.command(self.root, self.product('cli'), ['kill', '--pid', str(self.pid)], timeout=12)
            # Canonical ownership/termination is unchanged. Its wait result is
            # not reported; confirm termination on our original retained handle.
            wait_ms = 2000 if self.pid in result.get('killed', []) else 0
            wait_code = self.win.k.WaitForSingleObject(self.handle, wait_ms)
            closure = {'processId':self.pid,'canonicalKill':result,'exitWaitMs':wait_ms,'exitWaitCode':wait_code}
            if self.pid not in result.get('killed', []) or wait_code != 0:
                raise CanonicalFailure('Owned game closure was not established; no other PID will be stopped. If the game is still open, exit it normally; do not save.', closure)
            result = {**result, 'closureVerified':True,'exitWaitMs':wait_ms,'exitWaitCode':wait_code}
        elif wait_code == 0:  # WAIT_OBJECT_0 only; WAIT_FAILED is never closure
            result = {'killed': [], 'note': 'Owned game already exited.', 'closureVerified':True,'exitWaitCode':wait_code}
        else:
            raise CanonicalFailure('Owned game handle wait failed; closure is unknown. If the game is still open, exit it normally; do not save.',
                                   {'processId':self.pid,'exitWaitMs':0,'exitWaitCode':wait_code})
        self.unresolved_launch = False  # only after confirmed WAIT_OBJECT_0
        self.win.close(self.handle)
        self.handle = None
        if self.appid_file:
            result['steamAppIdFile'] = self.appid_file.cleanup()
            if self.launch_directory:
                (self.launch_directory / 'appid-cleanup.json').write_text(json.dumps(result['steamAppIdFile'], indent=2), encoding='utf-8')
        return result


def parse_broker_identity(text):
    # Ignore partial trailing lines. Never use IDs in incoming-peer messages.
    lines = text.split('\n')[:-1]
    identities = []
    for line in lines:
        match = re.fullmatch(r'\[steam-broker\] ready appId=2552430 identity=([0-9]{17}) modulePinnedUntilExit=1', line.rstrip('\r'))
        if match:
            identities.append(steam_id(match[1]))
        if line.startswith(('[steam-broker] unavailable ', '[steam-broker] refused pipe-create', '[steam-broker] exception ')):
            raise ValueError(broker_error(line) or 'Steam broker is unavailable; see the owned game broker log.')
    if len(identities) > 1:
        raise ValueError('Ambiguous Steam broker identity receipt.')
    return identities[0] if identities else ''


def read_shared_prefix(path, *, offset=0):
    """One bounded read, with write sharing for the live log; never retry/block on a lock."""
    import ctypes as c
    from ctypes import wintypes as w
    k = c.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.argtypes = [w.LPCWSTR,w.DWORD,w.DWORD,c.c_void_p,w.DWORD,w.DWORD,w.HANDLE]
    k.CreateFileW.restype = w.HANDLE
    k.ReadFile.argtypes = [w.HANDLE,c.c_void_p,w.DWORD,c.POINTER(w.DWORD),c.c_void_p]
    k.ReadFile.restype = w.BOOL
    k.GetFileTime.argtypes = [w.HANDLE,c.POINTER(w.FILETIME),c.c_void_p,c.c_void_p]
    k.GetFileTime.restype = w.BOOL
    k.CloseHandle.argtypes = [w.HANDLE]; k.CloseHandle.restype = w.BOOL
    k.SetFilePointerEx.argtypes = [w.HANDLE,c.c_longlong,c.POINTER(c.c_longlong),w.DWORD]
    handle = k.CreateFileW(str(path),0x80000000,1|2|4,None,3,0,None)
    if handle == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    try:
        created = w.FILETIME(); count = w.DWORD(); buf = c.create_string_buffer(8192)
        if offset and not k.SetFilePointerEx(handle,offset,None,0):raise c.WinError(c.get_last_error())
        if not k.GetFileTime(handle,c.byref(created),None,None) or not k.ReadFile(handle,buf,8192,c.byref(count),None):
            raise c.WinError(c.get_last_error())
        stamp = ((created.dwHighDateTime << 32) | created.dwLowDateTime) * 100 - 11644473600000000000
        return buf.raw[:count.value].decode('utf-8',errors='replace'), stamp
    finally:
        k.CloseHandle(handle)
