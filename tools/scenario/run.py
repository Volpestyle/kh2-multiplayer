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
                "range": range, "round": round, "int": int, "str": str}

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
    # step "env" reaches the game (kh2ctl launch passes its environment on),
    # e.g. {"KH2COOP_PUPPET_TRACE": "1"}.
    data = kh2ctl("launch", env={k: str(v) for k, v in step.get("env", {}).items()})
    inst = Instance(len(ctx.instances), data["processId"])
    ctx.instances.append(inst)
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
    kh2ctl("capture", "--out", str(out), pid=inst.pid)
    ctx.artifacts.append(out.name)
    return {"path": out.name}


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
    return kh2ctl(*map(str, step["args"]), pid=ctx.inst(step.get("instance", 0)).pid)


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
                                        *map(str, step.get("args", []))])
    ctx.sleep(1)
    if proc.poll() is not None:
        raise StepFailed(f"relay exited with {proc.returncode}")
    ctx.relay_port = port
    return {"pid": proc.pid, "port": port}


def step_runtime(ctx: Context, step: dict) -> dict:
    """Start a runtime bound to one instance (--pid) and connect it to
    the relay. Waits until its log shows it connected."""
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
    name = f"runtime_{inst.index}"
    proc = start_process(ctx, name, cmd)
    log = ctx.run_dir / f"{name}.log"
    expect = step.get("expect", "connected to server")

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


def native_puppet_actors(ctx: Context, index: int) -> dict:
    """Resolve the DLL's actual friend-slot targets in the active entity list.

    Companion pointers live in unit slot 1, at SLOT0_BASE + SLOT_STRIDE +
    0x220/0x228. Sora clones bypass those pointers: use the DLL's per-slot
    motion-driver actor log from this load, never actor names or list order.
    GameBridge's state command can return a default ActorState for an absent
    actor, so it is not evidence that a native friend actor exists.
    """
    keys = ("0x2A239B0", "0x2A239B8")
    sample = kh2ctl("peek", "--rva", ",".join(f"{key}:u64" for key in keys),
                    pid=ctx.inst(index).pid)["samples"][0]
    pointers = [int(sample[key], 16) for key in keys]
    entities = ctx.entities(index)
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
         "align_courtyard_exit": step_align_courtyard_exit}


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
        report["seconds"] = round(time.monotonic() - t0, 1)
        report["instances"] = [inst.pid for inst in ctx.instances]
        (run_dir / "report.json").write_text(json.dumps(report, indent=2))
        (run_dir / "report.md").write_text(render_md(report))
    report["dir"] = str(run_dir)
    return report


def render_md(report: dict) -> str:
    lines = [f"# {report['scenario']} (attempt {report['attempt']}): {report['status'].upper()}", "",
             f"Started {report['started']}, {report.get('seconds', '?')} s, instances {report.get('instances')}.", ""]
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
    required = {"warp": ("world", "room"), "press": ("button",),
                "assert": ("expr",), "wait_until": ("expr",), "save": ("as", "expr"),
                "runtime": ("role",), "record": ("as",), "record_stop": ("as",),
                "kh2ctl": ("args",), "align_courtyard_exit": ("blockedExpr",)}
    for number, step in enumerate(scenario["steps"]):
        prefix = f"step {number}"
        if not isinstance(step, dict) or step.get("do") not in STEPS:
            raise ValueError(f"{prefix}: unknown or missing do")
        kind = step["do"]
        for key in required.get(kind, ()):
            if key not in step:
                raise ValueError(f"{prefix} ({kind}): missing {key}")
        if kind in ("boot", "launch"):
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
        if "expr" in step:
            compile(step["expr"], f"<{prefix}>", "eval")
        if "blockedExpr" in step:
            compile(step["blockedExpr"], f"<{prefix} blockedExpr>", "eval")
        if kind == "runtime" and step["role"] not in ("player", "friend1", "friend2", "spectator"):
            raise ValueError(f"{prefix}: invalid runtime role")


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
