"""A-03 raw replay controls: local chains only; no coverage certificate."""
import gzip
import hashlib
import json
import re
import struct
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_combat_event_replay import edit
import combat_event_replay as replay

FIXTURE=Path(__file__).parent/'fixtures/combat_event_replay/a03'
HOST=gzip.decompress((FIXTURE/'host.log.gz').read_bytes()).decode('utf8')
CLIENT=gzip.decompress((FIXTURE/'client.log.gz').read_bytes()).decode('utf8')

def event(result, serial):
    return next(e for e in result['events'] if e['hostSerial']==serial)

def duplicate(text,marker):
    line,=[l for l in text.splitlines(keepends=True) if marker in l]
    return text.replace(line,line+line)

class A03ReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls): cls.baseline=replay.replay(HOST,CLIENT)

    def test_exact_evidence(self):
        manifest=json.loads((FIXTURE/'manifest.json').read_text())
        for role in ('host','client'):
            raw=gzip.decompress((FIXTURE/(role+'.log.gz')).read_bytes())
            self.assertEqual(len(raw),manifest['files'][role]['bytes'])
            self.assertEqual(hashlib.sha256(raw).hexdigest(),manifest['files'][role]['sha256'])

    def test_two_claims_two_host_hits_companions_and_open_tail(self):
        r=self.baseline
        self.assertEqual(sum(e['linked'] for e in r['events']),10)
        self.assertEqual([e['hostSerial'] for e in r['events'] if e['linked'] and e['native']['sourceKind']=='client_canonical_claim'],[117,128])
        self.assertEqual([e['event'] for e in r['events'] if e['linked'] and e['native']['sourceKind']=='host_canonical_player'],[3,7])
        self.assertEqual([e['event'] for e in r['events'] if e['linked'] and e['native']['sourceKind']=='host_companion'],[1,5,9,11,13,15])
        self.assertEqual([(e['native']['claimSequence'],e['hpSequence'],e['friendStoreSerial']) for e in r['events'] if e['linked'] and e['native']['sourceKind']=='client_canonical_claim'],[(1,28,245),(2,30,269)])
        self.assertEqual(event(r,306)['refusal'],'missing_or_duplicate_event_publication')
        self.assertEqual(r['status'],'UNRESOLVED')
        self.assertEqual(r['wholeGraph'],replay.causal.audit_texts(HOST,CLIENT))
        self.assertEqual(r['wholeRunStatus'],'FAIL')
        self.assertFalse(r['acceptance']);self.assertFalse(r['coverageQualified']);self.assertFalse(r['exactlyOneSourceProven'])
        self.assertEqual(len(r['receiptDispositions']),sum(1 for t in (HOST,CLIENT) for l in t.splitlines() if '[combat-causal] receipt ' in l))
        self.assertTrue(all(c['disposition']=='linked_claim' for c in r['claims']))

    def test_claim_native_policy_parent_owner_and_zero_mutants(self):
        cases=[
            ('[hittrace] event schema=1 seq=1 ','parent',99),
            ('[hittrace] event schema=1 seq=1 ','ownerThread',0),
            ('[hittrace] event schema=1 seq=1 ','callerRva','3D613C'),
            ('[hittrace] context schema=1 seq=1 phase=1 ','loadSerial',99),
            ('[damagepolicy] event schema=1 seq=1 ','reason',11),
            ('[damagepolicy] event schema=1 seq=1 ','role',1),
            ('[damagepolicy] event schema=1 seq=1 ','claimQueued',0),
            ('[damagepolicy] event schema=1 seq=1 ','revalidationPassed',0),
            ('[damagepolicy] event schema=1 seq=1 ','zeroAttempted',0),
            ('[damagepolicy] input schema=1 seq=1 phase=0 ','owner','1234'),
            ('[hittrace] input schema=1 seq=1 phase=0 ','owner','1234'),
            ('[hittrace] input schema=1 seq=1 phase=1 ','damage',3),
            ('[hittrace] input schema=1 seq=1 phase=1 ','syncDrop',0),
            ('[hittrace] child schema=1 seq=1 ','applySeq',3),
            ('[hittrace] child schema=1 seq=1 ','delta',-3),
            ('[hittrace] actor schema=1 seq=1 phase=1 subject=2 ','hp',15),
            ('[hittrace] actor schema=1 seq=1 phase=0 subject=1 ','type',1),
        ]
        for marker,key,value in cases:
            with self.subTest(marker=marker,key=key):
                e=event(replay.replay(HOST,edit(CLIENT,marker,key,value)),117)
                self.assertFalse(e['linked']);self.assertTrue(e['refusal'].startswith('missing_or_ambiguous_native_policy_witness:'))

    def test_claim_packet_and_authority_mutants(self):
        cases=[
            ('client',226,'damage',4),('client',226,'attackId',999),('client',226,'enqueued',0),
            ('client',226,'claimConnection',99),('client',226,'payloadFnv64',0),
            ('client',227,'afterHp',15),('client',227,'actor','1234'),
            ('host',117,'attempted',0),('host',117,'damage',4),('host',117,'qualified',0),
            ('host',118,'hpSequence',29),('client',240,'hpSequence',29),('client',245,'afterHp',14),
            ('client',245,'actor','1234'),('client',245,'returned',0),
        ]
        for role,serial,key,value in cases:
            with self.subTest(role=role,serial=serial,key=key):
                h,c=(edit(HOST,f'receipt schema=1 serial={serial} ',key,value),CLIENT) if role=='host' else (HOST,edit(CLIENT,f'receipt schema=1 serial={serial} ',key,value))
                e=event(replay.replay(h,c),117)
                self.assertFalse(e['linked']);self.assertIn('refusal',e)

    def test_duplicate_claim_apply_receive_and_store(self):
        for role,serial in [('client',226),('host',117),('host',118),('client',240),('client',245)]:
            with self.subTest(role=role,serial=serial):
                marker=f'receipt schema=1 serial={serial} '
                h,c=(duplicate(HOST,marker),CLIENT) if role=='host' else (HOST,duplicate(CLIENT,marker))
                self.assertFalse(event(replay.replay(h,c),117)['linked'])

    def test_changed_batch_target_and_multi_source_store(self):
        # Same HPseq batch, an unchanged sibling target is substituted. No filtering it out.
        for role,serial in [('host',119),('client',241)]:
            with self.subTest(role=role):
                h,c=(edit(HOST,f'receipt schema=1 serial={serial} ','netId',99),CLIENT) if role=='host' else (HOST,edit(CLIENT,f'receipt schema=1 serial={serial} ','netId',99))
                self.assertFalse(event(replay.replay(h,c),117)['linked'])
        h=edit(HOST,'receipt schema=1 serial=118 ','causeCount',2)
        line,=[l for l in h.splitlines(keepends=True) if 'association schema=1 serial=118 ' in l]
        h=h.replace(line,line+line.replace('index=0','index=1').replace('event=9223372036854775925','event=1'))
        self.assertFalse(event(replay.replay(h,CLIENT),117)['linked'])

    def test_typed_incoming_and_noop_keep_original_graph(self):
        r=self.baseline
        self.assertEqual([(i['serial'],i['disposition']) for i in r['incoming']],[(450,'recorded_incoming_player_damage'),(528,'recorded_incoming_noop_already_applied')])
        self.assertTrue(all(i['linked'] for i in r['incoming']))
        self.assertIn('unadmitted_client_native_delta',{i['code'] for i in r['wholeGraph']['issues']})
        self.assertEqual(sum(p['disposition']=='noop_unmatched' for p in r['publications']),5)
        self.assertTrue(all(p['transportDisposition']=='NOT_ESTABLISHED' for p in r['publications'] if p['disposition']=='noop_unmatched'))

    def test_forged_incoming_refuses(self):
        cases=[
            ('[hittrace] event schema=1 seq=5 ','parent',99),
            ('[hittrace] input schema=1 seq=5 phase=0 ','owner','1234'),
            ('[damagepolicy] event schema=1 seq=5 ','sourceClass',1),
            ('[damagepolicy] event schema=1 seq=5 ','supported',0),
            ('[damagepolicy] actor schema=1 seq=5 phase=0 subject=0 ','actor','1234'),
            ('[damagepolicy] context schema=1 seq=5 phase=0 ','connectionId',99),
            ('receipt schema=1 serial=450 ','beforeHp',25),
            ('receipt schema=1 serial=450 ','load',99),
            ('[hittrace] input schema=1 seq=8 phase=0 ','owner','1234'),
            ('[hittrace] actor schema=1 seq=8 phase=1 subject=0 ','hp',15),
            ('[damagepolicy] event schema=1 seq=8 ','reason',10),
        ]
        for marker,key,value in cases:
            with self.subTest(marker=marker,key=key):
                r=replay.replay(HOST,edit(CLIENT,marker,key,value))
                i=next(i for i in r['incoming'] if i['serial']==(528 if 'seq=8 ' in marker else 450))
                self.assertFalse(i['linked']);self.assertTrue(i['disposition'].startswith('unresolved'))

    def test_forged_consumer_header_refuses_link(self):
        for key,value in [('outcome',2),('admittedEpoch',99),('originalBytes',1),('wireType',24)]:
            with self.subTest(key=key):
                c=edit(CLIENT,'consumer schema=1 serial=239 ',key,value)
                e=event(replay.replay(HOST,c),117)
                self.assertFalse(e['linked']);self.assertEqual(e['refusal'],'invalid_event_receipt')

    def test_typed_rejection_is_retained_not_promoted(self):
        # Change one actual accepted consumer header to a rejection; retain its other target rows.
        c=edit(CLIENT,'receipt schema=1 serial=239 ','kind',12)
        r=replay.replay(HOST,c)
        d=next(d for d in r['receiptDispositions'] if d['peer']=='client' and d['serial']==239)
        self.assertEqual(d['disposition'],'recorded_consumer_rejection')
        self.assertTrue(d['receiptIssues'])
        self.assertFalse(event(r,117)['linked'])
        self.assertFalse(r['acceptance'])

    def test_unterminated_and_duplicate_native_records_refuse(self):
        line,=[l for l in CLIENT.splitlines(keepends=True) if '[hittrace] event schema=1 seq=1 ' in l]
        c=CLIENT.replace(line,'')+line.rstrip('\r\n')
        with self.assertRaisesRegex(ValueError,'malformed_or_unterminated'): replay.replay(HOST,c)
        c=duplicate(CLIENT,'[hittrace] child schema=1 seq=1 ')
        self.assertFalse(event(replay.replay(HOST,c),117)['linked'])
        for role,serial in (('host',101),('client',117)):
            marker='[hittrace] event schema=1 seq=1 '
            h,c=(duplicate(HOST,marker),CLIENT) if role=='host' else (HOST,duplicate(CLIENT,marker))
            self.assertFalse(event(replay.replay(h,c),serial)['linked'])
        for seq, serial in ((5,450),(8,528)):
            c=duplicate(CLIENT,f'[hittrace] actor schema=1 seq={seq} phase=0 subject=0 ')
            i=next(i for i in replay.replay(HOST,c)['incoming'] if i['serial']==serial)
            self.assertFalse(i['linked']);self.assertEqual(i['refusal'],'incoming_record_shape_or_order')


    def test_wrong_payload_even_with_correct_checksum_refuses(self):
        rows=HOST.splitlines(keepends=True)
        line,=[l for l in rows if 'receipt schema=1 serial=118 ' in l]
        payload=bytes.fromhex(re.search(r'(?<!\S)payload=(\S+)',line)[1])
        changed=bytearray(payload);struct.pack_into('<i',changed,19,14)
        h=HOST.replace('payload='+payload.hex(), 'payload='+changed.hex()).replace('payloadFnv64='+str(replay.causal.fnv(payload)), 'payloadFnv64='+str(replay.causal.fnv(changed)))
        self.assertFalse(event(replay.replay(h,CLIENT),117)['linked'])

    def test_coherent_reused_target_still_refuses_original_admission(self):
        # Rewrite both current ledger edges AND their raw/native-policy roots, leaving original admission intact.
        actor='7FF6D2F17550'
        changed='12345678'
        rows=[]
        for line in HOST.splitlines(keepends=True):
            if ('receipt schema=1 serial=101 ' in line or 'receipt schema=1 serial=102 ' in line or
                    ((' [hittrace] ' in ' '+line or ' [damagepolicy] ' in ' '+line) and 'seq=1 ' in line)):
                line=line.replace('actor='+actor,'actor='+changed)
            rows.append(line)
        r=replay.replay(''.join(rows),CLIENT)
        self.assertFalse(event(r,101)['linked'])
        self.assertIn('native',event(r,101))
        self.assertEqual(event(r,101)['refusal'],'invalid_event_receipt')

    def test_valid_rejection_and_its_raw_reason_remain(self):
        c=edit(CLIENT,'receipt schema=1 serial=239 ','kind',12)
        c=edit(c,'receipt schema=1 serial=239 ','reason',21)
        c=edit(c,'consumer schema=1 serial=239 ','reason',21)
        c=edit(c,'consumer schema=1 serial=239 ','outcome',2)
        r=replay.replay(HOST,c)
        d=next(d for d in r['receiptDispositions'] if d['peer']=='client' and d['serial']==239)
        self.assertEqual((d['disposition'],d['reason']),('recorded_consumer_rejection',21))
        self.assertEqual(d['fields']['kind'],12)
        self.assertNotIn('invalid_consumer_supplement',{i['code'] for i in d['receiptIssues']})
        self.assertFalse(event(r,117)['linked'])
        self.assertEqual(len(r['receiptDispositions']),len(self.baseline['receiptDispositions']))

if __name__=='__main__': unittest.main()
