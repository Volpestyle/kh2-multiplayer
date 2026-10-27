"""Generates net_prologue_mansion_axel_form.json (VUH-2911): Axel II's scripted dual-wield Roxas for both seats.

    python -B tools/scenario/scenarios/net_prologue_mansion_axel_form.gen.py

FM-02 rehearsal route (the committed Computer Room -> Basement Hall pilot, both Dusk waves) into Axel II,
02/14 btl137 (checked site tt20-137). Both default Roxas seats must build the native encounter body 0x323
(P_EX110_BTLF) for their local and their clone of the other seat, with both keyblades, and release together.
Inside the combat-admission hold, one synthetic 1-HP native hit lands on each seat's clone: the observer's
own HP, its SAVE character record (Sora/Roxas, SAVE+0x24F0) and the drive-form rows (SAVE+0x32F4, the row
the clone's status +0x254 references) must not change. Each owner then moves, jumps and attacks alone,
filmed on both seats; each owner is downed once (DownedSpike fixture Kill) and revived by the other seat;
then both are downed and the host takes the native Continue to the Computer Room 02/15 d99, where no
dual-wield body or old clone may remain. Video-only paired clips (lead amendment). Rehearsal save, no save.
"""
import json
from pathlib import Path

AXEL = [2, 20, 0, 137, 137, 137]
CHECKPOINT = [2, 21, 99, 2, 0, 0]
RELEASED_AXEL = "control released at=02/14 evt=137 site=tt20-137"
RELEASED_HALL = "control released at=02/13 evt=134 site=tt19-134"
FORM_ROWS = ["forms", 0x32F4, 0x230]  # ten 0x38-byte drive-form rows; form 10 (dual-wield) is SAVE+0x34EC
FORM10 = [f"0x{0x9A98B0 + 0x34EC + 8 * k:X}" for k in range(7)]
ENV = {"KH2COOP_ENEMY_MIRROR_TRACE": "1", "KH2COOP_DOWNED_SPIKE_FIXTURE": "1"}


def canon(i):
    return f"int(peek('0x2A105D0','u64',{i}),16)"


def clone(i):
    return f"[a['address'] for a in actors({i}) if a['name'] == 'P_EX110_BTLF' and int(a['address'],16) != {canon(i)}][0]"


def local_hp(i):
    return f"[a['hp'] for a in actors({i}) if int(a['address'],16) == {canon(i)}][0]"


def count(name, i):
    return f"len([a for a in actors({i}) if a['name'] == '{name}'])"


def probe(i):
    forms = ", ".join(f"peek('{rva}','u64',{i})" for rva in FORM10)
    return (f"{{'save': peek('0x9ABDA0','u64',{i}), 'form10': [{forms}], 'local': {local_hp(i)}, 'clone': {clone(i)}, "
            f"'admitted': log_count('[combat-admission] released admitted ', 0)}}")


def census(i):
    return (f"{{'btlf': {count('P_EX110_BTLF', i)}, 'roxas': {count('P_EX110', i)}, 'w10': {count('W_EX010_10', i)}, "
            f"'w20': {count('W_EX010_20', i)}, 'member0': peek('0x2A25300','u16',{i}), 'location': location({i})}}")


def clip_pair(name, seconds):
    return [{"do": "clip", "instance": i, "seconds": seconds, "fps": 30, "background": True, "name": f"{name}_{i}"} for i in (0, 1)]


def owner_actions(i):
    """Owner-only input on seat i: forward stick, a jump (Circle), three attacks (Cross)."""
    press = lambda button: {"do": "kh2ctl", "instance": i, "args": ["player-press", "--button", button, "--duration-ms", "120"]}
    return [{"do": "kh2ctl", "instance": i, "args": ["player-input", "--ly", "1.0", "--duration-ms", "900"]},
            {"do": "wait", "ms": 200}, press("circle"), {"do": "wait", "ms": 700},
            press("cross"), {"do": "wait", "ms": 350}, press("cross"), {"do": "wait", "ms": 350}, press("cross"),
            {"do": "wait", "ms": 1200}]


