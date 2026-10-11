"""Read-only event-local native/policy -> HP packet -> friend-store replay.

This is a recorded-chain check, never a native-coverage or AC3 certificate.
Exit 0 means all recorded host Hit/claim chains linked; 1 means an unresolved chain;
2 means invalid invocation/files. Unmatched/no-op publications remain explicit.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

import combat_causal_audit as causal
import trace_audit


def records(text):
    """Keep raw native/policy fields and local line numbers; reject duplicates."""
    result = []
    for line, raw in enumerate(text.splitlines(keepends=True), 1):
        match = re.search(r"\[(hittrace|damagepolicy)\] (event|context|actor|input|child) (.*)\r?\n$", raw)
        if not match:
            if re.search(r"\[(hittrace|damagepolicy)\] (event|context|actor|input|child)\b", raw):
                raise ValueError("malformed_or_unterminated_native_record")
            continue
        domain, kind, body = match.groups()
        fields = {}
        for token in body.split():
            key, value = token.split("=", 1)
            if key in fields:
                raise ValueError("duplicate native field")
            fields[key] = value
        if fields.get("schema") != "1":
            raise ValueError("unsupported native schema")
        result.append((domain, kind, fields, line))
    return result


def num(row, key):
    return int(row[key], 16 if key in {"actor", "objentry", "status", "owner", "hit", "attack",
                                     "canonicalPlayer", "head", "tracked", "callerRva", "namePrefix"} else 10)


def require(condition, code):
    if not condition:
        raise ValueError(code)


def native_proof(raw, hit, *, client_claim=False):
    """Replay host direct-Stat or client queued-claim zero-Stat, not a witness bit.

    Unimplemented path families explicitly refuse instead of trusting qualified=1.
    Native roots/authority are copied observations, not incarnation authentication.
    """
    def one(domain, kind, **selectors):
        found = [(r, line) for d, k, r, line in raw if d == domain and k == kind and
                 all(r.get(key) == str(value) for key, value in selectors.items())]
        require(len(found) == 1, "missing_or_duplicate_native_record")
        return found[0]

    candidates = [(r, line) for d, k, r, line in raw if d == "hittrace" and k == "context" and
                  r.get("phase") == "0" and num(r, "frame") == hit["frame"] and
                  num(r, "seq") == hit["event"]]
    proofs = []
    for context, _ in candidates:
        seq = num(context, "seq")
        try:
            event, event_line = one("hittrace", "event", seq=seq)
            group = [(d,k,r,line) for d,k,r,line in raw if r.get("seq") == str(seq)]
            expected = Counter({("hittrace","event"):1,("hittrace","context"):2,("hittrace","input"):2,
                                ("hittrace","actor"):6,("hittrace","child"):1,("damagepolicy","event"):1,
                                ("damagepolicy","context"):1,("damagepolicy","input"):1,("damagepolicy","actor"):2})
            require(Counter((d,k) for d,k,r,line in group) == expected and
                    all(line > event_line for d,k,r,line in group if (d,k) != ("hittrace","event")), "native_record_shape_or_order")
            require(seq > 0 and num(event, "coverageSerial") > 0 and num(event, "lossSerial") == 0, "native_coverage_unavailable")
            require(all(num(event, k) == v for k, v in {
                "parent": 0, "depth": 1, "ownerThread": 1, "returned": 1, "unwound": 0,
                "nested": 0, "overflow": 0, "coverageStable": 1, "coverageMask": 7,
                "contextStable": 1, "metadataStable": 1, "lossStable": 1,
                "callerAvailable": 1, "callerRva": 0x410EFF, "takeCalls": 0,
                "statCalls": 1, "childCount": 1}.items()), "unsupported_or_unsafe_native_path")
            def snapshot(domain, kind, phase, subject=None):
                selectors = dict(seq=seq, phase=phase)
                if subject is not None:
                    selectors["subject"] = subject
                return one(domain, kind, **selectors)[0]
            before = snapshot("hittrace", "actor", 0, 0)
            after = snapshot("hittrace", "actor", 1, 0)
            source = snapshot("hittrace", "actor", 0, 1)
            source_after = snapshot("hittrace", "actor", 1, 1)
            inp = snapshot("hittrace", "input", 0)
            inp_after = snapshot("hittrace", "input", 1)
            policy, policy_line = one("damagepolicy", "event", seq=seq)
            context_keys = "readMask available frame generation epoch transitionSerial loadSerial connectionId hostConnectionId role slot location".split()
            for ctx in (snapshot("hittrace", "context", 1), snapshot("damagepolicy", "context", 0)):
                require(all(ctx[k] == context[k] for k in context_keys), "native_context_mismatch")
            require(num(context, "readMask") == 127 and num(context, "available") == 1 and
                    num(context, "role") == (2 if client_claim else 1) and
                    num(context, "slot") == (1 if client_claim else 0), "non_host_native_context")
            bindings = {"frame": "frame", "generation": "generation", "epoch": "epoch",
                        "transitionSerial": "transition", "loadSerial": "load",
                        "connectionId": "connection", "hostConnectionId": "host"}
            require(all(num(context, a) == hit[b] for a, b in bindings.items()), "native_ledger_scope_mismatch")
            require(context["location"] == "".join(f"{x:04x}" for x in hit["location"]), "native_location_mismatch")
            actor_keys = "actor objentry status objectId readMask type team maxHp namePrefix".split()
            require(all(before[k] == after[k] for k in actor_keys) and
                    all(source[k] == source_after[k] for k in actor_keys + ["hp"]), "native_actor_replaced")
            for actor, subject in ((before, 0), (source, 1)):
                authority = snapshot("damagepolicy", "actor", 0, subject)
                require(all(actor[k] == authority[k] for k in actor_keys + ["hp"]), "policy_actor_mismatch")
                require(num(actor, "readMask") == 511 and all(num(actor, k) for k in ("actor", "objentry", "status")) and
                        0 < num(actor, "hp") <= num(actor, "maxHp"), "unavailable_native_actor")
            require(all(num(before, k) == hit[k] for k in ("actor", "objentry", "status", "objectId", "type", "maxHp")) and
                    num(before, "objectId") == 302 and num(before, "type") == 4 and
                    num(before, "hp") == hit["beforeHp"] and num(after, "hp") == hit["afterHp"], "native_ledger_target_mismatch")
            require(num(source, "type") in (0, 1) and num(source, "team") == 1 and num(source, "namePrefix") == 0x5F50 and
                    num(source, "actor") != num(before, "actor") and num(source, "status") != num(before, "status"), "invalid_native_source")
            companion = num(source, "type") == 1
            require(not client_claim or not companion, "noncanonical_client_source")
            require(all(num(policy, k) == v for k, v in {
                "recorded": 1, "role": 2 if client_claim else 1, "ownerThread": 1, "contextAvailable": 1,
                "victimClass": 5, "sourceClass": 4 if companion else 1, "supported": 1,
                "action": 2 if client_claim else 0, "reason": 13 if client_claim else 12 if companion else 11, "readMask": 15,
                "zeroAttempted": int(client_claim), "claimAttempted": int(client_claim), "claimQueued": int(client_claim),
                "roster0": hit["host"], "roster1": hit["peer"], "roster2": 0}.items()), "native_policy_refused")
            identity_keys = "hit attack owner canonicalPlayer head tracked attackHandle atkpHandle ownerHandle attackId readMask stat kind manualFilterOn manualDrop".split()
            if not client_claim:
                identity_keys += ["damage", "syncDrop"]
            else:
                require(num(inp_after, "damage") == 0 and num(inp, "syncDrop") == num(inp_after, "syncDrop") == 1 and
                        all(num(policy, k) == v for k, v in {"revalidationAttempted": 1, "revalidationPassed": 1, "zeroResult": 2}.items()),
                        "client_zero_or_revalidation_missing")
            require(all(inp[k] == inp_after[k] for k in identity_keys), "native_input_changed")
            authority_input = snapshot("damagepolicy", "input", 0)
            require(all(inp[k] == authority_input[k] for k in identity_keys + ["flags", "damage"]) and
                    (not client_claim or num(authority_input, "syncDrop") == 0), "native_policy_input_mismatch")
            require(num(inp, "readMask") == 255 and all(num(inp, k) for k in ("hit", "attack", "owner", "attackHandle", "ownerHandle")) and
                    num(inp, "owner") == num(source, "actor") and num(inp, "canonicalPlayer") != num(before, "actor") and
                    num(inp, "canonicalPlayer") == num(inp, "head") == num(inp, "tracked") != 0 and
                    (num(inp, "canonicalPlayer") != num(source, "actor")) == companion and
                    num(inp, "damage") > 0 and num(inp, "stat") == 0 and num(inp, "kind") not in (5, 6) and
                    not any(num(inp, k) for k in (("manualFilterOn", "manualDrop") if client_claim else ("syncDrop", "manualFilterOn", "manualDrop"))) and
                    not num(inp, "flags") & 2 and num(inp_after, "flags") == num(inp, "flags") | 2,
                    "invalid_native_input")
            require(all(num(policy, a) == num(inp, b) for a, b in {"amount": "damage", "flags": "flags", "stat": "stat", "kind": "kind"}.items()), "policy_damage_mismatch")
            child, _ = one("hittrace", "child", seq=seq, index=0)
            require(all(num(child, k) == v for k, v in {"kind": 2, "applySeq": seq, "takeSeq": 0, "matching": 0,
                    "returned": 1, "unwound": 0, "callerAvailable": 1, "callerRva": 0x3D37D2,
                    "actor": hit["actor"], "stat": 0, "delta": 0 if client_claim else -num(inp, "damage"), "result": hit["afterHp"]}.items()) and
                    num(child, "childSeq") > seq and hit["afterHp"] == (hit["beforeHp"] if client_claim else max(0, hit["beforeHp"] - num(inp, "damage"))), "native_child_mismatch")
            for phase, actor in ((0, before), (1, after)):
                child_actor = snapshot("hittrace", "actor", phase, 2)
                require(all(child_actor[k] == actor[k] for k in actor_keys + ["hp"]), "native_child_actor_mismatch")
            proofs.append({"applySequence": seq, "nativeLine": event_line, "policyLine": policy_line,
                           "sourceActor": source["actor"], "sourceObjectId": num(source, "objectId"),
                           "sourceKind": "client_canonical_claim" if client_claim else "host_companion" if companion else "host_canonical_player",
                           "attackId": num(inp, "attackId"), "damage": num(inp, "damage")})
        except (ValueError, KeyError) as error:
            proofs.append({"refusal": str(error), "applySequence": seq})
    valid = [p for p in proofs if "refusal" not in p]
    require(len(valid) == 1 and len(proofs) == 1, "missing_or_ambiguous_native_policy_witness:" + str(proofs))
    return valid[0]


def packet_batch(rows, kind, row):
    batch = [r for r in rows if r["kind"] == kind and r["payload"] == row["payload"]]
    decoded = causal.wire(row)
    require(decoded[:3] == (23,row["epoch"],row["hpSequence"]) and
            sorted(r["netId"] for r in batch) == sorted(e[0] for e in decoded[3]), "invalid_packet_batch_membership")
    require(all(causal.scope(r) == causal.scope(row) and r["hpSequence"] == row["hpSequence"] and
                r["enqueued"] == r["qualified"] == 1 and r["reason"] == 0 and
                (r["netId"],r["requestedHp"],r["maxHp"]) in decoded[3] for r in batch), "invalid_packet_batch_readback")
    headers = []
    if kind == 5:
        headers = [r for r in rows if r["kind"] == 13 and r["payload"] == row["payload"]]
        require(len(headers) == 1 and headers[0]["serial"] < min(r["serial"] for r in batch) and
                headers[0]["qpc"] <= min(r["qpc"] for r in batch) and
                causal.scope(headers[0]) == causal.scope(row) and headers[0]["qualified"] == headers[0]["reason"] == 0,
                "missing_or_duplicate_batch_admission")
    return batch + headers


def claim_proof(host, client, raw, applied):
    """Bind actual client Apply/zero/queue to the host authority readback.

    HostApply is a copied native-call receipt, not a new native coverage proof.
    No assumption that local addresses or local clocks agree across machines.
    """
    claims = [r for r in client if r["kind"] == 2 and causal.key(r) == causal.key(applied)]
    require(len(claims) == 1, "missing_or_duplicate_claim")
    claim = claims[0]
    require(len([r for r in host if r["kind"] == 3 and causal.key(r) == causal.key(applied)]) == 1,
            "duplicate_host_claim_application")
    require(causal.wire(claim) == (8, causal.key(applied), 1) and claim["enqueued"] == claim["qualified"] == 1 and
            claim["reason"] == 12 and claim["claimConnection"] == claim["connection"] == claim["peer"] and
            claim["claimSeq"] > 0 and applied["event"] == ((1 << 63) | applied["serial"]) and
            applied["attempted"] == 1 and applied["damage"] > 0 and
            applied["afterHp"] == max(0, applied["beforeHp"] - applied["damage"]), "invalid_claim_authority")
    require(all(claim[k] == applied[k] for k in ("generation", "sessionSalt", "host", "peer", "hostDelivery", "peerDelivery", "epoch")),
            "claim_authority_scope_mismatch")
    hits = [r for r in client if r["kind"] == 1 and r["event"] == claim["event"]]
    require(len(hits) == 1, "missing_or_duplicate_claim_hit")
    hit = hits[0]
    require(causal.target(hit) == causal.target(claim) and causal.scope(hit) == causal.scope(claim) and
            hit["frame"] == claim["frame"] and hit["qualified"] == hit["returned"] == hit["readback"] == 1 and
            hit["reason"] == 0 and hit["beforeHp"] == hit["afterHp"] > 0 and claim["serial"] < hit["serial"] and
            claim["qpc"] <= hit["qpc"], "claim_hit_mismatch")
    native = native_proof(raw, hit, client_claim=True)
    require((native["attackId"], native["damage"]) == (claim["attackId"], claim["damage"]), "claim_native_payload_mismatch")
    return {**native, "clientHitSerial": hit["serial"], "claimSerial": claim["serial"],
            "claimSequence": claim["claimSeq"], "claimPayloadSha256": hashlib.sha256(claim["payload"]).hexdigest()}


def incoming_disposition(raw, hit, native_events):
    """Type a local enemy->Sora event; never silently exclude a netId=0 row."""
    item = {"serial": hit["serial"], "event": hit["event"], "disposition": "unresolved_incoming", "linked": False}
    try:
        matches = [e for e in native_events if e["sequence"] == hit["event"]]
        require(len(matches) == 1, "missing_or_duplicate_incoming_native")
        event = matches[0]
        child_count = event["event"].get("childCount")
        require(child_count in (0,2), "unsupported_incoming_children")
        group = [(d,k,r,line) for d,k,r,line in raw if r.get("seq") == str(hit["event"])]
        expected = Counter({("hittrace","event"):1,("hittrace","context"):2,("hittrace","input"):2,
                            ("hittrace","actor"):4+2*child_count,("damagepolicy","event"):1,
                            ("damagepolicy","context"):1,("damagepolicy","input"):1,("damagepolicy","actor"):2})
        if child_count: expected[("hittrace","child")] = child_count
        require(Counter((d,k) for d,k,r,line in group) == expected and
                all(line > event["line"] for d,k,r,line in group if (d,k) != ("hittrace","event")), "incoming_record_shape_or_order")
        noop = hit["beforeHp"] == hit["afterHp"]
        if noop:
            header = event["event"]
            require(all(header.get(k) == v for k,v in {"parent":0,"depth":1,"ownerThread":1,"returned":1,"unwound":0,
                    "nested":0,"overflow":0,"coverageMask":7,"lossSerial":0,"coverageStable":1,"contextStable":1,
                    "metadataStable":1,"lossStable":1,"callerAvailable":1,"callerRva":0x3D613C,
                    "takeCalls":0,"statCalls":0,"childCount":0,"witness":0}.items()) and header.get("coverageSerial",0)>0,
                    "incoming_noop_path_refused")
            a,b = event["victims"];src,src_after = event["sources"];inp,inp_after = event["inputs"];c,ca = event["contexts"]
            require(c == {**ca,"phase":c.get("phase")} and c.get("available") == 1 and c.get("readMask") == 127 and
                    c.get("role") == 2 and c.get("slot") == 1 and
                    trace_audit.hit_actor_same(a,b) and a["hp"] == b["hp"] > 0 and a["type"] == 0 and
                    trace_audit.hit_actor_same(src,src_after) and src["hp"] == src_after["hp"] > 0 and src["type"] == 4 and
                    src["objectId"] == 302 and src["namePrefix"] == 0x5F4D and a["namePrefix"] == 0x5F50 and
                    inp == {**inp_after,"phase":inp.get("phase")} and inp["readMask"] == 255 and inp["flags"] & 2 and
                    inp["canonicalPlayer"] == inp["head"] == inp["tracked"] == a["actor"] and
                    inp["owner"] == src["actor"] != a["actor"] and all(inp[k]>0 for k in ("hit","attack","owner","attackHandle","atkpHandle","ownerHandle")) and
                    inp["damage"] > 0 and inp["stat"] == 0 and inp["kind"] not in (5,6) and
                    all(inp[k] == 0 for k in ("syncDrop","manualFilterOn","manualDrop")) and not event["children"], "incoming_noop_identity_refused")
        else:
            require(event["localChecksSatisfied"], "incoming_native_path_refused")
        require(hit["netId"] == 0 and all(hit[k] == 0 for k in causal.ROOTS) and
                hit["qualified"] == 0 and hit["reason"] == 4 and hit["returned"] == hit["readback"] == 1 and
                not hit["causes"] and not hit["payload"] and hit["damage"] == hit["claimSeq"] == 0,
                "incoming_ledger_domain_mismatch")
        c = event["contexts"][0]
        require(all(c[a] == hit[b] for a, b in {"frame": "frame", "generation": "generation", "epoch": "epoch",
                "transitionSerial": "transition", "loadSerial": "load", "connectionId": "connection", "hostConnectionId": "host",
                "role": "role", "slot": "slot"}.items()) and tuple(c["location"]) == hit["location"], "incoming_scope_mismatch")
        require(tuple(a["hp"] for a in event["victims"]) == (hit["beforeHp"], hit["afterHp"]), "incoming_hp_mismatch")
        policy = [(r, line) for d,k,r,line in raw if d == "damagepolicy" and k == "event" and num(r,"seq") == hit["event"]]
        require(len(policy) == 1, "missing_or_duplicate_incoming_policy")
        p, line = policy[0]
        require(all(num(p,k) == v for k,v in {"recorded":1,"role":2,"ownerThread":1,"contextAvailable":1,
                "victimClass":1,"sourceClass":5,"action":0,"reason":4 if noop else 10,"supported":0 if noop else 1,"readMask":15,
                "zeroAttempted":0,"claimAttempted":0,"claimQueued":0,"roster0":hit["host"],"roster1":hit["peer"],"roster2":0}.items()), "incoming_policy_refused")
        for kind, selectors, expected in (("context",{},c), ("input",{},event["inputs"][0]),
                ("actor",{"subject":"0"},event["victims"][0]), ("actor",{"subject":"1"},event["sources"][0])):
            rows = [r for d,k,r,_ in raw if d == "damagepolicy" and k == kind and num(r,"seq") == hit["event"] and
                    r.get("phase") == "0" and all(r.get(k) == v for k,v in selectors.items())]
            require(len(rows) == 1, "incoming_policy_snapshot_missing")
            for key in expected:
                if key in ("schema","seq","phase","subject"): continue
                value = tuple(int(rows[0][key][j:j+4],16) for j in range(0,24,4)) if key == "location" else num(rows[0],key)
                require(value == (tuple(expected[key]) if key == "location" else expected[key]), "incoming_policy_snapshot_mismatch")
        require(all(num(p,a) == event["inputs"][0][b] for a,b in {"amount":"damage","flags":"flags","stat":"stat","kind":"kind"}.items()),
                "incoming_policy_input_mismatch")
        item.update(linked=True, disposition="recorded_incoming_noop_already_applied" if noop else "recorded_incoming_player_damage", nativeLine=event["line"], policyLine=line,
                    beforeHp=hit["beforeHp"], afterHp=hit["afterHp"])
    except (ValueError, KeyError) as error:
        item["refusal"] = str(error)
    return item


def replay(host_text, client_text):
    audit = causal.Auditor()
    host = causal.parse(host_text, "host", audit)
    client = causal.parse(client_text, "client", audit)
    causal.prepare(host, "host", audit)
    causal.prepare(client, "client", audit)
    raw = records(host_text)
    client_raw = records(client_text)
    whole = causal.audit_texts(host_text, client_text)
    events, dispositions = [], []
    for hit in (r for r in host if r["kind"] in (1, 3)):
        item = {"event": hit["event"], "netId": hit["netId"], "hostSerial": hit["serial"], "linked": False}
        try:
            require(len([r for r in host if r["kind"] in (1, 3) and r["event"] == hit["event"]]) == 1, "duplicate_host_event")
            require(hit["event"] > 0 and hit["qualified"] == hit["returned"] == hit["readback"] == 1 and hit["reason"] == 0 and
                    0 <= hit["afterHp"] < hit["beforeHp"], "unqualified_host_hit")
            item["native"] = (claim_proof(host, client, client_raw, hit) if hit["kind"] == 3 else native_proof(raw, hit))
            pubs = [r for r in host if r["kind"] == 4 and hit["event"] in r["causes"]]
            require(len(pubs) == 1, "missing_or_duplicate_event_publication")
            pub = pubs[0]
            pub_batch = packet_batch(host, 4, pub)
            require(pub["causes"] == [hit["event"]] and pub["netId"] == hit["netId"] and
                    (pub["beforeHp"], pub["afterHp"]) == (hit["beforeHp"], hit["afterHp"]) and
                    pub["serial"] > hit["serial"] and pub["qpc"] >= hit["qpc"] and
                    causal.target(pub) == causal.target(hit) and causal.scope(pub) == causal.scope(hit), "event_publication_mismatch")
            receives = [r for r in client if r["kind"] == 5 and r["netId"] == hit["netId"] and r["payload"] == pub["payload"]]
            require(len(receives) == 1, "missing_or_duplicate_event_receive")
            receive = receives[0]
            receive_batch = packet_batch(client, 5, receive)
            require(all(receive[k] == pub[k] for k in ("generation", "sessionSalt", "host", "peer", "hostDelivery", "peerDelivery", "epoch", "hpSequence", "beforeHp", "afterHp")), "paired_packet_scope_mismatch")
            stores = [r for r in client if r["kind"] == 6 and r["netId"] == hit["netId"] and r["hpSequence"] == pub["hpSequence"]]
            require(len(stores) == 1, "missing_or_duplicate_event_store")
            store = stores[0]
            require(store["attempted"] == store["returned"] == store["readback"] == store["qualified"] == 1 and
                    store["reason"] == 0 and store["serial"] > receive["serial"] and store["qpc"] >= receive["qpc"] and
                    causal.scope(store) == causal.scope(receive) and causal.target(store) == causal.target(receive) and
                    (store["beforeHp"], store["afterHp"], store["requestedHp"]) == (hit["beforeHp"], hit["afterHp"], hit["afterHp"]), "event_store_mismatch")
            relevant_host = {hit["serial"]} | {r["serial"] for r in pub_batch}
            relevant_client = {store["serial"]} | {r["serial"] for r in receive_batch}
            relevant_client.update(item["native"][k] for k in ("claimSerial","clientHitSerial") if k in item["native"])
            def on_edge(issue):
                return ((issue.get("peer") == "host" and issue.get("serial") in relevant_host) or
                        (issue.get("peer") == "client" and issue.get("serial") in relevant_client))
            require(not any(on_edge(i) for i in audit.issues), "invalid_event_receipt")
            relevant_issues = [i for i in whole["issues"] if on_edge(i)]
            require(not relevant_issues, "invalid_whole_graph_edge:" + str([i["code"] for i in relevant_issues]))
            item.update(linked=True, publicationSerial=pub["serial"], receiveSerial=receive["serial"],
                        friendStoreSerial=store["serial"], hpSequence=pub["hpSequence"],
                        payloadSha256=hashlib.sha256(pub["payload"]).hexdigest(),
                        beforeHp=hit["beforeHp"], afterHp=hit["afterHp"])
        except (ValueError, KeyError) as error:
            item["refusal"] = str(error)
        events.append(item)
    native_events = []
    for domain, kind, event, line in raw:
        if domain != "hittrace" or kind != "event":
            continue
        seq = event.get("seq")
        victims = [r for d, k, r, _ in raw if d == "hittrace" and k == "actor" and
                   r.get("seq") == seq and r.get("subject") == "0"]
        before = [r for r in victims if r.get("phase") == "0"]
        after = [r for r in victims if r.get("phase") == "1"]
        enemy_damage = (len(before) == len(after) == 1 and before[0].get("objectId") == "302" and
                        num(after[0], "hp") < num(before[0], "hp"))
        linked = [e for e in events if e.get("native", {}).get("applySequence") == int(seq) and e["linked"] and
                  e["native"].get("sourceKind") != "client_canonical_claim"]
        native_events.append({"applySequence": int(seq), "line": line,
                              "disposition": "linked" if len(linked) == 1 else
                              "unresolved_enemy_damage" if enemy_damage else "not_replayed_native_event"})
    for pub in (r for r in host if r["kind"] == 4):
        matches = [r for r in client if r["kind"] == 5 and r["netId"] == pub["netId"] and r["payload"] == pub["payload"]]
        noop = pub["beforeHp"] == pub["afterHp"] and not pub["causes"]
        disposition = ("noop_received" if matches else "noop_unmatched") if noop else ("changed_received" if matches else "changed_unmatched")
        dispositions.append({"hostSerial": pub["serial"], "netId": pub["netId"], "hpSequence": pub["hpSequence"],
                             "disposition": disposition, "receiveCount": len(matches), "events": pub["causes"],
                             "transportDisposition": "NOT_ESTABLISHED" if not matches else "recorded_receive"})
    trace = trace_audit.audit_text(client_text)["nativeHitTrace"]
    incoming = [incoming_disposition(client_raw, r, trace["events"]) for r in client if r["kind"] == 1 and r["netId"] == 0]
    # Every parsed receipt is retained and typed, including rejected and no-op rows.
    claims = [{"serial":r["serial"],"claimSequence":r["claimSeq"],"disposition":
               "linked_claim" if len([e for e in events if e["linked"] and e["native"].get("claimSerial") == r["serial"]]) == 1
               else "unresolved_claim"} for r in client if r["kind"] == 2]
    receipts = []
    for peer, rows in (("host",host),("client",client)):
        for row in rows:
            disposition = causal.KINDS[row["kind"]]
            if row["kind"] == 12: disposition = "recorded_consumer_rejection"
            elif row["kind"] == 13: disposition = "recorded_consumer_admission"
            elif row["kind"] in (4,5,6) and row["beforeHp"] == row["afterHp"]: disposition = "recorded_noop_" + disposition
            elif row["kind"] == 1 and peer == "client" and row["netId"] == 0:
                disposition = next(i["disposition"] for i in incoming if i["serial"] == row["serial"])
            receipts.append({"peer":peer,"serial":row["serial"],"line":row["line"],"kind":row["kind"],"reason":row["reason"],
                             "netId":row["netId"],"disposition":disposition,"payloadSha256":hashlib.sha256(row["payload"]).hexdigest(),
                             "fields":{k:(v.hex() if isinstance(v,bytes) else v) for k,v in row.items()},
                             "receiptIssues":[i for i in whole["issues"] if i.get("peer") == peer and i.get("serial") == row["serial"]]})
    return {"schemaVersion": 1, "status": "RECORDED_LOCAL_CHAINS_LINKED" if events and all(e["linked"] for e in events) and
            not any(e["disposition"] == "unresolved_enemy_damage" for e in native_events) and
            all(c["disposition"] == "linked_claim" for c in claims) else "UNRESOLVED",
            "acceptance": False, "exactlyOneSourceProven": False, "coverageQualified": False,
            "events": events, "nativeEvents": native_events, "publications": dispositions,
            "incoming": incoming, "claims": claims, "receiptDispositions": receipts, "wholeGraph": whole,
            "rawNativePolicy":{peer:[{"domain":d,"kind":k,"fields":r,"line":line} for d,k,r,line in rows]
                               for peer,rows in (("host",raw),("client",client_raw))},
            "clientNativeEvents":[{"applySequence":e["sequence"],"line":e["line"],"disposition":
                "linked_claim" if any(c["linked"] and c["native"].get("sourceKind") == "client_canonical_claim" and
                                      c["native"].get("applySequence") == e["sequence"] for c in events) else
                next((i["disposition"] for i in incoming if i["event"] == e["sequence"]),"unresolved_native_event")}
                for e in trace["events"]],
            "wholeRunStatus": whole["status"], "coverageIssues": whole["issues"],
            "limits": ["Recorded host direct-Stat, client queued-claim zero-Stat and incoming Take/Stat paths are replayed; unsupported paths refuse.",
                       "No sealed drain/end cutoff, authenticated runtime session or complete native-writer coverage certificate.",
                       "Unmatched publications are unresolved observations, not inferred network loss or application failure.",
                       "No cross-peer clock comparison; packet bytes and local scope/order bind the edge."]}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        result = replay(args.host.read_text(encoding="utf-8"), args.client.read_text(encoding="utf-8"))
        result["inputs"] = {role: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                            for role, path in (("host", args.host), ("client", args.client))}
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        return 0 if result["status"] == "RECORDED_LOCAL_CHAINS_LINKED" else 1
    except (ValueError, OSError, KeyError) as error:
        parser.exit(2, str(error) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
