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

Offline test suites (no KH2): `kh2coop_fake_sim`, `kh2coop_avatar_test`,
`kh2coop_world_test`, `kh2coop_bridge_test` — each prints `ALL CHECKS PASSED`.

Dependencies: ENet v1.3.17 (FetchContent), MinHook v1.3.3 (FetchContent, inject only).

## Directory layout

```
kh2-multiplayer/
  inject/          ← DLL injected into KH2 process (friend control)
  runtime/         ← External process that reads KH2 memory + bridges to network
  server/          ← Host-authoritative multiplayer session server
  common/          ← Shared types, codec, protocol, networking, IPC mailbox
  tools/           ← kh2ctl CLI + MCP wrapper, avatarctl, headless Ghidra scripts
  scripts/         ← PowerShell helpers (restart-kh2, run MCP server, ghidra)
  tests/           ← E2E integration tests (no KH2 required)
  docs/            ← All documentation
  content/         ← Content/mod packaging notes
```

## inject/ — In-process DLL (the friend control system)

Loaded into the KH2 process by `kh2ctl launch`/`inject` (Cheat Engine is a fallback). Hooks the entity update loop to replace friend AI with player input.

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

## server/ — Multiplayer session server (relay)

Accepts client connections, version-gates them and assigns party slots. Relays
owner-authoritative avatars (stamping the owner slot) and host-authored world
sync (room transitions, cutscene holds, enemy manifest/HP/deaths, progress),
which it accepts only from the host (slot Player). Routes hit claims to the
host only. Caches world state so late joiners are caught up. The older
fake-physics `SimulationState` is a test double.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/SessionHost.hpp` | 129 | Session manager with peer lifecycle and slot assignment |
| `include/kh2coop/SimulationState.hpp` | 48 | Server-side fake physics for 3 actors |
| `include/kh2coop/PeerState.hpp` | 43 | Per-peer tracking (slot, status, heartbeat) |
| `src/ServerMain.cpp` | 133 | Entry point, 60fps main loop |
| `src/SessionHost.cpp` | ~810 | Handshake, validation, avatar relay, host-only world sync, claim routing, late-joiner catch-up |
| `src/SimulationState.cpp` | 213 | Input-driven movement, gravity, action timers |

## common/ — Shared library

Used by all components. Defines the wire protocol, domain types, serialization, and IPC.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/Types.hpp` | ~190 | Vec3, ActorState, AvatarState, InputFrame, SlotType, etc. |
| `include/kh2coop/Protocol.hpp` | ~250 | Session messages, clock sync, world sync (RoomTransition, EventHold, EnemyManifest/Hp/Death, HitClaim, TransitionAck, ProgressUpdate) |
| `include/kh2coop/Codec.hpp` | ~170 | PacketType enum, encode/decode declarations, `isWorldPacket` |
| `include/kh2coop/ByteBuffer.hpp` | 140 | Little-endian byte writer/reader |
| `include/kh2coop/NetworkClient.hpp` | ~155 | ENet client: callbacks, avatars, clock sync, world sync, raw packets, link-conditioner test hook |
| `include/kh2coop/LinkConditioner.hpp` | ~90 | Seeded latency/jitter/loss per direction for repeatable network tests |
| `include/kh2coop/AvatarInterpolator.hpp` | ~115 | Per-avatar snapshot buffer sampled at a render delay |
| `include/kh2coop/AvatarSync.hpp` | ~95 | Remote avatars to the two friend-slot puppets, with visibility rules |
| `include/kh2coop/AvatarBridge.hpp` | ~170 | Shared memory DLL/runtime: local avatar out, puppet poses in (seqlock) |
| `include/kh2coop/PacketRing.hpp` | ~125 | Lock-free SPSC ring of variable-length packets in shared memory |
| `include/kh2coop/WorldBridge.hpp` | ~110 | Shared memory DLL/runtime for world events (two PacketRings) |
| `include/kh2coop/WorldPump.hpp` | ~55 | Runtime's bridge-to-network forwarding for world packets |
| `include/kh2coop/ProgressMirror.hpp` | ~145 | Host story-flag diff/snapshot; client allow-list accept + re-assert |
| `include/kh2coop/ProgressAllowList.hpp` | ~40 | Candidate (unverified) save-body ranges to mirror |
| `include/kh2coop/InputMailbox.hpp` | 412 | Cross-process shared memory IPC (seqlock, 3 slots) |
| `src/Codec.cpp` | ~730 | All serialization implementations |
| `src/NetworkClient.cpp` | ~405 | ENet connect/tick/send, clock sync, conditioned send/receive |

## tools/ — Developer tooling

| File | What |
|------|------|
| `kh2ctl/src/main.cpp` (1498 lines) | CLI for KH2 control: process attach, state queries, save loading, input injection |
| `mcp_kh2ctl/server.py` (372 lines) | Python MCP server wrapping kh2ctl for agent use |
| `avatarctl/main.cpp` | Drive an AvatarBridge without a network: `synth`, `record`/`replay`, `fake-local`, `peek` |
| `ghidra/*.java` | Headless Ghidra scripts behind `scripts/ghidra.ps1` (decompile, xrefs, strings, symbols) |

## tests/

| File | What |
|------|------|
| `FakeSimulation.cpp` (772 lines) | E2E test: 3 clients + server, verifies handshake, input exchange, snapshot consistency, event delivery |
| `AvatarRelayTest.cpp` | Avatar codec, LinkConditioner, interpolation, AvatarSync, AvatarBridge; 3 clients at 100 ms + 2% loss with skewed clocks |
| `WorldSyncTest.cpp` | Host-only world sync, claim routing, late-joiner catch-up, acks, ProgressMirror and allow-list policy |
| `BridgeTest.cpp` | PacketRing (incl. two-thread stress) and fake DLL to relay to fake DLL world events |

## scripts/

| File | What |
|------|------|
| `restart-kh2.ps1` | Kill/rebuild/relaunch KH2 bypassing Steam launcher |
| `run-kh2ctl-mcp.ps1` | Launch the kh2ctl MCP server |
| `ghidra.ps1` | Headless Ghidra: `-Setup`, `-Decompile`, `-Xrefs`, `-Strings`, `-Symbols` by RVA |

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
- **server** relays avatars and host-authored world sync between all connected clients
- **kh2ctl** is a developer tool that talks to the runtime's mailbox and KH2's memory

Avatar and world paths (plan D2/D9; the DLL sides of both bridges are live-lane work):

```
 KH2 + inject DLL                 runtime                       relay
 ----------------                 -------                       -----
 local avatar --AvatarBridge--> sendAvatar ---AvatarState----> stamp owner,
                                                                forward to
 puppet poses <-AvatarBridge--- AvatarSync <--AvatarRelay----- the others
                                (interpolate)
 world events --WorldBridge---> WorldPump ----world packets--> host-only check,
 (transition, enemies, claims)                                  cache, forward
 act on them  <-WorldBridge---- onWorldPacket <--------------- (claims -> host)
```
