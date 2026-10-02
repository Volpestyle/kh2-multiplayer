from __future__ import annotations

import contextvars
import functools
import inspect
import json
import os
import subprocess
from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP


REPO_ROOT = Path(__file__).resolve().parents[2]


def _candidate_bins() -> list[Path]:
    env_bin = os.getenv("KH2CTL_BIN")
    candidates: list[Path] = []
    if env_bin:
        candidates.append(Path(env_bin))

    candidates.extend(
        [
            REPO_ROOT / "build" / "kh2ctl.exe",
            REPO_ROOT / "build" / "Release" / "kh2ctl.exe",
            REPO_ROOT / "build" / "Debug" / "kh2ctl.exe",
            REPO_ROOT / "build" / "tools" / "kh2ctl" / "kh2ctl.exe",
            REPO_ROOT / "build" / "tools" / "kh2ctl" / "Release" / "kh2ctl.exe",
            REPO_ROOT / "build" / "tools" / "kh2ctl" / "Debug" / "kh2ctl.exe",
        ]
    )
    return candidates


def _find_kh2ctl() -> Path:
    for candidate in _candidate_bins():
        if candidate.exists():
            return candidate
    raise FileNotFoundError(
        "kh2ctl.exe not found. Build the `kh2ctl` target or set KH2CTL_BIN."
    )


# Instance chosen by the `pid` argument of the game tools (see _with_pid).
_target_pid: contextvars.ContextVar[int | None] = contextvars.ContextVar(
    "target_pid", default=None
)


def _run_kh2ctl(*args: str) -> dict[str, Any]:
    exe = _find_kh2ctl()
    pid = _target_pid.get()
    pid_args = ["--pid", str(pid)] if pid is not None else []
    result = subprocess.run(
        [str(exe), *args, *pid_args],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )

    payload = result.stdout.strip()
    if not payload:
        raise RuntimeError(result.stderr.strip() or "kh2ctl produced no output")

    try:
        data = json.loads(payload)
    except json.JSONDecodeError as exc:
        raise RuntimeError(
            f"Failed to parse kh2ctl output: {payload}"
        ) from exc

    return data


def _bool_flag(flag: str, enabled: bool) -> list[str]:
    return [flag] if enabled else []


def _with_pid(fn):
    """Adds an optional `pid` argument that targets one KH2 instance.

    kh2ctl refuses game commands without --pid when several instances run.
    """
    sig = inspect.signature(fn)
    pid_param = inspect.Parameter(
        "pid", inspect.Parameter.KEYWORD_ONLY, default=None, annotation=int | None
    )

    @functools.wraps(fn)
    def wrapper(*args, pid: int | None = None, **kwargs):
        token = _target_pid.set(pid)
        try:
            return fn(*args, **kwargs)
        finally:
            _target_pid.reset(token)

    params = [
        p.replace(kind=inspect.Parameter.KEYWORD_ONLY)
        if p.kind == inspect.Parameter.POSITIONAL_OR_KEYWORD
        else p
        for p in sig.parameters.values()
    ]
    wrapper.__signature__ = sig.replace(parameters=[*params, pid_param])
    wrapper.__annotations__ = {**fn.__annotations__, "pid": int | None}
    return wrapper


mcp = FastMCP(
    "kh2ctl",
    instructions=(
        "Structured control surface for KH2 local testing. Prefer wait/state "
        "tools to polling manually. Player tools drive native slot-0 raw input "
        "through the inject DLL; friend tools drive the existing mailbox AI "
        "replacement path; save-menu helpers still use keyboard."
    ),
)


@mcp.tool(
    description=(
        "Launch a new KH2 instance and inject the current inject DLL build. "
        "Reports the process id, per-PID log path and installed hooks."
    )
)
def launch_kh2(no_inject: bool = False, init_timeout_ms: int = 15000) -> dict[str, Any]:
    return _run_kh2ctl(
        "launch",
        *_bool_flag("--no-inject", no_inject),
        "--init-timeout-ms",
        str(init_timeout_ms),
    )


@mcp.tool(description="Inject the current inject DLL build into a running KH2 process.")
def inject_kh2(pid: int, init_timeout_ms: int = 15000) -> dict[str, Any]:
    return _run_kh2ctl(
        "inject", "--pid", str(pid), "--init-timeout-ms", str(init_timeout_ms)
    )


@mcp.tool(description="List KH2 processes and whether the rig launched (owns) each.")
def list_instances() -> dict[str, Any]:
    return _run_kh2ctl("instances")


@mcp.tool(
    description=(
        "Kill rig-launched KH2 processes (one pid, or all). KH2 processes the "
        "rig did not launch are never killed and are reported as skippedUnowned."
    )
)
def kill_kh2(pid: int | None = None) -> dict[str, Any]:
    if pid is None:
        return _run_kh2ctl("kill", "--all")
    return _run_kh2ctl("kill", "--pid", str(pid))


