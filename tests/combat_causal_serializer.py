"""Offline interpretation of the production Drain printf calls, not a new schema.

Extracts the real format strings AND ordered argument expressions from
CombatCausalTrace.hpp. Only the simple copied-facts expressions used there are
supported; source changes outside this subset fail tests. No C++ binary is run.
Receipt facts are synthetic, not output from native gameplay or Engine admission.
"""
import re
from pathlib import Path
from types import SimpleNamespace as NS

SOURCE = Path(__file__).resolve().parents[1] / "inject/src/CombatCausalTrace.hpp"


def split_args(text):
    result, start, depth = [], 0, 0
    for i, c in enumerate(text):
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        elif c == "," and depth == 0:
            result.append(text[start:i].strip())
            start = i + 1
    result.append(text[start:].strip())
    return result


CALLS = {}
for match in re.finditer(r'log\("(\[combat-causal\] (\w+) [^"\n]*)",\s*(.*?)\);',
                         SOURCE.read_text(encoding="utf-8"), re.S):
    CALLS[match[2]] = match[1], split_args(match[3])
assert set(CALLS) == {"receipt", "consumer", "association", "summary"}


def namespace(row):
    native = NS(**{dest: row[src] for src, dest in (
        ("generation", "generation"), ("epoch", "epoch"), ("load", "loadSerial"),
        ("transition", "transitionSerial"), ("connection", "connectionId"),
        ("host", "hostConnectionId"), ("role", "role"), ("slot", "slot"),
        ("frame", "frame"), ("location", "location"))})
    scope = NS(native=native, delivery=row["delivery"], sessionSalt=row["sessionSalt"],
               roster=[row["host"], row["peer"], 0],
               peerDelivery=[row["hostDelivery"], row["peerDelivery"], 0])
    target = NS(**{k: row[k] for k in ("netId", "objectId", "type", "actor", "objentry",
                                       "status", "controller", "record", "maxHp")}, hp=row["targetHp"])
    key = NS(connection=row["claimConnection"], sequence=row["claimSeq"], epoch=row["epoch"],
             netId=row["netId"], objectId=row["objectId"], attackId=row["attackId"], damage=row["damage"])
    r = NS(**{k: row[k] for k in ("serial", "qpc", "kind", "reason", "event", "hpSequence", "loss",
                                  "beforeHp", "afterHp", "requestedHp", "attempted", "returned", "readback",
                                  "enqueued", "causeCount", "payloadBytes")},
           locallyQualified=row["qualified"], payloadDigest=row["payloadFnv64"],
           scope=scope, admittedScope=scope, target=target, key=key,
           causes=row.get("causes", []), consumerOutcome=2 if row["kind"] == 12 else 1,
           wireType=row["payload"][0] if row["payload"] else 0,
           consumerEpoch=row.get("expectedEpoch", 0), consumerSequenceFloor=row.get("sequenceFloor", 0),
           payloadOriginalBytes=len(row["payload"]), payloadTruncated=False)
    return r


def emit(name, **context):
    fmt, expressions = CALLS[name]
    values = []
    for expression in expressions:
        expression = expression.replace("static_cast<unsigned long long>", "int")
        # No arbitrary source execution: only member/index reads, wrapper calls,
        # and the eight named zero-argument Engine accessors used by Drain.
        if not re.fullmatch(r"[A-Za-z0-9_.\[\]()]+", expression):
            raise ValueError("unsupported serializer expression " + expression)
        values.append(eval(expression, {"__builtins__": {}, "U": int, "unsigned": int,
                                        "int": int}, context))
    return re.sub(r"%llu", "%d", fmt).replace("%llX", "%X").replace("%u", "%d") % tuple(values) + "\n"


def render(rows, summary_changes=None):
    text = ""
    for row in rows:
        r = namespace(row)
        text += emit("receipt", r=r, payload=row["payload"].hex())
        if row["kind"] in (5, 8, 12, 13):
            text += emit("consumer", r=r, a=r.admittedScope)
        for i in range(row["causeCount"]):
            text += emit("association", r=r, i=i)
    fields = dict(Admitted=1, Retired=0, Started=len(rows), Drained=len(rows), Dropped=0, Loss=0)
    fields.update(summary_changes or {})
    engine = NS(**{k: (lambda value=v: value) for k, v in fields.items()})
    return text + emit("summary", engine=engine)
