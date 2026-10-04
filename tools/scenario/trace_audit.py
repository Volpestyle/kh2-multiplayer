"""Audit saved native trace logs, without accessing KH2 or any process.

Usage: python -B tools/scenario/trace_audit.py LOG [LOG ...] --output LOCAL.json
Exit 0: complete supplied trace envelope at its recorded summary cutoff.
Exit 1: incomplete provenance. Exit 2: usage or file error.
These exit codes never describe KH2 scenario acceptance.
"""
from __future__ import annotations

import argparse
from bisect import bisect_left
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys


KV = re.compile(r"(?<!\S)([A-Za-z][\w-]*)=")
TAG = re.compile(r"\[(spawntrace|lifecycletrace|hittrace|damagepolicy)\]\s+([\w-]+)(?::)?(?:\s+(.*))?$")
COVERAGE_ISSUES = {
    "trace_unavailable_at_summary", "nonzero_coverage_counter", "missing_summary",
    "events_after_latest_summary", "published_events_missing_at_eof", "started_events_not_fully_published",
    "controller_state_unavailable", "cache_state_unavailable", "trace_hook_unavailable",
    "incomplete_event_outcome", "incomplete_wrapper_tick_scope", "observed_actor_unavailable",
    "spawn_record_unavailable", "unobserved_producer_hooks", "spawn_trace_not_recorded",
    "lifecycle_trace_not_recorded", "hook_not_ready", "unknown_wrapper_scope", "unknown_lifecycle_kind",
    "unknown_caller_scope", "interrupted_native_call", "lifecycle_actor_unavailable",
    "incomplete_lifecycle_stamp", "event_crossed_lifecycle", "record_location_changed",
    "event_without_ready_hook", "post_actor_not_comparable", "unknown_trace_record",
    "controller_not_comparable", "actor_controller_mismatch",
    "role_availability_not_recorded", "capture_role_unavailable", "unknown_role_value",
    "binding_capture_role_unverified",
    "ordinary_record_index_unavailable", "generated_point_unavailable",
    "lifecycle_record_unavailable", "lifecycle_classification_unavailable", "lifecycle_actor_out_of_scope",
    "predicate_coverage_unavailable", "predicate_scope_incomplete", "predicate_branch_unknown",
    "predicate_supplement_missing", "predicate_summary_missing", "predicate_counter_loss",
    "native_hit_coverage_unavailable", "native_hit_scope_unverified", "native_hit_summary_missing",
    "native_hit_counter_loss", "native_hit_pending_tail",
}
LIMITS = [
    "Local addresses are correlation hints within one log, never portable actor identities.",
    "netId and bindingEpoch describe a current binding, never creation identity or creation epoch.",
    "Counts and missing events do not attribute a producer, removal cause, death cause, or authority.",
    "A ready hook with no observed calls does not prove that no relevant native calls occurred elsewhere.",
    "A periodic summary is a cutoff, not a shutdown flush or proof of complete game-lifetime coverage.",
    "A dispatcher return address in the DLL can result from the hook/trampoline and a script tail-jump. An unknown return RVA is an instrumentation limit, not evidence that no script ran; an explicit script scope remains an observed dynamic enclosure.",
    "role=0 with roleAvailable=0 or an absent availability bit is unknown, never known Off. Raw NOW bytes can remain readable without a valid native lifecycle stamp or current binding.",
    "An unavailable record index leaves ordinary controller/table membership unverified. Raw record bytes and returned actors remain observations, never inferred creation identities.",
    "This audits recorded diagnostic structure, never overall KH2 acceptance or native parity.",
    "Predicate coverage is reported separately; a historical envelope has no new predicate coverage unless explicitly recorded.",
    "Budget and removal operand snapshots do not establish executed branches. Only genuine returns in a fully covered scope can do so.",
    "An allocator null return does not establish heap exhaustion. An admission rejection does not establish object type four.",
    "A completed removal predicate precedes later removal; actor addresses, ordering and stamps do not establish a causal parent or incarnation.",
    "Native-hit observations concern ordinary incoming client-local damage from local enemy AI, not step 2 host-replicated attacks or the full victim-ownership rejection matrix.",
    "ApplyHitDamage returns are raw ABI values, not semantic success or HP. Native-hit outcomes require checked HP and the actual adjusted nested delta.",
    "Names, frames, attack-parameter IDs and actor/hit addresses are not attack-lifetime or creation-incarnation identities. Missing hit observations never establish absence of damage.",
]


class Audit:
    def __init__(self):
        self.issues = []

    def issue(self, code, line=None, detail=None):
        item = {"code": code, "category": "coverage" if code in COVERAGE_ISSUES else "structure"}
        if line is not None:
            item["line"] = line
        if detail is not None:
            item["detail"] = detail
        self.issues.append(item)

    def integer(self, row, key, *, base=10, minimum=0, required=True):
        value = row["fields"].get(key)
        if value is None:
            if required:
                self.issue("missing_field", row["line"], key)
            return None
        try:
            parsed = int(value, base)
            if parsed < minimum:
                raise ValueError()
            return parsed
        except ValueError:
            self.issue("invalid_integer", row["line"], key)
            return None

    def bit(self, row, key):
        value = self.integer(row, key)
        if value is not None and value not in (0, 1):
            self.issue("invalid_validity_bit", row["line"], key)
            return None
        return value

    def hex(self, row, key, size):
        value = row["fields"].get(key, "")
        if not re.fullmatch(rf"[0-9a-fA-F]{{{size * 2}}}", value):
            self.issue("invalid_hex_bytes", row["line"], key)
            return None
        return bytes.fromhex(value)


def parse_lines(text, audit):
    rows = []
    for number, line in enumerate(text.splitlines(keepends=True), 1):
        if not any(prefix in line for prefix in ("[spawntrace", "[lifecycletrace", "[hittrace", "[damagepolicy")):
            continue
        if not line.endswith(("\n", "\r")):
            audit.issue("unterminated_trace_line", number)
        match = TAG.search(line.rstrip("\r\n"))
        if not match:
            audit.issue("malformed_trace_line", number)
            continue
        family, kind, body = match.groups()
        body = body or ""
        fields = {}
        parts = list(KV.finditer(body))
        free = body[:parts[0].start()].strip() if parts else body
        for i, part in enumerate(parts):
            key = part[1]
            value = body[part.end():parts[i + 1].start() if i + 1 < len(parts) else len(body)].strip()
            if key in fields:
                audit.issue("duplicate_field", number, key)
            fields[key] = value
        if free and kind not in ("unavailable",):
            audit.issue("unexpected_unstructured_text", number, kind)
        rows.append({"family": family, "kind": kind, "line": number,
                     "fields": fields, "message": free})
    return rows


def check_counters(rows, events, audit, lifecycle=False):
    """Summaries are emitted before a drain; compare at their line positions."""
    summaries = []
    previous = {}
    for row in rows:
        if row["kind"] not in ("summary", "loss"):
            continue
        names = ("started", "published", "dropped", "unavailable", "nativeFaults")
        if row["kind"] == "summary":
            names += (("drained", "outOfScope", "unwound", "depthOverflow") if lifecycle
                      else ("drained", "unsupportedCaller"))
            if not lifecycle and audit.bit(row, "available") != 1:
                audit.issue("trace_unavailable_at_summary", row["line"])
        values = {key: audit.integer(row, key) for key in names}
        audit.integer(row, "lastException", base=16)
        for key, value in values.items():
            if value is None:
                continue
            if key in previous and value < previous[key]:
                audit.issue("counter_regressed", row["line"], key)
            previous[key] = value
            if key in ("dropped", "unavailable", "nativeFaults", "unsupportedCaller", "outOfScope", "unwound", "depthOverflow") and value:
                audit.issue("nonzero_coverage_counter", row["line"], {key: value})
        if row["kind"] != "summary":
            continue
        started, published, drained = (values.get(k) for k in ("started", "published", "drained"))
        if None not in (started, published, drained) and not drained <= published <= started:
            audit.issue("inconsistent_counter_order", row["line"])
        count_before = sum(e["line"] < row["line"] for e in events)
        if drained is not None and drained != count_before:
            audit.issue("drained_event_count_mismatch", row["line"],
                        {"drained": drained, "eventHeadersBeforeSummary": count_before})
        summaries.append({"line": row["line"], **values,
                          "eventHeadersBeforeSummary": count_before})
    if not summaries:
        audit.issue("missing_summary")
        return {"latest": None, "pendingTail": "unknown"}
    latest = summaries[-1]
    after = sum(e["line"] > latest["line"] for e in events)
    published = latest.get("published")
    known_unobserved = max(0, published - len(events)) if published is not None else None
    if after:
        audit.issue("events_after_latest_summary", latest["line"], after)
    if known_unobserved:
        audit.issue("published_events_missing_at_eof", latest["line"], known_unobserved)
    if latest.get("started") != latest.get("published"):
        audit.issue("started_events_not_fully_published", latest["line"])
    return {"latest": latest, "summaryCount": len(summaries),
            "eventHeadersAfterLatestSummary": after,
            "knownPublishedNotObservedAtEof": known_unobserved,
            "pendingTail": "unverified" if after or known_unobserved else "none recorded at latest summary cutoff"}


def check_capture_role(row, key, audit):
    """Legacy absence and foreign-thread zero are unknown, not Role::Off."""
    fields = row["fields"]
    if "roleAvailable" not in fields:
        audit.issue("role_availability_not_recorded", row["line"])
        available = None
    else:
        available = audit.bit(row, "roleAvailable")
        if available != 1:
            audit.issue("capture_role_unavailable", row["line"])
    value = audit.integer(row, key)
    known = available == 1 and value in (0, 1, 2)
    if available == 1 and value not in (0, 1, 2):
        audit.issue("unknown_role_value", row["line"])
    if not known and (fields.get("netId", "0") != "0" or fields.get("bindingEpoch", "0") != "0"):
        audit.issue("binding_capture_role_unverified" if available is None else "binding_without_capture_role", row["line"])
    return {"available": available, "rawValue": fields.get(key),
            "meaning": {0: "off", 1: "host", 2: "client"}[value] if known else "unknown"}


