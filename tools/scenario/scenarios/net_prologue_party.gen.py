"""VUH-2464: two Roxases from New Game's first controllable frame (writes net_prologue_party.json).

PC1, two instances. Relay and both runtimes connect at the title; both games start NEW GAME with
the default rows. The host plays the opening (Cross only while its own field gate is closed and no
menu is open); the friend is held at its own blocked opening exit and follows the host's first
prologue field. Checks: both seats declare and acknowledge Roxas during the opening; the
first-field barrier releases on both with two P_EX110 and two weapons each; owner-only movement
seen from both seats; one synthetic hit per seat leaves the other seat's HP and SAVE ranges
unchanged; the Armor Shop event's same-room reload (evt95 -> evt97) rebinds both seats before
control returns; the Day 1 lock-on tutorial (Accessory Shop talk, evt99, cat lock-on) takes the
first room change to the Sandlot, where both seats are rebuilt, no field was left unheld and
movement stays owner-only. Never saves.
"""
import json
from pathlib import Path


def gates(i):
    return (f"[peek('0x9BA8D0','u8',{i}), peek('0x7435D0','u8',{i}), peek('0xB65210','i32',{i}), "
            f"peek('0x2A11478','u32',{i}), peek('0x2A1147C','u32',{i})]")


def field(i):
    return (f"peek('0x9BA8D0','u8',{i}) != 0 and peek('0x7435D0','u8',{i}) == 255 and peek('0xB65210','i32',{i}) == 0 "
            f"and peek('0x2A11478','u32',{i}) == 0 and peek('0x2A1147C','u32',{i}) == 0")


def canon(i):
    return f"peek('0x2A105D0','u64',{i})"


def roxas(i):
    return f"[(a['address'], a.get('hp'), a['position']) for a in actors({i}) if a['name'] == 'P_EX110']"


def clone_addr(i):
    return (f"[a['address'] for a in actors({i}) if a['name'] == 'P_EX110' and "
            f"int(a['address'], 16) != int({canon(i)}, 16)][0]")


def lines(pattern, i, last=40):
    return f"[m['l'] for m in log_matches(r'(?P<l>{pattern}[^\\n]*)', {i})][-{last}:]"


def snap(tag, capture=True):
    out = []
    for i in (0, 1):
        if capture:
            out.append({"do": "capture", "instance": i, "name": f"{tag}_{i}"})
        out += [{"do": "save", "as": f"{tag}_loc{i}", "expr": f"location({i})"},
                {"do": "save", "as": f"{tag}_gates{i}", "expr": gates(i)}]
    return out


def census(tag):
    out = []
    for i in (0, 1):
        out += [{"do": "save", "as": f"{tag}_roxas{i}", "expr": roxas(i)},
                {"do": "save", "as": f"{tag}_canonical{i}", "expr": canon(i)}]
    return out


def both(step):
    return [dict(step, instance=0), dict(step, instance=1)]


def gate_closed(i):
    return f"(not ({field(i)}) and peek('0x7435D0','u8',{i}) == 255)"


