"""Post-closure AC2 check; AC3 auditor and native coverage are unchanged.

A qualified native hit is necessary, never sufficient for visual consistency.
Human paired-frame review is explicit and hashes the exact inspected media.
"""
import hashlib
from pathlib import Path
from .profile import need
from . import health

def sha(path):
    with Path(path).open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()

def canonical_record(record, original):
    need(record['ok'] is True and record['identityBefore']==record['identityAfter']==record['session']==original,'canonical read identity/qualification mismatch')
    need(record['before']==record['after'],'canonical read straddled; must reacquire')
    row=record['after']
    need(row['actor']==row['head'] and (row['objectId'],row['objectType'],row['team'])==(84,0,1) and health.LOW_HP<row['hp']<=row['maxHp'],'canonical local victim unavailable/low')
    need(0<=record['readFinishedNs']-record['readStartedNs']<=health.MAX_READ_NS,'canonical chronology invalid')
    need({x['name']:(x['actual'],x['expected']) for x in record['flags']}=={name:(expected,expected) for name,_,_,expected in health.FLAGS},'canonical native safety flags missing/mismatched')
    return row

def paired_hp(before, after, original, connection):
    """Original canonical friend and authenticated host puppet, both HP endpoints.

    Each endpoint is a profile sample+CanonicalHealth receipt. Host puppet pose
    HP is read from avatarctl's provenance-bound bridge, not guessed entities.
    """
    need(before['friend']['canonical']['readFinishedNs']<=after['friend']['canonical']['readStartedNs'],'friend HP endpoint order invalid')
    need(before['host']['canonical']['readFinishedNs']<=after['host']['canonical']['readStartedNs'],'host HP endpoint order invalid')
    values={}
    for role in ('host','friend'):
        values[role]=[]
        for endpoint in (before,after):
            record=endpoint[role];sample=record['sample'];canonical=record['canonical']
            need(sample['ownedBefore']==sample['ownedAfter']==canonical['session']==original[role], 'HP endpoint original target mismatch')
            canonical_record(canonical,original[role])
            health.crosscheck_sample(sample,canonical)
            if role=='friend':
                values[role].append(canonical['after']['hp']);continue
            observation=sample['avatarObserve']
            need(observation['ok'] is True and observation['readOnly'] is True and observation['processId']==original['host']['pid'],'host read-only observer mismatch')
            need(len(observation['samples'])==1,'one host bridge bracket required')
            row=observation['samples'][0];world=row['worldAfter']
            need(row['complete'] is True and row['identityStable'] is True and row['worldBefore']==world,'host bridge identity changed')
            need(world['localSlot']==0 and world['connectionIds'][1]==connection,'original friend roster binding mismatch')
            puppets=[x['value'] for x in row['puppets'] if x['readAvailable'] and x['value']['active']==1 and x['value']['provenance']['ownerConnectionId']==connection]
            need(len(puppets)==1,'unique friend puppet unavailable')
            puppet=puppets[0];provenance=puppet['provenance']
            need(puppet['provenanceMatchesWorld'] is True and provenance['generation']==world['generation'] and provenance['localSlot']==0 and provenance['localConnectionId']==provenance['hostConnectionId']==world['connectionIds'][0],'puppet provenance mismatch')
            need(puppet['pose']['finite'] is True and [puppet['pose'][k] for k in ('worldId','roomId')]==[5,6],'puppet room/finite mismatch')
            need(puppet['pose']['maxHp']==endpoint['friend']['canonical']['after']['maxHp'],'paired maximum HP differs')
            values[role].append(puppet['pose']['hp'])
    need(values['host']==values['friend'] and values['friend'][1]<values['friend'][0], 'paired HP before/after does not agree')
    return values

def qualify_incoming(friend_log, before, after, original, original_scope, reviews, media_root, trace_audit):
    audit=trace_audit.audit_text(friend_log)['nativeHitTrace']
    need(audit['complete'],'candidate native-hit audit incomplete; retain issues, no fallback')
    hp=paired_hp(before,after,original,int(original_scope['connection']))
    candidates=[]
    for event in audit['events']:
        if not event['ordinaryIncomingClientLocalWitness']:continue
        c=event['contexts'][0];v0,v1=event['victims']
        if c['location']!=[5,6,0,1,1,0]:continue
        canonical=before['friend']['canonical']['after']
        if not all(c[k]==int(original_scope[src]) for k,src in [('connectionId','connection'),('generation','generation'),('epoch','epoch'),('loadSerial','load'),('transitionSerial','transition')]):continue
        if [v0['hp'],v1['hp']]!=hp['friend']:continue
        if any(v[k]!=canonical[k] for v in (v0,v1) for k in ('actor','status','objentry','maxHp')):continue
        candidates.append(event)
    need(len(candidates)==1,'unique original native enemy-to-friend HP event missing')
    event=candidates[0];root=Path(media_root).resolve()
    need(set(reviews)=={'host','friend'},'BOTH renderer reviews required')
    for role,row in reviews.items():
        file=(root/row['path']).resolve()
        need(file.is_relative_to(root) and file.is_file() and sha(file)==row['sha256'],'reviewed media missing/changed')
        need(row['target']==original[role] and row['eventSequence']==event['sequence'],'media original target/event mismatch')
        need(row['reviewer'] and row['gameOnly'] is True and row['allFramesPrivacyChecked'] is True and row['visibleEnemyImpact'] is True and row['coverageQualified'] is True,'paired impact/coverage/privacy review unavailable')
        need(type(row['beforeFrame']) is int and type(row['impactFrame']) is int and type(row['afterFrame']) is int and 0<=row['beforeFrame']<row['impactFrame']<row['afterFrame'],'review frame order missing')
    return {'criterion':2,'status':'QUALIFIED_RECORDED_HIT','eventSequence':event['sequence'],
            'hpBeforeAfter':hp,'nativeWitness':event,'pairedReviews':reviews,
            'acceptance':False,'criterion3':'NOT_QUALIFIED'}
