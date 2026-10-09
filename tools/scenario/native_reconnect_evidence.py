"""Pure, fail-closed evidence checks for the three-peer native reconnect fixture.

Inputs are observations, not authority to drive a rig. Runtime/bridge identities
are separately observed; neither implies native bootstrap readiness. Returned
prefix receipts are JSON serializable; callers retain the original log bytes.
"""
from __future__ import annotations

import hashlib
import math
import re


LOCATION = (r"(?:room|target)=(?P<world>[0-9A-Fa-f]+)/(?P<room>[0-9A-Fa-f]+) "
            r"door=(?P<door>\d+) map=(?P<map>\d+) btl=(?P<btl>\d+) evt=(?P<evt>\d+)")
ARRIVAL = re.compile(r"\[enemysync\] (?P<role>host|client) arrived epoch=(?P<epoch>\d+) " + LOCATION)
LOAD = re.compile(r"\[warp\] load complete serial=(?P<serial>\d+) transition=(?P<transition>\d+) " + LOCATION)
QUEUE = re.compile(r"\[warp\] client queued epoch=(?P<epoch>\d+) " + LOCATION)
ISSUED = re.compile(r"\[warp\] client issued epoch=(?P<epoch>\d+) transition=(?P<transition>\d+)")
RESET = re.compile(r"\[enemysync\] session reset: host epoch and pending target cleared")
FULL = re.compile(r"\[progresssync\] client full version=(?P<version>\d+) spans=(?P<spans>\d+) bytes=(?P<bytes>\d+) complete=1")
APPLY = re.compile(r"\[progresssync\] apply version=(?P<version>\d+) spans=\d+ bytes=\d+ hash=[0-9A-Fa-f]+ personal_before=(?P<before>[0-9A-Fa-f]+) personal_after=(?P<after>[0-9A-Fa-f]+) personal_unchanged=1")
LEASE = re.compile(r"\[spawn-authority\] lease epoch=(?P<epoch>\d+) source=(?P<source>\d+) point=\((?P<point>[^)]+)\)")
SPAWN_APPLY = re.compile(r"\[spawnctl\] action=apply [^\r\n]*? records=(?P<records>\d+) transition=(?P<transition>\d+) load=(?P<load>\d+) room=(?P<world>[0-9A-Fa-f]+)/(?P<room>[0-9A-Fa-f]+) [^\r\n]*? apply=(?P<apply>\d+) [^\r\n]*? pointAvailable=1 point=\((?P<point>[^)]+)\)")
LIFECYCLE = re.compile(r"\[warp\] (?:load complete|client issued|client queued)|Warp: [0-9A-Fa-f]{2}/[0-9A-Fa-f]{2} ->|\[enemysync\] session reset:")


def _require(condition, message, problems):
    if not condition:
        problems.append(message)


def _receipt(data):
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def _bytes(value, name, problems):
    if not isinstance(value, bytes) or not value:
        problems.append(f"{name}: missing byte log")
        return b""
    if not value.endswith(b"\n"):
        problems.append(f"{name}: partial final line")
    return value


def _suffix(data, receipt, name, problems):
    count = receipt["bytes"]
    if len(data) < count or _receipt(data[:count]) != receipt:
        problems.append(f"{name}: baseline byte prefix changed or truncated")
        return ""
    return data[count:].decode("utf-8", errors="replace")


def _identities(text):
    result = []
    for line in text.splitlines():
        if "[runtime-identity] " not in line:
            continue
        fields = dict(re.findall(r"(\w+)=([^\s]+)", line.split("[runtime-identity] ", 1)[1]))
        result.append({k: int(v) if v.isdecimal() and k not in ("session", "pinSession", "peerHex") else v
                       for k, v in fields.items()})
    return result


def _location(match):
    return [int(match[k], 16 if k in ("world", "room") else 10)
            for k in ("world", "room", "door", "map", "btl", "evt")]


def _last(pattern, text):
    matches = list(pattern.finditer(text))
    return matches[-1] if matches else None


def _pin(identity, session, host, slot):
    return (identity.get("pinPresent") == 1 and identity.get("pinSession") == session
            and identity.get("pinHostConnection") == host and identity.get("pinSlot") == slot)