def advance_until(expr, rounds, tag, skip=False):
    """Advance each seat's own scripted scenes while its field gate is closed; snap every 8 rounds.

    Without `skip`, Cross advances dialog only while the gate is closed and no menu is open (the full,
    unskipped playback that acceptance keeps). With `skip` (development runs, James 2026-10-19), a seat
    first pauses and picks Skip Scene (Start, Down, Cross, the native event skip of net_host_cutscene_skip)
    while its cutscene runs. The friend skips only its own opening, before its first exit is blocked; after
    that it follows the host and must not hold a menu open.
    """
    out = []
    for n in range(rounds):
        for i in (0, 1):
            if skip:
                own = " and log_count('client native exit blocked', 1) == 0" if i == 1 else ""
                menu_in_scene = f"(peek('0x7435D0','u8',{i}) != 255 and peek('0xB65210','i32',{i}) != 0{own})"
                out += [{"do": "kh2ctl", "instance": i, "argsExpr":
                         f"['player-press', '--button', 'start', '--duration-ms', '100'] if ({gate_closed(i)} "
                         f"and peek('0xB65210','i32',{i}) != 0 and not ({expr}){own}) else ['peek', '--rva', '0x9BA8D0:u8']"},
                        {"do": "wait", "ms": 700},
                        {"do": "kh2ctl", "instance": i, "argsExpr":
                         f"['player-press', '--button', 'down', '--duration-ms', '100'] if {menu_in_scene} else ['peek', '--rva', '0x9BA8D0:u8']"},
                        {"do": "wait", "ms": 300},
                        {"do": "kh2ctl", "instance": i, "argsExpr":
                         f"['player-press', '--button', 'cross', '--duration-ms', '100'] if {menu_in_scene} else ['peek', '--rva', '0x9BA8D0:u8']"},
                        {"do": "wait", "ms": 300}]
            out.append({"do": "kh2ctl", "instance": i, "argsExpr":
                        f"['player-press', '--button', 'cross', '--duration-ms', '150'] if ({gate_closed(i)} "
                        f"and not ({expr})) else ['peek', '--rva', '0x9BA8D0:u8']"})
        out.append({"do": "wait", "ms": 1200 if skip else 2500})
        if n % 8 == 7:
            out += snap(f"{tag}{n // 8:02d}", capture=(n % 32 == 31))
    return out


released = ("log_count('[prologue] control released at=02/{room:02X} evt={evt}', 0) > 0 and "
            "log_count('[prologue] control released at=02/{room:02X} evt={evt}', 1) > 0")
