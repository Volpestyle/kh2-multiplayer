"""KH2 scenario runner (VUH-1488).

Runs JSON scenarios against rig-launched KH2 instances through kh2ctl and
writes a report per run. Usage:

    python tools/scenario/run.py SCENARIO.json [SCENARIO.json ...] [--repeat N]

Exit codes: 0 all passed, 1 a scenario failed, 2 a scenario crashed or hung,
3 the rig is unavailable (lock held, or a KH2 the rig didn't launch is
running, which means James is playing).

A scenario is {"name", "description", "protect": [instances], "steps": [...]}.
Each step is {"do": KIND, ...}; "instance" (default 0) picks the instance.
Expressions (assert, wait_until, save) are Python over the helpers in
Context.namespace(). See docs/SCENARIOS.md.
"""

from __future__ import annotations

import argparse
import ctypes
import datetime as dt
import hashlib
import json
import math
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys
import threading
import time
import traceback
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
KH2CTL = ROOT / "build" / "tools" / "kh2ctl" / "Release" / "kh2ctl.exe"
RIG = ROOT / "build" / "rig"
LOGS = RIG / "logs"
LOCK = RIG / "rig.lock"
RUNS = ROOT / "build" / "scenarios"
SAVE_DIR = Path(os.environ["USERPROFILE"]) / "OneDrive" / "Documents" / "My Games" / \
    "KINGDOM HEARTS HD 1.5+2.5 ReMIX"
STEAM_OWNED = {"steam_autocloud.vdf"}  # written by the Steam client, not the game

WARP_NAME = "Local\\kh2coop_warp_{pid}"
WARP_LIVE_FRAME = 76  # WarpChannel.liveFrame
HANG_SECONDS = 30.0   # gameplay frames stalled this long in the field = hang

EXIT_PASS, EXIT_FAIL, EXIT_CRASH, EXIT_RIG = 0, 1, 2, 3


class StepFailed(Exception):
    pass


class InstanceDied(Exception):
    def __init__(self, index: int, pid: int, kind: str):
        super().__init__(f"instance {index} (pid {pid}) {kind}")
        self.index, self.pid, self.kind = index, pid, kind


# --------------------------------------------------------------------------
# kh2ctl
# --------------------------------------------------------------------------

def kh2ctl(*args: str, pid: int | None = None, check: bool = True, timeout: float = 120,
           env: dict | None = None) -> dict:
    cmd = [str(KH2CTL), *map(str, args)]
    if pid is not None:
        cmd += ["--pid", str(pid)]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                          env=dict(os.environ, **env) if env else None)
    text = proc.stdout.strip()
    try:
        data = json.loads(text.splitlines()[-1]) if text else {}
    except json.JSONDecodeError:
        data = {"ok": False, "error": text or proc.stderr.strip()}
    if check and not data.get("ok", False):
        raise StepFailed(f"kh2ctl {' '.join(map(str, args))}: {data.get('error', data)}")
    return data


def pid_alive(pid: int) -> bool:
    handle = ctypes.windll.kernel32.OpenProcess(0x1000, False, pid)  # QUERY_LIMITED_INFORMATION
    if not handle:
        return False
    code = ctypes.c_ulong()
    ctypes.windll.kernel32.GetExitCodeProcess(handle, ctypes.byref(code))
    ctypes.windll.kernel32.CloseHandle(handle)
    return code.value == 259  # STILL_ACTIVE


# --------------------------------------------------------------------------
# Rig lock and save safety
# --------------------------------------------------------------------------

class RigLock:
    """One live lane: build/rig/rig.lock names the holder; a dead holder's
    lock is stale and taken over."""

    def __init__(self) -> None:
        self.held = False

    def acquire(self) -> str | None:
        LOCK.parent.mkdir(parents=True, exist_ok=True)
        for _ in range(2):
            try:
                fd = os.open(LOCK, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                try:
                    holder = json.loads(LOCK.read_text())
                except (OSError, json.JSONDecodeError):
                    holder = {}
                if holder.get("pid") and pid_alive(int(holder["pid"])):
                    return f"rig lock held by pid {holder['pid']} since {holder.get('since')}"
                LOCK.unlink(missing_ok=True)  # stale
                continue
            with os.fdopen(fd, "w") as fh:
                json.dump({"pid": os.getpid(), "since": dt.datetime.now().isoformat(timespec="seconds"),
                           "cmd": " ".join(sys.argv)}, fh)
            self.held = True
            return None
        return "could not take the rig lock"

    def release(self) -> None:
        if self.held:
            LOCK.unlink(missing_ok=True)
            self.held = False


def hash_saves() -> dict[str, str]:
    out = {}
    for path in sorted(SAVE_DIR.rglob("*")):
        if path.is_file():
            out[str(path.relative_to(SAVE_DIR))] = hashlib.sha256(path.read_bytes()).hexdigest()
    return out


def foreign_kh2() -> list[int]:
    return [i["processId"] for i in kh2ctl("instances")["instances"] if not i["owned"]]


# --------------------------------------------------------------------------
# Instances
# --------------------------------------------------------------------------

class Instance:
    def __init__(self, index: int, pid: int) -> None:
        self.index, self.pid = index, pid
        self.inject_log: Path | None = None  # exact registration from our launch result
        self.live_frame_seen: tuple[int, float] | None = None  # (frame, when it last changed)
        self.in_field = False
        self.expect_exit = False

    def live_frame(self) -> int | None:
        try:
            m = mmap.mmap(-1, 128, tagname=WARP_NAME.format(pid=self.pid), access=mmap.ACCESS_READ)
        except OSError:
            return None
        try:
            return struct.unpack_from("<i", m, WARP_LIVE_FRAME)[0]
        finally:
            m.close()

    def check(self) -> None:
        """Raises InstanceDied if the process exited or gameplay hung."""
        if not pid_alive(self.pid):
            raise InstanceDied(self.index, self.pid, "exited")
        if not self.in_field:
            return
        frame, now = self.live_frame(), time.monotonic()
        if frame is None:
            return
        if self.live_frame_seen is None or self.live_frame_seen[0] != frame:
            self.live_frame_seen = (frame, now)
        elif now - self.live_frame_seen[1] > HANG_SECONDS:
            raise InstanceDied(self.index, self.pid, f"hung (frame {frame} stalled {HANG_SECONDS:.0f}s)")


# --------------------------------------------------------------------------
# Run context
# --------------------------------------------------------------------------

class Context:
    def __init__(self, run_dir: Path) -> None:
        self.run_dir = run_dir
        self.instances: list[Instance] = []
        self.saved: dict = {}
        self.artifacts: list[str] = []
        self.protect: set[int] = set()
        self.processes: list[tuple] = []  # (name, Popen, log file) to stop at the end
        self.relay_port = "7782"
        self._stop = threading.Event()
        self._protector = threading.Thread(target=self._protect_loop, daemon=True)
        self._protector.start()

    def inst(self, index: int) -> Instance:
        if index >= len(self.instances):
            raise StepFailed(f"instance {index} isn't running (boot or launch it first)")
        return self.instances[index]

    def check_all(self) -> None:
        for inst in self.instances:
            if not inst.expect_exit:
                inst.check()

    def sleep(self, seconds: float) -> None:
        end = time.monotonic() + seconds
        while True:
            self.check_all()
            left = end - time.monotonic()
            if left <= 0:
                return
            time.sleep(min(0.5, left))

    # ---- state helpers for expressions ----
    def actors(self, index: int = 0) -> list[dict]:
        return self.entities(index)["actors"]

    def entities(self, index: int = 0) -> dict:
        """kh2ctl entities: world, room and every actor."""
        return kh2ctl("entities", pid=self.inst(index).pid)

    def actor(self, name: str, index: int = 0) -> dict:
        for a in self.actors(index):
            if a["name"] == name:
                return a
        raise StepFailed(f"no actor named {name} on instance {index}")

    def namespace(self) -> dict:
        def peek(rva: int | str, kind: str = "u32", index: int = 0):
            key = rva if isinstance(rva, str) else f"0x{rva:X}"
            sample = kh2ctl("peek", "--rva", f"{key}:{kind}", pid=self.inst(index).pid)["samples"][0]
            return next(v for k, v in sample.items() if k != "t")

        def pos(name: str = "P_EX100", index: int = 0):
            p = self.actor(name, index)["position"]
            return (p["x"], p["y"], p["z"])

        def room(index: int = 0):
            r = kh2ctl("state", pid=self.inst(index).pid)["room"]
            return (r["worldId"], r["roomId"])

        def location(index: int = 0) -> list[int]:
            # One sample of NOW; commit stores the entrance as a byte at +2.
            fields = ("0x717008:u8", "0x717009:u8", "0x71700A:u8",
                      "0x71700C:u16", "0x71700E:u16", "0x717010:u16")
            sample = kh2ctl("peek", "--rva", ",".join(fields),
                            pid=self.inst(index).pid)["samples"][0]
            return [sample[f.split(":")[0]] for f in fields]

        def log_matches(pattern: str, index: int = 0) -> list[dict]:
            path = LOGS / f"kh2coop_inject_{self.inst(index).pid}.log"
            text = path.read_text(errors="replace") if path.exists() else ""
            return [match.groupdict() for match in re.finditer(pattern, text)]

        def enemies(index: int = 0):
            return [a for a in self.actors(index) if a.get("objectType") in (3, 4)]

        def enemy_hps(index: int = 0) -> list:
            """Sorted (name, hp) of live combat enemies (stats, not F_)."""
            return sorted((a["name"], a["hp"]) for a in self.actors(index)
                          if a.get("objectType") in (3, 4) and not a["name"].startswith("F_")
                          and a.get("hp", -1) > 0)

        def log_count(pattern: str, index: int = 0) -> int:
            path = LOGS / f"kh2coop_inject_{self.inst(index).pid}.log"
            return sum(pattern in line for line in path.read_text(errors="replace").splitlines())

        def dist(a, b) -> float:
            return math.dist(a, b)

        def runtime_log(index: int = 0) -> str:
            path = self.run_dir / f"runtime_{index}.log"
            return path.read_text(errors="replace") if path.exists() else ""

        def bridge(index: int = 0, seconds: float = 0.5) -> dict:
            """The instance's AvatarBridge: local frames/s and both puppet slots."""
            out = subprocess.run([str(AVATARCTL), "peek", "--pid", str(self.inst(index).pid),
                                  "--seconds", str(seconds)], capture_output=True, text=True, timeout=30)
            return json.loads(out.stdout.strip().splitlines()[-1])

        return {"peek": peek, "pos": pos, "room": room, "enemies": enemies, "enemy_hps": enemy_hps, "actor": self.actor,
                "actors": self.actors, "log_count": log_count, "dist": dist, "saved": self.saved,
                "puppet_error": puppet_error, "bridge": bridge, "runtime_log": runtime_log, "jitter_stats": jitter_stats,
                "len": len, "abs": abs, "min": min, "max": max, "any": any, "all": all,
                "location": location, "log_matches": log_matches,
                "range": range, "round": round, "int": int, "str": str,
                "chests": lambda index=0: chest_records(self, index)}

    def eval(self, expr: str):
        # Helpers go in globals: comprehensions inside an expression only see globals.
        return eval(expr, {"__builtins__": {}, **self.namespace()})  # noqa: S307 (repo-owned scenarios)

    # ---- Sora protection (team 0 is in no attack's hit mask) ----
    def _protect_loop(self) -> None:
        while not self._stop.wait(1.0):
            for index in list(self.protect):
                try:
                    inst = self.instances[index]
                    sora = next(a for a in self.actors(index) if a["name"] == "P_EX100")
                    addr = int(sora["address"], 16) + 0x4DC
                    kh2ctl("poke", "--addr", f"0x{addr:X}", "--type", "u32", "--value", "0",
                           pid=inst.pid, check=False)
                except Exception:  # noqa: BLE001 - best effort; loading rooms have no Sora
                    pass

    def close(self) -> None:
        self._stop.set()
        for value in self.saved.values():
            if isinstance(value, Recorder):
                value.stop()
        for name, proc, log in reversed(self.processes):  # runtimes before the relay
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
            log.close()



def chest_records(ctx: Context, index: int = 0) -> list[dict]:
    """Read bounded treasure records only from native F_* actors."""
    pid = ctx.inst(index).pid
    header = kh2ctl("peek", "--rva", "0x717008:u8,0x717009:u8", pid=pid)
    if "moduleBase" not in header:
        raise StepFailed("chests requires kh2ctl peek moduleBase metadata")
    base = int(str(header["moduleBase"]), 0)
    location = [header["samples"][0][key] for key in ("0x717008", "0x717009")]

    def at(address: int, kind: str):
        rva = (address - base) & ((1 << 64) - 1)
        key = f"0x{rva:X}"
        sample = kh2ctl("peek", "--rva", f"{key}:{kind}", pid=pid)["samples"][0]
        return sample[key]

    result = []
    for actor in ctx.actors(index):
        if not actor.get("name", "").startswith("F_"):
            continue
        pointer = int(str(at(int(actor["address"], 16) + 0xC00, "u64")), 0)
        if not 0x10000 <= pointer < 0x0000800000000000 or pointer % 2:
            continue
        try:
            schema = (("treasureId", 0, "u16"), ("itemId", 2, "u16"), ("type", 4, "u8"),
                      ("world", 5, "u8"), ("room", 6, "u8"), ("roomIndex", 7, "u8"),
                      ("event", 8, "u16"), ("flag", 10, "u16"))
            keys = [f"0x{(pointer + offset - base) & ((1 << 64) - 1):X}" for _, offset, _ in schema]
            sample = kh2ctl("peek", "--rva", ",".join(f"{key}:{kind}" for key, (_, _, kind) in zip(keys, schema)),
                            pid=pid)["samples"][0]
            fields = {name: sample[key] for key, (name, _, _) in zip(keys, schema)}
        except StepFailed:
            continue  # F_* also contains non-chest field objects.
        if fields["type"] != 0 or [fields["world"], fields["room"]] != location or not 1 <= fields["flag"] <= 411:
            continue
        byte_rva = 0x9ABC5C + fields["flag"] // 8
        raw = ctx.namespace()["peek"](byte_rva, "u8", index)
        result.append({**actor, **fields, "treasureRecord": f"0x{pointer:X}",
                       "flagRva": f"0x{byte_rva:X}", "flagByte": raw,
                       "openedBit": bool(raw & (1 << (fields["flag"] % 8)))})
    return result


# --------------------------------------------------------------------------
# Steps
# --------------------------------------------------------------------------

def title_rows(ctx: Context, inst: Instance, shot: Path) -> dict:
    """Which title-menu row is highlighted (orange bar right of the text)."""
    from PIL import Image

    data = kh2ctl("capture", "--out", str(shot), pid=inst.pid, check=False)
    if not data.get("ok"):
        return {"new": False, "load": False}
    with Image.open(shot) as img:
        rgb = img.convert("RGB")
        w, h = rgb.size

        def orange(y: int) -> bool:
            for x in (1700, 1800):
                r, g, b = rgb.getpixel((int(x * w / 1920), int(y * h / 1080)))
                if not (r > 190 and 60 < g < 170 and b < 100):
                    return False
            return True

        return {"new": orange(825), "load": orange(915)}


def wait_for(ctx: Context, cond, what: str, timeout: float, poll: float = 0.7) -> None:
    end = time.monotonic() + timeout
    while not cond():
        if time.monotonic() > end:
            raise StepFailed(f"{what} not reached within {timeout:.0f}s")
        ctx.sleep(poll)


def step_launch(ctx: Context, step: dict) -> dict:
    init_timeout_ms = step.get("initTimeoutMs", 15000)
    if type(init_timeout_ms) is not int or not 1 <= init_timeout_ms <= 60000:
        raise StepFailed("launch initTimeoutMs must be an integer from 1 to 60000")
    # step "env" reaches the game (kh2ctl launch passes its environment on),
    # e.g. {"KH2COOP_PUPPET_TRACE": "1"}.
    # Keep the existing 120s CLI budget, plus any additional init wait. This
    # also covers the CLI's window, settle and injection waits (max 165s).
    data = kh2ctl("launch", "--init-timeout-ms", str(init_timeout_ms), check=False,
                  timeout=120 + max(0, init_timeout_ms - 15000) / 1000,
                  env={k: str(v) for k, v in step.get("env", {}).items()})
    if type(data) is not dict:
        raise StepFailed("kh2ctl launch: invalid reply")
    pid = data.get("processId")
    if type(pid) is not int or not 0 < pid <= 0xFFFFFFFF or data.get("command") != "launch":
        raise StepFailed(f"kh2ctl launch: invalid launched processId/command: {data.get('error', data)}")
    inst = Instance(len(ctx.instances), pid)
    # Initialization can fail after launch succeeds. Retain that exact process
    # for run_scenario's owned kh2ctl cleanup before propagating the failure.
    ctx.instances.append(inst)
    log = data.get("log")
    if log is not None:
        if type(log) is not str:
            raise StepFailed("kh2ctl launch: invalid inject log path")
        if log:
            inst.inject_log = Path(log).resolve()
    if data.get("ok") is not True:
        raise StepFailed(f"kh2ctl launch: {data.get('error', data)}")
    if step.get("mute", True):
        kh2ctl("mute", pid=inst.pid, check=False)
    return {"processId": inst.pid, "instance": inst.index}


def press(inst: Instance, button: str) -> None:
    """A pad press injected through the DLL's input collector: no window
    focus needed, so a dialog in the foreground can't break a boot."""
    kh2ctl("player-press", "--button", button, "--duration-ms", "150", pid=inst.pid)


def step_boot(ctx: Context, step: dict) -> dict:
    """Launch, then load the save list's default (last-used) slot. Each menu
    move is checked in an in-renderer capture."""
    result = step_launch(ctx, step)
    inst = ctx.instances[-1]
    shot = ctx.run_dir / f"boot_{inst.index}.png"
    timeout = float(step.get("timeoutSec", 120))
    wait_for(ctx, lambda: title_rows(ctx, inst, shot)["new"], "title menu", timeout)
    ctx.sleep(1)
    for _ in range(5):
        if title_rows(ctx, inst, shot)["load"]:
            break
        press(inst, "down")
        ctx.sleep(0.7)
    else:
        raise StepFailed("LOAD never highlighted")
    press(inst, "cross")
    ctx.sleep(2)

    def in_save_list() -> bool:
        rows = title_rows(ctx, inst, shot)
        return not rows["new"] and not rows["load"]

    wait_for(ctx, in_save_list, "save list", timeout)
    ctx.sleep(1)
    press(inst, "cross")

    def loaded() -> bool:
        s = kh2ctl("peek", "--rva", "0x9BA8D0:u8,0x717008:u8", pid=inst.pid)["samples"][0]
        return s["0x9BA8D0"] != 0 and s["0x717008"] != 255

    wait_for(ctx, loaded, "loaded room", timeout)
    ctx.sleep(2)
    inst.in_field = True
    shot.unlink(missing_ok=True)
    r = kh2ctl("state", pid=inst.pid)["room"]
    result.update(world=r["worldId"], room=r["roomId"])
    return result


def step_warp(ctx: Context, step: dict) -> dict:
    args = ["warp", "--world", step["world"], "--room", step["room"]]
    for key, flag in (("door", "--door"), ("map", "--map"), ("btl", "--btl"), ("evt", "--evt")):
        if key in step:
            args += [flag, step[key]]
    data = kh2ctl(*map(str, args), pid=ctx.inst(step.get("instance", 0)).pid, timeout=60)
    return {"seconds": data.get("seconds"), "room": data.get("room")}


def step_input(ctx: Context, step: dict) -> dict:
    args = ["player-input", "--duration-ms", str(step.get("ms", 500))]
    for axis in ("lx", "ly", "rx", "ry"):
        if axis in step:
            args += [f"--{axis}", str(step[axis])]
    kh2ctl(*args, pid=ctx.inst(step.get("instance", 0)).pid)
    return {}


def step_align_courtyard_exit(ctx: Context, step: dict) -> dict:
    """Bounded door-0 fixture alignment, not general navigation.

    This camera faces out from the castle stairs: screen-left should reduce
    the negative X offset toward the doorway centerline at X=0. Check that claim
    after every pulse instead of continuing with a wrong camera orientation.
    """
    index = step.get("instance", 1)
    name = step.get("as", "courtyard_alignment")
    helpers = ctx.namespace()
    evidence = {"instance": index, "start": helpers["pos"](index=index), "pulses": []}
    deadline = time.monotonic() + 6.0
    try:
        if helpers["location"](index)[:3] != [5, 6, 0]:
            raise StepFailed("courtyard alignment requires BC 05/06 entrance 0")
        position = evidence["start"]
        while True:
            if ctx.eval(step["blockedExpr"]):
                evidence["stopped"] = "native exit blocked"
                break
            if abs(position[0]) < 60:
                evidence["stopped"] = "centered"
                break
            if time.monotonic() + 0.25 > deadline:
                raise StepFailed("courtyard alignment did not reach |x| < 60 within 6 seconds")
            start = position
            kh2ctl("player-input", "--lx", "-1", "--duration-ms", "250", pid=ctx.inst(index).pid)
            ctx.check_all()
            position = helpers["pos"](index=index)
            evidence["pulses"].append({"start": start, "end": position, "lx": -1, "ms": 250})
            if ctx.eval(step["blockedExpr"]):
                evidence["stopped"] = "native exit blocked"
                break
            if abs(position[0]) >= abs(start[0]):
                raise StepFailed(f"courtyard alignment pulse did not reduce |x|: {start[0]:.2f} -> "
                                 f"{position[0]:.2f}; check camera direction and obstruction")
        evidence["end"] = position
    except StepFailed as error:
        evidence["error"] = str(error)
        try:
            step_capture(ctx, {"instance": index, "name": f"{name}_failed"})
        except Exception as capture_error:
            evidence["captureError"] = str(capture_error)
        path = ctx.run_dir / f"{name}_failed.json"
        path.write_text(json.dumps(evidence, indent=2))
        ctx.artifacts.append(path.name)
        raise
    ctx.saved[name] = evidence
    return evidence


def step_press(ctx: Context, step: dict) -> dict:
    pid = ctx.inst(step.get("instance", 0)).pid
    for i in range(int(step.get("times", 1))):
        kh2ctl("player-press", "--button", step["button"], "--duration-ms", str(step.get("ms", 80)), pid=pid)
        if i + 1 < int(step.get("times", 1)):
            ctx.sleep(step.get("gapMs", 300) / 1000)
    return {}


def step_wait(ctx: Context, step: dict) -> dict:
    ctx.sleep(step.get("ms", 1000) / 1000)
    return {}


def step_wait_until(ctx: Context, step: dict) -> dict:
    wait_for(ctx, lambda: bool(ctx.eval(step["expr"])), step["expr"],
             step.get("timeoutMs", 10000) / 1000, step.get("pollMs", 500) / 1000)
    return {}


def step_assert(ctx: Context, step: dict) -> dict:
    value = ctx.eval(step["expr"])
    if not value:
        raise StepFailed(step.get("message", f"assertion failed: {step['expr']}"))
    return {"value": repr(value)}


def step_save(ctx: Context, step: dict) -> dict:
    ctx.saved[step["as"]] = ctx.eval(step["expr"])
    return {step["as"]: repr(ctx.saved[step["as"]])}


def step_protect(ctx: Context, step: dict) -> dict:
    index = step.get("instance", 0)
    ctx.inst(index)
    (ctx.protect.add if step.get("on", True) else ctx.protect.discard)(index)
    return {}


def step_capture(ctx: Context, step: dict) -> dict:
    inst = ctx.inst(step.get("instance", 0))
    out = ctx.run_dir / f"{step.get('name', 'capture')}_{inst.index}.png"
    receipt = kh2ctl("capture", "--out", str(out), pid=inst.pid)
    ctx.artifacts.append(out.name)
    return {"path": out.name, "receipt": receipt}


def step_clip(ctx: Context, step: dict) -> dict:
    inst = ctx.inst(step.get("instance", 0))
    out = ctx.run_dir / f"{step.get('name', 'clip')}_{inst.index}.mp4"
    kh2ctl("clip", "--seconds", str(step.get("seconds", 5)), "--out", str(out), pid=inst.pid,
           timeout=120)
    ctx.artifacts.append(out.name)
    return {"path": out.name}


def step_crash(ctx: Context, step: dict) -> dict:
    """Test hook: faults the instance (kh2ctl crash). The runner should then
    produce a crash bundle and a failed report."""
    kh2ctl("crash", pid=ctx.inst(step.get("instance", 0)).pid)
    ctx.sleep(step.get("ms", 5000) / 1000)
    return {}


def step_kh2ctl(ctx: Context, step: dict) -> dict:
    """Any kh2ctl command against one instance, e.g. ["overlay", "on"]."""
    args = ctx.eval(step["argsExpr"]) if "argsExpr" in step else step["args"]
    return kh2ctl(*map(str, args), pid=ctx.inst(step.get("instance", 0)).pid)


def step_hit_all(ctx: Context, step: dict) -> dict:
    """kh2ctl hit damage/kill on every live combat enemy of one instance
    (objentry type 3/4 with stats, not F_). For host-only damage tests."""
    inst = ctx.inst(step.get("instance", 0))
    done = 0
    for a in ctx.actors(inst.index):
        if a.get("objectType") in (3, 4) and not a["name"].startswith("F_") and a.get("hp", -1) > 0:
            args = ["hit", step.get("op", "damage"), "--victim", a["address"]]
            if step.get("op", "damage") == "damage":
                args += ["--amount", str(step.get("amount", 1))]
            if kh2ctl(*args, pid=inst.pid, check=False).get("ok"):
                done += 1
    return {"hit": done}


def step_freeze(ctx: Context, step: dict) -> dict:
    """Test hook: suspends the instance so gameplay frames stop, then waits
    past the hang threshold. The runner should dump it and report HANG."""
    inst = ctx.inst(step.get("instance", 0))
    handle = ctypes.windll.kernel32.OpenProcess(0x0800, False, inst.pid)  # SUSPEND_RESUME
    if not handle:
        raise StepFailed("OpenProcess(SUSPEND_RESUME) failed")
    try:
        ctypes.windll.ntdll.NtSuspendProcess(handle)
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)
    ctx.sleep(HANG_SECONDS + 10)
    return {}


# ---- Networking (VUH-1492) ----

SERVER = ROOT / "build" / "Release" / "kh2coop_server.exe"
RUNTIME = ROOT / "build" / "Release" / "kh2coop_runtime_scaffold.exe"
AVATARCTL = ROOT / "build" / "Release" / "avatarctl.exe"


def start_process(ctx: Context, name: str, cmd: list[str], env: dict | None = None) -> subprocess.Popen:
    log = open(ctx.run_dir / f"{name}.log", "w")  # noqa: SIM115 - closed with the process
    proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    ctx.processes.append((name, proc, log))
    ctx.artifacts.append(f"{name}.log")
    return proc


def step_relay(ctx: Context, step: dict) -> dict:
    """Start the relay (kh2coop_server) on loopback for this run."""
    port = str(step.get("port", 7782))
    # The relay's version gate must match what the runtime sends (its
    # defaults: build 1.0.0.10-steam-global, content none, mod empty).
    gate = ["--build", step.get("build", "1.0.0.10-steam-global"),
            "--content", step.get("content", "none"), "--mod", step.get("mod", "")]
    proc = start_process(ctx, "relay", [str(SERVER), "--port", port, *gate,
                                        *map(str, step.get("args", [])),
                                        "--desync-dir", str((ctx.run_dir / "desync-reports").resolve())])
    ctx.sleep(1)
    if proc.poll() is not None:
        raise StepFailed(f"relay exited with {proc.returncode}")
    ctx.relay_port = port
    return {"pid": proc.pid, "port": port}


def step_runtime(ctx: Context, step: dict) -> dict:
    """Start a runtime bound to one instance (--pid) and connect it to
    the relay. Waits for verified membership; native bootstrap is checked separately."""
    inst = ctx.inst(step.get("instance", 0))
    cmd = [str(RUNTIME), "--network", "--server", step.get("server", "127.0.0.1"), "--port", str(ctx.relay_port),
           "--pid", str(inst.pid), "--role", step["role"],
           "--peer-id", step.get("peerId", f"peer{inst.index}"), "--no-camera"]
    # Impairment, applied by the runtime to both directions: owner -> viewer
    # crosses two runtimes, so 50 ms here is 100 ms end to end.
    link = step.get("link", {})
    for key, flag in (("latencyMs", "--link-latency-ms"), ("jitterMs", "--link-jitter-ms"),
                      ("lossPct", "--link-loss")):
        if key in link:
            cmd += [flag, str(link[key])]
    cmd += [str(a) for a in step.get("args", [])]
    # Keep diagnostics inside this run and register only our launch-owned log.
    cmd += ["--desync-dir", str((ctx.run_dir / "desync-local" / f"peer_{inst.index}").resolve())]
    if inst.inject_log is not None:
        cmd += ["--inject-log", str(inst.inject_log)]
    name = f"runtime_{inst.index}"
    # A second writer would also truncate the original identity/recovery log.
    if any(owner == name for owner, _, _ in ctx.processes) or (ctx.run_dir / f"{name}.log").exists():
        raise StepFailed(f"runtime {inst.index} already has an owned process/log; duplicate launch refused")
    for owner, existing, _ in ctx.processes:
        args = existing.args if isinstance(existing.args, (list, tuple)) else []
        if owner.startswith("runtime_") and existing.poll() is None and any(
                args[n] == "--pid" and n + 1 < len(args) and str(args[n + 1]) == str(inst.pid)
                for n in range(len(args))):
            raise StepFailed(f"game {inst.pid} already has a live runtime writer")
    proc = start_process(ctx, name, cmd)
    log = ctx.run_dir / f"{name}.log"
    expect = step.get("expect", "Verified membership; native bootstrap remains separate")

    def ready() -> bool:
        text = log.read_text(errors="replace")
        if expect in text:
            return True
        if proc.poll() is not None:
            raise StepFailed(f"runtime {inst.index} exited with {proc.returncode}: {text[-300:]}")
        return False

    wait_for(ctx, ready, f"runtime {inst.index}: {expect!r}", step.get("timeoutMs", 15000) / 1000, 0.5)
    return {"pid": proc.pid, "role": step["role"], "link": link}


