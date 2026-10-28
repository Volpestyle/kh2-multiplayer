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


KILL_CLEARANCE = 600  # units between Axel and each seat before a forced owner_downs Kill


def axel_clear(distance):
    """On the host's screen: Axel at least `distance` (horizontal) from the host and from its clone of the friend
    (the address probe_before saved)."""
    return (f"(lambda a, ps: len(ps) == 2 and all(((p[0] - a[0]) ** 2 + (p[1] - a[2]) ** 2) ** 0.5 >= {distance} "
            f"for p in ps))(pos('B_EX110', 0), [(x['position']['x'], x['position']['z']) for x in actors(0) "
            f"if int(x['address'],16) in ({canon(0)}, int(saved['probe_before'][0]['clone'],16))])")


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


def arrival(env):
    """FM-02 boot through the Hall pilot and both Dusk waves to Axel II, until both seats built their tt20-137 pair."""
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
    ]


# owner_downs ended in the host's game-over request (fullParty "continue"): the owners were not both revived.
FULL_PARTY = "saved['owner_downs']['fullParty'] is not None"
SORA_HOST_NOTE = (
    " The Sora-host fixture alone (VUH-2931, runs vuh2931-sora-01/02: Axel downed both seats during owner_downs) also "
    "accepts that natural full-party down as its game over: owner_downs' fullParty continue passes only if the host "
    "requested the game over while both seats were down, and records which seat fell first. The steps that need the "
    "owners revived (their actions, clone motions) and the forced full-party Kills are then skipped (skipIf), and the "
    "run goes on to the same Continue and 02/15 restore checks.")


