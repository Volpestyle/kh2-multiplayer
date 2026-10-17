"""Route pilot (VUH-2340): plays one seat with real OS keyboard input.

It reads the owned game's state (read-only) and sends scan-code key events
with SendInput to that game's focused window, so the route is played through
native doors and events. Nothing here warps, teleports or writes memory.

Modes (run from the Session-1 desk bridge, normally through the scenario
runner's `pilot` step, which owns the game and cleans up):
  --route R.json   play data-driven route steps; the stuck detector stops it
  --teach DIR      execute command files dropped in DIR (route discovery)
  --release-all    send key-up for every pilot key and exit

Every door crossing is logged with the last position in the old room, which
is where that room's door trigger is: route waypoints come from those logs.
"""
from __future__ import annotations

import argparse
import ctypes
import ctypes.wintypes as wt
import hashlib
import json
import math
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KH2CTL = ROOT / "build" / "tools" / "kh2ctl" / "Release" / "kh2ctl.exe"

# exe-relative addresses (runtime/include/kh2coop/KH2Offsets.hpp)
NOW, IN_FIELD, OPEN_MENU = 0x717008, 0x9BA8D0, 0x7435D0
CUTSCENE_STATE, EVENT_CONTEXT = 0xB65210, 0x2A11478
BATTLE_STATUS, REACT_CMD = 0x2A11404, 0x2A11162
CAMERA, INPUT_STRUCT_PTR = 0x718C60, 0x79CF00
SAVE, STORY = 0x9A98B0, (0x1C90, 0x260)  # story flag range of the live SAVE block
SLOT0_HP = 0x2A23518 + 0x80  # max HP follows at +4
SAVE_REACTIONS = {55}  # save point "Save" (FM-02 start, 2026-10-17); never taken

STUCK_SECONDS = 120.0
MOVE_EPSILON = 50.0

# KH2's default keyboard map, calibrated on PC1 2026-10-17 with teach `key` (pad bits
# read mid-hold): WASD = left stick, Enter = Cross, Space = Circle, F = Triangle,
# R = R1, E = R2, C = L2, Shift = L1, Q = nothing. Escape = Start is unverified.
# A route's "keys" overrides these.
KEYS = {"up": "W", "down": "S", "left": "A", "right": "D", "confirm": "ENTER",
        "attack": "ENTER", "react": "F", "jump": "SPACE", "lockon": "R", "start": "ESCAPE"}
VK = {"ENTER": 0x0D, "ESCAPE": 0x1B, "SPACE": 0x20, "SHIFT": 0x10, "UP": 0x26, "DOWN": 0x28,
      "LEFT": 0x25, "RIGHT": 0x27, "TAB": 0x09, "BACKSPACE": 0x08}
EXTENDED = {"UP", "DOWN", "LEFT", "RIGHT"}

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