def _identity(identity, slot, pid, problems, allow_missing_friend=False):
    label = f"peer {slot} runtime"
    expected = {"schema": 1, "identityCurrent": 1, "stringsComplete": 1,
                "slot": slot, "attachedPid": pid, "generationValid": 1,
                "worldSlot": slot, "authority": 2, "bridgeOpen": 1,
                "quarantine": 0, "admitted": 1, "transportConnected": 1,
                "errors": 0, "atomicBinding": 0, "nativeBootstrapReady": "unverified"}
    for key, value in expected.items():
        _require(identity.get(key) == value, f"{label}: {key} != {value}", problems)
    roster = [identity.get(f"roster{i}") for i in range(3)]
    _require(all(isinstance(v, int) and v > 0 for i, v in enumerate(roster)
                 if not (allow_missing_friend and i == 1)), f"{label}: incomplete roster", problems)
    _require(roster == [identity.get(f"worldRoster{i}") for i in range(3)],
             f"{label}: admitted/world rosters differ", problems)
    _require(identity.get("selfConnection") == roster[slot] and identity.get("hostConnection") == roster[0],
             f"{label}: self/host connection mismatch", problems)
    _require(isinstance(identity.get("generation"), int) and identity["generation"] > 0
             and isinstance(identity.get("delivery"), int) and identity["delivery"] > 0,
             f"{label}: missing generation/delivery observations", problems)
    session = identity.get("session")
    _require(isinstance(session, str) and session not in ("", "-"), f"{label}: missing session", problems)
    _require(_pin(identity, session, roster[0], slot), f"{label}: original pin unavailable/mismatched", problems)
    peer_hex = identity.get("peerHex", "")
    _require(isinstance(peer_hex, str) and re.fullmatch(r"(?:[0-9a-f]{2})+", peer_hex) is not None
             and len(peer_hex) == identity.get("peerBytes", -1) * 2, f"{label}: incomplete peer label", problems)
    return roster


def _pose(pose, owner, location, label, problems):
    _require(pose.get("ownerSlot") == owner and [pose.get("worldId"), pose.get("roomId")] == location[:2],
             f"{label}: wrong owner/room", problems)
    position = pose.get("position", [])
    _require(pose.get("finite") is True and len(position) == 3
             and all(isinstance(v, (float, int)) and math.isfinite(v) for v in position),
             f"{label}: nonfinite/missing pose", problems)
    _require(isinstance(pose.get("seq"), int) and pose["seq"] > 0, f"{label}: unpublished sequence", problems)


