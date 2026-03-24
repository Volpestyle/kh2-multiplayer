# KH2 Co-op

Three-player Kingdom Hearts II Final Mix co-op prototype. Each player runs their own KH2 client with an independent camera, sharing one host-authoritative world/session over the network.

## Repository layout

| Directory | Status | Contents |
|-----------|--------|----------|
| `common/` | **Implemented** | Shared types (`Types.hpp`), protocol structs (`Protocol.hpp`), binary codec (`Codec.hpp/cpp`), `ByteBuffer`, and `NetworkClient` (ENet-based). |
| `server/` | **Implemented** | `SessionHost` -- lobby management, version gating, canonical slot assignment, snapshot/event broadcasting. `ServerMain` -- CLI server with signal handling. |
| `runtime/` | **Scaffold** | `IGameBridge` interface (pure virtual), `CameraController`, `ReplicaController`. No concrete `IGameBridge` implementation yet -- requires live KH2 reverse engineering. |
| `tests/` | **Implemented** | `FakeSimulation` -- end-to-end test: 3 clients connect, handshake, exchange packets, verify slot assignment, reject mismatched versions and duplicate slots. |
| `content/` | **Placeholder** | Notes for the GoA test content pack. No game data yet. |
| `docs/` | Design specs | Milestone backlog, acceptance tests, agent assignments, RE probe checklist, full design doc. |

## Building

```
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

ENet is fetched automatically via CMake FetchContent.

### Build targets
- `kh2coop_server` -- host-authoritative relay server
- `kh2coop_runtime_scaffold` -- placeholder runtime entry point
- `kh2coop_fake_sim` -- end-to-end test harness (run to verify everything works)

### Running the test
```
./build/kh2coop_fake_sim
```

### Running the server
```
./build/kh2coop_server --port 7782 --build <hash> --mod <hash>
```

## Current status

The networking layer (Agent 2's domain) is functional: protocol serialization, framed packets, ENet transport, session management, and a test harness that proves 3 clients can connect and exchange data without KH2 attached.

**What works:**
- Binary codec for all 11 domain structs + framed packet encode/decode
- Server: lobby, version gating, canonical slot assignment (Player A=0, B=1, C=2), snapshot/event broadcast
- Client: connect, version handshake with slot request, receive snapshots/events
- Test: codec round-trips, 3-client integration, version mismatch rejection, duplicate slot rejection

**What's next:**
- Authoritative simulation loop on the server (process InputFrames, update state, broadcast snapshots per tick)
- `IGameBridge` concrete implementation (requires live KH2 memory mapping)
- Wire `NetworkClient` into the runtime

## Recommended order
1. Do **not** start with world progression or cutscenes.
2. Lock a single supported KH2 PC build and one exact mod hash.
3. Build the `IGameBridge` pointer map first.
4. Get local camera retargeting working before any networking.
5. Prove 3 clients in one fixed room before touching transitions.
6. Only after stable single-room combat should you attempt room travel or story flags.

## Handoff boundary
The first thing that genuinely requires a live coding / reverse-engineering environment is:
- locating the actor structures for `PLAYER`, `FRIEND_1`, and `FRIEND_2`,
- locating the camera target / look-at control path,
- locating the input consumption path for friend slots,
- locating room/world/progress state and enemy lists.

Everything up to that point can still be specified and scaffolded from design docs.