@mcp.tool(
    description=(
        "Restart KH2 through the rig: refuse if a KH2 the rig didn't launch is "
        "running, kill rig-launched instances, rebuild the inject DLL, then "
        "launch and inject."
    )
)
def restart_kh2(
    no_build: bool = False,
    kill: bool = False,
    no_inject: bool = False,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "restart",
        *_bool_flag("--no-build", no_build),
        *_bool_flag("--kill", kill),
        *_bool_flag("--no-inject", no_inject),
    )


@mcp.tool(description="Read current KH2 room and actor state.")
@_with_pid
def get_state() -> dict[str, Any]:
    return _run_kh2ctl("state")


@mcp.tool(description="Wait until KH2 is at the title/loading state.")
@_with_pid
def wait_title(timeout_ms: int = 60000, poll_ms: int = 250) -> dict[str, Any]:
    return _run_kh2ctl(
        "wait-title",
        "--timeout-ms",
        str(timeout_ms),
        "--poll-ms",
        str(poll_ms),
    )


@mcp.tool(description="Wait until KH2 is in a live room, not title/loading.")
@_with_pid
def wait_ingame(timeout_ms: int = 60000, poll_ms: int = 250) -> dict[str, Any]:
    return _run_kh2ctl(
        "wait-ingame",
        "--timeout-ms",
        str(timeout_ms),
        "--poll-ms",
        str(poll_ms),
    )


@mcp.tool(description="Wait until KH2 reaches a specific world/room.")
@_with_pid
def wait_room(
    world: int,
    room: int,
    timeout_ms: int = 60000,
    poll_ms: int = 250,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "wait-room",
        "--world",
        str(world),
        "--room",
        str(room),
        "--timeout-ms",
        str(timeout_ms),
        "--poll-ms",
        str(poll_ms),
    )


@mcp.tool(description="Focus the KH2 game window.")
@_with_pid
def focus_game() -> dict[str, Any]:
    return _run_kh2ctl("focus")


@mcp.tool(description="Tap a keyboard key against KH2, focusing first by default.")
@_with_pid
def tap_key(key: str, duration_ms: int = 60, focus: bool = True) -> dict[str, Any]:
    args = ["tap-key", "--key", key, "--duration-ms", str(duration_ms)]
    if not focus:
        args.append("--no-focus")
    return _run_kh2ctl(*args)


@mcp.tool(description="Hold a keyboard key against KH2 for a fixed duration.")
@_with_pid
def hold_key(key: str, duration_ms: int = 500, focus: bool = True) -> dict[str, Any]:
    args = ["hold-key", "--key", key, "--duration-ms", str(duration_ms)]
    if not focus:
        args.append("--no-focus")
    return _run_kh2ctl(*args)


@mcp.tool(description="Drive the save-load menu using keyboard confirm/down keys.")
@_with_pid
def load_save(
    slot: int,
    confirm_key: str = "enter",
    down_key: str = "down",
    wake_presses: int = 1,
    wake_delay_ms: int = 1000,
    step_delay_ms: int = 250,
    post_select_delay_ms: int = 800,
    final_confirm_presses: int = 1,
    load_timeout_ms: int = 60000,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "load-save",
        "--slot",
        str(slot),
        "--confirm-key",
        confirm_key,
        "--down-key",
        down_key,
        "--wake-presses",
        str(wake_presses),
        "--wake-delay-ms",
        str(wake_delay_ms),
        "--step-delay-ms",
        str(step_delay_ms),
        "--post-select-delay-ms",
        str(post_select_delay_ms),
        "--final-confirm-presses",
        str(final_confirm_presses),
        "--load-timeout-ms",
        str(load_timeout_ms),
    )


@mcp.tool(
    description=(
        "Restart KH2 through the rig (see restart_kh2), wait for title/loading, select a save slot, and wait "
        "until gameplay is live."
    )
)
def boot_load_save(
    slot: int,
    no_build: bool = False,
    no_inject: bool = False,
    confirm_key: str = "enter",
    down_key: str = "down",
    title_timeout_ms: int = 60000,
    wake_presses: int = 1,
    wake_delay_ms: int = 1000,
    step_delay_ms: int = 250,
    post_select_delay_ms: int = 800,
    final_confirm_presses: int = 1,
    load_timeout_ms: int = 60000,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "boot-load-save",
        "--slot",
        str(slot),
        *_bool_flag("--no-build", no_build),
        *_bool_flag("--no-inject", no_inject),
        "--confirm-key",
        confirm_key,
        "--down-key",
        down_key,
        "--title-timeout-ms",
        str(title_timeout_ms),
        "--wake-presses",
        str(wake_presses),
        "--wake-delay-ms",
        str(wake_delay_ms),
        "--step-delay-ms",
        str(step_delay_ms),
        "--post-select-delay-ms",
        str(post_select_delay_ms),
        "--final-confirm-presses",
        str(final_confirm_presses),
        "--load-timeout-ms",
        str(load_timeout_ms),
    )


