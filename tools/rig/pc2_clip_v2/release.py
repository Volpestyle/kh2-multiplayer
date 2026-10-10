"""Read-only release admission; native Acquire remains authoritative."""
import math


def phase_deadline(*, seconds, started, join_deadline, cleanup_reserve=60.):
    if type(seconds) is not int or seconds not in (3,16):
        raise ValueError('exact3s90 or16s480 mode required')
    if any(type(x) is not float or not math.isfinite(x) for x in (started,join_deadline,cleanup_reserve)) or cleanup_reserve<60.:
        raise ValueError('original finite QPC JOIN budget and >=60s cleanup reserve required')
    deadline=min(started+(45 if seconds==3 else 90),join_deadline-cleanup_reserve)
    if deadline<=started:raise ValueError('original JOIN cleanup reserve exhausted')
    return deadline


def validate_release(row, *, helper, target, seconds, raw_pattern, request,
                     worker_started_ns, now_ns, sample):
    """Original helper journal + observed full raw completion, not doneSeq alone.

    This records lease release, not helper exit, video success or future coverage.
    Split-mode second capture also requires the canonical capture receipt and
    helper absence. Encoding starts only after independently verified run closure.
    """
    if type(seconds) is not int or seconds not in (3,16):raise ValueError('mode refused')
    frames=seconds*30
    if row.get('schema')!='pc2-capture-release-v1':raise ValueError('release schema refused')
    if row['helper']!={k:helper[k] for k in ('pid','creationTicks')}:raise ValueError('original helper differs')
    if any(type(row['helper'][k]) is not int or row['helper'][k]<=0 for k in ('pid','creationTicks')):raise ValueError('helper types refused')
    if any(type(row['target'].get(k)) is not int or row['target'][k]!=v for k,v in target.items()):raise ValueError('original target differs')
    if type(row['seconds']) is not int or type(row['frames']) is not int or row['seconds']!=seconds or row['frames']!=frames or row['output']!=raw_pattern:raise ValueError('mode/path differs')
    cap=row['capture']
    if type(request) is not int or request<=0:raise ValueError('original observed request required')
    for key,value in (('expectedRequestSeq',request),('doneSeq',request),('nativeStatus',0),('framesWritten',frames)):
        if type(cap.get(key)) is not int or cap[key]!=value:raise ValueError('native completion differs')
    if cap.get('captureLeaseReleased') is not True:raise ValueError('lease release unproven')
    if any(type(cap.get(k)) is not int or cap[k]<=0 for k in ('startedNs','finishedNs')) or not worker_started_ns<=cap['startedNs']<cap['finishedNs']<=now_ns:raise ValueError('capture chronology refused')
    if cap['finishedNs']-cap['startedNs']>(seconds+7)*1_000_000_000:raise ValueError('capture acquisition bound exceeded')
    if sample.get('request')!=request or sample.get('completeFrames')!=frames or sample.get('recording') is not False or sample.get('output')!=raw_pattern:raise ValueError('early/draining/raw completion refused')
    return row
