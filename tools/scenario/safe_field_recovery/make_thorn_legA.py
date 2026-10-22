"""Derive the VUH-2640 Leg A live check from net_twilight_thorn_bots05.json (offline, deterministic).

The bots05 steps through the Calling and its menu tutorials are kept verbatim. Then the original
Friend1 runtime is paused for the qualified 4200 ms fixture (pause.py), with no friend pilot bound, so
the friend's lifetime expires before Thorn. Only the host continues: Leg A must carry the fenced friend
into the arena sitting out, the host clears Thorn alone, and the friend receives no 4/16, 315 or
restored control. KH2COOP_RESYNC_CATALOG_TRACE=1 also records the first post-Thorn field's catalog
(VUH-2674). Run it through this directory's thin entry (run.py), which registers the pause step.

    python -B tools/scenario/safe_field_recovery/make_thorn_legA.py          # rewrite the scenario
    python -B tools/scenario/safe_field_recovery/make_thorn_legA.py --check  # verify it is current
"""
import copy
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SOURCE = ROOT / "tools/scenario/scenarios/net_twilight_thorn_bots05.json"
TARGET = ROOT / "tools/scenario/scenarios/net_twilight_thorn_legA.json"
ARENA = [2, 34, 0, 157, 157, 157]
CALLING = [2, 33, 0, 0, 1, 0]
PILOTS_IDLE = "not saved.get('_background_pilots',[])"
PILOT_ALIVE = ("all(p[0].poll() in (None,0) or p[3].get('allowStuck',False) "
               "for p in saved.get('_background_pilots',[]))")
FIELD = ("peek('0x9BA8D0','u8',{i})==1 and peek('0x7435D0','u8',{i})==255 and peek('0xB65210','i32',{i})==0")


