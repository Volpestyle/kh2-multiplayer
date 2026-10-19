"""VUH-2464: two Roxases from New Game's first controllable frame (writes net_prologue_party.json).

PC1, two instances. Relay and both runtimes connect at the title; both games start NEW GAME with
the default rows. The host plays the opening (Cross only while its own field gate is closed and no
menu is open); the friend is held at its own blocked opening exit and follows the host's first
prologue field. Checks: both seats declare and acknowledge Roxas during the opening; the
first-field barrier releases on both with two P_EX110 and two weapons each; owner-only movement
seen from both seats; one synthetic hit per seat leaves the other seat's HP and SAVE ranges
unchanged; the Armor Shop event's same-room reload (evt95 -> evt97) rebinds both seats before
control returns. Never saves.
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


def advance_until(expr, rounds, tag):
    """Cross for each seat only while its own gate is closed and no menu is open; snap every 8 rounds."""
    out = []
    for n in range(rounds):
        for i in (0, 1):
            out.append({"do": "kh2ctl", "instance": i, "argsExpr":
                        f"['player-press', '--button', 'cross', '--duration-ms', '150'] if (not ({field(i)}) "
                        f"and peek('0x7435D0','u8',{i}) == 255 and not ({expr})) else ['peek', '--rva', '0x9BA8D0:u8']"})
        out.append({"do": "wait", "ms": 2500})
        if n % 8 == 7:
            out += snap(f"{tag}{n // 8:02d}", capture=(n % 32 == 31))
    return out


released = ("log_count('[prologue] control released at=02/07 evt={evt}', 0) > 0 and "
            "log_count('[prologue] control released at=02/07 evt={evt}', 1) > 0")
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
for n in range(20):
    steps += [{"do": "wait", "ms": 15000}] + snap(f"op{n:02d}", capture=(n % 5 == 4))
# The 2/1 evt56 title scene can stall near frame 12,600 (VUH-2338); allow for a slow opening.
steps.append({"do": "wait_until", "expr": "location(0)[1] == 2", "timeoutMs": 420000, "pollMs": 1000})
steps += advance_until("location(0) == [2, 7, 0, 95, 95, 95]", 130, "ev")
steps += [{"do": "wait_until", "expr": "location(0) == [2, 7, 0, 95, 95, 95] and location(1) == [2, 7, 0, 95, 95, 95]",
           "timeoutMs": 240000, "pollMs": 1000},
          {"do": "wait_until", "expr": released.format(evt=95), "timeoutMs": 180000, "pollMs": 1000},
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
steps += advance_until("location(0) == [2, 7, 0, 97, 97, 97]", 40, "rl")
steps += [{"do": "wait_until", "expr": "location(0) == [2, 7, 0, 97, 97, 97] and location(1) == [2, 7, 0, 97, 97, 97]",
           "timeoutMs": 120000, "pollMs": 1000},
          {"do": "wait_until", "expr": released.format(evt=97), "timeoutMs": 120000, "pollMs": 1000},
          {"do": "wait", "ms": 2000}] + snap("reload97") + census("reload97") + [
    {"do": "save", "as": "reload97_log0", "expr": lines("\\[(?:prologue|missing-party\\] born)", 0)},
    {"do": "save", "as": "reload97_log1", "expr": lines("\\[(?:prologue|missing-party\\] born|warp\\] client)", 1)},
    {"do": "clip", "instance": 0, "seconds": 5, "fps": 30, "name": "reload97_host"},
    {"do": "clip", "instance": 1, "seconds": 5, "fps": 30, "name": "reload97_friend"}]
scenario = {"name": "net_prologue_party", "description": (__doc__ or "").strip().split("\n\n")[1].replace("\n", " "),
            "playerDefaults": True, "steps": steps}
Path(__file__).with_name("net_prologue_party.json").write_text(json.dumps(scenario, indent=1), encoding="utf-8")

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
