# Developer setup

For an unpacked player preview, start with [the player guide](docs/FRIEND_PLAYTEST_STEAM.md).
This page covers building and developing the Windows Steam Global mod.

## Requirements

- A legally installed **KINGDOM HEARTS HD 1.5+2.5 ReMIX**, KH2 Final Mix **Steam Global**, and Steam running for Steam sessions. Epic, Steam JP and PCSX2 are not supported by the current runtime offsets.
- Visual Studio Build Tools with a C++20-capable MSVC x64 toolchain, Windows SDK and MASM (`ml64`).
- CMake 3.20+, Git, and Python 3.11+ for scenarios and developer tooling. The Windows Python installer includes Tkinter for the launcher.
- An interactive Windows desktop for KH2. Session 0 cannot run the game.

Cheat Engine and Ghidra are optional reverse-engineering tools, not requirements
for building, launching or injecting the mod. OpenKH, the KH2 Lua Library and the
other reference repositories in [AGENTS.md](AGENTS.md) are likewise optional;
they are not build dependencies.

## Clone and build

Run from a Visual Studio x64 developer shell:

```powershell
git clone https://github.com/Volpestyle/kh2-multiplayer.git
cd kh2-multiplayer
cmake -S . -B build -A x64
cmake --build build --config Release
```

CMake fetches ENet and, on Windows, MinHook. An offline build can supply their
existing source directories through `FETCHCONTENT_SOURCE_DIR_ENET` and
`FETCHCONTENT_SOURCE_DIR_MINHOOK` with `FETCHCONTENT_FULLY_DISCONNECTED=ON`.

| Target | Release output |
|---|---|
| `kh2coop_inject` | `build/inject/staging/kh2coop_inject.dll` |
| `kh2coop_runtime_scaffold` | `build/Release/kh2coop_runtime_scaffold.exe` |
| `kh2coop_server` | `build/Release/kh2coop_server.exe` |
| `kh2coop_fake_sim` | `build/Release/kh2coop_fake_sim.exe` |
| `avatarctl` | `build/Release/avatarctl.exe` |
| `kh2ctl` | `build/tools/kh2ctl/Release/kh2ctl.exe` |

These paths assume the Visual Studio multi-configuration generator. Source uses
protocol **29**, AvatarBridge **4**, WorldBridge **12** and CaptureChannel **1**.
Rebuild the DLL, runtime, relay and control tools together; an already injected
DLL retains its old build. Packaged products need a matching receipt cohort.
The checked-in protocol-18 product manifest and launcher validator are historical;
packaging current source requires updating both with reviewed products.
See [packaging](tools/packaging/README.md).

## Verify without KH2

```powershell
ctest --test-dir build -C Release --output-on-failure
.\build\Release\kh2coop_fake_sim.exe
```

Run CTest serially: some headless tests bind fixed loopback ports. The fake
simulation exercises networking without attaching to the game. These checks do
not establish live gameplay compatibility.

## Run and configure

Use [the development workflow](docs/DEVELOPMENT_WORKFLOW.md) for launch,
injection, scenario runs, desktop-session requirements and save protection.
Read the `kh2-rig` skill before any live automated run. Add the built `kh2ctl`
directory to PATH for the short commands in that workflow.

```powershell
Copy-Item kh2coop_runtime.ini.example kh2coop_runtime.ini
```

The standalone runtime starts offline with camera override enabled. Its example
ENet port is **7946**, while the standalone server defaults to **7782**; set the
same explicit port and matching game/content/mod identifiers on both sides.
See [runtime](runtime/README.md) and [server](server/README.md) for CLI options.
The friend launcher selects the player defaults (including Normal difficulty)
for its children; direct developer launches retain lower-level defaults.

For a packaged player build, [friend.py](tools/launcher/README.md) handles owned
game launch, injection and Steam account confirmation. The older developer UI
`pythonw tools/launcher/launcher.py` expects an already injected game and uses ENet.

## Optional analysis and MCP tools

- [Lessons learned](docs/LESSONS_LEARNED.md): read before reverse engineering.
- [OpenKH reference](docs/OPENKH_REFERENCE.md): file formats, animation/entity IDs and party mapping; not runtime offsets.
- `scripts/ghidra.ps1`: saved-project headless decompilation and xrefs. Coordinate access to the same Ghidra project; do not remove another reader's lock.
- Cheat Engine MCP: call `ping` and `get_process_info` before memory operations and verify attachment to the intended live KH2 PID.
- [KH2 control CLI/MCP](docs/KH2_CONTROL_CLI.md): canonical controls and wrapper setup. The wrapper is `tools/mcp_kh2ctl/server.py`; set `KH2CTL_BIN` to the absolute built CLI path if discovery needs an override.

Use the scenario runner's checked `boot` step to load saves during automation;
`boot-load-save` is known broken. Never save in-game during automation without
James's explicit approval. Launch, restart and kill only rig-owned processes
through `kh2ctl`; do not use `scripts/restart-kh2.ps1` from an agent.
