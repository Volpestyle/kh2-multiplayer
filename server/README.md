# Server

Host-authoritative relay server for the co-op session. Runs without KH2 attached.

## Components

- **`SessionHost`** (`server/include/kh2coop/SessionHost.hpp`) -- manages the lobby, version-gates peers, assigns canonical slots, and broadcasts snapshots/events to all verified clients.
- **`PeerState`** (`server/include/kh2coop/PeerState.hpp`) -- per-peer tracking: slot, connection status, heartbeat, last input.
- **`ServerMain`** (`server/src/ServerMain.cpp`) -- CLI entry point with arg parsing and signal handling.

## Running

```
./build/kh2coop_server [options]
  --port <port>       Listen port (default 7782)
  --build <hash>      Required game build hash
  --mod <hash>        Required mod hash
  --session <id>      Session identifier
  --max-peers <n>     Max peers (default 3)
```

## What works

- Accept up to 3 ENet peers
- Version gate: reject peers with mismatched build/mod hash
- Canonical slot assignment: client declares desired slot in handshake, server honors or rejects if taken
- Broadcast `SessionState`, `ActorSnapshot`, `EnemySnapshot`, `EventMessage` to all verified peers
- Graceful disconnect handling and session state rebuild

## What's next

- Authoritative simulation loop: process `InputFrame`s, update `ActorState` per tick, broadcast snapshots at a fixed cadence
- Heartbeat timeout and stale peer eviction
- Packet logging for replay/debug