def check_states(seq, rows, audit, phases=("before", "after"), optional_phases=()):
    result = {}
    states = [r for r in rows if r["kind"] == "state"]
    chunks = [r for r in rows if r["kind"] == "cache"]
    for phase in phases:
        phase_states = [r for r in states if r["fields"].get("phase") == phase]
        phase_chunks = [r for r in chunks if r["fields"].get("phase") == phase]
        if len(phase_states) != 1:
            audit.issue("state_count_mismatch", detail={"seq": seq, "phase": phase, "count": len(phase_states)})
            continue
        state = phase_states[0]
        if audit.bit(state, "controllerAvailable") != 1 and phase not in optional_phases:
            audit.issue("controller_state_unavailable", state["line"], phase)
        available = audit.bit(state, "cacheAvailable")
        if available != 1 and phase not in optional_phases:
            audit.issue("cache_state_unavailable", state["line"], phase)
        for key in ("key", "headerId", "records", "flags", "stage", "currentCountU32", "initialCountU32", "activation"):
            audit.integer(state, key)
        for key in ("header", "cacheBucket"):
            audit.integer(state, key, base=16)
        if "-" in phase or state["family"] == "lifecycletrace":
            audit.integer(state, "spawnArray", base=16)
            audit.integer(state, "nativeType")
        for key in ("cacheRoom", "cacheAge"):
            audit.integer(state, key, minimum=-(2 ** 31))
        try:
            if not math.isfinite(float(state["fields"].get("cooldown", ""))):
                raise ValueError()
        except ValueError:
            audit.issue("invalid_cooldown", state["line"])
        pieces = {}
        for chunk in phase_chunks:
            match = re.fullmatch(r"([0-7])/8", chunk["fields"].get("chunk", ""))
            if not match:
                audit.issue("invalid_cache_chunk_index", chunk["line"])
                continue
            index = int(match[1])
            if index in pieces:
                audit.issue("duplicate_cache_chunk", chunk["line"], {"seq": seq, "phase": phase, "chunk": index})
            value = audit.hex(chunk, "idsU16Hex", 64)
            pieces[index] = chunk["fields"].get("idsU16Hex") if value is not None else None
        if available == 1 and (set(pieces) != set(range(8)) or any(v is None for v in pieces.values())):
            audit.issue("incomplete_cache_chunks", state["line"], {"seq": seq, "phase": phase})
        if available == 0 and phase_chunks:
            audit.issue("cache_chunks_with_unavailable_state", state["line"], phase)
        result[phase] = {"fields": state["fields"], "cacheChunks": pieces}
    for row in states + chunks:
        if row["fields"].get("phase") not in phases:
            audit.issue("unknown_state_phase", row["line"])
    return result


def audit_spawn(rows, audit):
    known_kinds = {"ready", "unavailable", "component-unavailable", "summary", "loss", "event", "record", "state", "cache", "factory"}
    for row in rows:
        if row["kind"] not in known_kinds:
            audit.issue("unknown_trace_record", row["line"], row["kind"])
        if row["kind"] in ("unavailable", "component-unavailable"):
            audit.issue("trace_hook_unavailable", row["line"], row["message"])
    ready = [r for r in rows if r["kind"] in ("ready", "unavailable")]
    hooks = {key: {"ready": False, "observedEventCount": 0}
             for key in ("fixed", "generated", "dispatcher", "script42DC10")}
    expanded = any("fixed" in r["fields"] for r in ready)
    if len(ready) != 1:
        audit.issue("readiness_count_mismatch", detail=len(ready))
    for row in ready:
        if "fixed" in row["fields"]:
            for hook in hooks:
                hooks[hook]["ready"] = audit.bit(row, hook) == 1
                if not hooks[hook]["ready"]:
                    audit.issue("hook_not_ready", row["line"], hook)
            if row["fields"].get("callers") != "all":
                audit.issue("unknown_wrapper_scope", row["line"])
        elif row["kind"] == "ready":
            wrapper = audit.integer(row, "wrapper", base=16)
            caller = audit.integer(row, "return", base=16)
            if wrapper != 0x3FE590 or caller != 0x3FE83F:
                audit.issue("unknown_wrapper_scope", row["line"])
            elif audit.integer(row, "status") == 0:
                hooks["fixed"]["ready"] = True
        audit.integer(row, "queueCap", minimum=1)
        audit.integer(row, "tickCap", minimum=1)
    groups = defaultdict(list)
    events = []
    for row in rows:
        if row["kind"] in ("event", "record", "state", "cache"):
            seq = audit.integer(row, "seq", minimum=1)
            if seq is not None:
                groups[seq].append(row)
        if row["kind"] == "event":
            events.append(row)
    details = []
    observed_sequences = []
    outcomes = Counter()
    returned = Counter()
    for seq, group in sorted(groups.items()):
        headers = [r for r in group if r["kind"] == "event"]
        if len(headers) != 1:
            audit.issue("event_header_count_mismatch", detail={"seq": seq, "count": len(headers)})
            continue
        row = headers[0]
        observed_sequences.append(seq)
        fields = row["fields"]
        capture_role = check_capture_role(row, "captureRole", audit)
        extended = "wrapper" in fields
        wrapper = fields.get("wrapper", "fixed")
        if wrapper not in ("fixed", "generated"):
            audit.issue("unknown_wrapper_scope", row["line"], wrapper)
        elif not hooks[wrapper]["ready"]:
            audit.issue("event_without_ready_hook", row["line"], wrapper)
        outcome = fields.get("outcome", "unknown")
        outcomes[outcome] += 1
        if outcome not in ("observed", "null-return"):
            audit.issue("incomplete_event_outcome", row["line"], outcome)
        tick = audit.integer(row, "tick")
        for key in ("stampAvailable", "postStampAvailable", "lifecycleStable"):
            if audit.bit(row, key) != 1:
                audit.issue("incomplete_wrapper_tick_scope", row["line"], key)
        enclosing_tick = audit.bit(row, "enclosingTick")
        tick_complete = audit.bit(row, "tickComplete")
        if (enclosing_tick == 1 and (not tick or tick_complete != 1)) or (not extended and enclosing_tick != 1):
            audit.issue("missing_enclosing_tick_identity", row["line"])
        if extended:
            if audit.bit(row, "wrapperComplete") != 1:
                audit.issue("interrupted_native_call", row["line"], "wrapperComplete")
            if fields.get("wrapperOutcome") not in ("observed", "null-return"):
                audit.issue("incomplete_event_outcome", row["line"], fields.get("wrapperOutcome"))
            if audit.bit(row, "callerRvaAvailable") != 1:
                audit.issue("unknown_caller_scope", row["line"])
            audit.integer(row, "callerRva", base=16)
            for hook, bit, identity, caller, caller_bit in (
                ("dispatcher", "enclosingDispatcher", "dispatcherSeq", "dispatcherCallerRva", "dispatcherCallerRvaAvailable"),
                ("script42DC10", "enclosingScript42DC10", "scriptSeq", "scriptCallerRva", "scriptCallerRvaAvailable"),
            ):
                inside = audit.bit(row, bit)
                scope_id = audit.integer(row, identity)
                available = audit.bit(row, caller_bit)
                audit.integer(row, caller, base=16)
                if inside == 1:
                    hooks[hook]["observedEventCount"] += 1
                    if not hooks[hook]["ready"]:
                        audit.issue("event_without_ready_hook", row["line"], hook)
                    if not scope_id or available != 1:
                        audit.issue("unknown_caller_scope", row["line"], hook)
                elif scope_id:
                    audit.issue("scope_identity_without_enclosing_scope", row["line"], hook)
            audit.integer(row, "dispatcherRegion", base=16)
            if audit.bit(row, "recordIndexAvailable") != 1:
                audit.issue("ordinary_record_index_unavailable", row["line"])
            audit.bit(row, "objectIdMatchesRecord")
            point_available = audit.bit(row, "generatedPointAvailable")
            if wrapper == "generated" and point_available != 1:
                audit.issue("generated_point_unavailable", row["line"])
            if point_available == 1:
                try:
                    point = [float(v) for v in fields.get("generatedPoint", "").strip("()").split(",")]
                    if len(point) != 4 or not all(math.isfinite(v) for v in point):
                        raise ValueError()
                except ValueError:
                    audit.issue("malformed_generated_point", row["line"])
        for key in ("captureRole", "drainRole", "drainGeneration", "transition", "load", "postTransition", "postLoad",
                    "recordIndex", "nativeRecordId", "objectId", "actorType", "actorObjectId", "bindingEpoch"):
            audit.integer(row, key)
        for key in ("controller", "record", "actor", "objentry", "status", "actorController", "actorRecord"):
            audit.integer(row, key, base=16)
        for key in ("hp", "maxHp", "netId"):
            audit.integer(row, key, minimum=-(2 ** 31))
        actor_available = audit.bit(row, "actorAvailable")
        audit.bit(row, "censusComplete")
        confirmed = audit.bit(row, "censusConfirmed")
        if outcome == "observed" and actor_available != 1:
            audit.issue("observed_actor_unavailable", row["line"])
        if actor_available == 1 and any(fields.get(k, "0").strip("0") == "" for k in ("actor", "objentry", "status")):
            audit.issue("available_actor_has_null_identity", row["line"])
        if outcome == "observed" and (fields.get("actorType") not in ("3", "4") or
                fields.get("actorController") != fields.get("controller") or fields.get("actorRecord") != fields.get("record")):
            audit.issue("observed_actor_provenance_mismatch", row["line"])
        if outcome == "null-return" and (fields.get("actor", "0").strip("0") or actor_available != 0):
            audit.issue("null_return_has_actor_claim", row["line"])
        if confirmed == 1 and (actor_available != 1 or fields.get("censusComplete") != "1"):
            audit.issue("invalid_census_confirmation", row["line"])
        if fields.get("netId", "0") != "0" and (confirmed != 1 or fields.get("bindingEpoch") == "0"):
            audit.issue("binding_without_confirmation", row["line"])
        if fields.get("transition") != fields.get("postTransition") or fields.get("load") != fields.get("postLoad"):
            audit.issue("event_crossed_lifecycle", row["line"])
        records = [r for r in group if r["kind"] == "record"]
        if len(records) != 1:
            audit.issue("record_count_mismatch", row["line"], {"seq": seq, "count": len(records)})
        else:
            record = records[0]
            if audit.bit(record, "available") != 1:
                audit.issue("spawn_record_unavailable", record["line"])
            raw = audit.hex(record, "bytes", 64)
            before = audit.hex(record, "beforeNow", 10)
            after = audit.hex(record, "afterNow", 10)
            if before is not None and after is not None and before != after:
                audit.issue("record_location_changed", record["line"])
            if raw is not None and record["fields"].get("available") == "1":
                if str(int.from_bytes(raw[:4], "little")) != fields.get("objectId"):
                    audit.issue("record_object_mismatch", record["line"])
                if str(int.from_bytes(raw[0x1E:0x20], "little")) != fields.get("nativeRecordId"):
                    audit.issue("native_record_id_mismatch", record["line"])
        phases = ("tick-before", "tick-after", "wrapper-before", "wrapper-after") if extended else ("before", "after")
        optional = ("tick-before", "tick-after") if extended and enclosing_tick == 0 else ()
        states = check_states(seq, group, audit, phases, optional)
        if wrapper in hooks:
            hooks[wrapper]["observedEventCount"] += 1
        if actor_available == 1 and fields.get("actorObjectId") in ("309", "311"):
            returned[(fields["actorObjectId"], wrapper, fields.get("enclosingDispatcher", "unknown"),
                      fields.get("enclosingScript42DC10", "unknown"))] += 1
        details.append({"sequence": seq, "line": row["line"], "fields": fields, "captureRole": capture_role,
                        "record": records[0]["fields"] if len(records) == 1 else None, "states": states})
    if observed_sequences and (observed_sequences[0] != 1 or any(b != a + 1 for a, b in zip(observed_sequences, observed_sequences[1:]))):
        audit.issue("event_sequence_gaps")
    # Legacy wrapper trace is only a call-site subset, never dispatcher coverage.
    if not expanded:
        audit.issue("unobserved_producer_hooks", detail=["generated_wrapper", "dispatcher_3FE700", "script42DC10"])
    for summary in (r for r in rows if r["kind"] == "summary" and expanded):
        for hook, key in (("fixed", "fixedAvailable"), ("generated", "generatedAvailable"),
                          ("dispatcher", "dispatcherAvailable"), ("script42DC10", "scriptAvailable")):
            if audit.bit(summary, key) != 1:
                audit.issue("hook_not_ready", summary["line"], hook)
    return {"hooks": hooks, "readiness": ready, "eventCount": len(events),
            "outcomes": dict(outcomes), "returnedObjectHints": [
                {"actorObjectId": int(key[0]), "wrapper": key[1], "enclosingDispatcher": key[2],
                 "enclosingScript42DC10": key[3], "recordedEvents": count}
                for key, count in sorted(returned.items())], "events": details,
            "counters": check_counters(rows, events, audit)}


