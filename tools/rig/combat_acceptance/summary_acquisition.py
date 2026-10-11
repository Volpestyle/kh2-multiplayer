"""Authenticate owner summaries spanning a bracket gap; never rewrite raw logs.

Port of practice-summary-gap-fix-01's immutable baseline/frame/scope rule.
The complete fresh native bracket and the original five-body Admission remain
mandatory. This module grants no input and never resets Admission.
"""
import copy
from . import population as p

SUMMARY='[combat-causal] summary '

# These are exactly the scope fields emitted by CombatCausalTrace::Drain's
# summary serializer (no sessionSalt/roster fields are serialized in summaries).
SUMMARY_SCOPE=('generation','delivery','connection','epoch','load','transition','role','slot','location')

def suffix(base,after,admitted):
    """Validate EVERY serialized suffix row; never filter below the frame floor.

    Drain has a one-second throttle but no unique summary sequence. A stalled
    producer can legitimately repeat ownerFrame, including identical rows. Such
    rows provide no new coverage: refuse equal frames conservatively, without
    claiming corruption. Conflicting equal frames and reversed tails also refuse.
    """
    p.need(after.startswith(base),'summary suffix prefix replaced/truncated')
    original=p.records(base,SUMMARY)
    p.need(bool(original),'pre-action owner summary baseline missing')
    first=original[-1];previous=p.number(first,'ownerFrame')
    p.need(len(admitted)==5 and all(p.number(r,'qualified')==1 and p.number(r,'reason')==0 for r in admitted),
           'summary missing five original admissions')
    rows=p.records(after[len(base):],SUMMARY)
    # Even hidden/stale rows must agree with the original summary/admissions.
    for row in rows:
        for key in SUMMARY_SCOPE:
            p.need(row[key]==first[key] and all(r[key]==row[key] for r in admitted),
                   'summary suffix differs from original scope/admissions: '+key)
        frame=p.number(row,'ownerFrame')
        p.need(frame>previous,'summary suffix duplicate/reversed ownerFrame')
        previous=frame
        p.need(all(p.number(row,k)==0 for k in ('retired','dropped','loss')),
               'summary suffix observer retired/lost')
        p.need(p.number(row,'ownerAvailable')==1,'summary suffix owner unavailable')
        p.need(p.number(row,'admitted') in (0,1),'invalid summary admitted value')
        p.need(0<=int(row.get('pendingNativeLeave','0'))<=5,'invalid pending native leave')
    return rows[-1] if rows else None

def in_bracket(sample):
    """The direct path uses the same entire-suffix checks as gap replay."""
    after=sample['injectAfter']
    admitted=[r for r in p.receipt_rows(after) if p.number(r,'kind')==0]
    row=suffix(sample['injectBefore'],after,admitted)
    return bool(row and p.number(row,'admitted')==1 and
                p.number(row,'ownerFrame')>=max(p.number(r,'frame') for r in admitted) and
                int(row.get('pendingNativeLeave','0'))==0)

def qualify(sample, baseline):
    """Return replayable proof or None if no newer summary exists yet.

    All prefix, ownership, retirement and scope discrepancies are terminal.
    A non-admitted summary is not qualified, not a relaxed admission.
    """
    before,after=sample['injectBefore'],sample['injectAfter']
    p.need(after.startswith(before),'summary bracket prefix replaced/truncated')
    base=baseline['injectAfter']
    p.need(before.startswith(base),'summary baseline prefix replaced/truncated')
    p.need(sample['runtimeBefore'].startswith(baseline['runtimeAfter']) and
           sample['runtimeAfter'].startswith(sample['runtimeBefore']),'summary runtime prefix replaced/truncated')
    p.need(baseline['ownedBefore']==baseline['ownedAfter']==sample['ownedBefore']==sample['ownedAfter'],
           'summary baseline original identity changed')
    p.need(baseline['readFinishedNs']<=sample['readStartedNs']<sample['readFinishedNs'],
           'summary baseline/fresh bracket chronology invalid')
    rows=p.records(base,SUMMARY)
    p.need(bool(rows),'pre-action owner summary baseline missing')
    all_rows=p.records(after,SUMMARY)
    p.need(not any(p.number(r,k) for r in all_rows for k in ('retired','dropped','loss')),
           'summary observer retired/lost')
    receipts=p.receipt_rows(after)
    p.need(not any(p.number(r,'kind')==11 or p.number(r,'loss') for r in receipts),
           'summary native observer retired/lost')
    admitted=[r for r in receipts if p.number(r,'kind')==0]
    row=suffix(base,after,admitted)
    if row is None or p.number(row,'admitted')==0:return None
    p.need(p.number(row,'ownerFrame')>=max(p.number(r,'frame') for r in admitted),
           'gap summary owner behind admissions')
    if int(row.get('pendingNativeLeave','0')):return None
    return dict(schema='source-summary-gap-v1',baseline=copy.deepcopy(baseline),summary=dict(row))

def verify(sample):
    proof=sample['summaryAcquisition']
    p.need(proof.get('schema')=='source-summary-gap-v1','unknown summary acquisition schema')
    expected=qualify(sample,proof['baseline'])
    p.need(expected is not None and proof==expected,'summary acquisition proof disagrees')
    return True