class Game:
    """Read-only view of one owned KH2 process."""

    def __init__(self, pid: int) -> None:
        self.pid = pid
        self.handle = k32.OpenProcess(0x1000 | 0x0010, False, pid)  # QUERY_LIMITED | VM_READ
        if not self.handle:
            raise OSError(f"cannot open pid {pid}")
        mods = (ctypes.c_void_p * 1)()
        need = wt.DWORD()
        if not psapi.EnumProcessModulesEx(self.handle, mods, ctypes.sizeof(mods), ctypes.byref(need), 3):
            raise OSError("cannot read the exe base")
        self.base = mods[0]

    def read(self, addr: int, size: int) -> bytes:
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(self.handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(got)) \
                or got.value != size:
            raise OSError(f"read 0x{addr:X} failed")
        return buf.raw

    def rva(self, rva: int, size: int) -> bytes:
        return self.read(self.base + rva, size)

    def alive(self) -> bool:
        code = wt.DWORD()
        return bool(k32.GetExitCodeProcess(self.handle, ctypes.byref(code))) and code.value == 259

    def state(self) -> dict:
        now = self.rva(NOW, 4)
        s = {"world": now[0], "room": now[1], "door": now[2],
             "inField": self.rva(IN_FIELD, 1)[0] != 0,
             "event": int.from_bytes(self.rva(CUTSCENE_STATE, 4), "little", signed=True) != 0
             or int.from_bytes(self.rva(EVENT_CONTEXT, 8), "little") != 0,
             "menu": self.rva(OPEN_MENU, 1)[0],
             "battle": int.from_bytes(self.rva(BATTLE_STATUS, 4), "little"),
             "react": int.from_bytes(self.rva(REACT_CMD, 2), "little"),
             "hp": int.from_bytes(self.rva(SLOT0_HP, 4), "little", signed=True),
             "maxHp": int.from_bytes(self.rva(SLOT0_HP + 4, 4), "little", signed=True),
             "story": hashlib.sha1(self.rva(SAVE + STORY[0], STORY[1])).hexdigest()[:12]}
        cam = self.rva(CAMERA, 0x58)
        look, eye = struct_floats(cam, 0x08, 3), struct_floats(cam, 0x18, 3)
        s["forward"] = (look[0] - eye[0], look[2] - eye[2])
        s["pos"] = None
        actor = int.from_bytes(cam[0x50:0x58], "little")
        if actor:
            try:
                p = struct_floats(self.read(actor + 0x640 + 0x30, 12), 0, 3)
                s["pos"] = (round(p[0], 1), round(p[1], 1), round(p[2], 1))
            except OSError:
                pass
        try:
            raw = self.read(int.from_bytes(self.rva(INPUT_STRUCT_PTR, 8), "little") + 0x18, 6)
            s["pad"] = {"buttons": f"0x{int.from_bytes(raw[:2], 'little'):04X}",
                        "rx": raw[2], "ry": raw[3], "lx": raw[4], "ly": raw[5]}
        except OSError:
            s["pad"] = None
        s["free"] = s["inField"] and not s["event"] and s["menu"] == 0xFF and s["world"] != 0xFF
        return s


def struct_floats(data: bytes, offset: int, n: int) -> tuple:
    return struct.unpack_from(f"<{n}f", data, offset)


class Keyboard:
    """SendInput scan codes to the owned game, only while its window has focus."""

    def __init__(self, game: Game, keys: dict) -> None:
        self.game, self.keys, self.held = game, keys, set()
        self.unfocused_since: float | None = None

    @staticmethod
    def code(name: str) -> tuple[int, bool]:
        name = name.upper()
        vk = VK.get(name) or (ord(name) if len(name) == 1 else None)
        if vk is None:
            raise ValueError(f"unknown key {name}")
        return u32.MapVirtualKeyW(vk, 0), name in EXTENDED  # MAPVK_VK_TO_VSC

    def send(self, name: str, up: bool) -> None:
        scan, ext = self.code(name)
        inp = INPUT(type=1)  # INPUT_KEYBOARD
        inp.ki = KEYBDINPUT(0, scan, 0x0008 | (0x0001 if ext else 0) | (0x0002 if up else 0), 0, 0)
        if u32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT)) != 1:
            raise OSError(f"SendInput {name} failed")

    def focused(self) -> bool:
        pid = wt.DWORD()
        u32.GetWindowThreadProcessId(u32.GetForegroundWindow(), ctypes.byref(pid))
        if pid.value == self.game.pid:
            self.unfocused_since = None
            return True
        self.release()
        subprocess.run([str(KH2CTL), "focus", "--pid", str(self.game.pid)], capture_output=True, timeout=20,
                       creationflags=subprocess.CREATE_NO_WINDOW)
        u32.GetWindowThreadProcessId(u32.GetForegroundWindow(), ctypes.byref(pid))
        if pid.value == self.game.pid:
            self.unfocused_since = None
            return True
        self.unfocused_since = self.unfocused_since or time.monotonic()
        return False

    def hold(self, actions: set[str]) -> None:
        """Hold exactly these actions (names from the key map or raw key names)."""
        want = {self.keys.get(a, a).upper() for a in actions}
        if want and not self.focused():
            return
        for k in self.held - want:
            self.send(k, True)
        for k in want - self.held:
            self.send(k, False)
        self.held = want

    def tap(self, action: str, ms: int = 90) -> bool:
        if not self.focused():
            return False
        self.hold({action})
        time.sleep(ms / 1000)
        self.hold(set())
        return True

    def release(self) -> None:
        for k in self.held:
            self.send(k, True)
        self.held = set()

    def release_all(self) -> None:
        for k in {*self.keys.values(), *VK, "W", "A", "S", "D", "E", "R", "Q", "F"}:
            try:
                self.send(k, True)
            except (OSError, ValueError):
                pass


