"""Real filesystem, pure release-admission controls. No game/native calls."""
import copy
import json
from pathlib import Path
import struct
import sys

from .adapter import read
from .release import WindowSequence, validate_release


def run(out):
    out.mkdir(exist_ok=False)
    root = out / 'first'
    raw = root / 'hb_arrival_peer0_raw'
    raw.mkdir(parents=True)
    header = bytearray(54)
    struct.pack_into('<2sIHHI', header, 0, b'BM', 58, 0, 0, 54)
    struct.pack_into('<IiiHHIIiiII', header, 14, 40, 1, -1, 1, 32, 0, 4, 0, 0, 0, 0)
    for i in range(480):
        (raw / f'f_{i:05}.bmp').write_bytes(header + b'\x01\x02\x03\xff')
    target = dict(pid=101, creationTicks=202, moduleBase=303)
    helper = dict(pid=404, creationTicks=505)
    row = dict(schema='pc2-capture-release-v1', helper=helper, target=target,
               seconds=16, frames=480, output=str(raw / 'f_%05u.bmp'),
               releasedNs=300, deadlineNs=1000,
               capture=dict(expectedRequestSeq=7, doneSeq=7, nativeStatus=0,
                            framesWritten=480, startedNs=100, finishedNs=250,
                            captureLeaseReleased=True))
    args = dict(helper=helper, target=target, root=raw, started_ns=50,
                deadline_ns=1000, now_ns=400)
    validate_release(row, **args)
    checks = ['complete-original-release']

    def refused(label, call):
        try:
            call()
        except (ValueError, OSError, KeyError):
            checks.append(label)
        else:
            raise AssertionError('not refused: ' + label)

    for label, mutate in (
            ('foreign-helper', lambda r: r['helper'].update(pid=405)),
            ('foreign-target', lambda r: r['target'].update(moduleBase=304)),
            ('third-mode', lambda r: r.update(seconds=12)),
            ('wrong-count', lambda r: r.update(frames=90)),
            ('release-before-finish', lambda r: r.update(releasedNs=200)),
            ('boolean-release-time', lambda r: r.update(releasedNs=True)),
            ('future-release-time', lambda r: r.update(releasedNs=401)),
            ('wrong-deadline', lambda r: r.update(deadlineNs=1001)),
            ('unfinished-sequence', lambda r: r['capture'].update(doneSeq=6)),
            ('lease-not-released', lambda r: r['capture'].update(captureLeaseReleased=False)),
            ('boolean-count', lambda r: r['capture'].update(framesWritten=True))):
        bad = copy.deepcopy(row)
        mutate(bad)
        refused(label, lambda: validate_release(bad, **args))
    leaf = raw / 'f_00479.bmp'
    data = leaf.read_bytes()
    leaf.write_bytes(data[:-1])
    refused('partial-last-frame', lambda: validate_release(row, **args))
    leaf.write_bytes(data)
    live=[True]
    sequence = WindowSequence(read,lambda helper:live[0])
    first = sequence.reserve(0, target, root, 50, 1000, 10000)
    sequence.update(first, helper=helper)
    second_root = out / 'second'
    refused('missing-journal-no-second-window',
            lambda: sequence.reserve(0, target, second_root, 400, 1200, 10000))
    (root / 'hb_arrival_peer0_capture_released.json').write_text(json.dumps(row), encoding='utf-8')
    sequence.update(first, state='FAILED', closed=False)
    refused('failed-UNKNOWN-no-second-window',
            lambda: sequence.reserve(0, target, second_root, 400, 1200, 10000))
    sequence.update(first, state='RUNNING')
    live[0]=False
    refused('helper-exited-before-canonical-closure',lambda:sequence.reserve(0,target,second_root,400,1200,10000))
    live[0]=True
    sequence.reserve(0, target, second_root, 400, 1200, 10000)
    checks.append('released-first-encoder-may-still-run')
    refused('third-window', lambda: sequence.reserve(0, target, out / 'third', 500, 1300, 10000))
    # Execute the actual clip finally block: failed receipt publication cannot
    # leave an apparently RUNNING, releasable previous worker.
    import ast, inspect, textwrap
    from types import SimpleNamespace
    from .adapter import MediaProduct
    fn=ast.parse(textwrap.dedent(inspect.getsource(MediaProduct.clip))).body[0]
    final=next(n for n in fn.body if isinstance(n,ast.Try)).finalbody
    window={'state':'RUNNING','closed':True}
    holder=SimpleNamespace(long_windows=sequence,native=SimpleNamespace(close=lambda handle:None))
    exec(compile(ast.fix_missing_locations(ast.Module(body=final,type_ignores=[])),'actual-clip-finally','exec'),
         {'self':holder,'window':window,'event':None,'game':None})
    assert window=={'state':'FAILED','closed':False}
    checks.append('actual-finally-failed-journal-is-UNKNOWN')
    return dict(passed=len(checks), failed=0, checks=checks,
                nativeBuilt=False, game=False, limit='Synthetic journal/BMP fixtures; native destructor tests authored, unrun.')


if __name__ == '__main__':
    result = run(Path(sys.argv[1]).resolve())
    (Path(sys.argv[1]) / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result))