def build(bots05: dict) -> dict:
    steps = bots05["steps"]
    # bots05 anchors (checked so a changed source fails loudly rather than drifting).
    assert steps[17] == {"do": "pilot_wait"} and steps[21]["do"] == "assert" and steps[22]["name"] == "host_to_awakening"
    assert steps[30]["name"] == "host_thorn" and steps[32]["do"] == "fight" and steps[32]["as"] == "thorn_00"
    out = []
    for i in (0, 1):
        boot = copy.deepcopy(steps[i])
        boot["env"]["KH2COOP_RESYNC_CATALOG_TRACE"] = "1"   # VUH-2674 field rows, read-only
        out.append(boot)
    out += copy.deepcopy(steps[2:22])                         # through the Calling and both menu tutorials
    out += [
        {"do": "save", "as": "legA_before", "expr":
            "{'friend_aborts':runtime_log(1).count('[eventhold-control] aborted reason=4'),"
            "'friend_restored':runtime_log(1).count('[eventhold-recovery] control restored'),"
            "'host_pending':runtime_log(0).count('[returning-seat-resync] action=pending reason=same-peer-new-connection'),"
            "'friend_end':log_count('[mission-mirror] client boss end',1),"
            "'friend_315':log_count('353133',1)}"},
        {"do": "assert", "expr": f"location(0)=={CALLING} and location(1)=={CALLING} and {PILOTS_IDLE}",
         "message": "Pause in the Calling, after its pilots: the friend's game has no other owned writer"},
        {"do": "friend_heartbeat_pause", "instance": 1, "pauseMs": 4200},
        {"do": "wait_until", "expr":
            "runtime_log(1).count('[eventhold-control] aborted reason=4')>saved['legA_before']['friend_aborts'] and "
            "'[eventhold-recovery] expired connection=' in runtime_log(1) and "
            "runtime_log(0).count('[returning-seat-resync] action=pending reason=same-peer-new-connection')>saved['legA_before']['host_pending']",
         "timeoutMs": 60000, "pollMs": 500},
        {"do": "wait", "ms": 8000},
        {"do": "save", "as": "legA_calling", "expr":
            "{'host_refusals':log_matches(r'\\[resync-capture-refused\\] stage=(?P<stage>\\S+)',0),"
            "'host_returning':[l for l in runtime_log(0).splitlines() if '[returning-seat-resync]' in l][-8:],"
            "'friend_recovery':[l for l in runtime_log(1).splitlines() if '[eventhold-recovery]' in l][-8:],"
            "'locations':[location(0),location(1)]}"},
        copy.deepcopy(steps[22]),                             # host_to_awakening: the host alone
        {"do": "wait_until", "expr": f"location(0)=={ARENA} and any(a.get('objectId')==307 and a.get('objectType')==12 and a.get('hp',0)>0 for a in actors(0))",
         "timeoutMs": 120000},
        {"do": "wait_until", "expr":
            f"location(1)=={ARENA} and log_count('[resync-sit-out] landed',1)>0 and "
            "log_count('[downed] outage arrival qualified=1',1)>0",
         "timeoutMs": 120000, "pollMs": 500},
        {"do": "wait_until", "expr": "log_count('[downed] outage sit-out',1)>0", "timeoutMs": 60000, "pollMs": 500},
        {"do": "save", "as": "legA_arrival", "expr":
            "{'host':[m['line'] for m in log_matches(r'(?P<line>\\[resync-sit-out\\] [^\\n]*)',0)],"
            "'friend':[m['line'] for m in log_matches(r'(?P<line>\\[(?:resync-sit-out|load-cause\\] retained-issue)[^\\n]*)',1)],"
            "'friend_outage':[m['line'] for m in log_matches(r'(?P<line>\\[downed\\] outage [^\\n]*)',1)],"
            "'friend_runtime':[l for l in runtime_log(1).splitlines() if 'sit-out presence' in l or '[eventhold-recovery]' in l][-8:],"
            "'locations':[location(0),location(1)],'missions':[mission(0),mission(1)],"
            "'friend_boss':[a for a in actors(1) if a.get('objectId')==307]}"},
        {"do": "assert", "expr":
            "log_count('[resync-sit-out] host arena=1 boss=307',0)>0 and log_count('[resync-sit-out] admitted request=',1)>0 and "
            "runtime_log(1).count('[eventhold-recovery] control restored')==saved['legA_before']['friend_restored']",
         "message": "Leg A: the host attests the sit-out, the fenced friend admits it, and no control is restored"},
        {"do": "capture", "instance": 0, "name": "legA_arrival_host"},
        {"do": "capture", "instance": 1, "name": "legA_arrival_friend"},
        copy.deepcopy(steps[27]),                             # thorn_baseline
    ]
    # Twelve 30-second blocks with the host's pilot only (no revivePeer: the friend's outage is not a revive).
    out += [copy.deepcopy(steps[28]), copy.deepcopy(steps[29])]
    host_pilot = copy.deepcopy(steps[30]); host_pilot.pop("revivePeer", None)
    out.append(host_pilot)
    for n in range(12):
        fight = copy.deepcopy(steps[32]); fight["as"] = f"thorn_{n:02d}"
        out.append(fight)
        if n == 0:
            out.append(copy.deepcopy(steps[33]))              # clip_wait after the first block
        out.append({"do": "assert", "expr": PILOT_ALIVE,
                    "message": "The host's combat pilot failed; the current paired clip block is finalized. See host_thorn."})
    out += [
        {"do": "pilot_wait"},
        {"do": "save", "as": "legA_fight", "expr":
            "{'friend_outage':[m['line'] for m in log_matches(r'(?P<line>\\[downed\\] outage [^\\n]*)',1)],"
            "'held_moved':log_count('[downed] outage held-player-moved',1),"
            "'friend_hold_ended':log_count('[downed] logical hold ended',1),"
            "'host_end':log_count('[mission-mirror] host end trap=mission_dead_boss',0)-saved['thorn_baseline']['host_end'],"
            "'victory':log_count('[event-mirror] host publishes start id=353133 ',0)-saved['thorn_baseline']['victory_start']}"},
        {"do": "assert", "expr":
            "saved['legA_fight']['host_end']==1 and saved['legA_fight']['victory']==1",
            "message": "The host alone clears Thorn: exactly one host mission_dead_boss and one ASCII315 publication"},
        {"do": "assert", "expr":
            "log_count('[mission-mirror] client boss end',1)==saved['legA_before']['friend_end'] and "
            "log_count('353133',1)==saved['legA_before']['friend_315'] and "
            "runtime_log(1).count('[eventhold-recovery] control restored')==saved['legA_before']['friend_restored']",
         "message": "The sitting-out friend never replays 4/16 or 315 and gets no control back"},
        {"do": "capture", "instance": 0, "name": "legA_victory_host"},
        {"do": "capture", "instance": 1, "name": "legA_victory_friend"},
        {"do": "wait_until", "expr":
            f"location(0)[:2]!=[2,34] and {FIELD.format(i=0)} and "
            "len(log_matches(r'\\[resync-catalog\\] schema=1 kind=summary [^\\n]* phase=field ',0))>0",
         "timeoutMs": 300000, "pollMs": 1000},
        {"do": "wait", "ms": 10000},
        {"do": "save", "as": "vuh2674_field", "expr":
            "{'host_location':location(0),'host_loads':[m['line'] for m in log_matches(r'(?P<line>\\[warp\\] load complete [^\\n]*)',0)][-12:],"
            "'catalog':[m['line'] for m in log_matches(r'(?P<line>\\[resync-catalog\\] schema=1 [^\\n]* phase=field [^\\n]*)',0)],"
            "'host_capture':log_matches(r'\\[resync-capture-refused\\] stage=(?P<stage>\\S+)',0),"
            "'host_returning':[l for l in runtime_log(0).splitlines() if '[returning-seat-resync]' in l][-8:],"
            "'friend_location':location(1),'friend_outage':[m['line'] for m in log_matches(r'(?P<line>\\[downed\\] outage [^\\n]*)',1)][-6:]}"},
        {"do": "capture", "instance": 0, "name": "vuh2674_field_host"},
        {"do": "capture", "instance": 1, "name": "legB_friend"},
    ]
    return {
        "name": "vuh2640_thorn_legA_sitout",
        "description": (
            "VUH-2640 Leg A live check (single-PC loopback), derived offline from net_twilight_thorn_bots05 by "
            "tools/scenario/safe_field_recovery/make_thorn_legA.py; run through that directory's run.py thin entry. "
            "The Calling route is bots05's. In the Calling, after its pilots, the original Friend1 runtime is paused "
            "4200 ms (bounded experiment, pause.py budget) so its lifetime expires before Thorn. Only the host "
            "continues: the host attests a boss sit-out at 2/34/157, the fenced friend lands in the arena sitting out "
            "(outage), the host clears Thorn alone, and the friend gets no 4/16, 315 or restored control. "
            "KH2COOP_RESYNC_CATALOG_TRACE=1 records the first post-Thorn field's catalog for VUH-2674. Leg B is "
            "expected NOT to complete yet (no reviewed post-Thorn row): the friend is expected to stay sitting out."),
        "playerDefaults": bots05["playerDefaults"],
        "protect": bots05["protect"],
        "qualificationGaps": [
            "Needs VUH-2640 Leg A (protocol 35) and the VUH-2674 catalog trace on the tested products.",
            "The 4200 ms pause is a bounded experiment, not a lease-before-relay guarantee (pre-pause relay age is assumed).",
            "Pre-ready native safety (between the arena load and DownedSpike's first ready frame) is shown only by footage and the friend's outage/hold rows.",
            "A host clearing Thorn alone is untested; a host death with the friend sitting out is everyone down (Continue).",
            "Leg B after Thorn needs VUH-2674's reviewed row; the friend staying in the arena sitting out is the expected first-slice result.",
            "The atk_thorn attach on the sitting-out friend is traced ([downed] outage held-player-moved), not prevented.",
        ],
        "steps": out,
    }


def main() -> int:
    built = json.dumps(build(json.loads(SOURCE.read_text(encoding="utf-8"))), indent=2) + "\n"
    if "--check" in sys.argv[1:]:
        current = TARGET.read_text(encoding="utf-8") if TARGET.exists() else ""
        ok = current.replace("\r\n", "\n") == built
        print(json.dumps({"ok": ok, "target": str(TARGET.relative_to(ROOT))}))
        return 0 if ok else 1
    TARGET.write_text(built, encoding="utf-8", newline="\n")
    print(json.dumps({"written": str(TARGET.relative_to(ROOT)), "steps": len(json.loads(built)["steps"])}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