def _observation(row, identity, roster, problems):
    slot, pid, location = row["slot"], row["gamePid"], row["location"]
    raw = row.get("avatarObservation", {})
    for key, expected in {"schema": 1, "command": "observe", "processId": pid,
                          "ok": True, "readOnly": True, "atomicAcrossBridges": False,
                          "avatarBridgeVersion": 4, "worldBridgeVersion": 12}.items():
        _require(raw.get(key) == expected, f"peer {slot} observe: invalid {key}", problems)
    samples = raw.get("samples", [])
    _require(len(samples) >= 2, f"peer {slot}: two pose samples required", problems)
    previous = None
    for sample in samples:
        label = f"peer {slot} observe {sample.get('index')}"
        _require(all(sample.get(k) is True for k in ("complete", "identityStable", "worldMappingAvailable", "avatarMappingAvailable", "localReadAvailable")),
                 f"{label}: incomplete bracket/read", problems)
        world = sample.get("worldAfter") or {}
        _require(sample["endMonotonicMs"] >= sample["beginMonotonicMs"], f"{label}: invalid observation interval", problems)
        _require(sample.get("worldBefore") == world, f"{label}: identity changed", problems)
        expected = {"localSlot": slot, "generation": identity.get("generation"),
                    "deliverySerial": identity.get("delivery"), "authorityMode": 2,
                    "connectionIds": roster, "peerDeliverySerials": [identity.get(f"peerFloor{i}") for i in range(3)]}
        _require(all(world.get(k) == v for k, v in expected.items()), f"{label}: runtime/bridge identity mismatch", problems)
        local = sample.get("local") or {}
        # The DLL publishes raw local captures with default ownerSlot=Player;
        # the relay stamps network ownership later. Checked process/worldSlot,
        # not this raw capture field, identifies the local player.
        _pose(local, 0, location, label + " raw local", problems)
        puppets = sample.get("puppets", [])
        _require(len(puppets) == 2, f"{label}: incomplete puppets", problems)
        sequences = [local.get("seq", 0)]
        for index, owner in enumerate(i for i in range(3) if i != slot):
            puppet = puppets[index]
            value = puppet.get("value") or {}
            pose = value.get("pose") or {}
            _require(puppet.get("index") == index and puppet.get("readAvailable") is True
                     and value.get("active") == 1 and value.get("provenanceMatchesWorld") is True,
                     f"{label} puppet {index}: inactive/unreadable/stale", problems)
            provenance = {"producer": 2, "localSlot": slot, "generation": world.get("generation"),
                          "ownerConnectionId": roster[owner], "localConnectionId": roster[slot],
                          "hostConnectionId": roster[0]}
            _require(value.get("provenance") == provenance, f"{label} puppet {index}: wrong network provenance", problems)
            _pose(pose, owner, location, f"{label} puppet {index}", problems)
            sequences.append(pose.get("seq", 0))
        if previous:
            _require(sample["beginMonotonicMs"] >= previous["end"]
                     and sample["endMonotonicMs"] >= sample["beginMonotonicMs"]
                     and all(a > b for a, b in zip(sequences, previous["seqs"])),
                     f"{label}: local/remote poses did not freshly advance", problems)
        previous = {"seqs": sequences, "end": sample["endMonotonicMs"]}
    last = samples[-1]
    native = row.get("nativePuppets", {})
    _require(native.get("room") == location[:2], f"peer {slot}: native actors in wrong room", problems)
    pointers = [int(v, 16) for v in native.get("pointers", [])]
    local_address = int(native.get("localAddress", "0"), 16)
    _require(len(pointers) == 2 and len(set(pointers)) == 2 and all(p and p != local_address for p in pointers),
             f"peer {slot}: native puppet aliases/missing targets", problems)
    errors = []
    for index in range(2):
        actor = native["actors"][index]
        _require(bool(actor) and int(actor["address"], 16) == pointers[index],
                 f"peer {slot}: puppet {index} absent from active native list", problems)
        if actor:
            point = [actor["position"][k] for k in ("x", "y", "z")]
            pose = last["puppets"][index]["value"]["pose"]["position"]
            error = math.dist(point, pose)
            errors.append(error if math.isfinite(error) else None)
            _require(math.isfinite(error) and error <= 100, f"peer {slot}: puppet {index} pose error exceeds 100", problems)
    return {"world": last["worldAfter"], "sequences": previous["seqs"], "puppetErrors": errors}


