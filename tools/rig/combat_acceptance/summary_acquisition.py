"""Authenticate owner summaries spanning a bracket gap; never rewrite raw logs.

Port of practice-summary-gap-fix-01's immutable baseline/frame/scope rule.
The complete fresh native bracket and the original five-body Admission remain
mandatory. This module grants no input and never resets Admission.
"""
import copy
from . import population as p

SUMMARY='[combat-causal] summary '

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
    first=rows[-1];frame=p.number(first,'ownerFrame')
    all_rows=p.records(after,SUMMARY)
    p.need(not any(p.number(r,k) for r in all_rows for k in ('retired','dropped','loss')),
           'summary observer retired/lost')
    receipts=p.receipt_rows(after)
    p.need(not any(p.number(r,'kind')==11 or p.number(r,'loss') for r in receipts),
           'summary native observer retired/lost')
    fresh=[r for r in p.records(after[len(base):],SUMMARY) if p.number(r,'ownerFrame')>frame]
    if not fresh:return None
    row=fresh[-1]
    p.need(p.number(row,'admitted') in (0,1),'invalid summary admitted value')
    if p.number(row,'admitted')==0:return None
    admitted=[r for r in receipts if p.number(r,'kind')==0]
    p.need(len(admitted)==5 and all(p.number(r,'qualified')==1 and p.number(r,'reason')==0 for r in admitted),
           'gap summary missing five original admissions')
    for key in ('generation','delivery','connection','epoch','load','transition','role','slot','location'):
        p.need(row[key]==first[key] and all(r[key]==row[key] for r in admitted),
               'gap summary differs from original scope/admissions: '+key)
    p.need(p.number(row,'ownerAvailable')==1 and p.number(row,'ownerFrame')>=max(p.number(r,'frame') for r in admitted),
           'gap summary owner unavailable/behind admissions')
    pending=p.number(row,'pendingNativeLeave') if 'pendingNativeLeave' in row else 0
    p.need(0<=pending<=5,'invalid pending native leave')
    if pending:return None
    return dict(schema='source-summary-gap-v1',baseline=copy.deepcopy(baseline),summary=dict(row))

def verify(sample):
    proof=sample['summaryAcquisition']
    p.need(proof.get('schema')=='source-summary-gap-v1','unknown summary acquisition schema')
    expected=qualify(sample,proof['baseline'])
    p.need(expected is not None and proof==expected,'summary acquisition proof disagrees')
    return True
