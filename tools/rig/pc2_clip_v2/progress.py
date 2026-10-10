"""Narrow observation of clip04's current 90-frame request. Never writes a channel."""
from pathlib import Path
import struct
import threading
import time
from types import MappingProxyType

from .channel import snapshot


def complete_bmp(path, not_before_ns):
    """A partial or inaccessible file is not recording progress."""
    try:
        if path.is_symlink():
            return False
        stat = path.stat()
        if stat.st_mtime_ns < not_before_ns:
            return False
        with path.open('rb') as stream:
            header = stream.read(54)
        if len(header) != 54 or header[:2] != b'BM':
            return False
        size, offset = struct.unpack_from('<I', header, 2)[0], struct.unpack_from('<I', header, 10)[0]
        dib, width, height, planes, bpp, compression = struct.unpack_from('<IiiHHI', header, 14)
        return (dib == 40 and width > 0 and height < 0 and planes == 1 and bpp == 32
                and compression == 0 and offset == 54
                and size == 54 + width * (-height) * 4 and stat.st_size == size)
    except OSError:
        return False


class RecordingProgress:
    def __init__(self, *, pid, run_id, phase, role, target, output, cancel, deadline, target_current,
                 max_progress_age_ns, snapshot_fn=snapshot, expected_frames=90):
        if type(pid) is not int or pid <= 0 or not callable(target_current):
            raise ValueError('original local owned target and identity callback required')
        if any(type(value) is not str or not value for value in (run_id, phase, role)):
            raise ValueError('immutable run, phase and role required')
        if (type(target) is not dict or set(target) != {'pid', 'creationTicks', 'moduleBase'}
                or any(type(value) is not int or value <= 0 for value in target.values())
                or target['pid'] != pid):
            raise ValueError('initial owned PID/creation/module binding required')
        self._binding = MappingProxyType(dict(run=run_id, phase=phase, role=role))
        self._target = MappingProxyType(dict(target))
        if not isinstance(cancel, threading.Event):
            raise ValueError('original cancellation Event required')
        if type(deadline) is not float or not 0 < deadline < float('inf'):
            raise ValueError('finite local QPC deadline required')
        if type(max_progress_age_ns) is not int or max_progress_age_ns <= 0:
            raise ValueError('explicit progress freshness budget required')
        output = Path(output)
        if not output.is_absolute() or output.suffix != '.mp4':
            raise ValueError('absolute exact MP4 output required')
        self.output = output.resolve()
        self.raw_dir = self.output.with_name(self.output.stem + '_raw')
        if self.output.exists() or self.raw_dir.exists() or not self.output.parent.is_dir():
            raise ValueError('fresh output and raw directory required')
        self.pattern = str(self.raw_dir / 'f_%05u.bmp')
        self.pid, self.cancel, self.deadline = pid, cancel, deadline
        self.target_current, self.snapshot = target_current, snapshot_fn
        self.max_age = max_progress_age_ns
        self.started_ns, self.not_before_ns = time.perf_counter_ns(), time.time_ns()
        self._check()
        baseline = self.snapshot(pid)
        self._check()
        if (type(baseline.get('request')) is not int or
                not 0 <= baseline['request'] <= 0xffffffff or
                baseline['request'] != baseline.get('done')):
            raise ValueError('idle original capture baseline required')
        self.baseline = dict(baseline)
        self.request = (baseline['request'] + 1) & 0xffffffff
        if type(expected_frames) is not int or expected_frames not in (90,480):raise ValueError('exact90/480 frame mode required')
        self.expected_frames=expected_frames
        self.count, self.last_advance, self.advances, self.sequence = 0, None, 0, 0
        self._sample_lock = threading.Lock()

    def _check(self):
        if self.cancel.is_set() or time.perf_counter() >= self.deadline:
            raise RuntimeError('recording observation cancelled or deadline expired')
        if self.target_current() is not True:
            raise RuntimeError('original local target identity/liveness unavailable')

    @staticmethod
    def _bookend(row):
        return tuple(row.get(k) for k in ('request', 'done', 'output', 'frameCount', 'interval'))

    def sample(self):
        # The transport responder and local dispatcher may both request samples.
        # Serialize chronology so every returned sequence advances exactly once.
        with self._sample_lock:
            return self._sample()

    def _sample(self):
        sample_start = time.perf_counter_ns()
        self._check()
        row = self.snapshot(self.pid)
        self._check()
        self.sequence += 1
        if row['request'] == self.baseline['request'] and row['done'] == self.baseline['done']:
            if self.count or self._bookend(row) != self._bookend(self.baseline):
                raise RuntimeError('capture reverted or idle channel changed')
            return self._result(0, False, False, False, sample_start)
        if (row['request'] != self.request or type(row.get('frameCount')) is not int
                or row['frameCount'] != self.expected_frames or row.get('output') != self.pattern
                or type(row.get('interval')) is not int or row['interval'] <= 0
                or row['done'] not in (self.baseline['done'], self.request)):
            raise RuntimeError('unexpected capture request/count/path/bookend')
        if self.raw_dir.is_symlink() or self.raw_dir.resolve() != self.raw_dir:
            raise RuntimeError('raw directory binding changed')
        count = 0
        file_observed_ns = None
        while count < self.expected_frames and complete_bmp(self.raw_dir / ('f_%05d.bmp' % count), self.not_before_ns):
            file_observed_ns = time.perf_counter_ns()
            self._check()
            count += 1
        end = self.snapshot(self.pid)
        self._check()
        if self._bookend(end) != self._bookend(row):
            raise RuntimeError('capture changed during progress read')
        if count < self.count:
            raise RuntimeError('current-request complete frames regressed')
        now = time.perf_counter_ns()
        advanced = count > self.count
        if advanced:
            self.count, self.last_advance = count, file_observed_ns
            self.advances += 1
        completed = row['done'] == self.request
        if completed and (count != self.expected_frames or row.get('status') != 0):
            raise RuntimeError('native completion is incomplete or failed')
        recording = not completed and 0 < count < self.expected_frames
        ready = (recording and self.advances >= 2 and self.last_advance is not None
                 and now - self.last_advance <= self.max_age)
        return self._result(count, recording, advanced, ready, sample_start)

    def _result(self, count, recording, advanced, ready, sample_start):
        # No claimed future duration: source must challenge both roles before each
        # dispatch, budget locally, and authenticate final paired interval coverage.
        sample_end = time.perf_counter_ns()
        ready = (ready and self.last_advance is not None
                 and sample_end - self.last_advance <= self.max_age)
        return dict(schema='combat-recording-progress-v1', **self._binding,
                    target=dict(self._target), targetPid=self.pid,
                    request=self.request, output=self.pattern, frameCount=self.expected_frames,
                    completeFrames=count, observedNs=sample_end,
                    sampleStartNs=sample_start, sampleEndNs=sample_end,
                    lastAdvanceNs=self.last_advance, sequence=self.sequence,
                    recording=recording, advancing=advanced, ready=ready)