def _capture(sample):
    result = {"schema": 1, "ready": False, "complete": False, "problems": [], "peers": [],
              "atomicAcrossSources": False, "nativeReadinessFromIdentity": False}
    problems = result["problems"]
    relay = _bytes(sample.get("relayLog"), "relay", problems)
    result["relayLog"] = _receipt(relay)
    rows = sample.get("peers", [])
    _require(len(rows) == 3 and sorted(row.get("slot", -1) for row in rows) == [0, 1, 2],
             "exactly slots 0,1,2 required", problems)
    for row in sorted(rows, key=lambda v: v["slot"]):
        slot = row["slot"]
        problems.extend(f"peer {slot}: {p}" for p in row.get("problems", []))
        _require(row.get("runtimeAlive") is True and row.get("runtimePid", 0) > 0 and row.get("gamePid", 0) > 0,
                 f"peer {slot}: original runtime/game identity unavailable", problems)
        native = _bytes(row.get("nativeLog"), f"peer {slot} native", problems)
        runtime = _bytes(row.get("runtimeLog"), f"peer {slot} runtime", problems)
        nt, rt = native.decode("utf-8", errors="replace"), runtime.decode("utf-8", errors="replace")
        identities = _identities(rt)
        _require(bool(identities) and "[runtime-identity-gap]" not in rt,
                 f"peer {slot}: missing/gapped runtime identity", problems)
        identity = identities[-1] if identities else {}
        roster = _identity(identity, slot, row["gamePid"], problems)
        arrival, load, issued = _last(ARRIVAL, nt), _last(LOAD, nt), _last(ISSUED, nt)
        _require(arrival is not None and load is not None, f"peer {slot}: missing native arrival/load", problems)
        location = row["location"]
        _require(len(location) == 6 and all(type(v) is int for v in location), f"peer {slot}: incomplete location", problems)
        _require(_location(arrival) == location and _location(load) == location and load.start() < arrival.start()
                 and int(arrival["epoch"]) > 0 and arrival["role"] == ("host" if slot == 0 else "client"),
                 f"peer {slot}: latest native load/arrival/location not qualified", problems)
        _require(int(load["serial"]) > 0, f"peer {slot}: native load serial unavailable", problems)
        if slot != 0:
            _require(issued is not None and issued.start() < load.start()
                     and issued["transition"] == load["transition"] and issued["epoch"] == arrival["epoch"],
                     f"peer {slot}: arrival does not match issued native transition", problems)
        _require(not LIFECYCLE.search(nt, arrival.end()), f"peer {slot}: native transition pending after arrival", problems)
        observed = _observation(row, identity, roster, problems)
        data = {"slot": slot, "gamePid": row["gamePid"], "runtimePid": row["runtimePid"],
                "runtimeLog": _receipt(runtime), "nativeLog": _receipt(native), "identity": identity,
                "epoch": int(arrival["epoch"]), "location": location, "loadSerial": int(load["serial"]),
                "transition": int(load["transition"]), "issued": issued.groupdict() if issued else None,
                "observation": observed}
        result["peers"].append(data)
    host = result["peers"][0]
    result.update(session=host["identity"]["session"], roster=[host["identity"][f"roster{i}"] for i in range(3)],
                  epoch=host["epoch"], location=host["location"])
    for peer in result["peers"]:
        _require(peer["identity"]["session"] == result["session"]
                 and [peer["identity"][f"roster{i}"] for i in range(3)] == result["roster"]
                 and peer["epoch"] == result["epoch"] and peer["location"] == result["location"],
                 f"peer {peer['slot']}: session/roster/epoch/location differs", problems)
    _require(len({p["runtimePid"] for p in result["peers"]}) == 3
             and len({p["gamePid"] for p in result["peers"]}) == 3, "duplicate process identities", problems)
    result["ready"] = result["complete"] = not problems
    return result


def _guard(operation):
    try:
        return operation()
    except (KeyError, IndexError, TypeError, ValueError, AttributeError, OverflowError) as error:
        return {"schema": 1, "ready": False, "complete": False, "peers": [],
                "epoch": None, "location": None, "session": None, "roster": [],
                "problems": [f"missing or malformed evidence: {type(error).__name__}: {error}"]}


def capture_baseline(sample):
    """Qualify original native state and seal complete byte-prefix receipts."""
    return _guard(lambda: _capture(sample))


def _native_chain(text, before, epoch, location, problems):
    patterns = (RESET, FULL, QUEUE, APPLY, ISSUED, LOAD, ARRIVAL, LEASE, SPAWN_APPLY)
    names = ("reset", "fullProgress", "queue", "apply", "issued", "load", "arrival", "lease", "spawnApply")
    # Retirement and readmission can each reset; only the final reset's chain is current.
    final_reset = _last(RESET, text)
    cursor, chain = final_reset.start() if final_reset else 0, {}
    for name, pattern in zip(names, patterns):
        match = pattern.search(text, cursor)
        if match is None:
            problems.append(f"Friend1: fresh ordered native chain missing {name}")
            return chain
        chain[name] = {**match.groupdict(), "offset": match.start()}
        cursor = match.end()
    for name in ("queue", "arrival", "lease", "issued"):
        _require(int(chain[name]["epoch"]) == epoch, f"Friend1: {name} epoch changed", problems)
    for name in ("queue", "load", "arrival"):
        _require(_location(chain[name]) == location, f"Friend1: {name} full tuple changed", problems)
    _require(int(chain["load"]["serial"]) > before["loadSerial"]
             and int(chain["issued"]["transition"]) > before["transition"]
             and chain["load"]["transition"] == chain["issued"]["transition"],
             "Friend1: load serial/issued transition did not freshly advance", problems)
    _require(chain["fullProgress"]["version"] == chain["apply"]["version"]
             and int(chain["fullProgress"]["bytes"]) > 0 and int(chain["fullProgress"]["spans"]) > 0
             and chain["apply"]["before"] == chain["apply"]["after"],
             "Friend1: complete progress was not applied before load", problems)
    point = [float(v) for v in chain["lease"]["point"].split(",")]
    _require(len(point) == 4 and all(math.isfinite(v) for v in point) and int(chain["lease"]["source"]) > 0,
             "Friend1: invalid fresh activation lease", problems)
    applied = chain["spawnApply"]
    applied_point = [float(v) for v in applied["point"].split(",")]
    _require(applied["load"] == chain["load"]["serial"] and applied["transition"] == chain["load"]["transition"]
             and [int(applied[k], 16) for k in ("world", "room")] == location[:2]
             and int(applied["records"]) > 0 and int(applied["apply"]) > 0
             and len(applied_point) == 4 and all(math.isfinite(v) for v in applied_point),
             "Friend1: fresh activation application lacks matching native load/room", problems)
    _require(len(list(LOAD.finditer(text))) == 1 and len(list(ISSUED.finditer(text))) == 1
             and len(list(QUEUE.finditer(text))) == 1, "Friend1: extra native reload/transition", problems)
    return chain


