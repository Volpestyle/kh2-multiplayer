"""Friend Steam UX boundaries. No Steam initialization, sockets, or game calls."""
import os
from pathlib import Path
import stat
from plan import steam_id

INVITE_PREFIX = 'kh2coop:steam:'
APP_ID_BYTES = b'2552430\n'


def invitation(identity):
    return INVITE_PREFIX + steam_id(identity)


def host_identity(text):
    text = text.strip()
    if text.startswith(INVITE_PREFIX): text = text[len(INVITE_PREFIX):]
    return steam_id(text)


def confirm_account(actual, expected, confirmed):
    actual = steam_id(actual)
    if expected.strip() and steam_id(expected.strip()) != actual:
        raise ValueError('Wrong Steam account. Close this game, sign in to the expected account in Steam, then start again.')
    if confirmed is not True:
        raise ValueError('Check that the displayed SteamID is your account, then confirm it before connecting.')
    return actual


class AppIdFile:
    """Consent to create one missing file; never overwrite or remove another file."""
    def __init__(self, path):
        self.path = Path(path); self.owned = None

    @staticmethod
    def identity(info): return (info.st_dev, info.st_ino, info.st_mtime_ns, info.st_size)

    def prepare(self, consent):
        try:
            info = self.path.lstat()
        except FileNotFoundError:
            if consent is not True:
                raise ValueError('steam_appid.txt is missing. Allow the launcher to create the KH2 app-ID file before Start game.')
            try:
                with self.path.open('xb') as stream:
                    stream.write(APP_ID_BYTES); stream.flush(); os.fsync(stream.fileno())
                    created = os.fstat(stream.fileno())
                info = self.path.lstat()
                if (created.st_dev, created.st_ino) != (info.st_dev, info.st_ino):
                    raise ValueError('steam_appid.txt changed during creation; preserved. No game was launched.')
                self.owned = self.identity(info)
            except FileExistsError:
                return self.prepare(False)  # a concurrent creator owns it
            return {'created': True, 'path': str(self.path), 'appId': 2552430}
        if not stat.S_ISREG(info.st_mode) or self.path.is_symlink():
            raise ValueError('steam_appid.txt must be a regular file, not a link or directory. No file was changed.')
        if self.path.read_bytes().strip() != b'2552430':
            raise ValueError('steam_appid.txt has a different app ID. No file was changed; check your KH2 installation.')
        return {'created': False, 'path': str(self.path), 'appId': 2552430}

    def cleanup(self):
        if self.owned is None: return {'removed': False, 'reason': 'pre-existing or not created here'}
        try:
            info = self.path.lstat()
            if (not stat.S_ISREG(info.st_mode) or self.path.is_symlink()
                    or self.identity(info) != self.owned or self.path.read_bytes() != APP_ID_BYTES):
                return {'removed': False, 'reason': 'file changed; preserved'}
            self.path.unlink(); self.owned = None
            return {'removed': True}
        except FileNotFoundError:
            self.owned = None
            return {'removed': False, 'reason': 'already absent'}


def broker_error(line):
    if 'unavailable existing-session' in line:
        return 'Steam session unavailable. Start Steam and sign in to the account owning KH2 in this same desktop session, then close and restart this game.'
    if 'unavailable auth-relay' in line:
        return 'Steam authentication or Valve relay unavailable. Check that Steam is online, then close and restart this game.'
    if line.startswith(('[steam-broker] refused ', '[steam-broker] exception ')):
        return 'Steam connection refused. Relay-only privacy or broker startup failed; inspect the session logs. No direct-IP fallback.'
    return ''


class RuntimeStatus:
    def __init__(self, hosting=False):
        self.hosting = hosting; self.listener = False; self.roster = False
        self.terminal = False; self.text = 'Starting hosting…' if hosting else 'Connecting to host…'

    def feed(self, line):
        if self.terminal: return
        if '[Runtime] Networking stopped: terminal relay/session close' in line:
            self.text = 'Host left. This session has ended. Ask the host before joining a new session.'
            self.roster = False; self.terminal = True
        elif '[Runtime] Networking stopped:' in line or '[Runtime] Rejoin stopped:' in line:
            self.text = 'Connection ended. Check Steam, the host ID and host allowlist before trying Join again.'
            self.roster = False; self.terminal = True
        elif 'Network: refused by relay:' in line:
            self.text = 'Session refused: ' + line.split('refused by relay:', 1)[1].strip()
            self.roster = False; self.terminal = True
        elif 'Rejoin attempt=' in line or 'Rejoin waiting;' in line or 'Network: closed ' in line:
            self.text = 'Connection interrupted. Reconnecting to the same host…'
            self.roster = False
        elif line.startswith('[Runtime] Network: SessionState session='):
            self.roster = True
            self.text = 'Hosting — roster verified' if self.hosting and self.listener else 'Connected — roster verified'
        elif line.startswith('[steam-broker] listen handle=') and 'iceCreation=0' in line:
            self.listener = True
            if self.hosting and self.roster: self.text = 'Hosting — roster verified'
        elif line.startswith('[steam-broker]'):
            error = broker_error(line)
            if error:
                self.text = error; self.roster = False; self.terminal = True
