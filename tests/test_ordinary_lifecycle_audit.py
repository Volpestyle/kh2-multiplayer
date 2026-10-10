"""Constructed certificate mutants plus retained ca007 evidence; no native authority."""
import copy
import importlib.util
import json
import re
import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools/scenario"));sys.path.insert(0,str(ROOT/"tests"))
from ordinary_lifecycle_audit import History,decode
from combat_causal_serializer import render
import test_combat_causal_audit as legacy
A=legacy.AUDIT
LOCATION=(5,6,0,1,1,0)


def fixture(retire=False,birth=False):
    rows=[dict(net=i+1,ordinal=i,birth=i+1,terminal=0,state=0) for i in range(5)]
    if retire:rows[3].update(terminal=6,state=2)
    if birth:rows.append(dict(net=6,ordinal=3,birth=7,terminal=0,state=0))
    return rows


def encode(rows,sequence=None,epoch=9,load=3):
    sequence=sequence or max(max(r["birth"],r["terminal"]) for r in rows)
    body=struct.pack("<BIQII6HH",2,epoch,sequence,load,2,*LOCATION,1)
    header=bytearray(44);header[4]=5
    body+=bytes([1])*32+struct.pack("<6HI",*LOCATION,1)+header+struct.pack("<H",5)
    for i in range(5):body+=struct.pack("<I",302)+bytes(60)
    body+=struct.pack("<H",len(rows))
    for r in rows:
        body+=struct.pack("<HHHIQQQB4f",r["net"],0,r["ordinal"],302,r["birth"],r["birth"],r["terminal"],r["state"],float(r["ordinal"]),0,0,1)
    return struct.pack("<BH",48,len(body))+body


def timeline(peer):
    rows=[legacy.row(peer,0,netId=i,actor=(0x1000 if peer=="host" else 0x2000)+i,objentry=0x3000+i,status=0x4000+i,controller=0x5000,record=0x6000+i) for i in range(1,6)]
    initial=encode(fixture());retire=encode(fixture(True));birth=encode(fixture(True,True))
    for i in range(5):
        row=copy.deepcopy(rows[i]);row.update(kind=15,event=i+1,beforeHp=100,afterHp=100,payload=initial);rows.append(row)
    row=copy.deepcopy(rows[3]);row.update(kind=14,event=6,beforeHp=100,afterHp=100,payload=retire);rows.append(row)
    row=copy.deepcopy(rows[3]);row.update(kind=15,netId=6,actor=0x7000 if peer=="host" else 0x8000,event=7,beforeHp=100,afterHp=100,payload=birth);rows.append(row)
    return legacy.finalize(rows)


