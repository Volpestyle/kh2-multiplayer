"""Route pilot (VUH-2340): plays one seat from its key map.

It reads the owned game's state (read-only) and plays through native doors and
events. By default (`--input pad`) the keys become slot-0 pad state in the
inject's existing input mailbox, so the game never takes focus from the desktop.
`--input keys` sends real scan-code keys with SendInput instead, which brings
the game to the foreground before each key: use it only when nobody is using
the PC and the real-keyboard proof is wanted. Nothing here warps, teleports or
writes game memory.

Modes (run from the Session-1 desk bridge, normally through the scenario
runner's `pilot` step, which owns the game and cleans up):
  --route R.json   play data-driven route steps; the stuck detector stops it
  --teach DIR      execute command files dropped in DIR (route discovery)
  --release-all    clear the pad mailbox (and, with --input keys, send key-up for every pilot key), then exit
  --input pad|keys background mailbox pad (default) or real foreground keyboard

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
import os
import re
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
KH2CTL = ROOT / "build" / "tools" / "kh2ctl" / "Release" / "kh2ctl.exe"
# A sealed lane packet (PC2) has no checkout CLI: it names its packaged diagnostic CLI, which runs
# from its package root (the parent of bin/).
PACKAGED_KH2CTL = os.environ.get("KH2_PILOT_KH2CTL")

# exe-relative addresses (runtime/include/kh2coop/KH2Offsets.hpp)
NOW, IN_FIELD, OPEN_MENU = 0x717008, 0x9BA8D0, 0x7435D0
CUTSCENE_STATE, EVENT_CONTEXT = 0xB65210, 0x2A11478
BATTLE_STATUS, REACT_CMD = 0x2A11404, 0x2A11162
CAMERA, INPUT_STRUCT_PTR = 0x718C60, 0x79CF00
SAVE, STORY = 0x9A98B0, (0x1C90, 0x260)  # story flag range of the live SAVE block
CANONICAL_PLAYER = 0x2A105D0  # actor -> status +0x5C0 -> HP +0, max HP +4 (EntityHook::ActorHp)
SAVE_REACTIONS = {55}  # save point "Save" (FM-02 start, 2026-10-17); never taken
KNOWN_EVENT_REACTIONS = frozenset((297, 298, 299))  # Thorn Key Counter/Lunarsault/Break Raid only
GAME_OVER_MENU = 8  # open-menu id while the Game Over screen is up (solo teach-03); co-op downed has none

STUCK_SECONDS = 120.0
MOVE_EPSILON = 50.0

# KH2's default keyboard map, calibrated on PC1 2026-10-17 with teach `key` (pad bits
# read mid-hold): WASD = left stick, Enter = Cross, Space = Circle, F = Triangle,
# R = R1, E = R2, C = L2, Shift = L1, Q = nothing. Escape = Start is unverified.
# A route's "keys" overrides these.
KEYS = {"up": "W", "down": "S", "left": "A", "right": "D", "confirm": "ENTER",
        "attack": "ENTER", "react": "F", "jump": "SPACE", "lockon": "R", "start": "ESCAPE",
        "guard": "SQUARE"}  # no keyboard key produces Square: guard is pad input only
VK = {"ENTER": 0x0D, "ESCAPE": 0x1B, "SPACE": 0x20, "SHIFT": 0x10, "UP": 0x26, "DOWN": 0x28,
      "LEFT": 0x25, "RIGHT": 0x27, "TAB": 0x09, "BACKSPACE": 0x08}
EXTENDED = {"UP", "DOWN", "LEFT", "RIGHT"}

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD),
                ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


# Mouse buttons as pseudo keys: (down flag, up flag). No movement is ever sent.
MOUSE = {"MOUSE_LEFT": (0x0002, 0x0004), "MOUSE_RIGHT": (0x0008, 0x0010), "MOUSE_MIDDLE": (0x0020, 0x0040)}


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT), ("mi", MOUSEINPUT), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


class PromptReactionReader:
    """Read existing v7 prompt telemetry; never creates or writes its mapping.

    Requires the native actionable-RC/field-guard prompt precedence (VUH-2569).
    Unknown, frozen or out-of-scope observations retain the original raw command.
    """

    SIZE, MAGIC, VERSION = 360, 0x4E574448, 7
    SETTLE_FRAMES, FRESH_SECONDS = 30, 0.5

    def __init__(self, pid: int) -> None:
        self.pid, self.handle, self.view = pid, None, None
        self.retry_at = 0.0
        self.reset()

    def reset(self) -> None:
        self.key = self.frame = self.first_frame = None
        self.advanced_at = 0.0

    def close(self) -> None:
        if self.view:
            k32.UnmapViewOfFile(self.view)
            self.view = None
        if self.handle:
            k32.CloseHandle(self.handle)
            self.handle = None
        self.reset()

    def read(self) -> bytes | None:
        if not self.view:
            if time.monotonic() < self.retry_at:
                return None
            self.retry_at = time.monotonic() + 1.0
            k32.OpenFileMappingW.argtypes = [wt.DWORD, wt.BOOL, wt.LPCWSTR]
            k32.OpenFileMappingW.restype = wt.HANDLE
            k32.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
            k32.MapViewOfFile.restype = ctypes.c_void_p
            k32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
            k32.UnmapViewOfFile.restype = wt.BOOL
            k32.CloseHandle.argtypes = [wt.HANDLE]
            self.handle = k32.OpenFileMappingW(4, False, f"Local\\kh2coop_downed_{self.pid}")
            if not self.handle:
                return None
            self.view = k32.MapViewOfFile(self.handle, 4, 0, 0, self.SIZE)
            if not self.view:
                self.close()
                return None
        first = ctypes.string_at(self.view, self.SIZE)
        second = ctypes.string_at(self.view, self.SIZE)
        return first if first == second else None

    def interpret(self, s: dict, raw: bytes | None) -> tuple[int, dict]:
        original = s["react"]
        info = {"source": "raw", "filtered": False}
        field = s["inField"] and not s["event"] and s["menu"] == 255 \
            and s["hp"] is not None and s["hp"] > 0 and s["player"] > 0x10000 \
            and len(s["roomScope"]) == 6 and s["world"] != 255
        if not field or raw is None or len(raw) != self.SIZE:
            self.reset()
            return original, info
        magic, version = struct.unpack_from("<II", raw)
        frame, state, actor, hp, maximum, flags, dead, off, published, task = \
            struct.unpack_from("<IIQiiIIIIQ", raw, 24)
        kind, hide, shifted = struct.unpack_from("<I", raw, 328)[0], \
            struct.unpack_from("<I", raw, 340)[0], struct.unpack_from("<I", raw, 356)[0]
        known = (kind, hide) in {(0, 8), (0, 9), (0, 10), (2, 10), (1, 0), (2, 0), (0, 7)}
        if magic != self.MAGIC or version != self.VERSION or not frame or state != 1 \
                or actor != s["player"] or hp != s["hp"] or maximum != s["maxHp"] \
                or flags & 4 or dead or off or published != 1 or task or shifted != original or not known:
            self.reset()
            return original, info
        info.update(frame=frame, promptKind=kind, promptHide=hide)
        now, key = time.monotonic(), (tuple(s["roomScope"]), actor)
        if self.key != key or self.frame is None or frame < self.frame \
                or frame-self.frame > 120 or now-self.advanced_at > self.FRESH_SECONDS:
            self.key, self.frame, self.first_frame, self.advanced_at = key, frame, frame, now
            return original, info
        if frame != self.frame:
            self.frame, self.advanced_at = frame, now
        if frame-self.first_frame < self.SETTLE_FRAMES:
            return original, info
        info["source"] = "downed-prompt-v7"
        if hide == 7:  # The native actionable command owns Triangle.
            return original, info
        info["filtered"] = original != 0
        return 0, info

    def reaction(self, game, s: dict) -> tuple[int, dict]:
        try:
            if game.pid != self.pid or not game.alive():
                raise ValueError("fixture PID/process unavailable")
            raw = self.read()
            now = game.rva(NOW, 10)
            current_scope = tuple(now[:3]) + struct.unpack_from("<HHH", now, 4)
            if current_scope != tuple(s["roomScope"]) \
                    or int.from_bytes(game.rva(CANONICAL_PLAYER, 8), "little") != s["player"]:
                raise ValueError("fixture actor/scope changed")
            return self.interpret(s, raw)
        except (OSError, ValueError, struct.error):
            self.reset()
            return s["react"], {"source": "raw", "filtered": False}


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

    def modules(self) -> list[str]:
        """Loaded module paths (read-only). route-08's KH2 crashed in clr.dll, which our
        native inject doesn't load; a snapshot shows when the CLR arrived and with what."""
        handle = k32.OpenProcess(0x0400 | 0x0010, False, self.pid)  # QUERY_INFORMATION | VM_READ
        if not handle:
            return []
        try:
            mods = (ctypes.c_void_p * 1024)()
            need = wt.DWORD()
            if not psapi.EnumProcessModulesEx(handle, mods, ctypes.sizeof(mods), ctypes.byref(need), 3):
                return []
            names = []
            for i in range(min(need.value // ctypes.sizeof(ctypes.c_void_p), 1024)):
                buf = ctypes.create_unicode_buffer(520)
                if psapi.GetModuleFileNameExW(handle, ctypes.c_void_p(mods[i]), buf, 520):
                    names.append(buf.value)
            return names
        finally:
            k32.CloseHandle(handle)

    def state(self) -> dict:
        now = self.rva(NOW, 10)
        s = {"world": now[0], "room": now[1], "door": now[2],
             "roomScope": tuple(now[:3]) + struct.unpack_from("<HHH", now, 4),
             "inField": self.rva(IN_FIELD, 1)[0] != 0,
             "event": int.from_bytes(self.rva(CUTSCENE_STATE, 4), "little", signed=True) != 0
             or int.from_bytes(self.rva(EVENT_CONTEXT, 8), "little") != 0,
             "menu": self.rva(OPEN_MENU, 1)[0],
             "battle": int.from_bytes(self.rva(BATTLE_STATUS, 4), "little"),
             "react": int.from_bytes(self.rva(REACT_CMD, 2), "little"),
             "hp": None, "maxHp": None, "player": 0, "playerPos": None,
             "story": hashlib.sha1(self.rva(SAVE + STORY[0], STORY[1])).hexdigest()[:12]}
        # Prologue status-pool index 79 may belong to the other seat's clone.
        # Read only this game's canonical actor, and refuse a replaced actor/status.
        try:
            player = int.from_bytes(self.rva(CANONICAL_PLAYER, 8), "little")
            if player > 0x10000:
                status = int.from_bytes(self.read(player + 0x5C0, 8), "little")
                if status > 0x10000:
                    hp, max_hp = struct.unpack("<ii", self.read(status, 8))
                    position = struct.unpack("<fff", self.read(player + 0x670, 12))
                    if int.from_bytes(self.rva(CANONICAL_PLAYER, 8), "little") == player \
                            and int.from_bytes(self.read(player + 0x5C0, 8), "little") == status \
                            and 0 <= hp <= max_hp <= 10000000 and all(math.isfinite(v) for v in position):
                        s.update(hp=hp, maxHp=max_hp, player=player, playerPos=position)
        except OSError:
            pass  # actor replacement/loading: the fight waits with neutral input
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
        s["reactRaw"] = s["react"]
        reader = getattr(self, "prompt_reactions", None)
        if reader is None:
            reader = self.prompt_reactions = PromptReactionReader(self.pid)
        s["react"], s["reactObservation"] = reader.reaction(self, s)
        return s

    def close_prompt(self) -> None:
        reader = getattr(self, "prompt_reactions", None)
        if reader is not None:
            reader.close()


def struct_floats(data: bytes, offset: int, n: int) -> tuple:
    return struct.unpack_from(f"<{n}f", data, offset)


class Keyboard:
    """SendInput scan codes to the owned game, only while its window has focus."""

    def __init__(self, game: Game, keys: dict) -> None:
        self.game, self.keys, self.held, self.sent_at = game, keys, set(), 0.0
        self.unfocused_since: float | None = None

    @staticmethod
    def code(name: str) -> tuple[int, bool]:
        name = name.upper()
        vk = VK.get(name) or (ord(name) if len(name) == 1 else None)
        if vk is None:
            raise ValueError(f"unknown key {name}")
        return u32.MapVirtualKeyW(vk, 0), name in EXTENDED  # MAPVK_VK_TO_VSC

    def send(self, name: str, up: bool) -> None:
        if name.upper() in MOUSE:
            # A click lands on the window under the cursor, which might not be
            # the game: refuse unless the cursor is over the owned game.
            point, pid = wt.POINT(), wt.DWORD()
            u32.GetCursorPos(ctypes.byref(point))
            u32.GetWindowThreadProcessId(u32.WindowFromPoint(point), ctypes.byref(pid))
            if pid.value != self.game.pid and not up:
                raise OSError(f"cursor ({point.x},{point.y}) is not over the game; {name} refused")
            inp = INPUT(type=0)  # INPUT_MOUSE, button only
            inp.mi = MOUSEINPUT(0, 0, 0, MOUSE[name.upper()][1 if up else 0], 0, 0)
            if u32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT)) != 1:
                raise OSError(f"SendInput {name} failed")
            return
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
            # The game drops key-downs that arrive before it has taken focus
            # (teach-02: the first walk got no input), so let it settle.
            time.sleep(0.4)
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
        # Key-downs are re-sent every half second, like typematic repeat, so a
        # held key the game missed still registers.
        repeat = time.monotonic() - self.sent_at > 0.5
        for k in (want if repeat else want - self.held):
            self.send(k, False)
        if repeat or want != self.held:
            self.sent_at = time.monotonic()
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
        for k in {*self.keys.values(), *VK, *MOUSE, "W", "A", "S", "D", "E", "R", "Q", "F"}:
            try:
                self.send(k, True)
            except (OSError, ValueError):
                pass


