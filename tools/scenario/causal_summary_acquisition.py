"""Acquire a complete fresh admitted owner-frame bracket BEFORE Admission.

The caller owns native reads, original identities, cancellation and outer budget.
read_sample(deadline_ns) must bound every CLI/read/join by that absolute QPC
deadline, and return an unmodified full sample. validate(sample, now_ns) must be
the original activation/safety/scope validator. This helper never invokes,
resets or replaces the irreversible Admission instance and never dispatches.
"""
import hashlib
import re
import time

SUMMARY = '[combat-causal] summary '
RECEIPT = '[combat-causal] receipt '
MAX_ACQUISITION_NS = 3_000_000_000

class AcquisitionFailed(RuntimeError):
    def __init__(self, reason, attempts, last_sample=None):
        super().__init__(reason)
        self.attempts = list(attempts)
        self.last_sample = last_sample

def _rows(text, prefix):
    return [dict(re.findall(r'(\w+)=([^\s]*)', line.split(prefix, 1)[1]))
            for line in text.splitlines() if prefix in line]

def _integer(row, key):
    return int(row[key])

def acquire(read_sample, validate, cancel, deadline_ns, *, _clock_ns=time.perf_counter_ns):
    """Return (original_sample, evidence) or fail with retained attempt evidence.

    Retries are reads only. Every safety/prefix/retirement failure is terminal;
    only no new summary, a fresh not-yet-admitted summary, or an explicitly
    pending native leave awaiting its original host cut may wait. At most
    three seconds and 64 attempts, clamped to the original caller deadline.
    """
    attempts = []
    start = _clock_ns()
    if type(deadline_ns) is not int or deadline_ns <= start:
        raise AcquisitionFailed('summary acquisition deadline expired', attempts)
    end = min(deadline_ns, start + MAX_ACQUISITION_NS)
    sample = None

    def check():
        if cancel.is_set():
            raise AcquisitionFailed('summary acquisition cancelled', attempts, sample)
        if _clock_ns() >= end:
            raise AcquisitionFailed('summary acquisition deadline expired', attempts, sample)

    previous = None
    for _ in range(64):
        check()
        requested = _clock_ns()
        sample = read_sample(end)
        check()
        now = _clock_ns()
        try:
            if not (requested <= sample['readStartedNs'] < sample['readFinishedNs'] <= now):
                raise ValueError('summary acquisition cached/mixed-clock native read')
            validate(sample, now)
            check()
            before, after = sample['injectBefore'], sample['injectAfter']
            if not after.startswith(before) or (previous is not None and not before.startswith(previous)):
                raise ValueError('summary acquisition log prefix replaced/truncated')
            receipts = _rows(after, RECEIPT)
            if any(_integer(r, 'kind') == 11 or _integer(r, 'loss') != 0 for r in receipts):
                raise ValueError('summary acquisition native observer retired/lost')
            all_summaries = _rows(after, SUMMARY)
            if any(_integer(r, k) != 0 for r in all_summaries for k in ('retired', 'dropped', 'loss')):
                raise ValueError('summary acquisition producer summary retired/lost')
            fresh = _rows(after[len(before):], SUMMARY)
            attempt = dict(index=len(attempts), requestedNs=requested, observedNs=now,
                           readStartedNs=sample['readStartedNs'], readFinishedNs=sample['readFinishedNs'],
                           beforeSha256=hashlib.sha256(before.encode('utf-8')).hexdigest(),
                           afterSha256=hashlib.sha256(after.encode('utf-8')).hexdigest(),
                           newSummaries=len(fresh), status='WAIT_NO_SUMMARY')
            attempts.append(attempt)
            previous = after
            if fresh:
                row = fresh[-1]
                if _integer(row, 'admitted') == 0:
                    attempt['status'] = 'WAIT_NOT_ADMITTED'
                elif _integer(row, 'admitted') != 1:
                    raise ValueError('invalid admitted summary value')
                else:
                    pending = int(row.get('pendingNativeLeave', '0'))
                    if not 0 <= pending <= 5:
                        raise ValueError('invalid pending native leave count')
                    admitted = [r for r in receipts if _integer(r, 'kind') == 0]
                    if len(admitted) != 5 or any(_integer(r, 'qualified') != 1 or _integer(r, 'reason') != 0 for r in admitted):
                        raise ValueError('fresh summary missing five original admissions')
                    for key in ('generation', 'delivery', 'connection', 'epoch', 'load', 'transition', 'role', 'slot', 'location'):
                        if any(r[key] != row[key] for r in admitted):
                            raise ValueError('fresh owner summary scope differs from original admission')
                    if _integer(row, 'ownerAvailable') != 1 or _integer(row, 'ownerFrame') < max(_integer(r, 'frame') for r in admitted):
                        raise ValueError('fresh owner summary unavailable/behind admission')
                    earlier = _rows(before, SUMMARY)
                    if earlier and 'ownerFrame' in earlier[-1] and _integer(row, 'ownerFrame') <= _integer(earlier[-1], 'ownerFrame'):
                        raise ValueError('fresh summary owner frame did not advance')
                    if pending:
                        attempt['status'] = 'WAIT_PENDING_NATIVE_LEAVE'
                    else:
                        attempt['status'] = 'FRESH_ADMITTED'
                        evidence = dict(schema='causal-summary-acquisition-v1', clock='perf_counter_ns',
                                        startedNs=start, deadlineNs=end, finishedNs=_clock_ns(),
                                        attempts=attempts, summary=dict(row), dispatched=False)
                        check()
                        return sample, evidence
        except AcquisitionFailed:
            raise
        except Exception as exc:
            raise AcquisitionFailed(str(exc), attempts, sample) from exc
        check()
        cancel.wait(min(0.05, max(0, end - _clock_ns()) / 1_000_000_000))
    raise AcquisitionFailed('summary acquisition attempt cap', attempts, sample)