ARRIVAL_PATTERN = (r"\[enemysync\] (?:host|client) arrived epoch=(?P<epoch>\d+) "
                   r"room=(?P<world>[0-9A-Fa-f]+)/(?P<room>[0-9A-Fa-f]+) "
                   r"door=(?P<door>\d+) map=(?P<map>\d+) btl=(?P<btl>\d+) evt=(?P<evt>\d+)")


def native_puppet_actors(ctx: Context, index: int, *, _read=None, _entities=None) -> dict:
    """Resolve the DLL's actual friend-slot targets in the active entity list.

    Companion pointers live in unit slot 1, at SLOT0_BASE + SLOT_STRIDE +
    0x220/0x228. Sora clones bypass those pointers: use the DLL's per-slot
    motion-driver actor log from this load, never actor names or list order.
    GameBridge's state command can return a default ActorState for an absent
    actor, so it is not evidence that a native friend actor exists.
    """
    keys = ("0x2A239B0", "0x2A239B8")
    sample = (_read or kh2ctl)("peek", "--rva", ",".join(f"{key}:u64" for key in keys),
                    pid=ctx.inst(index).pid)["samples"][0]
    pointers = [int(sample[key], 16) for key in keys]
    entities = (_entities or ctx.entities)(index)
    active = {int(actor["address"], 16): actor for actor in entities.get("actors", [])}
    local = int(entities["actors"][0]["address"], 16) if entities.get("actors") else 0
    clones = [address for address, actor in active.items()
              if address != local and actor.get("objectType") == 0]
    mode = "friendPointers"
    friend_pointers = [f"0x{p:X}" for p in pointers]
    if clones:
        mode = "cloneDriverLogs"
        path = LOGS / f"kh2coop_inject_{ctx.inst(index).pid}.log"
        text = path.read_text(errors="replace") if path.exists() else ""
        load = text.rfind("[warp] load complete")
        pointers = [0, 0]
        # A stale actor address can be reused in the next room. Only accept
        # driver bindings observed after this load's genuine completion.
        for match in re.finditer(r"\[puppet ([01])\] frame \d+ motion [^\r\n]* actor=(?:0x)?([0-9A-Fa-f]+)",
                                 text[load:] if load >= 0 else ""):
            address = int(match[2], 16)
            pointers[int(match[1])] = address if address in clones else 0
    return {"pointers": [f"0x{p:X}" for p in pointers], "localAddress": f"0x{local:X}",
            "source": mode, "friendPointers": friend_pointers,
            "cloneCandidates": [f"0x{p:X}" for p in clones],
            "room": [entities.get("world"), entities.get("room")],
            "actors": [active.get(p) if p and p != local and pointers.count(p) == 1 else None
                       for p in pointers]}


def transition_evidence(ctx: Context, instances: list[int], after_epoch: int = -1) -> dict:
    """Read settled-load evidence plus actual location and puppet slot state.

    Instance indices match session slots in these fixtures (host starts first).
    Bridge activity alone only proves the runtime published a pose: also check
    each native friend actor exists and is near that pose after settling.
    """
    helpers = ctx.namespace()
    evidence: dict = {"ready": False, "instances": {}, "problems": []}
    problems = evidence["problems"]
    for index in instances:
        arrivals = helpers["log_matches"](ARRIVAL_PATTERN, index)
        arrival = arrivals[-1] if arrivals else None
        data = {"location": helpers["location"](index), "arrival": arrival,
                "bridge": helpers["bridge"](index, 0.1),
                "nativePuppets": native_puppet_actors(ctx, index)}
        evidence["instances"][str(index)] = data
        if arrival is None:
            problems.append(f"instance {index}: no completed-load arrival")
            continue
        data["epoch"] = int(arrival["epoch"])
        target = [int(arrival[k], 16 if k in ("world", "room") else 10)
                  for k in ("world", "room", "door", "map", "btl", "evt")]
        if data["epoch"] <= after_epoch:
            problems.append(f"instance {index}: epoch did not advance past {after_epoch}")
        if data["location"] != target:
            problems.append(f"instance {index}: current full location differs from arrival")
        if data["nativePuppets"]["room"] != target[:2]:
            problems.append(f"instance {index}: native entity list is not in the target room")
        b = data["bridge"]
        if not b.get("local") or b["local"].get("room") != target[:2] or b.get("localFramesPerSecond", 0) <= 0:
            problems.append(f"instance {index}: local avatar is not live in the target room")
        expected = [owner for owner in range(3) if owner != index]
        for slot, owner in enumerate(expected):
            puppet = b.get(f"puppet{slot}") or {}
            if owner not in instances:
                continue
            pose = puppet.get("pose") or {}
            actor = data["nativePuppets"]["actors"][slot]
            if not puppet.get("active") or pose.get("owner") != owner or pose.get("room") != target[:2]:
                problems.append(f"instance {index}: puppet {slot} is not active for owner {owner}")
            elif not actor:
                pointer = data["nativePuppets"]["pointers"][slot]
                problems.append(f"instance {index}: native friend slot {slot + 1} target {pointer} "
                                "is absent from the active entity list or aliases another party slot")
            else:
                point = actor["position"]
                error = math.dist([point[k] for k in ("x", "y", "z")], pose["pos"])
                data.setdefault("puppetErrors", {})[str(slot)] = round(error, 2)
                if error > 100:
                    problems.append(f"instance {index}: friend slot {slot + 1} is {error:.1f} units off its pose")
    host = evidence["instances"][str(instances[0])]
    evidence["epoch"] = host.get("epoch", -1)
    relay_path = ctx.run_dir / "relay.log"
    relay_text = relay_path.read_text(errors="replace") if relay_path.exists() else ""
    evidence["acks"] = [match.groupdict() for match in re.finditer(
        r"\[SessionHost\] TransitionAck slot=(?P<slot>\d+) epoch=(?P<epoch>\d+) "
        r"room=(?P<world>[0-9A-Fa-f]+)/(?P<room>[0-9A-Fa-f]+) arrived=(?P<arrived>[01])", relay_text)]
    for index in instances[1:]:
        data = evidence["instances"][str(index)]
        if data.get("epoch") != host.get("epoch") or data["location"] != host["location"]:
            problems.append(f"instance {index}: full location or epoch differs from host")
        if not any(int(ack["slot"]) == index and int(ack["epoch"]) == evidence["epoch"]
                   and ack["arrived"] == "1"
                   and [int(ack[k], 16) for k in ("world", "room")] == host["location"][:2]
                   for ack in evidence["acks"]):
            problems.append(f"instance {index}: relay has no successful acknowledgement for current epoch")
    evidence["ready"] = not problems
    return evidence


def step_transition_check(ctx: Context, step: dict) -> dict:
    name = step.get("as", "transition")
    after = ctx.saved[step["after"]]["epoch"] if "after" in step else -1
    latest = {}

    def ready() -> bool:
        nonlocal latest
        latest = transition_evidence(ctx, step.get("instances", [0, 1, 2]), after)
        if "target" in step and latest["instances"]["0"]["location"][:2] != step["target"]:
            latest["problems"].append("host did not reach requested world/room")
            latest["ready"] = False
        return latest["ready"]

    def capture_samples(suffix: str) -> None:
        for index in step.get("instances", [0, 1, 2]):
            try:
                step_capture(ctx, {"instance": index, "name": f"{name}_{suffix}"})
            except Exception as error:  # keep diagnostic capture failure from hiding the checkpoint failure
                latest.setdefault("captureErrors", []).append(f"instance {index}: {error}")

    try:
        wait_for(ctx, ready, f"transition checkpoint {name}", step.get("timeoutMs", 30000) / 1000, 0.5)
    except StepFailed:
        capture_samples("failed")
        path = ctx.run_dir / f"{name}_failed.json"
        path.write_text(json.dumps(latest, indent=2))
        ctx.artifacts.append(path.name)
        raise StepFailed(f"transition checkpoint {name}: {latest.get('problems', [])}") from None
    if step.get("capture", False):
        capture_samples("passed")
    ctx.saved[name] = latest
    return latest



def reconnect_runtime(ctx: Context, index: int):
    matches = [proc for name, proc, _ in ctx.processes if name == f"runtime_{index}"]
    if len(matches) != 1 or matches[0].poll() is not None:
        raise StepFailed(f"reconnect requires exactly one live owned runtime for instance {index}")
    args = matches[0].args
    expected_role = ("player", "friend1", "friend2")[index]
    if not isinstance(args, (tuple, list)) or Path(args[0]).resolve() != RUNTIME.resolve():
        raise StepFailed("reconnect runtime executable does not match our launch")
    for flag, value in (("--pid", str(ctx.inst(index).pid)), ("--role", expected_role)):
        positions = [n for n, arg in enumerate(args) if arg == flag]
        if len(positions) != 1 or positions[0] + 1 >= len(args) or str(args[positions[0] + 1]) != value:
            raise StepFailed(f"reconnect runtime {index}: invalid owned {flag} binding")
    return matches[0]


def reconnect_log_bytes(path: Path) -> bytes:
    if not path.is_file():
        raise StepFailed(f"reconnect evidence log missing: {path}")
    return path.read_bytes()


def collect_reconnect_sample(ctx: Context, deadline: float) -> dict:
    """Read separate current observations; never claim an atomic snapshot."""
    def read(*args, **kwargs):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise StepFailed("reconnect observation deadline exceeded")
        return kh2ctl(*args, **kwargs, timeout=min(5.0, remaining))

    sample = {"peers": []}
    ctx._reconnect_partial_sample = sample
    fields = ("0x717008:u8", "0x717009:u8", "0x71700A:u8",
              "0x71700C:u16", "0x71700E:u16", "0x717010:u16")
    for index in range(3):
        ctx.check_all()
        proc = reconnect_runtime(ctx, index)
        inst = ctx.inst(index)
        row = {"slot": index, "gamePid": inst.pid, "runtimePid": proc.pid,
               "runtimeAlive": True, "runtimeArgv": list(proc.args), "problems": []}
        sample["peers"].append(row)
        try:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise StepFailed("reconnect observation deadline exceeded")
            observed = subprocess.run(
                [str(AVATARCTL), "observe", "--pid", str(inst.pid), "--samples", "2", "--interval-ms", "100"],
                capture_output=True, text=True, timeout=min(5.0, remaining))
            row["avatarObservation"] = json.loads(observed.stdout)
            row["avatarExitCode"] = observed.returncode
            row["avatarStderr"] = observed.stderr
            values = read("peek", "--rva", ",".join(fields), pid=inst.pid)["samples"][0]
            row["location"] = [values[field.split(":")[0]] for field in fields]
            row["nativePuppets"] = native_puppet_actors(
                ctx, index, _read=read,
                _entities=lambda peer: read("entities", pid=ctx.inst(peer).pid))
        except (StepFailed, subprocess.TimeoutExpired, ValueError, KeyError) as error:
            row["problems"].append(str(error))
        row["runtimeAlive"] = proc.poll() is None
        row["nativeLog"] = reconnect_log_bytes(inst.inject_log or LOGS / f"kh2coop_inject_{inst.pid}.log")
        row["runtimeLog"] = reconnect_log_bytes(ctx.run_dir / f"runtime_{index}.log")
    sample["relayLog"] = reconnect_log_bytes(ctx.run_dir / "relay.log")
    return sample