# Background input (VUH-2340): the same pilot keys as raw slot-0 pad state, written to the
# inject's existing input mailbox (common/include/kh2coop/InputMailbox.hpp), the channel
# `kh2ctl player-input` uses. It needs no window focus, so the game never takes the foreground.
# Raw bits are the PS2 order kh2ctl uses; sticks are (axis, value) with +y forward, +x right.
PAD = {"W": ("ly", 1.0), "S": ("ly", -1.0), "A": ("lx", -1.0), "D": ("lx", 1.0),
       "ENTER": 0x4000, "SPACE": 0x2000, "F": 0x1000, "SQUARE": 0x8000, "R": 0x0800, "E": 0x0200,
       "C": 0x0100, "SHIFT": 0x0400, "X": 0x0002, "N": 0x0001, "3": 0x0001, "M": 0x0008, "4": 0x0008,
       "ESCAPE": 0x0008, "UP": 0x0010, "RIGHT": 0x0020, "DOWN": 0x0040, "LEFT": 0x0080}
MAILBOX_MAGIC, MAILBOX_VERSION, MAILBOX_SIZE = 0x4332484B, 1, 256  # header 64 + three 64-byte slots
SLOT0 = 64  # seq u32, lx ly rx ry f32, buttons u32, raw u16, reserved u16, actor u32, target u32, time u64 @40