LIFECYCLE_HOOKS = {
    0: ("removal-bookkeeping", 0x3FFD90), 1: ("disposal", 0x3B45C0),
    2: ("death-mark", 0x3D4A40), 3: ("death-bookkeeping", 0x3FED10),
    4: ("count-decrement", 0x3FED40),
}
REMOVAL_HOOKS = {5: ("removal-predicate", 0x3DAC30),
                 6: ("script-predicate", 0x3B4420), 7: ("auxiliary-predicate", 0x3CE550)}


def audit_lifecycle(rows, audit):
    kinds = {"ready", "unavailable", "hook", "summary", "loss", "event", "actor", "stamp", "state", "cache", "predicate", "operands"}
    expanded = any(r["kind"] == "predicate" or "predicateStarted" in r["fields"] or
                   r["fields"].get("kindId") == "5" or
                   (r["kind"] == "hook" and r["fields"].get("kind") in ("5", "6", "7")) for r in rows)
    hook_definitions = {**LIFECYCLE_HOOKS, **(REMOVAL_HOOKS if expanded else {})}
    hooks = {name: {"kindId": kind, "rva": f"{rva:X}", "verified": False,
                    "installed": False, "recordedEvents": 0} for kind, (name, rva) in hook_definitions.items()}
    readiness = [r for r in rows if r["kind"] in ("ready", "unavailable")]
    if len(readiness) != 1:
        audit.issue("readiness_count_mismatch", detail={"family": "lifecycletrace", "count": len(readiness)})
    for row in rows:
        if row["kind"] not in kinds:
            audit.issue("unknown_trace_record", row["line"], row["kind"])
        if row["kind"] == "unavailable":
            audit.issue("trace_hook_unavailable", row["line"])
        if row["kind"] in ("ready", "unavailable", "summary"):
            if audit.bit(row, "requested") != 1:
                audit.issue("hook_not_ready", row["line"], "lifecycletrace not requested")
            verified, installed, failed = (audit.integer(row, k) for k in ("verifiedMask", "installedMask", "failedMask"))
            if None not in (verified, installed, failed):
                if (verified | installed | failed) & ~(255 if expanded else 31) or installed & ~verified or installed & failed:
                    audit.issue("invalid_hook_masks", row["line"])
                for kind, (name, _) in hook_definitions.items():
                    hooks[name].update(verified=bool(verified & (1 << kind)), installed=bool(installed & (1 << kind)))
                    if not installed & (1 << kind) or failed & (1 << kind):
                        audit.issue("hook_not_ready", row["line"], name)
        if row["kind"] == "hook":
            kind = audit.integer(row, "kind")
            rva = audit.integer(row, "rva", base=16)
            verified, installed = audit.bit(row, "verified"), audit.bit(row, "installed")
            if kind not in hook_definitions or rva != hook_definitions[kind][1]:
                audit.issue("unknown_lifecycle_kind", row["line"])
            if verified != 1 or installed != 1:
                audit.issue("hook_not_ready", row["line"], kind)
    groups = defaultdict(list)
    events = []
    for row in rows:
        if row["kind"] in ("event", "actor", "stamp", "state", "cache"):
            seq = audit.integer(row, "seq", minimum=1)
            if seq is not None:
                groups[seq].append(row)
        if row["kind"] == "event":
            events.append(row)
    details, ancestry = [], {}
    for seq, group in sorted(groups.items()):
        headers = [r for r in group if r["kind"] == "event"]
        if len(headers) != 1:
            audit.issue("event_header_count_mismatch", detail={"family": "lifecycletrace", "seq": seq, "count": len(headers)})
            continue
        row = headers[0]
        fields = row["fields"]
        capture_role = check_capture_role(row, "role", audit)
        kind = audit.integer(row, "kindId")
        if kind not in hook_definitions or kind in (6, 7) or fields.get("kind") != hook_definitions[kind][0]:
            audit.issue("unknown_lifecycle_kind", row["line"])
        else:
            hook = hooks[hook_definitions[kind][0]]
            hook["recordedEvents"] += 1
            if not hook["installed"]:
                audit.issue("event_without_ready_hook", row["line"], fields.get("kind"))
        parent, depth = audit.integer(row, "parent"), audit.integer(row, "depth")
        ancestry[seq] = (parent, depth, row["line"])
        for key in ("role", "bindingEpoch", "drainGeneration"):
            audit.integer(row, key)
        audit.integer(row, "netId", minimum=-(2 ** 31))
        for key in ("callerRva", "actorArgument", "controllerArgument", "exceptionCode"):
            audit.integer(row, key, base=16)
        flags = {key: audit.bit(row, key) for key in (
            "callerInImage", "originalReturned", "unwound", "lifecycleStable", "postActorComparable",
            "beforeStampAvailable", "afterStampAvailable", "censusComplete", "censusCurrentPresent",
            "unavailable", "outOfScope", "controllerComparable", "controllerFromActor", "actorControllerMismatch")}
        if flags["callerInImage"] != 1:
            audit.issue("unknown_caller_scope", row["line"])
        if flags["originalReturned"] != 1 or flags["unwound"] != 0:
            audit.issue("interrupted_native_call", row["line"])
        if flags["unavailable"] != 0 or flags["outOfScope"] != 0:
            audit.issue("incomplete_event_outcome", row["line"], {key: flags[key] for key in ("unavailable", "outOfScope")})
        if flags["lifecycleStable"] != 1:
            audit.issue("event_crossed_lifecycle", row["line"])
        if flags["controllerComparable"] != 1 and fields.get("controllerArgument", "0").strip("0"):
            audit.issue("controller_not_comparable", row["line"])
        if flags["actorControllerMismatch"] != 0:
            audit.issue("actor_controller_mismatch", row["line"])
        count_only = kind == 4
        if flags["postActorComparable"] != 1 and not count_only:
            audit.issue("post_actor_not_comparable", row["line"])
        if count_only and (fields.get("actorArgument") != "0" or flags["postActorComparable"] != 0):
            audit.issue("count_event_has_actor_claim", row["line"])
        if count_only and (flags["censusCurrentPresent"] != 0 or fields.get("netId") != "0" or fields.get("bindingEpoch") != "0"):
            audit.issue("count_event_has_binding_claim", row["line"])
        if fields.get("netId", "0") != "0" and (flags["censusCurrentPresent"] != 1 or flags["censusComplete"] != 1 or fields.get("bindingEpoch") == "0"):
            audit.issue("binding_without_confirmation", row["line"])
        actors, stamps = {}, {}
        for phase in ("before", "after"):
            actor_rows = [r for r in group if r["kind"] == "actor" and r["fields"].get("phase") == phase]
            stamp_rows = [r for r in group if r["kind"] == "stamp" and r["fields"].get("phase") == phase]
            if len(actor_rows) != 1:
                audit.issue("actor_snapshot_count_mismatch", row["line"], phase)
            else:
                actor = actor_rows[0]
                actor_flags = {key: audit.bit(actor, key) for key in ("available", "classificationAvailable", "isCombat", "recordAvailable")}
                if actor_flags["available"] != 1 and not count_only:
                    audit.issue("lifecycle_actor_unavailable", actor["line"], phase)
                if not count_only:
                    if actor_flags["recordAvailable"] != 1:
                        audit.issue("lifecycle_record_unavailable", actor["line"], phase)
                    if actor_flags["classificationAvailable"] != 1:
                        audit.issue("lifecycle_classification_unavailable", actor["line"], phase)
                    elif actor_flags["isCombat"] != 1:
                        audit.issue("lifecycle_actor_out_of_scope", actor["line"], phase)
                if count_only:
                    if any(value != 0 for value in actor_flags.values()):
                        audit.issue("count_event_has_actor_claim", actor["line"])
                    for key, value in actor["fields"].items():
                        if key not in ("seq", "phase") and not re.fullmatch(r"0+(?:\.0+)?", value):
                            audit.issue("count_event_nonzero_actor_placeholder", actor["line"], key)
                for key in ("actor", "objentry", "status", "controller", "record"):
                    audit.integer(actor, key, base=16)
                for key in ("objectId", "actorType", "recordId", "recordMode", "recordStage", "flags120", "flags9B8", "flags6C8"):
                    audit.integer(actor, key)
                for key in ("hp", "maxHp"):
                    audit.integer(actor, key, minimum=-(2 ** 31))
                for key in ("fadeA08", "slopeA0C", "fadeAAC", "slopeAB0"):
                    try:
                        if not math.isfinite(float(actor["fields"].get(key, ""))):
                            raise ValueError()
                    except ValueError:
                        audit.issue("invalid_actor_float", actor["line"], key)
                actors[phase] = actor["fields"]
            if len(stamp_rows) != 1:
                audit.issue("stamp_count_mismatch", row["line"], phase)
            else:
                stamp = stamp_rows[0]
                audit.integer(stamp, "transition")
                audit.integer(stamp, "load")
                audit.hex(stamp, "nowHex", 10)
                stamps[phase] = stamp["fields"]
            if flags[phase + "StampAvailable"] != 1:
                audit.issue("incomplete_lifecycle_stamp", row["line"], phase)
        for member in group:
            if member["kind"] in ("actor", "stamp") and member["fields"].get("phase") not in ("before", "after"):
                audit.issue("unknown_state_phase", member["line"])
        if len(stamps) == 2 and flags["lifecycleStable"] == 1:
            if any(stamps["before"].get(k) != stamps["after"].get(k) for k in ("transition", "load", "nowHex")):
                audit.issue("stable_lifecycle_stamp_mismatch", row["line"])
        if len(actors) == 2 and flags["postActorComparable"] == 1:
            keys = ("available", "actor", "objentry", "status", "controller", "record", "objectId",
                    "actorType", "isCombat", "recordAvailable")
            if actors["before"].get("recordAvailable") == "1":
                keys += ("recordId", "recordMode", "recordStage")
            if any(actors["before"].get(k) != actors["after"].get(k) for k in keys):
                audit.issue("comparable_actor_metadata_mismatch", row["line"])
        details.append({"sequence": seq, "line": row["line"], "fields": fields, "captureRole": capture_role, "actors": actors,
                        "stamps": stamps, "states": check_states(seq, group, audit)})
    for seq, (parent, depth, line) in ancestry.items():
        if parent:
            if parent not in ancestry:
                audit.issue("missing_parent_event", line)
            elif parent >= seq or depth is None or ancestry[parent][1] != depth - 1:
                audit.issue("inconsistent_event_nesting", line)
        elif depth != 0:
            audit.issue("inconsistent_event_nesting", line)
    sequences = sorted(ancestry)
    if sequences and (sequences[0] != 1 or any(b != a + 1 for a, b in zip(sequences, sequences[1:]))):
        audit.issue("event_sequence_gaps", detail="lifecycletrace")
    return {"hooks": hooks, "readiness": readiness, "eventCount": len(events), "events": details,
            "counters": check_counters(rows, events, audit, lifecycle=True),
            "exceptionCounting": "nativeFaults counts observer filter hits; nested hooks can observe one native exception multiple times"}


