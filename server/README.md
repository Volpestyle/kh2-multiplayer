# Server

Host-authoritative relay server for the co-op session. Runs without KH2 attached.

## Components

- **`SessionHost`** -- manages the lobby, version-gates peers, assigns canonical slots, broadcasts snapshots/events, evicts stale peers.
- **`SimulationState`** -- server-side fake physics sim (movement, gravity, jump, attack/guard/dodge state machine) for protocol testing without KH2.
- **`PeerState`** -- per-peer tracking: slot, connection status, heartbeat, last input.
- **`ServerMain`** -- CLI entry point with arg parsing, signal handling, and 60fps authoritative loop.

## Running

```
./build/kh2coop_server [options]
  --port <port>                Listen port (default 7782)
  --build <hash>               Required game build hash
  --mod <hash>                 Required mod hash
  --session <id>               Session identifier
  --max-peers <n>              Max peers (default 3)
  --heartbeat-timeout-ms <ms>  Heartbeat timeout (default 5000)
  --pending-timeout-ms <ms>    Pending peer timeout (default 2000)
```

## What works

- Accept up to 3 ENet peers
- Version gate: reject peers with mismatched build/mod hash
- Canonical slot assignment: client declares desired slot in handshake, server honors or rejects if taken
- Broadcast `SessionState`, `ActorSnapshot`, `EnemySnapshot`, `EventMessage` to all verified peers
- Stale-peer eviction: configurable heartbeat timeout and pending-peer timeout
- Authoritative simulation loop: processes `InputFrame`s, updates `SimulationState` per tick (60fps), broadcasts snapshots
- Graceful disconnect handling and session state rebuild
- All verified via 3-client integration test in `FakeSimulation.cpp`

## What's next

- Wire to live KH2 host process (replace `SimulationState` with real game state from `GameBridgePC`)
- Packet logging for replay/debug
- Reconnect flow with state resync
- Session/instance split for public-realm mode (Track B/C)
