# ENet session relay

`kh2coop_server` runs without KH2 attached. It admits peers, assigns canonical
slots and routes host-native avatar/world/session traffic. Native traffic is
the default; synthetic physics is available only with `--simulate` for tests.
Steam sessions use the in-game broker and do not start this separate ENet server.

## Running

Build with `cmake --build build --target kh2coop_server --config Release`.
For a local development session:

```powershell
.\build\Release\kh2coop_server.exe --bind 127.0.0.1 --port 7946 --build dev --content none --mod none
```

Select matching game/content/mod labels and port on every runtime. The server's
default port is **7782**, while the standalone runtime defaults to **7946**.
Without `--bind`, the server listens on all interfaces; use an explicit loopback
or authorized private address for development.

| Option | Default / meaning |
|---|---|
| `--bind <ip>` | Listen address; omitted means all interfaces |
| `--port <port>` | `7782` |
| `--build <hash>` | Required game build label, `dev` |
| `--content <hash>` | Required content label, `none` |
| `--mod <hash>` | Required mod label, `none` |
| `--session <id>` | Session identifier, `local-test` |
| `--max-peers <n>` | `3` |
| `--heartbeat-timeout-ms <ms>` | Verified peer idle timeout, `5000` |
| `--pending-timeout-ms <ms>` | Unverified peer timeout, `2000` |
| `--desync-dir <path>` | Automatic report root, `build/rig/desync` |
| `--simulate` | Legacy synthetic actor simulation; no native KH2 validation |

## Responsibilities and limits

- `SessionHost` gates protocol/build/content/mod compatibility, maintains the roster and routes admitted host world authority.
- Host-agreed settings include room-locked co-op difficulty and friendly fire; a friend's environment does not override them.
- Native world caching, ordered HP/death, resync and diagnostic collection are implemented. See [forced resync](../docs/FORCED_RESYNC.md), [HP ordering](../docs/HP_ORDERING.md) and [desync reports](../docs/DESYNC_REPORTS.md) for their bounds.
- `SimulationState` provides fake movement/combat only when explicitly enabled. It does not replace the host's live game simulation.
- `ServerMain` services ENet on an approximately 16 ms loop and stops on Ctrl+C.

Current source requires protocol **31** peers. Compatibility labels are handshake
values, not binary provenance; rebuild and select a coherent source cohort.
The [project plan](../docs/ONLINE_COOP_PLAN.md) records current native acceptance
and remaining route/reconnect work. Public-realm session/instance splitting is
not the current campaign milestone.

For headless regression use the serial CTest suite and `kh2coop_fake_sim` described
in [setup](../SETUP.md); these do not establish live gameplay correctness.