def predicate_coverage(coverage_before, row, audit, factory):
    """Require recorded installation before the scope, not a later summary."""
    prefix = "factory" if factory else ""
    keys = tuple(prefix + key[0].upper() + key[1:] if prefix else key
                 for key in ("verifiedMask", "installedMask", "failedMask"))
    required = 3 if factory else 224
    prior = coverage_before.get(row["line"])
    if prior is None:
        audit.issue("predicate_coverage_unavailable", row["line"], "no prior installation masks")
        return False
    verified, installed, failed = (audit.integer(prior, k) for k in keys)
    mask = audit.integer(row, "coverageMask")
    if None in (verified, installed, failed, mask):
        return False
    allowed = 3 if factory else 255
    if (verified | installed | failed | mask) & ~allowed or installed & ~verified or installed & failed:
        audit.issue("invalid_predicate_masks", row["line"])
        return False
    complete = (verified & installed & required) == required and not failed & required and (mask & required) == required
    if not complete:
        audit.issue("predicate_coverage_unavailable", row["line"], {"effectiveMask": mask, "requiredMask": required})
    return complete


def audit_factory(row, event, rows, records, audit):
    start = len(audit.issues)
    fields = row["fields"]
    coverage = predicate_coverage(rows, row, audit, True)
    values = {k: audit.integer(row, k) for k in (
        "coverageSerial", "coverageMask", "depth", "operandMask", "admissionCalls", "admissionResult",
        "allocationCalls", "allocationSize", "outcome")}
    bits = {k: audit.bit(row, k) for k in (
        "eligible", "complete", "unwound", "countOverflow", "admissionReturned", "admissionFault",
        "allocationReturned", "allocationFault")}
    allocation = audit.integer(row, "allocationResult", base=16)
    samples = {}
    for key in ("weightBits", "limitBeforeBits", "usedBeforeBits", "limitAfterBits", "usedAfterBits"):
        raw = audit.integer(row, key, base=16)
        if raw is None or raw > 0xFFFFFFFF:
            audit.issue("invalid_predicate_float_bits", row["line"], key)
            continue
        exponent, fraction = (raw >> 23) & 255, raw & 0x7FFFFF
        samples[key] = {"bits": fields.get(key), "classification":
                        "nan" if exponent == 255 and fraction else "infinity" if exponent == 255 else "finite"}
        if exponent != 255:
            samples[key]["value"] = struct.unpack("<f", struct.pack("<I", raw))[0]
        bit = {"limitBeforeBits": 1, "usedBeforeBits": 2, "limitAfterBits": 4, "usedAfterBits": 8}.get(key)
        samples[key]["available"] = (values["admissionCalls"] not in (None, 0) if bit is None else
                                     values["operandMask"] is not None and bool(values["operandMask"] & bit))
    for key, maximum in (("operandMask", 15), ("admissionResult", 255),
                         ("admissionCalls", 65535), ("allocationCalls", 65535), ("outcome", 4)):
        if values[key] is not None and values[key] > maximum:
            audit.issue("invalid_predicate_value", row["line"], key)
    e = event["fields"]
    actor = audit.integer(event, "actor", base=16)
    object_id = audit.integer(event, "objectId")
    # Same explicit resolver exclusions as NativeSpawnController::DirectObjectId.
    # No table lookup is invented: aliases retain raw observations, no branch.
    aliases = {0x236, 0x237, 0x238, 0x23B, 0x23C, 0x23D, 0x23F, 0x240,
               0x2C0, 0x319, 0x31A, 0x3EE, 0x62A, 0x62B}
    record_eligible = (object_id is not None and 0 < object_id < 0x10000000 and object_id not in aliases and
                       len(records) == 1 and records[0]["fields"].get("available") == "1")
    if record_eligible:
        raw = records[0]["fields"].get("bytes", "")
        record_eligible = bool(re.fullmatch(r"[0-9A-Fa-f]{128}", raw))
        if record_eligible:
            record_eligible = int.from_bytes(bytes.fromhex(raw[:8]), "little") == object_id
    normal = (coverage and values["coverageSerial"] not in (None, 0) and bits["eligible"] == 1 and
              bits["complete"] == 1 and all(bits[k] == 0 for k in
                  ("unwound", "countOverflow", "admissionFault", "allocationFault")) and
              e.get("roleAvailable") == "1" and e.get("wrapperComplete") == "1" and
              e.get("wrapper") in ("fixed", "generated"))
    branch = 0
    if normal and record_eligible and len(audit.issues) == start and values["admissionCalls"] == 1 and bits["admissionReturned"] == 1:
        if values["admissionResult"] == 0 and values["allocationCalls"] == 0 and bits["allocationReturned"] == 0 and allocation == 0 and actor == 0:
            branch = 1
        elif values["admissionResult"] and values["allocationCalls"] == 1 and bits["allocationReturned"] == 1 and values["allocationSize"] == 0xD50:
            if allocation == 0 and actor == 0:
                branch = 2
            elif allocation and actor == allocation:
                branch = 3
    if not normal:
        audit.issue("predicate_scope_incomplete", row["line"], "factory")
    # The producer can conservatively decline classification for extra native
    # eligibility limits. Never upgrade its Unknown/Ambiguous from snapshots.
    if values["outcome"] in (0, 4):
        branch = 0
    if not branch:
        audit.issue("predicate_branch_unknown", row["line"], "factory")
    if (branch and values["outcome"] != branch) or (not branch and values["outcome"] not in (0, 4, None)):
        audit.issue("predicate_branch_mismatch", row["line"], {"recorded": values["outcome"], "derived": branch})
        branch = 0
    names = {0: "unknown", 1: "weight-admission-rejected", 2: "type4-allocation-returned-null", 3: "allocation-and-wrapper-returned-nonnull"}
    return {"sequence": audit.integer(row, "seq"), "fields": fields, "floatSamples": samples,
            "operandSamplesComplete": values["operandMask"] == 15,
            "recordEligible": record_eligible,
            "branch": names[branch], "branchSupported": bool(branch),
            "scopeComplete": normal and bool(branch),
            "classificationScope": "genuine child returns within this wrapper; no allocation-exhaustion or creation-identity claim"}


def audit_removal_predicate(row, operands, event, rows, audit):
    start = len(audit.issues)
    coverage = predicate_coverage(rows, row, audit, False)
    values = {k: audit.integer(row, k) for k in (
        "coverageGeneration", "coverageMask", "parentResult", "scriptCalls", "scriptReturned", "scriptResult",
        "auxiliaryCalls", "auxiliaryReturned", "auxiliaryResult", "faultMask", "unwindMask", "branch",
        "auxiliaryBefore", "auxiliaryAfter", "auxiliaryAvailableMask")}
    argument = audit.integer(row, "auxiliaryArgument", base=16)
    bits = {k: audit.bit(row, k) for k in ("coverageStable", "originalReturned", "resultAvailable", "countOverflow", "nestedAmbiguous")}
    for key in ("parentResult", "scriptResult", "auxiliaryResult"):
        if values[key] is not None and values[key] > 255:
            audit.issue("invalid_predicate_value", row["line"], key)
    if values["auxiliaryAvailableMask"] is not None and values["auxiliaryAvailableMask"] & ~3:
        audit.issue("invalid_predicate_value", row["line"], "auxiliaryAvailableMask")
    for key in ("scriptCalls", "scriptReturned", "auxiliaryCalls", "auxiliaryReturned", "auxiliaryBefore", "auxiliaryAfter"):
        if values[key] is not None and values[key] > 0xFFFFFFFF:
            audit.issue("invalid_predicate_value", row["line"], key)
    samples = {}
    for phase in ("before", "afterScript", "after"):
        found = [r for r in operands if r["fields"].get("phase") == phase]
        if len(found) != 1:
            audit.issue("predicate_operand_count_mismatch", row["line"], phase)
            continue
        item = found[0]
        mask = audit.integer(item, "availableMask")
        if mask is not None and mask & ~31:
            audit.issue("invalid_predicate_value", item["line"], "availableMask")
        for key in ("scriptState", "field80", "field98"):
            audit.integer(item, key, base=16)
        for key in ("scriptTest", "auxiliaryHandle"):
            audit.integer(item, key)
        samples[phase] = item["fields"]
    if any(r["fields"].get("phase") not in ("before", "afterScript", "after") for r in operands):
        audit.issue("unknown_predicate_operand_phase", row["line"])
    e = event["fields"]
    normal = (coverage and values["coverageGeneration"] not in (None, 0) and
              all(bits[k] == 1 for k in ("coverageStable", "originalReturned", "resultAvailable")) and
              bits["countOverflow"] == bits["nestedAmbiguous"] == 0 and values["faultMask"] == values["unwindMask"] == 0 and
              e.get("originalReturned") == "1" and e.get("unwound") == "0" and e.get("roleAvailable") == "1" and
              e.get("callerInImage") == "1" and audit.integer(event, "callerRva", base=16) == 0x3BFD6F)
    branch = 0
    if normal and len(audit.issues) == start and values["scriptCalls"] == values["scriptReturned"] == 1:
        script, parent = bool(values["scriptResult"]), bool(values["parentResult"])
        if values["auxiliaryCalls"] == values["auxiliaryReturned"] == 0:
            if not script and not parent:
                branch = 1
            elif script:
                branch = 5 if parent else 2
        elif values["auxiliaryCalls"] == values["auxiliaryReturned"] == 1 and argument and script:
            auxiliary = bool(values["auxiliaryResult"])
            if auxiliary and not parent:
                branch = 3
            elif not auxiliary and parent:
                branch = 4
    if not normal:
        audit.issue("predicate_scope_incomplete", row["line"], "removal")
    if not branch:
        audit.issue("predicate_branch_unknown", row["line"], "removal")
    if values["branch"] != branch:
        audit.issue("predicate_branch_mismatch", row["line"], {"recorded": values["branch"], "derived": branch})
        branch = 0
    names = {0: "unknown", 1: "post-service-script-predicate-blocked", 2: "both-pointer-tests-blocked",
             3: "auxiliary-predicate-blocked", 4: "auxiliary-predicate-permitted", 5: "post-service-resolution-null-permitted"}
    return {"sequence": audit.integer(row, "seq"), "fields": row["fields"], "operands": samples,
            "branch": names[branch], "branchSupported": bool(branch), "scopeComplete": normal and bool(branch),
            "classificationScope": "completed predicate only; never an enclosing parent of subsequent removal/disposal"}


