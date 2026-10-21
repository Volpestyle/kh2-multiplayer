"""Pure log correlation for the single forced heartbeat-expiry fixture."""
import re


def rows(text, marker):
    result = []
    for number, line in enumerate(text.splitlines(), 1):
        if marker in line:
            row = dict(re.findall(r"(\w+)=([^\s]+)", line))
            row.update(_line=number, _text=line)
            result.append(row)
    return result


def baseline(friend):
    arms = rows(friend, "[eventhold-control] armed ")
    if not arms or rows(friend, "[eventhold-control] aborted "):
        raise ValueError("initial clean armed lifetime required")
    arm = arms[-1]
    identities = [r for r in rows(friend, "[runtime-identity]")
                  if r.get("identityCurrent") == "1" and r.get("pinPresent") == "1"
                  and r.get("stringsComplete") == "1" and r.get("slot") == "1"
                  and r.get("selfConnection") == arm["self"]
                  and r.get("hostConnection") == arm["host"]
                  and r.get("pinSession") == r.get("session")
                  and r.get("pinHostConnection") == arm["host"] and r.get("pinSlot") == "1"]
    if not identities:
        raise ValueError("initial admitted identity and matching retained pin required")
    identity = identities[-1]
    if not re.fullmatch(r"[0-9a-f]{32}", identity["session"]):
        raise ValueError("full session identity required")
    return {"arm": arm, "identity": identity}


