"""One host boss HP pool, damaged by both seats exactly once (VUH-2910, Axel I).

Reads the host and friend inject logs of a run that set KH2COOP_TRACE_HITS=1 and
KH2COOP_ENEMY_MIRROR_TRACE=1 on both seats, and joins every change to the
host boss's HP to its cause:

- a host native hit: a `[hittrace]` ApplyHitDamage scope on the host whose victim
  is the boss and whose `[damagepolicy]` source is the host's canonical avatar
  (class 1, action Native), with the victim's HP before and after the call;
- a friend hit: a `[enemysync] host hit claim` application with nativeOk=1 and
  its before/after HP, joined by all seven of connection/seq/epoch/netId/
  objectId/attackId/damage to exactly one `[enemysync] client hit claim` sent in
  the friend's window. Every client claim sent in the window must have exactly
  one application, or a `[hitclaim] reject` with the same identity.

One boss lifetime: the host's (epoch, netId, actor) is pinned from its samples
in the window and at settle. The friend's copy is the actor of the
`[enemy-binding]` decision in force when the window opens (its `selected` is the
bound netId, 0 on conflict), which must not be lost or changed until the friend's
settled sample. Claims, host hits, start and settled samples, the friend's
take-over, brain records, samples and local hits must all belong to it. The
reported two-player scale is that boss actor's scale line, and the host room
line, between the previous epoch's arrival and the window; both must say
players=2, and the boss's max HP on its hits must be the scale's.

Every hit scope the ledger relies on must be complete, read proof: NativeHitTrace
prints an unread field as 0, so both phases need full read masks (context 127 and
available, victim and source actors 511, hit record 255), verified hook coverage
(mask 7) and the producer's stability flags, before/after contexts, actors and hit
records (handles, kind, stat) whose fields are present and agree, and a recorded
policy envelope whose authority rows and decision input are the phase-0 capture
(vetoes revalidated). Every component appears once. A scope that is incomplete,
duplicated or contradictory is kept as invalid evidence: on the boss (or on an
unknown victim) it fails the ledger; as a player hit it does not count. A player hit counts only from
that seat's copy of the pinned boss in the boss lifetime's epoch.

The successful decrements must tile the boss's HP from the window's starting
value to the settled value with no gap or overlap: the decrease equals their
sum, and no other writer (a puppet-replayed hit, a client write) changed it.
Host hits from the friend's puppet must be checked zeros (classes 2/3, action
ZeroHp) and leave HP unchanged. The friend's own hits on its copy must be
claim-then-zero with HP unchanged there: ClaimThenZero with the claim queued and
a checked zero, one per sent claim (same attackId and damage). A checked zero is
DamagePolicy's Zeroed, or Changed when the claim channel's sync drop zeroed the
record first (syncDrop=1 and damage 0 after the call), as on the live friend. A
zero-damage local hit is a native refusal. Every host and friend
`[statehash] native` HP sample for the boss in the window must be a value on the
chain, and the last samples after the window must agree. Native guard/i-frame
and zero-damage outcomes are listed as refusals, not failures.

The host boss's hits on the host's copy of a remote player (VUH-2912, when the boss targets that seat) must be
complete checked zeros with the copy's HP unchanged; its owner takes the attack natively from its own mirrored
boss. An incomplete scope with the boss as source fails. A phase with `cloneZero` requires at least one such
checked zero, and `hitOnHost`/`hitOnFriend` a native boss hit on that seat's canonical player (which seat the
boss chose, and the hit's reaction, are boss_target_ledger.py's).

Also reported: the boss's two-player scale and room lines, the friend's binding and
brain-skip (enemy-mirror updates == skips, noBrain 0, no retire) through the
window, each seat's native boss hits on its canonical player (required in a phase
where both seats must hit, and over the window when no phases are given; a
downed solo seat cannot be hit), per-phase counts,
and tick stamps (GetTickCount of the next `[statehash] role=` line: at most about
1 s late; one-PC runs only) for warp, intro hold, release and first damage.

Windows are line ranges. A scenario saves `[log_count('', host), log_count('',
friend)]` at each mark; phases name a start and end mark and what they require.

    python -B tools/scenario/boss_hp_ledger.py --host H.log --friend F.log --object 2229
        --marks marks.json [--out ledger.json]

marks.json: {"marks": {"start": [h, f], ...}, "phases": [{"name": "host_only", "from":
"start", "to": "m1", "host": true, "friend": false}, ...], "window": ["start", "end"]}
Exit 0 on pass, 1 on fail.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

FIELD = re.compile(r"(\w+)=(\S+)")
TRACE = re.compile(r"\[(?P<family>hittrace|damagepolicy)\] (?P<kind>event|context|actor|input|child) schema=1 seq=(?P<seq>\d+)")
HOST_CLAIM = re.compile(r"\[enemysync\] host hit claim ")
CLIENT_CLAIM = re.compile(r"\[enemysync\] client hit claim ")
CLAIM_REJECT = re.compile(r"\[hitclaim\] reject ")
NATIVE_HP = re.compile(r"\[statehash\] native epoch=(?P<epoch>\d+) frame=(?P<frame>\d+) netId=(?P<netId>\d+) objectId=(?P<objectId>\d+) hp=(?P<hp>-?\d+) actor=(?P<actor>[0-9A-F]+)")
ROLE_TICK = re.compile(r"\[statehash\] role=(?:host|client) .*?tickMs=(?P<tick>\d+)")
SCALE = re.compile(r"\[coop-difficulty\] scale frame=\d+ objectId=(?P<objectId>\d+) actor=(?P<actor>[0-9A-F]+) players=(?P<players>\d+) mode=(?P<mode>\d+) hp=(?P<hp0>-?\d+)->(?P<hp1>-?\d+) max=(?P<max0>-?\d+)->(?P<max1>-?\d+)")
ROOM_SCALE = re.compile(r"\[coop-difficulty\] room role=(?P<role>\w+) mode=(?P<mode>\d+) players=(?P<players>\d+) mob=(?P<mob>\d+) boss=(?P<boss>\d+)")
MIRROR = re.compile(r"\[enemy-mirror\] (?P<what>take-over|actor|retire) frame=(?P<frame>\d+) netId=(?P<netId>\d+) actor=(?P<actor>[0-9A-F]+) running=(?P<running>\d+) .*?updates=(?P<updates>\d+) skips=(?P<skips>\d+) .*?noBrain=(?P<noBrain>\d+) .*?objectId=(?P<objectId>\d+)")
BINDING = re.compile(r"\[enemy-binding\] ")
ARRIVED = re.compile(r"\[enemysync\] host arrived epoch=(?P<epoch>\d+) ")
SUMMARY = re.compile(r"\[hittrace\] summary schema=1 .*?dropped=(?P<dropped>\d+) foreign=\d+ unwound=(?P<unwound>\d+) nested=(?P<nested>\d+) overflow=(?P<overflow>\d+)")
MILESTONES = {
    "warp": re.compile(r"\[warp\] load complete serial=\d+ transition=\d+ room=(?P<room>[0-9A-F]{2}/[0-9A-F]{2}) door=\d+ map=\d+ btl=(?P<btl>\d+)"),
    "hold": re.compile(r"\[prologue\] hold at=(?P<room>[0-9A-F]{2}/[0-9A-F]{2}) evt=(?P<evt>\d+) site=(?P<site>\S+)"),
    "release": re.compile(r"\[prologue\] control released at=(?P<room>[0-9A-F]{2}/[0-9A-F]{2}) evt=(?P<evt>\d+) site=(?P<site>\S+)"),
}
CLAIM_KEY = ("connection", "seq", "epoch", "netId", "objectId", "attackId", "damage")
REJECT_KEY = ("connection", "seq", "epoch", "netId", "objectId", "damage")  # LogClaim carries no attackId
# DamagePolicy enums (inject/src/DamagePolicy.hpp).
LOCAL_AVATAR, REMOTE_CLASSES, NATIVE, ZERO_HP, CLAIM_THEN_ZERO, ZEROED, CHANGED = 1, (2, 3), 0, 1, 2, 0, 2


def fields(line: str) -> dict:
    return dict(FIELD.findall(line))


# NativeHitTrace read bits (inject/src/NativeHitTrace.hpp). An unread field is printed as 0, so every field
# the ledger trusts needs its read bit, and the field itself must be present. Before/after snapshots must agree
# (as its SameContext/SameActor do), and the policy's recorded authority must be the phase-0 capture.
ALL_HOOKS, CONTEXT_COMPLETE, ACTOR_COMPLETE, HIT_COMPLETE, POLICY_HIT_READS = 7, 127, 511, 255, 7
EVENT_OK = {"ownerThread": "1", "returned": "1", "unwound": "0", "nested": "0", "overflow": "0", "coverageStable": "1",
            "contextStable": "1", "metadataStable": "1", "lossStable": "1", "coverageMask": str(ALL_HOOKS)}
SAME_CONTEXT = ("frame", "generation", "epoch", "transitionSerial", "loadSerial", "connectionId", "hostConnectionId",
                "role", "slot", "location")
SAME_ACTOR = ("actor", "objentry", "status", "objectId", "type", "team", "namePrefix", "maxHp")
# Stable across the call; flags (the applied bit) and damage (a sync drop) legitimately change.
SAME_HIT = ("hit", "attack", "owner", "canonicalPlayer", "head", "tracked", "attackHandle", "atkpHandle",
            "ownerHandle", "attackId", "stat", "kind")


def part(family: str, kind: str, phase: int | None = None, sub: int | None = None) -> tuple:
    return family, kind, phase, sub


EVENT, POLICY = part("hittrace", "event"), part("damagepolicy", "event")
REQUIRED = ([EVENT, POLICY] + [part("hittrace", k, ph) for k in ("context", "input") for ph in (0, 1)] +
            [part("hittrace", "actor", ph, sub) for ph in (0, 1) for sub in (0, 1)] +
            [part("damagepolicy", k, 0) for k in ("context", "input")] +
            [part("damagepolicy", "actor", 0, sub) for sub in (0, 1)])


def differ(a: dict, b: dict, keys) -> list[str]:
    """Keys missing from either row or unequal: a field absent from both is not agreement."""
    return [k for k in keys if k not in a or k not in b or a[k] != b[k]]


def scope_problem(parts: dict, duplicates: list) -> str | None:
    """Why an ApplyHitDamage scope is not complete, read, stable and coherent proof, or None."""
    if duplicates:
        return f"duplicate components {sorted({str(d) for d in duplicates})}"
    missing = [k for k in REQUIRED if k not in parts]
    if missing:
        return f"missing components {missing}"
    ev, p = parts[EVENT], parts[POLICY]
    ctx = {ph: parts[part("hittrace", "context", ph)] for ph in (0, 1)}
    hits = {ph: parts[part("hittrace", "input", ph)] for ph in (0, 1)}
    actor = lambda ph, sub: parts[part("hittrace", "actor", ph, sub)]
    bad = [k for k, v in EVENT_OK.items() if ev.get(k) != v]
    if bad:
        return f"event flags {bad}"
    mask = lambda f: int(f["readMask"]) if f.get("readMask", "").isdigit() else 0
    for ph in (0, 1):
        if ctx[ph].get("available") != "1" or mask(ctx[ph]) & CONTEXT_COMPLETE != CONTEXT_COMPLETE:
            return f"context phase {ph} unread"
        for sub in (0, 1):
            a = actor(ph, sub)
            if mask(a) & ACTOR_COMPLETE != ACTOR_COMPLETE or "0" in (a.get("actor", "0"), a.get("objentry", "0"), a.get("status", "0")):
                return f"actor phase {ph} subject {sub} unread"
        if mask(hits[ph]) & HIT_COMPLETE != HIT_COMPLETE or "0" in (hits[ph].get("hit", "0"), hits[ph].get("attack", "0")):
            return f"hit record phase {ph} unread"
    checks = [("context changed across the call", ctx[0], ctx[1], SAME_CONTEXT),
              ("victim changed across the call", actor(0, 0), actor(1, 0), SAME_ACTOR),
              ("source changed across the call", actor(0, 1), actor(1, 1), SAME_ACTOR),
              ("hit record identity changed across the call", hits[0], hits[1], SAME_HIT),
              ("policy context is not the capture", parts[part("damagepolicy", "context", 0)], ctx[0],
               SAME_CONTEXT + ("readMask", "available")),
              ("policy hit record is not the capture", parts[part("damagepolicy", "input", 0)], hits[0],
               SAME_HIT + ("flags", "damage", "readMask")),
              ("policy victim is not the capture", parts[part("damagepolicy", "actor", 0, 0)], actor(0, 0),
               SAME_ACTOR + ("hp", "readMask")),
              ("policy source is not the capture", parts[part("damagepolicy", "actor", 0, 1)], actor(0, 1),
               SAME_ACTOR + ("hp", "readMask"))]
    for why, x, y, keys in checks:
        off = differ(x, y, keys)
        if off:
            return f"{why} {off}"
    if (p.get("recorded"), p.get("ownerThread"), p.get("contextAvailable")) != ("1", "1", "1") or \
            mask(p) & POLICY_HIT_READS != POLICY_HIT_READS or p.get("role") != ctx[0].get("role"):
        return "policy envelope incomplete"
    decided = {k: p[k] for k in ("stat", "kind", "flags", "amount") if k in p}
    captured = {k: hits[0][k] for k in ("stat", "kind", "flags") if k in hits[0]}
    if "damage" in hits[0]:
        captured["amount"] = hits[0]["damage"]
    off = differ(decided, captured, ("stat", "kind", "flags", "amount"))
    if off:
        return f"policy decision input is not the capture {off}"
    if p.get("action") != "0" and (p.get("supported"), p.get("revalidationAttempted"), p.get("revalidationPassed")) != ("1", "1", "1"):
        return "policy veto not revalidated"
    return None


def hit_events(lines: list[str], lo: int, hi: int) -> list[dict]:
    """Every ApplyHitDamage scope (sequence) placed in [lo, hi) by its event line, or by its first component when
    the event is missing. Incomplete, duplicated or contradictory scopes are kept, with `invalid` saying why."""
    scopes: dict[int, dict] = {}
    for n, line in enumerate(lines):
        m = TRACE.search(line)
        if not m:
            continue
        f = fields(line[m.end():])
        num = lambda k: int(f[k]) if f.get(k, "").isdigit() else None
        key = part(m["family"], m["kind"], num("phase"), num("subject") if "subject" in f else num("index"))
        s = scopes.setdefault(int(m["seq"]), {"parts": {}, "duplicates": [], "first": n})
        if key in s["parts"]:
            s["duplicates"].append(key)
        else:
            s["parts"][key] = f
            if key == EVENT:
                s["line"] = n
    out = []
    for seq, s in scopes.items():
        line, parts = s.get("line", s["first"]), s["parts"]
        if not lo <= line < hi:
            continue
        get = lambda *key: parts.get(part(*key), {})
        num = lambda f, k, default=-1: int(f[k]) if re.fullmatch(r"-?\d+", f.get(k, "")) else default
        victim = get("hittrace", "actor", 0, 0) or get("hittrace", "actor", 1, 0) or get("damagepolicy", "actor", 0, 0)
        source, hit0, hit1 = get("hittrace", "actor", 0, 1), get("hittrace", "input", 0), get("hittrace", "input", 1)
        ctx0, p = get("hittrace", "context", 0), parts.get(POLICY, {})
        out.append({"seq": seq, "line": line, "frame": num(ctx0, "frame"),
                    "invalid": scope_problem(parts, s["duplicates"]),
                    "epoch": num(ctx0, "epoch"), "attackId": num(hit0, "attackId"),
                    "syncDrop": hit0.get("syncDrop") == "1", "damageBefore": num(hit0, "damage"),
                    "damageAfter": num(hit1, "damage"),
                    "victimObject": num(victim, "objectId", None), "victimActor": victim.get("actor"),
                    "victimMaxHp": num(victim, "maxHp"),
                    "sourceObject": num(source, "objectId"), "sourceActor": source.get("actor"),
                    "hpBefore": num(get("hittrace", "actor", 0, 0), "hp"),
                    "hpAfter": num(get("hittrace", "actor", 1, 0), "hp"),
                    "victimClass": num(p, "victimClass"), "sourceClass": num(p, "sourceClass"),
                    "amount": num(p, "amount"), "stat": num(p, "stat"), "action": num(p, "action"),
                    "reason": num(p, "reason"), "zeroResult": num(p, "zeroResult"),
                    "zeroAttempted": p.get("zeroAttempted") == "1", "claimQueued": p.get("claimQueued") == "1"})
    return sorted(out, key=lambda e: e["seq"])


def checked_zero(e: dict) -> bool:
    """On a valid scope: DamagePolicy zeroed the record, or the claim channel's sync drop had already zeroed it
    (read nonzero before, read zero after; DamagePolicy then reports Changed)."""
    return not e["invalid"] and e["zeroAttempted"] and (
        e["zeroResult"] == ZEROED or
        e["zeroResult"] == CHANGED and e["syncDrop"] and e["damageBefore"] > 0 and e["damageAfter"] == 0)


def claims(lines: list[str], pattern: re.Pattern, objectId: int, lo: int = 0, hi: int | None = None) -> list[dict]:
    rows = []
    for n, line in enumerate(lines[lo:hi], lo):
        if pattern.search(line):
            f = fields(line)
            if int(f.get("objectId", objectId)) == objectId:  # a row missing objectId still has to join
                rows.append({"line": n, **{k: int(v) for k, v in f.items() if re.fullmatch(r"-?\d+", v)}})
    return rows


def tick_after(lines: list[str], n: int) -> int | None:
    """GetTickCount of the first role statehash at or after line n (at most ~1 s late)."""
    for line in lines[n:]:
        m = ROLE_TICK.search(line)
        if m:
            return int(m["tick"])
    return None


def first(rows: list, key=lambda r: True):
    return next((r for r in rows if key(r)), None)


def ledger(host_text: str, friend_text: str, objectId: int, marks: dict) -> dict:
    host, friend = host_text.splitlines(), friend_text.splitlines()
    problems: list[str] = []
    m = marks["marks"]
    start, end = (m[name] for name in marks.get("window", ["start", "end"]))
    (h0, f0), (h1, f1) = start, end

    # The boss on the host: its scale, its one lifetime (epoch, netId, actor), its samples.
    def samples(lines):
        return [(n, int(s["epoch"]), int(s["netId"]), s["actor"], int(s["hp"])) for n, line in enumerate(lines)
                for s in [NATIVE_HP.search(line)] if s and int(s["objectId"]) == objectId]
    host_hp, friend_hp = samples(host), samples(friend)
    settled_row = host_hp[-1] if host_hp and host_hp[-1][0] >= h1 else None
    lifetimes = {r[1:4] for r in host_hp if h0 <= r[0] < h1} | ({settled_row[1:4]} if settled_row else set())
    if len(lifetimes) != 1:
        problems.append(f"host boss lifetimes (epoch, netId, actor) in the window: {sorted(lifetimes)} (want exactly one)")
    epoch, net_id, boss_actor = next(iter(lifetimes)) if len(lifetimes) == 1 else (None, None, None)
    ours = lambda row: row[1:4] == (epoch, net_id, boss_actor)
    # Everything from the window's start to the settled sample must be explained.
    host_end = settled_row[0] + 1 if settled_row else h1

    # Two-player scale of this boss in this encounter: the boss actor's scale line and the host room line
    # between the previous epoch's arrival and the window.
    floor = max([n for n, line in enumerate(host[:h0]) for r in [ARRIVED.search(line)]
                 if r and epoch is not None and int(r["epoch"]) < epoch], default=0)
    scales = [r.groupdict() for line in host[floor:h0] for r in [SCALE.search(line)]
              if r and int(r["objectId"]) == objectId and r["actor"] == boss_actor]
    rooms = [r.groupdict() for line in host[floor:h0] for r in [ROOM_SCALE.search(line)] if r and r["role"] == "host"]
    scale, room = (scales or [None])[-1], (rooms or [None])[-1]
    if not scale or scale["players"] != "2" or not room or room["players"] != "2":
        problems.append(f"no two-player scale for boss {boss_actor} in this encounter: scale {scale}, room {room}")

    # The friend's copy: the binding decision in force when the window opens (selected is the bound netId;
    # 0 on conflict), unchanged until the friend's settled sample.
    friend_settled_line = friend_hp[-1][0] if friend_hp and friend_hp[-1][0] >= f1 else f1
    bindings = [(n, fields(line)) for n, line in enumerate(friend) if BINDING.search(line)]
    before = [b for n, b in bindings if n < f0 and b.get("objectId") == str(objectId)]
    authority = before[-1] if before else None
    bound = bool(authority) and (authority.get("selected"), authority.get("conflict"), authority.get("epoch")) == \
        (str(net_id), "0", str(epoch)) and authority.get("actor", "0") != "0"
    friend_actor = authority["actor"] if bound else None
    if not bound:
        problems.append(f"friend copy not bound to netId {net_id} in epoch {epoch} before the window: {authority}")
    identity = ("objectId", "actor", "selected", "conflict", "epoch")
    changed = [n for n, b in bindings if f0 <= n <= friend_settled_line and authority and
               (b.get("objectId") == str(objectId) or b.get("actor") == friend_actor) and differ(b, authority, identity)]
    if changed:
        problems.append(f"friend binding lost or changed after the window opened (lines {changed[:3]})")
    friend_ours = lambda row: row[1:4] == (epoch, net_id, friend_actor)

    # Causes of HP change on the host.
    host_events = hit_events(host, h0, host_end)
    on_boss = [e for e in host_events if e["victimObject"] in (objectId, None)]
    native_hits, refusals, puppet, unexplained = [], [], [], []
    for e in on_boss:
        delta = e["hpBefore"] - e["hpAfter"]
        if e["invalid"]:
            unexplained.append({**e, "why": f"incomplete hit proof: {e['invalid']}"})
        elif (e["victimActor"], e["epoch"]) != (boss_actor, epoch):
            unexplained.append({**e, "why": "victim is not the boss lifetime"})
        elif e["sourceClass"] in REMOTE_CLASSES:
            if delta != 0 or e["action"] != ZERO_HP or not checked_zero(e):
                unexplained.append({**e, "why": "friend puppet hit was not a checked zero"})
            else:
                puppet.append(e)
        elif delta > 0 and e["sourceClass"] == LOCAL_AVATAR and e["action"] == NATIVE:
            native_hits.append(e)
        elif delta == 0:
            refusals.append(e)
        else:
            unexplained.append({**e, "why": f"HP {e['hpBefore']}->{e['hpAfter']} from source class {e['sourceClass']} action {e['action']}"})

    # Claims: both directions, all seven identity fields, the boss lifetime, sent inside the friend window.
    applied = claims(host, HOST_CLAIM, objectId, h0, host_end)
    rejects = claims(host, CLAIM_REJECT, objectId, h0, host_end)
    sent = claims(friend, CLIENT_CLAIM, objectId)
    key = lambda row, names=CLAIM_KEY: tuple(row.get(k) for k in names)
    for who, rows in (("host application", applied), ("client claim", [c for c in sent if f0 <= c["line"]])):
        for r in rows:
            missing = [k for k in CLAIM_KEY if k not in r]
            if missing:
                problems.append(f"{who} at line {r['line']} lacks {missing}")
            elif (r["epoch"], r["netId"]) != (epoch, net_id):
                problems.append(f"{who} seq {r['seq']} epoch/netId {r['epoch']}/{r['netId']} is not the boss lifetime {epoch}/{net_id}")
    complete = lambda r: all(k in r for k in CLAIM_KEY)
    joined, claim_refusals = [], []
    for a in applied:
        matches = [c for c in sent if complete(a) and complete(c) and key(c) == key(a)]
        dup = [b for b in applied if (b.get("connection"), b.get("seq")) == (a.get("connection"), a.get("seq"))]
        if len(matches) != 1:
            problems.append(f"host claim seq {a.get('seq')} matches {len(matches)} friend claims (want 1)")
        elif not f0 <= matches[0]["line"] < f1:
            problems.append(f"host claim seq {a['seq']} joins a client claim sent outside the friend window (line {matches[0]['line']})")
        if len(dup) != 1:
            problems.append(f"claim connection {a.get('connection')} seq {a.get('seq')} applied {len(dup)} times")
        if a.get("nativeOk") != 1 or a.get("afterHpAvailable") != 1:
            problems.append(f"claim seq {a.get('seq')} native application not checked: {a}")
        elif a["afterHp"] == a["beforeHp"]:
            claim_refusals.append(a)
        elif a["afterHp"] != a["beforeHp"] - a.get("damage", 0):
            problems.append(f"claim seq {a['seq']} HP {a['beforeHp']}->{a['afterHp']} is not -{a.get('damage')}")
        else:
            joined.append({**a, "friendLine": matches[0]["line"] if len(matches) == 1 else None})
    window_sent = [c for c in sent if f0 <= c["line"] < f1]
    unapplied = []
    for c in window_sent:
        apps = [a for a in applied if complete(a) and complete(c) and key(a) == key(c)]
        if len(apps) == 1:
            continue
        refused = [r for r in rejects if key(r, REJECT_KEY) == key(c, REJECT_KEY)]
        unapplied.append({**c, "rejects": refused})
        if apps:
            problems.append(f"client claim seq {c.get('seq')} applied {len(apps)} times")
        elif not refused:
            problems.append(f"client claim seq {c.get('seq')} never applied and no matching reject")

    # The chain: successful decrements tile [settled, start] exactly.
    steps = sorted([{"kind": "host", "before": e["hpBefore"], "after": e["hpAfter"], "line": e["line"], "seq": e["seq"]}
                    for e in native_hits] +
                   [{"kind": "friend", "before": a["beforeHp"], "after": a["afterHp"], "line": a["line"], "seq": a["seq"],
                     "friendLine": a["friendLine"]} for a in joined], key=lambda s: -s["before"])
    before_window = [r for r in host_hp if r[0] < h0]
    if before_window and not ours(before_window[-1]):
        problems.append(f"host start sample {before_window[-1][1:4]} is not the boss lifetime")
    start_hp = before_window[-1][4] if before_window else (int(scale["hp1"]) if scale else None)
    settled = settled_row[4] if settled_row else None
    nodes = {start_hp}
    expect = start_hp
    for s in steps:
        if s["before"] != expect:
            problems.append(f"HP chain gap: {s['kind']} step {s['before']}->{s['after']} follows {expect}")
        expect = s["after"]
        nodes.add(s["after"])
    if settled is None:
        problems.append("no host boss HP sample after the window")
    elif expect != settled:
        problems.append(f"HP chain ends at {expect}, host settled at {settled}: an unrecorded writer")
    decrease = (start_hp - settled) if None not in (start_hp, settled) else None
    total = sum(s["before"] - s["after"] for s in steps)
    if decrease is not None and decrease != total:
        problems.append(f"host HP fell {decrease}, recorded successes sum to {total}")
    for who, rows, lo, hi, mine in (("host", host_hp, h0, h1, ours), ("friend", friend_hp, f0, f1, friend_ours)):
        window = [r for r in rows if lo <= r[0] < hi]
        strangers = sorted({r[1:4] for r in window if not mine(r)})
        if strangers:
            problems.append(f"{who} boss samples in the window from another lifetime: {strangers}")
        off = sorted({r[4] for r in window} - nodes)
        if off:
            problems.append(f"{who} boss HP samples {off} in the window are not on the host chain")
    friend_after = [r for r in friend_hp if r[0] >= f1]
    friend_settled = friend_after[-1][4] if friend_after else None
    if not friend_after or not friend_ours(friend_after[-1]) or friend_settled != settled:
        problems.append(f"settled boss: host {settled} on {(epoch, net_id, boss_actor)}, friend "
                        f"{friend_after[-1][4:] + friend_after[-1][1:4] if friend_after else None}")

    # The friend's copy never changes its own HP: its hits are checked claim-then-zeroes, one per sent claim,
    # and the host's puppet hits on it are checked zeroes.
    friend_events = hit_events(friend, f0, len(friend))
    friend_on_boss = [e for e in friend_events if e["victimObject"] in (objectId, None)]
    friend_claimed, friend_refusals = [], []
    for e in friend_on_boss:
        why = None
        if e["invalid"]:
            why = f"is incomplete proof: {e['invalid']}"
        elif (e["victimActor"], e["epoch"]) != (friend_actor, epoch):
            why = "is not on the bound friend copy"
        elif e["hpBefore"] != e["hpAfter"]:
            problems.append(f"friend copy HP changed locally {e['hpBefore']}->{e['hpAfter']} (seq {e['seq']}, source class {e['sourceClass']})")
        elif e["sourceClass"] == LOCAL_AVATAR and e["amount"] == 0:
            friend_refusals.append(e)
        elif e["sourceClass"] == LOCAL_AVATAR:
            if e["action"] != CLAIM_THEN_ZERO or not e["claimQueued"] or not checked_zero(e):
                why = "is not a queued, checked claim-then-zero"
            else:
                friend_claimed.append(e)
        elif e["sourceClass"] in REMOTE_CLASSES and (e["action"] != ZERO_HP or not checked_zero(e)):
            why = "from the host's puppet is not a checked zero"
        if why:
            problems.append(f"friend local hit seq {e['seq']} {why}")
    unmatched = list(friend_claimed)
    for c in (c for c in sent if f0 <= c["line"]):
        hit = first(unmatched, lambda e: (e["attackId"], e["amount"]) == (c.get("attackId"), c.get("damage")))
        if hit:
            unmatched.remove(hit)
        else:
            problems.append(f"client claim seq {c.get('seq')} has no checked claim-then-zero local hit")
    for e in unmatched:
        problems.append(f"friend claim-then-zero seq {e['seq']} sent no client claim")

    # Each seat's canonical player takes a native boss hit: a valid scope whose source is that seat's copy of
    # the pinned boss (host actor / bound friend actor) in the boss lifetime's epoch.
    def boss_hits(events, boss):
        return [e for e in events if not e["invalid"] and (e["sourceObject"], e["sourceActor"], e["epoch"]) ==
                (objectId, boss, epoch) and e["victimClass"] == LOCAL_AVATAR and e["action"] == NATIVE and
                e["hpBefore"] > e["hpAfter"]]
    host_taken = boss_hits(hit_events(host, h0, h1), boss_actor)
    friend_taken = boss_hits(hit_events(friend, f0, f1), friend_actor)
    # VUH-2912: the host boss's hits on the host's copy of a remote player (classes 2/3) must leave it alone: each
    # is a checked zero (RemoteVictim). Its owner takes that attack natively from its own mirrored copy instead.
    # An incomplete scope whose source is the boss cannot prove that, so it fails (Ione 24860051).
    clone_hits = [e for e in hit_events(host, h0, h1) if e["victimClass"] in REMOTE_CLASSES and
                  (e["sourceObject"] == objectId or e["sourceActor"] == boss_actor)]
    for e in clone_hits:
        if e["invalid"]:
            problems.append(f"host boss hit seq {e['seq']} on a remote player's copy has incomplete proof: {e['invalid']}")
        elif (e["sourceObject"], e["sourceActor"], e["epoch"]) != (objectId, boss_actor, epoch):
            problems.append(f"host boss hit seq {e['seq']} on a remote player's copy is not from the boss lifetime")
    clone_zeroed = [e for e in clone_hits if not e["invalid"] and
                    (e["sourceObject"], e["sourceActor"], e["epoch"]) == (objectId, boss_actor, epoch)]
    for e in clone_zeroed:
        if e["action"] != ZERO_HP or not checked_zero(e) or e["hpBefore"] != e["hpAfter"]:
            problems.append(f"host boss hit seq {e['seq']} on a remote player's copy was not a checked zero: action "
                            f"{e['action']} zero {e['zeroResult']} HP {e['hpBefore']}->{e['hpAfter']}")
    # Required per phase where both seats must contribute; a downed solo seat cannot be hit. Without phases, per window.
    if not marks.get("phases") and (not host_taken or not friend_taken):
        problems.append(f"native boss hits on the canonical players: host {len(host_taken)}, friend {len(friend_taken)}")

    # One brain: the bound friend copy is taken over before the window and skipped through it.
    mirror = [r.groupdict() | {"line": n} for n, line in enumerate(friend) for r in [MIRROR.search(line)]
              if r and int(r["objectId"]) == objectId]
    same = lambda r: (int(r["netId"]), r["actor"]) == (net_id, friend_actor)
    takeover = [r for r in mirror if r["what"] == "take-over" and r["line"] < f0]
    in_window = [r for r in mirror if f0 <= r["line"] < f1]
    bad = [r for r in in_window if not same(r) or r["what"] == "retire" or r["running"] != "1" or
           r["updates"] != r["skips"] or r["noBrain"] != "0"]
    if not takeover or not same(takeover[-1]) or not in_window or bad:
        problems.append(f"friend brain not skipped throughout: takeover={takeover[-1:] and same(takeover[-1])} "
                        f"samples={len(in_window)} bad={bad[:2]}")
    loss = [s.groupdict() for text in (host[h0:h1], friend[f0:f1]) for line in text for s in [SUMMARY.search(line)] if s]
    if any(int(s["dropped"]) or int(s["overflow"]) for s in loss):  # cumulative counters
        problems.append("hit trace dropped or overflowed events in the window")

    # Phases: who may and must deal damage. A friend hit belongs to the phase its claim was sent in.
    phases = []
    for p in marks.get("phases", []):
        (ph0, pf0), (ph1, pf1) = m[p["from"]], m[p["to"]]
        hits = [s for s in steps if (ph0 <= s["line"] < ph1 if s["kind"] == "host" else
                                     s["friendLine"] is not None and pf0 <= s["friendLine"] < pf1)]
        row = {"name": p["name"], "host": sum(s["kind"] == "host" for s in hits), "friend": sum(s["kind"] == "friend" for s in hits),
               "decrease": sum(s["before"] - s["after"] for s in hits),
               "bossHitsOnHost": sum(ph0 <= e["line"] < ph1 for e in host_taken),
               "bossHitsOnFriend": sum(pf0 <= e["line"] < pf1 for e in friend_taken),
               "bossHitsOnClone": sum(ph0 <= e["line"] < ph1 for e in clone_zeroed)}
        # VUH-2912 targeting phases: the seat the host boss chose must take its native hit in the phase.
        for seat, key in (("host", "bossHitsOnHost"), ("friend", "bossHitsOnFriend")):
            if p.get(f"hitOn{seat.title()}") and row[key] == 0:
                problems.append(f"phase {p['name']}: no native boss hit on the {seat}'s canonical player")
        # While the boss targets the friend, the host's copy of the friend must take (and zero) at least one hit.
        if p.get("cloneZero") and row["bossHitsOnClone"] == 0:
            problems.append(f"phase {p['name']}: no complete checked zero of a boss hit on the host's copy of the friend")
        for seat in ("host", "friend"):
            want = p.get(seat)
            if want is True and row[seat] == 0 or want is False and row[seat] != 0:
                problems.append(f"phase {p['name']}: {row[seat]} successful {seat} hits (want {'some' if want else 'none'})")
        if p.get("host") is True and p.get("friend") is True and not (row["bossHitsOnHost"] and row["bossHitsOnFriend"]):
            problems.append(f"phase {p['name']}: native boss hits on the canonical players: host {row['bossHitsOnHost']}, "
                            f"friend {row['bossHitsOnFriend']} (want both)")
        phases.append(row)

    # When each seat got there and first did or took damage (tick of the next role statehash).
    def stamps(lines, attacks, taken):
        out = {}
        program = str(marks.get("program", 87))  # the encounter's btl (warp) and evt (intro hold/release)
        for name, pattern in MILESTONES.items():
            n = first([n for n, line in enumerate(lines) for r in [pattern.search(line)]
                       if r and (r.groupdict().get("btl") or r.groupdict().get("evt")) == program])
            if n is not None:
                out[name] = {"line": n, "tickMs": tick_after(lines, n)}
        if attacks:
            out["firstBossDamage"] = {"line": attacks[0], "tickMs": tick_after(lines, attacks[0])}
        if taken:
            out["firstBossHitTaken"] = {"line": taken[0]["line"], "tickMs": tick_after(lines, taken[0]["line"])}
        return out
    friend_attack_lines = sorted(s["friendLine"] for s in joined if s.get("friendLine") is not None)
    timing = {"host": stamps(host, sorted(e["line"] for e in native_hits), host_taken),
              "friend": stamps(friend, friend_attack_lines, friend_taken)}

    max_seen = sorted({e["victimMaxHp"] for e in on_boss if not e["invalid"] and e["victimActor"] == boss_actor})
    if scale and any(str(v) != scale["max1"] for v in max_seen):
        problems.append(f"boss max HP {max_seen} on its hits is not the scale's {scale['max1']}")

    return {
        "pass": not problems and not unexplained,
        "problems": problems + [f"unexplained host boss HP change: {u['why']} (seq {u['seq']})" for u in unexplained],
        "objectId": objectId, "bossActor": boss_actor,
        "boss": {"epoch": epoch, "netId": net_id, "hostActor": boss_actor, "friendActor": friend_actor},
        "scale": scale, "roomScale": room,
        "hp": {"start": start_hp, "settled": settled, "friendSettled": friend_settled,
               "decrease": decrease, "sum": total},
        "counts": {"hostNativeHits": len(native_hits), "friendClaimsApplied": len(joined), "hostRefusals": len(refusals),
                   "claimRefusals": len(claim_refusals), "puppetZeroed": len(puppet), "claimRejects": len(rejects),
                   "friendClaimsUnapplied": len(unapplied), "friendLocalClaimZeros": len(friend_claimed),
                   "friendLocalRefusals": len(friend_refusals),
                   "bossHitsOnHost": len(host_taken), "bossHitsOnFriend": len(friend_taken),
                   "bossHitsOnClone": len(clone_zeroed),
                   "brainSamples": len(in_window)},
        "phases": phases, "timing": timing, "binding": authority,
        "chain": steps, "refusals": refusals + claim_refusals + friend_refusals, "puppet": puppet, "rejects": rejects,
        "unapplied": unapplied, "unexplained": unexplained,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--host", required=True, type=Path)
    ap.add_argument("--friend", required=True, type=Path)
    ap.add_argument("--object", required=True, type=int)
    ap.add_argument("--marks", required=True, type=Path)
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    read = lambda p: p.read_text(encoding="utf-8-sig", errors="replace")
    result = ledger(read(a.host), read(a.friend), a.object, json.loads(read(a.marks)))
    text = json.dumps(result, indent=2)
    if a.out:
        a.out.write_text(text, encoding="utf-8")
    print(json.dumps({k: result[k] for k in ("pass", "problems", "hp", "counts", "phases")}, indent=2))
    return 0 if result["pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
