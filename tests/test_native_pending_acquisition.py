"""Actual sampler controls; constructed producer rows, never native evidence."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('acquisition', Path(__file__).resolve().parents[1] / 'tools/scenario/causal_summary_acquisition.py')
acquisition = importlib.util.module_from_spec(spec)
spec.loader.exec_module(acquisition)


class Clock:
    value = 1_000_000_000

    def __call__(self):
        return self.value


class Cancel:
    def __init__(self, clock):
        self.clock, self.cancelled, self.waits = clock, False, 0

    def is_set(self):
        return self.cancelled

    def wait(self, seconds):
        self.waits += 1
        self.clock.value += int(seconds * 1e9)


class PendingAcquisition(unittest.TestCase):
    def run_samples(self, pending, mutation=None, cancel_after=None):
        clock = Clock()
        cancel = Cancel(clock)
        original = dict(generation='1', delivery='1', connection='2', epoch='2', load='3',
                        transition='1', role='2', slot='1', location='5,6,0,1,1,0')
        prefix = ''.join(acquisition.RECEIPT + ' '.join(f'{k}={v}' for k, v in dict(
            original, kind='0', qualified='1', reason='0', frame='700', loss='0', netId=str(i)).items()) + '\n'
            for i in range(1, 6))
        reads = []

        def read(deadline):
            nonlocal prefix
            index = len(reads)
            before = prefix
            row = dict(original, admitted='1', retired='0', dropped='0', loss='0', ownerAvailable='1',
                       ownerFrame=str(801 + index), pendingNativeLeave=str(pending[min(index, len(pending)-1)]))
            if mutation:
                mutation(row)
            prefix += acquisition.SUMMARY + ' '.join(f'{k}={v}' for k, v in row.items()) + '\n'
            started = clock.value
            clock.value += 1_000_000
            sample = dict(readStartedNs=started, readFinishedNs=clock.value, injectBefore=before, injectAfter=prefix)
            reads.append(copy.deepcopy(sample))
            if cancel_after == index:
                cancel.cancelled = True
            self.assertLessEqual(deadline, 4_000_000_000)
            return sample

        result = acquisition.acquire(read, lambda s, n: None, cancel, 4_000_000_000, _clock_ns=clock)
        return result, reads, cancel

    def test_pending_waits_for_host_cut_without_dispatch(self):
        result, reads, cancel = self.run_samples([1, 1, 0])
        self.assertEqual(len(reads), 3)
        self.assertEqual(cancel.waits, 2)
        self.assertEqual([r['status'] for r in result[1]['attempts']],
                         ['WAIT_PENDING_NATIVE_LEAVE', 'WAIT_PENDING_NATIVE_LEAVE', 'FRESH_ADMITTED'])
        self.assertFalse(result[1]['dispatched'])

    def test_pending_timeout_is_not_admission(self):
        with self.assertRaisesRegex(acquisition.AcquisitionFailed, 'deadline expired'):
            self.run_samples([1])

    def test_pending_cancel_refuses(self):
        with self.assertRaisesRegex(acquisition.AcquisitionFailed, 'cancelled'):
            self.run_samples([1], cancel_after=0)

    def test_pending_keeps_all_original_summary_guards(self):
        cases = [('retired', '1', 'retired/lost'), ('epoch', '99', 'differs from original'),
                 ('ownerAvailable', '0', 'unavailable'), ('ownerFrame', '1', 'behind admission'),
                 ('loss', '1', 'retired/lost'), ('pendingNativeLeave', '-1', 'invalid pending'),
                 ('pendingNativeLeave', '6', 'invalid pending'), ('pendingNativeLeave', 'x', 'invalid literal')]
        for key, value, reason in cases:
            with self.subTest(key=key, value=value):
                with self.assertRaisesRegex(acquisition.AcquisitionFailed, reason):
                    self.run_samples([1], mutation=lambda row: row.update({key: value}))

    def test_legacy_summary_still_qualifies(self):
        result, _, _ = self.run_samples([0], mutation=lambda row: row.pop('pendingNativeLeave'))
        self.assertEqual(result[1]['attempts'][0]['status'], 'FRESH_ADMITTED')


if __name__ == '__main__':
    unittest.main()