def build(skip):
    steps = [{"do": "boot", "newGame": True, "initTimeoutMs": 60000}, {"do": "boot", "newGame": True, "initTimeoutMs": 60000},
             {"do": "relay", "port": 27802, "args": ["--bind", "127.0.0.1"]},
             {"do": "runtime", "instance": 0, "role": "player"}, {"do": "runtime", "instance": 1, "role": "friend1"},
             {"do": "hang_check", "instance": 0, "on": False}, {"do": "hang_check", "instance": 1, "on": False},
             {"do": "wait", "ms": 8000}] + snap("ng1_game_mode")
    for tag in ("ng2_vibration", "ng3_confirm"):
        steps += both({"do": "press", "button": "cross", "ms": 150}) + [{"do": "wait", "ms": 4000}] + snap(tag)
    steps += both({"do": "press", "button": "cross", "ms": 150}) + [{"do": "wait", "ms": 15000}] + snap("ng4_opening")
    steps += [{"do": "wait_until", "expr": "log_count('[prologue] seat accepted', 0) >= 2 and log_count('[prologue] seat accepted', 1) >= 2",
               "timeoutMs": 120000, "pollMs": 2000},
              {"do": "save", "as": "seats_opening0", "expr": lines("\\[prologue\\] seat accepted", 0, 6)},
              {"do": "save", "as": "seats_opening1", "expr": lines("\\[prologue\\] seat accepted", 1, 6)}]
    if skip:
        steps += advance_until("location(0) == [2, 7, 0, 95, 95, 95]", 50, "ev", skip=True)
    else:
        for n in range(20):
            steps += [{"do": "wait", "ms": 15000}] + snap(f"op{n:02d}", capture=(n % 5 == 4))
        # The 2/1 evt56 title scene can stall near frame 12,600 (VUH-2338); allow for a slow opening.
        steps.append({"do": "wait_until", "expr": "location(0)[1] == 2", "timeoutMs": 420000, "pollMs": 1000})
        steps += advance_until("location(0) == [2, 7, 0, 95, 95, 95]", 130, "ev")
    steps += [{"do": "wait_until", "expr": "location(0) == [2, 7, 0, 95, 95, 95] and location(1) == [2, 7, 0, 95, 95, 95]",
               "timeoutMs": 240000, "pollMs": 1000},
              {"do": "wait_until", "expr": released.format(room=7, evt=95), "timeoutMs": 180000, "pollMs": 1000},
              {"do": "wait", "ms": 2000}] + snap("first_control") + census("first_control") + [
        {"do": "save", "as": "first_control_actors0", "expr": "[(a['name'], a.get('objectId'), a.get('hp')) for a in actors(0)]"},
        {"do": "save", "as": "first_control_actors1", "expr": "[(a['name'], a.get('objectId'), a.get('hp')) for a in actors(1)]"},
        {"do": "save", "as": "first_control_log0", "expr": lines("\\[(?:prologue|missing-party\\] born)", 0)},
        {"do": "save", "as": "first_control_log1", "expr": lines("\\[(?:prologue|missing-party\\] born|warp\\] client)", 1)},
        {"do": "clip", "instance": 1, "seconds": 4, "fps": 30, "name": "first_control_friend"},
        # Owner-only input, each seat filmed from the other.
        {"do": "clip", "instance": 1, "seconds": 8, "fps": 30, "name": "friend_view_host_moves", "background": True},
        {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/prologue_move.json", "seconds": 60, "name": "host_move", "allowStuck": True},
        {"do": "clip_wait"}, {"do": "wait", "ms": 1000}] + census("after_host_move") + [
        {"do": "clip", "instance": 0, "seconds": 8, "fps": 30, "name": "host_view_friend_moves", "background": True},
        {"do": "pilot", "instance": 1, "route": "tools/rig/route_pilot/routes/prologue_move.json", "seconds": 60, "name": "friend_move", "allowStuck": True},
        {"do": "clip_wait"}, {"do": "wait", "ms": 1000}] + census("after_friend_move") + [
        # One synthetic native hit per seat (kh2ctl hit damage): the host's private clone of the friend,
        # then the friend's own Roxas. The other seat's HP and both SAVE ranges must not change.
        {"do": "progress_snapshot", "instances": [0, 1], "as": "save_before_damage"},
        {"do": "kh2ctl", "instance": 0, "argsExpr": f"['hit', 'damage', '--victim', {clone_addr(0)}, '--amount', '1']"},
        {"do": "wait", "ms": 1500}] + census("after_clone_damage") + [
        {"do": "kh2ctl", "instance": 1, "argsExpr": f"['hit', 'damage', '--victim', {canon(1)}, '--amount', '1']"},
        {"do": "wait", "ms": 1500}] + census("after_friend_damage") + [
        {"do": "progress_snapshot", "instances": [0, 1], "as": "save_after_damage", "compare": "save_before_damage"},
        # Same-room event reload: Armor Shop event 104 reloads Tram Common as evt97.
        {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/prologue_armor_shop.json", "seconds": 60, "name": "host_armor_shop", "allowStuck": True}]
    steps += advance_until("location(0) == [2, 7, 0, 97, 97, 97]", 40, "rl", skip=skip)
    steps += [{"do": "wait_until", "expr": "location(0) == [2, 7, 0, 97, 97, 97] and location(1) == [2, 7, 0, 97, 97, 97]",
               "timeoutMs": 120000, "pollMs": 1000},
              {"do": "wait_until", "expr": released.format(room=7, evt=97), "timeoutMs": 120000, "pollMs": 1000},
              {"do": "wait", "ms": 2000}] + snap("reload97") + census("reload97") + [
        {"do": "save", "as": "reload97_log0", "expr": lines("\\[(?:prologue|missing-party\\] born)", 0)},
        {"do": "save", "as": "reload97_log1", "expr": lines("\\[(?:prologue|missing-party\\] born|warp\\] client)", 1)},
        {"do": "clip", "instance": 0, "seconds": 5, "fps": 30, "name": "reload97_host"},
        {"do": "clip", "instance": 1, "seconds": 5, "fps": 30, "name": "reload97_friend"},
        # Day 1 lock-on tutorial: talk at the Accessory Shop (event 106 reloads the room as evt99) ...
        {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/prologue_accessory_shop.json", "seconds": 60,
         "name": "host_accessory_shop", "allowStuck": True}]
    for _ in range(4):
        steps += [{"do": "kh2ctl", "instance": 0, "argsExpr":
                   f"['player-press', '--button', 'triangle', '--duration-ms', '120'] if (location(0) == [2, 7, 0, 97, 97, 97] "
                   f"and {field(0)} and peek('0x2A11162','u16',0) == 31) else ['peek', '--rva', '0x9BA8D0:u8']"},
                  {"do": "wait", "ms": 2000}]
    steps += advance_until("location(0) == [2, 7, 0, 99, 99, 99]", 40, "as", skip=skip)
    steps += [{"do": "wait_until", "expr": "location(0) == [2, 7, 0, 99, 99, 99] and location(1) == [2, 7, 0, 99, 99, 99]",
               "timeoutMs": 120000, "pollMs": 1000},
              {"do": "wait_until", "expr": released.format(room=7, evt=99), "timeoutMs": 120000, "pollMs": 1000},
              {"do": "wait", "ms": 2000}] + snap("reload99") + census("reload99") + [
        # ... then lock on to the cat: event 108 is the prologue's first room change, to the Sandlot.
        {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/prologue_cat.json", "seconds": 90,
         "name": "host_cat", "allowStuck": True}]
    steps += advance_until("location(0) == [2, 4, 0, 76, 76, 76]", 60, "ct", skip=skip)
    steps += [{"do": "wait_until", "expr": "location(0) == [2, 4, 0, 76, 76, 76] and location(1) == [2, 4, 0, 76, 76, 76]",
               "timeoutMs": 180000, "pollMs": 1000},
              {"do": "wait_until", "expr": released.format(room=4, evt=76), "timeoutMs": 120000, "pollMs": 1000},
              {"do": "wait", "ms": 2000}] + snap("sandlot") + census("sandlot") + [
        {"do": "save", "as": "sandlot_actors0", "expr": "[(a['name'], a.get('objectId'), a.get('hp')) for a in actors(0)]"},
        {"do": "save", "as": "sandlot_actors1", "expr": "[(a['name'], a.get('objectId'), a.get('hp')) for a in actors(1)]"},
        {"do": "save", "as": "sandlot_log0", "expr": lines("\\[(?:prologue|missing-party\\] born)", 0)},
        {"do": "save", "as": "sandlot_log1", "expr": lines("\\[(?:prologue|missing-party\\] born|warp\\] client)", 1)},
        # Both seats rebuilt at the new room: two Roxases each, and no open prologue field anywhere in the
        # run played native solo and unheld (the DLL logs each one as an unpinned field).
        {"do": "assert", "expr": "len([a for a in actors(0) if a['name'] == 'P_EX110']) == 2 and "
                                 "len([a for a in actors(1) if a['name'] == 'P_EX110']) == 2 and "
                                 "log_count('[prologue] unpinned field', 0) == 0 and "
                                 "log_count('[prologue] unpinned field', 1) == 0",
         "message": "a seat reached the Sandlot without both Roxases, or a prologue field was left unheld"},
        {"do": "clip", "instance": 0, "seconds": 5, "fps": 30, "name": "sandlot_host"},
        {"do": "clip", "instance": 1, "seconds": 5, "fps": 30, "name": "sandlot_friend"},
        {"do": "clip", "instance": 1, "seconds": 8, "fps": 30, "name": "sandlot_friend_view_host_moves", "background": True},
        {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/prologue_sandlot_move.json", "seconds": 60,
         "name": "sandlot_host_move", "allowStuck": True},
        {"do": "clip_wait"}, {"do": "wait", "ms": 1000}] + census("sandlot_after_host_move") + [
        {"do": "clip", "instance": 0, "seconds": 8, "fps": 30, "name": "sandlot_host_view_friend_moves", "background": True},
        {"do": "pilot", "instance": 1, "route": "tools/rig/route_pilot/routes/prologue_sandlot_move.json", "seconds": 60,
         "name": "sandlot_friend_move", "allowStuck": True},
        {"do": "clip_wait"}, {"do": "wait", "ms": 1000}] + census("sandlot_after_friend_move")
    return steps


steps = build(False)
scenario = {"name": "net_prologue_party", "description": (__doc__ or "").strip().split("\n\n")[1].replace("\n", " "),
            "playerDefaults": True, "steps": steps}
Path(__file__).with_name("net_prologue_party.json").write_text(json.dumps(scenario, indent=1), encoding="utf-8")
# Development variant: the same checks with the opening and event cutscenes skipped natively (iteration only;
# the unskipped fixture above stays the acceptance evidence for the engine-driven scenes).
Path(__file__).with_name("net_prologue_party_skip.json").write_text(json.dumps(
    dict(scenario, name="net_prologue_party_skip", steps=build(True),
         description=scenario["description"] + " Development variant: cutscenes skipped natively (Start, Down, Cross)."),
    indent=1), encoding="utf-8")

# Refusal: the friend selects Sora, which the prologue layout does not admit yet (VUH-1512). Both seats
# must hold at the first field with that reason: no clone, no Sora stand-in, no second load, no control.
both_at_95 = "location(0) == [2, 7, 0, 95, 95, 95] and location(1) == [2, 7, 0, 95, 95, 95]"
opening = steps[:next(n for n, st in enumerate(steps) if st.get("expr") == both_at_95)]
opening[1] = dict(opening[1], env={"KH2COOP_PROFILE": "0x54"})  # the friend's boot
refusal = opening + [
    {"do": "wait_until", "expr": both_at_95, "timeoutMs": 240000, "pollMs": 1000},
    {"do": "wait_until", "expr": "log_count('[prologue] hold reason=unqualified-profile', 0) > 0 and "
                                 "log_count('[prologue] hold reason=unqualified-profile', 1) > 0", "timeoutMs": 120000, "pollMs": 1000},
    {"do": "wait", "ms": 3000}] + snap("held") + census("held") + [
    {"do": "save", "as": "held_actors0", "expr": "[(a['name'], a.get('objectId'), a.get('hp')) for a in actors(0)]"},
    {"do": "save", "as": "held_actors1", "expr": "[(a['name'], a.get('objectId'), a.get('hp')) for a in actors(1)]"},
    {"do": "save", "as": "held_log0", "expr": lines("\\[(?:prologue|missing-party)\\]", 0)},
    {"do": "save", "as": "held_log1", "expr": lines("\\[(?:prologue|missing-party|warp)\\]", 1)},
    {"do": "assert", "expr": "len([a for a in actors(0) if a['name'] == 'P_EX110']) == 1 and "
                             "len([a for a in actors(1) if a['name'] == 'P_EX110']) == 1 and "
                             "not [a for a in actors(0) + actors(1) if a['name'] == 'P_EX100']",
     "message": "a refused profile built a clone or a Sora stand-in"},
    {"do": "pilot", "instance": 0, "route": "tools/rig/route_pilot/routes/prologue_move.json", "seconds": 60, "name": "host_try_move", "allowStuck": True},
    {"do": "wait", "ms": 1000}] + census("after_try_move") + [
    {"do": "assert", "expr": "saved['held_roxas0'] == saved['after_try_move_roxas0'] and log_count('[prologue] control released', 0) == 0",
     "message": "the held host moved or was released"},
    {"do": "clip", "instance": 0, "seconds": 4, "fps": 30, "name": "held_host"}]
Path(__file__).with_name("net_prologue_party_refusal.json").write_text(json.dumps(
    {"name": "net_prologue_party_refusal",
     "description": "VUH-2464 refusal on PC1: as net_prologue_party, but the friend selects Sora (KH2COOP_PROFILE=0x54), "
                    "which the prologue layout does not admit. Both seats must hold at Tram Common evt95 with reason "
                    "unqualified-profile, build no clone or Sora stand-in, and not move under input. Never saves.",
     "playerDefaults": True, "steps": refusal}, indent=1), encoding="utf-8")
