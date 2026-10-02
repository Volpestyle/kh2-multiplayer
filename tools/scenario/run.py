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

def kh2ctl(*args: str, pid: int | None = None, check: bool = True, timeout: float = 120) -> dict:
    cmd = [str(KH2CTL), *map(str, args)]
    if pid is not None:
        cmd += ["--pid", str(pid)]
    proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
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
        return kh2ctl("entities", pid=self.inst(index).pid)["actors"]

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

        def enemies(index: int = 0):
            return [a for a in self.actors(index) if a.get("objectType") in (3, 4)]

        def log_count(pattern: str, index: int = 0) -> int:
            path = LOGS / f"kh2coop_inject_{self.inst(index).pid}.log"
            return sum(pattern in line for line in path.read_text(errors="replace").splitlines())

        def dist(a, b) -> float:
            return math.dist(a, b)

        return {"peek": peek, "pos": pos, "room": room, "enemies": enemies, "actor": self.actor,
                "actors": self.actors, "log_count": log_count, "dist": dist, "saved": self.saved,
                "len": len, "abs": abs, "min": min, "max": max, "any": any, "all": all}

    def eval(self, expr: str):
        return eval(expr, {"__builtins__": {}}, self.namespace())  # noqa: S307 (repo-owned scenarios)

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
    data = kh2ctl("launch")
    inst = Instance(len(ctx.instances), data["processId"])
    ctx.instances.append(inst)
    if step.get("mute", True):
        kh2ctl("mute", pid=inst.pid, check=False)
    return {"processId": inst.pid, "instance": inst.index}


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
        kh2ctl("tap-key", "--key", "down", pid=inst.pid)
        ctx.sleep(0.7)
    else:
        raise StepFailed("LOAD never highlighted")
    kh2ctl("tap-key", "--key", "enter", pid=inst.pid)
    ctx.sleep(2)

    def in_save_list() -> bool:
        rows = title_rows(ctx, inst, shot)
        return not rows["new"] and not rows["load"]

    wait_for(ctx, in_save_list, "save list", timeout)
    ctx.sleep(1)
    kh2ctl("tap-key", "--key", "enter", pid=inst.pid)

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


STEPS = {"boot": step_boot, "launch": step_launch, "warp": step_warp, "input": step_input,
         "press": step_press, "wait": step_wait, "wait_until": step_wait_until,
         "assert": step_assert, "save": step_save, "protect": step_protect,
         "capture": step_capture, "clip": step_clip, "crash": step_crash, "freeze": step_freeze}


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

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("scenarios", nargs="+", type=Path)
    parser.add_argument("--repeat", type=int, default=1)
    args = parser.parse_args()

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