class PadInput:
    """Pilot keys as slot-0 mailbox pad state: same interface as Keyboard, never takes focus."""

    unfocused_since = None

    def __init__(self, game: Game, keys: dict) -> None:
        self.game, self.keys, self.held = game, keys, set()
        k32.CreateFileMappingW.restype = wt.HANDLE
        k32.CreateFileMappingW.argtypes = [wt.HANDLE, ctypes.c_void_p, wt.DWORD, wt.DWORD, wt.DWORD, wt.LPCWSTR]
        k32.MapViewOfFile.restype = ctypes.c_void_p
        k32.MapViewOfFile.argtypes = [wt.HANDLE, wt.DWORD, wt.DWORD, wt.DWORD, ctypes.c_size_t]
        self.mapping = k32.CreateFileMappingW(wt.HANDLE(-1), None, 0x04, 0, MAILBOX_SIZE,
                                              f"Local\\kh2coop_input_{game.pid}")
        created = ctypes.get_last_error() != 183  # ERROR_ALREADY_EXISTS
        self.view = k32.MapViewOfFile(self.mapping, 0xF001F, 0, 0, MAILBOX_SIZE) if self.mapping else None
        if not self.view:
            raise OSError(f"input mailbox for pid {game.pid} unavailable")
        magic, version, writer = struct.unpack("<III", ctypes.string_at(self.view, 12))
        if created:
            ctypes.memset(self.view, 0, MAILBOX_SIZE)
            ctypes.memmove(self.view, struct.pack("<IIII", MAILBOX_MAGIC, MAILBOX_VERSION, k32.GetCurrentProcessId(), 0), 16)
        elif (magic, version) != (MAILBOX_MAGIC, MAILBOX_VERSION):
            raise OSError(f"input mailbox for pid {game.pid} has magic {magic:#x} version {version}")
        elif not alive(writer):
            # The inject drops a mailbox whose writer has exited (a finished kh2ctl pulse);
            # a live writer such as the co-op runtime keeps its ownership.
            ctypes.memmove(self.view + 8, struct.pack("<I", k32.GetCurrentProcessId()), 4)

    def pad(self, names: set[str]) -> tuple[float, float, int]:
        lx = ly = 0.0
        raw = 0
        for name in names:
            entry = PAD.get(name)
            if entry is None:
                raise ValueError(f"key {name} has no pad mapping")
            if isinstance(entry, tuple):
                if entry[0] == "lx":
                    lx += entry[1]
                else:
                    ly += entry[1]
            else:
                raw |= entry
        norm = math.hypot(lx, ly)
        return (lx / norm, ly / norm, raw) if norm > 1 else (lx, ly, raw)

    def write(self, names: set[str]) -> None:
        lx, ly, raw = self.pad(names)
        seq = struct.unpack("<I", ctypes.string_at(self.view + SLOT0, 4))[0]
        ctypes.memmove(self.view + SLOT0, struct.pack("<I", seq + 1), 4)
        payload = struct.pack("<ffffIHHII4xQ", lx, ly, 0.0, 0.0, 0, raw, 0, 0, 0, int(time.time() * 1000))
        ctypes.memmove(self.view + SLOT0 + 4, payload, len(payload))
        ctypes.memmove(self.view + SLOT0, struct.pack("<I", seq + 2), 4)

    def focused(self) -> bool:
        return True

    def hold(self, actions: set[str]) -> None:
        """Hold exactly these actions; the inject keeps the last frame until the next one."""
        want = {self.keys.get(a, a).upper() for a in actions}
        if want != self.held:
            self.write(want)
            self.held = want

    def tap(self, action: str, ms: int = 90) -> bool:
        self.hold({action})
        time.sleep(ms / 1000)
        self.hold(set())
        return True

    def release(self) -> None:
        self.write(set())
        self.held = set()

    release_all = release


def alive(pid: int) -> bool:
    handle = k32.OpenProcess(0x00100000, False, pid) if pid else None  # SYNCHRONIZE
    if handle:
        k32.CloseHandle(handle)
    return bool(handle)


def kh2ctl(*args: str, timeout: float = 60) -> dict:
    cli = Path(PACKAGED_KH2CTL) if PACKAGED_KH2CTL else KH2CTL
    out = subprocess.run([str(cli), *args], capture_output=True, text=True, timeout=timeout,
                         cwd=cli.parents[1] if PACKAGED_KH2CTL else None, creationflags=subprocess.CREATE_NO_WINDOW)
    try:
        return json.loads(out.stdout.strip().splitlines()[-1])
    except (IndexError, json.JSONDecodeError):
        return {"ok": False, "error": out.stderr.strip()[-300:]}


