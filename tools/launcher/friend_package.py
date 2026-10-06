"""Portable friend package checks and canonical, retained game ownership.

Importing this file starts nothing. No game memory or direct game termination API.
"""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

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


def child_environment(root):
    # Do not inherit a developer's test/trace/write-policy switches into a friend game.
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith('KH2COOP_')}
    env['SteamAppId'] = env['SteamGameId'] = '2552430'
    # The injected DLL also uses the bundled MSVC runtime. KH2 keeps its game
    # cwd; dependency lookup receives this package path only in the child env.
    env['PATH'] = str(Path(root).resolve()/'bin') + os.pathsep + env.get('PATH', '')
    return env


class CanonicalFailure(ValueError):
    def __init__(self, message, receipt):
        super().__init__(message)
        self.receipt = receipt


def canonical(root, cli, args, *, timeout, run=subprocess.run):
    result = run([str(cli), *args], cwd=str(root), env=child_environment(root),
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

    def product(self, name):
        return self.root / self.manifest['products'][name]

    def launch(self, game_dir, receipt_dir):
        if self.unresolved_launch:
            raise ValueError('A previous start has no verified completion. Exit its game normally and reopen the launcher; do not start another copy.')
        if self.handle and self.win.alive(self.handle):
            raise ValueError('This launcher already owns a game. Close it before starting another.')
        exe = verify_game(game_dir, self.root)  # must precede every launch/injection
        verify_package(self.root)
        receipt_dir = Path(receipt_dir); receipt_dir.mkdir(parents=True, exist_ok=False)
        argv = ['launch', '--game-dir', str(exe.parent), '--dll', str(self.product('dll'))]
        (receipt_dir / 'launch-request.json').write_text(json.dumps({'argv':argv,'gameSHA256':SUPPORTED_GAME,
            'startedNs':time.perf_counter_ns()}, indent=2), encoding='utf-8')
        self.unresolved_launch = True
        launch_failure = None
        try:
            receipt = self.command(self.root, self.product('cli'), argv, timeout=105)
        except Exception as error:
            (receipt_dir / 'launch-error.json').write_text(json.dumps({'error':str(error),
                'ownershipUnresolved':True,'note':'No guessed PID is adopted or killed.'},indent=2),encoding='utf-8')
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
        except Exception as error:
            # An injected game without confirmed protection must not be left
            # playable silently. Canonical kill still checks retained ownership.
            try:
                if not self.handle:
                    raise ValueError('No retained game handle; ownership could not be confirmed')
                cleanup = self.close_game()
                self.unresolved_launch = False  # close_game verified the retained game exited
            except Exception as stop_error:
                cleanup = {'error':str(stop_error),'closureVerified':False}
                raise ValueError(f'{error} Closure failed: {stop_error}. Exit that game normally; do not save.') from error
            raise ValueError(f'{error} The owned game was closed; connection was not started.') from error
        finally:
            (receipt_dir / 'launch-result.json').write_text(json.dumps({'receipt':receipt,
                'ready':self.ready,'cleanup':cleanup,'finishedNs':time.perf_counter_ns()}, indent=2), encoding='utf-8')
        return receipt

    def close_game(self):
        self.ready = False
        if not self.handle:
            return {'killed': [], 'note': 'No retained game ownership.'}
        if self.win.alive(self.handle):
            result = self.command(self.root, self.product('cli'), ['kill', '--pid', str(self.pid)], timeout=12)
            if self.pid not in result.get('killed', []) or self.win.alive(self.handle):
                raise ValueError('Owned game closure was not established. Exit that game normally; no other PID will be stopped.')
        else:
            result = {'killed': [], 'note': 'Owned game already exited.'}
        self.win.close(self.handle)
        self.handle = None
        return result