def kh2ctl(*args: str, timeout: float = 60) -> dict:
    out = subprocess.run([str(KH2CTL), *args], capture_output=True, text=True, timeout=timeout,
                         creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        return json.loads(out.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {"ok": False, "error": out.stderr.strip()[-300:]}


def enemies(pid: int) -> list[dict]:
    actors = kh2ctl("entities", "--pid", str(pid)).get("actors", [])
    return [a for a in actors if a.get("objectType") in (3, 4) and not a["name"].startswith("F_")
            and a.get("hp", -1) > 0]


def steer(s: dict, target: tuple[float, float], dead: float = 0.38) -> set[str]:
    """8-way keys that move toward target, from the camera's horizontal basis."""
    if not s["pos"]:
        return set()
    dx, dz = target[0] - s["pos"][0], target[1] - s["pos"][2]
    fx, fz = s["forward"]
    d, f = math.hypot(dx, dz), math.hypot(fx, fz)
    if d < 1 or f < 1:
        return set()
    dx, dz, fx, fz = dx / d, dz / d, fx / f, fz / f
    lx, ly = dx * fz - dz * fx, dx * fx + dz * fz
    keys = set()
    if ly > dead:
        keys.add("up")
    elif ly < -dead:
        keys.add("down")
    if lx > dead:
        keys.add("right")
    elif lx < -dead:
        keys.add("left")
    return keys


class Stuck(Exception):
    pass


class Pilot:
    def __init__(self, game: Game, out: Path, keys: dict) -> None:
        self.game, self.out = game, out
        self.kb = Keyboard(game, keys)
        self.log = open(out / "pilot.jsonl", "a", encoding="utf-8")  # noqa: SIM115
        self.last: dict | None = None
        self.mark: tuple | None = None
        self.idle = 0.0  # seconds without progress outside events and menus
        self.tick_at = time.monotonic()
        self.foe_hp: int | None = None
        self.foe_at = 0.0
        self.clips: list[subprocess.Popen] = []
        self.doors: list[dict] = []

    def write(self, kind: str, **data) -> None:
        self.log.write(json.dumps({"t": round(time.time(), 2), "kind": kind, **data}) + "\n")
        self.log.flush()

    def state(self) -> dict:
        if not self.game.alive():
            raise Stuck("game exited")
        s = self.game.state()
        now = time.monotonic()
        if self.last and (s["world"], s["room"]) != (self.last["world"], self.last["room"]) and s["world"] != 0xFF:
            door = {"from": [self.last["world"], self.last["room"]], "to": [s["world"], s["room"]],
                    "entrance": s["door"], "lastPos": self.last.get("lastPos")}
            self.doors.append(door)
            self.write("door", **door)
        if s["pos"] and s["world"] != 0xFF:
            s["lastPos"] = s["pos"]
        elif self.last:
            s["lastPos"] = self.last.get("lastPos")
        if now - self.foe_at > 2 and s["free"]:
            self.foe_hp, self.foe_at = sum(a["hp"] for a in enemies(self.game.pid)), now
        sig = (s["world"], s["room"], s["story"], self.foe_hp)
        moved = self.mark and s["pos"] and self.mark[1] and math.dist(s["pos"], self.mark[1]) > MOVE_EPSILON
        if self.mark is None or sig != self.mark[0] or moved or not s["free"]:
            self.mark, self.idle = (sig, s["pos"]), 0.0
        else:
            self.idle += now - self.tick_at
        self.tick_at = now
        if self.kb.unfocused_since and now - self.kb.unfocused_since > 30:
            raise Stuck("game window lost focus for 30 s")
        if self.idle >= STUCK_SECONDS:
            raise Stuck(f"no progress for {STUCK_SECONDS:.0f} s outside an event or menu")
        self.last = s
        return s

    def clip(self, name: str, seconds: int) -> None:
        path = self.out / f"{name}.mp4"
        self.clips.append(subprocess.Popen(
            [str(KH2CTL), "clip", "--pid", str(self.game.pid), "--seconds", str(min(seconds, 30)), "--fps", "30",
             "--out", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW))
        self.write("clip", name=path.name, seconds=seconds)

    def capture(self, name: str) -> str:
        path = self.out / f"{name}.png"
        kh2ctl("capture", "--pid", str(self.game.pid), "--out", str(path))
        return path.name

    # ---- route steps ----
    def run_step(self, step: dict) -> dict:
        kind = step["do"]
        if step.get("clip"):
            self.clip(step.get("name", kind), int(step["clip"]))
        deadline = time.monotonic() + float(step.get("timeoutSec", 60))
        result: dict = {}

        def expired() -> bool:
            if time.monotonic() > deadline:
                raise Stuck(f"step {kind} timed out")
            return False

        if kind in ("walk", "door"):
            target = tuple(step["to"])
            want = tuple(step["room"]) if kind == "door" else None
            start = self.state()
            while not expired():
                s = self.state()
                if want and (s["world"], s["room"]) == want:
                    break
                if not want and s["pos"] and math.hypot(target[0] - s["pos"][0], target[1] - s["pos"][2]) \
                        <= step.get("tol", 80):
                    break
                if not s["free"]:
                    self.kb.hold(set())
                    if want and s["world"] == 0xFF:
                        time.sleep(0.2)
                        continue
                    if step.get("advance", True) and s["event"]:
                        self.kb.tap("confirm")
                    time.sleep(0.3)
                    continue
                # A door step aims past its point so the trigger is crossed.
                aim = target
                if want and s["pos"] and math.hypot(target[0] - s["pos"][0], target[1] - s["pos"][2]) < 60:
                    aim = (target[0] + step.get("push", [0, 0])[0], target[1] + step.get("push", [0, 0])[1])
                self.kb.hold(steer(s, aim))
                time.sleep(0.06)
            self.kb.hold(set())
            if want:
                self.settle()
            result = {"from": start["pos"], "to": self.state()["pos"]}
        elif kind == "hold":
            self.kb.hold(set(step["keys"]))
            time.sleep(step.get("ms", 500) / 1000)
            self.kb.hold(set())
        elif kind == "press":
            for _ in range(int(step.get("times", 1))):
                self.kb.tap(step.get("key", "confirm"), step.get("ms", 90))
                time.sleep(step.get("gapMs", 400) / 1000)
                self.state()
        elif kind == "react":
            # Only the named reaction: a save point offers one too, and the
            # pilot never opens the save menu.
            while not expired():
                s = self.state()
                if s["free"] and s["react"] == step["code"]:
                    self.kb.tap("react")
                    break
                time.sleep(0.2)
            self.settle(step.get("advance", True))
        elif kind == "dialog":
            self.settle(True, float(step.get("calmSec", 2.0)), deadline)
        elif kind == "skip":
            self.skip(deadline)
        elif kind == "fight":
            result = self.fight(step, deadline)
        elif kind == "wait_room":
            want = tuple(step["room"])
            while not expired():
                s = self.state()
                if (s["world"], s["room"]) == want and s["free"]:
                    break
                if s["event"] and step.get("advance", True):
                    self.kb.tap("confirm")
                time.sleep(0.4)
        elif kind == "capture":
            result = {"path": self.capture(step.get("name", "capture"))}
        elif kind == "clip":
            pass  # started above
        else:
            raise ValueError(f"unknown step {kind}")
        return result

    def settle(self, advance: bool = True, calm: float = 2.0, deadline: float | None = None) -> None:
        """Wait until the room is free for `calm` seconds, pressing confirm through dialog."""
        deadline = deadline or time.monotonic() + 300
        free_since = None
        while time.monotonic() < deadline:
            s = self.state()
            if s["free"]:
                free_since = free_since or time.monotonic()
                if time.monotonic() - free_since >= calm:
                    return
            else:
                free_since = None
                if advance and s["event"] and s["inField"]:
                    self.kb.tap("confirm")
            time.sleep(0.5)
        raise Stuck("dialog/event did not settle")

    def skip(self, deadline: float) -> None:
        """Host only: pause during a cutscene and choose Skip Scene."""
        while time.monotonic() < deadline:
            s = self.state()
            if s["event"]:
                self.kb.tap("start")
                time.sleep(0.8)
                self.kb.tap("down")  # Skip Scene is below Continue (verify in teach captures)
                time.sleep(0.4)
                self.kb.tap("confirm")
                time.sleep(2.0)
                return
            time.sleep(0.3)
        raise Stuck("no cutscene to skip")

    def fight(self, step: dict, deadline: float) -> dict:
        """Fight until no living enemy is listed for `clearSec`. In battle it takes
        any offered reaction (Reversal etc.) except Save, uses `heal` key sequence
        (a Potion through the command menu) below its threshold, backs off with a
        jump below `lowHp` of max HP, and answers Game Over with Continue (the
        native retry of this room) up to `retries` times."""
        reach, low = float(step.get("reach", 220)), float(step.get("lowHp", 0.35))
        stats = {"enemiesStart": None, "swings": 0, "reactions": {}, "minHp": None, "deaths": 0, "evades": 0}
        calm_since, locked, healed = None, False, 0.0
        while time.monotonic() < deadline:
            s = self.state()
            stats["minHp"] = s["hp"] if stats["minHp"] is None else min(stats["minHp"], s["hp"])
            if s["hp"] <= 0 and s["world"] != 0xFF:
                self.kb.hold(set())
                stats["deaths"] += 1
                self.write("death", stats=stats)
                if stats["deaths"] > int(step.get("retries", 2)):
                    raise Stuck(f"died {stats['deaths']} times")
                time.sleep(7)  # death animation, then Game Over opens on Continue
                self.capture(f"gameover-{stats['deaths']}")
                self.kb.tap("confirm")
                time.sleep(3)
                locked = False
                continue
            if not s["free"]:
                self.kb.hold(set())
                if s["event"]:
                    self.kb.tap("confirm")
                time.sleep(0.3)
                continue
            if s["react"] and s["battle"] and s["react"] not in SAVE_REACTIONS:
                stats["reactions"][s["react"]] = stats["reactions"].get(s["react"], 0) + 1
                self.kb.tap("react")
                continue
            foes = enemies(self.game.pid)
            if stats["enemiesStart"] is None:
                stats["enemiesStart"] = len(foes)
            if not foes:
                self.kb.hold(set())
                calm_since = calm_since or time.monotonic()
                if time.monotonic() - calm_since > float(step.get("clearSec", 3)):
                    return stats
                time.sleep(0.3)
                continue
            calm_since = None
            if not locked:
                self.kb.tap("lockon")
                locked = True
            p = s["pos"] or (0, 0, 0)
            target = min(foes, key=lambda a: math.hypot(a["position"]["x"] - p[0], a["position"]["z"] - p[2]))
            tx, tz = target["position"]["x"], target["position"]["z"]
            gap = math.hypot(tx - p[0], tz - p[2])
            heal = step.get("heal")  # {"below": 0.4, "keys": [...], "max": 3}: menu keys to use a Potion
            if heal and s["hp"] < heal.get("below", 0.4) * max(s["maxHp"], 1)                     and stats.setdefault("heals", 0) < heal.get("max", 3) and time.monotonic() > healed + 3:
                stats["heals"] += 1
                healed = time.monotonic()
                self.kb.hold(set())
                for action in heal["keys"]:
                    self.kb.tap(action)
                    time.sleep(heal.get("gapMs", 250) / 1000)
                self.write("heal", hp=s["hp"], after=self.state()["hp"])
                continue
            if s["hp"] < low * max(s["maxHp"], 1) and gap < reach * 2:
                stats["evades"] += 1
                self.kb.hold(steer(s, (2 * p[0] - tx, 2 * p[2] - tz)))
                time.sleep(0.15)
                self.kb.tap("jump")
                time.sleep(0.6)
                self.kb.hold(set())
            elif gap > reach:
                self.kb.hold(steer(s, (tx, tz)))
                time.sleep(0.15)
            else:
                self.kb.hold(set())
                for _ in range(3):
                    self.kb.tap("attack", 70)
                    time.sleep(0.18)
                    stats["swings"] += 1
        raise Stuck("fight did not clear")

    def finish(self) -> None:
        self.kb.release()
        for proc in self.clips:
            try:
                proc.wait(timeout=120)
            except subprocess.TimeoutExpired:
                proc.kill()
        (self.out / "doors.json").write_text(json.dumps(self.doors, indent=1), encoding="utf-8")


def run_route(pilot: Pilot, route: dict, first: int) -> int:
    steps = route["steps"]
    for i, step in enumerate(steps[first:], start=first):
        pilot.write("step", index=i, step=step)
        t0 = time.monotonic()
        try:
            result = pilot.run_step(step)
        except Stuck as why:
            pilot.kb.release()
            pilot.write("stuck", index=i, reason=str(why), state=pilot.last)
            try:
                pilot.capture("stuck")
                pilot.clip("stuck", 10)
            except Exception as error:  # evidence is best effort; the stop isn't
                pilot.write("evidence_error", error=str(error))
            print(json.dumps({"ok": False, "stuckAt": i, "reason": str(why)}))
            return 3
        pilot.write("done", index=i, seconds=round(time.monotonic() - t0, 1), result=result,
                    room=[pilot.last["world"], pilot.last["room"]] if pilot.last else None)
    print(json.dumps({"ok": True, "steps": len(steps) - first}))
    return 0


def run_teach(pilot: Pilot, box: Path, seconds: float) -> int:
    """Executes box/cmd-*.json in name order and answers each in box/res-*.json.
    A command is {"op": "state"|"capture"|"key"|"stop"} or any route step.
    `key` holds raw keys ({"keys": ["W"], "ms": 400}) and reports the pad
    bits seen mid-hold: that is how the key map is calibrated."""
    box.mkdir(parents=True, exist_ok=True)
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        pilot.state()
        cmds = sorted(box.glob("cmd-*.json"))
        if not cmds:
            time.sleep(0.25)
            continue
        cmd_path = cmds[0]
        res_path = box / cmd_path.name.replace("cmd-", "res-")
        try:
            cmd = json.loads(cmd_path.read_text(encoding="utf-8"))
            op, res = cmd.get("op") or cmd.get("do"), {}
            if op == "stop":
                res_path.write_text(json.dumps({"ok": True, "stopped": True}), encoding="utf-8")
                cmd_path.unlink()
                return 0
            if op == "key":
                if pilot.kb.focused():
                    pilot.kb.hold(set(cmd["keys"]))
                    time.sleep(min(cmd.get("ms", 300), 3000) / 2000)
                    res["midHold"] = pilot.game.state()["pad"]
                    time.sleep(min(cmd.get("ms", 300), 3000) / 2000)
                    pilot.kb.hold(set())
                else:
                    res["error"] = "not focused"
            elif op not in ("state", "capture"):
                res["result"] = pilot.run_step(cmd)
            if op == "capture" or cmd.get("capture"):
                res["capture"] = pilot.capture(f"teach-{cmd_path.stem[4:]}")
            res["state"] = pilot.state()
            res["ok"] = True
        except Stuck as why:
            pilot.kb.release()
            res = {"ok": False, "stuck": str(why), "state": pilot.last}
        except Exception as error:  # report and keep serving commands
            pilot.kb.release()
            res = {"ok": False, "error": f"{type(error).__name__}: {error}"}
        pilot.write("teach", cmd=cmd_path.name, res=res)
        res_path.write_text(json.dumps(res, indent=1), encoding="utf-8")
        cmd_path.unlink()
    return 0


def owned(pid: int) -> bool:
    rows = (ROOT / "build" / "rig" / "owned.txt")
    return rows.exists() and any(r.split()[:1] == [str(pid)] for r in rows.read_text().splitlines())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pid", type=int, required=True)
    ap.add_argument("--out", type=Path)
    ap.add_argument("--route", type=Path)
    ap.add_argument("--from-step", type=int, default=0)
    ap.add_argument("--teach", type=Path)
    ap.add_argument("--seconds", type=float, default=900)
    ap.add_argument("--release-all", action="store_true")
    args = ap.parse_args()
    game = Game(args.pid)
    route = json.loads(args.route.read_text(encoding="utf-8")) if args.route else {}
    keys = {**KEYS, **route.get("keys", {})}
    if args.release_all:
        Keyboard(game, keys).release_all()
        return 0
    if not owned(args.pid):
        print(json.dumps({"ok": False, "error": f"pid {args.pid} is not a rig-owned game"}))
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    pilot = Pilot(game, args.out, keys)
    try:
        if args.route:
            return run_route(pilot, route, args.from_step)
        return run_teach(pilot, args.teach, args.seconds)
    finally:
        pilot.finish()


if __name__ == "__main__":
    sys.exit(main())