def enemies(pid: int, timeout: float = 60) -> list[dict]:
    actors = kh2ctl("entities", "--pid", str(pid), timeout=timeout).get("actors", [])
    return [a for a in actors if (a.get("objectType") in (3, 4)
            or (a.get("objectType") == 12 and a.get("objectId") == 307)) and not a["name"].startswith("F_")
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


def heal_due(hp: int, max_hp: int, heal: dict | None, used: int, missed: int, ready_at: float, now: float,
             drop: int = 0) -> bool:
    """Use a Potion now? `drop` is the HP lost in the last 2 s: half of a combo already
    landing counts against the threshold, so the heal starts before it finishes. Only
    Potions that went through count toward `max`; menu misses have their own cap."""
    if not heal or hp <= 0 or used >= heal.get("max", 3) or missed >= heal.get("missMax", 4) or now < ready_at:
        return False
    return hp - drop * float(heal.get("lead", 0.5)) < heal.get("below", 0.5) * max(max_hp, 1)


class Retry(Exception):
    """Game Over → Continue reloaded another room: replay from the step that leads back."""


def event_reaction_allowed(s: dict, scope: tuple, commands: set[int]) -> bool:
    """A named native camera RC is narrower than ordinary field freedom."""
    return bool(commands and s["event"] and s["inField"] and s["menu"] == 0xFF
                and s["roomScope"] == scope and s["player"] and s["hp"] is not None and s["hp"] > 0
                and s["react"] in commands)


class DownedLog:
    """Observe one current-run PID's logical state; native HP1 is not alive."""

    def __init__(self, pid: int) -> None:
        self.path = ROOT / "build" / "rig" / "logs" / f"kh2coop_inject_{pid}.log"
        self.offset, self.pending = 0, b""
        self.downed, self.full_party, self.actor, self.known = False, False, 0, False
        self.downs, self.revives = 0, 0

    def update(self) -> bool:
        try:
            with self.path.open("rb") as log:
                if log.seek(0, 2) < self.offset:
                    self.known = False  # unexpected truncation is not proof of recovery
                    return False
                log.seek(self.offset)
                data = log.read(1024 * 1024)
                self.offset += len(data)
        except OSError:
            self.known = False
            return False
        lines = (self.pending + data).split(b"\n")
        self.pending = lines.pop()
        for raw in lines:
            line = raw.decode("utf-8", "replace")
            if "[downed] gate intercept " in line:
                actor = re.search(r" actor=([0-9A-Fa-f]+) ", line)
                self.actor = int(actor[1], 16) if actor else 0
                self.downed, self.full_party = True, False
                self.downs += 1
            elif "[downed] revive returned " in line:
                state = re.search(r" state=(\d+)", line)
                fault = re.search(r" fault=(\d+)", line)
                hp = re.search(r" hp=(\d+)/", line)
                if state and state[1] == "1" and hp and int(hp[1]) > 0 and (not fault or fault[1] == "0"):
                    self.downed, self.full_party, self.actor = False, False, 0
                    self.revives += 1
            elif "[downed] logical hold ended " in line:
                self.downed, self.full_party, self.actor = False, False, 0
            elif "[downed] full party down " in line or "[downed] gate pass mode=" in line and "full party down:" in line:
                self.full_party = True
            elif "[downed] hold lost " in line or "[downed] revive FAULT" in line:
                self.downed, self.actor = True, 0  # refused/unknown: never infer living
        self.known = len(data) < 1024 * 1024 and (not self.downed or bool(self.actor))
        return self.known


class CooperativeFight:
    """Revive within the active pilot: the same pad writer owns every input.

    Reach100/hold1600ms/timeout60s reuse scenario.ReviveGuard's proven bounds.
    Native reaction commands always take priority; no death is forced.
    """

    REACH, HOLD_SECONDS, TIMEOUT = 100.0, 1.6, 60.0

    def __init__(self, game: Game, peer: Game, hold_ms: int | None = None) -> None:
        self.peer = peer
        if hold_ms is not None:  # VUH-2632 (D17): Standard's revive hold is 300 frames (~5 s)
            self.HOLD_SECONDS = hold_ms / 1000.0
        self.own_log, self.peer_log = DownedLog(game.pid), DownedLog(peer.pid)
        self.since: float | None = None
        self.mode = ""

    def neutral(self, pilot, mode: str) -> bool:
        pilot.kb.hold(set())
        if mode != self.mode:
            pilot.write("coop_revive", state=mode, peerPid=self.peer.pid,
                        ownDowns=self.own_log.downs, peerDowns=self.peer_log.downs,
                        ownRevives=self.own_log.revives, peerRevives=self.peer_log.revives)
            self.mode = mode
        if self.since is None:
            self.since = time.monotonic()
        if time.monotonic() - self.since > self.TIMEOUT:
            raise Stuck(f"cooperative revive {mode} exceeded {self.TIMEOUT:.0f}s")
        return True

    def step(self, pilot, own: dict, event_reaction: bool = False) -> bool:
        """True consumes this combat tick; False leaves native combat/RCs alone."""
        if not isinstance(pilot.kb, PadInput):
            raise Stuck("cooperative revive requires pad input")
        own_ok, peer_ok = self.own_log.update(), self.peer_log.update()
        if not own_ok or not peer_ok:
            return self.neutral(pilot, "unknown-log")
        if self.own_log.full_party or self.peer_log.full_party:
            return self.neutral(pilot, "full-party-down")
        if self.own_log.downed:
            return self.neutral(pilot, "own-logical-downed")
        if event_reaction:
            return False  # authenticated own living state: native camera RC wins revival
        if not self.peer_log.downed:
            if self.mode:
                pilot.write("coop_revive", state="combat", peerPid=self.peer.pid,
                            ownRevives=self.own_log.revives, peerRevives=self.peer_log.revives)
            self.since, self.mode = None, ""
            return False
        self.neutral(pilot, "peer-logical-downed")
        try:
            peer = self.peer.state()
        except (OSError, ValueError):
            return True
        if own["hp"] is None or own["hp"] <= 0 or not peer["free"] \
                or own["roomScope"] != peer["roomScope"] or not own["playerPos"] or not peer["playerPos"] \
                or self.peer_log.actor != peer["player"]:
            return True
        if not own["free"]:
            return False  # ordinary bounded tutorial/event handling still applies
        if own["react"]:
            return False  # native RC handler, never a revive hold
        target = peer["playerPos"]
        distance = math.hypot(target[0] - own["playerPos"][0], target[2] - own["playerPos"][2])
        if distance > self.REACH:
            pilot.kb.hold(steer({**own, "pos": own["playerPos"]}, (target[0], target[2])))
            time.sleep(0.2)
            pilot.kb.hold(set())
            return True
        pilot.write("coop_revive_hold", peerPid=self.peer.pid, distance=round(distance, 1),
                    milliseconds=round(self.HOLD_SECONDS * 1000))
        end = time.monotonic() + self.HOLD_SECONDS
        pilot.kb.hold({"react"})
        try:
            while time.monotonic() < end:
                time.sleep(0.05)
                current, peer_now = pilot.game.state(), self.peer.state()
                if not self.own_log.update() or not self.peer_log.update() or self.own_log.downed \
                        or not self.peer_log.downed or self.own_log.full_party or self.peer_log.full_party \
                        or not current["free"] or current["react"] or not peer_now["free"] \
                        or current["roomScope"] != peer_now["roomScope"] \
                        or current["player"] != own["player"] or peer_now["player"] != peer["player"]:
                    break
        except (OSError, ValueError):
            pass
        finally:
            pilot.kb.hold(set())
        return True


class Pilot:
    def __init__(self, game: Game, out: Path, keys: dict, mode: str = "pad") -> None:
        self.game, self.out = game, out
        self.kb = PadInput(game, keys) if mode == "pad" else Keyboard(game, keys)
        self.log = open(out / "pilot.jsonl", "a", encoding="utf-8")  # noqa: SIM115
        self.last: dict | None = None
        self.mark: tuple | None = None
        self.idle = 0.0  # seconds without progress outside events and menus
        self.tick_at = time.monotonic()
        self.foe_hp: int | None = None
        self.foe_at = 0.0
        self.clips: list[threading.Thread] = []
        self.log_lock = threading.Lock()
        self.doors: list[dict] = []
        self.module_set: frozenset[str] = frozenset()
        self.module_at = 0.0
        self.cooperative: CooperativeFight | None = None

    def write(self, kind: str, **data) -> None:
        with self.log_lock:
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
        if now - self.module_at > 60:
            self.module_at = now
            current = frozenset(self.game.modules())
            if current and current != self.module_set:
                self.write("modules", added=sorted(current - self.module_set), removed=sorted(self.module_set - current))
                self.module_set = current
                (self.out / "modules.json").write_text(json.dumps(sorted(current), indent=1), encoding="utf-8")
        if now - self.foe_at > 2 and s["free"]:
            self.foe_hp, self.foe_at = sum(a["hp"] for a in enemies(self.game.pid)), now
        sig = (s["world"], s["room"], s["story"], self.foe_hp)
        moved = self.mark and s["pos"] and self.mark[1] and math.dist(s["pos"], self.mark[1]) > MOVE_EPSILON
        # A mark taken without a position (during a load or event) must not hide later movement
        # (route-06: walking the Sandlot counted as no progress for 120 s).
        unplaced = self.mark is not None and s["pos"] is not None and self.mark[1] is None
        if self.mark is None or sig != self.mark[0] or moved or unplaced or not s["free"]:
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
        """Records in the background. kh2ctl allows one capture per game at a
        time, so each clip waits for the previous one (a door clip is still
        encoding when the fight after it starts). A replayed step gets -2, -3."""
        path, n = self.out / f"{name}.mp4", 1
        while path.exists() or any(c.name == path.name for c in self.clips):
            n += 1
            path = self.out / f"{name}-{n}.mp4"
        prev = self.clips[-1] if self.clips else None

        def record() -> None:
            if prev:
                prev.join(timeout=240)
            if not self.game.alive():
                self.write("clip_skipped", name=path.name, reason="owned-game-exited")
                return
            try:
                done = subprocess.run([str(KH2CTL), "clip", "--pid", str(self.game.pid), "--seconds",
                                       str(min(seconds, 30)), "--fps", "30", "--out", str(path)],
                                      capture_output=True, text=True, timeout=240,
                                      creationflags=subprocess.CREATE_NO_WINDOW)
                self.write("clip_done", name=path.name, exit=done.returncode, out=done.stdout.strip()[-300:])
            except subprocess.TimeoutExpired:
                self.write("clip_done", name=path.name, exit=None, out="timeout")

        thread = threading.Thread(target=record, name=path.name, daemon=True)
        thread.start()
        self.clips.append(thread)
        self.write("clip", name=path.name, seconds=seconds)

    def capture(self, name: str) -> str | None:
        """A running clip holds the capture lease, so retry past it (clips are
        at most 30 s here). None, logged, when no screenshot was written."""
        path, end = self.out / f"{name}.png", time.monotonic() + 45
        while True:
            if not self.game.alive():
                self.write("capture_skipped", name=name, reason="owned-game-exited")
                return None
            out = kh2ctl("capture", "--pid", str(self.game.pid), "--out", str(path))
            if path.exists():
                return path.name
            if time.monotonic() > end:
                self.write("capture_failed", name=name, error=out.get("error") or out)
                return None
            time.sleep(2)

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
            still = (start["pos"], time.monotonic())  # last position and when it last changed
            while not expired():
                s = self.state()
                if want and (s["world"], s["room"]) == want:
                    break
                if s["pos"] and still[0] and math.dist(s["pos"], still[0]) > 15:
                    still = (s["pos"], time.monotonic())
                elif s["free"] and time.monotonic() - still[1] > 1.5:
                    # A tutorial box ("Roxas's abilities passed on to Sora") blocks movement while
                    # the state still reads free (route-05). Enter dismisses it; in the field it is
                    # attack, never the Save reaction (Triangle).
                    self.kb.hold(set())
                    self.kb.tap("confirm")
                    still = (s["pos"], time.monotonic())
                    self.write("nudge", pos=s["pos"])
                    continue
                if not still[0]:
                    still = (s["pos"], still[1])
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
            if want and step.get("settle", True):  # opt-out hands a native event to the next owned pilot
                self.settle(True, 2.0, time.monotonic() + float(step.get("settleSec", 300)), step.get("menus"))
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

    def settle(self, advance: bool = True, calm: float = 2.0, deadline: float | None = None,
               menus: dict | None = None) -> None:
        """Wait until the room is free for `calm` seconds, pressing confirm through dialog.
        `menus` maps a known open-menu id to the keys that answer it, pressed once
        (route-05: the TT prologue ends in the forced party screen, menu 10)."""
        deadline = deadline or time.monotonic() + 300
        free_since, menu_since = None, None
        menus = dict(menus or {})
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
            # A menu with no event is not dialog. The pilot never answers one it
            # doesn't know: it could be a save prompt (route-04 stopped on menu 10
            # after the TT prologue finale). Record it and stop for a decision.
            if s["menu"] != 0xFF and not s["event"] and s["world"] != 0xFF:
                menu_since = menu_since or time.monotonic()
                keys = menus.pop(str(s["menu"]), None)
                if keys is not None and time.monotonic() - menu_since > 3:
                    self.capture(f"menu-{s['menu']}-before")
                    for key in keys:
                        self.kb.tap(key)
                        time.sleep(1.0)
                    self.capture(f"menu-{s['menu']}-after")
                    self.write("menu", menu=s["menu"], keys=keys)
                    menu_since = None
                    continue
                if keys is not None:
                    menus[str(s["menu"])] = keys  # not yet settled for 3 s; keep it for the next pass
                if time.monotonic() - menu_since > 15:
                    self.capture(f"menu-{s['menu']}")
                    raise Stuck(f"waiting on unknown menu {s['menu']}")
            else:
                menu_since = None
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
        """Fight until no living enemy is listed for `clearSec`.

        In battle it takes any offered reaction except Save (Reversal, a revive),
        at most every `reactGapSec` unless HP is falling. `heal` runs a menu key
        sequence (a Potion) below its threshold (`heal_due`), checked between swings
        too; a press that uses no Potion retries after 1 s without spending one.
        With pad input it guards (Square) when a swing burst is answered with damage.
        When recent damage says Roxas is pinned (`pinnedDrop` of max HP lost in
        2 s), it runs from the nearby enemies with a jump, trying the sides when
        a wall stops it. At 0 HP, Game Over (menu 8) gets Continue, the native
        retry, up to `retries` times. Without that menu the player is downed
        (co-op, VUH-1504) and waits up to `downedSec` for a teammate's revive."""
        reach, low = float(step.get("reach", 220)), float(step.get("lowHp", 0.35))
        stats = {"enemiesStart": None, "swings": 0, "reactions": {}, "minHp": None, "deaths": 0, "evades": 0,
                 "escapes": 0, "downed": 0, "heals": 0}
        calm_since, locked, heal_ready, reacted, window_tries = None, False, 0.0, 0.0, 0
        tutorial_confirms, tutorial_at = 0, 0.0
        blocked_approach, probe_count, probe_at = None, 0, -3.0
        guard = step.get("guard", "guard" if isinstance(self.kb, PadInput) else None)
        recent: list[tuple[float, int]] = []  # (time, hp) samples for the damage window
        first = self.state()
        room = (first["world"], first["room"])
        event_commands = set(step.get("eventReactions", ()))
        if not event_commands <= KNOWN_EVENT_REACTIONS or any(type(command) is not int for command in event_commands):
            raise ValueError("eventReactions permits only named Thorn commands297/298/299")
        if event_commands and not isinstance(self.kb, PadInput):
            raise ValueError("eventReactions requires pad input")
        event_scope = first["roomScope"]
        event_log = self.cooperative.own_log if self.cooperative else DownedLog(self.game.pid)
        event_presses, event_exhausted = 0, False
        heal = step.get("heal")  # {"below": 0.5, "keys": [...], "max": 3}: menu keys to use a Potion

        def hurting(s: dict, share: float) -> bool:
            now = time.monotonic()
            recent.append((now, s["hp"]))
            del recent[:-40]
            window = [hp for at, hp in recent if now - at <= 2.0]
            return max(window) - s["hp"] >= share * max(s["maxHp"], 1)

        def want_heal(s: dict) -> bool:
            now = time.monotonic()
            window = [hp for at, hp in recent if now - at <= 2.0]
            return heal_due(s["hp"], s["maxHp"], heal, stats["heals"], stats.get("healMisses", 0), heal_ready, now,
                            max(window, default=s["hp"]) - s["hp"])

        def heal_window(s: dict, foes: list[dict]) -> bool:
            # Menu presses during hit-stun don't use the Potion (route-02: 6 of 9, route-04: 0 of 3).
            # Heal only after a gap in the damage with no enemy close enough to start a combo.
            now, p = time.monotonic(), s["pos"] or (0.0, 0.0, 0.0)
            quiet = all(hp <= s["hp"] for at, hp in recent if now - at <= float(heal.get("quietSec", 0.7)))
            clear = all(math.hypot(f["position"]["x"] - p[0], f["position"]["z"] - p[2]) > float(heal.get("clear", 300))
                        for f in foes)
            return quiet and clear

        while time.monotonic() < deadline:
            s = self.state()
            event_rc = event_reaction_allowed(s, event_scope, event_commands)
            if self.cooperative and self.cooperative.step(self, s, event_rc):
                blocked_approach = None
                time.sleep(0.1)
                continue
            if event_rc:
                blocked_approach = None
                self.kb.hold(set())
                if not self.cooperative and (not event_log.update() or event_log.downed or event_log.full_party):
                    time.sleep(0.1)
                    continue
                if event_presses < 32 and time.monotonic() > reacted + float(step.get("reactGapSec", 0.8)):
                    reacted = time.monotonic()
                    event_presses += 1
                    stats["reactions"][s["react"]] = stats["reactions"].get(s["react"], 0) + 1
                    self.write("event_reaction", command=s["react"], attempt=event_presses, scope=list(event_scope))
                    self.kb.tap("react")
                elif event_presses >= 32 and not event_exhausted:
                    self.write("event_reaction_limit", attempts=event_presses, scope=list(event_scope))
                    event_exhausted = True
                time.sleep(0.1)
                continue
            if s["hp"] is None:
                blocked_approach = None
                self.kb.hold(set())
                time.sleep(0.3)
                continue
            stats["minHp"] = s["hp"] if stats["minHp"] is None else min(stats["minHp"], s["hp"])
            if s["hp"] <= 0 and s["world"] != 0xFF:
                blocked_approach = None
                self.kb.hold(set())
                if s["menu"] != GAME_OVER_MENU:
                    # Downed in co-op: no Game Over; wait (bounded) for a revive or the menu.
                    stats["downed"] += 1
                    self.write("downed", stats=stats)
                    until = time.monotonic() + float(step.get("downedSec", 60))
                    while time.monotonic() < until and (s["hp"] is None or s["hp"] <= 0) and s["menu"] != GAME_OVER_MENU:
                        time.sleep(0.5)
                        s = self.state()
                    if s["hp"] is not None and s["hp"] > 0:
                        self.write("revived", hp=s["hp"])
                        recent.clear()
                        continue
                    if s["menu"] != GAME_OVER_MENU:
                        self.capture("downed")
                        raise Stuck("downed and not revived")
                stats["deaths"] += 1
                self.write("death", stats=stats)
                if stats["deaths"] > int(step.get("retries", 2)):
                    raise Stuck(f"died {stats['deaths']} times")
                self.capture(f"gameover-{stats['deaths']}")
                self.kb.tap("confirm")  # Game Over opens on Continue
                time.sleep(3)
                self.settle(True, 1.0, time.monotonic() + 120)
                s = self.state()
                if (s["world"], s["room"]) != room:
                    raise Retry(f"Continue reloaded {s['world']}/{s['room']}")
                locked = False
                recent.clear()
                continue
            if not s["free"]:
                blocked_approach = None
                self.kb.hold(set())
                if s["event"]:
                    self.kb.tap("confirm")
                elif step.get("dismissComboTutorial", False) and isinstance(self.kb, PadInput) \
                        and s["inField"] and s["battle"] and s["menu"] == 0 \
                        and (s["world"], s["room"]) == room and tutorial_confirms < 4 \
                        and time.monotonic() - tutorial_at >= 1.0:
                    # VUH-2555/eddd8884: the first-hit combo box has menu=0,
                    # event=0. In this opted-in combat fixture only, Cross closes
                    # it through the pad mailbox. Never answer an unknown menu.
                    self.kb.tap("confirm")
                    tutorial_confirms += 1
                    tutorial_at = time.monotonic()
                    self.write("combat_tutorial_confirm", attempt=tutorial_confirms,
                               menu=s["menu"], event=s["event"], room=list(room))
                time.sleep(0.3)
                continue
            falling = hurting(s, 0.1)
            if s["react"] and s["battle"] and s["react"] not in SAVE_REACTIONS \
                    and (falling or time.monotonic() > reacted + float(step.get("reactGapSec", 0.8))):
                blocked_approach = None
                reacted = time.monotonic()
                stats["reactions"][s["react"]] = stats["reactions"].get(s["react"], 0) + 1
                self.kb.tap("react")
                continue
            foes = enemies(self.game.pid)
            if stats["enemiesStart"] is None:
                stats["enemiesStart"] = len(foes)
            if (s["world"], s["room"]) != room and s["world"] != 0xFF:
                # No death on the way: the won fight's story moved on (Basement Hall -> 2/20).
                self.kb.hold(set())
                return {**stats, "exitRoom": [s["world"], s["room"]]}
            if not foes:
                blocked_approach = None
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
            near = [f for f in foes if math.hypot(f["position"]["x"] - p[0], f["position"]["z"] - p[2]) < reach * 2]
            # A child pilot (route_pilot/thorn.py) may spend this turn on its own combat move; the base never does.
            if self.combat_turn(s, foes, target, gap, reach, stats):
                blocked_approach = None
                continue
            if want_heal(s):
                blocked_approach = None
            if want_heal(s) and not heal_window(s, foes) and window_tries < int(heal.get("escapes", 2)):
                # Make the window: run from the nearby enemies, then heal on a later pass.
                # Enemies that keep up get a best-effort heal after `escapes` tries.
                stats["escapes"] += 1
                window_tries += 1
                self.escape(s, near or foes)
                continue
            if want_heal(s):
                window_tries = 0
                self.kb.hold(set())
                for action in heal["keys"]:
                    self.kb.tap(action)
                    time.sleep(heal.get("gapMs", 250) / 1000)
                time.sleep(1.5)
                after = self.state()["hp"]
                self.write("heal", hp=s["hp"], after=after)
                if after is not None and after > s["hp"]:
                    stats["heals"] += 1
                    heal_ready = time.monotonic() + 3
                else:
                    heal_ready = time.monotonic() + 1  # a miss retries sooner and spends no Potion
                    # No Potion went through; back out of any submenu so later presses start clean.
                    stats["healMisses"] = stats.get("healMisses", 0) + 1
                    for action in heal.get("reset", ["SPACE", "SPACE"]):
                        self.kb.tap(action)
                        time.sleep(0.3)
                continue
            if near and step.get("escape", True) and hurting(s, float(step.get("pinnedDrop", 0.3))):
                blocked_approach = None
                stats["escapes"] += 1
                self.escape(s, near)
                recent.clear()
                continue
            if low and s["hp"] < low * max(s["maxHp"], 1) and gap < reach * 2:
                blocked_approach = None
                stats["evades"] += 1
                self.kb.hold(steer(s, (2 * p[0] - tx, 2 * p[2] - tz)))
                time.sleep(0.15)
                self.kb.tap("jump")
                time.sleep(0.6)
                self.kb.hold(set())
            elif gap > reach:
                # Opt-in Thorn stall experiment: root distance is not its head
                # distance. Only a reproduced short, blocked approach may probe.
                probe = step.get("blockedThornProbe", False) and isinstance(self.kb, PadInput) \
                    and target.get("objectId") == 307 and target.get("objectType") == 12 \
                    and s["roomScope"] == event_scope and s["playerPos"] is not None \
                    and s["react"] == 0 and not falling and reach < gap <= reach + 80
                if probe:
                    point, now = s["playerPos"], time.monotonic()
                    identity = (target["address"], target["hp"], s["player"])
                    if blocked_approach is None or blocked_approach[0] != identity \
                            or math.dist(point, blocked_approach[1]) >= 15 \
                            or math.hypot(tx - blocked_approach[3], tz - blocked_approach[4]) >= 15:
                        blocked_approach = (identity, point, now, tx, tz)
                    elif now - blocked_approach[2] >= 1.0 and probe_count < 3 and now - probe_at >= 3.0:
                        probe_count += 1
                        probe_at, blocked_approach = now, None
                        result = self.thorn_attack_probe(event_scope, target["address"], deadline)
                        self.write("blocked_thorn_probe", attempt=probe_count, gap=round(gap, 1), **result)
                        stats["swings"] += result["attacks"]
                        continue
                else:
                    blocked_approach = None
                self.kb.hold(steer(s, (tx, tz)))
                time.sleep(0.15)
            else:
                blocked_approach = None
                self.kb.hold(set())
                for _ in range(3):
                    self.kb.tap("attack", 70)
                    time.sleep(0.18)
                    stats["swings"] += 1
                    # A Dusk combo can take most of Roxas's HP inside one burst: recheck between swings.
                    now = self.state()
                    if now["hp"] is None or now["hp"] <= 0 or want_heal(now) or hurting(now, float(step.get("pinnedDrop", 0.3))) or \
                            (now["react"] and now["battle"] and now["react"] not in SAVE_REACTIONS):
                        break
                # Block the counter when the burst was answered with damage and an enemy is in reach.
                after_burst = self.state()
                if guard and gap < reach and after_burst["hp"] is not None and hurting(after_burst, 0.05):
                    stats["guards"] = stats.get("guards", 0) + 1
                    self.kb.tap(guard, 300)
        raise Stuck("fight did not clear")

    def combat_turn(self, s: dict, foes: list[dict], target: dict, gap: float, reach: float, stats: dict) -> bool:
        """Hook for child pilots: True when this fight turn was spent on their own move (it then restarts
        from fresh state). The base pilot never takes a turn, so its fights are unchanged."""
        return False

    def thorn_attack_probe(self, scope: tuple, target_address: str, deadline: float) -> dict:
        """One bounded native jump/two-Cross experiment; no direct state writes.

        Every action rechecks ordinary combat and cooperative priorities. A
        selected, live exact307 is required throughout; no inferred head pose.
        """
        end = min(deadline, time.monotonic() + 1.0)
        start_hp = None
        start_player = None
        result = {"attacks": 0, "jumped": False, "reason": "complete"}

        def ready(action_ms: int) -> bool:
            nonlocal start_hp, start_player
            if time.monotonic() >= end:
                result["reason"] = "deadline"
                return False
            try:
                now = self.state()
            except (OSError, ValueError):
                result["reason"] = "state-unavailable"
                return False
            if self.cooperative and self.cooperative.step(self, now):
                result["reason"] = "cooperative-priority"
                return False
            if not isinstance(self.kb, PadInput) or not now["free"] or not now["inField"] \
                    or now["menu"] != 255 or now["event"] or now["react"] \
                    or now["roomScope"] != scope or not now["player"] or not now["playerPos"] \
                    or now["hp"] is None or now["hp"] <= 0:
                result["reason"] = "combat-priority"
                return False
            if not self.cooperative:
                own_log = DownedLog(self.game.pid)
                if not own_log.update() or own_log.downed or own_log.full_party:
                    result["reason"] = "logical-state"
                    return False
            if start_hp is not None and now["hp"] < start_hp:
                result["reason"] = "incoming-damage"
                return False
            if start_player is not None and now["player"] != start_player:
                result["reason"] = "actor-replaced"
                return False
            start_hp = now["hp"] if start_hp is None else start_hp
            start_player = now["player"] if start_player is None else start_player
            try:
                foes = enemies(self.game.pid, timeout=max(0.01, end - time.monotonic()))
                point = now["playerPos"]
                selected = min(foes, key=lambda a: math.hypot(a["position"]["x"] - point[0],
                               a["position"]["z"] - point[2])) if foes else None
                live = selected is not None and selected["address"] == target_address \
                    and selected.get("objectId") == 307 and selected.get("objectType") == 12 \
                    and selected.get("hp", 0) > 0
            except (OSError, ValueError, subprocess.TimeoutExpired):
                live = False
            if not live:
                result["reason"] = "target-retired"
                return False
            if time.monotonic() + action_ms / 1000 >= end:
                result["reason"] = "deadline"
                return False
            return True

        self.kb.hold(set())
        try:
            if not ready(90):
                return result
            self.kb.tap("jump", 90)
            result["jumped"] = True
            time.sleep(min(0.1, max(0.0, end - time.monotonic())))
            for _ in range(2):
                if not ready(70):
                    break
                self.kb.tap("attack", 70)
                result["attacks"] += 1
                time.sleep(min(0.1, max(0.0, end - time.monotonic())))
        finally:
            self.kb.hold(set())
        return result

    def escape(self, s: dict, near: list[dict]) -> None:
        """Run from the nearby enemies' centroid with a jump. A wall stops the
        straight line, so when Roxas barely moves, try each side instead."""
        p = s["pos"] or (0.0, 0.0, 0.0)
        cx = sum(f["position"]["x"] for f in near) / len(near)
        cz = sum(f["position"]["z"] for f in near) / len(near)
        dx, dz = p[0] - cx, p[2] - cz
        if math.hypot(dx, dz) < 1:
            dx, dz = 1.0, 0.0
        for rx, rz in ((dx, dz), (-dz, dx), (dz, -dx)):
            now = self.state()
            start = now["pos"] or p
            self.kb.hold(steer(now, (start[0] + rx * 10, start[2] + rz * 10)))
            time.sleep(0.12)
            self.kb.tap("jump")
            time.sleep(0.5)
            self.kb.hold(set())
            moved = self.state()["pos"] or start
            if math.dist(moved, start) > 120:
                return

    def finish(self) -> None:
        self.kb.release()
        for thread in self.clips:
            thread.join(timeout=300)
        (self.out / "doors.json").write_text(json.dumps(self.doors, indent=1), encoding="utf-8")


def run_route(pilot: Pilot, route: dict, first: int) -> int:
    steps, i, retries = route["steps"], first, 0
    while i < len(steps):
        step = steps[i]
        pilot.write("step", index=i, step=step)
        if step["do"] == "stop":  # a parked route tail: the steps after it are kept, not run
            pilot.write("stopped", index=i, reason=step.get("note", ""))
            print(json.dumps({"ok": True, "steps": i - first, "retries": retries, "stoppedAt": i}))
            return 0
        t0 = time.monotonic()
        try:
            result = pilot.run_step(step)
        except Retry as why:
            retries += 1
            pilot.write("retry", index=i, reason=str(why), to=step.get("retryFrom", first))
            if "retryFrom" in step and retries <= int(route.get("retries", 3)):
                i = step["retryFrom"]
                continue
            why = Stuck(f"{why}; no retry left")
            pilot.kb.release()
            pilot.write("stuck", index=i, reason=str(why), state=pilot.last)
            print(json.dumps({"ok": False, "stuckAt": i, "reason": str(why)}))
            return 3
        except Stuck as why:
            pilot.kb.release()
            pilot.write("stuck", index=i, reason=str(why), state=pilot.last)
            if getattr(pilot, "external_evidence", False):
                pilot.write("evidence_external", owner="scenario")
            else:
                try:
                    if pilot.game.alive():
                        pilot.capture("stuck")
                    if pilot.game.alive():
                        pilot.clip("stuck", 10)
                except Exception as error:  # evidence is best effort; the stop isn't
                    pilot.write("evidence_error", error=str(error))
            print(json.dumps({"ok": False, "stuckAt": i, "reason": str(why)}))
            return 3
        pilot.write("done", index=i, seconds=round(time.monotonic() - t0, 1), result=result,
                    room=[pilot.last["world"], pilot.last["room"]] if pilot.last else None)
        i += 1
    print(json.dumps({"ok": True, "steps": len(steps) - first, "retries": retries}))
    return 0


def run_teach(pilot: Pilot, box: Path, seconds: float) -> int:
    """Executes box/cmd-*.json in name order and answers each in box/res-*.json.
    A command is {"op": "state"|"capture"|"key"|"stop"} or any route step.
    `key` holds raw keys ({"keys": ["W"], "ms": 400}) and reports the pad
    bits seen mid-hold: that is how the key map is calibrated."""
    box.mkdir(parents=True, exist_ok=True)
    for stale in sorted(box.glob("cmd-*.json")):  # left by a session that died; never replay them
        (box / stale.name.replace("cmd-", "res-")).write_text(
            json.dumps({"ok": False, "error": "stale: sent to an earlier session, not run"}), encoding="utf-8")
        stale.unlink()
        pilot.write("stale_cmd", cmd=stale.name)
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        try:
            pilot.state()  # door logging between commands
        except Stuck as why:
            if not pilot.game.alive():
                raise
            pilot.write("idle_stuck", reason=str(why))  # an operator is driving; don't end the session
            pilot.idle, pilot.mark = 0.0, None
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
        except (Stuck, Retry) as why:
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
    ap.add_argument("--input", choices=("pad", "keys"), default="pad")
    ap.add_argument("--revive-peer-pid", type=int, help="integrated pad-only cooperative fight with this owned game")
    ap.add_argument("--revive-hold-ms", type=int, help="cooperative revive hold (default 1600; Standard needs ~5600)")
    ap.add_argument("--external-evidence", action="store_true", help="scenario owns failure captures/clips")
    args = ap.parse_args()
    game = Game(args.pid)
    route = json.loads(args.route.read_text(encoding="utf-8")) if args.route else {}
    keys = {**KEYS, **route.get("keys", {})}
    if args.release_all:
        try:
            PadInput(game, keys).release_all()
        except OSError:
            pass
        # Pad mode never touched the OS keyboard, so it sends no key-ups either: a synthetic key-up
        # resets the PC's idle timer and wakes its monitor even with the game in the background.
        if args.input == "keys":
            Keyboard(game, keys).release_all()
        return 0
    if not owned(args.pid):
        print(json.dumps({"ok": False, "error": f"pid {args.pid} is not a rig-owned game"}))
        return 2
    if args.revive_peer_pid and (args.input != "pad" or args.revive_peer_pid == args.pid or not owned(args.revive_peer_pid)):
        print(json.dumps({"ok": False, "error": "revive peer requires pad input and a different rig-owned game"}))
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    pilot = Pilot(game, args.out, keys, args.input)
    pilot.external_evidence = args.external_evidence
    if args.revive_peer_pid:
        pilot.cooperative = CooperativeFight(game, Game(args.revive_peer_pid), args.revive_hold_ms)
    try:
        if args.route:
            return run_route(pilot, route, args.from_step)
        return run_teach(pilot, args.teach, args.seconds)
    finally:
        try:
            pilot.finish()
        finally:
            game.close_prompt()
            if pilot.cooperative:
                pilot.cooperative.peer.close_prompt()


if __name__ == "__main__":
    sys.exit(main())