class LifecycleTests(unittest.TestCase):
    def test_retained_ca007_removal_relabel_and_hp198(self):
        saved=json.loads((ROOT/"tests/fixtures/ordinary_lifecycle_ca007.json").read_text())
        self.assertEqual(saved["status"],"ORIGINAL_FAIL_PRESERVED")
        host={r["line"]:r["text"] for r in saved["files"][0]["rows"]}
        friend={r["line"]:r["text"] for r in saved["files"][1]["rows"]}
        values=lambda text:dict(re.findall(r"(\w+)=([^\s]*)",text))
        removed=values(host[625]);retired=values(host[626]);relabel=values(friend[796]);lost=values(friend[807])
        self.assertEqual([removed[k] for k in ("originalNetId","originalHp","hp","scopeStable","watchedComplete","watchedPresent")],["4","20","20","1","1","0"])
        self.assertEqual([retired[k] for k in ("frame","kind","reason")],["3352","11","3"])
        self.assertEqual([relabel[k] for k in ("prior","candidate","selected","reason")],["4","6","6","newest-live-point"])
        self.assertEqual([lost[k] for k in ("serial","netId","payloadBytes")],["198","0","0"])
        raw=bytes.fromhex(values(friend[799])["payload"])
        self.assertEqual(struct.unpack_from("<IQH",raw,3),(2,24,5))
        ids=[struct.unpack_from("<Hii",raw,17+10*i)[0] for i in range(5)]
        self.assertEqual(ids,[1,2,3,5,6])
        original=History().admit(encode(fixture(),epoch=2))
        self.assertEqual(set(ids)-original["rows"].keys(),{6})
        # No retained schema2 producer proof => the historical removal cannot
        # become a qualified retirement. The constructed proof is tested above.
        with self.assertRaises(ValueError):History().admit(raw)
    def test_ordered_history(self):
        h=History();h.admit(encode(fixture()));h.admit(encode(fixture(True)));c=h.admit(encode(fixture(True,True)))
        self.assertEqual(c["rows"][4]["state"],2);self.assertEqual(c["rows"][6]["birth"],7)
    def test_missing_retirement(self):
        h=History();h.admit(encode(fixture()));rows=fixture(False,True)
        with self.assertRaisesRegex(ValueError,"duplicate live record|birth before removal"):h.admit(encode(rows))
    def test_reuse_reorder_duplicate(self):
        for mutation in ("id","birth","duplicate","record","missing","scope"):
            with self.subTest(mutation=mutation):
                h=History();h.admit(encode(fixture()));rows=fixture(True,True);kwargs={}
                if mutation=="id":rows[-1]["net"]=4
                elif mutation=="birth":rows[-1]["birth"]=6
                elif mutation=="duplicate":rows.append(copy.deepcopy(rows[-1]))
                elif mutation=="record":rows[0]["ordinal"]=4
                elif mutation=="missing":rows.pop(0)
                else:kwargs["epoch"]=10
                with self.assertRaises(ValueError):h.admit(encode(rows,**kwargs))
                self.assertEqual(h.cut["sequence"],5)
    def test_duplicate_cut_and_noop_floor_poison(self):
        h=History();raw=encode(fixture());h.admit(raw)
        with self.assertRaisesRegex(ValueError,"history scope/order"):h.admit(raw)
        with self.assertRaisesRegex(ValueError,"membership boundary"):h.admit(encode(fixture(),sequence=999))
    def test_truncated_and_wrong_schema(self):
        raw=encode(fixture(True,True))
        for n in range(len(raw)):
            with self.subTest(bytes=n),self.assertRaises((ValueError,struct.error)):decode(raw[:n])
        bad=bytearray(raw);bad[3]=1
        with self.assertRaisesRegex(ValueError,"schema"):decode(bad)
    def test_production_serializer_lifecycle_graph(self):
        h,c=timeline("host"),timeline("client")
        result=A.audit_texts(render(h),render(c))
        self.assertEqual({i["code"] for i in result["issues"]},{"no_applied_changes","native_coverage_unqualified_schema1","runtime_session_and_shutdown_coverage_unqualified"})
        self.assertFalse(result["acceptance"])
    def test_new_occurrence_has_own_damage_source(self):
        h,c=timeline("host"),timeline("client");hit=copy.deepcopy(h[-1]);hit.update(kind=1,event=1,beforeHp=100,afterHp=95,returned=1,readback=1,payload=b"")
        h.append(hit);raw=legacy.packet(23,struct.pack("<IQHHii",9,1,1,6,95,100))
        publish=copy.deepcopy(hit);publish.update(kind=4,hpSequence=1,requestedHp=95,enqueued=1,payload=raw,causes=[1]);h.append(publish)
        receive=copy.deepcopy(c[-1]);receive.update(kind=5,event=0,hpSequence=1,beforeHp=100,afterHp=95,requestedHp=95,enqueued=1,payload=raw)
        c.extend([legacy.accepted(receive),receive]);apply=copy.deepcopy(receive);apply.update(kind=6,attempted=1,returned=1,readback=1,enqueued=0,payload=b"");c.append(apply)
        result=A.audit_texts(render(legacy.finalize(h)),render(legacy.finalize(c)))
        self.assertTrue(result["recordedGraphConsistent"],result["issues"]);self.assertFalse(result["acceptance"])
    def test_missing_actual_retirement_row(self):
        h,c=timeline("host"),timeline("client");h.pop(10)
        result=A.audit_texts(render(legacy.finalize(h)),render(c))
        codes={i["code"] for i in result["issues"]}
        self.assertIn("invalid_membership_lifecycle",codes);self.assertIn("paired_membership_lifecycle_mismatch",codes)
    def test_kill_credit_refusal(self):
        h,c=timeline("host"),timeline("client");h[10]["afterHp"]=0
        result=A.audit_texts(render(h),render(c))
        self.assertIn("invalid_membership_lifecycle",{i["code"] for i in result["issues"]})
    def test_actor_reuse_refusal(self):
        h,c=timeline("host"),timeline("client");h[-1]["actor"]=h[3]["actor"]
        result=A.audit_texts(render(h),render(c))
        self.assertIn("invalid_membership_lifecycle",{i["code"] for i in result["issues"]})
    def test_old_claim_after_retirement(self):
        h,c=timeline("host"),timeline("client");old=copy.deepcopy(h[3]);old.update(kind=3,event=99)
        h.append(old);result=A.audit_texts(render(legacy.finalize(h)),render(c))
        self.assertIn("operation_on_retired_occurrence",{i["code"] for i in result["issues"]})


if __name__=="__main__":unittest.main()