@mcp.tool(description="Send a raw slot-0/player input pulse through the inject DLL.")
@_with_pid
def player_input(
    duration_ms: int = 100,
    lx: float = 0.0,
    ly: float = 0.0,
    rx: float = 0.0,
    ry: float = 0.0,
    buttons: str = "",
) -> dict[str, Any]:
    args = [
        "player-input",
        "--duration-ms",
        str(duration_ms),
        "--lx",
        str(lx),
        "--ly",
        str(ly),
        "--rx",
        str(rx),
        "--ry",
        str(ry),
    ]
    if buttons:
        args.extend(["--buttons", buttons])
    return _run_kh2ctl(*args)


@mcp.tool(description="Move the local player with a slot-0 left-stick pulse.")
@_with_pid
def player_move(
    x: float = 0.0,
    y: float = 1.0,
    duration_ms: int = 500,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "player-move",
        "--x",
        str(x),
        "--y",
        str(y),
        "--duration-ms",
        str(duration_ms),
    )


@mcp.tool(description="Press a raw slot-0/player controller button.")
@_with_pid
def player_press(button: str, duration_ms: int = 100) -> dict[str, Any]:
    return _run_kh2ctl(
        "player-press",
        "--button",
        button,
        "--duration-ms",
        str(duration_ms),
    )


@mcp.tool(description="Send a raw friend-slot mailbox input pulse.")
@_with_pid
def friend_input(
    slot: str,
    duration_ms: int = 100,
    lx: float = 0.0,
    ly: float = 0.0,
    rx: float = 0.0,
    ry: float = 0.0,
    buttons: str = "",
    target_id: int = 0,
) -> dict[str, Any]:
    args = [
        "input",
        "--slot",
        slot,
        "--duration-ms",
        str(duration_ms),
        "--lx",
        str(lx),
        "--ly",
        str(ly),
        "--rx",
        str(rx),
        "--ry",
        str(ry),
    ]
    if buttons:
        args.extend(["--buttons", buttons])
    if target_id:
        args.extend(["--target-id", str(target_id)])
    return _run_kh2ctl(*args)


@mcp.tool(description="Move Friend1 or Friend2 with a left-stick pulse.")
@_with_pid
def friend_move(
    slot: str,
    x: float = 0.0,
    y: float = 1.0,
    duration_ms: int = 500,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "move",
        "--slot",
        slot,
        "--x",
        str(x),
        "--y",
        str(y),
        "--duration-ms",
        str(duration_ms),
    )


@mcp.tool(description="Press a single friend-slot action button.")
@_with_pid
def friend_press(slot: str, button: str, duration_ms: int = 100) -> dict[str, Any]:
    return _run_kh2ctl(
        "press",
        "--slot",
        slot,
        "--button",
        button,
        "--duration-ms",
        str(duration_ms),
    )


@mcp.tool(
    description=(
        "Screenshot one KH2 instance from inside its renderer (works when the "
        "window is hidden or unfocused). Returns the PNG path."
    )
)
@_with_pid
def capture_screenshot(out: str = "") -> dict[str, Any]:
    return _run_kh2ctl("capture", *(["--out", out] if out else []))


@mcp.tool(
    description=(
        "Record a short MP4 clip of one KH2 instance from inside its renderer. "
        "Reports the game's fps before and during capture."
    )
)
@_with_pid
def capture_clip(seconds: float = 3.0, fps: int = 30, out: str = "") -> dict[str, Any]:
    return _run_kh2ctl(
        "clip", "--seconds", str(seconds), "--fps", str(fps),
        *(["--out", out] if out else []),
    )


@mcp.tool(description="Turn the in-game debug overlay (pid, frame, world/room, fps) on or off.")
@_with_pid
def set_overlay(on: bool = True) -> dict[str, Any]:
    return _run_kh2ctl("overlay", "on" if on else "off")


@mcp.tool(description="Measure one KH2 instance's frame rate.")
@_with_pid
def get_fps(window_ms: int = 2000) -> dict[str, Any]:
    return _run_kh2ctl("fps", "--window-ms", str(window_ms))


@mcp.tool(description="Mute or unmute one KH2 instance's audio.")
def mute_kh2(pid: int, mute: bool = True) -> dict[str, Any]:
    return _run_kh2ctl("mute", "--pid", str(pid), *([] if mute else ["--off"]))


if __name__ == "__main__":
    mcp.run(transport="stdio")
