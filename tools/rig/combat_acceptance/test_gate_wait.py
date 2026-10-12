"""Bounded idle-wait policy/full-gate collection, without native side effects."""
import copy
import unittest
from . import gate_wait as gate

def busy():
    return dict(ok=False, status='REFUSED', reasons=[dict(category='occupancy', reason='recent desktop input')], idleMs=32)

def passed():
    return dict(ok=True, status='PASS', reasons=[], idleMs=60_000)

class Clock:
    def __init__(self):self.now = 1
    def read(self):return self.now
    def sleep(self, seconds):self.now += round(seconds * 1_000_000_000)

class GateWait(unittest.TestCase):
    def run_wait(self, evaluate, *, deadline_ns=None, cancel=None):
        self.clock = Clock(); self.retained = []
        return gate.wait_gate(evaluate, lambda i, v: self.retained.append((i, copy.deepcopy(v))),
            clock_ns=self.clock.read, sleep=self.clock.sleep, deadline_ns=deadline_ns,
            cancelled=cancel or (lambda: False))

    def test_retained_32ms_then_full_idle_pass(self):
        values = iter([busy(), passed()]); result = self.run_wait(lambda: next(values))
        self.assertTrue(result['ok']); self.assertEqual(result['samples'], 2)
        self.assertEqual(self.clock.now, 1 + gate.POLL_NS)
        self.assertEqual(self.retained[0][1]['idleMs'], 32)

    def test_still_busy_expires_original_ten_minutes(self):
        result = self.run_wait(busy)
        self.assertFalse(result['ok']); self.assertEqual(result['reason'], 'idle wait deadline expired')
        self.assertEqual(self.clock.now, 1 + gate.WAIT_NS); self.assertEqual(len(self.retained), 40)

    def test_non_occupancy_immediate(self):
        value = busy(); value['reasons'].append(dict(category='unsafe', reason='disk below15GiB'))
        self.assertFalse(self.run_wait(lambda: value)['ok'])
        self.assertEqual(len(self.retained), 1); self.assertEqual(self.clock.now, 1)

    def test_recognized_game_then_clear(self):
        value = busy(); value['reasons'][0]['reason'] = 'foreign game present'
        values = iter([value, passed()]); self.assertTrue(self.run_wait(lambda: next(values))['ok'])

    def test_existing_friend_deadline_is_never_renewed(self):
        result = self.run_wait(busy, deadline_ns=20_000_000_001)
        self.assertEqual(self.clock.now, 20_000_000_001)
        self.assertFalse(result['ok']); self.assertEqual(len(self.retained), 2)

    def test_stop_during_wait(self):
        result = self.run_wait(busy, cancel=lambda: self.clock.now > 1)
        self.assertEqual(result['reason'], 'STOP requested'); self.assertEqual(len(self.retained), 1)
        self.assertLessEqual(self.clock.now, 250_000_001)

    def test_late_full_pass_is_refused(self):
        def read():self.clock.now += gate.WAIT_NS; return passed()
        self.assertFalse(self.run_wait(read)['ok'])

    def test_unknown_refusal_cannot_wait(self):
        result = self.run_wait(lambda: dict(ok=False, status='REFUSED'))
        self.assertEqual(self.clock.now, 1); self.assertEqual(result['reason'], 'non-occupancy or unknown refusal')

    def test_exception_is_not_reclassified_as_busy(self):
        with self.assertRaisesRegex(OSError, 'unreadable'):
            self.run_wait(lambda: (_ for _ in ()).throw(OSError('unreadable')))

    def test_all_checks_run_even_on_recent_input(self):
        seen = []
        names = ['originalController', 'pinsSha256', 'zipSha256', 'foreignGameGate',
                 'idleMs', 'steamAccount', 'locksAbsent', 'freeBytes', 'gamesAbsent']
        def check(name):
            seen.append(name)
            if name == 'foreignGameGate':return dict(ok=True, session=1, activeConsole=1, reasons=[])
            if name == 'idleMs':return 32
            if name == 'freeBytes':raise OSError('disk refusal')
            return True
        value = gate.collect_gate([(n, lambda n=n: check(n)) for n in names], game_names=set())
        self.assertEqual(seen, names)
        self.assertEqual({r['category'] for r in value['reasons']}, {'occupancy', 'unsafe'})
        self.assertFalse(self.run_wait(lambda: value)['ok']); self.assertEqual(self.clock.now, 1)

    def test_full_gate_is_repeated_before_pass(self):
        seen = []
        def evaluate():
            index = len(seen)
            def read():seen.append('all-pins-account-session-locks-disk'); return True
            return gate.collect_gate([('integrity', read), ('idleMs', lambda: 32 if index == 0 else 60_000)], game_names=set())
        self.assertTrue(self.run_wait(evaluate)['ok']); self.assertEqual(len(seen), 2)

    def test_game_vs_anticheat_unknown_and_session(self):
        value = dict(ok=False, session=1, activeConsole=1, reasons=[dict(
            reason='foreign game or anti-cheat present', process=dict(name='r5apex_dx12.exe'))])
        self.assertEqual(gate.occupancy_reasons(value, {'r5apex_dx12.exe'})[0]['category'], 'occupancy')
        value['reasons'][0]['process']['name'] = 'EasyAntiCheat.exe'
        self.assertEqual(gate.occupancy_reasons(value, {'r5apex_dx12.exe'})[0]['category'], 'unsafe')
        value['session'] = 0
        self.assertEqual(gate.occupancy_reasons(value, set())[0]['category'], 'session')

if __name__ == '__main__':unittest.main()
