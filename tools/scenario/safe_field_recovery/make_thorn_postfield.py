"""Derive the VUH-2640 post-Thorn field recovery check from net_twilight_thorn_bots05.json (offline, deterministic).

Lead #33/#34 (option A): both seats fight Thorn with bots05's reviewed pilot (route_pilot/thorn.py) and bots05's
whole run is kept, through the victory, the first settled post-Thorn field and both seats' onward movement. Only
then, at that field with no pilot running, the original Friend1 runtime is paused for the qualified 4200 ms
fixture (pause.py). The expiry is outside any boss fight, so the friend never sits out (`outage expiry boss=0`):
it takes VUH-2628's recovery at that same field (host capture, converged Bootstrap, fresh lifetime, reset-1 ACK
and Arm; recovery_proof), with its HP unchanged, and both seats move on again. KH2COOP_RESYNC_CATALOG_TRACE=1
records that field's phase=field catalog for VUH-2674. True Leg B (the 25% return after an in-fight F1 expiry)
is a follow-up on VUH-2640. Run it through this directory's thin entry (run.py), which registers the pause step.

    python -B tools/scenario/safe_field_recovery/make_thorn_postfield.py          # rewrite the scenario
    python -B tools/scenario/safe_field_recovery/make_thorn_postfield.py --check  # verify it is current
"""
import copy
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "tools/scenario/scenarios/net_twilight_thorn_bots05.json"
TARGET = ROOT / "tools/scenario/scenarios/net_twilight_thorn_postfield.json"
PILOTS_IDLE = "not saved.get('_background_pilots',[])"
FIELD = ("peek('0x9BA8D0','u8',{i})==1 and peek('0x7435D0','u8',{i})==255 and peek('0xB65210','i32',{i})==0 and "
         "int(str(peek('0x2A11478','u64',{i})),16)==0")
VICTORY = "[mission-mirror] host end trap=mission_dead_boss"
FRIEND_HP = "[a for a in actors(1) if int(a['address'],16)==int(peek('0x2A105D0','u64',1),16)][0]['hp']"
RENAMES = (("bot2617_host_onward", "postfield_host_onward"), ("bot2617_friend_onward", "postfield_friend_onward"),
           ("host_onward_verified_", "postfield_host_onward_"), ("friend_onward_verified_", "postfield_friend_onward_"))


def renamed(step: dict) -> dict:
    text = json.dumps(step)
    for old, new in RENAMES:
        text = text.replace(old, new)
    return json.loads(text)