def correlate(old, pause, friend, host, native, relay):
    """All arguments after old/pause are post-pause boundary log tails."""
    try:
        if not pause.get("completed") or not pause.get("resumed") or pause.get("terminal"):
            raise ValueError("successful retained-handle resume required")
        initial = old["arm"]
        session = old["identity"]["session"]
        aborts = rows(friend, "[eventhold-control] aborted ")
        expired = [r for r in aborts if r.get("reason") == "4" and
                   pause["suspendTickMs"] <= int(r["observationMs"]) <= pause["resumeTickMs"] + 3000]
        if not expired:
            raise ValueError("reason4 not observed by resume+3000ms")
        rejoining = [r for r in rows(friend, "[eventhold-recovery] expired connection=")
                     if r.get("connection") == initial["self"] and r.get("pinnedRejoin") == "1"
                     and r.get("quarantined") == "1"]
        if len(rejoining) != 1:
            raise ValueError("one original expired connection must enter pinned rejoin/quarantine")
        armed = [r for r in rows(friend, "[eventhold-control] armed ")
                 if int(r["generation"]) > int(initial["generation"]) and r["host"] == initial["host"]
                 and r["self"] != initial["self"]]
        if len(armed) != 1:
            raise ValueError("one fresh armed incarnation required")
        arm = armed[0]
        if any(r["_line"] > arm["_line"] for r in aborts):
            raise ValueError("new lifetime aborted")
        pin = [r for r in rows(friend, "[runtime-identity]") if r.get("pinPresent") == "1"
               and r.get("stringsComplete") == "1" and r.get("identityCurrent") == "1"
               and r.get("session") == session and r.get("pinSession") == session
               and r.get("hostConnection") == arm["host"] and r.get("pinHostConnection") == arm["host"]
               and r.get("selfConnection") == arm["self"] and r.get("pinSlot") == "1"]
        if not pin:
            raise ValueError("new connection must retain original session/host pin")
        submits = [r for r in rows(host, "[returning-seat-resync]")
                   if r.get("action") == "submitted" and r.get("reason") == "host-generated-request"
                   and r.get("slot") == "1" and r.get("connection") == arm["self"]
                   and int(r.get("request", "0")) > 0]
        if len(submits) != 1:
            raise ValueError("one host returning-seat request required")
        request = submits[0]["request"]
        acks = [r for r in rows(native, "[resync] ack ") if r.get("request") == request
                and r.get("session") == session and r.get("host") == arm["host"]
                and r.get("targetSlot") == "1" and r.get("connection") == arm["self"]
                and r.get("delivery") == arm["targetDelivery"] and r.get("phase") in ("0", "1")
                and r.get("status") == "2" and r.get("serialized") == "1"
                and re.fullmatch(r"[0-9a-f]{64}", r.get("fingerprint", ""))
                and int(r["frame2"]) > int(r["frame1"]) > 0]
        bootstraps = [r for r in acks if r["phase"] == "0" and int(r["loadAfter"]) > int(r["loadBefore"])]
        if len(bootstraps) != 1:
            raise ValueError("one actual changed-load two-frame native Bootstrap convergence required")
        bootstrap = bootstraps[0]
        results = [r for r in rows(relay, "[resync-result]") if r.get("component") == "relay"
                   and r.get("session") == session and r.get("host") == arm["host"]
                   and r.get("request") == request and r.get("reason") == "0"
                   and r.get("targetCount") == "1"]
        if len(results) != 1:
            raise ValueError("matching relay Converged target/cut/fingerprint required")
        terminal = [r for r in acks if results[0].get("target0") == ",".join(
            ("1", arm["self"], arm["targetDelivery"], "2", r["cut"], r["fingerprint"], "-"))]
        if len(terminal) != 1:
            raise ValueError("relay result must exactly match one actual native terminal ACK")
        ack = terminal[0]
        if submits[0].get("epoch") != ack["epoch"]:
            raise ValueError("host returning-seat request and native epoch differ")
        fields = ("epoch", "world", "room", "door", "map", "battle", "event", "loadAfter")
        if any(bootstrap[k] != ack[k] for k in fields) or int(ack["cut"]) < int(bootstrap["cut"]):
            raise ValueError("terminal snapshot drifted from the actual Bootstrap load/room")
        native_lifetimes = [r for r in rows(native, "[event-control] recovered lifetime ")
                            if r.get("generation") == arm["generation"] and r.get("self") == arm["self"]
                            and r.get("delivery") == arm["targetDelivery"] and r.get("epoch") == ack["epoch"]
                            and r.get("resetAckRequired") == "1"]
        reset_acks = [r for r in rows(native, "[event-control] owner ack ")
                      if r.get("ordinal") == "1" and r.get("source") == "0" and r.get("flags") == "3"
                      and r.get("generation") == arm["generation"] and r.get("delivery") == arm["targetDelivery"]
                      and r.get("epoch") == ack["epoch"]]
        consumed = [r for r in rows(native, "[event-control] resync result ")
                    if r.get("request") == request and r.get("reason") == "0"
                    and r.get("recoveredLifetime") == "1"]
        baselines = [r for r in rows(friend, "[eventhold-recovery] fresh baseline ")
                     if r.get("request") == request and r.get("generation") == arm["generation"]
                     and r.get("self") == arm["self"] and r.get("epoch") == ack["epoch"]
                     and r.get("resetAckRequired") == "1"]
        restored = [r for r in rows(friend, "[eventhold-recovery] control restored ")
                    if r.get("generation") == arm["generation"] and r.get("self") == arm["self"]
                    and r.get("nativeResetAck") == "1"]
        if not native_lifetimes or not reset_acks or not baselines or not restored or not consumed:
            raise ValueError("fresh mapping/baseline/reset1 ACK/restored receipts required")
        if not expired[0]["_line"] < rejoining[0]["_line"] < baselines[0]["_line"] < arm["_line"] < restored[0]["_line"]:
            raise ValueError("friend lifecycle ordering mismatch")
        if not ack["_line"] < native_lifetimes[0]["_line"] < consumed[0]["_line"] < reset_acks[0]["_line"]:
            raise ValueError("reset ACK preceded native lifetime")
        location = [int(ack[k]) for k in ("world", "room", "door", "map", "battle", "event")]
        return {"ready": True, "session": session, "request": request, "scope": arm,
                "location": location, "initial": old, "expiry": expired[0], "pinnedRejoin": rejoining[0], "newPin": pin[-1],
                "hostSubmit": submits[0], "bootstrapAck": bootstrap, "nativeAck": ack, "relayResult": results[0],
                "nativeLifetime": native_lifetimes[0], "nativeResultConsumed": consumed[0], "resetAck": reset_acks[0],
                "runtimeBaseline": baselines[0], "runtimeRestored": restored[0],
                "lineReferenceBasis": "post-pause-boundary tail, line1; boundaries retained in pause receipt"}
    except (ValueError, KeyError, TypeError) as error:
        return {"ready": False, "missing": str(error)}
