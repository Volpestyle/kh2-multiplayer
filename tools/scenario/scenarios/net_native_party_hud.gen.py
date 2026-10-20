"""VUH-2568 single-PC loopback: native owner HP/MP in both directions.

Guest HP/MP changes are explicitly RAM-controlled; host damage/Fire/recharge are native.
The fixture temporarily unlocks Fire in RAM and gives each local status record 10 MP so one
native Fire cast empties its gauge. Restores Fire's RAM byte before closing; never saves.
"""
import json
from pathlib import Path

steps = []
def add(do, **kw):
    steps.append(dict(do=do, **kw))
def cmd(i, expr):
    add("kh2ctl", instance=i, argsExpr=expr)
def own(i, offset=0):
    return f"peek_at(saved['status'][{i}]+{offset},'i32',{i})"
def hud(i, offset=0):
    return f"peek_at(saved['hud_status'][{i}]+{offset},'i32',{i})"
def bindings():
    add("save", **{"as":"local", "expr":"[int(peek(0x2A105D0,'u64',i),16) for i in range(2)]"})
    add("save", **{"as":"status", "expr":"[int(peek_at(saved['local'][i]+0x5C0,'u64',i),16) for i in range(2)]"})
    add("save", **{"as":"gauges", "expr":"[int(peek(0xABD250,'u64',i),16) for i in range(2)]"})
    add("save", **{"as":"views", "expr":"[int(peek_at(saved['gauges'][i]+0x40,'u64',i),16) for i in range(2)]"})
    add("assert", expr="all(saved['views'][i] and saved['views'][i]!=saved['local'][i] for i in range(2))", message="native friend gauge did not bind a display view")
    add("save", **{"as":"hud_status", "expr":"[int(peek_at(saved['views'][i]+0x5C0,'u64',i),16) for i in range(2)]"})

for i in range(2):
    add("boot", instance=i, initTimeoutMs=60000, env={"KH2COOP_PROFILE":"0x54"})
    add("warp", instance=i, world=4, room=26)
add("relay", port=27808, args=["--bind","127.0.0.1"])
for i, role in enumerate(["player","friend1"]):
    add("runtime", instance=i, role=role)
add("wait_until", expr="all((bridge(i).get('puppet0') or {}).get('active') for i in range(2))", timeoutMs=30000, pollMs=1000)
add("wait_until", expr="all(log_count('[nativehud] slot=0 owner='+str(1-i),i)>0 for i in range(2))", timeoutMs=15000)
add("wait", ms=3000)
for i in range(2):
    add("input", instance=i, lx=1, ms=1500)
add("wait", ms=1000)
bindings()
add("save", **{"as":"fire_before", "expr":"[peek(0x9ACE44,'u8',i) for i in range(2)]"})
add("save", **{"as":"command_before", "expr":"[{'flags':peek(0x2A10644,'u32',i),'spellCount':peek(0x2A107D8,'i32',i),'boundActor':peek(0x2A11100,'u64',i),'charge':peek_at(saved['status'][i]+0x1BC,'f32',i),'chargeMax':peek_at(saved['status'][i]+0x1C0,'f32',i),'form':peek_at(saved['local'][i]+0xDE0,'i32',i)} for i in range(2)]"})
add("checkpoint", name="Single-PC loopback: each Sora owns 80 HP and 10 MP; Fire is temporarily available in RAM")
for i in range(2):
    cmd(i, f"['poke','--rva','0x9ACE44','--type','u8','--value',str(max(1,saved['fire_before'][{i}]))]")
    for offset, value in [(4,80),(0,80),(0x184,10),(0x180,10)]:
        cmd(i, f"['poke','--addr',str(saved['status'][{i}]+{offset}),'--type','i32','--value','{value}']")
add("wait_until", expr=f"{hud(0)}==80 and {hud(1)}==80 and {hud(0,0x180)}==10 and {hud(1,0x180)}==10", timeoutMs=10000)
add("save", **{"as":"command_after", "expr":"[{'flags':peek(0x2A10644,'u32',i),'spellCount':peek(0x2A107D8,'i32',i),'boundActor':peek(0x2A11100,'u64',i),'chargeMax':peek_at(saved['status'][i]+0x1C0,'f32',i)} for i in range(2)]"})
add("assert", expr="all(peek(0x2A107D8,'i32',i)>0 for i in range(2))", message="no native spells available in setup")
for i in range(2):
    add("capture", instance=i, name="native_party_full")
    add("clip", instance=i, seconds=20, fps=20, name="reciprocal_damage_fire", background=True)
