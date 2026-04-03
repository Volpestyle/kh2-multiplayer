from __future__ import annotations

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


def _run_kh2ctl(*args: str) -> dict[str, Any]:
    exe = _find_kh2ctl()
    result = subprocess.run(
        [str(exe), *args],
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


mcp = FastMCP(
    "kh2ctl",
    instructions=(
        "Structured control surface for KH2 local testing. Prefer wait/state "
        "tools to polling manually. Player tools drive native slot-0 raw input "
        "through the inject DLL; friend tools drive the existing mailbox AI "
        "replacement path; save-menu helpers still use keyboard."
    ),
)


@mcp.tool(description="Restart KH2 using the repo restart script.")
def restart_kh2(
    no_build: bool = False,
    kill: bool = False,
    copy_dll: bool = False,
    steam: bool = False,
) -> dict[str, Any]:
    return _run_kh2ctl(
        "restart",
        *_bool_flag("--no-build", no_build),
        *_bool_flag("--kill", kill),
        *_bool_flag("--copy-dll", copy_dll),
        *_bool_flag("--steam", steam),
    )


@mcp.tool(description="Read current KH2 room and actor state.")
def get_state() -> dict[str, Any]:
    return _run_kh2ctl("state")


@mcp.tool(description="Wait until KH2 is at the title/loading state.")
def wait_title(timeout_ms: int = 60000, poll_ms: int = 250) -> dict[str, Any]:
    return _run_kh2ctl(
        "wait-title",
        "--timeout-ms",
        str(timeout_ms),
        "--poll-ms",
        str(poll_ms),
    )


@mcp.tool(description="Wait until KH2 is in a live room, not title/loading.")
def wait_ingame(timeout_ms: int = 60000, poll_ms: int = 250) -> dict[str, Any]:
    return _run_kh2ctl(
        "wait-ingame",
        "--timeout-ms",
        str(timeout_ms),
        "--poll-ms",
        str(poll_ms),
    )


@mcp.tool(description="Wait until KH2 reaches a specific world/room.")
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
def focus_game() -> dict[str, Any]:
    return _run_kh2ctl("focus")


@mcp.tool(description="Tap a keyboard key against KH2, focusing first by default.")
def tap_key(key: str, duration_ms: int = 60, focus: bool = True) -> dict[str, Any]:
    args = ["tap-key", "--key", key, "--duration-ms", str(duration_ms)]
    if not focus:
        args.append("--no-focus")
    return _run_kh2ctl(*args)


@mcp.tool(description="Hold a keyboard key against KH2 for a fixed duration.")
def hold_key(key: str, duration_ms: int = 500, focus: bool = True) -> dict[str, Any]:
    args = ["hold-key", "--key", key, "--duration-ms", str(duration_ms)]
    if not focus:
        args.append("--no-focus")
    return _run_kh2ctl(*args)


@mcp.tool(description="Drive the save-load menu using keyboard confirm/down keys.")
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
        "Restart KH2, wait for title/loading, select a save slot, and wait "
        "until gameplay is live."
    )
)
def boot_load_save(
    slot: int,
    no_build: bool = False,
    copy_dll: bool = False,
    steam: bool = False,
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
        *_bool_flag("--copy-dll", copy_dll),
        *_bool_flag("--steam", steam),
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
def player_press(button: str, duration_ms: int = 100) -> dict[str, Any]:
    return _run_kh2ctl(
        "player-press",
        "--button",
        button,
        "--duration-ms",
        str(duration_ms),
    )


@mcp.tool(description="Send a raw friend-slot mailbox input pulse.")
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


if __name__ == "__main__":
    mcp.run(transport="stdio")
