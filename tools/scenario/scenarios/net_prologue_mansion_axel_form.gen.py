"""Generates the VUH-2911 Axel II fixtures: the scripted dual-wield Roxas for every default-Roxas seat.

    python -B tools/scenario/scenarios/net_prologue_mansion_axel_form.gen.py

FM-02 rehearsal route (the committed Computer Room -> Basement Hall pilot, both Dusk waves) into Axel II,
02/14 btl137 (checked site tt20-137). A default Roxas seat builds the native encounter body 0x323
(P_EX110_BTLF) for its local and for its clone on the other seat, with both keyblades; a Sora seat keeps Sora
for both. Inside the host's combat-admission hold (proven around the window, else a setup failure), one
synthetic 1-HP native hit lands on each seat's clone: the observer's own HP, every SAVE character record
(SAVE+0x24F0, 0xE04) and every drive-form row (SAVE+0x32F4, 0x230: the row a dual-wield status +0x254
references) must be byte-identical just before and just after the hits. Right after the admission release
each owner is downed once and revived by the other seat (owner_downs: an Axel downing counts, else the
DownedSpike fixture Kill; both seats down is a harness failure). Each owner then moves, jumps and attacks
alone, filmed on both seats; then both are downed and the host takes the native Continue to the Computer Room 02/15 d99, where
each seat holds only its selected body: no dual-wield body, keyblade pair or old clone. Video-only paired
clips (lead amendment). Rehearsal save, no save.
"""
import json
from pathlib import Path

AXEL = [2, 20, 0, 137, 137, 137]
CHECKPOINT = [2, 21, 99, 2, 0, 0]
RELEASED_AXEL = "control released at=02/14 evt=137 site=tt20-137"
RELEASED_HALL = "control released at=02/13 evt=134 site=tt19-134"
FORM_ROWS = ["forms", 0x32F4, 0x230]  # ten 0x38-byte drive-form rows; form 10 (dual-wield) is SAVE+0x34EC
ISOLATION = ["characters", "forms"]  # every SAVE character record (0x24F0, 0xE04) and the form rows
ENV = {"KH2COOP_ENEMY_MIRROR_TRACE": "1", "KH2COOP_DOWNED_SPIKE_FIXTURE": "1"}
ROXAS, SORA, BTLF = 0x5A, 0x54, 0x323
MODEL = {BTLF: "P_EX110_BTLF", SORA: "P_EX100", ROXAS: "P_EX110"}
KEY = {BTLF: 14, SORA: 1, ROXAS: 14}
# Each fixture's selections (host, friend). The default run is the delivery run; the mixed ones keep Sora.
VARIANTS = {
    "net_prologue_mansion_axel_form": (ROXAS, ROXAS),
    "net_prologue_mansion_axel_form_sora_host": (SORA, ROXAS),
    "net_prologue_mansion_axel_form_sora_friend": (ROXAS, SORA),
}


def axel_body(selected):
    return BTLF if selected == ROXAS else selected


def canon(i):
    return f"int(peek('0x2A105D0','u64',{i}),16)"


def probe(i, clone_model):
    # One entity listing per seat: the observer's own HP and its clone of the other seat.
    return (f"(lambda xs, c: {{'local': [a['hp'] for a in xs if int(a['address'],16) == c][0], "
            f"'clone': [a['address'] for a in xs if a['name'] == '{clone_model}' and int(a['address'],16) != c][0]}})"
            f"(actors({i}), {canon(i)})")


def count(name, i):
    return f"len([a for a in actors({i}) if a['name'] == '{name}'])"


def census(i):
    return (f"{{'btlf': {count('P_EX110_BTLF', i)}, 'roxas': {count('P_EX110', i)}, 'sora': {count('P_EX100', i)}, "
            f"'w10': {count('W_EX010_10', i)}, 'w20': {count('W_EX010_20', i)}, "
            f"'member0': peek('0x2A25300','u16',{i}), 'location': location({i})}}")