add("checkpoint", name="RAM-controlled guest owner: HP 80 -> 69, MP 10 -> 3 -> 0; host native HUD follows")
for offset,value in [(0,69),(0x180,3)]:
    cmd(1, f"['poke','--addr',str(saved['status'][1]+{offset}),'--type','i32','--value','{value}']")
add("wait_until", expr=f"{hud(0)}==69 and {hud(0,0x180)}==3", timeoutMs=5000, message="host native HUD did not show the guest's RAM-controlled owner HP/MP")
add("save", **{"as":"controlled_guest_partial", "expr":"{'ownerHP':peek_at(saved['status'][1],'i32',1),'ownerMP':peek_at(saved['status'][1]+0x180,'i32',1),'hostHUD_HP':peek_at(saved['hud_status'][0],'i32',0),'hostHUD_MP':peek_at(saved['hud_status'][0]+0x180,'i32',0)}"})
add("wait", ms=1500)
cmd(1, "['poke','--addr',str(saved['status'][1]+0x180),'--type','i32','--value','0']")
add("wait_until", expr=f"{hud(0)}==69 and {hud(0,0x180)}==0", timeoutMs=5000)
add("wait", ms=1500)
add("checkpoint", name="Host takes native test-hit damage 7 and casts real Fire; guest native HUD follows")
cmd(0, "['hit','damage','--victim',str(saved['local'][0]),'--amount','7']")
add("wait", ms=500)
add("press", instance=0, button="down", ms=40)
add("wait", ms=250)
add("press", instance=0, button="cross", ms=60)
add("wait", ms=350)
add("press", instance=0, button="cross", ms=60)
add("wait_until", expr=f"{own(0,0x180)}==0", timeoutMs=8000, message="native host Fire did not consume 10 MP")
add("wait_until", expr=f"{hud(1)}==73 and {hud(1,0x180)}==0", timeoutMs=5000, message="guest native party HUD did not show host damage and MP use")
add("assert", expr=f"{own(0)}==73", message="host native test-hit damage did not match")
add("clip_wait")
add("save", **{"as":"after_hits_casts", "expr":"[{'ownHP':peek_at(saved['status'][i],'i32',i),'ownMP':peek_at(saved['status'][i]+0x180,'i32',i),'remoteHP':peek_at(saved['hud_status'][i],'i32',i),'remoteMP':peek_at(saved['hud_status'][i]+0x180,'i32',i)} for i in range(2)]"})
add("checkpoint", name="Host naturally recharges; guest MP is restored in RAM; both remote gauges follow")
add("wait_until", expr=f"{own(0,0x180)}==10", timeoutMs=120000, pollMs=1000)
cmd(1, "['poke','--addr',str(saved['status'][1]+0x180),'--type','i32','--value','10']")
add("wait_until", expr=f"{hud(0,0x180)}==10 and {hud(1,0x180)}==10", timeoutMs=5000)
for i in range(2):
    add("clip", instance=i, seconds=4, fps=20, name="native_mp_recharged", background=True)
add("clip_wait")
for i in range(2):
    add("capture", instance=i, name="native_party_recharged")
    cmd(i, f"['poke','--rva','0x9ACE44','--type','u8','--value',str(saved['fire_before'][{i}])]")
add("checkpoint", name="Host reloads the same room: native friend HUD binds fresh views without duplicate slots")
add("warp", instance=0, world=4, room=26)
add("wait", ms=5000)
add("wait_until", expr="all((bridge(i).get('puppet0') or {}).get('active') for i in range(2))", timeoutMs=30000, pollMs=1000)
bindings()
for i in range(2):
    add("capture", instance=i, name="native_party_after_reload")

scenario = dict(name="net_native_party_hud", playerDefaults=True,
    description="VUH-2568 single-PC loopback, two Soras in GoA. Native party portraits and authoritative HP/MP in both directions. Guest owner HP 80->69 and MP 10->3->0->10 are RAM-controlled (not a native cast). Host takes native test-hit damage 7, casts real Fire using temporary RAM Fire unlock/10-MP setup, and naturally recharges. One same-room reload. Local HUD remains native. No saves; this is not two-PC acceptance.", steps=steps)
Path(__file__).with_suffix("").with_suffix(".json").write_text(json.dumps(scenario,indent=2)+"\n",encoding="utf-8")
