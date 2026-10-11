"""Offline controls from the exact retained activation-first A logs."""
import gzip
import hashlib
import json
from pathlib import Path
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/scenario"))
import combat_event_replay as replay

FIXTURE = Path(__file__).parent / "fixtures/combat_event_replay"
HOST = gzip.decompress((FIXTURE / "host.log.gz").read_bytes()).decode("utf-8")
CLIENT = gzip.decompress((FIXTURE / "client.log.gz").read_bytes()).decode("utf-8")


def edit(text, marker, key, value):
    rows = text.splitlines(keepends=True)
    indices = [i for i, line in enumerate(rows) if marker in line]
    assert len(indices) == 1, (marker, len(indices))
    i = indices[0]
    rows[i], count = re.subn(r"(?<!\S)" + key + r"=\S*", key + "=" + str(value), rows[i])
    assert count == 1
    return "".join(rows)


class EventReplayTests(unittest.TestCase):
    def test_exact_sample_bytes(self):
        manifest = json.loads((FIXTURE / "manifest.json").read_text())
        for role in ("host", "client"):
            raw = gzip.decompress((FIXTURE / (role + ".log.gz")).read_bytes())
            self.assertEqual(hashlib.sha256(raw).hexdigest(), manifest["files"][role]["sha256"])
            self.assertEqual(len(raw), manifest["files"][role]["bytes"])

    def test_actual_goofy_chain(self):
        result = replay.replay(HOST, CLIENT)
        self.assertEqual(result["status"], "RECORDED_LOCAL_CHAINS_LINKED")
        event, = result["events"]
        self.assertEqual((event["event"], event["hostSerial"], event["publicationSerial"],
                          event["receiveSerial"], event["friendStoreSerial"], event["hpSequence"]), (1,151,152,298,303,35))
        self.assertEqual((event["native"]["sourceObjectId"], event["native"]["attackId"],
                          event["beforeHp"], event["afterHp"]), (93,1161,20,18))
        self.assertEqual(result["nativeEvents"][0]["disposition"], "linked")
        self.assertFalse(result["acceptance"])
        self.assertFalse(result["exactlyOneSourceProven"])
        self.assertFalse(result["coverageQualified"])

    def test_explicit_noop_dispositions(self):
        result = replay.replay(HOST, CLIENT)
        missing = [p for p in result["publications"] if p["hpSequence"] in (8,17)]
        self.assertEqual(len(missing), 10)
        self.assertTrue(all(p["disposition"] == "noop_unmatched" and p["transportDisposition"] == "NOT_ESTABLISHED" and
                            not p["events"] and p["receiveCount"] == 0 for p in missing))
        hit = [p for p in result["publications"] if p["hpSequence"] == 35 and p["netId"] == 1]
        self.assertEqual(hit[0]["disposition"], "changed_received")

    def test_native_policy_mutants(self):
        cases = [
            ("[hittrace] event schema=1 seq=1 ", "callerRva", "3D613C"),
            ("[hittrace] event schema=1 seq=1 ", "coverageStable", 0),
            ("[hittrace] event schema=1 seq=1 ", "lossStable", 0),
            ("[hittrace] event schema=1 seq=1 ", "unwound", 1),
            ("[hittrace] context schema=1 seq=1 phase=1 ", "loadSerial", 99),
            ("[hittrace] actor schema=1 seq=1 phase=1 subject=1 ", "actor", "1234"),
            ("[hittrace] actor schema=1 seq=1 phase=0 subject=1 ", "type", 0),
            ("[hittrace] actor schema=1 seq=1 phase=0 subject=0 ", "hp", 19),
            ("[damagepolicy] event schema=1 seq=1 ", "recorded", 0),
            ("[damagepolicy] event schema=1 seq=1 ", "reason", 11),
            ("[damagepolicy] event schema=1 seq=1 ", "supported", 0),
            ("[damagepolicy] event schema=1 seq=1 ", "sourceClass", 1),
            ("[damagepolicy] event schema=1 seq=1 ", "claimQueued", 1),
            ("[damagepolicy] event schema=1 seq=1 ", "roster1", 99),
            ("[damagepolicy] context schema=1 seq=1 phase=0 ", "role", 2),
            ("[damagepolicy] actor schema=1 seq=1 phase=0 subject=1 ", "actor", "1234"),
            ("[damagepolicy] input schema=1 seq=1 phase=0 ", "owner", "1234"),
            ("[hittrace] input schema=1 seq=1 phase=1 ", "attackId", 999),
            ("[hittrace] child schema=1 seq=1 ", "applySeq", 99),
            ("[hittrace] child schema=1 seq=1 ", "delta", -3),
            ("[hittrace] actor schema=1 seq=1 phase=1 subject=2 ", "hp", 17),
        ]
        for marker, key, value in cases:
            with self.subTest(marker=marker, key=key):
                result = replay.replay(edit(HOST, marker, key, value), CLIENT)
                self.assertEqual(result["status"], "UNRESOLVED")
                self.assertTrue(result["events"][0]["refusal"].startswith("missing_or_ambiguous_native_policy_witness:"))

    def test_packet_store_mutants(self):
        cases = [
            ("host", "receipt schema=1 serial=151 ", "qualified", 0, "unqualified_host_hit"),
            ("host", "association schema=1 serial=152 ", "event", 99, "missing_or_duplicate_event_publication"),
            ("host", "receipt schema=1 serial=152 ", "hpSequence", 36, "invalid_packet_batch_membership"),
            ("client", "receipt schema=1 serial=298 ", "sessionSalt", 99, "invalid_packet_batch_readback"),
            ("client", "receipt schema=1 serial=298 ", "payloadFnv64", 0, "missing_or_duplicate_event_receive"),
            ("client", "receipt schema=1 serial=303 ", "attempted", 0, "event_store_mismatch"),
            ("client", "receipt schema=1 serial=303 ", "returned", 0, "event_store_mismatch"),
            ("client", "receipt schema=1 serial=303 ", "readback", 0, "event_store_mismatch"),
            ("client", "receipt schema=1 serial=303 ", "afterHp", 17, "event_store_mismatch"),
            ("client", "receipt schema=1 serial=303 ", "actor", "1234", "event_store_mismatch"),
        ]
        for role, marker, key, value, code in cases:
            with self.subTest(role=role, key=key):
                h, c = (edit(HOST, marker, key, value), CLIENT) if role == "host" else (HOST, edit(CLIENT, marker, key, value))
                result = replay.replay(h, c)
                self.assertEqual(result["status"], "UNRESOLVED")
                self.assertEqual(result["events"][0]["refusal"], code)

    def test_missing_and_duplicate_records(self):
        for role, marker, code in [
            ("host", "receipt schema=1 serial=151 ", "duplicate_host_event"),
            ("client", "receipt schema=1 serial=298 ", "missing_or_duplicate_event_receive"),
            ("client", "receipt schema=1 serial=303 ", "missing_or_duplicate_event_store"),
        ]:
            for duplicate in (False, True):
                with self.subTest(role=role, marker=marker, duplicate=duplicate):
                    text = HOST if role == "host" else CLIENT
                    line, = [line for line in text.splitlines(keepends=True) if marker in line]
                    text = text.replace(line, line + line if duplicate else "")
                    result = replay.replay(text if role == "host" else HOST, text if role == "client" else CLIENT)
                    self.assertEqual(result["status"], "UNRESOLVED")
                    if role == "host" and not duplicate:
                        self.assertEqual(result["nativeEvents"][0]["disposition"], "unresolved_enemy_damage")
                    else:
                        self.assertTrue(any(e.get("refusal") == code for e in result["events"]))

    def test_coverage_limits_survive_positive(self):
        result = replay.replay(HOST, CLIENT)
        codes = {i["code"] for i in result["coverageIssues"]}
        self.assertIn("pending_or_unsealed_tail", codes)
        self.assertTrue(any("authenticated runtime" in limit for limit in result["limits"]))

    def test_crlf_same_chain(self):
        result = replay.replay(HOST.replace("\r\n", "\n").replace("\n", "\r\n"),
                               CLIENT.replace("\r\n", "\n").replace("\n", "\r\n"))
        self.assertTrue(result["events"][0]["linked"])


if __name__ == "__main__":
    unittest.main()
