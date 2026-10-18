import json
GATES = "[peek('0x9BA8D0','u8'), peek('0x7435D0','u8'), peek('0xB65210','i32'), peek('0x2A11478','u32'), peek('0x2A1147C','u32')]"
FIELD = ("peek('0x9BA8D0','u8') != 0 and peek('0x7435D0','u8') == 255 and peek('0xB65210','i32') == 0 "
         "and peek('0x2A11478','u32') == 0 and peek('0x2A1147C','u32') == 0")
def snap(tag):
    return [{"do": "capture", "instance": 0, "name": tag},
            {"do": "save", "as": f"{tag}_loc", "expr": "location(0)"}, {"do": "save", "as": f"{tag}_gates", "expr": GATES}]
steps = [{"do": "boot", "newGame": True, "initTimeoutMs": 60000},
         {"do": "wait", "ms": 8000}] + snap("ng1_game_mode")
# Standard Mode is the default row; Cross until the vibration screen shows (checked in captures).
steps += [{"do": "press", "instance": 0, "button": "cross", "ms": 150}, {"do": "wait", "ms": 4000}] + snap("ng2_vibration")
steps += [{"do": "press", "instance": 0, "button": "cross", "ms": 150}, {"do": "wait", "ms": 4000}] + snap("ng3_confirm")
# "Begin with these settings?" (Game Mode Standard, Vibration ON): Yes is the default row.
steps += [{"do": "press", "instance": 0, "button": "cross", "ms": 150}, {"do": "wait", "ms": 15000}] + snap("ng4_opening")
# No input through the 2/1 events (they run on their own); look at the 2/2 Usual Spot event first.
steps.append({"do": "wait_until", "expr": "location(0)[1] == 2", "timeoutMs": 420000, "pollMs": 1000})
for n in range(6):
    steps += [{"do": "wait", "ms": 5000}] + snap(f"uss{n}")
# Then Cross only while the field gate is closed (event, cutscene or menu); a live field gets no input.
for n in range(72):
    steps.append({"do": "kh2ctl", "instance": 0, "argsExpr": "['peek', '--rva', '0x9BA8D0:u8'] if (" + FIELD + ") else "
                  "['player-press', '--button', 'cross', '--duration-ms', '150']"})
    steps.append({"do": "wait", "ms": 2500})
    if n % 4 == 3:
        steps += snap(f"ev{n // 4:02d}")
# No further input: wait for the first live field frame (no menu, cutscene or event), then record it as is.
steps += [{"do": "wait_until", "expr": FIELD, "timeoutMs": 120000, "pollMs": 250},
          {"do": "save", "as": "first_field_t", "expr": "location(0)"}] + snap("ng5_first_field") + [
          {"do": "progress_snapshot", "instances": [0], "as": "first_field_progress"},
          {"do": "save", "as": "first_field_actors", "expr": "[(a['name'], a.get('objectId'), a.get('hp'), a['position']) for a in actors(0)][:16]"},
          {"do": "save", "as": "first_field_room", "expr": "room(0)"},
          {"do": "wait", "ms": 3000}] + snap("ng6_held_3s")
s = {"name": "newgame_first_field", "description": "VUH-2338 on PC1: clean boot, NEW GAME, Standard Mode (default row), vibration ON (default row), Begin with these settings: Yes (default row), then Cross only while the field gate is closed (story dialogs), never in a live field, until the first live field frame (no menu, cutscene or event); records location, gates, progress snapshot and actors there and 3 s later with input still held. Never saves; DLL save guard on.", "steps": steps}
open("newgame_first_field.json", "w", encoding="utf-8").write(json.dumps(s, indent=1))
