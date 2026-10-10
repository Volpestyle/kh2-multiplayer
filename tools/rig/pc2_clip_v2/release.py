"""Original-window release admission. It never proves final media closure."""
import copy
import struct
import threading
from pathlib import Path


def validate_release(row, *, helper, target, root, started_ns, deadline_ns, now_ns):
    if type(row) is not dict or set(row) != {'schema', 'helper', 'target', 'seconds', 'frames',
                                            'output', 'releasedNs', 'deadlineNs', 'capture'}:
        raise ValueError('exact release journal shape required')
    if row['schema'] != 'pc2-capture-release-v1':
        raise ValueError('release schema refused')
    for key, expected in (('helper', {k: helper[k] for k in ('pid', 'creationTicks')}),
                          ('target', target)):
        actual = row[key]
        if type(actual) is not dict or actual != expected or set(actual) != set(expected):
            raise ValueError('original release identity differs')
        if any(type(v) is not int or v <= 0 for v in actual.values()):
            raise ValueError('release identity types refused')
    if type(row['seconds']) is not int or row['seconds'] != 16 or type(row['frames']) is not int or row['frames'] != 480:
        raise ValueError('release mode/count refused')
    root = Path(root)
    # Each record supplies the exact original per-peer raw root below.
    if root.name not in ('hb_arrival_peer0_raw', 'hb_arrival_peer1_raw'):
        raise ValueError('exact original raw directory required')
    pattern = str(root / 'f_%05u.bmp')
    if row['output'] != pattern or root.is_symlink() or root.resolve() != root:
        raise ValueError('release raw path differs')
    if type(row['deadlineNs']) is not int or row['deadlineNs'] != deadline_ns:
        raise ValueError('release original deadline differs')
    cap = row['capture']
    fields = {'expectedRequestSeq', 'doneSeq', 'nativeStatus', 'framesWritten',
              'startedNs', 'finishedNs', 'captureLeaseReleased'}
    if type(cap) is not dict or set(cap) != fields:
        raise ValueError('exact capture release shape required')
    if any(type(cap[k]) is not int for k in fields - {'captureLeaseReleased'}):
        raise ValueError('typed native completion required')
    if (cap['expectedRequestSeq'] <= 0 or cap['doneSeq'] != cap['expectedRequestSeq']
            or cap['nativeStatus'] != 0 or cap['framesWritten'] != 480
            or cap['captureLeaseReleased'] is not True):
        raise ValueError('native release completion refused')
    if (type(row['releasedNs']) is not int or not
            (started_ns <= cap['startedNs'] < cap['finishedNs'] <= row['releasedNs'] < deadline_ns and row['releasedNs'] <= now_ns)):
        raise ValueError('release chronology refused')
    if cap['finishedNs'] - cap['startedNs'] > 23_000_000_000:
        raise ValueError('acquisition cap exceeded')
    # doneSeq is insufficient: all exact native BMP files must be complete.
    if {p.name for p in root.iterdir()} != {f'f_{i:05}.bmp' for i in range(480)}:
        raise ValueError('complete original raw inventory required')
    for i in range(480):
        path = root / f'f_{i:05}.bmp'
        if path.is_symlink():
            raise ValueError('raw symlink refused')
        with path.open('rb') as stream:
            header = stream.read(54)
        if len(header) != 54 or header[:2] != b'BM':
            raise ValueError('incomplete raw BMP')
        size, offset = struct.unpack_from('<I', header, 2)[0], struct.unpack_from('<I', header, 10)[0]
        dib, width, height, planes, bpp, compression = struct.unpack_from('<IiiHHI', header, 14)
        if not (dib == 40 and width > 0 and height < 0 and planes == 1 and bpp == 32
                and compression == 0 and offset == 54 and size == 54 + width * (-height) * 4
                and path.stat().st_size == size):
            raise ValueError('incomplete raw BMP')
    return copy.deepcopy(row)


class WindowSequence:
    """At most two windows per original local peer, on one shared product object."""
    def __init__(self, read, helper_live):
        if not callable(helper_live):raise ValueError("original helper observation required")
        self.read = read
        self.helper_live = helper_live
        self.lock = threading.RLock()
        self.history = {}

    def reserve(self, instance, target, root, started_ns, deadline_ns, join_deadline_ns):
        with self.lock:
            previous = self.history.setdefault(instance, [])
            if len(previous) >= 2:
                raise ValueError('only two original long windows admitted')
            if previous:
                old = previous[-1]
                if old['target'] != target or old['joinDeadlineNs'] != join_deadline_ns or old['root'] == root:
                    raise ValueError('original sequence binding changed')
                if old['state'] == 'FAILED' and not old['closed']:
                    raise ValueError('previous failed media closure UNKNOWN')
                if old['state'] == 'RUNNING' and started_ns >= old['deadlineNs']:
                    raise ValueError('previous worker deadline expired without closure')
                if old['helper'] is None:
                    raise ValueError('previous original helper unavailable')
                self.released(old, started_ns)
                if old['state']=='RUNNING' and not self.helper_live(old['helper']):
                    raise ValueError("original helper exited without canonical worker closure")
            record = dict(instance=instance, target=copy.deepcopy(target), root=root, startedNs=started_ns,
                          deadlineNs=deadline_ns, joinDeadlineNs=join_deadline_ns,
                          helper=None, state='RUNNING', closed=False)
            previous.append(record)
            return record

    def released(self, record, now_ns):
        root = Path(record['root'])
        stem = 'hb_arrival_peer' + str(record['instance']) if 'instance' in record else root.name
        return validate_release(self.read(root / (stem + '_capture_released.json')),
                                helper=record['helper'], target=record['target'],
                                root=root / (stem + '_raw'), started_ns=record['startedNs'],
                                deadline_ns=record['deadlineNs'], now_ns=now_ns)

    def update(self, record, **values):
        with self.lock:
            record.update(values)