def motions(i):
    # The puppet driver's motion lines for this seat's Axel II clone (its address from the probe).
    return (f"sorted({{m['b'] for m in log_matches(r'\\[puppet 0\\] frame \\d+ motion \\d+ -> (?P<b>\\d+) [^\\n]*actor=0*(?P<a>[0-9A-F]+)', {i}) "
            f"if int(m['a'],16) == int(saved['probe_before'][{i}]['clone'],16)}})")


def down_revive(downed, reviver):
    tag = f"down{downed}"
    return [{"do": "save", "as": f"{tag}_before", "expr": f"log_count('[downed] revive returned', {downed})"},
            {"do": "revive_guard", "host": downed, "friend": reviver},
            *clip_pair(f"{tag}_revive", 12),
            {"do": "downed_kill", "instance": downed},
            {"do": "wait_until", "expr": f"log_count('[downed] revive returned', {downed}) > saved['{tag}_before']",
             "timeoutMs": 60000, "pollMs": 250},
            {"do": "wait", "ms": 1500},
            {"do": "revive_guard", "on": False, "as": f"{tag}_guard"},
            {"do": "clip_wait"},
            {"do": "save", "as": f"{tag}_after", "expr": f"[{census(0)}, {census(1)}]"}]


def gone_after_retry(i):
    addr = f"saved['probe_before'][{i}]['clone'][2:].upper()"
    return (f"len(log_matches(r'room=02/14 door=0 map=137 btl=137 evt=137[\\s\\S]*?load complete serial=\\d+ transition=\\d+ "
            f"room=02/15 door=99[\\s\\S]*\\[puppet 0\\][^\\n]*actor=0*' + {addr}, {i})) == 0")