def _validate(baseline, sample):
    result = _capture(sample)
    problems = result["problems"]
    _require(baseline.get("ready") is True and baseline.get("complete") is True,
             "baseline was not fully qualified", problems)
    _suffix(sample["relayLog"], baseline["relayLog"], "relay", problems)
    _require(result["session"] == baseline["session"] and result["epoch"] == baseline["epoch"]
             and result["location"] == baseline["location"], "original session/epoch/full tuple changed", problems)
    old, new = baseline["roster"], result["roster"]
    _require(new[0] == old[0] and new[2] == old[2] and new[1] > 0 and new[1] != old[1],
             "roster is not same host/Friend2 plus fresh Friend1", problems)
    raw_rows = {row["slot"]: row for row in sample["peers"]}
    for before, after in zip(baseline["peers"], result["peers"]):
        slot = after["slot"]
        row = raw_rows[slot]
        _require(after["runtimePid"] == before["runtimePid"] and after["gamePid"] == before["gamePid"],
                 f"peer {slot}: original live processes replaced", problems)
        _require(after["identity"]["peerHex"] == before["identity"]["peerHex"], f"peer {slot}: original peer label changed", problems)
        nt = _suffix(row["nativeLog"], before["nativeLog"], f"peer {slot} native", problems)
        rt = _suffix(row["runtimeLog"], before["runtimeLog"], f"peer {slot} runtime", problems)
        _require(after["identity"]["seq"] > before["identity"]["seq"], f"peer {slot}: no fresh identity observation", problems)
        owners = [slot] + [owner for owner in range(3) if owner != slot]
        # Transport avatar sequence restarts on a new connection; provenance and
        # advancement between the two new samples prove freshness for that owner.
        _require(all(a > b for index, (owner, a, b) in enumerate(zip(owners, after["observation"]["sequences"], before["observation"]["sequences"]))
                     if index == 0 or new[owner] == old[owner]),
                 f"peer {slot}: poses not fresh after baseline", problems)
        identities = _identities(rt)
        _require(all(_pin(identity, baseline["session"], old[0], slot) for identity in identities),
                 f"peer {slot}: original pin not retained throughout observed recovery", problems)
        _require(all(identity.get("session") == baseline["session"]
                     and identity.get("hostConnection") == old[0] and identity.get("slot") == slot
                     and identity.get("peerHex") == before["identity"]["peerHex"]
                     for identity in identities if identity.get("admitted") == 1),
                 f"peer {slot}: admitted identity departed original session/host/slot/peer", problems)
        if slot != 1:
            _require(not LIFECYCLE.search(nt) and after["loadSerial"] == before["loadSerial"]
                     and after["transition"] == before["transition"], f"peer {slot}: surviving native process reloaded", problems)
            _require(after["identity"]["generation"] == before["identity"]["generation"], f"peer {slot}: survivor generation changed", problems)
            continue
        _require(after["identity"]["generation"] > before["identity"]["generation"], "Friend1: bridge generation did not advance", problems)
        attempts = list(re.finditer(r"\[Runtime\] Rejoin attempt=(\d+)", rt))
        retry = [identity for identity in identities if identity.get("stage") == "retry-attempted"]
        _require(bool(attempts) and bool(retry) and any(identity.get("recoveryAttempts", 0) == int(attempt[1]) > 0
                 for identity in retry for attempt in attempts), "Friend1: no actual fresh automatic Rejoin attempt", problems)
        _require(any(identity.get("stage") == "retire-after" and identity.get("admitted") == 0
                     for identity in identities), "Friend1: no observed retirement", problems)
        retired = [identity for identity in identities if identity.get("stage") == "retire-after" and identity.get("admitted") == 0]
        _require(any(r["seq"] < t["seq"] < after["identity"]["seq"]
                     and 0 <= after["identity"]["observationMs"] - r["observationMs"] <= 60000
                     for r in retired for t in retry), "Friend1: recovery order/bounded episode unproven", problems)
        _require(not any(identity.get("stage", "").startswith(("resync-", "shutdown-"))
                         or identity.get("stage") in ("bootstrap-generation", "snapshot-delivered")
                         for identity in identities), "Friend1: forced resync/shutdown is not automatic reconnect", problems)
        recovery_cursor = 0
        recovery_patterns = (
            r"\[runtime-identity\] [^\r\n]* stage=retire-after ",
            r"\[Runtime\] Network: closed code=\d+ reason=(?:0|3|6|7)\r?\n",
            r"\[runtime-identity\] [^\r\n]* stage=transport-closed ",
            r"\[Runtime\] Rejoin attempt=[1-5]\r?\n",
            r"\[runtime-identity\] [^\r\n]* stage=retry-attempted ",
            r"\[runtime-identity\] [^\r\n]* stage=transport-connected ",
            r"\[runtime-identity\] [^\r\n]* stage=roster-admitted ")
        for pattern in recovery_patterns:
            match = re.compile(pattern).search(rt, recovery_cursor)
            if match is None:
                problems.append("Friend1: typed close/retirement/retry/transport/admission order incomplete")
                break
            recovery_cursor = match.end()
        result["nativeChain"] = _native_chain(nt, before, baseline["epoch"], baseline["location"], problems)
    result["ready"] = result["complete"] = not problems
    return result


