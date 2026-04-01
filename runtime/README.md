# Runtime

The runtime layer bridges the live KH2 process to the multiplayer system. It attaches externally via `ReadProcessMemory`/`WriteProcessMemory` and provides per-frame entity discovery, camera retargeting, and replica state application.

## Components

| File | Purpose |
|------|---------|
| `IGameBridge.hpp` | Abstract interface for all game memory operations |
| `GameBridgePC.hpp/cpp` | Concrete implementation for KH2 Final Mix PC (Steam Global) |
| `CameraController.hpp` | Per-frame camera follow override with cutscene, event, and room-transition safety |
| `ReplicaController.hpp` | Applies network snapshots to remote actors, drops stale/duplicate snapshots |
| `KH2Offsets.hpp` | Complete memory offset map (confirmed, KH2LIB, and unknown offsets) |
| `RuntimeMain.cpp` | CLI scaffold: INI config, attach loop, F8 panic hotkey, graceful shutdown |

## What works

- **Process attach**: finds KH2 by process name, resolves module base
- **Entity discovery**: auto-discovers slot-0 entity struct on each room transition using camera pointer chain (primary) and vtable+moveState scan (fallback), plus Friend1/Friend2 entity structs from `Slot1+0x220/+0x228`
- **Room state**: reads world ID, room ID, map/battle/event programs, cutscene timer
- **Actor state**: reads position, rotation, velocity, airborne flags, and HP for all 3 party slots
- **Enemy state**: traverses the active-entity list at `exe+0x2A171C8`, resolves `actor+0xA90` handles through the region table at `exe+0x2B0D720`, reads `objectId` via `actor+0x918`, and filters to `B_`/`M_` objentry names so `moveState 8/9` world-NPC false positives are excluded
- **Camera retarget**: allocates fake actor object (0x700 bytes) in target process, copies original actor, redirects `camStruct+0x50` pointer. Re-applied each frame only when the room is stable, yields to cutscenes/events/transitions, and restores cleanly on shutdown or F8.
- **Replica writeback**: dual-writes position to entity struct + buffer array for physics-active rooms; writes rotation, airborne flags, HP
- **Config**: INI file (`kh2coop_runtime.ini`) with `runtime_mode`, `client_role`, `camera_override`, `panic_hotkey`, `log_owned_actor_state`, `tick_ms`. CLI args override INI values.

## What's missing

- **Input injection**: `InjectOwnedInput()` returns false; per-slot input path not RE'd
- **Enemy HP/spawn-group mapping**: active enemy traversal exists, `objectId` is now mapped via the actor objentry pointer, but HP and spawn-group fields are still not mapped
- **Enemy replica**: `ApplyReplicaEnemyState()` returns false
- **MP offset**: MP within unit slot not confirmed
- **Network integration**: `NetworkClient` not yet wired into the runtime loop

## Running

```
./build/kh2coop_runtime_scaffold --role 0 --config kh2coop_runtime.ini
```

Options:
- `--mode <mode>` -- runtime mode: `campaign_coop` (default) or `public_realm`
- `--role <0|1|2>` -- owned slot (player/friend1/friend2)
- `--config <path>` -- INI config file
- `--tick-ms <ms>` -- tick interval (default 16)
- `--no-camera` -- disable camera override
- `--log-actor-state` -- log owned actor state once per second
- `--max-ticks <n>` -- exit after N ticks (0=infinite)

Press F8 during runtime to toggle camera override on/off (Windows only).