def audit_predicates(rows, audit):
    families = {}
    any_new = any(r["kind"] in ("factory", "predicate", "operands") or
                  "factoryVerifiedMask" in r["fields"] or "predicateStarted" in r["fields"] for r in rows)
    for family, factory in (("spawntrace", True), ("lifecycletrace", False)):
        selected = [r for r in rows if r["family"] == family]
        kind = "factory" if factory else "predicate"
        present = any(r["kind"] == kind or ("factoryVerifiedMask" if factory else "predicateStarted") in r["fields"] for r in selected)
        result = {"schemaRecorded": present, "complete": False, "events": [],
                  "limits": [] if present else ["new predicate coverage not recorded; historical envelope only"]}
        families[kind] = result
        if not present:
            if any_new:
                audit.issue("predicate_coverage_unavailable", detail=kind + " schema not recorded")
            continue
        initial_issues = len(audit.issues)
        headers = [r for r in selected if r["kind"] == "event" and (factory or r["fields"].get("kindId") == "5")]
        supplements = [r for r in selected if r["kind"] == kind]
        operands = [r for r in selected if r["kind"] == "operands"]
        event_groups = defaultdict(list)
        supplement_groups, operand_groups, record_groups = defaultdict(list), defaultdict(list), defaultdict(list)
        coverage_before, previous_coverage = {}, None
        for row in selected:
            if row["kind"] == "record":
                record_groups[row["fields"].get("seq")].append(row)
            if row["kind"] in ("ready", "unavailable", "summary") and ("factoryVerifiedMask" if factory else "verifiedMask") in row["fields"]:
                previous_coverage = row
            elif row["kind"] == kind:
                coverage_before[row["line"]] = previous_coverage
        for event in headers:
            event_groups[event["fields"].get("seq")].append(event)
        for row in supplements:
            supplement_groups[row["fields"].get("seq")].append(row)
            if len(event_groups[row["fields"].get("seq")]) != 1:
                audit.issue("predicate_without_unique_parent", row["line"])
        for operand in operands:
            operand_groups[operand["fields"].get("seq")].append(operand)
            if len(event_groups[operand["fields"].get("seq")]) != 1:
                audit.issue("predicate_operand_without_parent", operand["line"])
        for event in headers:
            seq = event["fields"].get("seq")
            if len(event_groups[seq]) != 1:
                continue
            found = supplement_groups[seq]
            if len(found) != 1:
                audit.issue("predicate_supplement_missing" if not found else "predicate_supplement_duplicated", event["line"], seq)
                continue
            if found[0]["line"] < event["line"]:
                audit.issue("predicate_supplement_before_parent", found[0]["line"], seq)
                continue
            detail = (audit_factory(found[0], event, coverage_before, record_groups[seq], audit) if factory else
                      audit_removal_predicate(found[0], operand_groups[seq], event, coverage_before, audit))
            result["events"].append(detail)
        summaries = [r for r in selected if r["kind"] == "summary"]
        if not summaries:
            audit.issue("predicate_summary_missing", detail=kind)
        previous = {}
        header_lines = [event["line"] for event in headers]
        for summary in summaries:
            keys = ("factoryForeignScopes", "factoryUnwoundScopes", "factoryAmbiguousScopes") if factory else (
                "predicateStarted", "predicatePublished", "predicateDropped", "predicateForeign", "predicateUnmatched",
                "predicateUnwound", "predicateDepthOverflow", "predicateCountOverflow")
            values = {key: audit.integer(summary, key) for key in keys}
            if factory and audit.integer(summary, "factoryRequestedMask") != 3:
                audit.issue("predicate_coverage_unavailable", summary["line"], "requested factory hooks")
            mask_keys = ("factoryVerifiedMask", "factoryInstalledMask", "factoryFailedMask") if factory else ("verifiedMask", "installedMask", "failedMask")
            masks = [audit.integer(summary, key) for key in mask_keys]
            required = 3 if factory else 224
            if any(v is None for v in masks) or (masks[0] & masks[1] & required) != required or masks[2] & required:
                audit.issue("predicate_coverage_unavailable", summary["line"], kind)
            if all(v is not None for v in masks) and (
                    (masks[0] | masks[1] | masks[2]) & ~(3 if factory else 255) or
                    masks[1] & ~masks[0] or masks[1] & masks[2]):
                audit.issue("invalid_predicate_masks", summary["line"])
            for key, value in values.items():
                if value is None:
                    continue
                if key in previous and value < previous[key]:
                    audit.issue("counter_regressed", summary["line"], key)
                previous[key] = value
                if key not in ("predicateStarted", "predicatePublished") and value:
                    audit.issue("predicate_counter_loss", summary["line"], {key: value})
            if not factory and None not in (values["predicateStarted"], values["predicatePublished"]):
                before = bisect_left(header_lines, summary["line"])
                if not before <= values["predicatePublished"] <= values["predicateStarted"]:
                    audit.issue("predicate_counter_inconsistent", summary["line"])
        if summaries and not factory:
            final = summaries[-1]
            published = audit.integer(final, "predicatePublished")
            started = audit.integer(final, "predicateStarted")
            if published != started or published is None or published > len(headers):
                audit.issue("predicate_counter_loss", final["line"], "predicate parents unpublished or absent")
        # Base-family queue loss/tails remain explicit independently of local decisions.
        family_lines = {r["line"] for r in selected}
        envelope_limits = [i["code"] for i in audit.issues if i.get("line") in family_lines and i["code"] in (
            "nonzero_coverage_counter", "events_after_latest_summary", "published_events_missing_at_eof",
            "started_events_not_fully_published", "drained_event_count_mismatch", "predicate_counter_loss")]
        result["limits"].extend(sorted(set(envelope_limits)))
        result["complete"] = len(audit.issues) == initial_issues and not envelope_limits and all(e["scopeComplete"] for e in result["events"])
        result["summaryFields"] = summaries[-1]["fields"] if summaries else None
    return {"schemaRecorded": any_new, "complete": any_new and all(r["complete"] for r in families.values()), **families}


def hit_values(row, audit, unsigned=(), signed=(), pointers=(), bits=(), widths=None):
    """Decode declared POD widths without silently truncating saved native facts."""
    values = {}
    widths = widths or {}
    for key in (*unsigned, *signed, *pointers):
        width = widths.get(key, 64 if key in pointers else 32)
        raw = row["fields"].get(key)
        pattern = r"[0-9A-Fa-f]+" if key in pointers else (r"-?[0-9]+" if key in signed else r"[0-9]+")
        if raw is not None and not re.fullmatch(pattern, raw):
            audit.issue("native_hit_invalid_number", row["line"], key)
            values[key] = None
            continue
        value = audit.integer(row, key, base=16 if key in pointers else 10,
                              minimum=-(1 << (width - 1)) if key in signed else 0)
        maximum = (1 << (width - (key in signed))) - 1
        if value is not None and value > maximum:
            audit.issue("native_hit_integer_out_of_range", row["line"], key)
            value = None
        values[key] = value
    for key in bits:
        values[key] = audit.bit(row, key)
    return values


def hit_decode(row, audit):
    """Schema 1 mirrors NativeHitTrace::Drain; unknown records cannot certify a scope."""
    u, s, p, b, widths = [], [], [], [], {}
    kind = row["kind"]
    if kind in ("ready", "summary"):
        u = ["verifiedMask", "installedMask", "coverageSerial"]
        b = ["requested"]
        widths["coverageSerial"] = 64
        if kind == "summary":
            counters = "started published drained dropped foreign unwound nested overflow unmatched".split()
            u += counters
            widths.update({k: 64 for k in counters})
    else:
        u = ["seq"]
        widths["seq"] = 64
        if kind == "event":
            u += "parent depth coverageSerial coverageMask lossSerial takeCalls statCalls childCount".split()
            widths.update({k: 64 for k in ("parent", "coverageSerial", "lossSerial")})
            p = ["callerRva", "rawResult"]
            b = "callerAvailable ownerThread returned unwound nested overflow coverageStable contextStable metadataStable lossStable witness".split()
        elif kind == "context":
            u += "phase readMask frame generation epoch transitionSerial loadSerial connectionId hostConnectionId role slot".split()
            widths.update({k: 64 for k in "frame generation epoch transitionSerial loadSerial connectionId hostConnectionId".split()})
            widths.update(role=8, slot=8)
            b = ["available"]
        elif kind == "actor":
            u += "phase subject objectId readMask type team".split()
            widths["type"] = 8
            s = ["hp", "maxHp"]
            p = ["actor", "objentry", "status", "namePrefix"]
            widths["namePrefix"] = 16
        elif kind == "input":
            u += "phase flags attackHandle atkpHandle ownerHandle attackId readMask stat kind".split()
            widths.update(stat=8, kind=8)
            s = ["damage"]
            p = "hit attack owner canonicalPlayer head tracked".split()
            b = ["syncDrop", "manualFilterOn", "manualDrop"]
        elif kind == "child":
            u += "index childSeq takeSeq kind".split()
            widths.update(childSeq=64, takeSeq=64)
            s = "delta stat react result".split()
            p = ["actor", "callerRva"]
            b = ["callerAvailable", "matching", "returned", "unwound"]
        else:
            audit.issue("unknown_native_hit_record", row["line"], kind)
    decoded = hit_values(row, audit, u, s, p, b, widths)
    if audit.integer(row, "schema") != 1:
        audit.issue("unsupported_native_hit_schema", row["line"])
    allowed = set(u + s + p + b + ["schema"] + (["location"] if kind == "context" else []))
    if set(row["fields"]) - allowed:
        audit.issue("unknown_native_hit_field", row["line"], sorted(set(row["fields"]) - allowed))
    if kind == "context":
        raw = row["fields"].get("location", "")
        if re.fullmatch(r"[0-9A-Fa-f]{24}", raw):
            decoded["location"] = [int(raw[i:i + 4], 16) for i in range(0, 24, 4)]
        else:
            audit.issue("invalid_native_hit_location", row["line"])
            decoded["location"] = None
    return {**row, "values": decoded}