def build(bots05: dict) -> dict:
    steps = bots05["steps"]
    # bots05 anchors (checked so a changed source fails loudly rather than drifting).
    assert steps[30]["name"] == "host_thorn" and steps[31]["name"] == "friend_thorn"
    assert steps[77]["do"] == "wait_until" and steps[79] == {"do": "save", "as": "bot2617_onward_scope", "expr": "location(0)"}
    assert steps[81]["as"] == "bot2617_host_onward_before" and steps[97]["name"] == "friend_onward_verified_friend"
    assert len(steps) == 100 and steps[99]["expr"] == "saved['thorn_native_attacks']"
    out = []
    for i in (0, 1):
        boot = copy.deepcopy(steps[i])
        boot["env"]["KH2COOP_RESYNC_CATALOG_TRACE"] = "1"   # VUH-2674 field rows, read-only
        out.append(boot)
    out += copy.deepcopy(steps[2:30])
    # The arena load before either fight pilot starts: the post-victory field's catalog block must be newer.
    out.append({"do": "save", "as": "postfield_arena_load", "expr": "last_load(0)"})
    out += copy.deepcopy(steps[30:100])                       # bots05's fight, victory, field and onward checks
    out += [
        {"do": "save", "as": "postfield_before", "expr":
            "{'friend_aborts':runtime_log(1).count('[eventhold-control] aborted reason=4'),"
            "'friend_restored':runtime_log(1).count('[eventhold-recovery] control restored'),"
            "'friend_expiry_boss0':log_count('[downed] outage expiry boss=0',1),"
            "'friend_outage':log_count('[downed] outage sit-out',1)+log_count('[downed] outage arrival',1),"
            f"'friend_hp':{FRIEND_HP},'location':location(0)}}"},
        {"do": "assert", "expr":
            f"location(0)==location(1)==saved['bot2617_onward_scope'] and {FIELD.format(i=0)} and {FIELD.format(i=1)} "
            f"and {PILOTS_IDLE}",
         "message": "Pause at the first settled post-Thorn field, both seats there, with no owned pilot running"},
        {"do": "wait_until", "expr": f"field_catalog(0, saved['postfield_arena_load'], '{VICTORY}') is not None",
         "timeoutMs": 30000, "pollMs": 1000},
        {"do": "save", "as": "vuh2674_field", "expr":
            f"{{'catalog':field_catalog(0, saved['postfield_arena_load'], '{VICTORY}'),'location':location(0)}}"},
        {"do": "friend_heartbeat_pause", "instance": 1, "pauseMs": 4200},
        {"do": "wait_until", "expr": "recovery_proof()['ready']", "timeoutMs": 120000, "pollMs": 1000},
        {"do": "save", "as": "heartbeat_recovery", "expr":
            "{'proof':recovery_proof(),"
            "'host_capture':log_matches(r'\\[resync-capture-refused\\] stage=(?P<stage>\\S+)',0),"
            "'host_returning':[l for l in runtime_log(0).splitlines() if '[returning-seat-resync]' in l][-8:],"
            "'friend_outage':[m['line'] for m in log_matches(r'(?P<line>\\[downed\\] outage [^\\n]*)',1)][-6:]}"},
        {"do": "assert", "expr":
            "log_count('[downed] outage expiry boss=0',1)==saved['postfield_before']['friend_expiry_boss0']+1 and "
            "log_count('[downed] outage sit-out',1)+log_count('[downed] outage arrival',1)==saved['postfield_before']['friend_outage']",
         "message": "An expiry outside the boss fight never sits out: one 'outage expiry boss=0', no outage sit-out or arrival"},
        {"do": "assert", "expr":
            f"location(0)==location(1)==saved['bot2617_onward_scope']==saved['heartbeat_recovery']['proof'].get('location') and "
            f"{FRIEND_HP}==saved['postfield_before']['friend_hp']",
         "message": "VUH-2628 recovery at the same post-Thorn field: both seats there, the friend's HP unchanged"},
        {"do": "capture", "instance": 0, "name": "postfield_recovered_host"},
        {"do": "capture", "instance": 1, "name": "postfield_recovered_friend"},
    ]
    out += [renamed(copy.deepcopy(steps[i])) for i in range(81, 98)]  # both seats move on again, after the recovery
    out += [
        {"do": "assert", "expr": "pause_relay()['ok']",
         "message": "Lease-only expiry (Quill): the relay must not have timed out the friend after the pause; see heartbeat_relay.json"},
    ]
    return {
        "name": "vuh2640_thorn_postfield_recovery",
        "description": (
            "VUH-2640 post-Thorn field recovery (option A, single-PC loopback), derived offline from "
            "net_twilight_thorn_bots05 by tools/scenario/safe_field_recovery/make_thorn_postfield.py; run through that "
            "directory's run.py thin entry. bots05's whole two-seat run (Thorn kill, victory, first settled post-Thorn "
            "field, onward movement) is kept. Then, at that field with no pilot running, the original Friend1 runtime is "
            "paused 4200 ms (pause.py budget). Outside a boss fight the friend never sits out; it takes VUH-2628's "
            "recovery at that same field with its HP unchanged, and both seats move on again. "
            "KH2COOP_RESYNC_CATALOG_TRACE=1 records that field's phase=field catalog for VUH-2674. True Leg B (the 25% "
            "return after a late-fight F1 expiry) is a follow-up."),
        "playerDefaults": bots05["playerDefaults"],
        "protect": bots05["protect"],
        "qualificationGaps": [
            "Needs a co-op Thorn kill first (bots05's acceptance, unproven with the r5 pilot).",
            "The host's capture at the first post-Thorn field may be refused (VUH-2674's reviewed row); then the "
            "recovery waits past the 120 s bound and the refusals are in heartbeat_recovery's host_capture.",
            "The 4200 ms pause is a bounded experiment, not a lease-before-relay guarantee (pre-pause relay age is assumed).",
            "Not Leg B: no outage sit-out, so no 25% return (VUH-2640 follow-up: a late-fight F1 expiry).",
        ],
        "steps": out,
    }


def main() -> int:
    text = json.dumps(build(json.loads(SOURCE.read_text(encoding="utf-8"))), indent=2) + "\n"
    if "--check" in sys.argv[1:]:
        current = TARGET.read_text(encoding="utf-8") if TARGET.exists() else ""
        ok = current.replace("\r\n", "\n") == text
        print(json.dumps({"ok": ok, "target": str(TARGET.relative_to(ROOT))}))
        return 0 if ok else 1
    TARGET.write_text(text, encoding="utf-8", newline="\n")
    print(json.dumps({"written": str(TARGET.relative_to(ROOT)), "steps": len(json.loads(text)["steps"])}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
