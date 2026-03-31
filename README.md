# KH2 Multiplayer

Kingdom Hearts II Final Mix multiplayer mod supporting two runtime modes on a shared infrastructure:

1. **Campaign Co-op** -- up to 3 players share one host-authoritative party/session, each with an independent camera.
2. **Public Realm** (planned) -- persistent characters, public hubs, party-formed adventure instances, and optional PvP arenas.

Both modes share the transport layer, protocol primitives, identity system, and content hashing. See `docs/ARCHITECTURE_MODES.md` for the full breakdown.

## Repository layout

| Directory | Status | Contents |
|-----------|--------|----------|
| `common/` | **Implemented** | Shared types (`Types.hpp`), protocol structs (`Protocol.hpp`), binary codec (`Codec.hpp/cpp`), `ByteBuffer`, `NetworkClient` (ENet). Stable ID types and forward-looking protocol records for both runtime modes. |
| `server/` | **Implemented** | `SessionHost` -- lobby, version gating, slot assignment, snapshot/event broadcast, stale-peer timeout. `SimulationState` -- authoritative fake-physics sim for testing. `ServerMain` -- CLI server at 60fps with signal handling. |
| `runtime/` | **Implemented** | `GameBridgePC` -- attaches to KH2 process, discovers entity structs dynamically, reads room/actor/HP state, retargets camera via fake-actor allocation, dual-writes replica positions. `CameraController`, `ReplicaController`, INI config, CLI scaffold with F8 panic hotkey. |
| `tests/` | **Implemented** | `FakeSimulation` -- codec round-trips, replica ordering, 3-client network integration, heartbeat timeout, version mismatch rejection, duplicate slot rejection. |
| `content/` | **Placeholder** | Notes for the GoA test content pack. |
| `docs/` | **Active** | Design specs, RE session notes, milestone backlog, architecture modes, scope expansion plan, realm protocol sketch. |

## Getting Started

See **[SETUP.md](SETUP.md)** for the full developer setup guide — prerequisites, sibling repos, RE tools (Cheat Engine + Ghidra), MCP configuration, and build instructions.

## Building

```
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

ENet is fetched automatically via CMake FetchContent.

### Build targets
- `kh2coop_server` -- host-authoritative relay server
- `kh2coop_runtime_scaffold` -- runtime entry point (attaches to live KH2)
- `kh2coop_fake_sim` -- end-to-end test harness

### Running the test
```
./build/kh2coop_fake_sim
```

### Running the server
```
./build/kh2coop_server --port 7782 --build <hash> --mod <hash>
```

### Running the runtime
```
./build/kh2coop_runtime_scaffold --mode campaign_coop --role 0 --config kh2coop_runtime.ini
```

## Current status

### What works

**Networking layer (server + common)**
- Binary codec for all domain structs with explicit little-endian framing and string length guards
- Server: lobby, version gating, canonical slot assignment (Player=0, Friend1=1, Friend2=2), snapshot/event broadcast, stale-peer timeout with configurable thresholds
- Client: connect, version handshake with slot request, heartbeat, receive snapshots/events
- SimulationState: server-side fake physics (movement, gravity, jump, attack/guard/dodge state machine) for protocol testing without KH2
- Tests: codec round-trips, snapshot ordering, heartbeat timeout, 3-client integration, version mismatch rejection, duplicate slot rejection

**Runtime layer (GameBridgePC)**
- Attaches to live KH2 process via `ReadProcessMemory`/`WriteProcessMemory`
- Resolves module base address dynamically
- Discovers slot-0 entity struct per room transition (camera pointer chain + vtable scan fallback)
- Reads room state: world ID, room ID, map/battle/event programs, cutscene timer
- Reads slot-0 position, rotation, velocity, airborne flags from entity struct
- Reads HP for all 3 slots from static unit slot memory
- Retargets camera via fake actor allocation + pointer redirect at `camStruct+0x50`
- Dual-writes replica position to entity struct + buffer array for physics-active rooms
- INI config system with CLI overrides, F8 panic hotkey to restore vanilla camera
- Auto-rediscovers entities on room transitions

### What's next (Track A -- finish co-op prototype)
- Friend1/Friend2 entity struct discovery (slot 0 is done, friends are the blocker)
- Friend-slot local input injection
- Native remote avatar spawning / replication beyond slot 0
- Enemy list discovery and shared combat
- Room transition protocol tied to live KH2 state
- Wire `NetworkClient` into the runtime for end-to-end multiplayer

### Future tracks
- **Track B** -- Refactor for scale: stable IDs, session/instance split, runtime mode enum
- **Track C** -- Public Realm v1: realm server, save-to-seed import, public hubs, party instances
- **Track D** -- PvP: arena instances, server-validated damage, anti-cheat

See `docs/IMPLEMENTATION_BACKLOG.md` for the full milestone breakdown.

## Architecture

The project supports two runtime modes sharing one infrastructure:

| Layer | Shared | CampaignCoop | PublicRealm |
|-------|--------|--------------|-------------|
| Transport (ENet) | Yes | | |
| Protocol codec | Yes | | |
| Identity (PeerId, ActorNetId) | Yes | | |
| Version/content gating | Yes | | |
| Session/lobby | | 3-slot canonical party | Realm service + instance registry |
| Actor model | | PLAYER/FRIEND_1/FRIEND_2 | LocalPrimary + RemoteReplica |
| Authority | | Host KH2 process | Client-hosted instances (v1) |
| Persistence | | Host save file | Character + realm persistence |

See `docs/ARCHITECTURE_MODES.md` and `docs/kh2_multiplayer_scope_expansion_review.md` for details.

## Recommended development order
1. Lock a single supported KH2 PC build and one exact mod hash.
2. Finish the `IGameBridge` pointer map (Friend1/2 entities, enemy list, input path).
3. Get local camera retargeting working for all 3 slots.
4. Prove 3 clients in one fixed room before touching transitions.
5. Only after stable single-room combat should you attempt room travel or story flags.
6. Refactor protocol IDs for scale (Track B) before branching into public-realm work.

## Key documentation
- `docs/ARCHITECTURE_MODES.md` -- CampaignCoop vs PublicRealm design
- `docs/kh2_multiplayer_scope_expansion_review.md` -- full scope expansion analysis
- `docs/kh2_realm_protocol_sketch.jsonc` -- protocol v2 schema sketch
- `docs/IMPLEMENTATION_BACKLOG.md` -- milestone breakdown (Tracks A-D)
- `docs/pointer_map_v1.md` -- confirmed memory offsets and runtime bridge coverage
- `docs/kh2_three_client_coop_design.md` -- original co-op design doc
- `docs/OPENKH_REFERENCE.md` -- OpenKH resource guide