def form_roots(i):
    # Per dual-wield body: its form (actor+0xDE0) and its status +0x254 drive-form-row handle (VUH-2911 RE).
    return (f"[{{'actor': a['address'], 'local': int(a['address'],16) == {canon(i)}, "
            f"'form': peek_at(int(a['address'],16) + 0xDE0, 'u8', {i}), "
            f"'root': peek_at(int(a['status'],16) + 0x254, 'u64', {i})}} for a in actors({i}) if a['name'] == 'P_EX110_BTLF']")


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


def gone_after_retry(i):
    addr = f"saved['probe_before'][{i}]['clone'][2:].upper()"
    return (f"len(log_matches(r'room=02/14 door=0 map=137 btl=137 evt=137[\\s\\S]*?load complete serial=\\d+ transition=\\d+ "
            f"room=02/15 door=99[\\s\\S]*\\[puppet 0\\][^\\n]*actor=0*' + {addr}, {i})) == 0")


def steps_for(selection):
    local = [axel_body(s) for s in selection]           # each seat's own body at Axel II
    clone = [local[1], local[0]]                         # each seat's clone of the other seat
    btlf = sum(body == BTLF for body in local)           # dual-wield bodies on each screen
    sora = sum(body == SORA for body in local)
    env = [dict(ENV, **({"KH2COOP_PROFILE": f"0x{s:X}"} if s != ROXAS else {})) for s in selection]
    born = " and ".join(
        f"len(log_matches(r'born site=tt20-137 bound=1 clone=\\S+ cloneObject={clone[i]} cloneKey={KEY[clone[i]]} "
        f"local=\\S+ localKey={KEY[local[i]]} ', {i})) == 1" for i in (0, 1))
    at_axel = " and ".join(
        f"saved['census_axel'][{i}]['btlf'] == {btlf} and saved['census_axel'][{i}]['sora'] == {sora} and "
        f"saved['census_axel'][{i}]['roxas'] == 0 and saved['census_axel'][{i}]['w10'] == {btlf} and "
        f"saved['census_axel'][{i}]['w20'] == {btlf} and saved['census_axel'][{i}]['member0'] == {local[i]}" for i in (0, 1))
    retired = " and ".join(
        f"saved['retired'][{i}]['btlf'] == 0 and saved['retired'][{i}]['w10'] == 0 and saved['retired'][{i}]['w20'] == 0 and "
        f"saved['retired'][{i}]['roxas'] == {int(selection[i] == ROXAS)} and saved['retired'][{i}]['sora'] == {int(selection[i] == SORA)} and "
        f"saved['retired'][{i}]['member0'] == {selection[i]} and "
        f"log_count('[prologue] encounter exit at=02/15', {i}) == {int(selection[i] != ROXAS)}" for i in (0, 1))
    return [
        {"do": "boot", "instance": 0, "slotMoves": -1, "initTimeoutMs": 60000, "env": env[0]},
        {"do": "boot", "instance": 1, "slotMoves": -1, "initTimeoutMs": 60000, "env": env[1]},
        {"do": "assert", "expr": "room(0)[0] == 2 and room(1)[0] == 2", "message": "the rehearsal save must load Twilight Town on both seats"},
        {"do": "save", "as": "boot_vitals", "expr": "[{'hp': [a['hp'] for a in actors(i) if int(a['address'],16) == "
                                                    "int(peek('0x2A105D0','u64',i),16)][0], 'difficulty': peek('0x9ABD48','u8',i)} for i in (0, 1)]"},
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
        # Both seats built their tt20-137 pair: start the clone-hit window at once, inside the host's hold.
        {"do": "wait_until", "expr": "log_count('[missing-party] born site=tt20-137 bound=1 ', 0) > 0 and "
                                     "log_count('[missing-party] born site=tt20-137 bound=1 ', 1) > 0", "timeoutMs": 30000, "pollMs": 100},
        {"do": "save", "as": "window_open", "expr": "[log_count('[combat-admission] deferred group=1 ', 0), "
                                                    "log_count('[combat-admission] released ', 0)]"},
        {"do": "progress_snapshot", "instances": [0, 1], "as": "hit_before", "only": ISOLATION, "extraRanges": [FORM_ROWS]},
        {"do": "save", "as": "probe_before", "expr": f"[{probe(0, MODEL[clone[0]])}, {probe(1, MODEL[clone[1]])}]"},
        {"do": "kh2ctl", "instance": 0, "as": "hit0", "argsExpr": "['hit', 'damage', '--victim', saved['probe_before'][0]['clone'], '--amount', '1']"},
        {"do": "kh2ctl", "instance": 1, "as": "hit1", "argsExpr": "['hit', 'damage', '--victim', saved['probe_before'][1]['clone'], '--amount', '1']"},
        {"do": "wait", "ms": 700},
        {"do": "save", "as": "probe_after", "expr": f"[{probe(0, MODEL[clone[0]])}, {probe(1, MODEL[clone[1]])}]"},
        {"do": "progress_snapshot", "instances": [0, 1], "as": "hit_after", "compare": "hit_before", "only": ISOLATION,
         "extraRanges": [FORM_ROWS]},
        {"do": "save", "as": "window_close", "expr": "log_count('[combat-admission] released ', 0)"},
        {"do": "assert", "expr": "saved['window_open'][0] > saved['window_open'][1] and saved['window_close'] == saved['window_open'][1]",
         "message": "SETUP FAILURE, not a form result: the host's combat-admission hold (Axel frozen) did not cover the whole clone-hit window; rerun"},
        {"do": "assert", "expr": "all(saved['hit%d' % i]['ok'] and saved['hit%d' % i]['hpAfter'] == saved['hit%d' % i]['hpBefore'] - 1 for i in (0, 1))",
         "message": "the synthetic native hit must damage each seat's clone by exactly 1"},
        {"do": "assert", "expr": "all(saved['probe_after'][i]['local'] == saved['probe_before'][i]['local'] for i in (0, 1)) and "
                                 "all(saved['hit_after']['instances'][k]['diffs'][r] == [] for k in ('0', '1') for r in ('characters', 'forms'))",
         "message": "a hit on a clone changed its observer's own HP, a SAVE character record byte (SAVE+0x24F0..0x32F4) or a drive-form row byte"},
        {"do": "save", "as": "census_axel", "expr": f"[{census(0)}, {census(1)}]"},
        {"do": "save", "as": "form_roots", "expr": f"[{form_roots(0)}, {form_roots(1)}]"},
        # Lead criterion: every drive-form row (SAVE+0x32F4, 0x230, form 10 at +0x34EC) also unchanged from boot.
        {"do": "progress_snapshot", "instances": [0, 1], "as": "save_posthit", "compare": "save_boot", "extraRanges": [FORM_ROWS]},
        {"do": "save", "as": "axel_log", "expr": "[[m['l'] for m in log_matches(r'(?P<l>\\[(?:missing-party\\] born|prologue\\] (?:construction|control released|local body|duplicate))[^\\n]*)', i)][-8:] for i in (0, 1)]"},
        {"do": "capture", "instance": 0, "name": "axel_form_host"},
        {"do": "capture", "instance": 1, "name": "axel_form_friend"},
        {"do": "assert", "expr": f"all(log_count('[missing-party] born site=tt20-137 bound=1 ', i) == 1 and "
                                 f"log_count('[prologue] construction at=02/14 evt=137', i) >= 1 for i in (0, 1)) and {born}",
         "message": "each seat must build one bound tt20-137 pair: a default Roxas as dual-wield 0x323 (key 14), Sora as Sora (key 1)"},
        {"do": "assert", "expr": at_axel,
         "message": "each screen must hold one body per seat (dual-wield Roxas with both keyblades, or Sora), no single-blade Roxas"},
        {"do": "assert", "expr": f"all(len(saved['form_roots'][i]) == {btlf} and all(int(str(b['form']), 0) == 10 and int(str(b['root']), 0) != 0 "
                                 f"for b in saved['form_roots'][i]) for i in (0, 1))",
         "message": "each dual-wield body must be native form 10 with a drive-form-row handle at status +0x254"},
        {"do": "assert", "expr": "all(saved['save_posthit']['instances'][k]['diffs']['forms'] == [] for k in ('0', '1'))",
         "message": "a drive-form SAVE row changed between boot and the clone hits"},
        {"do": "wait_until", "expr": f"log_count('{RELEASED_AXEL}', 0) > 0 and log_count('{RELEASED_AXEL}', 1) > 0",
         "timeoutMs": 60000, "pollMs": 100},
        {"do": "wait_until", "expr": "log_count('[combat-admission] released admitted ', 0) > 0", "timeoutMs": 30000, "pollMs": 100},
        # One logical down and partner revive per owner, right after the release (run vuh2911-axel-01: on the
        # 22-HP FM-02 save Axel downed the host before a fixed forced down). A natural down counts once revived.
        {"do": "owner_downs", "owners": [1, 0], "as": "owner_downs", "clipSeconds": 12, "timeoutMs": 240000},
        {"do": "clip_wait"},
        {"do": "save", "as": "downs_after", "expr": f"[{census(0)}, {census(1)}]"},
        {"do": "assert", "expr": f"all(saved['owner_downs']['owners'][k] for k in ('0', '1')) and "
                                 f"all(saved['downs_after'][i]['btlf'] == {btlf} for i in (0, 1))",
         "message": "each owner must be downed once and revived by the other seat, keeping both seats' bodies"},
        # Each owner alone moves, jumps and attacks, filmed on both screens.
        *clip_pair("owner0", 6), *owner_actions(0), {"do": "clip_wait"},
        *clip_pair("owner1", 6), *owner_actions(1), {"do": "clip_wait"},
        {"do": "save", "as": "clone_motions", "expr": f"[{motions(0)}, {motions(1)}]"},
        {"do": "assert", "expr": "all(len(saved['clone_motions'][i]) >= 3 for i in (0, 1))",
         "message": "each seat's clone must play its owner's motions (puppet receipts)"},
        # Full-party down (a seat Axel already downed refuses its Kill with WrongState), then the host's native
        # Continue to the Computer Room checkpoint.
        {"do": "save", "as": "arrivals_before_retry", "expr": "log_count('[enemysync] client arrived epoch=', 1)"},
        {"do": "downed_kill", "instance": 1, "require": False},
        {"do": "downed_kill", "instance": 0, "require": False},
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
        {"do": "assert", "expr": retired,
         "message": "after Continue each seat must hold only its selected body (Sora restored, Roxas native), no dual-wield body or keyblade pair"},
        {"do": "assert", "expr": f"{gone_after_retry(0)} and {gone_after_retry(1)}",
         "message": "the old Axel II clone must never be driven again after the Continue load"},
        {"do": "progress_snapshot", "instances": [0, 1], "as": "save_end", "compare": "save_boot", "extraRanges": [FORM_ROWS]},
        {"do": "assert", "expr": "all(saved['save_end']['instances'][k]['diffs']['forms'] == [] for k in ('0', '1'))",
         "message": "the drive-form rows (the dual-wield row the clone's status +0x254 references) changed on a seat"},
        {"do": "coop_verdict", "as": "verdict", "require": True, "arrivalTimeoutMs": 20000},
    ]


if __name__ == "__main__":
    description = __doc__.split("\n\n", 2)[2].strip().replace("\n", " ")
    for name, selection in VARIANTS.items():
        seats = f" Selections: host 0x{selection[0]:X}, friend 0x{selection[1]:X}."
        scenario = {"name": name, "description": description + seats, "playerDefaults": True, "protect": [],
                    "steps": steps_for(selection)}
        out = Path(__file__).with_name(f"{name}.json")
        out.write_text(json.dumps(scenario, indent=1) + "\n", encoding="utf-8")
        print(out)