steps = [
    {"do": "boot", "instance": 0, "slotMoves": -1, "initTimeoutMs": 60000, "env": ENV},
    {"do": "boot", "instance": 1, "slotMoves": -1, "initTimeoutMs": 60000, "env": ENV},
    {"do": "assert", "expr": "room(0)[0] == 2 and room(1)[0] == 2", "message": "the rehearsal save must load Twilight Town on both seats"},
    {"do": "save", "as": "boot_vitals", "expr": f"[{{'hp': {local_hp(0)}, 'difficulty': peek('0x9ABD48','u8',0)}}, "
                                               f"{{'hp': {local_hp(1)}, 'difficulty': peek('0x9ABD48','u8',1)}}]"},
    {"do": "progress_snapshot", "instances": [0, 1], "as": "save_boot", "extraRanges": [FORM_ROWS]},
    {"do": "relay", "port": 27846, "args": ["--bind", "127.0.0.1"]},
    {"do": "runtime", "instance": 0, "role": "player"},
    {"do": "runtime", "instance": 1, "role": "friend1"},
    {"do": "wait_until", "expr": "log_count('[enemysync] client arrived epoch=', 1) > 0", "timeoutMs": 60000},
    {"do": "hang_check", "instance": 0, "on": False},
    {"do": "hang_check", "instance": 1, "on": False},
    {"do": "wait_until", "expr": "log_count('[prologue] unpinned start', 0) > 0 and log_count('[prologue] unpinned start', 1) > 0",
     "timeoutMs": 60000, "pollMs": 500},
    {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/fm02_computer_to_hall.json", "seconds": 120,
     "name": "host_hall", "input": "pad", "allowStuck": True},
    {"do": "wait_until", "expr": f"log_count('{RELEASED_HALL}', 0) > 0 and log_count('{RELEASED_HALL}', 1) > 0",
     "timeoutMs": 90000, "pollMs": 500},
    {"do": "save", "as": "deaths_before", "expr": "log_count('[enemysync] host death epoch', 0)"},
    {"do": "fight", "instances": [0, 1], "seconds": 240, "reach": 250, "as": "hall",
     "until": "log_count('[enemysync] host death epoch', 0) >= saved['deaths_before'] + 6"},
    {"do": "wait_until", "expr": f"location(0) == {AXEL} and location(1) == {AXEL}", "timeoutMs": 90000, "pollMs": 250},
    {"do": "wait_until", "expr": f"log_count('{RELEASED_AXEL}', 0) > 0 and log_count('{RELEASED_AXEL}', 1) > 0",
     "timeoutMs": 60000, "pollMs": 100},
    # Inside the combat-admission hold: one 1-HP native hit on each seat's dual-wield clone.
    {"do": "save", "as": "probe_before", "expr": f"[{probe(0)}, {probe(1)}]"},
    {"do": "kh2ctl", "instance": 0, "as": "hit0", "argsExpr": f"['hit', 'damage', '--victim', saved['probe_before'][0]['clone'], '--amount', '1']"},
    {"do": "kh2ctl", "instance": 1, "as": "hit1", "argsExpr": f"['hit', 'damage', '--victim', saved['probe_before'][1]['clone'], '--amount', '1']"},
    {"do": "wait", "ms": 700},
    {"do": "save", "as": "probe_after", "expr": f"[{probe(0)}, {probe(1)}]"},
    {"do": "save", "as": "census_axel", "expr": f"[{census(0)}, {census(1)}]"},
    # Lead criterion: every drive-form row (SAVE+0x32F4, 0x230, form 10 at +0x34EC) unchanged after the hits too.
    {"do": "progress_snapshot", "instances": [0, 1], "as": "save_posthit", "compare": "save_boot", "extraRanges": [FORM_ROWS]},
    {"do": "save", "as": "axel_log", "expr": "[[m['l'] for m in log_matches(r'(?P<l>\\[(?:missing-party\\] born|prologue\\] (?:construction|control released|local body|duplicate))[^\\n]*)', i)][-8:] for i in (0, 1)]"},
    {"do": "capture", "instance": 0, "name": "axel_form_host"},
    {"do": "capture", "instance": 1, "name": "axel_form_friend"},
    {"do": "assert", "expr": "all(log_count('[missing-party] born site=tt20-137 bound=1 ', i) == 1 and "
                             "len(log_matches(r'born site=tt20-137 bound=1 clone=\\S+ cloneObject=803 cloneKey=14 local=\\S+ localKey=14 ', i)) == 1 and "
                             "log_count('[prologue] construction at=02/14 evt=137', i) >= 1 for i in (0, 1))",
     "message": "each seat must build one bound tt20-137 pair: its local and its clone of the other seat both dual-wield Roxas 0x323 (key 14)"},
    {"do": "assert", "expr": "all(saved['census_axel'][i]['btlf'] == 2 and saved['census_axel'][i]['roxas'] == 0 and "
                             "saved['census_axel'][i]['w10'] == 2 and saved['census_axel'][i]['w20'] == 2 and "
                             "saved['census_axel'][i]['member0'] == 0x323 for i in (0, 1))",
     "message": "each screen must hold exactly two dual-wield Roxases (local + clone) with both keyblades each, and no single-blade Roxas"},
    {"do": "assert", "expr": "all(saved['hit%d' % i]['ok'] and saved['hit%d' % i]['hpAfter'] == saved['hit%d' % i]['hpBefore'] - 1 for i in (0, 1))",
     "message": "the synthetic native hit must damage each seat's clone by exactly 1"},
    {"do": "assert", "expr": "all(saved['probe_after'][i]['local'] == saved['probe_before'][i]['local'] and "
                             "saved['probe_after'][i]['save'] == saved['probe_before'][i]['save'] and "
                             "saved['probe_after'][i]['form10'] == saved['probe_before'][i]['form10'] for i in (0, 1))",
     "message": "a hit on a clone changed its observer's own HP, SAVE character record or dual-wield form row"},
    {"do": "assert", "expr": "all(saved['save_posthit']['instances'][k]['diffs']['forms'] == [] for k in ('0', '1'))",
     "message": "a drive-form SAVE row changed between boot and the clone hits"},
    {"do": "wait_until", "expr": "log_count('[combat-admission] released admitted ', 0) > 0", "timeoutMs": 30000, "pollMs": 100},
    # Each owner alone moves, jumps and attacks, filmed on both screens.
    *clip_pair("owner0", 6), *owner_actions(0), {"do": "clip_wait"},
    *clip_pair("owner1", 6), *owner_actions(1), {"do": "clip_wait"},
    {"do": "save", "as": "clone_motions", "expr": f"[{motions(0)}, {motions(1)}]"},
    {"do": "assert", "expr": "all(len(saved['clone_motions'][i]) >= 3 for i in (0, 1))",
     "message": "each seat's dual-wield clone must play its owner's motions (puppet receipts)"},
    # One logical down and partner revive per owner.
    *down_revive(1, 0),
    *down_revive(0, 1),
    {"do": "assert", "expr": "log_count('[downed] full party down', 0) == 0 and all(saved[t][i]['btlf'] == 2 for t in ('down1_after', 'down0_after') for i in (0, 1))",
     "message": "a single down/revive must keep both dual-wield bodies and never end the party"},
    # Full-party down, then the host's native Continue to the Computer Room checkpoint.
    {"do": "save", "as": "arrivals_before_retry", "expr": "log_count('[enemysync] client arrived epoch=', 1)"},
    {"do": "downed_kill", "instance": 1},
    {"do": "downed_kill", "instance": 0},
    {"do": "wait_until", "expr": "log_count('[downed] full party down', 0) > 0 and int(str(peek('0x2AE8050','u64',0)),0) != 0",
     "timeoutMs": 30000, "pollMs": 250},
    {"do": "wait", "ms": 7000},
    {"do": "capture", "instance": 0, "name": "game_over_host"},
    {"do": "capture", "instance": 1, "name": "game_over_friend"},
    {"do": "press_continue", "instance": 0, "as": "retry", "presses": 5, "gapMs": 3000, "timeoutMs": 40000},
    {"do": "assert", "expr": f"saved['retry']['destination'] == {CHECKPOINT} and saved['retry']['armed'] == 0",
     "message": "native Continue must return unarmed to the Computer Room checkpoint"},
    {"do": "wait_until", "expr": f"location(0) == {CHECKPOINT} and location(1) == {CHECKPOINT} and "
                                 "log_count('[enemysync] client arrived epoch=', 1) > saved['arrivals_before_retry']",
     "timeoutMs": 40000, "pollMs": 250},
    {"do": "wait", "ms": 3000},
    {"do": "capture", "instance": 0, "name": "checkpoint_host"},
    {"do": "capture", "instance": 1, "name": "checkpoint_friend"},
    {"do": "save", "as": "retired", "expr": f"[{census(0)}, {census(1)}]"},
    {"do": "assert", "expr": "all(saved['retired'][i]['btlf'] == 0 and saved['retired'][i]['roxas'] == 1 and "
                             "saved['retired'][i]['member0'] == 0x5A for i in (0, 1))",
     "message": "after Continue each seat must be back to its selected Roxas only: no dual-wield body, no clone, member 0 Roxas"},
    {"do": "assert", "expr": f"{gone_after_retry(0)} and {gone_after_retry(1)}",
     "message": "the old Axel II clone must never be driven again after the Continue load"},
    {"do": "progress_snapshot", "instances": [0, 1], "as": "save_end", "compare": "save_boot", "extraRanges": [FORM_ROWS]},
    {"do": "assert", "expr": "all(saved['save_end']['instances'][k]['diffs']['forms'] == [] for k in ('0', '1'))",
     "message": "the drive-form rows (the dual-wield row the clone's status +0x254 references) changed on a seat"},
    {"do": "coop_verdict", "as": "verdict", "require": True, "arrivalTimeoutMs": 20000},
]

scenario = {
    "name": "net_prologue_mansion_axel_form",
    "description": __doc__.split("\n\n", 2)[2].strip().replace("\n", " "),
    "playerDefaults": True,
    "protect": [],
    "steps": steps,
}
out = Path(__file__).with_name("net_prologue_mansion_axel_form.json")
out.write_text(json.dumps(scenario, indent=1) + "\n", encoding="utf-8")
print(out)