def hit_actor_same(a, b):
    keys = "actor objentry status objectId type team maxHp namePrefix".split()
    return (a.get("readMask") == b.get("readMask") == 511 and
            all(a.get(k) is not None and a[k] == b.get(k) for k in keys) and
            all(a.get(k, 0) for k in ("actor", "objentry", "status")))


def hit_event_result(event, supplements, readiness, audit):
    v = event["values"]
    reasons = []
    def need(condition, reason):
        if not condition:
            reasons.append(reason)
    def unique(kind, **keys):
        found = [r for r in supplements if r["kind"] == kind and all(r["values"].get(k) == x for k, x in keys.items())]
        if len(found) != 1:
            audit.issue("native_hit_record_join", event["line"], {"kind": kind, **keys, "count": len(found)})
            return {}
        return found[0]["values"]

    need(readiness is not None and all(readiness["values"].get(k) == val for k, val in (
        ("requested", 1), ("verifiedMask", 7), ("installedMask", 7), ("coverageSerial", v.get("coverageSerial")))), "installed coverage not established before event")
    need(v.get("coverageSerial", 0) not in (None, 0) and v.get("coverageMask") == 7 and v.get("coverageStable") == 1, "partial or changed coverage")
    need(v.get("lossSerial") == 0 and v.get("lossStable") == 1, "loss before or within scope")
    need(v.get("ownerThread") == 1, "foreign or unknown thread")
    need(v.get("callerAvailable") == 1 and v.get("callerRva") == 0x3D613C, "unknown or unsupported direct caller")
    need(v.get("returned") == 1 and v.get("unwound") == 0, "native call did not return normally")
    need(v.get("parent") == 0 and v.get("depth") == 1 and v.get("nested") == 0 and v.get("overflow") == 0, "nested or overflow scope")
    need(v.get("takeCalls") == v.get("statCalls") == 1 and v.get("childCount") == 2, "repeated or missing children")
    need(v.get("contextStable") == v.get("metadataStable") == 1, "unverified context or metadata")
    contexts = [unique("context", phase=i) for i in range(2)]
    inputs = [unique("input", phase=i) for i in range(2)]
    victims = [unique("actor", phase=i, subject=0) for i in range(2)]
    sources = [unique("actor", phase=i, subject=1) for i in range(2)]
    c, ca = contexts
    h, ha = inputs
    a, aa = victims
    src, srca = sources
    context_keys = "frame generation epoch transitionSerial loadSerial connectionId hostConnectionId role slot location".split()
    need(all(x.get("available") == 1 and x.get("readMask") == 127 for x in contexts) and
         all(c.get(k) is not None and c.get(k) == ca.get(k) for k in context_keys), "incomplete or changed checked context")
    need(c.get("role") == 2 and c.get("slot") in (1, 2) and
         all(isinstance(c.get(k), int) and c[k] > 0 for k in ("generation", "epoch", "loadSerial", "connectionId", "hostConnectionId")) and
         c.get("connectionId") != c.get("hostConnectionId"), "not a current distinct client and host membership")
    need(hit_actor_same(a, aa) and a.get("type") == 0 and isinstance(a.get("maxHp"), int) and a["maxHp"] > 0,
         "victim metadata incomplete or changed")
    need(hit_actor_same(src, srca) and src.get("type") == 4 and
         src.get("objectId", 0) not in (None, 0) and src.get("namePrefix") != 0x5F46 and
         all(isinstance(src.get(k), int) and src[k] > 0 for k in ("hp", "maxHp")),
         "source is not a checked living ordinary enemy")
    need(all(x.get("readMask") == 255 for x in inputs), "incomplete native hit reads")
    need(all(x.get(k) == a.get("actor") and x.get(k) not in (None, 0) for x in inputs for k in ("canonicalPlayer", "head", "tracked")), "victim is not the canonical local Sora")
    need(all(isinstance(h.get(k), int) and h[k] > 0 for k in ("hit", "attack", "owner", "attackHandle", "atkpHandle", "ownerHandle")) and
         h.get("owner") == src.get("actor") and h.get("owner") != a.get("actor"), "direct resolved attack owner missing or mismatched")
    hit_keys = "hit attack owner attackHandle atkpHandle ownerHandle attackId kind stat".split()
    need(all(h.get(k) is not None and h.get(k) == ha.get(k) for k in hit_keys), "hit identity changed")
    need(h.get("stat") == 0 and isinstance(h.get("damage"), int) and h["damage"] > 0 and h.get("kind") not in (None, 5, 6), "not positive nonhealing HP damage")
    need(isinstance(h.get("flags"), int) and h["flags"] & 2 == 0 and
         isinstance(ha.get("flags"), int) and ha["flags"] & 2 != 0, "already applied or no final native applied flag")
    need(all(x.get(k) == 0 for x in inputs for k in ("syncDrop", "manualFilterOn", "manualDrop")), "sync or manual filtering active")
    child_rows = [r for r in supplements if r["kind"] == "child"]
    children = [r["values"] for r in child_rows]
    takes, stats = ([ch for ch in children if ch.get("kind") == k] for k in (1, 2))
    need(len(children) == 2 and len(takes) == len(stats) == 1, "no unique Take and Stat chain")
    for child in children:
        index = child.get("index")
        before = unique("actor", phase=0, subject=index + 2) if isinstance(index, int) and index < 8 else {}
        after = unique("actor", phase=1, subject=index + 2) if isinstance(index, int) and index < 8 else {}
        need(child.get("matching") == child.get("returned") == 1 and child.get("unwound") == 0 and
             child.get("actor") == a.get("actor") and child.get("stat") == 0 and
             isinstance(child.get("delta"), int) and child["delta"] < 0,
             "unmatched, non-HP or incomplete child")
        need(hit_actor_same(a, before) and hit_actor_same(a, after) and before.get("hp") == a.get("hp") and
             after.get("hp") == aa.get("hp"), "child actor or checked HP join mismatch")
    if len(takes) == len(stats) == 1:
        take, stat = takes[0], stats[0]
        need(take.get("takeSeq") == 0 and stat.get("takeSeq") == take.get("childSeq") and
             isinstance(take.get("childSeq"), int) and isinstance(stat.get("childSeq"), int) and
             v.get("seq", 0) is not None and v["seq"] < take["childSeq"] < stat["childSeq"], "invalid nested Take/Stat ancestry")
        need(take.get("delta") == stat.get("delta"), "adjusted Take/Stat deltas differ")
        need(stat.get("result") == aa.get("hp"), "Stat return differs from checked HP")
        hp, after_hp, delta = a.get("hp"), aa.get("hp"), stat.get("delta")
        need(all(isinstance(x, int) for x in (hp, after_hp, delta)) and
             0 <= after_hp < hp and after_hp == max(0, hp + delta), "actual adjusted delta and clamp do not match checked HP")
    need(v.get("witness") == 1, "producer did not report ordinary witness")
    if reasons:
        audit.issue("native_hit_scope_unverified", event["line"], reasons)
    return {"sequence": v.get("seq"), "line": event["line"], "reportedWitness": v.get("witness"),
            "localChecksSatisfied": not reasons, "ordinaryIncomingClientLocalWitness": False,
            "limits": reasons, "event": v, "contexts": contexts, "inputs": inputs,
            "victims": victims, "sources": sources, "children": children,
            "records": [{"kind": r["kind"], "line": r["line"], "fields": r["fields"]} for r in supplements]}


def audit_native_hits(rows, text, outer_audit):
    selected = [r for r in rows if r["family"] == "hittrace"]
    present = "[hittrace" in text
    result = {"schemaRecorded": present, "complete": False, "status": "unavailable",
              "eventCount": 0, "witnessCount": 0, "events": [], "issues": [],
              "scope": "ordinary incoming client-local native damage from local enemy AI; not full rejection matrix or step 2",
              "pendingTail": "unknown; periodic logs are not a shutdown flush"}
    if not present:
        return result
    audit = Audit()
    trace_lines = {n for n, line in enumerate(text.splitlines(), 1) if "[hittrace" in line}
    parse_issues = [i for i in outer_audit.issues if i.get("line") in trace_lines]
    decoded = [hit_decode(row, audit) for row in selected]
    events = [r for r in decoded if r["kind"] == "event"]
    result["eventCount"] = len(events)
    groups = defaultdict(list)
    headers = defaultdict(list)
    prior, ready_for, summaries, identities = None, {}, [], []
    previous_counters = {}
    for row in decoded:
        kind, v = row["kind"], row["values"]
        if kind in ("ready", "summary"):
            if not all(v.get(k) == x for k, x in (("requested", 1), ("verifiedMask", 7), ("installedMask", 7))) or v.get("coverageSerial") in (0, None):
                audit.issue("native_hit_coverage_unavailable", row["line"])
            if kind == "ready":
                prior = row
            else:
                summaries.append(row)
                if prior is None or v.get("coverageSerial") != prior["values"].get("coverageSerial"):
                    audit.issue("native_hit_coverage_unavailable", row["line"], "summary has no matching ready serial")
                for key in "started published drained dropped foreign unwound nested overflow unmatched".split():
                    value = v.get(key)
                    if value is None:
                        continue
                    if value < previous_counters.get(key, 0):
                        audit.issue("counter_regressed", row["line"], key)
                    previous_counters[key] = value
                    if key not in ("started", "published", "drained") and value:
                        audit.issue("native_hit_counter_loss", row["line"], {key: value})
                before = sum(e["line"] < row["line"] for e in events)
                if v.get("drained") != before or None in (v.get("started"), v.get("published"), v.get("drained")) or not v["drained"] <= v["published"] <= v["started"]:
                    audit.issue("native_hit_summary_count_mismatch", row["line"])
        elif kind == "event":
            headers[v.get("seq")].append(row)
            ready_for[row["line"]] = prior
            identities.append(v.get("seq"))
        else:
            groups[v.get("seq")].append(row)
            if kind == "child":
                identities.append(v.get("childSeq"))
    for seq, records in groups.items():
        if len(headers[seq]) != 1:
            audit.issue("native_hit_orphan_or_ambiguous_records", records[0]["line"], seq)
    for event in events:
        v = event["values"]
        seq, count = v.get("seq"), v.get("childCount")
        supplements = groups[seq]
        if seq in (None, 0) or len(headers[seq]) != 1:
            audit.issue("native_hit_duplicate_or_invalid_sequence", event["line"])
        if any(r["line"] < event["line"] for r in supplements):
            audit.issue("native_hit_record_before_parent", event["line"])
        if count is None or count > 8:
            audit.issue("native_hit_invalid_child_count", event["line"])
            count = 0
        expected = [("context", i, None) for i in range(2)] + [("input", i, None) for i in range(2)]
        expected += [("actor", i, j) for i in range(2) for j in range(count + 2)]
        actual = [(r["kind"], r["values"].get("phase"), r["values"].get("subject")) for r in supplements if r["kind"] != "child"]
        if Counter(actual) != Counter(expected):
            audit.issue("native_hit_incomplete_records", event["line"])
        child_indices = [r["values"].get("index") for r in supplements if r["kind"] == "child"]
        if Counter(child_indices) != Counter(range(count)):
            audit.issue("native_hit_child_index_mismatch", event["line"])
        result["events"].append(hit_event_result(event, supplements, ready_for[event["line"]], audit))
    valid_ids = [i for i in identities if isinstance(i, int) and i > 0]
    if len(valid_ids) != len(identities) or len(set(valid_ids)) != len(valid_ids):
        audit.issue("native_hit_duplicate_or_invalid_sequence")
    ordered = sorted(set(valid_ids))
    if ordered and (ordered[0] != 1 or any(b != a + 1 for a, b in zip(ordered, ordered[1:]))):
        audit.issue("native_hit_sequence_gap")
    if not summaries:
        audit.issue("native_hit_summary_missing")
    else:
        last = summaries[-1]
        v = last["values"]
        result["summary"] = v
        if not (v.get("started") == v.get("published") == v.get("drained") == len(events)) or any(
                r["line"] > last["line"] for r in decoded):
            audit.issue("native_hit_pending_tail", last["line"])
        else:
            result["pendingTail"] = "none recorded at latest summary cutoff; later activity unknown"
    result["issues"] = parse_issues + audit.issues
    result["complete"] = not result["issues"]
    result["status"] = "complete recorded scope" if result["complete"] else "incomplete provenance"
    for event in result["events"]:
        event["ordinaryIncomingClientLocalWitness"] = result["complete"] and event["localChecksSatisfied"]
    result["witnessCount"] = sum(e["ordinaryIncomingClientLocalWitness"] for e in result["events"])
    outer_audit.issues.extend(audit.issues)
    return result


