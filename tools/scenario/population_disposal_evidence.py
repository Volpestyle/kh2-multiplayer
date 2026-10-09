"""Offline coverage check for repair-06 ticket outcomes; supplements the population oracle.

This validates diagnostic continuity and native one-shot accounting. It does not
prove native absence, host terminal authority, or a living-set match; those still
require the fixture's anchored census/cut oracle.
"""
import json
import re

PREFIX = "[enemy-pop] disposal-outcome-json "
SEAL = re.compile(r"\[enemy-pop\] disposal-outcome-seal seq=(\d+) tickets=(\d+) loss=(\d+) attempts=(\d+)$")


def check_outcomes(text, *, require_seal=True):
    """Return INCONCLUSIVE on missing/ambiguous coverage, FAIL on repeat native calls."""
    try:
        if "[enemy-pop] receipt-loss " in text:
            raise ValueError("disposal receipt coverage lost")
        tickets, attempted, last_seq, seals = {}, {}, 0, []
        for line in text.splitlines():
            if PREFIX not in line:
                match = SEAL.search(line)
                if match:
                    seals.append(tuple(map(int, match.groups())))
                continue
            if seals:
                raise ValueError("outcome after final seal")
            event = json.loads(line.split(PREFIX, 1)[1])
            if not isinstance(event, dict) or type(event.get("schema")) is not int or event["schema"] != 1:
                raise ValueError("unknown disposal outcome schema")
            for field in ("seq", "ticket", "netId", "cut", "deathSequence", "frame"):
                if type(event.get(field)) is not int or event[field] <= 0:
                    raise ValueError("invalid outcome " + field)
            for field in ("load", "transition", "expected", "observed", "priorFirst", "priorLast", "priorRepeats"):
                if type(event.get(field)) is not int or event[field] < 0:
                    raise ValueError("invalid outcome " + field)
            if type(event.get("available")) is not bool or not isinstance(event.get("reason"), str) or not event["reason"]:
                raise ValueError("missing refusal detail")
            roots = event.get("roots")
            if not isinstance(roots, list) or len(roots) != 5 or any(type(v) is not int or v <= 0 for v in roots):
                raise ValueError("invalid five-root outcome identity")
            if event["seq"] != last_seq + 1:
                raise ValueError("outcome sequence gap or duplicate")
            last_seq = event["seq"]
            key = (*roots, event["load"], event["transition"])
            identity = (key, event["netId"], event["cut"], event["deathSequence"])
            ticket, action = event["ticket"], event.get("action")
            if action == "produced":
                if ticket in tickets:
                    raise ValueError("duplicate ticket production")
                tickets[ticket] = [identity, action, event["frame"]]
                continue
            if ticket not in tickets or tickets[ticket][0] != identity:
                raise ValueError("missing production or substituted ticket identity")
            previous, previous_frame = tickets[ticket][1:]
            if event["frame"] < previous_frame:
                raise ValueError("ticket frame regressed")
            if action == "attempted":
                if key in attempted:
                    return {"status": "FAIL", "reason": "native one-shot reattempted across ticket authorization"}
                if previous not in ("produced", "refused"):
                    raise ValueError("attempt lacks current ticket authority")
            elif action in ("call-returned", "call-fault"):
                if key in attempted:
                    return {"status": "FAIL", "reason": "native one-shot repeated"}
                if previous != "attempted":
                    raise ValueError("native call lacks attempted receipt")
                attempted[key] = event["frame"]
            elif action == "waiting-native-removal":
                if event["reason"] == "one-shot-poisoned" and key not in attempted:
                    raise ValueError("poisoned wait lacks native call receipt")
            elif action == "disposed":
                if (key not in attempted or previous not in ("call-returned", "call-fault", "waiting-native-removal")
                        or event["frame"] <= attempted[key]):
                    raise ValueError("disposed without native call continuity")
            elif action not in ("refused", "cancelled"):
                raise ValueError("unknown ticket outcome")
            if previous in ("disposed", "cancelled"):
                raise ValueError("outcome after terminal ticket receipt")
            tickets[ticket][1:] = [action, event["frame"]]
        if not tickets:
            raise ValueError("no disposal ticket outcomes")
        if require_seal:
            if len(seals) != 1:
                raise ValueError("one final disposal outcome seal required")
            sequence, count, loss, calls = seals[0]
            if sequence != last_seq or count != len(tickets) or loss or calls != len(attempted):
                raise ValueError("disposal seal coverage mismatch")
            if any(row[1] not in ("disposed", "cancelled", "refused") for row in tickets.values()):
                raise ValueError("ticket lacks final outcome")
        return {"status": "PASS", "tickets": len(tickets), "calls": len(attempted)}
    except (ValueError, TypeError, KeyError) as error:
        return {"status": "INCONCLUSIVE", "reason": str(error)}
