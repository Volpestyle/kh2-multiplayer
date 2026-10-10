"""Audit saved host/client combat-causal receipts; never accesses a process.

Exit 0: qualified coverage and exactly-one-source graph (schema 1 cannot supply
that coverage). Exit 1: FAIL or INCONCLUSIVE. Exit 2: invocation/file error.
Usage: python -B tools/scenario/combat_causal_audit.py --host HOST.log
       --client CLIENT.log --output audit.json
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

KINDS = ("Admission", "Hit", "Claim", "HostApply", "HpPublish", "HpReceive",
         "HpApply", "DeathPublish", "DeathReceive", "DeathApply", "Packet",
         "Retired", "ConsumerRejected", "ConsumerAccepted")
SCOPE = "generation delivery sessionSalt connection host peer hostDelivery peerDelivery epoch load transition role slot location".split()
ROOTS = "actor objentry status controller record objectId type maxHp".split()
HEX = set("actor objentry status controller record".split())
BITS = set("qualified attempted returned readback enqueued".split())
RECEIPT = set(("schema serial qpc kind reason qualified event hpSequence loss generation delivery sessionSalt connection host peer hostDelivery peerDelivery epoch load transition frame role slot location netId objectId type actor objentry status controller record targetHp maxHp claimConnection claimSeq attackId damage beforeHp afterHp requestedHp attempted returned readback enqueued causeCount payloadBytes payloadFnv64 payload incarnationAuthority").split())
SUMMARY = set("schema admitted retired started drained dropped loss nativeCoverageQualified acceptance".split())
CONSUMER = set(("schema serial outcome wireType reason expectedEpoch sequenceFloor originalBytes retainedBytes truncated admittedGeneration admittedDelivery admittedSessionSalt admittedConnection admittedHost admittedPeer admittedHostDelivery admittedPeerDelivery admittedEpoch admittedLoad admittedTransition admittedRole admittedSlot admittedLocation").split())
ASSOCIATION = set("schema serial index event".split())
LIMITS = [
    "Scope is one immutable two-peer BC [5,6,0,1,1,0] diagnostic admission per log; concatenated runs refuse.",
    "Source event IDs and addresses are local to their log. Cross-peer edges use exact raw packets and claim keys, never cross-PC QPC ordering.",
    "A qualified receipt is a recorded local assertion, not independent reconstruction of NativeHitTrace or complete HP-writer coverage.",
    "Schema 1 always emits nativeCoverageQualified=0 and acceptance=0; graph consistency cannot pass AC3 or grant gameplay acceptance.",
    "Session salt is not exact runtime session authentication. Same-pointer incarnation and continuous native coverage remain unqualified.",
    "The last summary is only a recorded drain cutoff, not proof that shutdown flushed or that no later hit occurred.",
    "Multiple admitted causes in one client HP store are reported as aggregate/multiple sources, not inferred duplicate native hits.",
]


class Auditor:
    def __init__(self):
        self.issues = []
        self.applications = []

    def issue(self, code, peer=None, row=None, detail=None, category="graph"):
        item = {"code": code, "category": category}
        if peer is not None:
            item["peer"] = peer
        if row is not None:
            item.update(line=row["line"], serial=row.get("serial"))
        if detail is not None:
            item["detail"] = detail
        self.issues.append(item)


def fnv(raw):
    value = 0xcbf29ce484222325
    for byte in raw:
        value = ((value ^ byte) * 0x100000001b3) & ((1 << 64) - 1)
    return value


def parse(text, peer, audit):
    rows, summaries, supplements, associations = [], [], {}, defaultdict(list)
    observed_lines = []
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        if "[combat-causal-runtime]" in line and re.search(r"\bloss=[1-9]\d*\b", line):
            audit.issue("runtime_observer_loss", peer, {"line": number}, category="coverage")
        if "[combat-causal]" not in line:
            continue
        marker = re.search(r"\[combat-causal\] (\w+) (.*?)(?:\r?\n)?$", line)
        if not marker or not line.endswith("\n"):
            audit.issue("malformed_or_truncated_line", peer, {"line": number}, category="coverage")
            continue
        name, body = marker.groups()
        observed_lines.append(number)
        expected = {"receipt": RECEIPT, "summary": SUMMARY, "consumer": CONSUMER,
                    "association": ASSOCIATION}.get(name)
        fields = {}
        try:
            for token in body.split():
                key, value = token.split("=", 1)
                if key in fields:
                    raise ValueError("duplicate field " + key)
                fields[key] = value
            if expected is None or fields.keys() != expected:
                raise ValueError("unknown/missing schema fields")
            converted = {"line": number}
            for key, value in fields.items():
                if key == "payload":
                    if not re.fullmatch(r"(?:[0-9a-fA-F]{2})*", value):
                        raise ValueError("payload hex")
                    converted[key] = bytes.fromhex(value)
                elif key in ("location", "admittedLocation"):
                    if not re.fullmatch(r"\d+(?:,\d+){5}", value):
                        raise ValueError("location")
                    converted[key] = tuple(map(int, value.split(",")))
                else:
                    pattern = r"[0-9A-Fa-f]+" if key in HEX else r"-?\d+"
                    if not re.fullmatch(pattern, value):
                        raise ValueError(key)
                    converted[key] = int(value, 16 if key in HEX else 10)
                    if not -(1 << 31) <= converted[key] < (1 << 64):
                        raise ValueError("integer range")
                    if key not in ("beforeHp", "afterHp", "requestedHp", "targetHp", "damage") and converted[key] < 0:
                        raise ValueError("negative counter")
                    if key in BITS and converted[key] not in (0, 1):
                        raise ValueError("validity bit")
                    if key in ("beforeHp", "afterHp", "requestedHp", "targetHp", "maxHp", "damage") and not -(1 << 31) <= converted[key] < (1 << 31):
                        raise ValueError("HP/damage range")
            row = converted
            if row["schema"] != 1:
                raise ValueError("schema")
            if name == "receipt":
                if row["kind"] >= len(KINDS) or row["reason"] > 20 or row["causeCount"] > 16:
                    raise ValueError("kind/reason/cause capacity")
                if row["payloadBytes"] != len(row["payload"]) or len(row["payload"]) > 4096:
                    raise ValueError("payload size")
                if (fnv(row["payload"]) if row["payload"] else 0) != row["payloadFnv64"]:
                    raise ValueError("payload checksum")
                if row["incarnationAuthority"] != 0:
                    raise ValueError("unsupported incarnation assertion")
                rows.append(row)
            elif name == "summary":
                summaries.append(row)
            elif name == "consumer":
                if row["serial"] in supplements:
                    raise ValueError("duplicate consumer")
                supplements[row["serial"]] = row
            else:
                associations[row["serial"]].append(row)
        except (ValueError, OverflowError) as error:
            audit.issue("invalid_record", peer, {"line": number}, str(error), "coverage")
    if not rows:
        audit.issue("missing_receipts", peer, category="coverage")
    # Count every receipt, including unqualified and rejected observations.
    for index, row in enumerate(rows, 1):
        if row["serial"] != index:
            audit.issue("receipt_serial_gap_or_duplicate", peer, row, {"expected": index}, "coverage")
        if row["qpc"] <= 0 or (index > 1 and row["qpc"] < rows[index - 2]["qpc"]):
            audit.issue("invalid_local_clock", peer, row, category="coverage")
        if row["loss"] or row["kind"] == 11:
            audit.issue("retired_or_loss", peer, row, row["reason"], "coverage")
        linked = associations.pop(row["serial"], [])
        row["causes"] = [item["event"] for item in linked]
        if ([item["index"] for item in linked] != list(range(row["causeCount"])) or
                any(item["line"] <= row["line"] for item in linked) or
                (index < len(rows) and any(item["line"] >= rows[index]["line"] for item in linked))):
            audit.issue("association_count_or_order", peer, row, category="coverage")
        boundary = min((s["line"] for s in summaries if s["line"] > row["line"]), default=float("inf"))
        if any(item["line"] >= boundary for item in linked):
            audit.issue("association_after_summary", peer, row, category="coverage")
        if len(set(row["causes"])) != len(row["causes"]):
            audit.issue("duplicate_association", peer, row)
        supplement = supplements.pop(row["serial"], None)
        incoming = row["kind"] in (5, 8, 12, 13)
        if incoming != (supplement is not None):
            audit.issue("missing_or_unexpected_consumer", peer, row, category="coverage")
        if supplement:
            original = tuple(supplement["admitted" + key[0].upper() + key[1:]] for key in SCOPE)
            row["admittedScope"] = original
            if (supplement["line"] <= row["line"] or supplement["line"] >= boundary or
                    (index < len(rows) and supplement["line"] >= rows[index]["line"]) or
                    supplement["reason"] != row["reason"] or
                    supplement["retainedBytes"] != len(row["payload"]) or supplement["truncated"] or
                    supplement["originalBytes"] != len(row["payload"]) or
                    supplement["outcome"] != (2 if row["kind"] == 12 else 1) or
                    not row["payload"] or supplement["wireType"] != row["payload"][0]):
                audit.issue("invalid_consumer_supplement", peer, row, category="coverage")
    if associations or supplements:
        audit.issue("orphan_supplement", peer, category="coverage")
    if not summaries:
        audit.issue("missing_summary", peer, category="coverage")
    previous = None
    for summary in summaries:
        count = sum(row["line"] < summary["line"] for row in rows)
        if (summary["drained"] != count or not summary["drained"] <= summary["started"] or
                (previous and any(summary[k] < previous[k] for k in ("started", "drained", "dropped", "loss")))):
            audit.issue("summary_counter_mismatch", peer, summary, category="coverage")
        pre_admission = (summary["admitted"] == 0 and summary["started"] == 0 and summary["drained"] == 0 and
                         bool(rows) and summary["line"] < rows[0]["line"])
        if (summary["admitted"] != 1 and not pre_admission) or summary["retired"] or summary["loss"] or summary["dropped"]:
            audit.issue("unqualified_summary", peer, summary, category="coverage")
        # Schema 1 explicitly has no native coverage certificate. Even a forged
        # flag cannot upgrade it into a new supported schema or a live verdict.
        if summary["nativeCoverageQualified"] != 0 or summary["acceptance"] != 0:
            audit.issue("unsupported_coverage_assertion", peer, summary, category="coverage")
        previous = summary
    if summaries:
        final = summaries[-1]
        if final["started"] != final["drained"] or final["drained"] != len(rows) or any(
                number > final["line"] for number in observed_lines):
            audit.issue("pending_or_unsealed_tail", peer, final, category="coverage")
    return rows


def scope(row):
    return tuple(row[k] for k in SCOPE)


def target(row):
    return tuple(row[k] for k in ROOTS)


def key(row):
    return tuple(row[k] for k in ("claimConnection", "claimSeq", "epoch", "netId", "objectId", "attackId", "damage"))


def wire(row, allow_empty=False):
    """Read exact existing wire fields, with no codec/network/native execution."""
    raw = row["payload"]
    if len(raw) < 3 or struct.unpack_from("<H", raw, 1)[0] != len(raw) - 3:
        raise ValueError("framing")
    tag = raw[0]
    if tag == 23 and len(raw) >= 17:
        epoch, sequence, count = struct.unpack_from("<IQH", raw, 3)
        if not sequence or not (0 if allow_empty else 1) <= count <= 5 or len(raw) != 17 + 10 * count:
            raise ValueError("HP header")
        entries = [struct.unpack_from("<Hii", raw, 17 + 10 * i) for i in range(count)]
        if len({e[0] for e in entries}) != count or any(not n or not 0 < hp <= maximum for n, hp, maximum in entries):
            raise ValueError("HP entries")
        return tag, epoch, sequence, entries
    if tag == 24 and len(raw) == 9:
        epoch, net = struct.unpack_from("<IH", raw, 3)
        return tag, epoch, 0, [(net, 0, row["maxHp"])]
    if tag == 8 and len(raw) == 46:
        epoch, seq, net, obj, connection, attack, damage = struct.unpack_from("<IIHIQIi", raw, 3)
        return tag, (connection, seq, epoch, net, obj, attack, damage), raw[45]
    raise ValueError("wire type/size")


def prepare(rows, peer, audit):
    admissions = [r for r in rows if r["kind"] == 0]
    mapping = {r["netId"]: r for r in admissions}
    if (len(admissions) != 5 or len(mapping) != 5 or
            len({r["actor"] for r in admissions}) != 5):
        audit.issue("missing_or_duplicate_admission", peer, category="coverage")
    anchor = scope(admissions[0]) if admissions else None
    for row in rows:
        if anchor != scope(row) or ("admittedScope" in row and row["admittedScope"] != anchor):
            audit.issue("scope_replacement", peer, row, category="coverage")
        if (row["role"] != (1 if peer == "host" else 2) or row["slot"] != (0 if peer == "host" else 1) or
                row["location"] != (5, 6, 0, 1, 1, 0) or
                not all(row[k] for k in SCOPE if k not in ("location", "slot")) or
                row["host"] == row["peer"] or
                row["connection"] != row["host" if peer == "host" else "peer"]):
            audit.issue("invalid_scope", peer, row, category="coverage")
        if row["causes"] and row["kind"] not in (4, 5, 7, 8):
            audit.issue("unexpected_source_association", peer, row)
        if row["kind"] in (11, 12, 13):
            if row["kind"] in (12, 13) and row["qualified"]:
                audit.issue("consumer_promoted_to_cause", peer, row)
            continue
        admitted = mapping.get(row["netId"])
        if not admitted or target(row) != target(admitted):
            audit.issue("target_replacement_or_unmapped", peer, row, category="coverage")
        if row["objectId"] != 302 or row["type"] != 4 or not all(row[k] for k in HEX):
            audit.issue("invalid_target", peer, row, category="coverage")
        if not row["qualified"]:
            audit.issue("unqualified_receipt", peer, row, KINDS[row["kind"]], "coverage")
        if row["reason"] != (12 if row["kind"] == 2 else 0):
            audit.issue("unqualified_reason", peer, row, row["reason"], "coverage")
        if row["kind"] == 0 and not 0 < row["targetHp"] <= row["maxHp"]:
            audit.issue("invalid_admission_hp", peer, row, category="coverage")
        if not 0 < row["netId"] <= 65535:
            audit.issue("invalid_target_net_id", peer, row, category="coverage")
        if row["kind"] in (2, 4, 5, 7, 8):
            try:
                decoded = wire(row)
                if row["kind"] == 2:
                    if decoded[0] != 8 or decoded[1] != key(row) or decoded[2] != 1:
                        raise ValueError("claim key/slot")
                else:
                    if (decoded[0] != (23 if row["kind"] in (4, 5) else 24) or
                            decoded[1] != row["epoch"] or decoded[2] != row["hpSequence"] or
                            (row["netId"], row["requestedHp"], row["maxHp"]) not in decoded[3]):
                        raise ValueError("packet receipt fields")
                if row["enqueued"] != 1:
                    raise ValueError("packet not submitted/received")
            except ValueError as error:
                audit.issue("invalid_raw_packet", peer, row, str(error))
    # Admissions must precede operations and have no interleaved events.
    if len(rows) >= 5 and any(r["kind"] != 0 for r in rows[:5]):
        audit.issue("operation_before_admission", peer, category="coverage")
    return mapping


def audit_texts(host_text, client_text):
    audit = Auditor()
    peers = {p: parse(t, p, audit) for p, t in (("host", host_text), ("client", client_text))}
    mappings = {p: prepare(rows, p, audit) for p, rows in peers.items()}
    hmap, cmap = mappings["host"], mappings["client"]
    if hmap.keys() != cmap.keys() or any(hmap[n]["maxHp"] != cmap[n]["maxHp"] or
            hmap[n]["targetHp"] != cmap[n]["targetHp"] for n in hmap.keys() & cmap.keys()):
        audit.issue("paired_admission_mismatch", category="coverage")
    if hmap and cmap:
        h, c = next(iter(hmap.values())), next(iter(cmap.values()))
        if any(h[k] != c[k] for k in ("generation", "sessionSalt", "host", "peer", "hostDelivery", "peerDelivery", "epoch")):
            audit.issue("paired_session_mismatch", category="coverage")
    causes, claims, client_hits = {}, defaultdict(list), defaultdict(list)
    for row in peers["client"]:
        if row["kind"] == 1:
            client_hits[row["event"]].append(row)
        if row["kind"] == 2:
            claims[key(row)].append(row)
    for event, hits in client_hits.items():
        if not event or len(hits) != 1:
            audit.issue("duplicate_client_hit", "client", hits[0])
    used_claims = set()
    last_host_sequence = 0
    hp = {n: r["targetHp"] for n, r in hmap.items()}
    for row in peers["host"]:
        if row["kind"] not in (1, 3):
            continue
        event, net = row["event"], row["netId"]
        if not event or event in causes:
            audit.issue("duplicate_source", "host", row)
        causes.setdefault(event, row)
        if row["kind"] == 3:
            if row["claimSeq"] <= last_host_sequence:
                audit.issue("duplicate_or_reordered_host_claim_sequence", "host", row)
            last_host_sequence = row["claimSeq"]
            matches = claims.get(key(row), [])
            if len(matches) != 1 or key(row) in used_claims:
                audit.issue("orphan_or_duplicate_claim", "host", row)
            else:
                claim = matches[0]
                hits = client_hits.get(claim["event"], [])
                if (len(hits) != 1 or hits[0]["netId"] != net or not claim["qualified"] or
                        not hits[0]["qualified"] or hits[0]["beforeHp"] != hits[0]["afterHp"] or
                        hits[0]["returned"] != 1 or hits[0]["readback"] != 1):
                    audit.issue("orphan_claim_hit", "host", row)
                used_claims.add(key(row))
            if (event != ((1 << 63) | row["serial"]) or row["claimConnection"] != row["peer"] or
                    row["damage"] <= 0 or row["afterHp"] != max(0, row["beforeHp"] - row["damage"])):
                audit.issue("invalid_host_apply", "host", row)
            if row["attempted"] != 1:
                audit.issue("unattempted_host_apply", "host", row)
        elif event >= 1 << 63:
            audit.issue("invalid_hit_event_namespace", "host", row)
        if (row["beforeHp"] != hp.get(net) or not 0 <= row["afterHp"] < row["beforeHp"] or
                row["returned"] != 1 or row["readback"] != 1 or not row["qualified"]):
            audit.issue("unaccounted_host_delta", "host", row)
        hp[net] = row["afterHp"]
        audit.applications.append({"peer": "host", "serial": row["serial"], "netId": net,
                                   "before": row["beforeHp"], "after": row["afterHp"], "sources": [event]})
    for claim_key, rows in claims.items():
        if len(rows) != 1 or claim_key not in used_claims:
            audit.issue("unresolved_or_duplicate_claim", "client", rows[0])
    claimed_hits = [r["event"] for rows in claims.values() for r in rows]
    last_client_sequence = 0
    for row in peers["client"]:
        if row["kind"] == 2:
            if row["claimSeq"] <= last_client_sequence:
                audit.issue("duplicate_or_reordered_client_claim_sequence", "client", row)
            last_client_sequence = row["claimSeq"]
        if row["kind"] == 1 and row["afterHp"] != row["beforeHp"]:
            audit.issue("unadmitted_client_native_delta", "client", row)
            audit.applications.append({"peer": "client", "serial": row["serial"], "netId": row["netId"],
                                       "before": row["beforeHp"], "after": row["afterHp"], "sources": []})
    for event, rows in client_hits.items():
        if claimed_hits.count(event) != 1:
            audit.issue("unresolved_or_duplicate_client_hit", "client", rows[0])
    publications = defaultdict(list)
    pending = defaultdict(list)
    published_hp = {n: r["targetHp"] for n, r in hmap.items()}
    consumed = set()
    for row in peers["host"]:
        net = row["netId"]
        if row["kind"] in (1, 3):
            pending[net].append(row["event"])
        elif row["kind"] in (4, 7):
            ids = row["causes"]
            # Death has a last-event fallback in the producer. It is valid only
            # for an as-yet unpublished terminal cause; never reuse a published one.
            if ids != pending[net]:
                audit.issue("orphan_or_reused_publication_sources", "host", row,
                            {"expected": pending[net], "recorded": ids})
            before = published_hp.get(net)
            for event in ids:
                cause = causes.get(event)
                if (not cause or cause["netId"] != net or cause["serial"] >= row["serial"] or
                        cause["beforeHp"] != before or event in consumed or not cause["qualified"]):
                    audit.issue("orphan_or_duplicate_publication_cause", "host", row, event)
                if cause:
                    before = cause["afterHp"]
                consumed.add(event)
            if row["beforeHp"] != published_hp.get(net) or row["afterHp"] != before or row["requestedHp"] != before:
                audit.issue("uncaused_publication_hp", "host", row)
            if row["kind"] == 7 and (not ids or row["event"] != ids[-1] or row["afterHp"] != 0):
                audit.issue("unqualified_terminal_cause", "host", row)
            published_hp[net] = row["afterHp"]
            pending[net] = []
            publications[(row["kind"], net, row["hpSequence"], row["payload"])].append(row)
    for net, events in pending.items():
        if events:
            audit.issue("unpublished_sources", "host", detail={"netId": net, "events": events})
    received = defaultdict(list)
    # Each raw batch has one target receipt per entry, even unchanged entries.
    # Otherwise a companion entry's HP change could disappear behind a valid row.
    for peer, kinds in (("host", (4, 7)), ("client", (5, 8))):
        batches = defaultdict(list)
        sequence_payloads = {}
        last_sequence = 0
        for row in peers[peer]:
            if row["kind"] in kinds:
                batches[(row["kind"], row["payload"])].append(row)
                if row["kind"] in (4, 5):
                    seq = row["hpSequence"]
                    if seq < last_sequence or (seq in sequence_payloads and sequence_payloads[seq] != row["payload"]):
                        audit.issue("reordered_or_substituted_hp_batch", peer, row)
                    last_sequence = seq
                    sequence_payloads[seq] = row["payload"]
        for (_, raw), batch in batches.items():
            try:
                entries = wire(batch[0])[3]
                if sorted(r["netId"] for r in batch) != sorted(e[0] for e in entries):
                    audit.issue("incomplete_or_duplicate_batch_targets", peer, batch[0])
            except ValueError:
                pass  # Per-row raw validation already reports the failure.
    accepted = defaultdict(list)
    for row in peers["client"]:
        if row["kind"] == 13:
            accepted[row["payload"]].append(row)
            try:
                decoded = wire(row, allow_empty=True)
                if decoded[0] not in (23, 24) or decoded[1] != row["epoch"]:
                    raise ValueError("accepted batch type/epoch")
                expected = sorted(e[0] for e in decoded[3])
                actual = sorted(r["netId"] for r in peers["client"] if r["kind"] in (5, 8) and r["payload"] == row["payload"])
                if expected != actual:
                    audit.issue("accepted_batch_missing_target_receipts", "client", row)
            except ValueError as error:
                audit.issue("invalid_accepted_batch", "client", row, str(error))
    for row in peers["client"]:
        if row["kind"] in (5, 8):
            headers = accepted[row["payload"]]
            if len(headers) != 1 or headers[0]["serial"] >= row["serial"]:
                audit.issue("missing_or_duplicate_accepted_batch", "client", row)
            match = publications.get((4 if row["kind"] == 5 else 7, row["netId"], row["hpSequence"], row["payload"]), [])
            row["publication"] = match[0] if len(match) == 1 else None
            if len(match) != 1:
                audit.issue("orphan_or_ambiguous_receive", "client", row)
            if row["causes"]:
                audit.issue("unexpected_client_source_association", "client", row)
            received[(row["kind"], row["netId"], row["hpSequence"])].append(row)
    local_hp = {n: r["targetHp"] for n, r in cmap.items()}
    used_receive = set()
    for row in peers["client"]:
        if row["kind"] not in (6, 9):
            continue
        net = row["netId"]
        match = received.get((5 if row["kind"] == 6 else 8, net, row["hpSequence"]), [])
        receive = match[0] if len(match) == 1 else None
        if not receive or receive["serial"] >= row["serial"] or receive["serial"] in used_receive:
            audit.issue("orphan_or_duplicate_application", "client", row)
        if receive:
            used_receive.add(receive["serial"])
        pub = receive.get("publication") if receive else None
        ids = pub["causes"] if pub else []
        if (row["beforeHp"] != local_hp.get(net) or row["afterHp"] != row["requestedHp"] or
                row["afterHp"] < 0 or row["afterHp"] > row["beforeHp"] or
                row["returned"] != 1 or row["readback"] != 1 or
                (pub and row["afterHp"] != pub["requestedHp"])):
            audit.issue("unaccounted_client_delta", "client", row)
        if row["afterHp"] != row["beforeHp"]:
            if row["attempted"] != 1:
                audit.issue("unattempted_client_change", "client", row)
            if not ids:
                audit.issue("orphan_application", "client", row)
            elif len(ids) > 1:
                audit.issue("multiple_sources_in_application", "client", row, ids)
            else:
                cause = causes.get(ids[0])
                if not cause or (cause["beforeHp"], cause["afterHp"]) != (row["beforeHp"], row["afterHp"]):
                    audit.issue("source_delta_mismatch", "client", row)
            audit.applications.append({"peer": "client", "serial": row["serial"], "netId": net,
                                       "before": row["beforeHp"], "after": row["afterHp"], "sources": ids})
        local_hp[net] = row["afterHp"]
    for row in peers["client"]:
        if row["kind"] in (5, 8) and row["serial"] not in used_receive:
            audit.issue("unapplied_receive", "client", row, category="coverage")
    for identifier, pubs in publications.items():
        for pub in pubs:
            if not any(r.get("publication") is pub for r in peers["client"]):
                audit.issue("publication_without_receive", "host", pub, category="coverage")
    for peer, rows in peers.items():
        allowed = {0, 1, 3, 4, 7, 11, 12, 13} if peer == "host" else {0, 1, 2, 5, 6, 8, 9, 11, 12, 13}
        last_hit, last_frame = 0, 0
        for row in rows:
            if row["kind"] not in allowed:
                audit.issue("unexpected_role_operation", peer, row)
            if row["kind"] == 1:
                if not last_hit < row["event"] < (1 << 63):
                    audit.issue("reordered_or_invalid_native_hit_sequence", peer, row)
                last_hit = row["event"]
            if row["frame"] < last_frame:
                audit.issue("local_frame_regression", peer, row, category="coverage")
            last_frame = row["frame"]
    if not audit.applications:
        audit.issue("no_applied_changes", category="coverage")
    audit.issue("native_coverage_unqualified_schema1", category="coverage")
    audit.issue("runtime_session_and_shutdown_coverage_unqualified", category="coverage")
    graph_ok = not any(i["category"] == "graph" for i in audit.issues)
    recorded_ok = not any(i["category"] == "coverage" and i["code"] not in
                          ("native_coverage_unqualified_schema1", "runtime_session_and_shutdown_coverage_unqualified") for i in audit.issues)
    orphan_codes = {"orphan_or_duplicate_claim", "orphan_claim_hit", "unresolved_or_duplicate_claim",
                    "unresolved_or_duplicate_client_hit", "orphan_or_reused_publication_sources",
                    "orphan_or_duplicate_publication_cause", "unpublished_sources", "orphan_or_ambiguous_receive",
                    "orphan_or_duplicate_application", "orphan_application"}
    return {"schemaVersion": 1, "status": "FAIL" if not graph_ok else "INCONCLUSIVE",
            "acceptance": False, "exactlyOneSourceProven": False,
            "recordedGraphConsistent": graph_ok and recorded_ok, "coverageQualified": False,
            "applications": audit.applications, "applicationCount": len(audit.applications),
            "recordedRanges": {p: {"firstReceiptLine": rows[0]["line"] if rows else None,
                                   "lastReceiptLine": rows[-1]["line"] if rows else None} for p, rows in peers.items()},
            "sourceRecords": [{"peer": "host", "event": event, "line": row["line"], "serial": row["serial"],
                               "kind": KINDS[row["kind"]], "netId": row["netId"],
                               "locallyQualified": bool(row["qualified"]),
                               "before": row["beforeHp"], "after": row["afterHp"]} for event, row in causes.items()],
            "orphanFindings": [i for i in audit.issues if i["code"] in orphan_codes],
            "duplicateFindings": [i for i in audit.issues if "duplicate" in i["code"] or "reused" in i["code"]],
            "unqualifiedCoverage": [i for i in audit.issues if i["category"] == "coverage"],
            "issues": audit.issues, "limits": LIMITS}


def audit_paths(host, client):
    paths = {"host": Path(host), "client": Path(client)}
    raw = {p: path.read_bytes() for p, path in paths.items()}
    texts = {p: data.decode("utf-8", errors="replace") for p, data in raw.items()}
    report = audit_texts(texts["host"], texts["client"])
    report["inputs"] = {p: {"path": str(path.resolve()), "bytes": len(raw[p]),
                             "sha256": hashlib.sha256(raw[p]).hexdigest()} for p, path in paths.items()}
    if any("\ufffd" in text for text in texts.values()):
        report["issues"].append({"code": "invalid_log_encoding", "category": "coverage"})
        report["recordedGraphConsistent"] = False
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)
    if args.host.resolve() == args.client.resolve():
        parser.error("host and client must be distinct saved logs")
    if args.output and args.output.resolve() in (args.host.resolve(), args.client.resolve()):
        parser.error("output must not overwrite an input log")
    try:
        if args.host.samefile(args.client):
            parser.error("host and client must be distinct saved logs")
        if args.output and args.output.exists() and any(args.output.samefile(p) for p in (args.host, args.client)):
            parser.error("output must not overwrite an input log")
        report = audit_paths(args.host, args.client)
        encoded = json.dumps(report, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
            print(json.dumps({"output": str(args.output), "status": report["status"],
                              "applicationCount": report["applicationCount"], "acceptance": False}))
        else:
            print(encoded, end="")
        return 1  # Existing schema cannot prove qualified coverage, even for zero issues.
    except OSError as error:
        print(json.dumps({"error": str(error), "scope": "local file I/O"}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