POLICY_LIMITS = [
    "A checked noncanonical type0 actor proves player exclusion, not authenticated remote connection membership.",
    "Driver and positive native-AI classifications are producer-reported; the audit cannot reconstruct their membership stamps.",
    "RevalidationPassed and Zeroed are recorded execution outcomes. No third roster/revalidation snapshot exists; only authority, enclosing pre-capture and raw post-capture are compared independently.",
    "A queued claim is not delivery, host application or HP acceptance. A policy decision alone is not a native veto.",
    "Evidence is bounded to the recorded Apply scope and summary cutoff, not native lifetime/incarnation, all-world compatibility, mirrored hitboxes or full D4 acceptance.",
]


def policy_expected(v):
    """Frozen DamagePolicy.hpp numeric enums; independently apply the pure matrix."""
    if v["ownerThread"] == 0: return (0, 1, 0)
    if v["contextAvailable"] == 0: return (0, 2, 0)
    if v["role"] == 0: return (0, 0, 0)
    if v["role"] not in (1, 2): return (0, 2, 0)
    if v["readMask"] & 7 != 7: return (0, 3, 0)
    if v["flags"] & 2: return (0, 4, 0)
    if v["stat"] != 0: return (0, 5, 0)
    if v["amount"] == 0: return (0, 6, 0)
    if v["victimClass"] in (2, 3): return (1, 7, 1)
    if v["sourceClass"] in (2, 3): return (1, 8, 1)
    if v["sourceClass"] not in (1, 4, 5, 6): return (1, 9, 1)
    if v["victimClass"] == 1: return (0, 10, 1)
    if v["victimClass"] == 5:
        if v["role"] == 1 and v["sourceClass"] == 1: return (0, 11, 1)
        if v["role"] == 1 and v["sourceClass"] == 4: return (0, 12, 1)
        if (v["role"] == 2 and v["sourceClass"] == 1 and v["amount"] > 0 and
                v["readMask"] & 8 and v["kind"] not in (5, 6)): return (2, 13, 1)
    return (1, 14, 1)


def policy_decode(row, audit):
    if row["kind"] != "event":
        if row["kind"] not in ("context", "input", "actor"):
            audit.issue("unknown_damage_policy_record", row["line"], row["kind"])
        return hit_decode(row, audit)
    unsigned = "seq role victimClass sourceClass flags readMask stat kind action reason roster0 roster1 roster2 zeroResult".split()
    bits = "recorded ownerThread contextAvailable supported revalidationAttempted revalidationPassed zeroAttempted claimAttempted claimQueued".split()
    values = hit_values(row, audit, unsigned, ["amount"], bits=bits,
                        widths={**{k: 64 for k in ("seq", "roster0", "roster1", "roster2")},
                                **{k: 8 for k in ("role", "stat", "kind", "victimClass", "sourceClass", "action", "reason", "zeroResult")}})
    if audit.integer(row, "schema") != 1:
        audit.issue("unsupported_damage_policy_schema", row["line"])
    extra = set(row["fields"]) - set(unsigned + bits + ["amount", "schema"])
    if extra:
        audit.issue("unknown_damage_policy_field", row["line"], sorted(extra))
    for key, maximum in (("victimClass", 6), ("sourceClass", 6), ("action", 2), ("reason", 14), ("zeroResult", 4), ("readMask", 15)):
        if values.get(key) is not None and values[key] > maximum:
            audit.issue("invalid_damage_policy_enum", row["line"], key)
    return {**row, "values": values}


def policy_event_result(event, records, hit, audit):
    v = event["values"]
    reasons = []
    def need(condition, reason):
        if not condition: reasons.append(reason)
    def unique(kind, **keys):
        found = [r for r in records if r["kind"] == kind and all(r["values"].get(k) == x for k, x in keys.items())]
        if len(found) != 1:
            audit.issue("damage_policy_record_join", event["line"], {"kind": kind, **keys, "count": len(found)})
            return {}
        return found[0]["values"]
    expected = policy_expected(v) if all(x is not None for x in v.values()) else None
    decision_valid = expected is not None and expected == tuple(v.get(k) for k in ("action", "reason", "supported"))
    if not decision_valid:
        audit.issue("damage_policy_decision_mismatch", event["line"], {"expected": expected})
    need(decision_valid, "recorded decision disagrees with independent matrix")
    c = unique("context", phase=0)
    h = unique("input", phase=0)
    a = unique("actor", phase=0, subject=0)
    s = unique("actor", phase=0, subject=1)
    expected_records = Counter([("context", 0, None), ("input", 0, None), ("actor", 0, 0), ("actor", 0, 1)])
    actual_records = Counter((r["kind"], r["values"].get("phase"), r["values"].get("subject")) for r in records)
    if actual_records != expected_records:
        audit.issue("damage_policy_incomplete_records", event["line"])
    need(hit is not None, "no unique enclosing Apply event")
    raw = hit or {}
    ev = raw.get("event", {})
    contexts, inputs = raw.get("contexts", [{}, {}]), raw.get("inputs", [{}, {}])
    victims, sources = raw.get("victims", [{}, {}]), raw.get("sources", [{}, {}])
    need(v.get("recorded") == 1 and v.get("supported") == 1 and v.get("action") == 1,
         "not a recorded supported zero-veto decision")
    need(all(ev.get(k) == val for k, val in (("ownerThread", 1), ("returned", 1), ("unwound", 0),
         ("parent", 0), ("depth", 1), ("nested", 0), ("overflow", 0), ("coverageMask", 7),
         ("coverageStable", 1), ("lossSerial", 0), ("lossStable", 1), ("contextStable", 1), ("metadataStable", 1))),
         "incomplete Apply coverage, completion or stable scope")
    context_keys = "frame generation epoch transitionSerial loadSerial connectionId hostConnectionId role slot location".split()
    need(all(x.get("readMask") == 127 and x.get("available") == 1 and
             all(c.get(k) is not None and x.get(k) == c.get(k) for k in context_keys) for x in [c, *contexts]),
         "authority and enclosing contexts are incomplete or differ")
    role, slot = c.get("role"), c.get("slot")
    roster = [v.get(f"roster{i}") for i in range(3)]
    positive_ids = [x for x in roster if isinstance(x, int) and x > 0]
    need(role in (1, 2) and ((role == 1 and slot == 0) or (role == 2 and slot in (1, 2))) and
         all(isinstance(c.get(k), int) and c[k] > 0 for k in ("generation", "epoch", "loadSerial", "connectionId", "hostConnectionId")) and
         isinstance(slot, int) and 0 <= slot < 3 and roster[slot] == c.get("connectionId") and
         roster[0] == c.get("hostConnectionId") and len(positive_ids) == len(set(positive_ids)) and
         None not in roster and v.get("role") == role and v.get("ownerThread") == v.get("contextAvailable") == 1,
         "invalid active role, current context or full-width roster")
    need(all(hit_actor_same(a, x) and x.get("hp") == a.get("hp") for x in victims) and
         isinstance(a.get("hp"), int) and isinstance(a.get("maxHp"), int) and 0 < a["hp"] <= a["maxHp"],
         "victim metadata or checked HP differs across captures")
    need(all(hit_actor_same(s, x) and x.get("hp") == s.get("hp") for x in sources),
         "source metadata or HP differs across captures")
    need(all(x.get("readMask") == 255 for x in [h, *inputs]), "incomplete raw hit captures")
    hit_keys = "hit attack owner attackHandle atkpHandle ownerHandle attackId stat kind canonicalPlayer head tracked".split()
    need(all(all(h.get(k) is not None and x.get(k) == h.get(k) for k in hit_keys) for x in inputs),
         "raw hit identities or canonical roots differ")
    canonical = h.get("canonicalPlayer")
    need(isinstance(canonical, int) and canonical > 0 and h.get("head") == h.get("tracked") == canonical and
         all(isinstance(h.get(k), int) and h[k] > 0 for k in ("hit", "attack", "owner", "attackHandle", "atkpHandle", "ownerHandle")) and
         h.get("owner") == s.get("actor"), "canonical roots or resolved source owner unavailable")
    def class_consistent(kind, actor):
        # These checks can refute an impossible class; they cannot prove a
        # driver exclusion or positive AI stamp that the envelope does not log.
        if kind == 0: return True
        if actor.get("readMask") != 511: return False
        local = actor.get("actor") == canonical
        actor_type = actor.get("type")
        enemy = (actor_type in (3, 4) and actor.get("namePrefix") != 0x5F46 and
                 all(isinstance(actor.get(k), int) and actor[k] > 0 for k in ("objectId", "hp", "maxHp")))
        return {1: local and actor_type == 0,
                2: not local and actor_type != 0,
                3: not local and actor_type == 0,
                4: not local and actor_type == 1,
                5: not local and enemy,
                6: not local and actor_type not in (0, 1) and not enemy}.get(kind, False)
    need(class_consistent(v.get("victimClass"), a) and class_consistent(v.get("sourceClass"), s),
         "reported actor classification contradicts checked raw metadata")
    need(v.get("flags") == h.get("flags") == inputs[0].get("flags") and
         v.get("amount") == h.get("damage") == inputs[0].get("damage") and
         v.get("stat") == h.get("stat") == 0 and v.get("kind") == h.get("kind") and v.get("readMask") == 15,
         "typed decision input does not match both raw pre-captures")
    need(isinstance(h.get("flags"), int) and h["flags"] & 2 == 0 and
         isinstance(h.get("damage"), int) and h["damage"] != 0 and inputs[1].get("damage") == 0 and
         inputs[1].get("flags") == (h.get("flags", 0) | 2), "zeroed record was not normally consumed with bit2")
    remote_victim = v.get("victimClass") == 3 and a.get("type") == 0 and a.get("actor") not in (None, 0, canonical)
    remote_source = v.get("sourceClass") == 3 and s.get("type") == 0 and s.get("actor") not in (None, 0, canonical)
    need((v.get("reason") == 7 and remote_victim) or (v.get("reason") == 8 and remote_source),
         "no independently checked noncanonical type0 exclusion; driver or AI classification is unverified")
    need(all(v.get(k) == 1 for k in ("revalidationAttempted", "revalidationPassed", "zeroAttempted")) and v.get("zeroResult") == 0,
         "actual revalidation and Zeroed outcome not recorded")
    need(v.get("claimAttempted") == v.get("claimQueued") == 0, "claim attempt or queue receipt is not veto evidence")
    need(all(x.get(k) == 0 for x in [h, *inputs] for k in ("syncDrop", "manualFilterOn", "manualDrop")),
         "manual or sync filtering can explain the zero")
    children = raw.get("children", [])
    need(ev.get("takeCalls") == sum(ch.get("kind") == 1 for ch in children) and
         ev.get("statCalls") == sum(ch.get("kind") == 2 for ch in children) and ev.get("childCount") == len(children),
         "child counts differ from recorded calls")
    for child in children:
        need(child.get("kind") in (1, 2) and child.get("matching") == child.get("returned") == 1 and
             child.get("unwound") == 0 and child.get("actor") == a.get("actor") and
             isinstance(child.get("childSeq"), int) and isinstance(ev.get("seq"), int) and
             child["childSeq"] > ev["seq"] and (child.get("stat") != 0 or child.get("delta") == 0),
             "unmatched/incomplete child, invalid ancestry or nonzero HP child delta")
        index = child.get("index")
        actors = [r for r in raw.get("records", []) if r["kind"] == "actor" and
                  r["fields"].get("subject") == str(index + 2 if isinstance(index, int) else -1)]
        checked = [hit_decode({**r, "family": "hittrace"}, audit)["values"] for r in actors]
        need(len(checked) == 2 and all(hit_actor_same(a, x) and x.get("hp") == a.get("hp") for x in checked),
             "child metadata or HP not checked unchanged")
        if child.get("kind") == 2:
            takes = [ch for ch in children if ch.get("kind") == 1 and ch.get("childSeq") == child.get("takeSeq")]
            need(len(takes) == 1 and isinstance(child.get("childSeq"), int) and
                 takes[0].get("childSeq", 0) < child["childSeq"] and
                 (child.get("stat") != 0 or child.get("result") in (0, a.get("hp"))),
                 "Stat ancestry or zero-delta no-write/checked HP result differs")
        else:
            need(child.get("takeSeq") == 0, "Take has invalid ancestry")
    return {"sequence": v.get("seq"), "line": event["line"], "decisionMatchesMatrix": decision_valid,
            "recordedDecision": v, "knownOwnerZeroVetoEvidence": False, "localChecksSatisfied": not reasons,
            "limits": reasons, "authorityContext": c, "authorityInput": h, "authorityVictim": a, "authoritySource": s,
            "records": [{"kind": r["kind"], "line": r["line"], "fields": r["fields"]} for r in records]}