def validate_reconnect(baseline, sample):
    """Require fresh same-process automatic recovery and actual native reload."""
    return _guard(lambda: _validate(baseline, sample))


def retirement_proof(baseline, relay_log, survivor_runtime_logs):
    """Read-only pause gate: relay departure AND both survivors' removed roster."""
    def check():
        problems, peers = [], []
        _require(baseline.get("ready") is True, "unqualified baseline", problems)
        relay = _bytes(relay_log, "relay", problems)
        tail = _suffix(relay, baseline["relayLog"], "relay", problems)
        label = bytes.fromhex(baseline["peers"][1]["identity"]["peerHex"]).decode("utf-8")
        departure = re.search(r"Peer (?:timed out|disconnected): " + re.escape(label) + r"\r?$", tail, re.M)
        _require(departure is not None,
                 "relay has no fresh Friend1 departure", problems)
        roster = baseline["roster"][:]
        roster[1] = 0
        for slot in (0, 2):
            before = baseline["peers"][slot]
            raw = _bytes(survivor_runtime_logs[slot], f"peer {slot} runtime", problems)
            fresh = _identities(_suffix(raw, before["runtimeLog"], f"peer {slot} runtime", problems))
            identity = fresh[-1] if fresh else {}
            current = _identity(identity, slot, before["gamePid"], problems, allow_missing_friend=True)
            _require(current == roster and identity.get("session") == baseline["session"]
                     and identity.get("selfConnection") == roster[slot]
                     and identity.get("peerHex") == before["identity"]["peerHex"]
                     and identity.get("generation") == before["identity"]["generation"]
                     and identity.get("seq", 0) > before["identity"]["seq"],
                     f"peer {slot}: no fresh exact Friend1 retirement roster", problems)
            peers.append(identity)
        return {"ready": not problems, "complete": not problems, "retired": not problems,
                "identityComplete": not problems, "slot": 1,
                "byteOffset": baseline["relayLog"]["bytes"] + (len(tail[:departure.start()].encode("utf-8")) if departure else 0),
                "connectionIdSource": "pre-drop-current-identity+post-drop-survivor-roster",
                "relayPeerId": label, "relayLine": departure[0] if departure else None,
                "connectionId": baseline["roster"][1], "retiredConnectionId": baseline["roster"][1],
                "problems": problems, "peers": peers, "relayLog": _receipt(relay)}
    result = _guard(check)
    result.setdefault("retired", False)
    return result
