"""Read-only, bounded PC1 idle wait; never starts a friend, host or game.

Each retry is a complete admission. Unknown/non-occupancy failures never wait.
An existing deadline can shorten, but never extend, the ten-minute allowance.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import time

WAIT_NS = 600_000_000_000
POLL_NS = 15_000_000_000
IDLE_MS = 60_000


def occupancy_reasons(observation, game_names):
    """Only a positively recognized game may use the occupancy wait path."""
    reasons = []
    if observation.get('session') != 1 or observation.get('activeConsole') != 1:
        reasons.append(dict(category='session', reason='active Session1 required'))
    for row in observation.get('reasons', []):
        name = row.get('process', {}).get('name', '').lower()
        known = row.get('reason') in (
            'foreign game or anti-cheat present',
            'foreign fullscreen application; conservative gaming occupancy refusal')
        reasons.append(dict(category='occupancy' if known and name in game_names else 'unsafe',
                            reason=row.get('reason', 'unknown occupancy reason'), evidence=row))
    if not observation.get('ok') and not reasons:
        reasons.append(dict(category='unsafe', reason='unqualified occupancy observation'))
    return reasons


def collect_gate(checks, *, game_names, clock_ns=time.perf_counter_ns):
    """Retain all checks, including non-occupancy errors during a busy desktop."""
    result = dict(ok=False, status='REFUSED', reasons=[], observedNs=clock_ns(),
                  checkedUtc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                  gameStarted=False, pc2Contact=False)
    for name, check in checks:
        try:
            value = check()
            result[name] = value
            if name == 'foreignGameGate':
                result['reasons'].extend(occupancy_reasons(value, game_names))
            elif name == 'idleMs' and (type(value) is not int or value < IDLE_MS):
                if type(value) is not int or value < 0:
                    raise ValueError('invalid idle observation')
                result['reasons'].append(dict(category='occupancy', reason='recent desktop input', idleMs=value))
        except Exception as exc:
            result['reasons'].append(dict(category='unsafe', check=name, reason=repr(exc)))
    result['finishedNs'] = clock_ns()
    result['ok'] = not result['reasons']
    result['status'] = 'PASS' if result['ok'] else 'REFUSED'
    return result


def wait_gate(evaluate, retain, *, clock_ns=time.perf_counter_ns, sleep=time.sleep,
              cancelled=lambda: False, deadline_ns=None):
    start = clock_ns()
    end = min(start + WAIT_NS, deadline_ns) if deadline_ns is not None else start + WAIT_NS
    samples = 0

    def finish(status, reason, sample=None):
        return dict(ok=status == 'PASS', status=status, reason=reason, lastSample=sample,
                    samples=samples, startedNs=start, finishedNs=clock_ns(), deadlineNs=end,
                    attemptSpent=False, friendGoSent=False, hostQueued=False)

    while True:
        if cancelled():
            return finish('REFUSED', 'STOP requested')
        if clock_ns() >= end:
            return finish('REFUSED', 'idle wait deadline expired')
        sample = evaluate()  # Exceptions propagate; never convert unknown into busy.
        samples += 1
        retain(samples, sample)
        if cancelled():
            return finish('REFUSED', 'STOP requested', sample)
        if clock_ns() >= end:
            return finish('REFUSED', 'idle wait deadline expired', sample)
        if sample.get('ok') is True and sample.get('status') == 'PASS' and not sample.get('reasons'):
            return finish('PASS', 'fresh full gate passed', sample)
        reasons = sample.get('reasons')
        if not isinstance(reasons, list) or not reasons or any(r.get('category') != 'occupancy' for r in reasons):
            return finish('REFUSED', 'non-occupancy or unknown refusal', sample)
        wake = min(clock_ns() + POLL_NS, end)
        while clock_ns() < wake:
            if cancelled():
                return finish('REFUSED', 'STOP requested', sample)
            sleep(min(0.25, (wake - clock_ns()) / 1_000_000_000))


def windows_gate(packet, stage, expected_account, checkout):
    """Exact packet gate plus the existing desktop/account/locks/disk checks."""
    import ctypes as c
    from ctypes import wintypes as w
    import winreg
    import sys
    packet = Path(packet).resolve()
    sys.path.insert(0, str(packet / 'tools'))

    def module(name, path):
        spec = importlib.util.spec_from_file_location(name, path)
        value = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(value)
        return value

    gates = module('wait_packet_gates', packet / 'gates.py')
    ready = module('wait_packet_readiness', packet / 'pc1_readiness.py')
    sessions = module('wait_packet_sessions', packet / 'tools/system_sessions.py')

    def identity():
        return next(x for x in sessions.snapshot() if x['pid'] == os.getpid())

    def integrity():
        gates.integrity()
        return gates.sha(packet / 'pins.json')

    def idle():
        class Input(c.Structure):
            _fields_ = [('cbSize', w.UINT), ('dwTime', w.DWORD)]
        u = c.WinDLL('user32', use_last_error=True)
        k = c.WinDLL('kernel32', use_last_error=True)
        u.GetLastInputInfo.argtypes = [c.POINTER(Input)]
        u.GetLastInputInfo.restype = w.BOOL
        k.GetTickCount.restype = w.DWORD
        value = Input(); value.cbSize = c.sizeof(value)
        if not u.GetLastInputInfo(c.byref(value)):
            raise c.WinError(c.get_last_error())
        return (k.GetTickCount() - value.dwTime) & 0xffffffff

    def account():
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam\ActiveProcess') as key:
            user = winreg.QueryValueEx(key, 'ActiveUser')[0]
        value = str(76561197960265728 + user)
        if value != expected_account:
            raise ValueError('Steam account mismatch')
        return value

    def locks():
        root = Path(checkout)
        if not root.is_absolute():
            raise ValueError('absolute checkout root required')
        paths = [root / 'build/rig/rig.lock', root / 'build/rig/ctest.lock',
                 Path(os.environ['USERPROFILE']) / 'lead-outbox/kh2-rig/rig.lock']
        if any(p.exists() for p in paths):
            raise ValueError('lock held')
        return list(map(str, paths))

    def disk():
        free = shutil.disk_usage('C:/').free
        if free < 15 * 1024**3:
            raise ValueError('C: below15GiB')
        return free

    def games():
        if any('kingdom hearts' in x['name'].lower() for x in sessions.snapshot()):
            raise ValueError('existing KH2/launcher')
        return True

    value = collect_gate([
        ('originalController', identity), ('pinsSha256', integrity),
        ('zipSha256', lambda: gates.sha(packet / 'KH2-Co-op.zip')),
        ('foreignGameGate', ready.observe), ('idleMs', idle), ('steamAccount', account),
        ('locksAbsent', locks), ('freeBytes', disk), ('gamesAbsent', games)],
        game_names=ready.GAME_NAMES)
    value['stage'] = stage
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packet', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--stage', required=True)
    parser.add_argument('--checkout-root', type=Path, required=True)
    parser.add_argument('--expected-account', default='76561198210234623')
    parser.add_argument('--stop', type=Path)
    parser.add_argument('--deadline-ns', type=int)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)

    def retain(index, value):
        (args.out / f'sample-{index:03d}.json').write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')

    try:
        outcome = wait_gate(lambda: windows_gate(args.packet, args.stage, args.expected_account, args.checkout_root), retain,
                            cancelled=lambda: args.stop is not None and args.stop.exists(),
                            deadline_ns=args.deadline_ns)
    except Exception as exc:
        failure = dict(ok=False, status='REFUSED', reasons=[dict(category='unsafe', reason=repr(exc))],
                       observedNs=time.perf_counter_ns())
        retain(0, failure)
        outcome = dict(ok=False, status='REFUSED', reason='gate evaluation/retention error', samples=0,
                       lastSample=failure, attemptSpent=False, friendGoSent=False, hostQueued=False)
    result = {**(outcome['lastSample'] or {}), 'ok': outcome['ok'], 'status': outcome['status'], 'wait': outcome}
    (args.out / 'pc1-admission.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(dict(ok=result['ok'], status=result['status'], samples=outcome['samples'], reason=outcome['reason'])))
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
