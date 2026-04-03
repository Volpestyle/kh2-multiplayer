# Codebase Map

Quick reference for what lives where and how the pieces connect.

## Build targets

```
cmake --build build --config Release                    # everything
cmake --build build --target kh2coop_inject --config Release   # inject DLL only
cmake --build build --target kh2ctl --config Release           # CLI tool only
cmake --build build --target kh2coop_server --config Release   # server only
cmake --build build --target kh2coop_fake_sim --config Release # E2E test
```

Dependencies: ENet v1.3.17 (FetchContent), MinHook v1.3.3 (FetchContent, inject only).

## Directory layout

```
kh2-multiplayer/
  inject/          ← DLL injected into KH2 process (friend control)
  runtime/         ← External process that reads KH2 memory + bridges to network
  server/          ← Host-authoritative multiplayer session server
  common/          ← Shared types, codec, protocol, networking, IPC mailbox
  tools/           ← kh2ctl CLI + MCP wrapper
  scripts/         ← PowerShell helpers (restart-kh2, run MCP server)
  tests/           ← E2E integration tests (no KH2 required)
  docs/            ← All documentation
  content/         ← Content/mod packaging notes
```

## inject/ — In-process DLL (the friend control system)

Loaded into the KH2 process via CE Lua injection or Panacea plugin. Hooks the entity update loop to replace friend AI with player input.

| File | Lines | What it does |
|------|-------|-------------|
| `src/EntityHook.cpp` | ~1930 | **The big file.** All hook logic — see section breakdown below |
| `src/EntityHook.hpp` | 28 | Public API: `Initialize()`, `Shutdown()`, `OnFrame()` |
| `src/DllMain.cpp` | 217 | DLL entry point, Panacea plugin exports, standalone init thread |
| `src/PatternScan.hpp` | 126 | AOB pattern scanner for finding functions in the .text section |

### EntityHook.cpp sections

| Section | Lines | What |
|---------|-------|------|
| Function pointer typedefs | ~50-130 | Ghidra-derived signatures for all hooked game functions |
| AOB signatures + RVAs | ~135-205 | Pattern bytes and fallback addresses for hook targets |
| Actor struct offsets | ~207-232 | Velocity, acceleration, follow-timer, animation fields |
| Global state | ~250-370 | Hook trampolines, friend tracking, gamepad state, mode flags |
| Input reading | ~415-760 | KH2 raw input buffer parsing, stick normalization, deadzone |
| Mailbox polling | ~530-630 | Shared-memory IPC from runtime process |
| Camera retargeting | ~815-875 | Redirect camStruct+0x50 to friend actor |
| Movement injection | ~920-1085 | Camera-relative stick-to-world, velocity/facing writes |
| **HookedMotionChainSetAnim** | ~1108-1180 | Blocks per-frame animation resets, guards our own calls |
| **HookedFriendAI** | ~1240-1335 | Skips vanilla AI, sets animation on stick transitions |
| HookedFollowSteering | ~1345-1375 | Returns zero vector to disable tether |
| HookedPerEntityUpdate | ~1510-1645 | Main hook: frame boundary, friend ID, gamepad reads, post-update |
| Initialize/Shutdown | ~1650-1930 | MinHook setup, AOB scan, hook installation, mailbox init |

## runtime/ — External process (memory reader + network bridge)

Attaches to KH2 via `ReadProcessMemory`/`WriteProcessMemory`. Reads game state, manages camera (via fake actor allocation), and connects to the session server.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/KH2Offsets.hpp` | 475 | **Master offset map** — all confirmed memory addresses |
| `include/kh2coop/GameBridgePC.hpp` | 146 | Concrete `IGameBridge` for KH2 PC |
| `include/kh2coop/IGameBridge.hpp` | 26 | Abstract game memory interface |
| `include/kh2coop/CameraController.hpp` | 74 | Per-frame camera override with room transition handling |
| `include/kh2coop/ReplicaController.hpp` | 81 | Applies incoming snapshots to non-owned entity slots |
| `src/GameBridgePC.cpp` | 1032 | Process attach, entity discovery, state reads/writes, camera |
| `src/RuntimeMain.cpp` | 996 | Main loop: config, per-frame tick, network client, mailbox IPC |

## server/ — Multiplayer session server

Accepts client connections, assigns party slots, relays input, runs lightweight simulation, broadcasts snapshots.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/SessionHost.hpp` | 129 | Session manager with peer lifecycle and slot assignment |
| `include/kh2coop/SimulationState.hpp` | 48 | Server-side fake physics for 3 actors |
| `include/kh2coop/PeerState.hpp` | 43 | Per-peer tracking (slot, status, heartbeat) |
| `src/ServerMain.cpp` | 133 | Entry point, 60fps main loop |
| `src/SessionHost.cpp` | 603 | Full session host: handshake, validation, routing, broadcasts |
| `src/SimulationState.cpp` | 213 | Input-driven movement, gravity, action timers |

## common/ — Shared library

Used by all components. Defines the wire protocol, domain types, serialization, and IPC.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/Types.hpp` | 159 | Vec3, ActorState, InputFrame, SlotType, etc. |
| `include/kh2coop/Protocol.hpp` | 147 | Protocol v1 messages + v2 forward declarations |
| `include/kh2coop/Codec.hpp` | 102 | PacketType enum, binary encode/decode declarations |
| `include/kh2coop/ByteBuffer.hpp` | 140 | Little-endian byte writer/reader |
| `include/kh2coop/NetworkClient.hpp` | 98 | ENet client with callback dispatch |
| `include/kh2coop/InputMailbox.hpp` | 412 | Cross-process shared memory IPC (seqlock, 3 slots) |
| `src/Codec.cpp` | 444 | All serialization implementations |
| `src/NetworkClient.cpp` | 230 | ENet connect/tick/send/disconnect |

## tools/ — Developer tooling

| File | What |
|------|------|
| `kh2ctl/src/main.cpp` (1498 lines) | CLI for KH2 control: process attach, state queries, save loading, input injection |
| `mcp_kh2ctl/server.py` (372 lines) | Python MCP server wrapping kh2ctl for agent use |

## tests/

| File | What |
|------|------|
| `FakeSimulation.cpp` (772 lines) | E2E test: 3 clients + server, verifies handshake, input exchange, snapshot consistency, event delivery |

## scripts/

| File | What |
|------|------|
| `restart-kh2.ps1` | Kill/rebuild/relaunch KH2 bypassing Steam launcher |
| `run-kh2ctl-mcp.ps1` | Launch the kh2ctl MCP server |

## How the pieces connect

```
                    ┌──────────────────┐
                    │   KH2 Process    │
                    │                  │
                    │  ┌────────────┐  │     shared memory
                    │  │ inject DLL │◄─┼──── (InputMailbox) ◄── runtime process
                    │  └────────────┘  │                         │
                    │        │         │                         │ ENet
                    │   hooks entity   │                         │
                    │   update loop    │                    ┌────▼────┐
                    │                  │                    │ server  │
                    └──────────────────┘                    └─────────┘
                                                                │
                                                           ENet │
                                                                │
                                                        ┌───────▼───────┐
                                                        │ other clients │
                                                        └───────────────┘
```

- **inject DLL** runs inside KH2, hooks entity updates, reads input from mailbox or local gamepads
- **runtime** runs outside KH2, reads game state via ReadProcessMemory, writes input to the mailbox, connects to server
- **server** relays input/snapshots between all connected clients
- **kh2ctl** is a developer tool that talks to the runtime's mailbox and KH2's memory