def save_reconnect_sample(ctx: Context, name: str, sample: dict, evidence: dict) -> None:
    """Retain exact byte inputs for independent replay of the pure validator."""
    metadata = {"schema": 1, "atomicAcrossSources": False, "peers": [], "evidence": evidence,
                "collectionIncomplete": sample.get("collectionIncomplete", False),
                "missingLogs": sample.get("missingLogs", [])}
    def store(log_name, data):
        path = ctx.run_dir / f"{name}_{log_name}.bin"
        path.write_bytes(data)
        if path.name not in ctx.artifacts:
            ctx.artifacts.append(path.name)
        return {"path": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    metadata["relayLog"] = store("relay", sample["relayLog"])
    for row in sample["peers"]:
        copied = {key: value for key, value in row.items() if key not in ("nativeLog", "runtimeLog")}
        for kind in ("nativeLog", "runtimeLog"):
            copied[kind] = store(f"peer{row['slot']}_{kind}", row[kind])
        metadata["peers"].append(copied)
    path = ctx.run_dir / f"{name}_sample.json"
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    if path.name not in ctx.artifacts:
        ctx.artifacts.append(path.name)


def reconnect_failure_sample(ctx: Context, sample: dict | None) -> dict:
    """Keep available exact inputs; missing bytes are labelled, never absence proof."""
    partial = getattr(ctx, "_reconnect_partial_sample", None)
    source = partial if partial is not None else sample or {}
    result = {**source, "peers": [dict(row) for row in source.get("peers", [])],
              "collectionIncomplete": True, "missingLogs": []}

    def recover_log(row, key, path, label):
        if isinstance(row.get(key), bytes):
            return
        try:
            row[key] = reconnect_log_bytes(path)
        except BaseException as error:
            row[key] = b""
            result["missingLogs"].append({"source": label, "path": str(path),
                                           "error": f"{type(error).__name__}: {error}",
                                           "emptyFallbackIsAbsenceProof": False})

    recover_log(result, "relayLog", ctx.run_dir / "relay.log", "relayLog")
    for index in range(3):
        rows = [row for row in result["peers"] if row.get("slot") == index]
        row = rows[0] if rows else {"slot": index, "problems": ["collection incomplete"]}
        if not rows:
            result["peers"].append(row)
        recover_log(row, "runtimeLog", ctx.run_dir / f"runtime_{index}.log", f"peer{index}.runtimeLog")
        try:
            inst = ctx.inst(index)
            path = inst.inject_log or LOGS / f"kh2coop_inject_{inst.pid}.log"
        except BaseException as error:
            path = ctx.run_dir / f"unavailable_native_log_{index}"
            row.setdefault("problems", []).append(f"native log path unavailable: {error}")
        recover_log(row, "nativeLog", path, f"peer{index}.nativeLog")
    return result


def retain_reconnect_failure(ctx: Context, name: str, sample: dict | None,
                             latest: dict, error: BaseException) -> None:
    """Best effort retention must never mask the original failure/cancellation."""
    failed = {**latest, "ready": False, "collectionIncomplete": True,
              "problems": [*latest.get("problems", []), f"{type(error).__name__}: {error}"]}
    ctx.saved[name] = failed
    try:
        partial = getattr(ctx, "_reconnect_partial_sample", None)
        if sample is not None and partial is not None and partial is not sample:
            save_reconnect_sample(ctx, name + "_last_complete", sample, failed)
        save_reconnect_sample(ctx, name, reconnect_failure_sample(ctx, sample), failed)
    except BaseException as persistence_error:
        failed["artifactPersistenceError"] = f"{type(persistence_error).__name__}: {persistence_error}"
        if hasattr(error, "add_note"):
            error.add_note("Reconnect artifact persistence failed: " + failed["artifactPersistenceError"])


def check_reconnect_bindings(ctx: Context, baseline: dict) -> None:
    ctx.check_all()
    originals = getattr(ctx, "_reconnect_owned", {}).get(baseline.get("baselineName"))
    if originals is None:
        raise StepFailed("reconnect original Popen bindings unavailable")
    for index in range(3):
        old = baseline["runnerBindings"][index]
        current = reconnect_runtime(ctx, index)
        if (current is not originals["runtimes"][index] or old["slot"] != index
                or old["gamePid"] != ctx.inst(index).pid or old["runtimePid"] != current.pid
                or old["runtimeArgv"] != list(current.args)):
            raise StepFailed("reconnect original game/runtime binding changed")
    relay = [proc for owner, proc, _ in ctx.processes if owner == "relay"]
    if (len(relay) != 1 or relay[0] is not originals["relay"]
            or relay[0].poll() is not None or relay[0].pid != baseline["runnerRelayPid"]):
        raise StepFailed("reconnect original owned relay binding changed or exited")


def step_reconnect_mark(ctx: Context, step: dict) -> dict:
    if step.get("instances", [0, 1, 2]) != [0, 1, 2]:
        raise StepFailed("reconnect supports the original ordered three-peer rig only")
    name = step.get("as", "reconnect_before")
    if name in ctx.saved:
        raise StepFailed("reconnect baseline name already exists")
    deadline = time.monotonic() + min(15.0, step.get("timeoutMs", 10000) / 1000)
    latest, sample = {}, None
    ctx._reconnect_partial_sample = None
    try:
        while True:
            if time.monotonic() >= deadline:
                raise StepFailed("reconnect baseline deadline exceeded")
            ctx._reconnect_partial_sample = None
            sample = collect_reconnect_sample(ctx, deadline)
            latest = _native_reconnect.capture_baseline(sample)
            if time.monotonic() >= deadline:
                raise StepFailed("reconnect baseline deadline exceeded")
            if latest.get("ready"):
                break
            ctx.sleep(min(0.25, max(0, deadline - time.monotonic())))
        latest["baselineName"] = name
        latest["runnerBindings"] = [{"slot": row["slot"], "gamePid": row["gamePid"],
                                     "runtimePid": row["runtimePid"], "runtimeArgv": row["runtimeArgv"]}
                                    for row in sample["peers"]]
        relay = [proc for owner, proc, _ in ctx.processes if owner == "relay"]
        if len(relay) != 1 or relay[0].poll() is not None:
            raise StepFailed("reconnect baseline lost its original owned relay")
        latest["runnerRelayPid"] = relay[0].pid
        if not hasattr(ctx, "_reconnect_owned"):
            ctx._reconnect_owned = {}
        ctx._reconnect_owned[name] = {"relay": relay[0],
                                      "runtimes": [reconnect_runtime(ctx, i) for i in range(3)]}
        check_reconnect_bindings(ctx, latest)
        if time.monotonic() >= deadline:
            raise StepFailed("reconnect baseline deadline exceeded")
        save_reconnect_sample(ctx, name, sample, latest)
        ctx.saved[name] = latest
        return latest
    except BaseException as error:
        retain_reconnect_failure(ctx, name, sample, latest, error)
        raise


def step_runtime_pause(ctx: Context, step: dict) -> dict:
    if step.get("instance") != 1:
        raise StepFailed("bounded reconnect fault may target only original Friend1 runtime")
    baseline = ctx.saved.get(step.get("after"), {})
    if not baseline.get("ready"):
        raise StepFailed("runtime pause requires a qualified saved reconnect baseline")
    name = step.get("as", "reconnect_pause")
    if name in ctx.saved:
        raise StepFailed("runtime pause receipt name already exists")
    friend = reconnect_runtime(ctx, 1)
    if any(isinstance(value, dict) and value.get("receiptKind") == "runtime_pause"
           and value.get("baselineName") == step["after"] for value in ctx.saved.values()):
        raise StepFailed("reconnect baseline already has a pause receipt")

    def check_alive():
        check_reconnect_bindings(ctx, baseline)

    def retired(_offset):
        proof = _native_reconnect.retirement_proof(
            baseline, reconnect_log_bytes(ctx.run_dir / "relay.log"),
            {index: reconnect_log_bytes(ctx.run_dir / f"runtime_{index}.log") for index in (0, 2)})
        return proof if proof and proof.get("retired") is True and proof.get("identityComplete") is True else None

    receipt = {}
    try:
        receipt = _runtime_lifecycle.pause_owned_friend_runtime(
            ctx.processes, ctx.instances, friend, ctx.inst(1).pid, 1, RUNTIME,
            retired, check_alive, step.get("timeoutMs", 20000) / 1000,
            relay_offset=baseline["relayLog"]["bytes"], expected_connection_id=baseline["roster"][1])
    except BaseException as error:
        receipt = getattr(error, "receipt", {"status": "failed", "completed": False, "error": str(error)})
        if isinstance(error, Exception):
            raise StepFailed(f"owned Friend1 pause failed: {error}") from error
        raise
    finally:
        receipt["baselineName"] = step["after"]
        receipt["receiptKind"] = "runtime_pause"
        ctx.saved[name] = receipt
        path = ctx.run_dir / f"{name}.json"
        path.write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        ctx.artifacts.append(path.name)
    return receipt


def step_reconnect_check(ctx: Context, step: dict) -> dict:
    baseline = ctx.saved.get(step.get("after"), {})
    if not baseline.get("ready"):
        raise StepFailed("reconnect check requires a qualified immutable baseline")
    name = step.get("as", "reconnect_after")
    if name in ctx.saved:
        raise StepFailed("reconnect checkpoint name already exists")
    deadline = time.monotonic() + min(60.0, step.get("timeoutMs", 45000) / 1000)
    latest, sample = {}, None
    ctx._reconnect_partial_sample = None
    try:
        receipts = [value for value in ctx.saved.values() if isinstance(value, dict)
                    and value.get("receiptKind") == "runtime_pause"
                    and value.get("baselineName") == step["after"]]
        if len(receipts) != 1 or not all(receipts[0].get(key) is True
                                        for key in ("completed", "resumed", "pinPreserved")):
            raise StepFailed("reconnect requires exactly one successful original-baseline pause receipt")
        pause_receipt = dict(receipts[0])
        latest["pauseReceipt"] = pause_receipt
        while True:
            if time.monotonic() >= deadline:
                raise StepFailed("native reconnect deadline exceeded")
            check_reconnect_bindings(ctx, baseline)
            ctx._reconnect_partial_sample = None
            sample = collect_reconnect_sample(ctx, deadline)
            check_reconnect_bindings(ctx, baseline)
            latest = {**_native_reconnect.validate_reconnect(baseline, sample),
                      "pauseReceipt": pause_receipt}
            check_reconnect_bindings(ctx, baseline)
            if time.monotonic() >= deadline:
                raise StepFailed("native reconnect deadline exceeded")
            if latest.get("ready"):
                break
            ctx.sleep(min(0.25, max(0, deadline - time.monotonic())))
        save_reconnect_sample(ctx, name, sample, latest)
        ctx.saved[name] = latest
        return latest
    except BaseException as error:
        retain_reconnect_failure(ctx, name, sample, latest, error)
        raise


def step_approach_goa_chest(ctx: Context, step: dict) -> dict:
    """Bounded camera-relative native walking to GoA flag 409 from its platform."""
    index = step.get("instance", 0)
    name = step.get("as", "goa_approach")
    helpers = ctx.namespace()
    evidence = {"target": [0, 460, 900], "pulses": []}
    deadline = time.monotonic() + 15
    window_start = None
    try:
        while True:
            if helpers["room"](index) != (4, 26):
                raise StepFailed("GoA approach requires current room 04/1A")
            position = helpers["pos"]("P_EX100", index)
            distance = math.hypot(position[0], 900 - position[2])
            evidence["finalPosition"] = position
            evidence["finalDistance"] = distance
            if abs(position[1] - 460) > 80:
                raise StepFailed("GoA approach must start on the chest platform (Y near 460)")
            if distance <= 100:
                break
            if time.monotonic() >= deadline:
                raise StepFailed("GoA native approach exceeded 15 seconds")
            keys = ("0x718C68", "0x718C70", "0x718C78", "0x718C80")
            sample = kh2ctl("peek", "--rva", ",".join(f"{key}:f32" for key in keys),
                            pid=ctx.inst(index).pid)["samples"][0]
            look_x, look_z, eye_x, eye_z = [sample[k] for k in keys]
            fx, fz = look_x - eye_x, look_z - eye_z
            length = math.hypot(fx, fz)
            if not math.isfinite(length) or length < 1:
                raise StepFailed("GoA camera has no usable horizontal forward basis")
            fx, fz = fx / length, fz / length
            dx, dz = -position[0] / distance, (900 - position[2]) / distance
            lx, ly = dx * fz - dz * fx, dx * fx + dz * fz
            kh2ctl("player-input", "--lx", f"{lx:.5f}", "--ly", f"{ly:.5f}",
                   "--duration-ms", "200", pid=ctx.inst(index).pid)
            end = helpers["pos"]("P_EX100", index)
            end_distance = math.hypot(end[0], 900 - end[2])
            evidence["pulses"].append({"start": position, "end": end, "distanceBefore": distance,
                "distanceAfter": end_distance, "camera": sample, "lx": lx, "ly": ly})
            if window_start is None:
                window_start = distance
            if len(evidence["pulses"]) % 3 == 0:
                if end_distance > 100 and end_distance >= window_start - 10:
                    raise StepFailed("GoA walking made no progress toward chest over three pulses")
                window_start = None
            ctx.check_all()
    except StepFailed:
        step_capture(ctx, {"instance": index, "name": f"{name}_failed"})
        raise
    finally:
        path = ctx.run_dir / f"{name}.json"
        path.write_text(json.dumps(evidence, indent=2))
        ctx.artifacts.append(path.name)
        ctx.saved[name] = evidence
    return evidence



def step_dismiss_goa_map_reward(ctx: Context, step: dict) -> dict:
    """Confirm GoA map obtained modal through native input; never bypass safe gate."""
    index = step.get("instance", 0)
    name = step.get("as", "goa_map_reward")
    evidence = {"samples": [], "presses": 0}
    step_capture(ctx, {"instance": index, "name": f"{name}_before"})
    start = time.monotonic()
    deadline = start + 10
    fields = (("world", 0x717008, "u8"), ("room", 0x717009, "u8"),
              ("chestByte", 0x9ABC8F, "u8"), ("controllable", 0x2A171E8, "i32"),
              ("inField", 0x9BA8D0, "u8"), ("openMenu", 0x7435D0, "u8"),
              ("cutsceneTimer", 0xB64F98, "i32"), ("uiTickDelta", 0x717484, "f32"),
              ("eventState", 0xB65210, "i32"), ("eventContext", 0x2A11478, "u64"))
    def sample():
        row = kh2ctl("peek", "--rva", ",".join(f"0x{rva:X}:{kind}" for _, rva, kind in fields),
                     pid=ctx.inst(index).pid)["samples"][0]
        data = {key: row[f"0x{rva:X}"] for key, rva, _ in fields}
        data["eventContext"] = int(data["eventContext"], 0) if isinstance(data["eventContext"], str) else data["eventContext"]
        data["elapsedMs"] = round((time.monotonic() - start) * 1000)
        data["safe"] = data["controllable"] == 0 and data["inField"] != 0 and data["openMenu"] == 255 and data["eventState"] == 0 and data["eventContext"] == 0
        evidence["samples"].append(data)
        if [data["world"], data["room"]] != [4, 26] or not data["chestByte"] & 2:
            raise StepFailed("GoA reward dismissal requires room 04/1A with flag409 already opened")
        return data["safe"]
    try:
        while not sample():
            if time.monotonic() >= deadline:
                raise StepFailed("GoA obtained-map modal did not reach native safe gameplay within 10 seconds")
            kh2ctl("player-press", "--button", "cross", "--duration-ms", "150", pid=ctx.inst(index).pid)
            evidence["presses"] += 1
            if sample():
                break
            ctx.sleep(min(0.7, max(0, deadline - time.monotonic())))
        evidence["safe"] = True
        step_capture(ctx, {"instance": index, "name": f"{name}_after"})
    except StepFailed:
        observation = {}
        for key, read in (
                ("actors", lambda: ctx.actors(index)),
                ("chests", lambda: chest_records(ctx, index)),
                ("location", lambda: ctx.namespace()["location"](index)),
                ("state", lambda: kh2ctl("state", pid=ctx.inst(index).pid)),
                ("liveFrame", lambda: ctx.inst(index).live_frame())):
            try:
                observation[key] = read()
            except Exception as error:
                observation[key + "Error"] = str(error)
        observation["nativeWarpGate"] = evidence["samples"][-1] if evidence["samples"] else None
        evidence["failureObservation"] = observation
        try:
            step_capture(ctx, {"instance": index, "name": f"{name}_failed"})
        except Exception as error:
            evidence["captureError"] = str(error)
        raise
    finally:
        path = ctx.run_dir / f"{name}.json"
        path.write_text(json.dumps(evidence, indent=2))
        ctx.artifacts.append(path.name)
        ctx.saved[name] = evidence
    return evidence


PERSONAL_RANGES = (("characters", 0x24F0, 0xE04), ("inventory", 0x3580, 0x140),
                   ("munny", 0x2440, 4), ("exp", 0x36E0, 4))
PROGRESS_RANGES = (("programs", 0x10, 0x1C80), ("story", 0x1C90, 0x260),
                   ("visited", 0x22F8, 0x98), ("chests", 0x23AC, 0x34))


def step_progress_snapshot(ctx: Context, step: dict, *, _read=None) -> dict:
    """Actual SAVE bytes, including all personal exclusions; room init can change them."""
    name = step.get("as", "progress")
    read = _read or kh2ctl
    evidence = {"instances": {}}
    for index in step.get("instances", [step.get("instance", 0)]):
        ranges = {}
        for label, offset, length in (*PROGRESS_RANGES, *PERSONAL_RANGES):
            fields = [(0x9A98B0 + offset + n, min(8, length - n)) for n in range(0, length, 8)]
            raw = bytearray()
            for start in range(0, len(fields), 128):
                batch = fields[start:start + 128]
                specs = [f"0x{rva:X}:u{size * 8}" for rva, size in batch]
                sample = read("peek", "--rva", ",".join(specs), pid=ctx.inst(index).pid)["samples"][0]
                for rva, size in batch:
                    value = sample[f"0x{rva:X}"]
                    raw.extend((int(value, 0) if isinstance(value, str) else value).to_bytes(size, "little"))
            ranges[label] = {"saveOffset": offset, "length": length, "hex": raw.hex(),
                             "sha256": hashlib.sha256(raw).hexdigest()}
        evidence["instances"][str(index)] = {"ranges": ranges}
    if "compare" in step:
        before = ctx.saved[step["compare"]]
        for key, data in evidence["instances"].items():
            data["diffs"] = {}
            for label, region in data["ranges"].items():
                old = bytes.fromhex(before["instances"][key]["ranges"][label]["hex"])
                new = bytes.fromhex(region["hex"])
                data["diffs"][label] = [{"saveOffset": region["saveOffset"] + n,
                                         "before": a, "after": b}
                                        for n, (a, b) in enumerate(zip(old, new)) if a != b]
    ctx.saved[name] = evidence
    path = ctx.run_dir / f"{name}.json"
    path.write_text(json.dumps(evidence, indent=2))
    ctx.artifacts.append(path.name)
    return {"path": path.name, "instances": list(evidence["instances"]),
            "changedBytes": {i: {k: len(v) for k, v in d.get("diffs", {}).items()}
                             for i, d in evidence["instances"].items()}}


HASH_PATTERN = re.compile(
    r"\[statehash\] role=(?P<role>host|client) epoch=(?P<epoch>\d+) frame=(?P<frame>\d+) "
    r"room=(?P<world>[0-9A-Fa-f]+)/(?P<room>[0-9A-Fa-f]+) door=(?P<door>\d+) "
    r"map=(?P<map>\d+) btl=(?P<btl>\d+) evt=(?P<evt>\d+) enemies=(?P<enemies>[0-9A-Fa-f]+) "
    r"progress=(?P<progress>[0-9A-Fa-f]+) count=(?P<count>\d+) unmatched=(?P<unmatched>\d+) observed=(?P<observed>\d+)")
NATIVE_HASH_PATTERN = re.compile(
    r"\[statehash\] native epoch=(?P<epoch>\d+) frame=(?P<frame>\d+) netId=(?P<netId>\d+) "
    r"objectId=(?P<objectId>\d+) hp=(?P<hp>-?\d+) actor=(?P<actor>[0-9A-Fa-f]+)")


def step_statehash_check(ctx: Context, step: dict) -> dict:
    """Require fresh completed-arrival hashes and their complete native population."""
    name = step.get("as", "statehash")
    indices = step.get("instances", list(range(len(ctx.instances))))
    expected = step.get("expectedFields", 0)
    paths = {i: LOGS / f"kh2coop_inject_{ctx.inst(i).pid}.log" for i in indices}
    offsets = {i: len(p.read_text(errors="replace")) if p.exists() else 0 for i, p in paths.items()}
    relay_path = ctx.run_dir / "relay.log"
    relay_offset = step.get("relayOffset", len(relay_path.read_text(errors="replace")) if relay_path.exists() else 0)
    latest = {}
    matching_frames = []
    matching_samples = []
    saw_extra = False

    def ready():
        nonlocal latest, saw_extra
        latest = {"ready": False, "instances": {}, "problems": [], "expectedFields": expected,
                  "sawUnmatchedExtra": saw_extra, "participatingInstances": indices,
                  "rigInstanceCount": len(ctx.instances)}
        problems = latest["problems"]
        for index, path in paths.items():
            text = path.read_text(errors="replace") if path.exists() else ""
            arrivals = list(re.finditer(ARRIVAL_PATTERN, text))
            matches = [m for m in HASH_PATTERN.finditer(text) if m.start() >= offsets[index]]
            if not arrivals or not matches or matches[-1].start() < arrivals[-1].end():
                problems.append(f"instance {index}: no fresh post-arrival hash")
                continue
            match = matches[-1]
            data = match.groupdict()
            for key in data.keys() - {"role"}:
                data[key] = int(data[key], 16 if key in ("world", "room", "enemies", "progress") else 10)
            data["location"] = [data[k] for k in ("world", "room", "door", "map", "btl", "evt")]
            data["currentLocation"] = ctx.namespace()["location"](index)
            arrival = arrivals[-1].groupdict()
            arrival_location = [int(arrival[k], 16 if k in ("world", "room") else 10)
                                for k in ("world", "room", "door", "map", "btl", "evt")]
            rows = []
            for row in NATIVE_HASH_PATTERN.finditer(text, match.end()):
                values = row.groupdict()
                if int(values["epoch"]) == data["epoch"] and int(values["frame"]) == data["frame"]:
                    rows.append({k: int(v, 16 if k == "actor" else 10) for k, v in values.items()})
            data["nativeRows"] = rows
            data["liveRows"] = sorted([[r[k] for k in ("netId", "objectId", "hp")] for r in rows if r["hp"] > 0])
            native_hash = 2166136261
            encoded = struct.pack("<II", 0x3145484B, len(data["liveRows"]))
            encoded += b"".join(struct.pack("<HIi", *row) for row in data["liveRows"])
            for byte in encoded:
                native_hash = ((native_hash ^ byte) * 16777619) & 0xFFFFFFFF
            data["recomputedEnemiesHash"] = native_hash
            latest["instances"][str(index)] = data
            if native_hash != data["enemies"]:
                problems.append(f"instance {index}: hash does not describe raw native rows")
            if data["epoch"] != int(arrival["epoch"]) or data["location"] != arrival_location or data["location"] != data["currentLocation"]:
                problems.append(f"instance {index}: hash is not current completed six-field location/epoch")
            if len(rows) != data["observed"] or len(data["liveRows"]) != data["count"]:
                problems.append(f"instance {index}: incomplete raw native records")
            if data["count"] < step.get("minEnemies", 1):
                problems.append(f"instance {index}: nonempty enemy population required")
            if not expected and (data["unmatched"] or any(r[0] == 0 for r in data["liveRows"])):
                problems.append(f"instance {index}: unmatched native enemies")
        if len(latest["instances"]) != len(indices):
            return False
        host = latest["instances"][str(indices[0])]
        latest["epoch"] = host["epoch"]
        if "epoch" in step and host["epoch"] != step["epoch"]:
            problems.append("hash checkpoint crossed the required epoch")
        if step.get("requireUnmatchedExtra", False):
            control = latest["instances"][str(step.get("controlInstance", 1))]
            extra = control["count"] > host["count"] and control["unmatched"] > 0 and any(r[0] == 0 for r in control["liveRows"])
            saw_extra = saw_extra or extra
            latest["sawUnmatchedExtra"] = saw_extra
            if not extra:
                problems.append("no extra positive unmatched native client actor")
        for index in indices[1:]:
            data = latest["instances"][str(index)]
            fields = (1 if data["location"] != host["location"] else 0) | (2 if data["enemies"] != host["enemies"] else 0) | (4 if data["progress"] != host["progress"] else 0)
            data["fields"] = fields
            want = expected if index == step.get("controlInstance", 1) else 0
            field_match = (fields & want) == want if want and step.get("allowOtherFields", False) else fields == want
            if data["epoch"] != host["epoch"] or data["location"] != host["location"] or not field_match:
                problems.append(f"instance {index}: epoch/hash mismatch fields={fields}, expected={want}")
            if not (want & 2) and data["liveRows"] != host["liveRows"]:
                problems.append(f"instance {index}: native netId/objectId/HP population differs")
        relay = relay_path.read_text(errors="replace")[relay_offset:] if relay_path.exists() else ""
        latest["relayDesync"] = [m.groupdict() for m in re.finditer(r"Desync: (?P<peer>\S+) fields=(?P<fields>\d+) epoch=(?P<epoch>\d+)", relay)]
        if expected and not any(((int(r["fields"]) & expected) == expected if step.get("allowOtherFields", False) else int(r["fields"]) == expected) and int(r["epoch"]) == host["epoch"] and r["peer"] == step.get("controlPeer", "peer1") for r in latest["relayDesync"]):
            problems.append("no fresh relay Desync with requested fields and epoch")
        if problems:
            matching_frames.clear()
            matching_samples.clear()
        else:
            frames = [latest["instances"][str(i)]["frame"] for i in indices]
            if not matching_frames or all(a != b for a, b in zip(frames, matching_frames[-1])):
                matching_frames.append(frames)
                matching_samples.append({"epoch": host["epoch"], "instances": {
                    peer: {key: data[key] for key in ("frame", "location", "enemies", "progress", "count", "unmatched", "nativeRows", "liveRows")}
                    for peer, data in latest["instances"].items()}})
            latest["matchingFrames"] = list(matching_frames)
            latest["matchingSamples"] = list(matching_samples)
            if len(matching_frames) < step.get("consecutiveSamples", 1):
                problems.append("waiting for another distinct matching frame on every peer")
        latest["ready"] = not problems
        return latest["ready"]

    try:
        wait_for(ctx, ready, f"statehash checkpoint {name}", step.get("timeoutMs", 20000) / 1000, 0.5)
    except StepFailed:
        path = ctx.run_dir / f"{name}_failed.json"
        path.write_text(json.dumps(latest, indent=2))
        ctx.artifacts.append(path.name)
        raise StepFailed(f"statehash checkpoint {name}: {latest.get('problems')}") from None
    ctx.saved[name] = latest
    path = ctx.run_dir / f"{name}.json"
    path.write_text(json.dumps(latest, indent=2))
    ctx.artifacts.append(path.name)
    return latest


def step_progress_hash_control(ctx: Context, step: dict) -> dict:
    """Reversible rig-only chest-bit fault; require actual readback and relay fields=4."""
    index = step.get("instance", 1)
    helpers = ctx.namespace()
    if helpers["room"](index) == (4, 26):
        raise StepFailed("progress hash control must run outside GoA")
    applies = helpers["log_matches"](r"\[progresssync\] apply version=(?P<version>\d+) [^\r\n]*personal_unchanged=1", index)
    if not applies:
        raise StepFailed("progress hash control requires a successfully applied progress version")
    original = helpers["peek"](0x9ABC8F, "u8", index)
    relay = ctx.run_dir / "relay.log"
    baseline = len(relay.read_text(errors="replace")) if relay.exists() else 0
    def write(value):
        kh2ctl("poke", "--rva", "0x9ABC8F", "--type", "u8", "--value", str(value), pid=ctx.inst(index).pid)
        if helpers["peek"](0x9ABC8F, "u8", index) != value:
            raise StepFailed("progress control SAVE byte readback failed")
    name = step.get("as", "statehash")
    control = {"rva": "0x9ABC8F", "before": original, "during": original ^ 2,
               "instance": index, "appliedVersion": int(applies[-1]["version"])}
    try:
        write(original ^ 2)
        control["mutationVerified"] = True
        result = step_statehash_check(ctx, {**step, "expectedFields": 4, "controlInstance": index,
                                           "relayOffset": baseline})
    finally:
        try:
            write(original)
            control["restored"] = original
            control["restorationVerified"] = True
        except Exception as error:
            control["restorationError"] = str(error)
            raise
        finally:
            path = ctx.run_dir / f"{name}_control.json"
            path.write_text(json.dumps(control, indent=2))
            ctx.artifacts.append(path.name)
    result["control"] = control
    (ctx.run_dir / f"{name}.json").write_text(json.dumps(result, indent=2))
    return result



def step_enemy_hash_control(ctx: Context, step: dict) -> dict:
    """Hold one client's native HP-lock bit while host native death is mirrored."""
    index = step.get("instance", 1)
    name = step.get("as", "enemy_negative")
    baseline = step_statehash_check(ctx, {"as": f"{name}_baseline", "minEnemies": 4})
    client_rows = baseline["instances"][str(index)]["nativeRows"]
    candidates = [r for r in client_rows if r["netId"] > 0 and r["hp"] > 0
                  and ("objectId" not in step or r["objectId"] == step["objectId"])]
    if not candidates:
        raise StepFailed(f"enemy negative control has no matched live objectId={step.get('objectId', 'any')}")
    row = candidates[0]
    host_row = next(r for r in baseline["instances"]["0"]["nativeRows"]
                    if r["netId"] == row["netId"] and r["objectId"] == row["objectId"] and r["hp"] > 0)
    def current_actor(peer=index, expected=row):
        return next((a for a in ctx.actors(peer) if int(a["address"], 16) == expected["actor"]), None)
    target_location = baseline["instances"]["0"]["location"]
    def active_gate(peer, expected, identity=None):
        helpers = ctx.namespace()
        location = helpers["location"](peer)
        arrivals = helpers["log_matches"](ARRIVAL_PATTERN, peer)
        actor = current_actor(peer, expected)
        if not arrivals or int(arrivals[-1]["epoch"]) != baseline["epoch"] or location != target_location:
            raise StepFailed(f"enemy control instance {peer}: completed epoch/full location changed")
        arrival_location = [int(arrivals[-1][k], 16 if k in ("world", "room") else 10)
                            for k in ("world", "room", "door", "map", "btl", "evt")]
        if arrival_location != location:
            raise StepFailed(f"enemy control instance {peer}: current location differs from arrival")
        if (not actor or actor["objectId"] != expected["objectId"] or actor.get("hp", 0) <= 0
                or int(actor.get("status", "0"), 16) == 0
                or (identity and any(actor.get(k) != v for k, v in identity.items()))):
            raise StepFailed(f"enemy control instance {peer}: live native actor identity/status changed")
        return {"instance": peer, "epoch": int(arrivals[-1]["epoch"]), "location": location, "actor": actor}
    initial_client = active_gate(index, row)
    initial_host = active_gate(0, host_row)
    identity = {k: initial_client["actor"][k] for k in ("address", "objectId", "status")}
    host_identity = {k: initial_host["actor"][k] for k in ("address", "objectId", "status")}
    pid = ctx.inst(index).pid
    header = kh2ctl("peek", "--rva", "0x717008:u8", pid=pid)
    base = int(str(header["moduleBase"]), 0)
    key = f"0x{(row['actor'] + 0x9B8 - base) & ((1 << 64) - 1):X}"
    def flags():
        return kh2ctl("peek", "--rva", f"{key}:u32", pid=pid)["samples"][0][key]
    def write(value):
        kh2ctl("poke", "--addr", f"0x{row['actor'] + 0x9B8:X}", "--type", "u32", "--value", str(value), pid=pid)
        if flags() != value:
            raise StepFailed("enemy HP-lock readback failed")
    original = flags()
    if original & 4:
        raise StepFailed("selected client enemy already has HP lock; cannot establish a new fault")
    evidence = {"instance": index, "identity": identity, "hostIdentity": host_identity, "netId": row["netId"],
                "hostActor": host_row["actor"], "epoch": baseline["epoch"], "flagsBefore": original,
                "initialGates": [initial_client, initial_host], "mutationGates": [],
                "selectionPolicy": {"requestedObjectId": step.get("objectId"),
                                    "selectedObjectId": row["objectId"], "selectedNetId": row["netId"],
                                    "rule": "first matched positive-HP native row satisfying explicit objectId filter"}}
    relay = ctx.run_dir / "relay.log"
    relay_offset = len(relay.read_text(errors="replace")) if relay.exists() else 0
    log = LOGS / f"kh2coop_inject_{pid}.log"
    log_offset = len(log.read_text(errors="replace"))
    try:
        evidence["mutationGates"].append({"operation": "client HP lock", "peers": [
            active_gate(index, row, identity), active_gate(0, host_row, host_identity)]})
        # Re-read at mutation time so unrelated native flag changes survive.
        current_flags = flags()
        original = current_flags
        if original & 4:
            raise StepFailed("client HP lock became set before control mutation")
        evidence["flagsBefore"] = original
        evidence["mutationAttempted"] = True
        write(current_flags | 4)
        evidence["lockVerified"] = True
        evidence["mutationGates"].append({"operation": "host native kill", "peers": [
            active_gate(index, row, identity), active_gate(0, host_row, host_identity)]})
        kh2ctl("hit", "kill", "--victim", f"0x{host_row['actor']:X}", pid=ctx.inst(0).pid)
        pattern = re.compile(r"host death netId " + str(row["netId"]) + r" FAILED \(hp (\d+) -> (\d+)\)")
        def failed_death():
            matches = list(pattern.finditer(log.read_text(errors="replace")[log_offset:]))
            current = current_actor()
            if matches and current and all(current.get(k) == v for k, v in identity.items()) and current.get("hp", 0) > 0:
                evidence["failedNativeDeath"] = matches[-1].group(0)
                evidence["survivingNativeActor"] = current
                return True
            return False
        wait_for(ctx, failed_death, "failed client native death with positive HP", 10, 0.2)
        result = step_statehash_check(ctx, {"as": name, "expectedFields": 2, "allowOtherFields": True,
            "controlInstance": index, "relayOffset": relay_offset, "minEnemies": 1, "consecutiveSamples": 2})
        if result["epoch"] != baseline["epoch"]:
            raise StepFailed("enemy negative control crossed an epoch")
        surviving = [r for r in result["instances"][str(index)]["nativeRows"]
                     if r["netId"] == row["netId"] and r["actor"] == row["actor"] and r["hp"] > 0]
        host_live = [r for r in result["instances"]["0"]["nativeRows"] if r["netId"] == row["netId"] and r["hp"] > 0]
        if not surviving or host_live:
            raise StepFailed("negative hashes do not contain the surviving client-only enemy")
    finally:
        try:
            try:
                restore_gate = active_gate(index, row, identity)
            except StepFailed as error:
                restore_gate = None
                evidence["restorationSkipped"] = str(error)
            if restore_gate and evidence.get("mutationAttempted"):
                evidence["restorationGate"] = restore_gate
                current_flags = flags()
                restored = (current_flags & ~4) | (original & 4)
                write(restored)
                evidence["flagsAtRestore"] = current_flags
                evidence["flagsRestored"] = restored
                evidence["restorationVerified"] = True
            else:
                evidence.setdefault("restorationSkipped", "no verified lock mutation; no restoration write")
        finally:
            path = ctx.run_dir / f"{name}_control.json"
            path.write_text(json.dumps(evidence, indent=2))
            ctx.artifacts.append(path.name)
    result["control"] = evidence
    return result



def step_courtyard_diagnostic(ctx: Context, step: dict) -> dict:
    """Replay the known courtyard trigger; absence of an extra actor is inconclusive."""
    index = step.get("instance", 1)
    name = step.get("as", "courtyard_diagnostic")
    baseline = ctx.saved[step.get("baseline", "hash_after_damage")]
    helpers = ctx.namespace()
    if helpers["location"](index) != baseline["instances"][str(index)]["location"] or helpers["room"](index) != (5, 6):
        raise StepFailed("courtyard diagnostic requires unchanged BC courtyard baseline")
    def observe():
        samples = {}
        for peer in (0, 1, 2):
            log = LOGS / f"kh2coop_inject_{ctx.inst(peer).pid}.log"
            text = log.read_text(errors="replace") if log.exists() else ""
            keys = [f"0x{0x718C60 + n:X}" for n in (8, 12, 16, 24, 28, 32)]
            samples[str(peer)] = {"location": helpers["location"](peer), "player": helpers["pos"]("P_EX100", peer),
                "camera": kh2ctl("peek", "--rva", ",".join(f"{k}:f32" for k in keys), pid=ctx.inst(peer).pid)["samples"][0],
                "actors": ctx.actors(peer), "spawnBindings": [line for line in text.splitlines()
                    if "[enemysync]" in line and any(word in line for word in ("spawn", "matched", "bind", "manifest"))][-100:]}
        return samples
    evidence = {"baseline": baseline, "before": observe()}
    relay = ctx.run_dir / "relay.log"
    relay_offset = len(relay.read_text(errors="replace")) if relay.exists() else 0
    evidence["relayOffsetBeforeReplay"] = relay_offset
    try:
        step_input(ctx, {"instance": index, "ly": 1, "ms": 600})
        step_press(ctx, {"instance": index, "button": "cross", "times": 15, "gapMs": 300})
        evidence["afterReplay"] = observe()
        try:
            result = step_statehash_check(ctx, {"as": f"{name}_hash", "expectedFields": 2, "allowOtherFields": True,
                "controlInstance": index, "relayOffset": relay_offset, "minEnemies": 1, "consecutiveSamples": 2,
                "requireUnmatchedExtra": True, "epoch": baseline["epoch"], "timeoutMs": step.get("timeoutMs", 12000)})
        except StepFailed:
            failed = ctx.run_dir / f"{name}_hash_failed.json"
            observation = json.loads(failed.read_text()) if failed.exists() else {}
            evidence["hashObservation"] = observation
            if any(data.get("epoch") != baseline["epoch"] or data.get("location") != baseline["instances"][peer]["location"]
                   for peer, data in observation.get("instances", {}).items()):
                evidence["outcome"] = "invalidated by epoch/location change"
                raise StepFailed("courtyard diagnostic invalidated by a completed epoch/location change") from None
            if not observation.get("sawUnmatchedExtra", False):
                evidence["outcome"] = "inconclusive"
                raise StepFailed("INCONCLUSIVE: courtyard replay did not produce an extra unmatched native actor") from None
            evidence["outcome"] = "extra actor without required desync evidence"
            raise
        evidence["hashObservation"] = result
        evidence["outcome"] = "extra native actor detected by applied hash and relay"
        ctx.saved[name] = evidence
        return evidence
    finally:
        try:
            evidence["after"] = observe()
        except Exception as error:
            evidence["afterError"] = str(error)
        try:
            step_capture(ctx, {"instance": index, "name": name})
        except Exception as error:
            evidence["captureError"] = str(error)
        path = ctx.run_dir / f"{name}.json"
        path.write_text(json.dumps(evidence, indent=2))
        ctx.artifacts.append(path.name)



def evaluate_native_region(region: dict, point: list[float]) -> dict:
    """Offline float32 approximation; edge outcomes explicitly remain uncertain."""
    result = {"kind": region["kind"], "nativePredicateCalled": False,
              "precision": "float32 operations; near-boundary results uncertain (native operation ordering not instrumented)"}
    if region["kind"] == "INFINITY":
        return dict(result, accepted=True, edgeUncertain=False)
    matrix, extents = region["inverseMatrix"], region["extents"]
    if len(point) != 4 or not all(math.isfinite(v) for v in point + matrix + extents):
        return dict(result, accepted=None, edgeUncertain=True, reason="nonfinite/incomplete geometry or position")
    def f32(value):
        return struct.unpack("<f", struct.pack("<f", value))[0]
    try:
        transformed = [f32(f32(f32(f32(matrix[j] * point[0]) + f32(matrix[j + 4] * point[1]))
                                + f32(matrix[j + 8] * point[2])) + matrix[j + 12]) for j in range(3)]
        result["transformed"] = transformed
        scale = max(1.0, *(abs(v) for v in transformed + extents),
                    *(sum(abs(matrix[j + k * 4] * point[k]) for k in range(3)) + abs(matrix[j + 12]) for j in range(3)))
        tolerance = 32 * 2 ** -23 * scale
        if region["kind"] == "BOX":
            margins = [extents[i] - abs(transformed[i]) for i in range(3)]
            result.update(accepted=all(v >= 0 for v in margins), margins=margins,
                          edgeUncertain=any(abs(v) <= tolerance for v in margins))
        elif region["kind"] == "CYLINDER":
            if extents[0] == 0 or extents[2] == 0:
                return dict(result, accepted=None, edgeUncertain=True, reason="zero cylinder radius: native IEEE comparison not emulated")
            x, z = f32(transformed[0] / extents[0]), f32(transformed[2] / extents[2])
            radial = f32(f32(x * x) + f32(z * z))
            ymargin = extents[1] - abs(transformed[1])
            radial_tolerance = 32 * 2 ** -23 * max(1.0, abs(radial)) + tolerance * (1 / abs(extents[0]) + 1 / abs(extents[2]))
            result.update(accepted=ymargin >= 0 and radial <= 1, radialSquared=radial, yMargin=ymargin,
                          edgeUncertain=abs(ymargin) <= tolerance or abs(1 - radial) <= radial_tolerance)
        else:
            return dict(result, accepted=None, edgeUncertain=True, reason="unknown region kind")
    except (OverflowError, ZeroDivisionError):
        result.update(accepted=None, edgeUncertain=True, reason="float32 overflow/division unavailable")
    return result


def capture_native_regions(entry: dict, base: int, read, region_table: list[int]) -> dict:
    """Read the actual type2 linked regions; descriptor count is a bound, not list membership."""
    geometry = {"complete": False, "regions": [], "descriptors": [], "limits": [], "diagnosticRegionCap": 64}
    try:
        count = entry["headerFields"]["regionCount"]
        if not 0 <= count <= 64:
            raise StepFailed("region descriptor count exceeds diagnostic cap64; no native maximum claimed")
        start = entry["regionArray"]
        descriptor_fields = {f"{n}:{j}": (start + n * 64 + j * 8, "u64") for n in range(count) for j in range(8)}
        descriptor_values = read(descriptor_fields)
        for n in range(count):
            raw = b"".join(descriptor_values[f"{n}:{j}"].to_bytes(8, "little") for j in range(8))
            geometry["descriptors"].append({"index": n, "address": start + n * 64, "hex": raw.hex(),
                "kind": struct.unpack_from("<H", raw)[0], "category": struct.unpack_from("<H", raw, 2)[0],
                "position": list(struct.unpack_from("<fff", raw, 4)), "extents": list(struct.unpack_from("<fff", raw, 0x10)),
                "rotationY": struct.unpack_from("<f", raw, 0x20)[0]})
        address, seen, verify_fields, captured = entry["regionHead"], set(), {}, {}
        known = {base + 0x5D4F98: (0, "BOX"), base + 0x5D4FB0: (1, "CYLINDER"), base + 0x5D4FC8: (2, "INFINITY")}
        while address:
            if address in seen or len(seen) >= count:
                raise StepFailed("region list repeats/cycles or exceeds rooted descriptor count")
            seen.add(address)
            fields = {f"{address}:{j}": (address + j * 8, "u64") for j in range(14)}
            words = read(fields)
            raw = b"".join(words[f"{address}:{j}"].to_bytes(8, "little") for j in range(14))
            vtable, descriptor = struct.unpack_from("<Q", raw)[0], struct.unpack_from("<Q", raw, 0x68)[0]
            handle = struct.unpack_from("<I", raw, 0x58)[0]
            node = {"address": address, "hex": raw.hex(), "vtable": vtable, "descriptor": descriptor,
                    "nextHandle": handle, "inverseMatrix": list(struct.unpack_from("<16f", raw, 8)),
                    "extents": list(struct.unpack_from("<fff", raw, 0x48))}
            geometry["regions"].append(node)
            if vtable not in known:
                raise StepFailed("runtime region has unknown vtable; predicate unavailable")
            if not start <= descriptor < start + count * 64 or (descriptor - start) % 64:
                raise StepFailed("runtime region descriptor is outside/alignment-mismatched rooted descriptor span")
            node["descriptorIndex"] = n = (descriptor - start) // 64
            kind, node["kind"] = known[vtable]
            if geometry["descriptors"][n]["kind"] != kind:
                raise StepFailed("runtime vtable kind differs from rooted source descriptor")
            if handle:
                masked = handle & 0x7FFFFFFF
                region = region_table[masked >> 25]
                nxt = region | (masked & 0x1FFFFFF)
                if region == 0xFFFFFFFFFFFFFFFF or region & 0x1FFFFFF or not 0x10000 <= nxt < 0x800000000000:
                    raise StepFailed("region next handle has unavailable region-table resolution")
            else:
                nxt = 0
            node["nextAddress"] = nxt
            verify_fields.update(fields); captured.update(words)
            address = nxt
        if (geometry["regions"][-1]["address"] if geometry["regions"] else 0) != entry["regionTail"]:
            raise StepFailed("runtime region terminal node does not match checked tail")
        geometry["bytesStable"] = read(verify_fields) == captured and read(descriptor_fields) == descriptor_values
        if not geometry["bytesStable"]:
            raise StepFailed("runtime region matrices/extents/links/descriptors changed while reading")
        geometry["complete"] = True
    except Exception as error:
        geometry["limits"].append(f"{type(error).__name__}: {error}")
    return geometry


def capture_raw_native_occupancy(base: int, read, cap: int = 256, *, lifecycle_read=None) -> dict:
    """Checked active/deferred sample only: no atomicity or pre-link exclusion claim."""
    receipt = {"schemaVersion": 2, "listedOccupancyComplete": False,
               "pendingExclusionComplete": False, "atomic": False,
               "controllerIncarnationQualified": False, "globalControllerIdCoverageComplete": False,
               "diagnosticNodeCap": cap, "lists": {}, "reasons": [],
               "readback": {"fields": {}, "before": {"values": {}, "complete": False},
                            "after": {"values": {}, "attempted": False, "complete": False}, "changes": []}}
    evidence = receipt["readback"]
    fields, captured = {}, evidence["before"]["values"]
    nodes = []
    lifecycle = receipt["lifecycle"] = {"before": {}, "after": {}, "available": False,
        "stable": False, "status": "unavailable", "changes": [], "reasons": [],
        "scope": "checked raw-interval bookends only; no atomicity or incarnation claim"}

    def sample_lifecycle(phase):
        sample = lifecycle[phase]
        sample.update(native={}, nativeComplete=False, logs={}, scope=None, available=False)
        try:
            if lifecycle_read is None:
                raise StepFailed("raw lifecycle reader unavailable")
            lifecycle_read(sample, "raw-occupancy-lifecycle-" + phase, lifecycle)
            sample["available"] = bool(sample["nativeComplete"] and sample["scope"] and sample["logs"].get("arrival")
                                       and sample["logs"].get("lifecycle"))
            if not sample["available"]:
                lifecycle["reasons"].append({"phase": phase, "code": "lifecycle-scope-unavailable"})
        except Exception as error:
            lifecycle["reasons"].append({"phase": phase, "code": "lifecycle-read-failed",
                                         "error": f"{type(error).__name__}: {error}"})

    def problem(code, **detail):
        receipt["reasons"].append({"code": code, **detail})

    def pointer(value):
        return type(value) is int and base <= value < 0x800000000000

    def validate_field_types(stage):
        # CmdPeek JSON keys contain only the RVA. Mixed types at one address
        # would decode the last value into every label, including on recheck.
        aliases = {}
        for label, (address, kind) in fields.items():
            aliases.setdefault(address, []).append({"label": label, "type": kind})
        conflicts = [(address, labels) for address, labels in aliases.items()
                     if len({field["type"] for field in labels}) > 1]
        for address, labels in conflicts:
            problem("typed-address-conflict", stage=stage, address=address, fields=labels)
        if conflicts:
            raise StepFailed("raw occupancy mixed-type address aliases cannot be read unambiguously")

    def collect(batch, stage):
        fields.update(batch)
        validate_field_types(stage)
        read(batch, captured, stage="raw-occupancy-" + stage, failure_evidence=receipt)

    roots = {name: (base + offset, "u64") for name, offset in (
        ("activeHead", 0x2A171C8), ("activeTail", 0x2A171D0),
        ("deferredHead", 0x2A171D8), ("deferredTail", 0x2A171E0))}
    roots.update({f"bucket{n}": (base + 0x2B0D720 + n * 8, "u64") for n in range(64)})
    sample_lifecycle("before")
    try:
        if type(base) is not int or not 0x10000 <= base < 0x800000000000:
            raise StepFailed("raw occupancy module base unavailable")
        if type(cap) is not int or not 1 <= cap <= 256:
            raise StepFailed("raw occupancy diagnostic node cap must be in1..256")
        collect(roots, "roots-before")
        memberships = {}
        metadata = {}
        for name in ("active", "deferred"):
            head, tail = captured[name + "Head"], captured[name + "Tail"]
            lane = {"head": head, "tail": tail, "nodes": [], "terminalHandleZero": False}
            receipt["lists"][name] = lane
            if bool(head) != bool(tail):
                problem("root-tail-null-mismatch", list=name)
            address = head
            seen = set()
            while address:
                if not pointer(address) or address % 8:
                    problem("invalid-node-pointer", list=name, address=address)
                    break
                if address in memberships:
                    problem("cycle-or-repeated-node" if address in seen else "cross-list-membership",
                            list=name, address=address, firstList=memberships[address])
                    break
                if len(nodes) >= cap:
                    problem("node-cap-exhausted", list=name, address=address)
                    break
                seen.add(address)
                memberships[address] = name
                node = {"address": address, "readSuccess": {}}
                lane["nodes"].append(node)
                nodes.append(node)
                prefix = f"node{len(nodes) - 1}:"
                node["fieldPrefix"] = prefix
                batch = {prefix + key: (address + offset, kind) for key, offset, kind in (
                    ("nextHandle", 0xA90, "u32"), ("flags120", 0x120, "u32"),
                    ("objectEntry", 0x918, "u64"), ("status", 0x5C0, "u64"),
                    ("controller", 0x9E8, "u64"), ("spawnRecord", 0x9F0, "u64"))}
                collect(batch, "node-before")
                for key in ("status", "controller", "spawnRecord", "objectEntry"):
                    value = captured[prefix + key]
                    if value and not pointer(value):
                        problem("invalid-field-pointer", list=name, address=address, field=key, value=value)
                descriptor = captured[prefix + "objectEntry"]
                if pointer(descriptor):
                    metadata.update({prefix + key: (descriptor + offset, kind) for key, offset, kind in (
                        ("objectId", 0, "u32"), ("objectType", 4, "u8"), ("namePrefix", 8, "u16"))})
                else:
                    problem("object-descriptor-unavailable", list=name, address=address)
                record = captured[prefix + "spawnRecord"]
                node["recordIdApplicable"] = record != 0
                if pointer(record):
                    metadata[prefix + "recordId"] = (record + 0x1E, "u16")
                handle = captured[prefix + "nextHandle"]
                if handle == 0:
                    node["nextAddress"] = 0
                    lane["terminalHandleZero"] = True
                    address = 0
                else:
                    masked = handle & 0x7FFFFFFF
                    bucket = masked >> 25
                    region = captured[f"bucket{bucket}"]
                    nxt = region | (masked & 0x1FFFFFF)
                    node.update(handleBucket=bucket, handleBucketBase=region)
                    if (region in (0, 0xFFFFFFFFFFFFFFFF) or region & 0x1FFFFFF
                            or not pointer(nxt) or nxt % 8):
                        problem("invalid-handle-bucket", list=name, address=address,
                                handle=handle, bucket=bucket, bucketBase=region)
                        break
                    node["nextAddress"] = address = nxt
            if not head:
                lane["terminalHandleZero"] = tail == 0
            if (lane["nodes"][-1]["address"] if lane["nodes"] else 0) != tail:
                problem("terminal-tail-mismatch", list=name, tail=tail)
        collect(metadata, "metadata-before")
        evidence["before"]["complete"] = True
        validate_field_types("after")
        evidence["after"]["attempted"] = True
        read(fields, evidence["after"]["values"], stage="raw-occupancy-after", failure_evidence=receipt)
        evidence["after"]["complete"] = True
    except Exception as error:
        problem("read-or-capture-failed", error=f"{type(error).__name__}: {error}")
    sample_lifecycle("after")
    lifecycle["available"] = all(lifecycle[phase]["available"] for phase in ("before", "after"))
    if lifecycle["available"]:
        for source, keys in (("native", ("head", "tail", "location", "regions", "inField", "frozen",
                                         "eventState", "eventContext", "openMenu")),
                             ("logs", ("arrival", "lifecycle"))):
            for key in keys:
                before, after = (lifecycle[phase][source][key] for phase in ("before", "after"))
                if before != after:
                    lifecycle["changes"].append({"source": source, "field": key, "before": before, "after": after})
        for phase in ("before", "after"):
            sample = lifecycle[phase]
            native, observed = sample["native"], sample["logs"]
            arrival_location = [int(observed["arrival"][k], 16 if k in ("world", "room") else 10)
                                for k in ("world", "room", "door", "map", "btl", "evt")]
            if (not observed.get("arrivalAfterLifecycle") or arrival_location != native["location"]
                    or sample["scope"]["location"] != native["location"]
                    or native["inField"] == 0 or native["frozen"] != 0 or native["eventState"] != 0
                    or native["eventContext"] != 0 or native["openMenu"] != 255):
                lifecycle["reasons"].append({"phase": phase, "code": "lifecycle-scope-unqualified"})
        lifecycle["stable"] = not lifecycle["changes"] and not lifecycle["reasons"]
        lifecycle["status"] = "stable" if lifecycle["stable"] else "changed"
    elif any(lifecycle[phase]["available"] for phase in ("before", "after")):
        lifecycle["status"] = "partial"
    # Header bookends and the intervening raw reads are separate observations.
    # Join their overlapping roots/buckets explicitly; equality is still only
    # sampled agreement, and the header makes no deferred-list observation.
    for phase in ("before", "after"):
        native = lifecycle[phase]["native"]
        raw = evidence[phase]["values"]
        join = lifecycle[phase]["rawReadbackJoin"] = {"complete": False, "missing": [], "mismatches": []}
        pairs = [("head", "activeHead"), ("tail", "activeTail")]
        pairs += [(f"region{n}", f"bucket{n}") for n in range(64)]
        for native_key, raw_key in pairs:
            if native_key.startswith("region") and "regions" in native:
                index = int(native_key[6:])
                native_present = index < len(native["regions"])
                native_value = native["regions"][index] if native_present else None
            else:
                native_present = native_key in native
                native_value = native.get(native_key)
            if not native_present or raw_key not in raw:
                join["missing"].append({"nativeField": native_key, "rawField": raw_key,
                    "nativeAvailable": native_present, "rawAvailable": raw_key in raw})
            elif native_value != raw[raw_key]:
                join["mismatches"].append({"nativeField": native_key, "rawField": raw_key,
                    "nativeValue": native_value, "rawValue": raw[raw_key]})
        join["complete"] = not join["missing"] and not join["mismatches"]
        for key, code in (("missing", "lifecycle-raw-readback-missing"),
                          ("mismatches", "lifecycle-raw-readback-mismatch")):
            if join[key]:
                lifecycle["reasons"].append({"phase": phase, "code": code, "fields": join[key]})
        if not join["complete"]:
            lifecycle["stable"] = False
            if any(lifecycle[p]["available"] for p in ("before", "after")):
                lifecycle["status"] = "partial"
    if not lifecycle["stable"]:
        problem("lifecycle-not-stable", status=lifecycle["status"])
    evidence["fields"] = {key: {"address": address, "type": kind} for key, (address, kind) in fields.items()}
    for phase in ("before", "after"):
        sample = evidence[phase]
        sample["unreadFields"] = [key for key in fields if key not in sample["values"]]
    for key, value in evidence["after"]["values"].items():
        if key in captured and value != captured[key]:
            change = {"field": key, "address": fields[key][0], "before": captured[key], "after": value}
            evidence["changes"].append(change)
            problem("read-changed", **change)
    for node in nodes:
        prefix = node.pop("fieldPrefix")
        for key in ("nextHandle", "flags120", "objectEntry", "status", "controller", "spawnRecord",
                    "objectId", "objectType", "namePrefix", "recordId"):
            node["readSuccess"][key] = prefix + key in captured
            if node["readSuccess"][key]:
                node[key] = captured[prefix + key]
    receipt["listedOccupancyComplete"] = bool(evidence["before"]["complete"]
        and evidence["after"]["complete"] and not receipt["reasons"])
    return receipt


def capture_native_secondary_bindings(base, read, census, *, lifecycle_read):
    """Bounded current slot0/1 data bindings, never execution or creator authority."""
    out = {"schemaVersion": 1, "pid": census.get("pid"), "moduleBase": base,
        "parentModuleBase": census.get("moduleBase"), "lineage": census.get("lineage"),
        "startedMonotonic": time.monotonic(), "deadlineOwner": "supplied census read/lifecycle readers",
        "diagnosticCaps": {"objectRows": 8192, "selectedEntries": 128, "ownerSlots": 256,
                           "barEntries": 256, "itemRows": 4096},
        "reasons": [], "observations": [], "tables": [], "selectedEntries": [], "bindings": [],
        "bar": {}, "items": {}, "lifecycle": {}, "readbackChanges": [],
        "inventoryComplete": False, "declaredExtentBindingComplete": False,
        "currentSelectionComplete": False, "sampledBindingsStable": False,
        "localBindingsStable": False, "sampledScope": "unavailable",
        "consumedBucketJoin": {"entries": [], "complete": False, "knownDisagreement": False},
        "effectiveObjectLookupQualified": False}
    for key in ("executedMapperCallObserved", "currentActorOwnerProven", "futureEquipmentDomainComplete",
                "replacementArgumentDomainComplete", "continuousTableIdentity", "pendingExclusionComplete",
                "creatorExclusive", "lifetimeProven", "atomic", "mayCreate", "creationAuthority"):
        out[key] = False
    widths = {"u64": 8, "u32": 4}
    declarations, stage_values, stage_bytes, scheduled = {}, {}, {}, {}
    blocked, conflicts = set(), []
    out["conflicts"] = conflicts

    def reason(code, **details):
        out["reasons"].append({"code": code, **details})

    def span(a, n):
        return (type(base) is int and 0x10000 <= base < 0x800000000000 and type(a) is int
                and type(n) is int and n > 0 and base <= a and a + n <= 0x800000000000)

    def fields(a, n):
        return [(a + i, "u64") for i in range(0, n, 8)]

    def collect(requests, stage="initial"):
        # A stage owns its first bytes. Extension never overwrites a previous
        # word; W's native reload and final readback are distinct observations.
        requests = list(dict.fromkeys(requests))
        values = stage_values.setdefault(stage, {})
        observed_bytes = stage_bytes.setdefault(stage, {})
        requested_once = scheduled.setdefault(stage, set())
        aliases = []
        pending = {}
        for a, kind in requests:
            key = f"{a:X}:{kind}"
            aliases.append(key)
            if not span(a, widths[kind]):
                blocked.add(key); reason("unsupported-span", address=a, type=kind, stage=stage)
                continue
            old = declarations.get(a)
            if old is not None and old != kind:
                blocked.update((key, f"{a:X}:{old}"))
                conflicts.append({"kind": "typed-address", "address": a, "types": [old, kind], "stage": stage})
                continue
            declarations[a] = kind
            if key not in blocked and key not in requested_once:
                pending[key] = (a, kind)
                requested_once.add(key)
        raw = {}
        event = {"stage": stage, "requested": aliases, "fields": pending, "values": raw, "hex": {}, "complete": False}
        out["observations"].append(event)
        try:
            if pending:
                read(pending, raw, stage="secondary-" + stage, failure_evidence=out, require_identity=True)
        except Exception as error:
            reason("read-failed", stage=stage, error=f"{type(error).__name__}: {error}")
        for key, value in raw.items():
            a, kind = pending[key]; width = widths[kind]
            if type(value) is not int or not 0 <= value < 1 << (8 * width):
                blocked.add(key); reason("invalid-typed-value", stage=stage, field=key, value=value)
                continue
            data = value.to_bytes(width, "little")
            event["hex"][key] = data.hex()
            for i, byte in enumerate(data):
                if a+i in observed_bytes and observed_bytes[a+i] != byte:
                    conflicts.append({"kind": "overlapping-bytes", "stage": stage, "address": a+i,
                                      "first": observed_bytes[a+i], "later": byte, "field": key})
                else:
                    observed_bytes[a+i] = byte
            values[key] = value
        event["complete"] = all(k in values and k not in blocked for k in aliases)
        return event["complete"]

    def value(a, kind="u64", stage="initial"):
        key = f"{a:X}:{kind}"
        return None if key in blocked else stage_values.get(stage, {}).get(key)

    def blob(a, n):
        words = [value(a+i) for i in range(0, n, 8)]
        return None if any(v is None for v in words) else b"".join(v.to_bytes(8, "little") for v in words)[:n]

    def lifecycle(phase):
        sample = {"native": {}, "nativeComplete": False, "logs": {}, "scope": None, "available": False}
        out["lifecycle"][phase] = sample
        try:
            lifecycle_read(sample, "secondary-lifecycle-" + phase, out, require_identity=True)
            native, logs, scope = sample["native"], sample["logs"], sample["scope"]
            expected = {"head": 64, "tail": 64, "cachePointer": 64, "cacheCounter": 32,
                        "inField": 8, "frozen": 32, "eventContext": 64, "openMenu": 8,
                        "world": 8, "room": 8, "door": 8, "map": 16, "btl": 16, "evt": 16}
            typed = all(type(native.get(k)) is int and 0 <= native[k] < 1 << w for k,w in expected.items())
            typed = typed and type(native.get("eventState")) is int and -(1 << 31) <= native["eventState"] < 1 << 31
            regions = native.get("regions")
            typed = typed and isinstance(regions, list) and len(regions) == 64 and all(type(v) is int and 0 <= v < 1 << 64 for v in regions)
            location = [native.get(k) for k in ("world", "room", "door", "map", "btl", "evt")]
            arrival = logs.get("arrival")
            arrival_location = [int(arrival[k], 16 if k in ("world", "room") else 10)
                                for k in ("world", "room", "door", "map", "btl", "evt")] if arrival else None
            sample["available"] = bool(typed and sample["nativeComplete"] and scope and logs.get("lifecycle")
                and logs.get("arrivalAfterLifecycle") is True and scope["location"] == location
                and native.get("location") == location and arrival_location == location
                and int(arrival["epoch"]) == scope["epoch"])
        except Exception as error:
            reason("lifecycle-read-failed", phase=phase, error=f"{type(error).__name__}: {error}")
        if not sample["available"]:
            reason("lifecycle-unavailable", phase=phase)

    def signed_count(data):
        return int.from_bytes(data[4:8], "little", signed=True)

    if not span(base, 1):
        reason("invalid-module-base")
        out["endedMonotonic"] = time.monotonic()
        return out
    lifecycle("before")
    root_addresses = {"object0": base+0x2A25030, "object1": base+0x2A25038,
        "object2": base+0x2A25040, "went": base+0x2AE5A38, "item": base+0x2A25370, "bar": base+0x2AE5E50}
    collect([(a,"u64") for a in root_addresses.values()])
    roots = {k: value(a) for k,a in root_addresses.items()}; out["roots"] = roots
    inventory_ok, declared_count = True, 0
    for i in range(3):
        root = roots[f"object{i}"]
        table = {"tableIndex": i, "root": root, "rows": [], "complete": False}
        out["tables"].append(table)
        if root == 0:
            table.update(observedNull=True, complete=True); continue
        if not span(root, 8):
            table["failure"] = "root-unavailable"; inventory_ok = False; continue
        collect(fields(root, 8)); data = blob(root, 8)
        if data is None:
            table["failure"] = "header-unavailable"; inventory_ok = False; continue
        count = signed_count(data); table.update(headerHex=data.hex(), count=count)
        declared_count += max(count, 0)
        if count < 0 or declared_count > 8192 or (count and not span(root+8, count*96)):
            table["failure"] = "count-cap-or-span"; inventory_ok = False; continue
        requests = [(root+8+96*j+off,"u64") for j in range(count) for off in (0,0x48)]
        table["complete"] = collect(requests)
        inventory_ok &= table["complete"]
        for j in range(count):
            a = root+8+96*j; lo, high = value(a), value(a+0x48)
            table["rows"].append({"index": j, "address": a, "idWord": lo, "selectorGroupWord": high,
                "objectId": None if lo is None else lo & 0xFFFFFFFF,
                "group": None if high is None else high >> 48})
        ids = [r["objectId"] for r in table["rows"]]
        multiplicity = {}
        for v in ids:
            if v is not None:
                multiplicity[v] = multiplicity.get(v, 0) + 1
        table["duplicateIds"] = sorted(v for v,n in multiplicity.items() if n > 1)
        table["unsignedSorted"] = all(v is not None for v in ids) and ids == sorted(ids)
        table["nativeComparatorScope"] = "signed32(key-rowId); no effective bsearch binding claimed"
    out["inventoryComplete"] = bool(inventory_ok)
    census_refs = {n.get("objectEntry") for n in census.get("nodes", []) if isinstance(n, dict)}
    census_refs.discard(None)
    census_refs.discard(0)
    rooted_addresses = {r["address"] for t in out["tables"] for r in t["rows"]}
    out["censusReferences"] = [{"address": a, "rootedExactRow": a in rooted_addresses}
                               for a in sorted(census_refs)]
    physical = {}
    for table in out["tables"]:
        for row in table["rows"]:
            if row["group"] not in (None, 0) or row["objectId"] == 302 or row["address"] in census_refs:
                selected = physical.setdefault(row["address"], {"address": row["address"], "aliases": []})
                selected["aliases"].append({"tableIndex": table["tableIndex"], "index": row["index"]})
    selection_capped = len(physical) > 128
    out["selectedPhysicalCount"] = len(physical)
    if selection_capped:
        reason("selected-entry-cap", count=len(physical))
    selected = list(physical.values())[:128]
    out["selectedEntries"] = selected
    collect([f for r in selected for f in fields(r["address"],96)])
    for entry in selected:
        data = blob(entry["address"],96)
        entry["available"] = data is not None
        if data is not None:
            entry.update(hex=data.hex(), objectId=int.from_bytes(data[:4],"little"), objectType=data[4],
                selector=int.from_bytes(data[0x4C:0x4E],"little"), group=int.from_bytes(data[0x4E:0x50],"little"),
                form=int.from_bytes(data[0x57:0x58],"little",signed=True))

    # First descriptors are retained even if malformed; no later-match fallback.
    bar, B = out["bar"], roots["bar"]
    bar.update(root=B, descriptors=[], complete=False)
    if span(B,16):
        collect(fields(B,16)); data=blob(B,16)
        if data is not None:
            count=signed_count(data); bar.update(headerHex=data.hex(), count=count)
            if 0 <= count <= 256 and (not count or span(B+16,16*count)):
                bar["complete"]=collect([f for j in range(count) for f in fields(B+16+16*j,16)])
                for j in range(count):
                    a=B+16+16*j; raw=blob(a,16)
                    d={"index":j,"address":a,"hex":None if raw is None else raw.hex()}
                    if raw is not None:
                        d.update(type=int.from_bytes(raw[:2],"little"), name=int.from_bytes(raw[4:8],"little"),
                                 handle=int.from_bytes(raw[8:12],"little"), length=int.from_bytes(raw[12:16],"little"))
                    bar["descriptors"].append(d)
            else: reason("bar-count-cap-or-span",count=count)
    extents={}
    for name,tag in (("went",0x746E6577),("item",0x6D657469)):
        matches=[d for d in bar["descriptors"] if d.get("type")==2 and d.get("name")==tag]
        binding={"matches":[d["index"] for d in matches],"complete":False}; bar[name]=binding
        if not bar["complete"] or not matches: continue
        d=matches[0]; h=d["handle"]; masked=h&0x7FFFFFFF; bucket=masked>>25
        binding.update(descriptorIndex=d["index"],rawHandle=h,length=d["length"],bucket=bucket)
        a=base+0x2B0D720+8*bucket; collect([(a,"u64")]); region=value(a)
        pointer=None if region is None else region|(masked&0x1FFFFFF)
        binding.update(bucketAddress=a,bucketBase=region,decodedPointer=pointer)
        length=d["length"]
        ok=bool(h and region is not None and region & 0x1FFFFFF == 0 and pointer==roots[name]
            and span(pointer,length) and (length%4==0 if name=="went" else length>=8))
        binding["complete"]=ok
        if ok: extents[name]=(pointer,length)
    out["declaredExtentBindingComplete"]=len(extents)==2
    if len(extents)!=2: reason("bar-root-extent-unavailable")

    equipment=[]
    for entry in selected:
        for slot in (0,1):
            binding={"entryAddress":entry["address"],"aliases":entry["aliases"],"slot":slot,"available":False,"status":"unavailable"}
            out["bindings"].append(binding)
            if not entry["available"]: continue
            binding.update(group=entry["group"],selector=entry["selector"],form=entry["form"])
            selector,form=entry["selector"],entry["form"]
            zero=None; a=None
            if entry["group"]==0: zero="group-zero"
            elif selector in (1,14) and slot==1:
                if 1<=form<=10: a=base+0x9ABDA0+0xE04+(form-1)*0x38
                else: zero="form-null"
            elif not 1<=selector<=15: zero="selector-null"
            else:
                mapped={14:1,15:6}.get(selector,selector)
                a=base+0x9ABDA0+(mapped-1)*0x114+slot*2
            if zero: binding.update(available=True,status=zero,rawOutput=0)
            else: binding["equipmentAddress"]=a; equipment.append((a,"u64"))
    collect(equipment)
    for binding in out["bindings"]:
        if "equipmentAddress" not in binding: continue
        word=value(binding["equipmentAddress"])
        if word is None: continue
        binding["equipmentWord"]=word; binding["equipmentId"]=word&0xFFFF
        if binding["equipmentId"]==0: binding.update(available=True,status="equipment-zero",rawOutput=0)
    def inside(name,a,n):
        return name in extents and span(a,n) and extents[name][0]<=a and a+n<=sum(extents[name])
    pending=[b for b in out["bindings"] if b.get("equipmentId",0)>0]
    offset_requests=[]
    for b in pending:
        a=roots["went"]+4*b["group"] if roots["went"] is not None else 0
        b["offsetBeforeAddress"]=a
        if inside("went",a,4): offset_requests.append((a,"u32"))
        else: b["status"]="offset-outside-qualified-extent"
    collect(offset_requests)
    for b in pending:
        b["wentBefore"]=roots["went"]; b["offsetBeforeLookup"]=value(b["offsetBeforeAddress"],"u32")
        if b["offsetBeforeLookup"]==0: b.update(available=True,status="row-offset-zero",rawOutput=0)
    lookup=[b for b in pending if b.get("offsetBeforeLookup") not in (None,0)]
    items=out["items"]; items.update(root=roots["item"],rows=[],complete=False)
    if "item" in extents:
        I=roots["item"]; collect(fields(I,8)); data=blob(I,8)
        if data is not None:
            count=signed_count(data); items.update(headerHex=data.hex(),count=count)
            if max(count,0)<=4096 and 8+24*max(count,0)<=extents["item"][1]:
                items["complete"]=collect([(I+8+24*j,"u64") for j in range(max(count,0))])
                for j in range(max(count,0)):
                    a=I+8+24*j; word=value(a)
                    items["rows"].append({"index":j,"address":a,"word":word,
                        "itemId":None if word is None else word&0xFFFF,
                        "ordinal":None if word is None else (word>>32)&0xFFFF})
            else: reason("item-count-cap-or-extent",count=count)
    full_items=[]
    for b in lookup:
        if not items["complete"]: b["status"]="item-inventory-unavailable"; continue
        matches=[r for r in items["rows"] if r["itemId"]==b["equipmentId"]]
        b["itemMatches"]=[r["index"] for r in matches]
        if not matches: b.update(available=True,status="would-fault-missing-item"); continue
        r=matches[0]; b.update(itemIndex=r["index"],itemAddress=r["address"],ordinal=r["ordinal"])
        full_items.extend(fields(r["address"],24))
    collect(full_items)
    reloads=[b for b in lookup if "itemAddress" in b]
    for b in reloads:
        data=blob(b["itemAddress"],24); b["itemHex"]=None if data is None else data.hex()
    if reloads:
        collect([(root_addresses["went"],"u64")],"post-lookup")
        W1=value(root_addresses["went"],stage="post-lookup")
        requests=[]
        for b in reloads:
            b["wentAfter"]=W1
            if W1!=roots["went"]: b["status"]="went-reload-drift"; continue
            a=W1+4*b["group"]
            if inside("went",a,4): requests.append((a,"u32"))
        collect(requests,"post-lookup")
        outputs=[]
        for b in reloads:
            if b.get("wentAfter")!=roots["went"]: continue
            offset=value(b["offsetBeforeAddress"],"u32","post-lookup")
            b["offsetAfterLookup"]=offset
            if offset is None: continue
            if offset!=b["offsetBeforeLookup"]: b["status"]="offset-reload-drift"; continue
            index=offset+b["ordinal"]; a=W1+4*index; b.update(outputIndex=index,outputAddress=a)
            if inside("went",a,4): outputs.append((a,"u32"))
            else: b["status"]="output-outside-qualified-extent"
        collect(outputs,"post-lookup")
        for b in reloads:
            if "outputAddress" not in b or not b.get("itemHex"): continue
            output=value(b["outputAddress"],"u32","post-lookup")
            if output is not None: b.update(available=True,status="sampled-output",rawOutput=output)

    collect([(a,k) for a,k in declarations.items()],"readback")
    after=stage_values.get("readback",{})
    for stage,values in stage_values.items():
        if stage=="readback": continue
        for key,before in values.items():
            if key not in after or after[key]!=before:
                out["readbackChanges"].append({"stage":stage,"field":key,"before":before,"after":after.get(key)})
    lifecycle("after")
    left,right=(out["lifecycle"][p] for p in ("before","after"))
    out["lifecycle"]["stable"]=bool(left["available"] and right["available"] and left["native"]==right["native"]
        and left["scope"]==right["scope"] and left["logs"]==right["logs"])
    out["readbackStable"]=bool(not blocked and not conflicts and not out["readbackChanges"]
        and all(f"{a:X}:{k}" in after for a,k in declarations.items()) and not out.get("readFailures")
        and not any(r["code"] in ("read-failed","invalid-typed-value") for r in out["reasons"]))
    out["itemFirstMatchCoverageComplete"] = items["complete"]
    out["currentSelectionComplete"]=bool(inventory_ok and not selection_capped
        and all(r["rootedExactRow"] for r in out["censusReferences"]) and all(e["available"] for e in selected)
        and all(b["available"] for b in out["bindings"]) and not blocked and not conflicts)
    try:
        parent_base = census.get("moduleBase")
        if isinstance(parent_base, str):
            parent_base = int(parent_base, 0)
        out["parentIdentityAvailable"] = (type(census.get("pid")) is int and census["pid"] > 0
                                          and type(parent_base) is int and parent_base == base)
    except (ValueError, TypeError):
        out["parentIdentityAvailable"] = False
    parent_scope = census.get("rawOccupancy", {}).get("lifecycle", {}).get("after", {}).get("scope")
    out["parentScopeJoin"] = {"available": parent_scope is not None, "parentScope": parent_scope,
                              "matches": parent_scope is not None and parent_scope == left["scope"] == right["scope"]}
    # Lifecycle endpoints and the content decoder read the same handle table
    # independently. Equal endpoints alone cannot erase a known intervening
    # bucket disagreement. Retain partial joins as well as complete mismatches.
    bucket_join = out["consumedBucketJoin"]
    for name in ("went", "item"):
        binding = bar.get(name, {})
        if "bucketAddress" not in binding:
            continue
        bucket, address = binding["bucket"], binding["bucketAddress"]
        samples = {"binding": binding.get("bucketBase"), "readback": after.get(f"{address:X}:u64")}
        for phase, endpoint in (("lifecycleBefore", left), ("lifecycleAfter", right)):
            regions = endpoint["native"].get("regions")
            samples[phase] = regions[bucket] if isinstance(regions, list) and bucket < len(regions) else None
        available = {key: type(v) is int and 0 <= v < 1 << 64 for key,v in samples.items()}
        known = [v for key,v in samples.items() if available[key]]
        disagreement = len(set(known)) > 1
        complete = all(available.values())
        bucket_join["entries"].append({"bindingName": name, "bucket": bucket, "address": address,
            "samples": samples, "available": available, "complete": complete,
            "knownDisagreement": disagreement, "matches": complete and not disagreement})
        if disagreement:
            reason("consumed-bucket-lifecycle-disagreement", binding=name, bucket=bucket)
        elif not complete:
            reason("consumed-bucket-join-unavailable", binding=name, bucket=bucket)
    bucket_join["complete"] = all(e["matches"] for e in bucket_join["entries"])
    bucket_join["knownDisagreement"] = any(e["knownDisagreement"] for e in bucket_join["entries"])
    out["localBindingsStable"]=bool(out["currentSelectionComplete"] and out["readbackStable"]
        and out["lifecycle"]["stable"] and out["parentIdentityAvailable"] and bucket_join["complete"])
    parent_join = out["parentScopeJoin"]
    out["sampledScope"] = ("local-only-parent-unavailable" if not parent_join["available"] else
                            "parent-joined" if parent_join["matches"] else "parent-conflict")
    if parent_join["available"] and not parent_join["matches"]:
        reason("available-parent-scope-disagreement")
    out["sampledBindingsStable"]=bool(out["localBindingsStable"]
        and (not parent_join["available"] or parent_join["matches"]))
    out["endedMonotonic"]=time.monotonic()
    return out


def capture_native_resource_bindings(base, read, census, *, lifecycle_read):
    """Unintegrated bounded pointer diagnostic; supplied readers own the existing deadline."""
    receipt = {"schemaVersion": 1, "pid": census.get("pid"), "moduleBase": base,
        "parentModuleBase": census.get("moduleBase"), "lineage": census.get("lineage"),
        "startedMonotonic": time.monotonic(), "deadlineOwner": "supplied census read/lifecycle readers",
        "diagnosticCaps": {"actors": 16, "barEntries": 64}, "actors": [], "selection": [], "reasons": [],
        "allocator": {"reasons": []}, "allocatorPointerBindingComplete": False, "actorCoverageComplete": False,
        "executedCallObserved": False, "callbackClosureComplete": False, "pendingExclusionComplete": False,
        "controllerIncarnationQualified": False, "atomic": False, "creationAuthority": False,
        "readback": {"fields": {}, "before": {"values": {}, "hex": {}, "complete": False},
                     "after": {"values": {}, "hex": {}, "attempted": False, "complete": False}, "changes": []},
        "lifecycle": {"before": {}, "after": {}, "available": False, "stable": False, "reasons": []}}
    evidence, lifecycle, allocator = receipt["readback"], receipt["lifecycle"], receipt["allocator"]
    fields, values, blocked = {}, evidence["before"]["values"], set()
    widths = {"u8": 1, "u16": 2, "u32": 4, "i32": 4, "u64": 8}

    def reason(sink, code, **details):
        sink["reasons"].append({"code": code, **details})

    def span(address, size, alignment=1):
        return (type(base) is int and 0x10000 <= base < 0x800000000000
            and type(address) is int and type(size) is int and size > 0
            and base <= address and address % alignment == 0 and address + size <= 0x800000000000)

    def object_pointer(value, size, alignment, sink, field):
        if not span(value, size, alignment):
            reason(sink, "unsupported-pointer-span", field=field, value=value, bytes=size, alignment=alignment)
            return False
        return True

    def collect(batch, stage, phase="before"):
        sample = evidence[phase]
        if phase == "before":
            fields.update(batch)
        aliases = {}
        for label, (address, kind) in fields.items():
            evidence["fields"][label] = {"address": address, "type": kind, "width": widths[kind]}
            aliases.setdefault(address, []).append({"label": label, "type": kind})
            if not span(address, widths[kind], 8 if kind == "u64" else 1):
                blocked.add(label)
                reason(receipt, "invalid-field-span", stage=stage, field=label, address=address, width=widths[kind])
        for address, labels in aliases.items():
            if len({f["type"] for f in labels}) > 1:
                blocked.update(f["label"] for f in labels)
                reason(receipt, "typed-address-conflict", stage=stage, address=address, fields=labels)
        # Conflicts retain all labels. No mixed-width request is sent, including
        # on readback. Independently readable components keep their own evidence.
        allowed = {k: v for k, v in batch.items() if k not in blocked}
        raw = {}
        stage_receipt = {"stage": stage, "phase": phase, "fieldLabels": list(batch), "complete": False}
        receipt.setdefault("stages", []).append(stage_receipt)
        failed = False
        try:
            if allowed:
                read(allowed, raw, stage="resource-binding-" + stage, failure_evidence=receipt)
        except Exception as error:
            failed = True
            reason(receipt, "read-failed", stage=stage, phase=phase, error=f"{type(error).__name__}: {error}")
        for label, value in raw.items():
            kind = fields[label][1]
            width = widths[kind]
            lower, upper = (-(1 << 31), (1 << 31) - 1) if kind == "i32" else (0, (1 << (8 * width)) - 1)
            if type(value) is not int or not lower <= value <= upper:
                sample.setdefault("invalidValues", {})[label] = value
                reason(receipt, "invalid-typed-value", stage=stage, phase=phase, field=label, type=kind)
                continue
            sample["values"][label] = value
            sample["hex"][label] = value.to_bytes(width, "little", signed=kind == "i32").hex()
        stage_receipt["complete"] = not failed and all(k in sample["values"] and k not in blocked for k in batch)
        return stage_receipt["complete"]

    def sample_lifecycle(phase):
        sample = lifecycle[phase]
        sample.update(native={}, nativeComplete=False, logs={}, scope=None, available=False)
        try:
            lifecycle_read(sample, "resource-binding-lifecycle-" + phase, lifecycle)
            native = sample["native"]
            header_types = {"head": "u64", "tail": "u64", "cachePointer": "u64", "cacheCounter": "u32",
                "inField": "u8", "frozen": "u32", "eventState": "i32", "eventContext": "u64", "openMenu": "u8",
                "world": "u8", "room": "u8", "door": "u8", "map": "u16", "btl": "u16", "evt": "u16"}
            for key, kind in header_types.items():
                value = native.get(key)
                low, high = (-(1 << 31), (1 << 31)-1) if kind == "i32" else (0, (1 << (8*widths[kind]))-1)
                if type(value) is not int or not low <= value <= high:
                    raise StepFailed(f"invalid or missing lifecycle typed field {key}")
            if (len(native.get("regions", [])) != 64
                    or any(type(v) is not int or not 0 <= v < 1 << 64 for v in native["regions"])
                    or native.get("location") != [native[k] for k in ("world", "room", "door", "map", "btl", "evt")]):
                raise StepFailed("invalid lifecycle region table or location")
            sample["available"] = bool(sample["nativeComplete"] and sample["scope"]
                and sample["logs"].get("arrival") and sample["logs"].get("lifecycle"))
        except Exception as error:
            reason(lifecycle, "lifecycle-read-failed", phase=phase, error=f"{type(error).__name__}: {error}")
        if not sample["available"]:
            reason(lifecycle, "lifecycle-unavailable", phase=phase)

    if not span(base, 1):
        reason(receipt, "invalid-module-base")
        receipt["endedMonotonic"] = time.monotonic()
        return receipt
    sample_lifecycle("before")
    raw_parent = census.get("rawOccupancy", {})
    parent_lifecycle = raw_parent.get("lifecycle", {})
    parent_raw_ok = (raw_parent.get("schemaVersion") == 2 and raw_parent.get("listedOccupancyComplete") is True
        and parent_lifecycle.get("available") is True and parent_lifecycle.get("stable") is True
        and all(parent_lifecycle.get(p, {}).get("rawReadbackJoin", {}).get("complete") is True for p in ("before", "after")))
    parent_actor_ok = parent_raw_ok and all(census.get(k) is True for k in
        ("complete", "listComplete", "classificationComplete", "actorIdentityStable", "lifecycleStable", "epochStable", "safeGameplay"))
    try:
        module_ok = type(base) is int and type(census.get("pid")) is int and census["pid"] > 0 and int(census["moduleBase"], 0) == base
    except (KeyError, TypeError, ValueError):
        module_ok = False
    if not module_ok:
        reason(receipt, "parent-process-module-unavailable")
    if not parent_raw_ok:
        reason(receipt, "parent-raw-scope-unavailable")
    eligible, selection_ok = [], parent_actor_ok
    living = census.get("livingCombatRows", [])
    raw_nodes = raw_parent.get("lists", {}).get("active", {}).get("nodes", [])
    for node in census.get("nodes", []):
        address = node.get("address")
        choice = {"actor": address, "eligible": False, "reasons": []}
        receipt["selection"].append(choice)
        matches = [n for n in living if n.get("address") == address]
        if not matches or node.get("objectId") != 302 or node.get("objectType") != 4:
            reason(choice, "not-selected-living-object302-type4")
            continue
        originals = [p for p in census.get("provenance", []) if p.get("actor") == address]
        raws = [n for n in raw_nodes if n.get("address") == address]
        identities = ("objectEntry", "status", "controller", "spawnRecord", "nextHandle", "flags120")
        qualified = (parent_actor_ok and len(matches) == len(originals) == len(raws) == 1
            and matches[0].get("combatEligible") is True and type(matches[0].get("hp")) is int and matches[0]["hp"] > 0
            and all(matches[0].get(k) == node.get(k) for k in identities)
            and all(raws[0].get("readSuccess", {}).get(k) is True and raws[0].get(k) == node.get(k) for k in identities))
        provenance = originals[0] if len(originals) == 1 else {}
        qualified = qualified and (provenance.get("ordinaryTableIdentityVerified") is True
            and provenance.get("controller") == node.get("controller") and provenance.get("pointer") == node.get("spawnRecord")
            and provenance.get("objectId") == 302 and raws[0].get("readSuccess", {}).get("recordId") is True
            and all(raws[0].get("readSuccess", {}).get(k) is True and raws[0].get(k) == node.get(k)
                    for k in ("objectId", "objectType"))
            and type(provenance.get("recordId")) is int and raws[0].get("recordId") == provenance.get("recordId"))
        try:
            original_record = bytes.fromhex(provenance.get("hex", ""))
            qualified = (qualified and len(original_record) == 64
                and struct.unpack_from("<I", original_record)[0] == 302
                and struct.unpack_from("<H", original_record, 0x1E)[0] == provenance["recordId"])
        except (TypeError, ValueError, KeyError, struct.error):
            qualified = False
        if not qualified:
            reason(choice, "qualified-census-raw-provenance-join-unavailable")
            selection_ok = False
            continue
        choice["eligible"] = True
        eligible.append((node, provenance))
    target_addresses = [n.get("address") for n in living if n.get("objectId") == 302 and n.get("objectType") == 4]
    if (len(set(target_addresses)) != len(target_addresses)
            or len({n[0]["address"] for n in eligible}) != len(eligible)
            or any(not any(n.get("address") == address for n in census.get("nodes", [])) for address in target_addresses)):
        reason(receipt, "living-traversal-membership-unavailable")
        selection_ok = False
    receipt.update(eligibleActors=len(eligible), attemptedActors=min(len(eligible), 16), omittedActors=max(0, len(eligible) - 16))
    if not eligible:
        reason(receipt, "no-qualified-actors")
    if len(eligible) > 16:
        reason(receipt, "actor-cap-exhausted", omitted=len(eligible) - 16)
    batch = {"allocator:root": (base + 0x9BA920, "u64")} if type(base) is int else {}
    for index, (node, provenance) in enumerate(eligible[:16]):
        actor = {"actor": node["address"], "prefix": f"actor{index}:", "selection": dict(node),
            "provenance": dict(provenance), "reasons": [], "modelPointerBindingComplete": False}
        receipt["actors"].append(actor)
        if object_pointer(actor["actor"], 0xA94, 8, actor, "actor"):
            batch.update({actor["prefix"] + name: (actor["actor"] + offset, kind) for name, offset, kind in (
                ("objectEntry", 0x918, "u64"), ("status", 0x5C0, "u64"), ("controller", 0x9E8, "u64"),
                ("spawnRecord", 0x9F0, "u64"), ("flags120", 0x120, "u32"), ("nextHandle", 0xA90, "u32"),
                ("bar", 0x920, "u64"), ("cached", 0xA88, "u64"))})
    collect(batch, "roots-identity")

    def actor_values(actor, names):
        prefix = actor["prefix"]
        if actor["reasons"]:
            return None
        if not all(prefix + n in values and prefix + n not in blocked for n in names):
            reason(actor, "dependency-unavailable", fields=names)
            return None
        return [values[prefix + n] for n in names]

    batch = {}
    a = values.get("allocator:root")
    if object_pointer(a, 8, 16, allocator, "allocator"):
        batch["allocator:vtable"] = (a, "u64")
    for actor in receipt["actors"]:
        names = ["objectEntry", "status", "controller", "spawnRecord", "flags120", "nextHandle", "bar", "cached"]
        identity = actor_values(actor, names)
        if identity is None:
            continue
        observed = dict(zip(names, identity)); actor["identity"] = observed
        if any(observed[k] != actor["selection"][k] for k in names[:6]):
            reason(actor, "census-identity-changed")
            continue
        o, s, c, r, b = (observed[k] for k in ("objectEntry", "status", "controller", "spawnRecord", "bar"))
        if not all([object_pointer(o, 5, 8, actor, "objectEntry"), object_pointer(s, 1, 8, actor, "status"),
                    object_pointer(c, 1, 8, actor, "controller"), object_pointer(r, 0x20, 4, actor, "spawnRecord"),
                    object_pointer(b, 8, 4, actor, "bar")]):
            continue
        p = actor["prefix"]
        batch.update({p+"objectId": (o, "u32"), p+"objectEntryType": (o+4, "u8"),
                      p+"recordId": (r+0x1E, "u16"), p+"barCount": (b+4, "i32")})
    collect(batch, "allocator-object-bar")
    batch = {}
    va = values.get("allocator:vtable")
    if va == base + 0x5B2BB0 and "allocator:vtable" not in blocked:
        batch["allocator:target"] = (va+8, "u64")
    else:
        reason(allocator, "unsupported-allocator-vtable", value=va)
    for actor in receipt["actors"]:
        data = actor_values(actor, ["objectId", "objectEntryType", "recordId", "barCount"])
        if data is None:
            continue
        oid, kind, rid, count = data
        if (oid, kind, rid) != (302, 4, actor["provenance"]["recordId"]):
            reason(actor, "object-record-mismatch", objectId=oid, objectEntryType=kind, recordId=rid)
            continue
        if not 1 <= count <= 64:
            reason(actor, "unsupported-bar-count", count=count, diagnosticCap=64)
            continue
        actor["barCount"] = count
        b = actor["identity"]["bar"]
        if object_pointer(b, 0x10 + 0x10 * count, 4, actor, "bar-entry-span"):
            batch.update({actor["prefix"]+f"barType{n}": (b+0x10+0x10*n, "u16") for n in range(count)})
    collect(batch, "allocator-slot-bar-types")
    if values.get("allocator:target") != base + 0x19C2B0:
        reason(allocator, "allocator-target-mismatch", value=values.get("allocator:target"))
    batch = {}
    for actor in receipt["actors"]:
        types = actor_values(actor, [f"barType{n}" for n in range(actor.get("barCount", 0))])
        if types is None:
            continue
        actor["barTypes"] = types
        if 4 not in types:
            reason(actor, "bar-type4-not-found")
            continue
        j = types.index(4); e = actor["identity"]["bar"] + 0x10 + 0x10*j
        actor.update(selectedIndex=j, selectedEntry=e, selectedPrefix=types[:j+1])
        batch.update({actor["prefix"]+"handle": (e+8, "u32"), actor["prefix"]+"resourceSize": (e+0xC, "u32")})
    collect(batch, "selected-entry")
    batch = {}
    for actor in receipt["actors"]:
        data = actor_values(actor, ["handle", "resourceSize"])
        if data is None:
            continue
        h, size = data
        if h == 0 or not h & 0x80000000 or size < 0x94:
            reason(actor, "unsupported-tagged-entry-shape", handle=h, resourceSize=size,
                   limit="bit31 is a diagnostic loader-shape guard, not a native decoder rejection")
            continue
        actor.update(handle=h, resourceSize=size, bucket=(h & 0x7FFFFFFF) >> 25)
        batch[actor["prefix"]+"bucket"] = (base+0x2B0D720+8*actor["bucket"], "u64")
    collect(batch, "fresh-handle-bucket")
    batch = {}
    for actor in receipt["actors"]:
        data = actor_values(actor, ["bucket"])
        if data is None:
            continue
        region = data[0]
        if not 0 < region < 0x800000000000 or region & 0x1FFFFFF:
            reason(actor, "invalid-handle-region", value=region)
            continue
        p = region | (actor["handle"] & 0x1FFFFFF)
        actor["model"] = p
        if not object_pointer(p, actor["resourceSize"], 8, actor, "model-resource-span"):
            continue
        if actor["identity"]["cached"] != p:
            reason(actor, "cached-model-mismatch", cached=actor["identity"]["cached"], decoded=p)
            continue
        batch.update({actor["prefix"]+"modelVtable": (p, "u64"), actor["prefix"]+"modelData": (p+8, "u64"),
                      actor["prefix"]+"modelKind": (p+0x90, "i32")})
    collect(batch, "model-shape")
    batch = {}
    classes = {3: (0x5B3E00, 0x1C21E0, "ModelSKL"), 2: (0x5B3D40, 0x1C1500, "ModelBG"), -1: (0x5B3EC0, 0x1C2CA0, "ModelMulti")}
    for actor in receipt["actors"]:
        data = actor_values(actor, ["modelVtable", "modelData", "modelKind"])
        if data is None:
            continue
        vp, pointer, kind = data
        if pointer != actor["model"]+0x90:
            reason(actor, "model-data-mismatch", value=pointer)
            continue
        if kind not in classes or vp != base + classes[kind][0]:
            reason(actor, "unsupported-model-pair", modelKind=kind, vtable=vp)
            continue
        actor.update(modelClass=classes[kind][2], expectedTarget=base+classes[kind][1])
        batch[actor["prefix"]+"modelTarget"] = (vp+0x40, "u64")
    collect(batch, "model-slot")
    for actor in receipt["actors"]:
        data = actor_values(actor, ["modelTarget"])
        if data is not None and data[0] != actor["expectedTarget"]:
            reason(actor, "model-target-mismatch", value=data[0])
    evidence["before"]["complete"] = all(k in values and k not in blocked for k in fields)
    evidence["after"]["requested"] = True
    evidence["after"]["complete"] = collect(fields, "frozen-address-readback", "after")
    evidence["after"]["attempted"] = any(work["stage"] == "resource-binding-frozen-address-readback"
        and work["peekAttempted"] for work in receipt.get("readWork", []))
    sample_lifecycle("after")
    for phase in ("before", "after"):
        sample = evidence[phase]
        sample["unreadFields"] = [k for k in fields if k not in sample["values"] or k in blocked]
    for label, before in values.items():
        if label in evidence["after"]["values"] and before != evidence["after"]["values"][label]:
            evidence["changes"].append({"field": label, "before": before, "after": evidence["after"]["values"][label]})
    native_keys = ("head", "tail", "location", "regions", "inField", "frozen", "eventState", "eventContext", "openMenu")
    lifecycle["available"] = all(lifecycle[p]["available"] for p in ("before", "after"))
    for phase in ("before", "after"):
        local = lifecycle[phase]
        try:
            native, observed, scope = local["native"], local["logs"], local["scope"]
            parent = parent_lifecycle[phase]
            if not local["available"] or not parent_raw_ok:
                raise StepFailed("local or parent raw scope unavailable")
            if (any(native[k] != lifecycle["before"]["native"][k] for k in native_keys)
                    or observed != lifecycle["before"]["logs"] or scope != lifecycle["before"]["scope"]):
                raise StepFailed("local lifecycle bookends changed")
            if (any(native[k] != parent["native"][k] for k in native_keys)
                    or scope != parent["scope"] or observed != parent["logs"]):
                raise StepFailed("local lifecycle differs from qualified census raw scope")
            arrival = [int(observed["arrival"][k], 16 if k in ("world", "room") else 10)
                       for k in ("world", "room", "door", "map", "btl", "evt")]
            if (not observed["arrivalAfterLifecycle"] or native["location"] != arrival or scope["location"] != arrival
                    or native["inField"] == 0 or native["frozen"] != 0 or native["eventState"] != 0
                    or native["eventContext"] != 0 or native["openMenu"] != 255):
                raise StepFailed("local gameplay/arrival scope unqualified")
            if eligible and (native["location"] != census["after"]["location"]
                    or observed["arrival"] != census["logsAfter"]["arrival"]):
                raise StepFailed("local scope differs from actor-qualifying legacy census")
            parent_raw_values = raw_parent["readback"][phase]["values"]
            if (native["head"] != parent_raw_values["activeHead"] or native["tail"] != parent_raw_values["activeTail"]
                    or len(native["regions"]) != 64 or any(native["regions"][n] != parent_raw_values[f"bucket{n}"] for n in range(64))):
                raise StepFailed("local roots/buckets disagree with parent raw readback")
            for actor in receipt["actors"]:
                label = actor["prefix"]+"bucket"
                if label in evidence[phase]["values"] and evidence[phase]["values"][label] != native["regions"][actor["bucket"]]:
                    raise StepFailed("fresh selected bucket disagrees with local lifecycle")
        except (KeyError, TypeError, ValueError, StepFailed) as error:
            reason(lifecycle, "scope-or-root-join-unavailable", phase=phase, error=str(error))
    lifecycle["stable"] = lifecycle["available"] and not lifecycle["reasons"] and module_ok
    def binding_fields_complete(prefix):
        keys = [k for k in fields if k.startswith(prefix)]
        return bool(keys) and all(k not in blocked and k in values and k in evidence["after"]["values"]
            and values[k] == evidence["after"]["values"][k] for k in keys)
    receipt["allocatorPointerBindingComplete"] = bool(lifecycle["stable"] and not allocator["reasons"]
        and binding_fields_complete("allocator:"))
    for actor in receipt["actors"]:
        actor["modelPointerBindingComplete"] = bool(lifecycle["stable"] and not actor["reasons"]
            and binding_fields_complete(actor["prefix"]))
    receipt["actorCoverageComplete"] = bool(selection_ok and lifecycle["stable"] and len(eligible) <= 16
        and all(a["modelPointerBindingComplete"] for a in receipt["actors"]))
    receipt["endedMonotonic"] = time.monotonic()
    return receipt


def capture_native_controller_id_coverage(base, read, census, *, lifecycle_read):
    """Bounded ordinary definitions and sampled list joins; never creation authority."""
    receipt = {"schemaVersion": 1, "diagnosticOnly": True, "pid": census.get("pid"),
        "moduleBase": base, "parentModuleBase": census.get("moduleBase"), "lineage": census.get("lineage"),
        "deadlineOwner": "supplied census read/lifecycle readers", "startedMonotonic": time.monotonic(),
        "diagnosticCaps": {"tableEntries": 64, "rawNodes": 256, "recordsPerDefinition": 256, "logicalRecords": 1024},
        "table": [], "definitions": [], "references": [], "idConflicts": [], "definitionAliases": [],
        "reasons": [], "parentReasons": [], "rawReasons": [], "tableReasons": [],
        "tableInventoryComplete": False, "ordinaryRecordBytesComplete": False,
        "listedReferenceJoinComplete": False, "sampledSupportedCoverageComplete": False,
        "noIdConflictInSampledScope": None,
        "idConflictScope": "duplicate raw u16 IDs among sampled ordinary slots; not an executed native lookup",
        "globalControllerIdCoverageComplete": False,
        "controllerIncarnationQualified": False, "pendingExclusionComplete": False, "atomic": False,
        "creationAuthority": False, "executedCallObserved": False,
        "readback": {"fields": {}, "before": {"values": {}, "complete": False},
                     "after": {"values": {}, "complete": False}, "changes": []},
        "lifecycle": {"before": {}, "after": {}, "available": False, "stable": False, "reasons": []}}
    rb, life = receipt["readback"], receipt["lifecycle"]
    fields, blocked, failed_fields = {}, set(), set()
    widths = {"u8": 1, "u16": 2, "u32": 4, "i32": 4, "u64": 8}
    raw_fields, parent_expected, raw_nodes = {}, {}, []
    table_labels, definition_labels = set(), set()

    def issue(sink, code, **details):
        sink.append({"code": code, **details})

    def span(address, size):
        return (type(base) is int and 0x10000 <= base < 0x800000000000
            and type(address) is int and type(size) is int and size > 0
            and base <= address and address + size <= 0x800000000000)

    def typed(value, kind):
        low, high = (-(1 << 31), (1 << 31)-1) if kind == "i32" else (0, (1 << (widths[kind]*8))-1)
        return type(value) is int and low <= value <= high

    def collect(batch, stage, phase="before"):
        if phase == "before":
            fields.update(batch)
        aliases = {}
        for label, (address, kind) in fields.items():
            rb["fields"][label] = {"address": address, "type": kind, "width": widths[kind]}
            aliases.setdefault(address, []).append(label)
            if not span(address, widths[kind]):
                blocked.add(label)
                issue(receipt["reasons"], "invalid-field-span", stage=stage, field=label, address=address)
        for address, labels in aliases.items():
            if len({fields[k][1] for k in labels}) > 1:
                blocked.update(labels)
                issue(receipt["reasons"], "typed-address-conflict", stage=stage, address=address, fields=labels)
        allowed = {k: v for k, v in batch.items() if k not in blocked}
        obtained = {}
        work = {"stage": stage, "phase": phase, "requested": len(batch), "allowed": len(allowed), "complete": False}
        receipt.setdefault("stages", []).append(work)
        try:
            if allowed:
                read(allowed, obtained, stage="controller-id-" + stage, failure_evidence=receipt)
        except Exception as error:
            failed_fields.update(set(batch) - set(obtained) or set(batch))
            issue(receipt["reasons"], "read-failed", stage=stage, phase=phase, error=f"{type(error).__name__}: {error}")
        for k, value in obtained.items():
            if typed(value, fields[k][1]):
                rb[phase]["values"][k] = value
            else:
                rb[phase].setdefault("invalidValues", {})[k] = value
                issue(receipt["reasons"], "invalid-typed-value", stage=stage, phase=phase, field=k)
        work["completed"] = sum(k in rb[phase]["values"] and k not in blocked for k in batch)
        work["complete"] = work["completed"] == len(batch) and not failed_fields.intersection(batch)

    def get(label):
        return rb["before"]["values"].get(label) if label not in blocked else None

    def stable(labels):
        return all(k not in blocked and k not in failed_fields and k in rb["before"]["values"] and k in rb["after"]["values"]
            and rb["before"]["values"][k] == rb["after"]["values"][k] for k in labels)

    def words(prefix, count, width, phase):
        labels = [prefix + str(i) for i in range(count)]
        values = rb[phase]["values"]
        if any(k in blocked or k not in values for k in labels):
            return None
        return b"".join(values[k].to_bytes(width, "little") for k in labels)

    native_types = {"head": "u64", "tail": "u64", "cachePointer": "u64", "cacheCounter": "u32",
        "inField": "u8", "frozen": "u32", "eventState": "i32", "eventContext": "u64", "openMenu": "u8",
        "world": "u8", "room": "u8", "door": "u8", "map": "u16", "btl": "u16", "evt": "u16"}
    native_keys = ("head", "tail", "location", "regions", "inField", "frozen", "eventState", "eventContext", "openMenu")

    def lifecycle(phase):
        sample = life[phase]
        sample.update(native={}, nativeComplete=False, logs={}, scope=None, available=False)
        try:
            lifecycle_read(sample, "controller-id-lifecycle-" + phase, life)
            n, logs, scope = sample["native"], sample["logs"], sample["scope"]
            if (not sample["nativeComplete"] or any(not typed(n.get(k), t) for k,t in native_types.items())
                    or len(n.get("regions", [])) != 64 or any(not typed(v, "u64") for v in n["regions"])
                    or n.get("location") != [n[k] for k in ("world", "room", "door", "map", "btl", "evt")]
                    or not scope or any(type(scope.get(k)) is not int or scope[k] < 0 for k in ("loadSerial", "transitionSerial", "epoch"))
                    or not logs.get("arrival") or not logs.get("lifecycle")):
                raise StepFailed("lifecycle typed fields/scope unavailable")
            sample["available"] = True
            arrival = [int(logs["arrival"][k], 16 if k in ("world", "room") else 10)
                       for k in ("world", "room", "door", "map", "btl", "evt")]
            if (scope["epoch"] != int(logs["arrival"]["epoch"]) or not logs.get("arrivalAfterLifecycle") or scope["location"] != n["location"] or arrival != n["location"]
                    or n["inField"] == 0 or n["frozen"] != 0 or n["eventState"] != 0
                    or n["eventContext"] != 0 or n["openMenu"] != 255):
                issue(life["reasons"], "lifecycle-scope-unqualified", phase=phase)
        except Exception as error:
            issue(life["reasons"], "lifecycle-unavailable", phase=phase, error=f"{type(error).__name__}: {error}")

    if not span(base, 1):
        issue(receipt["reasons"], "invalid-module-base")
        return receipt
    lifecycle("before")
    parent = census.get("rawOccupancy", {})
    if not isinstance(parent, dict):
        issue(receipt["parentReasons"], "parent-raw-invalid-shape")
        parent = {}
    try:
        if (type(census.get("pid")) is not int or census["pid"] <= 0 or int(census["moduleBase"], 0) != base
                or any(census.get(k) is not True for k in ("complete", "listComplete", "classificationComplete", "actorIdentityStable", "lifecycleStable", "epochStable", "safeGameplay"))
                or parent.get("schemaVersion") != 2 or parent.get("listedOccupancyComplete") is not True
                or parent["lifecycle"].get("available") is not True or parent["lifecycle"].get("stable") is not True
                or any(parent["lifecycle"][p]["rawReadbackJoin"]["complete"] is not True for p in ("before", "after"))):
            raise StepFailed("parent raw/module qualification unavailable")
    except (KeyError, TypeError, ValueError, StepFailed) as error:
        issue(receipt["parentReasons"], "parent-raw-unavailable", error=str(error))
    roots = {k: (base+r, "u64") for k,r in (("activeHead",0x2A171C8),("activeTail",0x2A171D0),
        ("deferredHead",0x2A171D8),("deferredTail",0x2A171E0))}
    roots.update({f"bucket{i}": (base+0x2B0D720+8*i,"u64") for i in range(64)})
    raw_fields.update(roots)
    try:
        seen = set()
        for lane in ("active", "deferred"):
            listing = parent["lists"][lane]
            for node in listing["nodes"]:
                if len(raw_nodes) >= 256:
                    raise StepFailed("aggregate raw node cap")
                address = node["address"]
                if not span(address, 0xA94) or address % 8 or address in seen:
                    raise StepFailed("invalid/repeated raw node address")
                seen.add(address)
                prefix = f"node{len(raw_nodes)}:"
                raw_nodes.append({"list": lane, "address": address, "prefix": prefix, "parent": node})
                for key,offset,kind in (("nextHandle",0xA90,"u32"),("flags120",0x120,"u32"),
                    ("objectEntry",0x918,"u64"),("status",0x5C0,"u64"),("controller",0x9E8,"u64"),("spawnRecord",0x9F0,"u64")):
                    raw_fields[prefix+key] = (address+offset,kind)
                obj, record = node.get("objectEntry"), node.get("spawnRecord")
                if span(obj,10):
                    for key,off,kind in (("objectId",0,"u32"),("objectType",4,"u8"),("namePrefix",8,"u16")):
                        raw_fields[prefix+key] = (obj+off,kind)
                if record:
                    raw_fields[prefix+"recordId"] = (record+0x1E,"u16")
        for k, (address, kind) in raw_fields.items():
            spec = parent["readback"]["fields"][k]
            if spec["address"] != address or spec["type"] != kind:
                raise StepFailed("parent raw field descriptor mismatch")
            value = parent["readback"]["before"]["values"][k]
            if not typed(value,kind) or parent["readback"]["after"]["values"].get(k) != value:
                raise StepFailed("parent raw values unavailable/changing")
            parent_expected[k] = value
        for node in raw_nodes:
            for k in raw_fields:
                if k.startswith(node["prefix"]) and (node["parent"].get(k.split(":",1)[1]) != parent_expected[k]
                        or node["parent"].get("readSuccess", {}).get(k.split(":",1)[1]) is not True):
                    raise StepFailed("parent node/raw value mismatch")
        if any(parent["readback"][p].get("complete") is not True for p in ("before", "after")):
            raise StepFailed("parent raw readback incomplete")
    except (KeyError, TypeError, ValueError, StepFailed) as error:
        issue(receipt["parentReasons"], "parent-raw-inventory-unavailable", error=str(error))
    collect({"count": (base+0x2A10418,"i32"), **raw_fields}, "roots-raw-before")
    table_labels.add("count")
    count = get("count")
    receipt["declaredTableCount"] = count
    if type(count) is int and 0 <= count <= 64:
        batch = {f"entry{i}:{k}": (base+0x2A10010+i*16+off,t)
                 for i in range(count) for k,off,t in (("key",0,"u32"),("flags",4,"u32"),("pointer",8,"u64"))}
        table_labels.update(batch)
        collect(batch, "table-before")
    else:
        issue(receipt["tableReasons"], "table-count-unavailable-or-cap", value=count)
        count = 0
    controller_batch = {}
    for i in range(count):
        entry = {"tableIndex": i, **{k:get(f"entry{i}:{k}") for k in ("key","flags","pointer")}}
        receipt["table"].append(entry)
        if any(entry[k] is None for k in ("key","flags","pointer")):
            issue(receipt["tableReasons"], "table-entry-unavailable", tableIndex=i)
        elif entry["flags"] & 1:
            entry["status"] = "unsupported alternate script pointer"
            issue(receipt["tableReasons"], "alternate-table-entry", tableIndex=i)
        elif not span(entry["pointer"],0x40) or entry["pointer"] % 8:
            issue(receipt["tableReasons"], "invalid-controller-pointer", tableIndex=i, value=entry["pointer"])
        else:
            definition = {"tableIndex": i, "pointer": entry["pointer"], "key": entry["key"], "prefix": f"def{i}:",
                          "records": [], "reasons": [], "fullBytesComplete": False}
            receipt["definitions"].append(definition)
            controller_batch.update({definition["prefix"]+k:(entry["pointer"]+off,t) for k,off,t in
                (("groupKey",0,"u32"),("flags",4,"u32"),("header",8,"u64"),("spawnArray",0x30,"u64"),("regionArray",0x38,"u64"))})
    definition_labels.update(controller_batch)
    collect(controller_batch, "controllers-before")
    headers = {}
    for d in receipt["definitions"]:
        d.update({k:get(d["prefix"]+k) for k in ("groupKey","flags","header","spawnArray","regionArray")})
        if d["groupKey"] != d["key"]:
            issue(d["reasons"], "table-key-mismatch")
        if not span(d["header"],44):
            issue(d["reasons"], "invalid-header-span", value=d["header"])
        else:
            headers.update({d["prefix"]+f"headerWord{i}":(d["header"]+4*i,"u32") for i in range(11)})
    definition_labels.update(headers)
    collect(headers,"headers-before")
    records, logical_count = {}, 0
    for d in receipt["definitions"]:
        data = words(d["prefix"]+"headerWord",11,4,"before")
        if data is None:
            issue(d["reasons"],"header-unavailable")
            continue
        d["headerBeforeHex"] = data.hex()
        d["headerFields"] = {"type":data[0],"flags":data[1],"headerId":int.from_bytes(data[2:4],"little"),
            "spawnCount":int.from_bytes(data[4:6],"little"),"regionCount":int.from_bytes(data[6:8],"little"),"activationMarker":data[14]}
        h = d["headerFields"]; n = h["spawnCount"]
        d["supportedType"] = h["type"] in (1,2)
        if not d["supportedType"]:
            issue(d["reasons"],"unsupported-header-type",value=h["type"])
        logical_count += n
        if n > 256 or logical_count > 1024:
            d["omittedRecordIndices"] = [0,n]
            issue(d["reasons"],"record-cap",declared=n,logicalTotal=logical_count)
            continue
        start = d["spawnArray"]
        d["layoutValidated"] = (start == d["header"]+44 and d["regionArray"] == start+n*64
                                and span(start,max(1,n*64)) and span(d["regionArray"],1))
        if not d["layoutValidated"]:
            issue(d["reasons"],"header-array-layout-mismatch")
            continue
        for i in range(n):
            prefix = d["prefix"]+f"record{i}:"
            row = {"recordIndex":i,"pointer":start+i*64,"prefix":prefix,"fullBytesComplete":False}
            d["records"].append(row)
            records.update({prefix+str(j):(row["pointer"]+j*8,"u64") for j in range(8)})
    receipt["declaredLogicalRecordCount"] = logical_count
    definition_labels.update(records)
    collect(records,"records-before")
    # Definitions first: a later missing raw field cannot erase completed table readback.
    ordered_after = {k:v for k,v in fields.items() if k not in raw_fields}
    ordered_after.update({k:fields[k] for k in raw_fields})
    collect(ordered_after,"all-after","after")
    lifecycle("after")
    # Full-byte overlap checks catch independently sampled overlapping typed reads.
    for phase in ("before","after"):
        bytes_seen = {}
        for k,value in rb[phase]["values"].items():
            address,kind = fields[k]
            for offset,byte in enumerate(value.to_bytes(widths[kind],"little",signed=kind=="i32")):
                other = bytes_seen.get(address+offset)
                if other is not None and other[0] != byte:
                    blocked.update((k,other[1]))
                    issue(receipt["reasons"],"overlapping-byte-mismatch",phase=phase,address=address+offset,fields=[other[1],k])
                else:
                    bytes_seen[address+offset] = (byte,k)
        rb[phase]["unreadFields"] = [k for k in fields if k not in rb[phase]["values"] or k in blocked]
        rb[phase]["complete"] = not rb[phase]["unreadFields"] and not failed_fields
    for k,v in rb["before"]["values"].items():
        if k in rb["after"]["values"] and v != rb["after"]["values"][k]:
            rb["changes"].append({"field":k,"before":v,"after":rb["after"]["values"][k]})
    for phase in ("before","after"):
        local,values = life[phase], rb[phase]["values"]
        try:
            if not local["available"]:
                raise StepFailed("local lifecycle unavailable")
            native = local["native"]
            if any(native[k] != life["before"]["native"][k] for k in native_keys) or local["logs"] != life["before"]["logs"] or local["scope"] != life["before"]["scope"]:
                issue(life["reasons"],"lifecycle-drift",phase=phase)
            if (native["head"] != values["activeHead"] or native["tail"] != values["activeTail"]
                    or any(native["regions"][i] != values[f"bucket{i}"] for i in range(64))):
                issue(life["reasons"],"lifecycle-raw-root-bucket-mismatch",phase=phase)
        except (KeyError,TypeError,ValueError,StepFailed) as error:
            issue(life["reasons"],"lifecycle-join-unavailable",phase=phase,error=str(error))
        try:
            for parent_phase in ("before","after"):
                other = parent["lifecycle"][parent_phase]
                if (not local["available"] or not other["available"] or any(local["native"][k] != other["native"][k] for k in native_keys)
                        or local["logs"] != other["logs"] or local["scope"] != other["scope"]):
                    raise StepFailed("local/parent native or log scope mismatch")
        except (KeyError,TypeError,ValueError,StepFailed) as error:
            issue(receipt["parentReasons"],"local-parent-lifecycle-mismatch",phase=phase,error=str(error))
        for k in raw_fields:
            if k not in parent_expected or values.get(k) != parent_expected[k]:
                issue(receipt["parentReasons"],"fresh-parent-raw-mismatch",phase=phase,field=k)
        # Validate both supplied list paths against the fresh roots/links; no new graph is chased.
        try:
            for lane in ("active","deferred"):
                nodes = [n for n in raw_nodes if n["list"]==lane]
                if values[lane+"Head"] != (nodes[0]["address"] if nodes else 0) or values[lane+"Tail"] != (nodes[-1]["address"] if nodes else 0):
                    raise StepFailed("raw root/tail does not match supplied list")
                for i,node in enumerate(nodes):
                    handle = values[node["prefix"]+"nextHandle"]
                    nxt = 0
                    if handle:
                        masked = handle & 0x7fffffff; bucket = values[f"bucket{masked>>25}"]
                        nxt = bucket | (masked & 0x1ffffff)
                        if bucket in (0,0xffffffffffffffff) or bucket & 0x1ffffff or not span(nxt,1) or nxt%8:
                            raise StepFailed("invalid raw handle bucket")
                    if nxt != (nodes[i+1]["address"] if i+1<len(nodes) else 0):
                        raise StepFailed("fresh raw link differs from supplied chain")
        except (KeyError,TypeError,ValueError,StepFailed) as error:
            issue(receipt["rawReasons"],"raw-chain-unavailable",phase=phase,error=str(error))
    life["available"] = all(life[p]["available"] for p in ("before","after"))
    life["stable"] = life["available"] and not life["reasons"]
    life["status"] = "stable" if life["stable"] else "partial" if any(life[p]["available"] for p in ("before","after")) else "unavailable"
    occurrences = {}
    for d in receipt["definitions"]:
        after = words(d["prefix"]+"headerWord",11,4,"after")
        if after is not None: d["headerAfterHex"] = after.hex()
        for row in d["records"]:
            for phase in ("before","after"):
                data = words(row["prefix"],8,8,phase)
                if data is not None: row[phase+"Hex"] = data.hex()
            row["fullBytesComplete"] = stable([row["prefix"]+str(i) for i in range(8)])
            if "beforeHex" in row:
                data = bytes.fromhex(row["beforeHex"])
                row.update(recordId=int.from_bytes(data[30:32],"little"),objectId=int.from_bytes(data[:4],"little"),
                           mode=data[28],positionMode=data[29],stage=data[48])
                row["lookupQueryId"] = 0x70 if row["recordId"] == 0 else row["recordId"]
                row["recordIdLimitations"] = (["zero-query-remaps-to-0x70"] if row["recordId"] == 0 else
                    ["high-bit-cache-signedness-unqualified"] if row["recordId"] > 0x7FFF else [])
                occurrences.setdefault(row["recordId"],[]).append({"tableIndex":d["tableIndex"],"controller":d["pointer"],
                    "recordIndex":row["recordIndex"],"pointer":row["pointer"],"fullBytesComplete":row["fullBytesComplete"]})
                if row["fullBytesComplete"]: row["sha256"] = hashlib.sha256(data).hexdigest()
        labels = [k for k in definition_labels if k.startswith(d["prefix"])]
        d["fullBytesComplete"] = bool(not d["reasons"] and "headerFields" in d and
            len(d["records"])==d["headerFields"]["spawnCount"] and stable(labels))
    for record_id,refs in occurrences.items():
        if len(refs)>1:
            receipt["idConflicts"].append({"recordId":record_id,"occurrences":refs,
                "samePhysicalRecord":len({r["pointer"] for r in refs})==1})
    definitions = receipt["definitions"]
    for i,d in enumerate(definitions):
        for other in definitions[:i]:
            if d["pointer"]==other["pointer"] or (d.get("records") and other.get("records") and
                    max(d["spawnArray"],other["spawnArray"]) < min(d["regionArray"],other["regionArray"])):
                receipt["definitionAliases"].append({"tableIndices":[other["tableIndex"],d["tableIndex"]],
                    "sameController":d["pointer"]==other["pointer"],"sameArray":d.get("spawnArray")==other.get("spawnArray")})
    for node in raw_nodes:
        prefix = node["prefix"]
        ref = {"list":node["list"],"actor":node["address"],"controller":get(prefix+"controller"),
            "record":get(prefix+"spawnRecord"),"recordId":get(prefix+"recordId"),"matches":[],"reasons":[]}
        ref["recordIdReadPointer"] = node["parent"].get("spawnRecord")
        ref["recordIdReadValue"] = ref["recordId"]
        ref["recordIdReadSuccess"] = (ref["recordId"] is not None and ref["record"] == ref["recordIdReadPointer"])
        if not ref["recordIdReadSuccess"]:
            ref["recordId"] = None
        receipt["references"].append(ref)
        if ref["record"]==0:
            ref["status"] = "null-record"
            if ref["controller"] != 0: issue(ref["reasons"],"controller-with-null-record")
        elif ref["record"] is None:
            issue(ref["reasons"],"record-pointer-unavailable")
        else:
            for d in definitions:
                if d["pointer"]==ref["controller"]:
                    for row in d["records"]:
                        if row["pointer"]==ref["record"]:
                            ref["matches"].append({"tableIndex":d["tableIndex"],"recordIndex":row["recordIndex"]})
                            if not d["fullBytesComplete"] or ref["recordId"]!=row.get("recordId"):
                                issue(ref["reasons"],"record-id-or-bytes-unavailable")
            if len(ref["matches"])!=1:
                issue(ref["reasons"],"record-reference-unresolved",matches=len(ref["matches"]))
    # A prior table/geometry receipt is a separate sample: explicitly join its identity projection.
    try:
        cause = census["causeContext"]
        if not all(cause.get(k) is True for k in ("complete","tableStable","lifecycleStable")):
            raise StepFailed("parent cause scope unavailable")
        for native, logs in ((census["before"], census["logsBefore"]), (census["after"], census["logsAfter"]),
                             (cause["nativeBefore"], cause["logsBefore"]), (cause["nativeAfter"], cause["logsAfter"])):
            if (not life["before"]["available"] or any(native[k] != life["before"]["native"][k] for k in native_keys)
                    or any(logs[k] != life["before"]["logs"][k] for k in ("arrival", "lifecycle", "arrivalAfterLifecycle"))):
                raise StepFailed("parent census/cause native lifecycle mismatch")
        if any(cause[p]["controllerCount"]!=receipt["declaredTableCount"] for p in ("before","after")) or len(cause["controllers"])!=len(receipt["table"]):
            raise StepFailed("parent table count mismatch")
        for entry in receipt["table"]:
            old = cause["controllers"][entry["tableIndex"]]
            if any(old.get(k)!=entry[k] for k in ("tableIndex","key","flags","pointer")):
                raise StepFailed("parent table entry mismatch")
        for d in definitions:
            old = cause["controllers"][d["tableIndex"]]
            if (any(old.get(k)!=d.get(k) for k in ("groupKey","header","spawnArray","regionArray"))
                    or any(old.get("headerFields",{}).get(k)!=d.get("headerFields",{}).get(k) for k in ("type","headerId","spawnCount","regionCount"))):
                raise StepFailed("parent definition mismatch")
    except (KeyError,IndexError,TypeError,ValueError,AttributeError,StepFailed) as error:
        issue(receipt["parentReasons"],"parent-table-join-unavailable",error=str(error))
    receipt["tableInventoryComplete"] = bool(type(receipt["declaredTableCount"]) is int and
        0<=receipt["declaredTableCount"]<=64 and len(receipt["table"])==receipt["declaredTableCount"] and stable(table_labels) and life["stable"])
    receipt["ordinaryRecordBytesComplete"] = bool(receipt["tableInventoryComplete"] and not receipt["tableReasons"]
        and len(definitions)==len(receipt["table"]) and all(d["fullBytesComplete"] for d in definitions))
    receipt["listedReferenceJoinComplete"] = bool(not receipt["parentReasons"] and not receipt["rawReasons"] and life["stable"]
        and stable(raw_fields) and all(not r["reasons"] for r in receipt["references"]))
    receipt["sampledSupportedCoverageComplete"] = receipt["ordinaryRecordBytesComplete"] and receipt["listedReferenceJoinComplete"]
    if receipt["idConflicts"] or receipt["definitionAliases"]:
        receipt["noIdConflictInSampledScope"] = False
    elif receipt["sampledSupportedCoverageComplete"]:
        receipt["noIdConflictInSampledScope"] = True
    receipt["endedMonotonic"] = time.monotonic()
    return receipt


def native_enemy_census_snapshot(ctx: Context, index: int, timeout: float = 30, *, resource_bindings: bool = False, controller_id_coverage: bool = False, secondary_bindings: bool = False) -> dict:
    """Checked, bounded native-list/cache observation independent of update-hook membership."""
    if type(controller_id_coverage) is not bool:
        raise StepFailed("native census controllerIdCoverage must be a boolean")
    if type(secondary_bindings) is not bool:
        raise StepFailed("native census secondaryBindings must be a boolean")
    started = time.monotonic()
    deadline = started + timeout
    pid = ctx.inst(index).pid
    out = {"instance": index, "pid": pid, "complete": False, "listComplete": False,
           "classificationComplete": False, "errors": [], "provenance": [], "provenanceErrors": [],
           "source": "kh2ctl entities candidates + independently checked native links/identity/HP; no game writes"}
    base = 0
    def remaining():
        left = deadline - time.monotonic()
        if left <= 0:
            raise StepFailed("native census read deadline exceeded")
        return left
    def valid_pointer(value):
        return 0x10000 <= value < 0x0000800000000000
    def read(fields, result=None, *, stage="native-field-batch", failure_evidence=None, require_identity=False):
        """Batched checked absolute reads through peek; unsupported below-module addresses fail."""
        # An optional evidence dictionary retains completed fields if a later
        # checked read times out or fails. It does not make the read complete.
        if result is None:
            result = {}
        items = list(fields.items())
        for start in range(0, len(items), 128):
            batch = items[start:start + 128]
            work = None
            if failure_evidence is not None:
                work = {"stage": stage, "batchStart": start, "fieldCount": len(batch),
                        "peekAttempted": False, "complete": False}
                failure_evidence.setdefault("readWork", []).append(work)
            for label, (address, _) in batch:
                if not valid_pointer(address) or address < base:
                    raise StepFailed(f"{label}: invalid/below-module pointer 0x{address:X}; not read")
            specs = [f"0x{address - base:X}:{kind}" for _, (address, kind) in batch]
            argv = [str(KH2CTL), "peek", "--rva", ",".join(specs), "--pid", str(pid)]
            phase = "deadline-check"
            try:
                budget = remaining()
                phase = "kh2ctl-call"  # subprocess creation/communication OR the tool's reported failure
                if work is not None:
                    work["peekAttempted"] = True
                response = kh2ctl("peek", "--rva", ",".join(specs), pid=pid, timeout=budget)
                if require_identity:
                    phase = "peek-response-identity"
                    if work is not None:
                        work["responseIdentity"] = {"processId": response.get("processId"), "moduleBase": response.get("moduleBase")}
                    observed_base = response.get("moduleBase")
                    if isinstance(observed_base, str):
                        observed_base = int(observed_base, 0)
                    if (type(response.get("processId")) is not int or response["processId"] != pid
                            or type(observed_base) is not int or observed_base != base):
                        raise StepFailed("secondary binding peek process/module identity mismatch or missing")
                phase = "peek-response-decode"
                sample = response["samples"][0]
                for label, (address, kind) in batch:
                    value = sample[f"0x{address - base:X}"]
                    result[label] = int(value, 0) if kind == "u64" and isinstance(value, str) else value
                if work is not None:
                    work["complete"] = True
            except Exception as error:
                # Keep invocation failures distinct from native read/tool JSON
                # failures and Python decoding. No retry or completeness change.
                sink = out if failure_evidence is None else failure_evidence
                failures = sink.setdefault("readFailures", [])
                if len(failures) < 8:
                    failures.append({"stage": stage, "operation": "kh2ctl peek", "phase": phase,
                                     "argv": argv, "batchStart": start, "fieldLabels": [label for label, _ in batch],
                                     "completedFieldCount": len(result), "exceptionType": type(error).__name__,
                                     "winerror": getattr(error, "winerror", None), "errno": getattr(error, "errno", None),
                                     "filename": str(error.filename) if getattr(error, "filename", None) is not None else None,
                                     "traceback": traceback.format_exc(limit=8)[-8192:]})
                else:
                    sink["readFailuresOmitted"] = sink.get("readFailuresOmitted", 0) + 1
                raise
        return result
    def header(result=None, *, stage="native-field-batch", failure_evidence=None, require_identity=False):
        fields = {"head": (base + 0x2A171C8, "u64"), "tail": (base + 0x2A171D0, "u64"),
                  "cachePointer": (base + 0x2AE6680, "u64"), "cacheCounter": (base + 0x2AE6688, "u32"),
                  "inField": (base + 0x9BA8D0, "u8"), "frozen": (base + 0x2A171E8, "u32"),
                  "eventState": (base + 0xB65210, "i32"), "eventContext": (base + 0x2A11478, "u64"),
                  "openMenu": (base + 0x7435D0, "u8")}
        fields.update({key: (base + rva, kind) for key, rva, kind in (
            ("world", 0x717008, "u8"), ("room", 0x717009, "u8"), ("door", 0x71700A, "u8"),
            ("map", 0x71700C, "u16"), ("btl", 0x71700E, "u16"), ("evt", 0x717010, "u16"))})
        fields.update({f"region{n}": (base + 0x2B0D720 + n * 8, "u64") for n in range(64)})
        data = read(fields, result, stage=stage, failure_evidence=failure_evidence,
                    **({"require_identity": True} if require_identity else {}))
        data["location"] = [data[k] for k in ("world", "room", "door", "map", "btl", "evt")]
        data["regions"] = [data.pop(f"region{n}") for n in range(64)]
        return data
    def logs():
        path = LOGS / f"kh2coop_inject_{pid}.log"
        text = path.read_text(errors="replace") if path.exists() else ""
        arrivals = list(re.finditer(ARRIVAL_PATTERN, text))
        hashes = list(HASH_PATTERN.finditer(text))
        result = {"arrival": arrivals[-1].groupdict() if arrivals else None, "hash": None, "nativeRows": []}
        if hashes:
            match = hashes[-1]
            result["hash"] = match.groupdict()
            result["hashAfterLatestArrival"] = bool(arrivals and match.start() > arrivals[-1].end())
            result["nativeRows"] = [r.groupdict() for r in NATIVE_HASH_PATTERN.finditer(text, match.end())
                                    if r["epoch"] == match["epoch"] and r["frame"] == match["frame"]]
        lifecycle = list(re.finditer(r"(?:\[warp\] (?:load complete|client issued|client queued)[^\r\n]*|Warp: [0-9A-Fa-f]{2}/[0-9A-Fa-f]{2} ->[^\r\n]*)", text))
        result["lifecycle"] = {"offset": lifecycle[-1].start(), "line": lifecycle[-1].group()} if lifecycle else None
        result["arrivalAfterLifecycle"] = bool(arrivals and lifecycle and arrivals[-1].start() > lifecycle[-1].end())
        result["bindingLines"] = [line for line in text.splitlines() if "[enemysync]" in line
                                   and any(word in line for word in ("spawn", "manifest", "matched", "bind"))][-100:]
        return result
    def cache_sample(pointer):
        buckets = [base + 0x2AE5E60 + n * 0x208 for n in range(4)]
        if pointer not in buckets:
            raise StepFailed(f"cache pointer 0x{pointer:X} is not one of four rooted inline buckets")
        values = read({"roomTag": (pointer, "i32"), "age": (pointer + 4, "i32"),
                       **{f"word{n}": (pointer + 8 + n * 8, "u64") for n in range(64)}})
        ids = [(values[f"word{n}"] >> (16 * j)) & 0xFFFF for n in range(64) for j in range(4)]
        return {"bucketIndex": buckets.index(pointer), "roomTag": values["roomTag"], "age": values["age"],
                "ids": ids, "nonzeroIds": [value for value in ids if value != 0],
                "layout": "root2AE5E60 + index*208; room-only tag; all256 raw u16 IDs, zero slots retained"}
    try:
        probe = kh2ctl("peek", "--rva", "0x2A171C8:u64", pid=pid, timeout=remaining())
        base = int(probe["moduleBase"], 0)
        out["moduleBase"] = f"0x{base:X}"
        out["before"] = before = header()
        out["logsBefore"] = logs()
        entities = kh2ctl("entities", pid=pid, timeout=remaining())
        out["entities"] = entities
        candidates = entities.get("actors", [])
        addresses = [int(actor["address"], 16) for actor in candidates]
        if len(addresses) >= 256 or len(set(addresses)) != len(addresses):
            raise StepFailed("entities candidate list hit256 cap or repeated an actor; not a complete traversal")
        if not addresses or not before["head"]:
            raise StepFailed("no live native root/candidates; empty entities alone is not absence proof")
        actor_fields = {}
        for n, address in enumerate(addresses):
            actor_fields.update({f"{n}:{key}": (address + offset, kind) for key, offset, kind in (
                ("nextHandle", 0xA90, "u32"), ("objectEntry", 0x918, "u64"), ("status", 0x5C0, "u64"),
                ("controller", 0x9E8, "u64"), ("spawnRecord", 0x9F0, "u64"),
                ("flags9B8", 0x9B8, "u32"), ("state9C0", 0x9C0, "u64"), ("flags120", 0x120, "u32"))})
        values = read(actor_fields)
        nodes = []
        for n, actor in enumerate(candidates):
            node = {"actor": actor, "address": addresses[n],
                    **{key.split(":", 1)[1]: value for key, value in values.items() if key.startswith(f"{n}:")}}
            handle = node["nextHandle"]
            if handle:
                masked = handle & 0x7FFFFFFF
                bucket = masked >> 25
                region = before["regions"][bucket] if bucket < 64 else 0
                if region == 0xFFFFFFFFFFFFFFFF or region & 0x1FFFFFF or not valid_pointer(region | (masked & 0x1FFFFFF)):
                    raise StepFailed(f"actor0x{addresses[n]:X}: nonzero link has invalid region/bucket")
                node["nextAddress"] = region | (masked & 0x1FFFFFF)
                node["handleBucket"] = bucket
            else:
                node["nextAddress"] = 0
            nodes.append(node)
        out["nodes"] = nodes
        if before["head"] != addresses[0] or before["tail"] != addresses[-1]:
            raise StepFailed("checked HEAD/TAIL do not bound the entities candidate order")
        if any(node["nextAddress"] != (addresses[n + 1] if n + 1 < len(nodes) else 0)
               for n, node in enumerate(nodes)):
            raise StepFailed("checked native chain differs from entities order, cycles or lacks terminal handle0")
        out["terminalHandleZero"] = nodes[-1]["nextHandle"] == 0
        descriptors = {}
        for n, node in enumerate(nodes):
            pointer = node["objectEntry"]
            if not base < pointer < base + 0x3000000:
                raise StepFailed(f"actor0x{node['address']:X}: object descriptor unavailable; classification incomplete")
            descriptors[f"{n}:objectId"] = (pointer, "u32")
            descriptors[f"{n}:objectType"] = (pointer + 4, "u8")
            descriptors[f"{n}:namePrefix"] = (pointer + 8, "u16")
        descriptor_values = read(descriptors, stage="actor-descriptors")
        hp_fields = {}
        for n, node in enumerate(nodes):
            node["objectId"] = descriptor_values[f"{n}:objectId"]
            node["objectType"] = descriptor_values[f"{n}:objectType"]
            node["namePrefix"] = descriptor_values[f"{n}:namePrefix"]
            node["nativeEnemyType"] = node["objectType"] in (3, 4)
            if node["nativeEnemyType"]:
                if not valid_pointer(node["status"]):
                    raise StepFailed(f"native enemy0x{node['address']:X}: status pointer unavailable, HP unknown")
                hp_fields[f"{n}:hp"] = (node["status"], "i32")
                hp_fields[f"{n}:maxHp"] = (node["status"] + 4, "i32")
        hp_values = read(hp_fields)
        for n, node in enumerate(nodes):
            if node["nativeEnemyType"]:
                node["hp"] = hp_values[f"{n}:hp"]
                node["maxHp"] = hp_values[f"{n}:maxHp"]
                node["combatEligible"] = node["namePrefix"] != 0x5F46
            else:
                node["combatEligible"] = False
        out["classificationComplete"] = True
        out["nativeEnemyRows"] = [node for node in nodes if node["nativeEnemyType"]]
        out["livingCombatRows"] = [node for node in nodes if node["combatEligible"] and node["hp"] > 0]
        out["cacheFirst"] = cache_sample(before["cachePointer"])
        for node in out["nativeEnemyRows"]:
            record = {"actor": node["address"], "controller": node["controller"], "pointer": node["spawnRecord"]}
            out["provenance"].append(record)
            if node["controller"] == 0 or node["spawnRecord"] == 0:
                record["status"] = "null native metadata; no dereference or invented record identity"
                continue
            try:
                controller = read({"groupKey": (node["controller"], "u32"),
                                   "header": (node["controller"] + 8, "u64"),
                                   "spawnArray": (node["controller"] + 0x30, "u64"),
                                   "regionArray": (node["controller"] + 0x38, "u64")})
                record["controllerFields"] = controller
                count = read({"spawnCount": (controller["header"] + 4, "u16")})["spawnCount"]
                record["spawnCount"] = count
                start = controller["spawnArray"]
                if (controller["header"] == 0 or start != controller["header"] + 0x2C
                        or controller["regionArray"] != start + count * 0x40
                        or not 0 < count <= 256 or not start <= node["spawnRecord"] < start + count * 0x40
                        or (node["spawnRecord"] - start) % 0x40):
                    raise StepFailed("spawn record is outside validated controller/header span (diagnostic count cap256)")
                words = read({str(n): (node["spawnRecord"] + n * 8, "u64") for n in range(8)})
                raw = b"".join(words[str(n)].to_bytes(8, "little") for n in range(8))
                record.update(status="shape-validated static provenance; ordinary table identity pending", hex=raw.hex(), objectId=struct.unpack_from("<I", raw)[0],
                              position=list(struct.unpack_from("<fff", raw, 4)), mode=raw[0x1C],
                              positionMode=raw[0x1D], recordId=struct.unpack_from("<H", raw, 0x1E)[0],
                              delay=struct.unpack_from("<H", raw, 0x2A)[0], stage=raw[0x30],
                              recordIndex=(node["spawnRecord"] - start) // 0x40)
                record["cacheSlots"] = [i for i, value in enumerate(out["cacheFirst"]["ids"])
                                        if record["recordId"] != 0 and value == record["recordId"]]
            except (StepFailed, subprocess.TimeoutExpired) as error:
                record.update(status="incomplete", error=str(error))
                out["provenanceErrors"].append(f"spawn provenance actor0x{node['address']:X}: {error}")
        out["cacheSecond"] = cache_sample(before["cachePointer"])
        identity_fields = {key: field for key, field in actor_fields.items()
                           if key.split(":", 1)[1] in ("nextHandle", "objectEntry", "status", "controller", "spawnRecord")}
        recheck = {"complete": False, "before": {key: values[key] for key in identity_fields},
                   "after": {}, "changes": []}
        out["actorIdentityRecheck"] = recheck
        out["actorIdentityStable"] = False
        try:
            read(identity_fields, recheck["after"])
            recheck["complete"] = True
        finally:
            recheck["changes"] = [
                {"actorIndex": int(key.split(":", 1)[0]),
                 "actor": addresses[int(key.split(":", 1)[0])],
                 "field": key.split(":", 1)[1], "fieldAddress": identity_fields[key][0],
                 "before": values[key], "after": value}
                for key, value in recheck["after"].items() if value != values[key]]
            recheck["unreadFields"] = [key for key in identity_fields if key not in recheck["after"]]
        out["actorIdentityStable"] = recheck["complete"] and not recheck["changes"]
        out["after"] = after = header()
        out["logsAfter"] = logs()
        stable_keys = ("head", "tail", "location", "regions", "inField", "frozen", "eventState", "eventContext", "openMenu")
        out["nativeHeaderStable"] = all(before[k] == after[k] for k in stable_keys)
        arrival_before, arrival_after = out["logsBefore"]["arrival"], out["logsAfter"]["arrival"]
        arrival_location = ([int(arrival_after[k], 16 if k in ("world", "room") else 10)
                             for k in ("world", "room", "door", "map", "btl", "evt")] if arrival_after else None)
        out["epochStable"] = bool(arrival_before and arrival_after and arrival_before == arrival_after
                                   and arrival_location == after["location"])
        out["safeGameplay"] = all(h["inField"] != 0 and h["frozen"] == 0 and h["eventState"] == 0
                                   and h["eventContext"] == 0 and h["openMenu"] == 255 for h in (before, after))
        out["lifecycleStable"] = bool(out["logsBefore"]["lifecycle"] == out["logsAfter"]["lifecycle"]
                                      and out["logsBefore"]["arrivalAfterLifecycle"]
                                      and out["logsAfter"]["arrivalAfterLifecycle"])
        out["listComplete"] = bool(out["terminalHandleZero"] and out["actorIdentityStable"]
                                    and out["nativeHeaderStable"] and out["epochStable"]
                                    and out["safeGameplay"] and out["lifecycleStable"])
        out["cacheStable"] = (before["cachePointer"] == after["cachePointer"]
                               and before["cacheCounter"] == after["cacheCounter"]
                               and out["cacheFirst"] == out["cacheSecond"])
        if not out["listComplete"]:
            out["errors"].append("native traversal/identity/location/load epoch changed during read; no complete absence proof")
        if not out["cacheStable"]:
            out["errors"].append("native cache changed during read; raw samples preserved")
        published = out["logsAfter"]
        native_addresses = {row["address"] for row in out["livingCombatRows"]}
        comparison = {"nativeLivingCount": len(native_addresses), "comparisonValid": False, "reasons": [],
                      "caution": "reads and periodic publisher are not atomic; repeat stable snapshots before inference"}
        out["comparison"] = comparison
        h = published["hash"]
        if not out["listComplete"]:
            comparison["reasons"].append("checked gameplay census/lifecycle is incomplete")
        if not h or not published.get("hashAfterLatestArrival"):
            comparison["reasons"].append("no hash after latest completed arrival")
        if h:
            hash_location = [int(h[k], 16 if k in ("world", "room") else 10)
                             for k in ("world", "room", "door", "map", "btl", "evt")]
            if (not arrival_after or h["epoch"] != arrival_after["epoch"]
                    or hash_location != before["location"] or hash_location != after["location"]):
                comparison["reasons"].append("published epoch/full location is not bound to checked census")
            rows = [{k: int(v, 16 if k == "actor" else 10) for k, v in row.items()}
                    for row in published["nativeRows"]]
            live = sorted([[r[k] for k in ("netId", "objectId", "hp")] for r in rows if r["hp"] > 0])
            encoded = struct.pack("<II", 0x3145484B, len(live)) + b"".join(struct.pack("<HIi", *r) for r in live)
            recomputed = 2166136261
            for byte in encoded:
                recomputed = ((recomputed ^ byte) * 16777619) & 0xFFFFFFFF
            comparison["recomputedEnemiesHash"] = recomputed
            if len(rows) != int(h["observed"]) or len(live) != int(h["count"]) or recomputed != int(h["enemies"], 16):
                comparison["reasons"].append("published native rows incomplete or canonical hash mismatch")
            comparison["comparisonValid"] = not comparison["reasons"]
            if comparison["comparisonValid"]:
                logged_addresses = {r["actor"] for r in rows if r["hp"] > 0}
                comparison.update(publishedCount=int(h["count"]),
                                  nativeLivingAbsentFromLatestHash=sorted(native_addresses - logged_addresses),
                                  publishedRowsAbsentFromNativeList=sorted(logged_addresses - set(addresses)))
        out["complete"] = out["listComplete"] and out["classificationComplete"] and out["cacheStable"] and not out["errors"]
    except Exception as error:
        out["errors"].append(f"{type(error).__name__}: {error}")
    # Causal context is separate: a truncated controller table must not erase
    # a completed checked native traversal/cache snapshot or imply native absence.
    causes = {"complete": False, "controllers": [], "limits": []}
    out["causeContext"] = causes
    if out.get("listComplete") and time.monotonic() < deadline:
        try:
            causes["nativeBefore"] = cause_before = header()
            causes["logsBefore"] = cause_logs_before = logs()
            roots = read({"controllerCount": (base + 0x2A10418, "i32"),
                          "activationActor": (base + 0x2A10420, "u64"),
                          "playerActor": (base + 0x2A105D0, "u64")})
            causes["before"] = roots
            count = roots["controllerCount"]
            if not 0 <= count <= 64:
                causes["limits"].append(f"controller count{count} exceeds diagnostic bound0..64;64 is not a proven native limit")
            table_count = min(64, max(0, count))
            fields = {}
            for n in range(table_count):
                entry = base + 0x2A10010 + n * 16
                fields.update({f"{n}:key": (entry, "u32"), f"{n}:flags": (entry + 4, "u32"),
                               f"{n}:pointer": (entry + 8, "u64")})
            entries = read(fields)
            controller_fields = {}
            for n in range(table_count):
                entry = {key: entries[f"{n}:{key}"] for key in ("key", "flags", "pointer")}
                entry["tableIndex"] = n
                causes["controllers"].append(entry)
                if entry["flags"] & 1:
                    entry["status"] = "alternate script-pointer entry; not interpreted as controller"
                elif not valid_pointer(entry["pointer"]):
                    entry["status"] = "invalid/null controller pointer"
                    causes["limits"].append(f"entry{n}: no valid ordinary controller")
                else:
                    pointer = entry["pointer"]
                    controller_fields.update({f"{n}:{key}": (pointer + offset, kind) for key, offset, kind in (
                        ("groupKey", 0, "u32"), ("controllerFlags", 4, "u32"), ("header", 8, "u64"),
                        ("regionHead", 0x10, "u64"), ("regionTail", 0x18, "u64"),
                        ("cooldown", 0x20, "f32"), ("currentCount", 0x24, "i32"),
                        ("initialCount", 0x28, "i32"), ("stage", 0x2C, "u8"),
                        ("spawnArray", 0x30, "u64"), ("regionArray", 0x38, "u64"))})
                    entry["status"] = "ordinary controller candidate"
            controller_values = read(controller_fields)
            header_fields = {}
            for entry in causes["controllers"]:
                n = entry["tableIndex"]
                if entry["status"] != "ordinary controller candidate":
                    continue
                entry.update({key.split(":", 1)[1]: value for key, value in controller_values.items()
                              if key.startswith(f"{n}:")})
                pointer = entry["header"]
                if not valid_pointer(pointer):
                    causes["limits"].append(f"entry{n}: invalid ordinary controller header")
                    entry["status"] = "unavailable header"
                    continue
                header_fields.update({f"{n}:{key}": (pointer + offset, kind) for key, offset, kind in (
                    ("type", 0, "u8"), ("flags", 1, "u8"), ("headerId", 2, "u16"),
                    ("spawnCount", 4, "u16"), ("regionCount", 6, "u16"),
                    ("activationMarker", 0xE, "u8"), ("removalDelay", 0x20, "u16"),
                    ("threshold", 0x22, "u8"), ("loopEndStage", 0x23, "u8"), ("restartStage", 0x24, "u8"))})
            headers = read(header_fields)
            for entry in causes["controllers"]:
                n = entry["tableIndex"]
                if entry["status"] != "ordinary controller candidate":
                    continue
                entry["headerFields"] = h = {key.split(":", 1)[1]: value for key, value in headers.items()
                                             if key.startswith(f"{n}:")}
                entry["layoutValidated"] = (entry["spawnArray"] == entry["header"] + 0x2C
                    and entry["regionArray"] == entry["spawnArray"] + h["spawnCount"] * 0x40)
                entry["tableKeyValidated"] = entry["groupKey"] == entry["key"]
                if not entry["tableKeyValidated"]:
                    causes["limits"].append(f"entry{n}: ordinary controller key does not match table identity")
                entry["status"] = "read" if entry["layoutValidated"] else "header/array layout inconsistent"
                if not entry["layoutValidated"]:
                    causes["limits"].append(f"entry{n}: native controller/header layout inconsistent")
            # Capture the native accessor's float4, including attachment selection.
            active = {node["address"]: node for node in out.get("nodes", [])}
            causes["activationActors"] = {}
            # Additive receipts for the four existing reads below. Raw bytes are
            # little-endian memory order; no float conversion defines stability.
            for name in ("positionSelectorReadback", "positionReadback"):
                causes[name] = {"schemaVersion": 1, "encoding": "little-endian memory bytes, hex",
                                "aliasing": "Same-address role labels share CLI address-keyed returned values; no independent per-role timestamps",
                                "scope": [], "fields": {},
                                "before": {"attempted": False, "complete": False, "bytesComplete": False, "bytes": {}, "unreadFields": []},
                                "after": {"attempted": False, "complete": False, "bytesComplete": False, "bytes": {}, "unreadFields": []},
                                "comparisonComplete": False, "changes": []}

            def geometry_readback(fields, name, phase, stage):
                receipt = causes[name]
                receipt["scope"] = list(dict.fromkeys(key.split(":", 1)[0] for key in fields))
                receipt["fields"] = {key: {"address": address, "type": kind}
                                     for key, (address, kind) in fields.items()}
                for pending in (receipt["before"], receipt["after"]):
                    if not pending["attempted"]:
                        pending["unreadFields"] = list(fields)
                sample, values = receipt[phase], {}
                sample["attempted"] = True
                try:
                    read(fields, values, stage=stage)
                    sample["complete"] = True
                    return values
                except Exception as error:
                    sample["error"] = f"{type(error).__name__}: {error}"[:1024]
                    raise
                finally:
                    sample["bytes"] = {}
                    for key, value in values.items():
                        try:
                            sample["bytes"][key] = value.to_bytes(4 if fields[key][1] == "u32" else 8, "little").hex()
                        except (AttributeError, TypeError, OverflowError) as error:
                            # Diagnostics must not replace the original read or
                            # comparison outcome on malformed decoded values.
                            sample.setdefault("encodingErrors", {})[key] = type(error).__name__
                    sample["unreadFields"] = [key for key in fields if key not in values]
                    sample["bytesComplete"] = len(sample["bytes"]) == len(fields)
                    before_bytes, after_bytes = receipt["before"]["bytes"], receipt["after"]["bytes"]
                    receipt["comparisonComplete"] = bool(fields) and all(
                        receipt[p]["complete"] and receipt[p]["bytesComplete"] for p in ("before", "after"))
                    receipt["changes"] = [{"field": key, "address": fields[key][0],
                                           "beforeHex": before_bytes[key], "afterHex": value}
                                          for key, value in after_bytes.items()
                                          if key in before_bytes and value != before_bytes[key]]

            position_fields, selector_fields = {}, {}
            for key in ("activationActor", "playerActor"):
                address = roots[key]
                record = {"address": address, "inCheckedNativeList": address in active}
                causes["activationActors"][key] = record
                if address in active:
                    record["actor"] = active[address]["actor"]
                    selector_fields[f"{key}:parentHandle"] = (address + 0x6A0, "u32")
                    selector_fields[f"{key}:objectEntry"] = (address + 0x918, "u64")
                    selector_fields[f"{key}:status"] = (address + 0x5C0, "u64")
                else:
                    record["positionStatus"] = "not dereferenced: no checked active-list identity"
                    causes["limits"].append(f"{key}: no checked active-list position identity")
            selectors = geometry_readback(selector_fields, "positionSelectorReadback", "before", "geometry-selectors-before")
            for key, record in causes["activationActors"].items():
                if not record["inCheckedNativeList"]:
                    continue
                address = record["address"]
                if (selectors[f"{key}:objectEntry"] != active[address]["objectEntry"]
                        or selectors[f"{key}:status"] != active[address]["status"]):
                    raise StepFailed("activation actor identity changed before native position read")
                handle = selectors[f"{key}:parentHandle"]
                resolved = 0
                if handle:
                    masked = handle & 0x7FFFFFFF
                    region = cause_before["regions"][masked >> 25]
                    resolved = region | (masked & 0x1FFFFFF)
                    if region == 0xFFFFFFFFFFFFFFFF or region & 0x1FFFFFF or not valid_pointer(resolved):
                        raise StepFailed("activation parent handle resolution unavailable; cannot select position")
                offset = 0x70 if resolved else 0x670
                record.update(parentHandle=handle, resolvedParent=resolved, selectedOffset=offset,
                              accessor="native3B5B40/3DA520; generic encoded-handle resolution")
                position_fields.update({f"{key}:{n}": (address + offset + n * 8, "u64") for n in range(2)})
            positions = geometry_readback(position_fields, "positionReadback", "before", "geometry-positions-before")
            for key, record in causes["activationActors"].items():
                if record["inCheckedNativeList"]:
                    raw = b"".join(positions[f"{key}:{n}"].to_bytes(8, "little") for n in range(2))
                    record["position4"] = list(struct.unpack("<4f", raw))
                    record["position"] = record["position4"][:3]
                    record["positionHex"] = raw.hex()
            for entry in sorted(causes["controllers"], key=lambda item: item.get("headerFields", {}).get("headerId") != 30):
                if entry.get("layoutValidated") and entry.get("tableKeyValidated") and entry.get("headerFields", {}).get("type") == 2:
                    entry["geometry"] = capture_native_regions(entry, base, read, cause_before["regions"])
                    if not entry["geometry"]["complete"]:
                        causes["limits"].append(f"controller{entry['tableIndex']}: incomplete native region geometry")
            causes["positionSelectorsStable"] = geometry_readback(
                selector_fields, "positionSelectorReadback", "after", "geometry-selectors-after") == selectors
            causes["positionsStableDuringGeometry"] = geometry_readback(
                position_fields, "positionReadback", "after", "geometry-positions-after") == positions
            if not causes["positionSelectorsStable"]:
                causes["limits"].append("activation actor identity/accessor selector changed during geometry capture")
            causes["nativeAfter"] = cause_after = header()
            causes["logsAfter"] = cause_logs_after = logs()
            causes["lifecycleStable"] = (all(cause_before[k] == cause_after[k] for k in stable_keys)
                and cause_logs_before["arrival"] == cause_logs_after["arrival"]
                and cause_logs_before["lifecycle"] == cause_logs_after["lifecycle"]
                and cause_logs_after["arrivalAfterLifecycle"]
                and cause_after["location"] == out["after"]["location"]
                and cause_logs_after["arrival"] == out["logsAfter"]["arrival"]
                and cause_after["inField"] != 0 and cause_after["frozen"] == 0
                and cause_after["eventState"] == 0 and cause_after["eventContext"] == 0 and cause_after["openMenu"] == 255)
            if not causes["lifecycleStable"]:
                causes["limits"].append("native location/gameplay/list/lifecycle changed during causal geometry capture")
            stable_controller_fields = {key: value for key, value in controller_fields.items()
                                       if key.split(":", 1)[1] in ("groupKey", "header", "regionHead", "regionTail", "spawnArray", "regionArray")}
            stable_header_fields = {key: value for key, value in header_fields.items()
                                   if key.split(":", 1)[1] in ("type", "headerId", "spawnCount", "regionCount")}
            causes["geometryRootsStable"] = (all(value == controller_values[key] for key, value in read(stable_controller_fields).items())
                                             and all(value == headers[key] for key, value in read(stable_header_fields).items()))
            if not causes["geometryRootsStable"]:
                causes["limits"].append("native controller/header region roots changed during geometry capture")
            after = read({"controllerCount": (base + 0x2A10418, "i32"),
                          "activationActor": (base + 0x2A10420, "u64"), "playerActor": (base + 0x2A105D0, "u64")})
            causes["after"] = after
            causes["tableStable"] = read(fields) == entries and after == roots
            if not causes["tableStable"]:
                causes["limits"].append("controller table/activation roots changed during read")
            causes["complete"] = not causes["limits"]
            for record in out["provenance"]:
                matches = [entry for entry in causes["controllers"] if not entry["flags"] & 1
                           and entry["pointer"] == record["controller"]
                           and entry.get("groupKey") == entry["key"]
                           and entry.get("groupKey") == record.get("controllerFields", {}).get("groupKey")
                           and entry.get("layoutValidated")]
                record["ordinaryTableIdentityVerified"] = bool(causes["tableStable"] and matches)
                record["ordinaryTableIndices"] = [entry["tableIndex"] for entry in matches]
                record["provenanceScope"] = "static shape plus table/key evidence; not proof of every native producer's semantic identity"
        except Exception as error:
            causes["limits"].append(f"{type(error).__name__}: {error}")
    else:
        causes["limits"].append("primary native list incomplete or30s deadline exhausted; causal reads skipped")
    # Independent post-census receipt: never changes population/readiness/hash or
    # geometry classifications. The same deadline/read failures remain visible.
    def raw_lifecycle_read(sample, stage, evidence, *, require_identity=False):
        try:
            header(sample["native"], stage=stage, failure_evidence=evidence,
                   **({"require_identity": True} if require_identity else {}))
            sample["nativeComplete"] = True
        finally:
            remaining()
            observed = logs()
            sample["logs"] = {key: observed[key] for key in ("arrival", "lifecycle", "arrivalAfterLifecycle")}
            line = observed["lifecycle"]["line"] if observed["lifecycle"] else ""
            completed = re.fullmatch(r"\[warp\] load complete serial=(\d+) transition=(\d+) "
                r"room=([0-9A-Fa-f]+)/([0-9A-Fa-f]+) door=(\d+) map=(\d+) btl=(\d+) evt=(\d+)", line)
            if completed and observed["arrival"]:
                values = completed.groups()
                sample["scope"] = {"loadSerial": int(values[0]), "transitionSerial": int(values[1]),
                    "location": [int(value, 16 if n < 2 else 10) for n, value in enumerate(values[2:])],
                    "epoch": int(observed["arrival"]["epoch"])}
            remaining()
    out["rawOccupancy"] = capture_raw_native_occupancy(base, read, lifecycle_read=raw_lifecycle_read)
    if resource_bindings:
        out["resourceBindings"] = capture_native_resource_bindings(base, read, out, lifecycle_read=raw_lifecycle_read)
    if controller_id_coverage:
        out["controllerIdCoverage"] = capture_native_controller_id_coverage(base, read, out, lifecycle_read=raw_lifecycle_read)
    if secondary_bindings:
        out["secondaryBindings"] = capture_native_secondary_bindings(base, read, out, lifecycle_read=raw_lifecycle_read)
    out["elapsedMs"] = round((time.monotonic() - started) * 1000)
    return out


def native_geometry_sampled_endpoints(snapshot: dict, entry: dict, other: dict) -> dict:
    """Evaluate two retained readbacks only; no new reads or trajectory/native-AL claim."""
    result = {"schemaVersion": 1, "available": False, "nativePredicateCalled": False,
              "sampling": "two retained readback endpoints; non-atomic; no continuous trajectory claim",
              "endpoints": []}
    try:
        def require(condition, reason):
            if not condition:
                raise ValueError(reason)

        def raw_word(value, size):
            require(isinstance(value, str) and re.fullmatch(r"[0-9a-fA-F]{%d}" % (size * 2), value) is not None,
                    "missing or malformed exact readback bytes")
            return bytes.fromhex(value)

        cause, point_cause = snapshot["causeContext"], other["causeContext"]
        location, arrival = snapshot["after"]["location"], snapshot["logsAfter"]["arrival"]
        require(isinstance(location, list) and len(location) == 6 and all(type(v) is int for v in location)
                and all(0 <= v <= (255 if n < 3 else 65535) for n, v in enumerate(location))
                and isinstance(arrival, dict) and int(arrival["epoch"]) > 0,
                "missing current full location or arrival")
        arrival_location = [int(arrival[k], 16 if k in ("world", "room") else 10)
                            for k in ("world", "room", "door", "map", "btl", "evt")]
        require(arrival_location == location, "arrival does not bind current full location")
        for peer, context in ((snapshot, cause), (other, point_cause)):
            require(all(peer.get(k) is True for k in ("complete", "listComplete", "actorIdentityStable", "lifecycleStable"))
                    and all(context.get(k) is True for k in ("complete", "lifecycleStable", "tableStable",
                                                           "geometryRootsStable", "positionSelectorsStable")),
                    "incomplete native identity, selectors, lifecycle or geometry context")
            require(all(peer[phase]["location"] == location for phase in ("before", "after"))
                    and all(context[phase]["location"] == location for phase in ("nativeBefore", "nativeAfter"))
                    and all(logs["arrival"] == arrival and logs["arrivalAfterLifecycle"] is True
                            for logs in (peer["logsBefore"], peer["logsAfter"], context["logsBefore"], context["logsAfter"]))
                    and peer["logsBefore"]["lifecycle"] is not None
                    and peer["logsBefore"]["lifecycle"] == peer["logsAfter"]["lifecycle"]
                       == context["logsBefore"]["lifecycle"] == context["logsAfter"]["lifecycle"],
                    "different or unbound completed lifecycle, epoch or full location")

        actor = point_cause["activationActors"]["activationActor"]
        address, offset = actor["address"], actor["selectedOffset"]
        require(type(address) is int and 0x10000 <= address <= 0x7FFFFFFFFFFF and actor["inCheckedNativeList"] is True
                and offset in (0x70, 0x670)
                and point_cause["before"]["activationActor"] == address == point_cause["after"]["activationActor"],
                "activation actor/root/accessor is unavailable")
        nodes = [node for node in other["nodes"] if node["address"] == address]
        require(len(nodes) == 1, "activation actor is not unique in checked native list")
        selectors, positions = point_cause["positionSelectorReadback"], point_cause["positionReadback"]
        require({key for key in positions["fields"] if key.startswith("activationActor:")}
                    == {"activationActor:0", "activationActor:1"}
                and {key for key in selectors["fields"] if key.startswith("activationActor:")}
                    == {"activationActor:parentHandle", "activationActor:objectEntry", "activationActor:status"},
                "activation readback field cardinality mismatch")
        for receipt in (selectors, positions):
            require(type(receipt["schemaVersion"]) is int and receipt["schemaVersion"] == 1
                    and receipt["encoding"] == "little-endian memory bytes, hex"
                    and receipt["comparisonComplete"] is True and "activationActor" in receipt["scope"],
                    "readback comparison or activation scope incomplete")
            for phase in ("before", "after"):
                sample = receipt[phase]
                require(all(sample.get(k) is True for k in ("attempted", "complete", "bytesComplete"))
                        and not sample.get("unreadFields") and not sample.get("encodingErrors")
                        and set(sample["bytes"]) == set(receipt["fields"]), "partial readback receipt")
                for label, field in receipt["fields"].items():
                    require(field["type"] in ("u32", "u64"), "unsupported readback field encoding")
                    raw_word(sample["bytes"][label], 4 if field["type"] == "u32" else 8)
        selector_values = {}
        for name, relative, kind, size in (("parentHandle", 0x6A0, "u32", 4),
                                           ("objectEntry", 0x918, "u64", 8), ("status", 0x5C0, "u64", 8)):
            label = "activationActor:" + name
            require(selectors["fields"][label] == {"address": address + relative, "type": kind},
                    "selector field address/type does not bind checked actor")
            first = raw_word(selectors["before"]["bytes"][label], size)
            require(first == raw_word(selectors["after"]["bytes"][label], size), "activation selector bytes changed")
            selector_values[name] = int.from_bytes(first, "little")
        require(all(selector_values[k] == nodes[0][k] for k in ("objectEntry", "status"))
                and selector_values["parentHandle"] == actor["parentHandle"], "selector native identity mismatch")
        handle = selector_values["parentHandle"]
        resolved = 0
        if handle:
            masked = handle & 0x7FFFFFFF
            before_regions = point_cause["nativeBefore"]["regions"]
            require(before_regions == point_cause["nativeAfter"]["regions"], "attachment handle roots changed")
            region = before_regions[masked >> 25]
            resolved = region | (masked & 0x1FFFFFF)
            require(region != 0xFFFFFFFFFFFFFFFF and not region & 0x1FFFFFF and 0x10000 <= resolved <= 0x7FFFFFFFFFFF,
                    "attachment handle resolution unavailable")
        require(actor["resolvedParent"] == resolved and offset == (0x70 if resolved else 0x670),
                "selected accessor offset does not match retained selector")

        geometry = entry["geometry"]
        regions, descriptors = geometry["regions"], geometry["descriptors"]
        owners = [candidate for candidate in cause["controllers"]
                  if candidate["pointer"] == entry["pointer"] and not candidate["flags"] & 1]
        require(len(owners) == 1 and owners[0] == entry
                and entry["layoutValidated"] is True and entry["tableKeyValidated"] is True
                and not entry["flags"] & 1 and entry["groupKey"] == entry["key"]
                and geometry["complete"] is True and geometry["bytesStable"] is True
                and 0 < len(regions) <= len(descriptors) == entry["headerFields"]["regionCount"]
                and len({r["address"] for r in regions}) == len(regions)
                and len({r["descriptorIndex"] for r in regions}) == len(regions)
                and entry["regionHead"] == regions[0]["address"] and entry["regionTail"] == regions[-1]["address"],
                "incomplete, empty or ambiguous rooted current geometry")
        for n, region in enumerate(regions):
            index = region["descriptorIndex"]
            require(type(index) is int and 0 <= index < len(descriptors)
                    and type(region["address"]) is int and 0x10000 <= region["address"] <= 0x7FFFFFFFFFFF
                    and region["descriptor"] == entry["regionArray"] + index * 64 == descriptors[index]["address"]
                    and region["nextAddress"] == (regions[n + 1]["address"] if n + 1 < len(regions) else 0)
                    and len(region["inverseMatrix"]) == 16 and len(region["extents"]) == 3,
                    "region list/descriptor identity mismatch")
        endpoints = []
        for phase in ("before", "after"):
            words = []
            for n in range(2):
                label = f"activationActor:{n}"
                require(positions["fields"][label] == {"address": address + offset + n * 8, "type": "u64"},
                        "position field address/type does not bind selected accessor")
                words.append(raw_word(positions[phase]["bytes"][label], 8))
            raw = b"".join(words)
            point = list(struct.unpack("<4f", raw))
            require(all(math.isfinite(v) for v in point), "nonfinite endpoint position")
            if phase == "before":
                require(raw == raw_word(actor["positionHex"], 16), "initial position differs from retained actor readback")
            evaluated = [dict(descriptorIndex=r["descriptorIndex"], address=r["address"],
                              **evaluate_native_region(r, point)) for r in regions]
            require(all(type(r.get("accepted")) is bool for r in evaluated), "endpoint geometry evaluation unavailable")
            endpoints.append({"phase": phase, "positionHex": raw.hex(), "position4": point, "regions": evaluated})
        result.update(available=True, endpoints=endpoints,
                      positionBytesStable=endpoints[0]["positionHex"] == endpoints[1]["positionHex"])
    except (AttributeError, KeyError, IndexError, TypeError, ValueError, OverflowError, struct.error) as error:
        result["reason"] = str(error)
    return result


def native_geometry_comparisons(peers: dict) -> list[dict]:
    """Evaluate sampled peer activation points against each captured type2 geometry."""
    comparisons = []
    for owner, snapshot in peers.items():
        cause = snapshot.get("causeContext", {})
        for entry in cause.get("controllers", []):
            if "geometry" not in entry:
                continue
            for peer, other in peers.items():
                other_cause = other.get("causeContext", {})
                point = other_cause.get("activationActors", {}).get("activationActor", {}).get("position4")
                result = {"geometryPeer": owner, "pointPeer": peer, "groupKey": entry.get("groupKey"),
                          "headerId": entry["headerFields"]["headerId"], "available": False,
                          "sampling": "concurrent read windows, not atomic game frames or native predicate return instrumentation"}
                result["sampledEndpoints"] = native_geometry_sampled_endpoints(snapshot, entry, other)
                comparisons.append(result)
                if (not cause.get("complete") or not other_cause.get("complete") or not point
                        or not entry["geometry"]["complete"]
                        or snapshot.get("after", {}).get("location") != other.get("after", {}).get("location")
                        or snapshot.get("logsAfter", {}).get("arrival") != other.get("logsAfter", {}).get("arrival")):
                    result["reason"] = "incomplete geometry/activation sample or different completed epoch/location"
                    continue
                result.update(available=True, position4=point,
                              pointStableDuringCapture=other_cause.get("positionsStableDuringGeometry"),
                              regions=[dict(descriptorIndex=r["descriptorIndex"],
                                            **evaluate_native_region(r, point)) for r in entry["geometry"]["regions"]])
    return comparisons


def step_native_enemy_census(ctx: Context, step: dict) -> dict:
    """Three read-only concurrent-peer snapshots; complete collection is not parity acceptance."""
    name = step.get("as", "native_enemy_census")
    indices = step.get("instances", list(range(len(ctx.instances))))
    count = step.get("samples", 3)
    interval = step.get("intervalMs", 1500)
    controller_id_coverage = step.get("controllerIdCoverage", False)
    if type(controller_id_coverage) is not bool:
        raise StepFailed("native census controllerIdCoverage must be a boolean")
    secondary_bindings = step.get("secondaryBindings", False)
    if type(secondary_bindings) is not bool:
        raise StepFailed("native census secondaryBindings must be a boolean")
    resource_bindings = step.get("resourceBindings", False)
    if type(resource_bindings) is not bool:
        raise StepFailed("native census resourceBindings must be a boolean")
    if not 1 <= count <= 3 or not 0 <= interval <= 5000:
        raise StepFailed("native census supports1..3 snapshots and0..5000ms settling interval")
    evidence = {"diagnosticOnly": True, "snapshots": [], "readOnly": True,
                "note": "a successful collection does not assert peer parity or empty enemy application"}
    try:
        for number in range(count):
            if number:
                ctx.sleep(interval / 1000)
            with ThreadPoolExecutor(max_workers=len(indices)) as pool:
                futures = {i: pool.submit(native_enemy_census_snapshot, ctx, i, 30,
                                         resource_bindings=resource_bindings,
                                         **({"controller_id_coverage": True} if controller_id_coverage else {}),
                                         **({"secondary_bindings": True} if secondary_bindings else {})) for i in indices}
                peers = {str(i): future.result() for i, future in futures.items()}
            evidence["snapshots"].append({"ordinal": number, "peers": peers,
                                          "geometryComparisons": native_geometry_comparisons(peers)})
            ctx.check_all()
        evidence["complete"] = all(peer["complete"] for snap in evidence["snapshots"] for peer in snap["peers"].values())
        evidence["geometryComplete"] = all(peer.get("causeContext", {}).get("complete", False)
                                            for snap in evidence["snapshots"] for peer in snap["peers"].values())
        if not evidence["complete"]:
            raise StepFailed("native census has incomplete/changing reads; inspect preserved snapshots, do not infer native absence")
        return {"path": f"{name}.json", "complete": True, "diagnosticOnly": True,
                "geometryComplete": evidence["geometryComplete"], "nativeCounts": [{i: peer["comparison"]["nativeLivingCount"] for i, peer in snap["peers"].items()}
                                 for snap in evidence["snapshots"]]}
    finally:
        ctx.saved[name] = evidence
        path = ctx.run_dir / f"{name}.json"
        path.write_text(json.dumps(evidence, indent=2))
        ctx.artifacts.append(path.name)


class Recorder:
    """Samples every party actor's position on the given instances in a
    thread: rows of (t, instance, room, head, address, name, objectType, x,
    y, z), where head is the instance's own Sora (the entity list head)."""

    def __init__(self, ctx: Context, instances: list[int]) -> None:
        self.ctx, self.instances = ctx, instances
        self.rows: list[tuple] = []
        self.heads: dict[int, str] = {}  # instance -> its own Sora (entity list head)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._loop, daemon=True)
        self._thread.start()

    def _loop(self) -> None:
        while not self._stop.is_set():
            for index in self.instances:
                try:
                    ents = self.ctx.entities(index)
                except Exception:  # noqa: BLE001 - loading screens etc.
                    continue
                t = time.monotonic()
                actors = ents.get("actors") or []
                if not actors:
                    continue
                # The list head is this instance's own Sora; the room key says
                # where the sample was taken (addresses repeat across rooms).
                head = actors[0]["address"]
                room = f"{ents.get('world', 0):02X}/{ents.get('room', 0):02X}"
                self.heads[index] = head
                for a in actors:
                    if a.get("objectType") in (0, 1):
                        p = a["position"]
                        self.rows.append((t, index, room, head, a["address"], a["name"], a["objectType"],
                                          p["x"], p["y"], p["z"]))

    def stop(self) -> None:
        self._stop.set()
        self._thread.join(timeout=5)


def step_wander(ctx: Context, step: dict) -> dict:
    """Random stick walks on several instances for a while (seeded), taking
    turns, with an occasional jump. For soaks."""
    import random

    rng = random.Random(step.get("seed", 1))
    instances = step.get("instances", [0])
    end = time.monotonic() + step.get("seconds", 60)
    moves = 0
    while time.monotonic() < end:
        for index in instances:
            angle = rng.uniform(0, 2 * math.pi)
            kh2ctl("player-input", "--lx", f"{math.cos(angle):.2f}", "--ly", f"{math.sin(angle):.2f}",
                   "--duration-ms", str(rng.randint(400, 900)), pid=ctx.inst(index).pid)
            if rng.random() < 0.15:
                kh2ctl("player-press", "--button", "circle", "--duration-ms", "120", pid=ctx.inst(index).pid)
            moves += 1
        ctx.check_all()
    return {"moves": moves}


def _tracks(rec: "Recorder", owner: int, viewer: int):
    """Owner's own-Sora samples (t, room, pos) and the viewer's other party
    actors (t, room, address, name, pos)."""
    truth = [(t, r, (x, y, z)) for t, i, r, h, a, n, o, x, y, z in rec.rows if i == owner and a == h]
    others = [(t, r, a, n, (x, y, z)) for t, i, r, h, a, n, o, x, y, z in rec.rows if i == viewer and a != h]
    return truth, others


class _Window:
    """Time-range slices of a sorted owner track."""

    def __init__(self, truth: list) -> None:
        self.truth, self.times = truth, [s[0] for s in truth]

    def __call__(self, start: float, end: float) -> list:
        import bisect

        return self.truth[bisect.bisect_left(self.times, start):bisect.bisect_right(self.times, end)]


def jitter_stats(rec: "Recorder", owner: int, viewer: int, actor: str, settle: float = 1.5) -> dict:
    """Pops: puppet steps between consecutive samples that exceed the owner's
    largest step over the same interval by more than 30 units. Only steps
    where the owner was in the viewer's room for the step and the `settle`
    seconds before it count (appear/hide snaps aren't jitter)."""
    name = actor.split("@")[0]
    truth, others = _tracks(rec, owner, viewer)
    puppet = [s for s in others if s[3] == name]
    owner_window = _Window(truth)
    pops, worst, counted, seconds = 0, 0.0, 0, 0.0
    for (t0, h0, a0, _, p0), (t1, h1, a1, _, p1) in zip(puppet, puppet[1:]):
        if h0 != h1 or a0 != a1 or t1 - t0 > 0.25:
            continue
        window = owner_window(t0 - settle, t1)
        if not window or any(h != h0 for _, h, _ in window) or window[0][0] > t0 - settle + 0.25:
            continue  # owner elsewhere, or only just arrived
        owner_step = max((math.dist(a[2], b[2]) for a, b in zip(window, window[1:])
                          if a[0] >= t0 - 0.5), default=0.0)
        excess = math.dist(p0, p1) - owner_step
        worst = max(worst, excess)
        counted += 1
        seconds += t1 - t0
        if excess > 30:
            pops += 1
    return {"steps": counted, "sameRoomMinutes": round(seconds / 60, 1), "pops": pops,
            "worstExcess": round(worst, 1)}


def step_record(ctx: Context, step: dict) -> dict:
    ctx.saved[step["as"]] = Recorder(ctx, step.get("instances", [0, 1]))
    return {}


def step_record_stop(ctx: Context, step: dict) -> dict:
    rec: Recorder = ctx.saved[step["as"]]
    rec.stop()
    path = ctx.run_dir / f"{step['as']}.csv"
    with open(path, "w") as fh:
        fh.write("t,instance,room,head,address,name,type,x,y,z\n")
        for r in rec.rows:
            fh.write(",".join(f"{v:.4f}" if isinstance(v, float) else str(v) for v in r) + "\n")
    ctx.artifacts.append(path.name)
    return {"samples": len(rec.rows)}


def puppet_error(rec: Recorder, owner: int, viewer: int, window: float = 0.5) -> dict:
    """How closely the viewer's puppet follows the owner's Sora. Each viewer
    actor (except its own Sora) is a candidate; for each of its samples, the
    error is the distance to the nearest owner position in the preceding
    `window` seconds (the puppet renders behind on purpose). The candidate
    with the lowest mean error is the puppet. Only samples where the owner is
    in the viewer's room count (same world/room), and candidates are
    keyed by name since actor addresses change per room. Units: KH2 units
    (100 = 1 m)."""
    truth, others = _tracks(rec, owner, viewer)
    if not truth:
        return {"ok": False, "why": "no owner samples"}
    candidates: dict[str, list] = {}
    for t, h, a, n, p in others:
        candidates.setdefault(n, []).append((t, h, a, p))
    owner_window = _Window(truth)
    best = None
    for name, samples in candidates.items():
        errors, lags = [], []
        for t, h, a, p in samples:
            if t - truth[0][0] < window:
                continue  # no full lag window of owner history yet
            near = [(math.dist(p, q), t - tq) for tq, hq, q in owner_window(t - window, t)
                    if hq == h]
            if near:
                d, lag = min(near)
                errors.append(d)
                lags.append(lag)
        key = f"{name}@{samples[-1][2]}"
        if len(errors) < 10:
            continue
        errors.sort()
        result = {"actor": key, "n": len(errors), "mean": sum(errors) / len(errors),
                  "p95": errors[int(0.95 * (len(errors) - 1))], "max": errors[-1],
                  "lagMs": 1000 * sum(lags) / len(lags)}
        if best is None or result["mean"] < best["mean"]:
            best = result
    out = best or {"ok": False, "why": "no candidate with 10+ matched samples"}
    out["ownerTravel"] = round(sum(math.dist(a[2], b[2]) for a, b in zip(truth, truth[1:])
                                   if a[1] == b[1]), 1)
    return out


STEPS = {"boot": step_boot, "launch": step_launch, "warp": step_warp, "input": step_input,
         "press": step_press, "wait": step_wait, "wait_until": step_wait_until,
         "assert": step_assert, "save": step_save, "protect": step_protect,
         "capture": step_capture, "clip": step_clip, "crash": step_crash, "freeze": step_freeze,
         "relay": step_relay, "runtime": step_runtime, "record": step_record,
         "record_stop": step_record_stop, "wander": step_wander, "hit_all": step_hit_all,
         "kh2ctl": step_kh2ctl, "transition_check": step_transition_check,
         "align_courtyard_exit": step_align_courtyard_exit,
         "progress_snapshot": step_progress_snapshot, "statehash_check": step_statehash_check,
         "progress_hash_control": step_progress_hash_control,
         "approach_goa_chest": step_approach_goa_chest,
         "dismiss_goa_map_reward": step_dismiss_goa_map_reward,
         "enemy_hash_control": step_enemy_hash_control,
         "courtyard_diagnostic": step_courtyard_diagnostic,
         "native_enemy_census": step_native_enemy_census}


# Saved-log forced-resync evidence is isolated from the live collector. Load by
# this file's fixed sibling path so both script and importlib test entry work.
import importlib.util as _importlib_util
_resync_spec = _importlib_util.spec_from_file_location(
    "kh2coop_resync_evidence", Path(__file__).with_name("resync_evidence.py"))
_resync_evidence = _importlib_util.module_from_spec(_resync_spec)
_resync_spec.loader.exec_module(_resync_evidence)
_resync_evidence.register(STEPS, kh2ctl=lambda *a, **kw: kh2ctl(*a, **kw), logs=LOGS,
                          step_failed=StepFailed, wait_for=wait_for)

_progress_resync_spec = _importlib_util.spec_from_file_location(
    "kh2coop_progress_resync_control", Path(__file__).with_name("progress_resync_control.py"))
_progress_resync = _importlib_util.module_from_spec(_progress_resync_spec)
_progress_resync_spec.loader.exec_module(_progress_resync)
_progress_resync.register(STEPS, kh2ctl=lambda *a, **kw: kh2ctl(*a, **kw), logs=LOGS,
                          step_failed=StepFailed, arrival_pattern=ARRIVAL_PATTERN,
                          snapshot=lambda ctx, step, read: step_progress_snapshot(ctx, step, _read=read))

_reconnect_spec = _importlib_util.spec_from_file_location(
    "kh2coop_native_reconnect_evidence", Path(__file__).with_name("native_reconnect_evidence.py"))
_native_reconnect = _importlib_util.module_from_spec(_reconnect_spec)
sys.modules[_reconnect_spec.name] = _native_reconnect
_reconnect_spec.loader.exec_module(_native_reconnect)
_lifecycle_spec = _importlib_util.spec_from_file_location(
    "kh2coop_runtime_lifecycle", Path(__file__).with_name("runtime_lifecycle.py"))
_runtime_lifecycle = _importlib_util.module_from_spec(_lifecycle_spec)
sys.modules[_lifecycle_spec.name] = _runtime_lifecycle
_lifecycle_spec.loader.exec_module(_runtime_lifecycle)
STEPS.update({"reconnect_mark": step_reconnect_mark, "runtime_pause": step_runtime_pause,
              "reconnect_check": step_reconnect_check})


# --------------------------------------------------------------------------
# One run
# --------------------------------------------------------------------------

def bundle(ctx: Context, died: InstanceDied) -> list[str]:
    """Collects a crash/hang bundle into the run dir."""
    files = []
    inst = ctx.instances[died.index]
    if pid_alive(inst.pid):  # hung: dump it from outside first
        out = ctx.run_dir / f"hang_{inst.pid}.dmp"
        if kh2ctl("dump", "--out", str(out), pid=inst.pid, check=False).get("ok"):
            files.append(out.name)
    for src in (LOGS / f"kh2coop_inject_{inst.pid}.log", LOGS / f"kh2coop_crash_{inst.pid}.dmp"):
        if src.exists():
            shutil.copy2(src, ctx.run_dir / src.name)
            files.append(src.name)
    return files


def link_desync_reports(run_dir: Path) -> dict:
    """Index relay-owned artifacts already in this run; never collect peer files.

    A killed relay can leave its initial collecting manifest. Keep it and label
    the collection partial instead of letting the scenario's PASS imply success.
    """
    root = run_dir / "desync-reports"
    result = {"collectionStatus": "none", "reports": [], "suppressionSummaries": [], "errors": [],
              "filesystemEvidence": "unavailable",
              "filesystemNote": "No automatic report files found; relay/runtime logs remain evidence of output failures or whether collection was triggered."}
    if not root.exists():
        return result
    run_root = run_dir.resolve()
    try:
        manifests = sorted(root.glob("*/*/manifest.json"))
        for path in manifests[:64]:
            if not path.resolve().is_relative_to(run_root):
                result["errors"].append("manifest escapes owned run directory")
                continue
            item = {"manifest": path.relative_to(run_dir).as_posix(),
                    "directory": path.parent.relative_to(run_dir).as_posix(),
                    "collectionStatus": "partial"}
            try:
                if path.stat().st_size > 2 * 1024 * 1024:
                    raise ValueError("manifest exceeds 2 MiB linkage limit")
                data = json.loads(path.read_text(encoding="utf-8"))
                status = data.get("collectionStatus")
                item["reportedStatus"] = status
                item["key"] = data.get("key")
                if data.get("schemaVersion") != 1:
                    raise ValueError("unknown desync manifest schema")
                if status in ("complete", "partial", "interrupted", "storage-error"):
                    item["collectionStatus"] = status
                elif status == "collecting":
                    item["error"] = "relay stopped before collection finalization"
                else:
                    raise ValueError("unknown desync collection status")
            except (OSError, ValueError, TypeError, AttributeError) as exc:
                item["error"] = f"{type(exc).__name__}: {exc}"
            result["reports"].append(item)
        if len(manifests) > 64:
            result["errors"].append("more than 64 manifests; additional reports retained on disk")
        summaries = sorted(root.glob("*/suppression-summary*"))
        for path in summaries[:64]:
            if not path.resolve().is_relative_to(run_root):
                result["errors"].append("suppression summary escapes owned run directory")
                continue
            item = {"path": path.relative_to(run_dir).as_posix(), "collectionStatus": "partial"}
            try:
                if path.stat().st_size > 2 * 1024 * 1024:
                    raise ValueError("suppression summary exceeds 2 MiB linkage limit")
                data = json.loads(path.read_text(encoding="utf-8"))
                if data.get("schemaVersion") != 1 or data.get("artifactType") != "desync-suppression-summary":
                    raise ValueError("unknown suppression summary schema")
                if data.get("collectionStatus") != "skipped":
                    raise ValueError("suppression summary cannot certify collected evidence")
                if data.get("sessionId") != path.parent.name:
                    raise ValueError("suppression session does not match owned directory")
                counters = data.get("counters", {})
                fields = ("cadence", "quota", "active", "finishing", "witnessOverflow", "lostTriggers", "storageErrors")
                if not all(type(counters.get(key)) is int and counters[key] >= 0 for key in fields):
                    raise ValueError("incomplete suppression counters")
                item.update(sessionId=data["sessionId"], revision=data.get("revision"), counters=counters)
                if path.name != "suppression-summary.json":
                    raise ValueError("suppression summary publication remains unfinished")
                item["collectionStatus"] = "skipped"
            except (OSError, ValueError, TypeError, AttributeError) as exc:
                item["error"] = f"{type(exc).__name__}: {exc}"
            result["suppressionSummaries"].append(item)
        if manifests or summaries:
            result["filesystemEvidence"] = "present"
            result.pop("filesystemNote", None)
        if len(summaries) > 64:
            result["errors"].append("more than 64 suppression summaries; additional evidence retained on disk")
        if not manifests and not summaries and any(root.iterdir()):
            result["errors"].append("report output exists without a manifest or suppression summary")
    except OSError as exc:
        result["errors"].append(f"{type(exc).__name__}: {exc}")
    if (result["errors"] or any(r["collectionStatus"] != "complete" for r in result["reports"])
            or any(r["collectionStatus"] != "skipped" for r in result["suppressionSummaries"])):
        result["collectionStatus"] = "partial"
    elif result["suppressionSummaries"]:
        result["collectionStatus"] = "partial" if result["reports"] else "skipped"
    elif result["reports"]:
        result["collectionStatus"] = "complete"
    return result


def run_scenario(path: Path, attempt: int) -> dict:
    scenario = json.loads(path.read_text())
    name = scenario.get("name", path.stem)
    stamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = RUNS / f"{stamp}_{name}_{attempt}"
    run_dir.mkdir(parents=True, exist_ok=True)
    ctx = Context(run_dir)
    for index in scenario.get("protect", []):
        ctx.protect.add(index)
    report = {"scenario": name, "file": str(path.relative_to(ROOT)) if path.is_relative_to(ROOT) else str(path),
              "attempt": attempt, "started": dt.datetime.now().isoformat(timespec="seconds"),
              "status": "pass", "steps": [], "artifacts": ctx.artifacts}
    t0 = time.monotonic()
    try:
        for i, step in enumerate(scenario["steps"]):
            kind = step["do"]
            entry = {"index": i, "do": kind, "status": "pass"}
            s0 = time.monotonic()
            try:
                if kind not in STEPS:
                    raise StepFailed(f"unknown step kind {kind!r}")
                entry["result"] = STEPS[kind](ctx, step)
                ctx.check_all()
            except StepFailed as e:
                entry.update(status="fail", error=str(e))
                report["status"] = "fail"
                report["error"] = f"step {i} ({kind}): {e}"
            except InstanceDied as died:
                entry.update(status="crash", error=str(died))
                raise
            finally:
                entry["seconds"] = round(time.monotonic() - s0, 2)
                report["steps"].append(entry)
            if entry["status"] != "pass":
                break
    except InstanceDied as died:
        report["status"] = "crash" if died.kind == "exited" else "hang"
        report["error"] = str(died)
        report["bundle"] = bundle(ctx, died)
    except Exception as e:  # noqa: BLE001 - runner bugs still produce a report
        report["status"] = "fail"
        report["error"] = f"runner error: {e}"
        report["traceback"] = traceback.format_exc()
    finally:
        ctx.close()
        for inst in ctx.instances:  # recover: never leave this run's instances behind
            kh2ctl("kill", pid=inst.pid, check=False)
        for inst in ctx.instances:
            log = LOGS / f"kh2coop_inject_{inst.pid}.log"
            if log.exists() and not (run_dir / log.name).exists():
                shutil.copy2(log, run_dir / log.name)
        report["desyncCollection"] = link_desync_reports(run_dir)
        if (run_dir / "desync-reports").exists():
            ctx.artifacts.append("desync-reports")
        for capture in report["desyncCollection"]["reports"]:
            ctx.artifacts.append(capture["directory"])
        for summary in report["desyncCollection"]["suppressionSummaries"]:
            ctx.artifacts.append(summary["path"])
        report["seconds"] = round(time.monotonic() - t0, 1)
        report["instances"] = [inst.pid for inst in ctx.instances]
        (run_dir / "report.json").write_text(json.dumps(report, indent=2))
        (run_dir / "report.md").write_text(render_md(report))
    report["dir"] = str(run_dir)
    return report


def render_md(report: dict) -> str:
    lines = [f"# {report['scenario']} (attempt {report['attempt']}): {report['status'].upper()}", "",
             f"Started {report['started']}, {report.get('seconds', '?')} s, instances {report.get('instances')}.", ""]
    if report.get("desyncCollection"):
        diagnostic = report["desyncCollection"]
        lines += [f"Automatic desync collection: **{diagnostic['collectionStatus'].upper()}** (separate from scenario step status).", ""]
        for capture in diagnostic["reports"]:
            lines += [f"- [{capture['manifest']}]({capture['manifest']}): {capture['collectionStatus']}"
                      + (f" - {capture['error']}" if capture.get("error") else "")]
        for summary in diagnostic.get("suppressionSummaries", []):
            lines += [f"- Suppression evidence [{summary['path']}]({summary['path']}): {summary['collectionStatus']}"
                      + (f" - {summary['error']}" if summary.get("error") else
                         " - skipped triggers; no collected peer report: " + json.dumps(summary.get("counters", {}), sort_keys=True))]
        if diagnostic.get("filesystemEvidence") == "unavailable":
            lines += ["- Filesystem evidence unavailable: " + diagnostic.get("filesystemNote", "See relay/runtime logs.")]
        for error in diagnostic["errors"]:
            lines += [f"- Collection error: {error}"]
        lines += [""]
    if report.get("error"):
        lines += [f"**Error:** {report['error']}", ""]
    lines += ["| # | step | status | s | detail |", "|---|---|---|---|---|"]
    for s in report["steps"]:
        detail = s.get("error") or json.dumps(s.get("result", {}))
        lines.append(f"| {s.get('index', '')} | {s['do']} | {s['status']} | {s.get('seconds', '')} | {detail} |")
    if report.get("artifacts"):
        lines += ["", "Artifacts: " + ", ".join(report["artifacts"])]
    if report.get("bundle"):
        lines += ["", "Crash bundle: " + ", ".join(report["bundle"])]
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# Suite
# --------------------------------------------------------------------------

def validate_scenario(scenario: dict) -> None:
    """Offline structure/expression checks; never evaluate code or touch the rig."""
    if not isinstance(scenario, dict) or not isinstance(scenario.get("steps"), list):
        raise ValueError("scenario must be an object with a steps array")
    if not scenario["steps"]:
        raise ValueError("scenario needs at least one step")
    count = 0
    required = {"forced_resync_evidence": ("statehash", "census"), "warp": ("world", "room"), "press": ("button",),
                "assert": ("expr",), "wait_until": ("expr",), "save": ("as", "expr"),
                "runtime": ("role",), "record": ("as",), "record_stop": ("as",),
                "reconnect_mark": ("as",), "runtime_pause": ("instance", "after"),
                "reconnect_check": ("after",),
                "align_courtyard_exit": ("blockedExpr",)}
    for number, step in enumerate(scenario["steps"]):
        prefix = f"step {number}"
        if not isinstance(step, dict) or step.get("do") not in STEPS:
            raise ValueError(f"{prefix}: unknown or missing do")
        kind = step["do"]
        for key in required.get(kind, ()):
            if key not in step:
                raise ValueError(f"{prefix} ({kind}): missing {key}")
        if kind in ("boot", "launch"):
            init_timeout = step.get("initTimeoutMs", 15000)
            if type(init_timeout) is not int or not 1 <= init_timeout <= 60000:
                raise ValueError(f"{prefix}: initTimeoutMs must be an integer in 1..60000")
            if "instance" in step and step["instance"] != count:
                raise ValueError(f"{prefix}: boot/launch appends instance {count}")
            count += 1
        elif "instance" in step:
            index = step["instance"]
            if type(index) is not int or not 0 <= index < count:
                raise ValueError(f"{prefix}: instance must refer to an earlier boot/launch")
        if "instances" in step and any(type(i) is not int or not 0 <= i < count
                                       for i in step["instances"]):
            raise ValueError(f"{prefix}: instances must refer to earlier boots/launches")
        if kind == "kh2ctl" and ("args" in step) == ("argsExpr" in step):
            raise ValueError(f"{prefix}: kh2ctl requires exactly one of args/argsExpr")
        if kind == "native_enemy_census" and type(step.get("secondaryBindings", False)) is not bool:
            raise ValueError(f"{prefix}: native census secondaryBindings must be a boolean")
        if kind == "native_enemy_census" and type(step.get("controllerIdCoverage", False)) is not bool:
            raise ValueError(f"{prefix}: native census controllerIdCoverage must be a boolean")
        if kind == "native_enemy_census" and type(step.get("resourceBindings", False)) is not bool:
            raise ValueError(f"{prefix}: native census resourceBindings must be a boolean")
        if "argsExpr" in step:
            compile(step["argsExpr"], f"<{prefix} argsExpr>", "eval")
        if "expr" in step:
            compile(step["expr"], f"<{prefix}>", "eval")
        if "blockedExpr" in step:
            compile(step["blockedExpr"], f"<{prefix} blockedExpr>", "eval")
        if kind == "runtime" and step["role"] not in ("player", "friend1", "friend2", "spectator"):
            raise ValueError(f"{prefix}: invalid runtime role")
        if kind in ("reconnect_mark", "runtime_pause", "reconnect_check"):
            if count != 3 or step.get("instances", [0, 1, 2]) != [0, 1, 2]:
                raise ValueError(f"{prefix}: reconnect requires exactly three earlier ordered instances")
            if kind == "runtime_pause" and step["instance"] != 1:
                raise ValueError(f"{prefix}: runtime_pause may target only Friend1")
            timeout = step.get("timeoutMs", 20000 if kind == "runtime_pause" else 10000 if kind == "reconnect_mark" else 45000)
            limit = 20000 if kind == "runtime_pause" else 15000 if kind == "reconnect_mark" else 60000
            minimum = 1000 if kind == "runtime_pause" else 1
            if type(timeout) is not int or not minimum <= timeout <= limit:
                raise ValueError(f"{prefix}: reconnect timeoutMs must be an integer in {minimum}..{limit}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("scenarios", nargs="+", type=Path)
    parser.add_argument("--repeat", type=int, default=1)
    parser.add_argument("--validate", action="store_true",
                        help="check JSON, step structure and expression syntax offline; never access the rig")
    args = parser.parse_args()

    try:
        for path in args.scenarios:
            validate_scenario(json.loads(path.read_text(encoding="utf-8")))
    except (ValueError, SyntaxError, OSError, TypeError) as error:
        print(json.dumps({"ok": False, "file": str(path), "error": str(error)}))
        return EXIT_FAIL
    if args.validate:
        print(json.dumps({"ok": True, "validated": [str(p) for p in args.scenarios],
                          "scope": "offline structure and expression syntax only"}))
        return EXIT_PASS

    if not KH2CTL.exists():
        print(f"kh2ctl not built: {KH2CTL}", file=sys.stderr)
        return EXIT_RIG
    lock = RigLock()
    problem = lock.acquire()
    if problem:
        print(json.dumps({"ok": False, "error": problem}))
        return EXIT_RIG
    try:
        foreign = foreign_kh2()
        if foreign:
            print(json.dumps({"ok": False, "error": f"KH2 running outside the rig (pids {foreign}); "
                              "James may be playing. Not touching it."}))
            return EXIT_RIG
        saves_before = hash_saves()
        results = []
        for attempt in range(1, args.repeat + 1):
            for path in args.scenarios:
                r = run_scenario(path.resolve(), attempt)
                results.append(r)
                print(f"[{r['status'].upper():5}] {r['scenario']} #{attempt} ({r.get('seconds')} s) {r.get('error', '')}",
                      flush=True)
        saves_after = hash_saves()
        changed = sorted(k for k in set(saves_before) | set(saves_after)
                         if saves_before.get(k) != saves_after.get(k))
        save_violation = [k for k in changed if Path(k).name not in STEAM_OWNED]
        summary = {
            "ok": all(r["status"] == "pass" for r in results) and not save_violation,
            "runs": len(results),
            "passed": sum(r["status"] == "pass" for r in results),
            "failed": [f"{r['scenario']}#{r['attempt']}: {r['status']}" for r in results if r["status"] != "pass"],
            "saveFilesChecked": len(saves_before),
            "saveFilesChanged": changed,
            "saveViolation": save_violation,
            "reports": [r["dir"] for r in results],
        }
        RUNS.mkdir(parents=True, exist_ok=True)
        (RUNS / "last_suite.json").write_text(json.dumps(summary, indent=2))
        print(json.dumps(summary, indent=2))
        if save_violation:
            return EXIT_FAIL
        if any(r["status"] in ("crash", "hang") for r in results):
            return EXIT_CRASH
        return EXIT_PASS if summary["ok"] else EXIT_FAIL
    finally:
        lock.release()


if __name__ == "__main__":
    sys.exit(main())
