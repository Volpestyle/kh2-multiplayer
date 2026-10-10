"""Saved-log mutation controls; Python only, no native/process/network access."""
import contextlib
import copy
import importlib.util
import io
import json
from pathlib import Path
import re
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from combat_causal_serializer import CALLS, render

SPEC = importlib.util.spec_from_file_location("combat_causal_audit", ROOT / "tools/scenario/combat_causal_audit.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


def packet(tag, body):
    return struct.pack("<BH", tag, len(body)) + body


def hp(value, sequence=1):
    return packet(23, struct.pack("<IQHHii", 9, sequence, 1, 1, value, 100))


def row(peer, kind, **changes):
    client = peer == "client"
    r = dict(schema=1, serial=0, qpc=0, kind=kind, reason=12 if kind == 2 else 0,
             qualified=1, event=0, hpSequence=0, loss=0, generation=2, delivery=5 if client else 4,
             sessionSalt=55, connection=101 if client else 100, host=100, peer=101,
             hostDelivery=4, peerDelivery=5, epoch=9, load=4 if client else 3, transition=2,
             frame=10, role=2 if client else 1, slot=1 if client else 0, location=(5, 6, 0, 1, 1, 0),
             netId=1, objectId=302, type=4, actor=0x1100 if client else 0x1000,
             objentry=0x2000, status=0x3000, controller=0x4000, record=0x5000,
             targetHp=100, maxHp=100, claimConnection=0, claimSeq=0, attackId=0, damage=0,
             beforeHp=0, afterHp=0, requestedHp=0, attempted=0, returned=0, readback=0, enqueued=0,
             causeCount=0, payloadBytes=0, payloadFnv64=0, payload=b"", incarnationAuthority=0, causes=[])
    r.update(changes)
    return r


def finalize(rows):
    for serial, r in enumerate(rows, 1):
        r["serial"], r["qpc"] = serial, serial * 100
        r["causeCount"] = len(r["causes"])
        r["payloadBytes"] = len(r["payload"])
        r["payloadFnv64"] = AUDIT.fnv(r["payload"]) if r["payload"] else 0
    return rows


def accepted(receive):
    r = dict(receive)
    r.update(kind=13,qualified=0,reason=0,netId=0,objectId=0,type=0,actor=0,objentry=0,
             status=0,controller=0,record=0,targetHp=0,maxHp=0,event=0,
             beforeHp=0,afterHp=0,requestedHp=0,enqueued=0,expectedEpoch=9)
    return r


def fixture():
    peers = {}
    for peer in ("host", "client"):
        peers[peer] = [row(peer, 0, netId=n, actor=(0x1100 if peer == "client" else 0x1000) + n,
                           objentry=0x2000+n, status=0x3000+n, controller=0x4000+n, record=0x5000+n)
                       for n in range(1, 6)]
    h, c = peers["host"], peers["client"]
    def op(peer, kind, **changes):
        r = dict(peers[peer][0]); r.update(kind=kind, **changes); return r
    h.append(op("host", 1, event=1, beforeHp=100, afterHp=93, returned=1, readback=1))
    h.append(op("host", 4, event=1, hpSequence=1, beforeHp=100, afterHp=93, requestedHp=93,
                enqueued=1, payload=hp(93), causes=[1]))
    c.append(op("client", 5, hpSequence=1, beforeHp=100, afterHp=93, requestedHp=93, enqueued=1, payload=hp(93)))
    c.append(op("client", 6, hpSequence=1, beforeHp=100, afterHp=93, requestedHp=93, attempted=1, returned=1, readback=1))
    claim_key = dict(claimConnection=101, claimSeq=1, attackId=65, damage=6)
    raw_claim = packet(8, struct.pack("<IIHIQIi3fB", 9, 1, 1, 302, 101, 65, 6, 0., 0., 0., 1))
    # Real Claim is emitted inside Apply, before the corresponding Hit ends.
    c.append(op("client", 2, reason=12, event=7, enqueued=1, payload=raw_claim, **claim_key))
    c.append(op("client", 1, event=7, beforeHp=93, afterHp=93, returned=1, readback=1))
    event = (1 << 63) | 8
    h.append(op("host", 3, event=event, beforeHp=93, afterHp=87, attempted=1, returned=1, readback=1, **claim_key))
    h.append(op("host", 4, event=event, hpSequence=2, beforeHp=93, afterHp=87, requestedHp=87,
                enqueued=1, payload=hp(87, 2), causes=[event]))
    c.append(op("client", 5, hpSequence=2, beforeHp=93, afterHp=87, requestedHp=87, enqueued=1, payload=hp(87, 2)))
    c.append(op("client", 6, hpSequence=2, beforeHp=93, afterHp=87, requestedHp=87, attempted=1, returned=1, readback=1))
    h.append(op("host", 1, event=2, beforeHp=87, afterHp=0, returned=1, readback=1))
    death = packet(24, struct.pack("<IH", 9, 1))
    h.append(op("host", 7, event=2, beforeHp=87, afterHp=0, requestedHp=0, enqueued=1, payload=death, causes=[2]))
    c.append(op("client", 8, beforeHp=87, afterHp=0, enqueued=1, payload=death))
    c.append(op("client", 9, beforeHp=87, afterHp=0, attempted=1, returned=1, readback=1))
    expanded = []
    for r in c:
        if r['kind'] in (5,8):
            expanded.append(accepted(r))
        expanded.append(r)
    return finalize(h), finalize(expanded)


def audit(h, c):
    return AUDIT.audit_texts(render(h), render(c))


class CombatCausalAuditTest(unittest.TestCase):
    def test_real_serializer_contract(self):
        self.assertEqual(set(CALLS), {"receipt", "consumer", "association", "summary"})
        h, c = fixture()
        a = AUDIT.Auditor()
        parsed = AUDIT.parse(render(c), "client", a)
        self.assertFalse(a.issues)
        for original, result in zip(c, parsed):
            for field in AUDIT.RECEIPT:
                self.assertEqual(original[field], result[field], field)
        self.assertIn("nativeCoverageQualified=0 acceptance=0", render(h))

    def test_complete_graph_never_passes_coverage(self):
        result = audit(*fixture())
        self.assertEqual(result["status"], "INCONCLUSIVE")
        self.assertTrue(result["recordedGraphConsistent"], result["issues"])
        self.assertEqual(result["applicationCount"], 6)
        self.assertFalse(result["coverageQualified"])
        self.assertFalse(result["exactlyOneSourceProven"])
        self.assertFalse(result["acceptance"])

    def test_local_addresses_and_clocks_not_cross_peer(self):
        h, c = fixture()
        for r in c:
            r["qpc"] += 99999999
            for k in AUDIT.HEX:
                r[k] += 0xABC000
        self.assertTrue(audit(h, c)["recordedGraphConsistent"])

    def test_noop_readback(self):
        h, c = fixture()
        # An unchanged periodic HP publication/readback has no new source.
        h = h[:7]; c = c[:8]
        p = dict(h[-1]); p.update(event=1, hpSequence=2, beforeHp=93, afterHp=93, requestedHp=93, causes=[], payload=hp(93, 2))
        h.append(p)
        p = dict(c[6]); p.update(hpSequence=2, beforeHp=93, afterHp=93, requestedHp=93, payload=hp(93, 2)); c.extend([accepted(p),p])
        p = dict(c[7]); p.update(hpSequence=2, beforeHp=93, afterHp=93, requestedHp=93, attempted=0); c.append(p)
        self.assertTrue(audit(finalize(h), finalize(c))["recordedGraphConsistent"])

    def test_aggregate_is_not_called_duplicate_hit(self):
        h, c = fixture(); h=h[:6]; c=c[:5]
        second = dict(h[-1]); second.update(event=2, beforeHp=93, afterHp=87); h.append(second)
        p = dict(fixture()[0][6]); p.update(event=2, afterHp=87, requestedHp=87, payload=hp(87), causes=[1, 2]); h.append(p)
        receive = dict(fixture()[1][6]); receive.update(afterHp=87, requestedHp=87, payload=hp(87)); c.extend([accepted(receive),receive])
        apply = dict(fixture()[1][7]); apply.update(afterHp=87, requestedHp=87); c.append(apply)
        result = audit(finalize(h), finalize(c))
        codes = {i["code"] for i in result["issues"]}
        self.assertIn("multiple_sources_in_application", codes)
        self.assertNotIn("duplicate_source", codes)
        self.assertEqual(result["status"], "FAIL")

    def test_graph_mutations(self):
        expected_codes = {
            'missing_host_hit':'orphan_or_duplicate_publication_cause',
            'duplicate_host_event':'duplicate_source',
            'missing_claim':'orphan_or_duplicate_claim',
            'duplicate_claim':'orphan_or_duplicate_claim',
            'wrong_claim_key':'orphan_or_duplicate_claim',
            'missing_client_hit':'orphan_claim_hit',
            'claim_different_target':'orphan_claim_hit',
            'unattributed_host_hp':'unaccounted_host_delta',
            'host_no_delta':'unaccounted_host_delta',
            'host_no_readback':'unaccounted_host_delta',
            'host_not_attempted':'unattempted_host_apply',
            'source_namespace':'invalid_host_apply',
            'missing_publication_association':'orphan_or_reused_publication_sources',
            'duplicate_association':'duplicate_association',
            'foreign_source':'orphan_or_duplicate_publication_cause',
            'publication_before':'uncaused_publication_hp',
            'publication_unexplained_hp':'uncaused_publication_hp',
            'publication_wrong_wire_hp':'invalid_raw_packet',
            'claim_wrong_wire':'invalid_raw_packet',
            'receive_wrong_wire':'invalid_raw_packet',
            'missing_receive':'orphan_or_duplicate_application',
            'duplicate_apply':'orphan_or_duplicate_application',
            'apply_unknown_sequence':'orphan_or_duplicate_application',
            'apply_wrong_delta':'unaccounted_client_delta',
            'apply_wrong_before':'unaccounted_client_delta',
            'apply_missing_readback':'unaccounted_client_delta',
            'apply_missing_attempt':'unattempted_client_change',
            'death_without_source':'unqualified_terminal_cause',
            'death_wrong_event':'unqualified_terminal_cause',
            'death_raw_other_target':'invalid_raw_packet',
            'unexpected_host_apply_kind':'unexpected_role_operation',
        }
        mutations = [
            ("missing_host_hit", lambda h,c: h.pop(5)),
            ("duplicate_host_event", lambda h,c: h[9].update(event=1)),
            ("missing_claim", lambda h,c: c.pop(8)),
            ("duplicate_claim", lambda h,c: c.insert(9, dict(c[8]))),
            ("wrong_claim_key", lambda h,c: h[7].update(attackId=99)),
            ("missing_client_hit", lambda h,c: c.pop(9)),
            ("claim_different_target", lambda h,c: c[9].update(netId=2)),
            ("unattributed_host_hp", lambda h,c: h[5].update(beforeHp=99)),
            ("host_no_delta", lambda h,c: h[5].update(afterHp=100)),
            ("host_no_readback", lambda h,c: h[7].update(readback=0)),
            ("host_not_attempted", lambda h,c: h[7].update(attempted=0)),
            ("source_namespace", lambda h,c: h[7].update(event=8)),
            ("missing_publication_association", lambda h,c: h[6].update(causes=[])),
            ("duplicate_association", lambda h,c: h[6].update(causes=[1,1])),
            ("foreign_source", lambda h,c: h[6].update(causes=[99])),
            ("publication_before", lambda h,c: h[6].update(beforeHp=99)),
            ("publication_unexplained_hp", lambda h,c: h[6].update(afterHp=92,requestedHp=92,payload=hp(92))),
            ("publication_wrong_wire_hp", lambda h,c: h[6].update(payload=hp(92))),
            ("claim_wrong_wire", lambda h,c: c[8].update(payload=b'\x08\x00\x00')),
            ("receive_wrong_wire", lambda h,c: c[6].update(payload=hp(92))),
            ("missing_receive", lambda h,c: c.pop(6)),
            ("duplicate_apply", lambda h,c: c.insert(8, dict(c[7]))),
            ("apply_unknown_sequence", lambda h,c: c[7].update(hpSequence=999)),
            ("apply_wrong_delta", lambda h,c: c[7].update(afterHp=92,requestedHp=92)),
            ("apply_wrong_before", lambda h,c: c[7].update(beforeHp=99)),
            ("apply_missing_readback", lambda h,c: c[7].update(readback=0)),
            ("apply_missing_attempt", lambda h,c: c[7].update(attempted=0)),
            ("death_without_source", lambda h,c: h[-1].update(causes=[])),
            ("death_wrong_event", lambda h,c: h[-1].update(event=99)),
            ("death_raw_other_target", lambda h,c: h[-1].update(payload=packet(24,struct.pack('<IH',9,2)))),
            ("unexpected_host_apply_kind", lambda h,c: h[5].update(kind=6)),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name):
                h,c=fixture(); mutate(h,c); result=audit(finalize(h),finalize(c))
                self.assertEqual(result['status'],'FAIL', (name,result['issues']))
                self.assertFalse(result['recordedGraphConsistent'])
                self.assertIn(expected_codes[name],{i['code'] for i in result['issues']},name)

    def test_coverage_mutations(self):
        mutations = [
            ("lost_receipt", lambda h,c: h[0].update(loss=1)),
            ("admission_missing", lambda h,c: c.pop(4)),
            ("admission_duplicate", lambda h,c: c[4].update(netId=1)),
            ("root_replacement", lambda h,c: c[7].update(status=999)),
            ("scope_load", lambda h,c: h[6].update(load=99)),
            ("scope_transition", lambda h,c: c[7].update(transition=99)),
            ("scope_epoch", lambda h,c: c[7].update(epoch=99)),
            ("scope_generation", lambda h,c: c[7].update(generation=99)),
            ("scope_delivery", lambda h,c: c[7].update(delivery=99)),
            ("scope_salt", lambda h,c: c[7].update(sessionSalt=99)),
            ("scope_peer", lambda h,c: c[7].update(peer=99)),
            ("unqualified_hit", lambda h,c: h[5].update(qualified=0)),
            ("unqualified_application", lambda h,c: c[7].update(qualified=0)),
            ("unsupported_location", lambda h,c: c[7].update(location=(5,6,0,0,0,0))),
            ("bad_local_qpc", lambda h,c: c[7].update(qpc=0)),
        ]
        for name, mutate in mutations:
            with self.subTest(name=name):
                h,c=fixture(); mutate(h,c)
                # Preserve explicit clock mutation instead of reassigning QPC.
                if name!='bad_local_qpc': finalize(h);finalize(c)
                result=audit(h,c)
                self.assertFalse(result['recordedGraphConsistent'],(name,result))
                self.assertNotEqual(result['status'],'PASS')

    def test_text_mutations(self):
        h,c=fixture(); host,client=render(h),render(c)
        mutations = [
            ('no_logs','',''),
            ('no_host','',client),
            ('no_summary',host[:host.rfind('[combat-causal] summary')],client),
            ('unfinished_line',host,client.rstrip('\n')),
            ('duplicate_field',host,client.replace('schema=1','schema=1 schema=1',1)),
            ('missing_field',host,client.replace('readback=1 ','',1)),
            ('unknown_kind',host,client.replace('kind=6 ','kind=99 ',1)),
            ('invalid_hex',host,client.replace('payload=17','payload=zz',1)),
            ('changed_checksum',host,client.replace('payloadFnv64=','payloadFnv64=9',1)),
            ('invalid_boolean',host,client.replace('qualified=1','qualified=2',1)),
            ('summary_drop',host,client.replace('dropped=0','dropped=1')),
            ('summary_retired',host,client.replace('retired=0','retired=1')),
            ('summary_pending',host,client.replace('started=16','started=17')),
            ('forged_coverage',host,client.replace('nativeCoverageQualified=0','nativeCoverageQualified=1')),
            ('forged_acceptance',host,client.replace('acceptance=0','acceptance=1')),
            ('consumer_missing',host,re.sub(r'\[combat-causal\] consumer[^\n]*\n','',client,count=1)),
            ('consumer_original_scope',host,client.replace('admittedGeneration=2','admittedGeneration=99',1)),
            ('consumer_truncated',host,client.replace('truncated=0','truncated=1',1)),
            ('consumer_outcome',host,client.replace('outcome=1','outcome=2',1)),
            ('association_gap',host.replace('index=0','index=1',1),client),
            ('serial_gap',host.replace('serial=6 ','serial=99 ',1),client),
            ('unknown_record',host+'[combat-causal] mystery schema=1\n',client),
        ]
        for name,h,c in mutations:
            with self.subTest(name=name):
                result=AUDIT.audit_texts(h,c)
                self.assertFalse(result['recordedGraphConsistent'],(name,result['issues']))
                self.assertFalse(result['exactlyOneSourceProven'])

    def test_rejection_is_not_a_cause(self):
        h,c=fixture()
        reject=dict(c[-1]);reject.update(kind=12,reason=14,qualified=0,payload=hp(93),
                                      netId=0,actor=0,objentry=0,status=0,controller=0,record=0,
                                      expectedEpoch=9,sequenceFloor=2)
        c.append(reject)
        self.assertTrue(audit(h,finalize(c))['recordedGraphConsistent'])

    def test_batch_companion_cannot_disappear(self):
        h,c=fixture()
        raw=packet(23,struct.pack('<IQH',9,1,2)+struct.pack('<Hii',1,93,100)+struct.pack('<Hii',2,100,100))
        h[6]['payload']=raw
        c[5]['payload']=raw
        c[6]['payload']=raw
        result=audit(finalize(h),finalize(c))
        self.assertIn('incomplete_or_duplicate_batch_targets',{i['code'] for i in result['issues']})
        self.assertIn('accepted_batch_missing_target_receipts',{i['code'] for i in result['issues']})
        self.assertEqual(result['status'],'FAIL')

    def test_extra_or_missing_source_metadata(self):
        for name,mutate,expected in [
            ('unexpected_hit_association',lambda h,c:h[5].update(causes=[99]),'unexpected_source_association'),
            ('missing_accepted_header',lambda h,c:c.pop(5),'missing_or_duplicate_accepted_batch'),
            ('duplicate_accepted_header',lambda h,c:c.insert(6,dict(c[5])),'missing_or_duplicate_accepted_batch'),
            ('client_native_delta',lambda h,c:c[9].update(afterHp=92),'unadmitted_client_native_delta'),
            ('zero_host_claim_sequence',lambda h,c:h[7].update(claimSeq=0),'duplicate_or_reordered_host_claim_sequence'),
            ('zero_client_claim_sequence',lambda h,c:c[8].update(claimSeq=0),'duplicate_or_reordered_client_claim_sequence'),
        ]:
            with self.subTest(name=name):
                h,c=fixture();mutate(h,c);result=audit(finalize(h),finalize(c))
                self.assertIn(expected,{i['code'] for i in result['issues']})
                self.assertEqual(result['status'],'FAIL')

    def test_post_summary_metadata_and_counter_mutants(self):
        h,c=fixture();host,client=render(h),render(c)
        association=re.search(r'\[combat-causal\] association[^\n]*\n',host)[0]
        consumer=re.search(r'\[combat-causal\] consumer[^\n]*\n',client)[0]
        cases=[
            ('association_after_summary',host.replace(association,'')+association,client),
            ('consumer_after_summary',host,client.replace(consumer,'')+consumer),
            ('runtime_loss',host+'[combat-causal-runtime] schema=1 loss=1 acceptance=0\n',client),
            ('receipt_after_summary',host+render(h[:1]).split('[combat-causal] summary')[0],client),
            ('duplicate_consumer',host,client.replace(consumer,consumer+consumer,1)),
            ('drained_count',host.replace('drained=11','drained=10'),client),
            ('negative_counter',host.replace('loss=0','loss=-1',1),client),
            ('payload_prefix',host,client.replace('originalBytes=27','originalBytes=5000',1)),
        ]
        for name,h,c in cases:
            with self.subTest(name=name):
                result=AUDIT.audit_texts(h,c)
                self.assertFalse(result['recordedGraphConsistent'],(name,result['issues']))
                self.assertFalse(result['coverageQualified'])

    def test_every_required_field_missing_refuses(self):
        h,c=fixture();host,client=render(h),render(c)
        for name,fields in [('receipt',AUDIT.RECEIPT),('consumer',AUDIT.CONSUMER),
                            ('association',AUDIT.ASSOCIATION),('summary',AUDIT.SUMMARY)]:
            original = host if name=='association' else client
            line=re.search(r'\[combat-causal\] '+name+r'[^\n]*\n',original)[0]
            for field in sorted(fields):
                with self.subTest(record=name,field=field):
                    changed=re.sub(r'(?<!\S)'+field+r'=\S* ?', '', line, count=1)
                    mutant=original.replace(line,changed,1)
                    result=AUDIT.audit_texts(mutant if name=='association' else host,
                                             client if name=='association' else mutant)
                    self.assertFalse(result['recordedGraphConsistent'],(name,field))

    def test_log_prefixes_and_crlf(self):
        h,c=fixture()
        def logged(rows):
            return ''.join('[2026-10-09 20:12:00] '+line+'\r\n' for line in render(rows).splitlines())
        self.assertTrue(AUDIT.audit_texts(logged(h),logged(c))['recordedGraphConsistent'])

    def test_empty_pre_admission_summaries(self):
        h,c=fixture()
        before=render([],{'Admitted':0})
        result=AUDIT.audit_texts(before+render(h),before+render(c))
        self.assertTrue(result['recordedGraphConsistent'],result['issues'])
        self.assertEqual(result['recordedRanges']['host']['firstReceiptLine'],2)
        self.assertFalse(result['coverageQualified'])

    def test_local_sequence_and_frame_mutants(self):
        for name,mutate in [
            ('client_hit_high_bit',lambda h,c:c[9].update(event=(1<<63))),
            ('host_frame_regression',lambda h,c:h[6].update(frame=9)),
            ('client_frame_regression',lambda h,c:c[7].update(frame=9)),
            ('pointer_zero',lambda h,c:c[7].update(controller=0)),
            ('hp_out_of_native_range',lambda h,c:c[7].update(afterHp=1<<31)),
        ]:
            with self.subTest(name=name):
                h,c=fixture();mutate(h,c);result=audit(finalize(h),finalize(c))
                self.assertFalse(result['recordedGraphConsistent'])

    def test_cli_file_hashes_and_no_input_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);h,c=fixture()
            host=p/'host.log';client=p/'client.log';out=p/'audit.json'
            host.write_text(render(h),encoding='utf-8');client.write_text(render(c),encoding='utf-8')
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(AUDIT.main(['--host',str(host),'--client',str(client),'--output',str(out)]),1)
            result=json.loads(out.read_text());self.assertEqual(result['status'],'INCONCLUSIVE')
            self.assertEqual(result['inputs']['host']['sha256'],AUDIT.hashlib.sha256(host.read_bytes()).hexdigest())
            before=host.read_bytes()
            with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                AUDIT.main(['--host',str(host),'--client',str(client),'--output',str(host)])
            self.assertEqual(before,host.read_bytes())
            with contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):
                AUDIT.main(['--host',str(host),'--client',str(host)])
            with contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(AUDIT.main(['--host',str(host),'--client',str(p/'absent.log')]),2)
            client.write_bytes(client.read_bytes()+b'\xff\n')
            self.assertFalse(AUDIT.audit_paths(host,client)['recordedGraphConsistent'])


if __name__ == '__main__':
    unittest.main()