def audit_damage_policy(rows, text, native_hits, outer_audit):
    result = {"schemaRecorded": "[damagepolicy" in text, "complete": False, "status": "unavailable",
              "eventCount": 0, "decisionCount": 0, "zeroVetoCount": 0, "events": [], "issues": [],
              "scope": "recorded decisions and bounded checked noncanonical-type0 zero-veto evidence; not authenticated peer ownership",
              "limits": POLICY_LIMITS}
    if not result["schemaRecorded"]: return result
    audit = Audit()
    policy_lines = {n for n, line in enumerate(text.splitlines(), 1) if "[damagepolicy" in line}
    issues = [i for i in outer_audit.issues if i.get("line") in policy_lines]
    # Expected non-witness scopes do not invalidate this separate envelope.
    # Every structural, loss, width, join and global coverage failure still does.
    issues += [i for i in native_hits["issues"] if i["code"] != "native_hit_scope_unverified"]
    if not native_hits["schemaRecorded"]:
        audit.issue("damage_policy_missing_hittrace")
    selected = [policy_decode(r, audit) for r in rows if r["family"] == "damagepolicy"]
    headers, groups = defaultdict(list), defaultdict(list)
    for row in selected:
        (headers if row["kind"] == "event" else groups)[row["values"].get("seq")].append(row)
    hits = defaultdict(list)
    for hit in native_hits["events"]: hits[hit["sequence"]].append(hit)
    ready_rows = [hit_decode(r, audit) for r in rows if r["family"] == "hittrace" and r["kind"] == "ready"]
    for hit in native_hits["events"]:
        ev = hit["event"]
        prior = [r for r in ready_rows if r["line"] < hit["line"]]
        ready = prior[-1]["values"] if prior else {}
        if not all(ev.get(k) == value for k, value in (("ownerThread", 1), ("returned", 1),
                ("unwound", 0), ("parent", 0), ("depth", 1), ("nested", 0), ("overflow", 0),
                ("coverageMask", 7), ("coverageStable", 1), ("lossSerial", 0), ("lossStable", 1))) or not (
                ev.get("coverageSerial") not in (None, 0) and ev.get("coverageSerial") == ready.get("coverageSerial") and
                all(ready.get(k) == value for k, value in (("requested", 1), ("verifiedMask", 7), ("installedMask", 7)))):
            audit.issue("damage_policy_global_scope_incomplete", hit["line"])
    summary_line = max((r["line"] for r in rows if r["family"] == "hittrace" and r["kind"] == "summary"), default=0)
    for seq in groups.keys() | headers.keys():
        if seq in (None, 0) or len(headers[seq]) != 1 or len(hits[seq]) != 1:
            audit.issue("damage_policy_orphan_or_ambiguous_sequence", detail=seq)
        for header in headers[seq]:
            if any(r["line"] <= header["line"] for r in groups[seq]) or header["line"] <= (hits[seq][0]["line"] if len(hits[seq]) == 1 else 0):
                audit.issue("damage_policy_record_order", header["line"])
            result["events"].append(policy_event_result(header, groups[seq], hits[seq][0] if len(hits[seq]) == 1 else None, audit))
    if any(r["line"] > summary_line for r in selected): audit.issue("damage_policy_pending_tail")
    result["events"].sort(key=lambda e: e["line"])
    result["issues"] = issues + audit.issues
    result["complete"] = not result["issues"] and bool(headers)
    result["status"] = "complete recorded envelope" if result["complete"] else "incomplete provenance"
    for event in result["events"]:
        event["knownOwnerZeroVetoEvidence"] = result["complete"] and event["localChecksSatisfied"]
    result["eventCount"] = len(result["events"])
    result["decisionCount"] = sum(e["decisionMatchesMatrix"] for e in result["events"])
    result["zeroVetoCount"] = sum(e["knownOwnerZeroVetoEvidence"] for e in result["events"])
    outer_audit.issues.extend(audit.issues)
    return result


def audit_text(text, source="<memory>"):
    audit = Audit()
    rows = parse_lines(text, audit)
    spawn = [r for r in rows if r["family"] == "spawntrace"]
    life = [r for r in rows if r["family"] == "lifecycletrace"]
    if not spawn:
        audit.issue("spawn_trace_not_recorded")
    spawn_result = audit_spawn(spawn, audit) if spawn else None
    lifecycle_result = audit_lifecycle(life, audit) if life else None
    if not life:
        audit.issue("lifecycle_trace_not_recorded")
    predicates = audit_predicates(rows, audit)
    native_hits = audit_native_hits(rows, text, audit)
    damage_policy = audit_damage_policy(rows, text, native_hits, audit)
    return {"source": source, "structuralComplete": not any(i["category"] == "structure" for i in audit.issues),
            "provenanceComplete": not audit.issues,
            "issues": audit.issues, "spawnTrace": spawn_result,
            "lifecycleTrace": lifecycle_result, "predicateTrace": predicates,
            "nativeHitTrace": native_hits, "damagePolicyTrace": damage_policy, "limits": LIMITS}


def audit_paths(paths):
    reports = []
    for name in paths:
        path = Path(name)
        raw = path.read_bytes()
        report = audit_text(raw.decode("utf-8", errors="replace"), str(path.resolve()))
        report["sha256"] = hashlib.sha256(raw).hexdigest()
        if "\ufffd" in raw.decode("utf-8", errors="replace"):
            report["issues"].append({"code": "invalid_log_encoding", "category": "structure"})
            report["provenanceComplete"] = False
            report["structuralComplete"] = False
            hit = report["nativeHitTrace"]
            if hit["schemaRecorded"]:
                hit["complete"] = False
                hit["status"] = "incomplete provenance"
                hit["witnessCount"] = 0
                hit["issues"].append({"code": "invalid_log_encoding", "category": "structure"})
                for event in hit["events"]:
                    event["ordinaryIncomingClientLocalWitness"] = False
            policy = report["damagePolicyTrace"]
            if policy["schemaRecorded"]:
                policy["complete"] = False
                policy["status"] = "incomplete provenance"
                policy["zeroVetoCount"] = 0
                policy["issues"].append({"code": "invalid_log_encoding", "category": "structure"})
                for event in policy["events"]:
                    event["knownOwnerZeroVetoEvidence"] = False
        reports.append(report)
    return {"schemaVersion": 1, "scope": "local recorded provenance audit; no game or process access",
            "structuralComplete": bool(reports) and all(r["structuralComplete"] for r in reports),
            "provenanceComplete": bool(reports) and all(r["provenanceComplete"] for r in reports),
            "logs": reports, "limits": LIMITS}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, help="write the full report to this local JSON file")
    args = parser.parse_args(argv)
    try:
        if args.output and args.output.resolve() in {p.resolve() for p in args.logs}:
            parser.error("output must not overwrite an input log")
        report = audit_paths(args.logs)
        encoded = json.dumps(report, indent=2, allow_nan=False) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8")
            print(json.dumps({"output": str(args.output), "logCount": len(report["logs"]),
                              "provenanceComplete": report["provenanceComplete"]}))
        else:
            print(encoded, end="")
        return 0 if report["provenanceComplete"] else 1
    except OSError as error:
        print(json.dumps({"error": str(error), "scope": "local file I/O"}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
