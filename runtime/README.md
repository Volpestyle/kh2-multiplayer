# Runtime

The runtime attaches to KH2 Final Mix PC (Steam Global) and bridges its injected
DLL to a co-op session. Each machine controls its own primary actor; remote
players arrive through AvatarBridge as puppets, while WorldBridge carries
host-native enemy, room and progress traffic. Network integration is active.

## Components

| Component | Purpose |
|---|---|
| `IGameBridge.hpp`, `GameBridgePC.hpp/cpp` | Process attachment, memory access and native actor discovery |
| `CameraController.hpp` | Optional external camera override and restoration |
| `ReplicaController.hpp` | Legacy actor snapshot application, opt-in |
| `KH2Offsets.hpp` | Steam Global memory offsets and their qualification |
| `RuntimeMain.cpp` | Config, network admission, avatar/world pumps, diagnostics and shutdown |

Source compatibility is protocol **29**, AvatarBridge **4**, WorldBridge **12**
and CaptureChannel **1**. Build the runtime, DLL, relay and control tools together;
restart the owned game to pick up a newly built DLL. See
[setup](../SETUP.md) and [the development workflow](../docs/DEVELOPMENT_WORKFLOW.md).

## Running

For player sessions use [the friend launcher](../tools/launcher/README.md), which
handles owned launch/injection, account confirmation and player defaults. The
standalone runtime defaults to offline, camera override on and a 16 ms loop:

```powershell
.\build\Release\kh2coop_runtime_scaffold.exe --pid 1234 --role player --config kh2coop_runtime.ini --no-camera
```

Use an actual owned PID in place of `1234`. Arguments override INI values.

| Option | Purpose |
|---|---|
| `--pid <pid>` | Select the intended game process |
| `--role player\|friend1\|friend2` | Canonical network slot (`0\|1\|2` also accepted) |
| `--mode campaign_coop\|public_realm` | Campaign is the supported development focus; public realm is planned scaffolding |
| `--network --server <host> --port <port>` | Enable ENet; default runtime endpoint is `127.0.0.1:7946` |
| `--peer-id <id>` | Unique peer label |
| `--content <hash>` | Content handshake identifier; game/mod labels come from INI |
| `--steam-host --steam-allow <ID64>` | Host through the owned game's Steam broker; allow up to two friends |
| `--steam-join <ID64>` | Join an allowlisting host as a friend slot |
| `--no-network` | Offline mode |
| `--no-camera` | Disable external camera override at startup |
| `--tick-ms <ms>`, `--max-ticks <n>` | Loop delay and bounded runtime lifetime |
| `--log-actor-state` | Periodic actor diagnostics |
| `--desync-dir <path>`, `--inject-log <path>` | Diagnostic spool and exact registered inject log |
| `--link-latency-ms <n>`, `--link-jitter-ms <n>`, `--link-loss <pct>` | Test link impairment |
| `--legacy-replica` | Opt into the old snapshot/input path |

Steam requires explicit `--pid`, a broker-enabled owned game and the appropriate
role. It uses Valve relays without the standalone ENet server. Prefer the launcher
for account, allowlist and missing-app-ID-file consent handling. `--help` lists
the complete runtime interface, including listener diagnostics.

The ENet relay defaults to port **7782**, unlike the runtime's **7946**. Set an
explicit matching port and game/content/mod identifiers. Admission rejects
incompatible protocol, build, content, mode or slot requests. A verified roster
does not by itself establish native world convergence.

F8 toggles the optional camera override on Windows. Direct developer launches
retain their native feature defaults; the friend launcher enables its player
profile, including Normal difficulty, downed/revive and enemy mirroring. See
[the plan](../docs/ONLINE_COOP_PLAN.md#player-launch-defaults-reviewed-2026-10-20) for scope.
The legacy `InjectOwnedInput()` and `ApplyReplicaEnemyState()` stubs are not the
current injected avatar/world gameplay path.

## Interpolation delay

The qualified defaults are avatar **80 ms** and enemy cursor **6 host frames**.
Set `KH2COOP_AVATAR_RENDER_DELAY_MS` on each runtime (integer 1..500), and
`KH2COOP_ENEMY_CURSOR_DELAY_FRAMES` on each game/DLL (integer 4..18), before
launch. Invalid values fall back to defaults and log `source=invalid-default`;
accepted overrides log `source=env`. The enemy lower bound preserves the existing
+/-3-frame cursor hysteresis. Hold/release thresholds and packet cadence are unchanged.

`KH2COOP_LATENCY_TRACE=1` enables bounded diagnostic counters (18,000 records/
frames); it does not gate gameplay. Qualified loopback comparisons and their
limits are retained in [the latency results](../docs/research/LATENCY_AB.md).