def steps_for(selection):
    # VUH-2931: only the Sora host's fixture continues from a natural full-party down to its Continue checks.
    continue_full = selection == VARIANTS["net_prologue_mansion_axel_form_sora_host"]

    def unless_full_party(steps):
        return [dict(s, skipIf=FULL_PARTY) for s in steps] if continue_full else steps

    local = [axel_body(s) for s in selection]           # each seat's own body at Axel II
    clone = [local[1], local[0]]                         # each seat's clone of the other seat
    btlf = sum(body == BTLF for body in local)           # dual-wield bodies on each screen
    sora = sum(body == SORA for body in local)
    # Each dual-wield body holds one W_EX010_10 and one W_EX010_20 (run vuh2911-axel-02: 2/2 for two bodies);
    # Sora's own keyblade is a W_EX010_20 too (run vuh2911-sora-01: 1 BTLF + 1 Sora held 1 and 2).
    w10, w20 = btlf, btlf + sora
    env = [dict(ENV, **({"KH2COOP_PROFILE": f"0x{s:X}"} if s != ROXAS else {})) for s in selection]
    born = " and ".join(
        f"len(log_matches(r'born site=tt20-137 bound=1 clone=\\S+ cloneObject={clone[i]} cloneKey={KEY[clone[i]]} "
        f"local=\\S+ localKey={KEY[local[i]]} ', {i})) == 1" for i in (0, 1))
    at_axel = " and ".join(
        f"saved['census_axel'][{i}]['btlf'] == {btlf} and saved['census_axel'][{i}]['sora'] == {sora} and "
        f"saved['census_axel'][{i}]['roxas'] == 0 and saved['census_axel'][{i}]['w10'] == {w10} and "
        f"saved['census_axel'][{i}]['w20'] == {w20} and saved['census_axel'][{i}]['member0'] == {local[i]}" for i in (0, 1))
    retired = " and ".join(
        # 02/15 is a non-battle field: no keyblade actor on either seat, Sora included (run vuh2931-sora-03).
        f"saved['retired'][{i}]['btlf'] == 0 and saved['retired'][{i}]['w10'] == 0 and "
        f"saved['retired'][{i}]['w20'] == 0 and "
        f"saved['retired'][{i}]['roxas'] == {int(selection[i] == ROXAS)} and saved['retired'][{i}]['sora'] == {int(selection[i] == SORA)} and "
        f"saved['retired'][{i}]['member0'] == {selection[i]} and "
        f"log_count('[prologue] encounter exit at=02/15', {i}) == {int(selection[i] != ROXAS)}" for i in (0, 1))
    return [
        *arrival(env),
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
        # A forced Kill waits for Axel to stand clear of both seats (run vuh2931-sora-01: it landed while Axel was
        # combo-hitting the 10-HP reviver, which fell before reviving).
        {"do": "owner_downs", "owners": [1, 0], "as": "owner_downs", "clipSeconds": 12, "timeoutMs": 240000,
         "killWhen": axel_clear(KILL_CLEARANCE), **({"fullParty": "continue"} if continue_full else {})},
        {"do": "clip_wait"},
        *unless_full_party([
            {"do": "save", "as": "downs_after", "expr": f"[{census(0)}, {census(1)}]"},
            {"do": "assert", "expr": f"all(saved['owner_downs']['owners'][k] for k in ('0', '1')) and "
                                     f"all(saved['downs_after'][i]['btlf'] == {btlf} for i in (0, 1))",
             "message": "each owner must be downed once and revived by the other seat, keeping both seats' bodies"},
            # Each owner alone moves, jumps and attacks, filmed on both screens.
            *clip_pair("owner0", 6), *owner_actions(0), {"do": "clip_wait"},
            *clip_pair("owner1", 6), *owner_actions(1), {"do": "clip_wait"},
            {"do": "save", "as": "clone_motions", "expr": f"[{motions(0)}, {motions(1)}]"},
            {"do": "assert", "expr": "all(len(saved['clone_motions'][i]) >= 3 for i in (0, 1))",
             "message": "each seat's clone must play its owner's motions (puppet receipts)"}]),
        # Full-party down (a seat Axel already downed refuses its Kill with WrongState), then the host's native
        # Continue to the Computer Room checkpoint.
        {"do": "save", "as": "arrivals_before_retry", "expr": "log_count('[enemysync] client arrived epoch=', 1)"},
        *unless_full_party([{"do": "downed_kill", "instance": 1, "require": False},
                            {"do": "downed_kill", "instance": 0, "require": False}]),
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


# ---- VUH-2912: shared HP and ordinary targeting after the form handoff (two default Roxases) ----------------
HP_NAME = "net_prologue_mansion_axel_hp"
HP_DESCRIPTION = (
    "VUH-2912, FM-02 rehearsal route into Axel II (02/14 btl137, B_EX110 81), two default dual-wield Roxases, "
    "shipped defaults, no protect; KH2COOP_TRACE_HITS and KH2COOP_ENEMY_MIRROR_TRACE are diagnostics only. "
    "Lease: no boss damage or boss hit on either seat before the host's admitted release. Then three short rounds, "
    "each filmed on both seats for its whole length: friend target (the friend walks in to Axel, the host walks away "
    "from him, again if still too close, and, once both seats stand measured apart, only revives; the friend fights, "
    "so the host's Axel must choose the friend's seat "
    "and the friend's own Roxas takes the native hit from its mirrored Axel), host target (the same, reversed) and "
    "interleaved (both fight). The boss HP ledger joins every host Axel HP change to one host native hit or one "
    "friend claim, and requires the host Axel's hits on the host's copy of the friend to be complete checked zeros "
    "(at least one in the friend round). The boss target ledger requires each round's choice of that seat before a "
    "native hit witnessed by its reaction motion and ignore-hit timer (and, on the friend, the driven host motion), "
    "never a choice of an ineligible candidate, and, around owner_downs, each downed seat out of Axel's candidates "
    "and never chosen. The atk_burst aim and leave distance read the canonical player directly; the friend round's "
    "position/motion samples and the host's boss hits measure that, unjudged. PC1's saves are Critical (22-HP Roxas, "
    "one Axel hit downs a 5-HP seat). Stops before victory. Rehearsal save, no save.")
HP_ENV = dict(ENV, KH2COOP_TRACE_HITS="1")
AXEL_OBJECT = 81


def canonical_xz(i):
    return (f"[(a['position']['x'], a['position']['z']) for a in actors({i}) "
            f"if int(a['address'],16) == {canon(i)}][0]")


def canonical_hp(i):
    return f"[a['hp'] for a in actors({i}) if int(a['address'],16) == {canon(i)}][0]"


def away_from_axel(i, distance=900):
    """A point `distance` from Axel on the line through seat i's canonical player (walk_to targetExpr): past the
    player when it stands nearer than that, between them when farther."""
    return (f"(lambda a, m: (lambda d: (a[0] + {distance} * (m[0] - a[0]) / d, a[2] + {distance} * (m[1] - a[2]) / d))"
            f"(max(1.0, ((m[0] - a[0]) ** 2 + (m[1] - a[2]) ** 2) ** 0.5)))(pos('B_EX110', {i}), {canonical_xz(i)})")


def targets(target):
    """Host boss-target lines for Axel naming candidate `target` (0 the host, 1 the friend's clone)."""
    return f"len(log_matches(r'boss-target frame=\\d+ enemy=\\S+ objectId={AXEL_OBJECT} target={target} ', 0))"


def selected(target):
    """Whether Axel's selection in force names candidate `target`. The host logs boss-target only on a change, so the
    last line is the current choice, whether it was made inside the round or held from the walk."""
    return (f"(log_matches(r'boss-target frame=\\d+ enemy=\\S+ objectId={AXEL_OBJECT} target=(?P<t>-?\\d+) ', 0) "
            f"or [{{'t': None}}])[-1]['t'] == '{target}'")


def marks(name):
    return {"do": "save", "as": name, "expr": "[log_count('', 0), log_count('', 1)]"}


# Lead #76 (vuh2912-hp-09): each seat logs the mirror's per-actor stats just inside every window (after its start
# mark, before its end mark), so the HP ledger's one-brain check has a sample in a window shorter than the 300-frame
# stats interval. Diagnostics: nothing is judged on it but that check.
STATS_MARK = {"do": "stats_mark"}


def host_claims():
    return f"len(log_matches(r'\\[enemysync\\] host hit claim [^\\n]*objectId={AXEL_OBJECT} [^\\n]*nativeOk=1', 0))"


def boss_hp(i):
    return f"([a['hp'] for a in actors({i}) if a['name'] == 'B_EX110' and a.get('hp', 0) > 0] or [-1])[0]"


def clone_xz(i):
    """Seat i's view of the other seat's dual-wield Roxas (its clone)."""
    return (f"[(a['position']['x'], a['position']['z']) for a in actors({i}) "
            f"if a['name'] == 'P_EX110_BTLF' and int(a['address'],16) != {canon(i)}][0]")


def boss_samples():
    """The host's periodic Axel HP samples (every 60 frames), oldest first: hp_ledger's start HP is the last one."""
    return (f"[int(m['hp']) for m in log_matches(r'\\[statehash\\] native epoch=\\d+ frame=\\d+ netId=\\d+ "
            f"objectId={AXEL_OBJECT} hp=(?P<hp>-?\\d+) ', 0)]")


def claims_settled():
    """Every friend claim on Axel so far has its host application or reject (by seq), so none crosses a mark."""
    seq = lambda pattern, i: f"[m['s'] for m in log_matches(r'{pattern}', {i})]"  # noqa: E731
    claim = "\\[enemysync\\] {} hit claim connection=\\d+ seq=(?P<s>\\d+) epoch=\\d+ netId=\\d+ objectId=" + str(AXEL_OBJECT) + " "
    reject = "\\[hitclaim\\] reject epoch=\\d+ seq=(?P<s>\\d+) [^\\n]*objectId=" + str(AXEL_OBJECT) + " "
    return (f"(lambda c, h: all(s in h for s in c))({seq(claim.format('client'), 1)}, "
            f"{seq(claim.format('host'), 0)} + {seq(reject, 0)})")


def aligned():
    """No Axel HP change since the host's last sample, and no friend claim in flight: hp_ledger's start HP is exact."""
    return f"({claims_settled()} and ({boss_samples()} or [None])[-1] == {boss_hp(0)})"


def quiet(extra=""):
    """Counts that move with any Axel hit, claim or selection change (host and friend logs), plus `extra` entries."""
    return (f"{{'hits': len(log_matches(r'(?P<x>victim=B_EX110@|\\[enemysync\\] host hit claim [^\\n]*objectId={AXEL_OBJECT} )', 0)), "
            f"'claims': len(log_matches(r'\\[enemysync\\] client hit claim [^\\n]*objectId={AXEL_OBJECT} ', 1)), "
            f"'targets': len(log_matches(r'boss-target frame=\\d+ enemy=\\S+ objectId={AXEL_OBJECT} ', 0))"
            f"{', ' + extra if extra else ''}}}")


def standing():
    return "all(log_count('[downed] gate intercept', i) <= log_count('[downed] revive returned', i) for i in (0, 1))"


def separation():
    """On the host's screen: each seat's horizontal distance from Axel, and whether both seats are standing."""
    d = "((p[0] - a[0]) ** 2 + (p[1] - a[2]) ** 2) ** 0.5"
    return (f"(lambda a, host, friend: {{'host': (lambda p: {d})(host), 'friend': (lambda p: {d})(friend), "
            f"'standing': {standing()}}})(pos('B_EX110', 0), {canonical_xz(0)}, {clone_xz(0)})")


SEPARATION_MARGIN = 200  # beyond the policy's 150-unit hysteresis
FIGHTER_NEAR = 350      # the fighter's approach stops inside this distance from Axel
SELECT_SECONDS = 15     # the fighter's bound to draw Axel's selection before its round's window
SETTLE_MS = 5000        # a fresh host HP sample (every ~1 s) and every friend claim applied, before the window
# Where the HP fixture's revive guards may step aside (Ione 00c3ac51): Axel II's sealed encounter scope, and a circle
# inside the positions the seats and Axel reached in runs vuh2912-hp-02/03 (centre (39, -27), up to 602 out).
AXEL_ARENA = {"roomScope": AXEL, "center": [39, -27], "radius": 450}


def target_round(name, fighter, reviver, last=False):
    """The fighter walks in to Axel, then the reviver walks away; with both seats standing and measured apart, the
    round's baseline and window start. Then the reviver only revives while the fighter fights, filmed on both seats
    for the whole round. The round must reach its milestone (Axel's selection names the fighter, the fighter was hit
    or downed, it landed damage) before its end mark; boss_target_ledger.py judges the choice, its order before the hit
    and the hit's witnesses. A `last` round (its run's only judged window) ends at its milestone: its ledgers follow
    at once, and its guard and clips are collected after them, so a later party down is no failure (lead #56)."""
    base = f"saved['{name}_base']"
    seat = ("host", "friend")
    apart = (f"(lambda s: s['standing'] and s['{seat[reviver]}'] - s['{seat[fighter]}'] > {SEPARATION_MARGIN})"
             f"({separation()})")
    near = f"(lambda s: s['{seat[fighter]}'] < {FIGHTER_NEAR})({separation()})"
    landed = f"{host_claims()} > {base}['claims']" if fighter == 1 else f"{boss_hp(0)} < {base}['bossHp']"
    hit = (f"({canonical_hp(fighter)} < {base}['hp'] or "
           f"log_count('[downed] gate intercept', {fighter}) > {base}['downs'])")
    observe = (f"{{'axel': [(a['position']['x'], a['position']['z'], a.get('motionId')) for a in actors(0) if a['name'] == 'B_EX110'], "
               f"'host': {canonical_xz(0)}, 'clone': {clone_xz(0)}}}")
    return [
        # The fighter's own approach, unless it already stands near Axel.
        {"do": "walk_to", "instance": fighter, "body": "self", "x": 0, "z": 0, "targetExpr": away_from_axel(fighter, 250),
         "radius": 60, "timeoutMs": 6000, "until": near, "as": f"{name}_walk_in"},
        # Axel's attacks can knock the reviver off its walk (vuh2912-hp-01: 193 of ~500 units in 4 s), so it has a
        # longer budget and one more walk, which does nothing once the seats already stand apart.
        {"do": "walk_to", "instance": reviver, "body": "self", "x": 0, "z": 0, "targetExpr": away_from_axel(reviver),
         "radius": 60, "timeoutMs": 8000, "as": f"{name}_walk_away"},
        {"do": "walk_to", "instance": reviver, "body": "self", "x": 0, "z": 0, "targetExpr": away_from_axel(reviver),
         "radius": 60, "timeoutMs": 8000, "until": apart, "as": f"{name}_walk_away_retry"},
        {"do": "save", "as": f"{name}_separation", "expr": separation()},
        {"do": "assert", "expr": f"saved['{name}_separation']['standing'] and saved['{name}_separation']['{seat[reviver]}'] - "
                                 f"saved['{name}_separation']['{seat[fighter]}'] > {SEPARATION_MARGIN}",
         "message": f"SETUP FAILURE, not a targeting result: {name} needs both seats standing and the {seat[reviver]} more than "
                    f"{SEPARATION_MARGIN} units farther from Axel than the {seat[fighter]} after both walks"},
        # vuh2912-hp-04: Axel chooses only when his own script searches (at its mode transitions), and he moved ~400
        # units between the separation and the window, so the friend stayed chosen all round. The fighter fights until
        # his selection names it, and the window opens on that choice; else the round stops as a SETUP FAILURE.
        {"do": "revive_guard", "host": fighter, "friend": reviver, "aside": AXEL_ARENA},
        {"do": "fight", "instances": [fighter], "seconds": SELECT_SECONDS, "reach": 120, "as": f"{name}_select",
         "until": selected(fighter)},
        {"do": "assert", "expr": selected(fighter),
         "message": f"SETUP FAILURE (selection), not a targeting result: in {SELECT_SECONDS} s Axel's selection never "
                    f"named the {seat[fighter]} before {name}'s window"},
        # Ione fc9e195f (2): setup hits must not reach the judged window. Wait for a fresh host HP sample after the
        # selection fight that equals Axel's live HP, with every friend claim applied or rejected.
        {"do": "save", "as": f"{name}_settle0", "expr": f"len({boss_samples()})"},
        {"do": "wait_until", "expr": f"len({boss_samples()}) > saved['{name}_settle0'] and {aligned()}",
         "timeoutMs": SETTLE_MS, "pollMs": 250},
        {"do": "save", "as": f"{name}_base", "expr": f"{{'chosen': {targets(fighter)}, 'claims': {host_claims()}, "
                                                    f"'bossHp': {boss_hp(0)}, 'hp': {canonical_hp(fighter)}, "
                                                    f"'downs': log_count('[downed] gate intercept', {fighter})}}"},
        {"do": "save", "as": f"{name}_quiet", "expr": quiet(f"'aligned': {aligned()}")},
        marks(f"m_{name}0"),
        # Ione fc9e195f (1): the window opens on the fighter's selection and settled HP, not merely after them. No
        # Axel hit, claim or selection change between the aligned snapshot and here, so both held at the mark.
        {"do": "assert", "expr": f"saved['{name}_quiet']['aligned'] and {quiet()} == {{k: saved['{name}_quiet'][k] "
                                 f"for k in ('hits', 'claims', 'targets')}} and {selected(fighter)} and {standing()}",
         "message": f"SETUP FAILURE (window), not a targeting result: {name}'s window opened without Axel's selection of "
                    f"the {seat[fighter]}, settled boss HP and both seats standing"},
        STATS_MARK,
        # The clips run as long as the round can, so the round's hits are on film. They run in the background: in
        # vuh2912-hp-06 the step waited ~70 s past the milestone for its 20 s clips to encode, and Axel downed both
        # idle seats inside the window.
        {"do": "fight", "instances": [fighter], "seconds": 20, "reach": 120, "as": name,
         "until": f"{selected(fighter)} and {hit} and {landed}",
         "sample": {"expr": observe, "everyMs": 500},
         "clips": [{"instance": 0, "seconds": 20, "fps": 30, "name": f"{name}_host", "background": True},
                   {"instance": 1, "seconds": 20, "fps": 30, "name": f"{name}_friend", "background": True}]},
        STATS_MARK,
        marks(f"m_{name}1"),
        {"do": "assert", "expr": f"saved['{name}']['until']['reached']",
         "message": f"{name}: in 20 s Axel never chose the {seat[fighter]} with the {seat[fighter]} hit and landing damage"},
        *([] if last else [
            # A downed fighter is revived by the reviver before the next round.
            {"do": "wait_until", "expr": f"log_count('[downed] gate intercept', {fighter}) <= log_count('[downed] revive returned', {fighter})",
             "timeoutMs": 60000, "pollMs": 250},
            {"do": "revive_guard", "on": False, "as": f"{name}_guard"},
            # The round's films are its evidence: a failed or unfinalized pair fails the run (Ione 2990c3d8).
            {"do": "clip_wait", "require": True},
        ]),
    ]


# The ledger phases each fixture round asks of hp_ledger (the downs window, judged alone in B2, asks nothing).
HP_PHASES = {
    "friend_target": {"host": False, "friend": True, "hitOnFriend": True, "cloneZero": True},
    "host_target": {"host": True, "friend": False, "hitOnHost": True},
    "interleaved": {"host": True, "friend": True},
    "downs": {},
}
HP_ROUNDS = {"friend_target": (1, 0), "host_target": (0, 1)}  # name -> (fighter, reviver)


def hp_steps(rounds=("friend_target", "host_target"), interleaved=True, downs=True):
    """The HP fixture's steps: the target `rounds` in order, then `interleaved`, then the `downs`. Every included
    window is judged by the ledgers. Without a fighting window (B2) the hp_ledger judges the downs window alone,
    after it, and pins the boss lifetime the target ledger needs."""
    # A boss hit (either way) or an Axel claim anywhere before the seat's release line. The friend anchors on its
    # own release, which follows the host's admitted release.
    boss_touch = "\\[hit\\] [^\\n]*(?:attacker=B_EX110 |victim=B_EX110@)"
    lease_host = (f"len(log_matches(r'(?P<x>{boss_touch}|\\[enemysync\\] host hit claim [^\\n]*objectId={AXEL_OBJECT} )"
                  f"[\\s\\S]*?\\[combat-admission\\] released admitted ', 0)) == 0")
    lease_friend = (f"len(log_matches(r'(?P<x>{boss_touch}|\\[enemysync\\] client hit claim [^\\n]*objectId={AXEL_OBJECT} )"
                    f"[\\s\\S]*?{RELEASED_AXEL}', 1)) == 0")
    claim_drop = (f"sum(int(r['b']) - int(r['a']) for r in log_matches(r'\\[enemysync\\] host hit claim [^\\n]*objectId={AXEL_OBJECT} "
                  f"[^\\n]*beforeHp=(?P<b>\\d+) afterHp=(?P<a>\\d+) nativeOk=1', 0)[saved['il_base']['claims']:])")
    living = "[[(a['hp'], a.get('maxHp')) for a in actors(i) if a['name'] == 'B_EX110' and a.get('hp', 0) > 0] for i in (0, 1)]"
    fought = [*rounds, *(["interleaved"] if interleaved else [])]
    judged = fought or ["downs"]
    # Lead #56: a split judges one round, and its ledgers run right after it, so a later party down is no failure.
    single = len(fought) == 1 and not downs
    # vuh2912-hp-07: B2 (the downs alone) closes and judges its window right after owner_downs too; its 42 s clip_wait
    # left both seats idle with the guards off, and Axel took the party to a game over before the end mark.
    downs_only = downs and not fought
    ledger_marks = [f"m_{name}{k}" for name in judged for k in (0, 1)]
    opening = [
        *arrival([HP_ENV, HP_ENV]),
        {"do": "wait_until", "expr": f"log_count('{RELEASED_AXEL}', 0) > 0 and log_count('{RELEASED_AXEL}', 1) > 0",
         "timeoutMs": 60000, "pollMs": 100},
        {"do": "wait_until", "expr": "log_count('[combat-admission] released admitted ', 0) > 0", "timeoutMs": 30000, "pollMs": 100},
        {"do": "assert", "expr": f"{lease_host} and {lease_friend}",
         "message": "the VUH-2897/2906 lease: no Axel damage or Axel hit on either seat before both admitted arrivals released the fight"},
        {"do": "save", "as": "boss_start", "expr": living},
        {"do": "assert", "expr": "all(len(seat) == 1 for seat in saved['boss_start'])", "message": "each seat must hold exactly one living Axel"},
        {"do": "wait_until", "expr": f"len(log_matches(r'\\[enemy-mirror\\] take-over frame=\\d+ netId=\\d+ .*?objectId={AXEL_OBJECT}', 1)) > 0",
         "timeoutMs": 20000, "pollMs": 250},
    ]
    fighting = [step for name in rounds for step in target_round(name, *HP_ROUNDS[name], last=single)]
    if interleaved:
        fighting += [
            {"do": "assert", "expr": standing(),
             "message": "SETUP FAILURE (window), not an HP result: interleaved needs both seats standing"},
            marks("m_interleaved0"),
            STATS_MARK,
            {"do": "save", "as": "il_base", "expr": f"{{'claims': {host_claims()}, 'bossHp': {boss_hp(0)}}}"},
            # Both fight until a friend claim and a host native hit (an HP drop beyond the claims) both landed. The
            # clips run in the background, as the target rounds' (vuh2912-hp-06). vuh2912-hp-10 (lead #84): in 10 s the
            # host pressed 3 times and landed nothing. Axel's full-screen fire filled about 2 s of it and he spent most
            # of the rest at the friend's side, so the window is 20 s, still ending at the first of both hits.
            {"do": "fight", "instances": [0, 1], "seconds": 20, "reach": 120, "as": "interleaved",
             "until": f"{host_claims()} > saved['il_base']['claims'] and {boss_hp(0)} < saved['il_base']['bossHp'] - {claim_drop}",
             "clips": [{"instance": 0, "seconds": 22, "fps": 30, "name": "interleaved_host", "background": True},
                       {"instance": 1, "seconds": 22, "fps": 30, "name": "interleaved_friend", "background": True}]},
            STATS_MARK,
            marks("m_interleaved1"),
            *([] if single else [{"do": "clip_wait", "require": True}]),
        ]
    captures = [{"do": "capture", "instance": 0, "name": "settled_host"},
                {"do": "capture", "instance": 1, "name": "settled_friend"}]
    settle = [
        {"do": "wait", "ms": 4000},
        {"do": "save", "as": "boss_settled", "expr": living},
        *([] if single or downs_only else captures),
        {"do": "hp_ledger", "objectId": AXEL_OBJECT, "program": 137, "marks": ledger_marks, "as": "ledger", "require": False,
         "window": [ledger_marks[0], ledger_marks[-1]],
         "phases": [{"name": name, "from": f"m_{name}0", "to": f"m_{name}1", **HP_PHASES[name]} for name in judged]},
        {"do": "assert", "expr": "len(saved['boss_settled'][0]) == 1 and saved['boss_settled'][0] == saved['boss_settled'][1]",
         "message": "after delivery settles both seats must show one living Axel with the host's HP and max HP"},
        {"do": "assert", "expr": "saved['ledger']['pass']", "message": "the boss HP ledger must join every Axel HP change to exactly one hit"},
        # Mirror mode: target choice only. No HP-only forwarding to the owner, no owner-side cancel.
        {"do": "assert", "expr": "log_count('[enemy-target] forward ', 0) == 0 and log_count('[enemy-target] remote-hit ', 1) == 0 "
                                 "and log_count('[enemy-target] veto ', 1) == 0",
         "message": "the boss's hits on the friend must stay native on the friend: no forwarded RemoteHit or vetoed local hit"},
    ]
    downing = [
        # A downed seat is no candidate: one down and partner revive per owner, bracketed for the target ledger.
        marks("m_downs0"),
        STATS_MARK,
        # vuh2912-hp-08 (lead #72): Axel hit the reviver 14 times in 26 s and broke every hold. Each forced down waits,
        # at most 20 s and labelled, until every living enemy is 500 units from both seats' canonical players: just
        # over the 90th percentile (479) of how far Axel travelled in the second before each of his 96 hits on a seat
        # in hp-05, hp-07 and hp-08 (max 591). Lead #82: through killWhen, owner_downs' one mechanism (VUH-2931).
        {"do": "owner_downs", "owners": [1, 0], "as": "owner_downs", "clipSeconds": 12, "timeoutMs": 240000,
         "aside": AXEL_ARENA, "killWhen": "enemy_clear(500)", "killWhenMs": 20000},
        *([] if downs_only else [{"do": "clip_wait"}]),
        STATS_MARK,
        marks("m_downs1"),
        {"do": "assert", "expr": "all(saved['owner_downs']['owners'][k] for k in ('0', '1'))",
         "message": "each owner must be downed once and revived by the other seat"},
        {"do": "save", "as": "boss_targets",
         "expr": f"[m['l'] for m in log_matches(r'(?P<l>\\[enemy-target\\] boss-target [^\\n]*objectId={AXEL_OBJECT} [^\\n]*)', 0)]"},
    ] if downs else []
    judging = [
        {"do": "target_ledger", "objectId": AXEL_OBJECT, "ledger": "ledger", "as": "targets", "require": False,
         "rounds": [{"name": name, "from": f"m_{name}0", "to": f"m_{name}1", "seat": HP_ROUNDS[name][0]} for name in rounds],
         **({"downs": {"from": "m_downs0", "to": "m_downs1"}} if downs else {})},
        {"do": "assert", "expr": "saved['targets']['pass']",
         "message": "Axel must choose each round's seat before its witnessed native hit, never choose an ineligible "
                    "candidate, and drop each downed seat from its candidates"},
        {"do": "coop_verdict", "as": "verdict", "require": True, "arrivalTimeoutMs": 20000},
    ]
    if single:
        # Judged, then the guard off and the round's clips and stills collected: nothing after the verdict can fail
        # on a party down or game over.
        tail = [*([{"do": "revive_guard", "on": False, "as": f"{rounds[0]}_guard"}] if rounds else []),
                {"do": "clip_wait", "require": True}, *captures]
        return opening + fighting + settle + judging + tail
    if downs_only:
        # Judged, then owner_downs' clips (diagnostics, not required) and the stills.
        return opening + downing + settle + judging + [{"do": "clip_wait"}, *captures]
    return opening + fighting + settle + downing + judging


# vuh2912-hp-02/03: chained on PC1's 22-HP saves, the later rounds and the downs fall to Axel's pressure, and the
# friend round has passed twice. Lead #37 splits the remaining evidence: B1 the host round and interleaved, B2 the
# downs, each judged alone and ending at its own milestone. vuh2912-hp-06: B1's host round passed, then the party went
# down before interleaved and its ledgers ever ran. Lead #56 splits B1 again: one judged round per run, its ledgers
# right after it.
HP_SPLITS = {
    f"{HP_NAME}_b1_host": (dict(rounds=("host_target",), interleaved=False, downs=False),
                           "B1-host of the split: the host target round alone, judged by both ledgers right after it; "
                           "no interleaved, no downs."),
    f"{HP_NAME}_b1_inter": (dict(rounds=(), interleaved=True, downs=False),
                            "B1-inter of the split: interleaved right after the take-over, judged by both ledgers right "
                            "after it; no target round, no downs."),
    f"{HP_NAME}_b2": (dict(rounds=(), interleaved=False, downs=True),
                      "B2 of the split: owner_downs right after the release, the hp_ledger over its window, and the "
                      "target ledger's downs; no target rounds."),
}

if __name__ == "__main__":
    description = __doc__.split("\n\n", 2)[2].strip().replace("\n", " ")
    for name, selection in VARIANTS.items():
        seats = f" Selections: host 0x{selection[0]:X}, friend 0x{selection[1]:X}."
        if name == "net_prologue_mansion_axel_form_sora_host":
            seats += SORA_HOST_NOTE
        scenario = {"name": name, "description": description + seats, "playerDefaults": True, "protect": [],
                    "steps": steps_for(selection)}
        out = Path(__file__).with_name(f"{name}.json")
        out.write_text(json.dumps(scenario, indent=1) + "\n", encoding="utf-8")
        print(out)
    for name, (kwargs, note) in {HP_NAME: ({}, ""), **HP_SPLITS}.items():
        scenario = {"name": name, "description": f"{HP_DESCRIPTION} {note}".strip(), "playerDefaults": True,
                    "protect": [], "steps": hp_steps(**kwargs)}
        out = Path(__file__).with_name(f"{name}.json")
        out.write_text(json.dumps(scenario, indent=1) + "\n", encoding="utf-8")
        print(out)
