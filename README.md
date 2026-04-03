# KH2 Multiplayer

Kingdom Hearts II Final Mix multiplayer mod. Player-controlled friend party members (Donald, Goofy) via in-process DLL injection with full animation, movement, and camera support.

## Start here

| What you want | Where to go |
|---------------|-------------|
| Understand the project | This file, then `docs/kh2_three_client_coop_design.md` |
| Build and test the inject DLL | `docs/DEVELOPMENT_WORKFLOW.md` |
| Navigate the codebase | `docs/CODEBASE_MAP.md` |
| Continue friend control work | `docs/HANDOFF_FRIEND_CONTROL.md` |
| Check milestone progress | `docs/IMPLEMENTATION_BACKLOG.md` |
| Look up memory offsets | `docs/pointer_map_v1.md` + `runtime/include/kh2coop/KH2Offsets.hpp` |
| Understand RE methodology | `docs/LESSONS_LEARNED.md` |

## What works today

**Friend entity control (M3 — in progress)**
- Press F5 in-game to take control of Donald (Friend1)
- Left stick moves Donald with proportional walk/run speed
- Camera follows Donald, right stick orbits
- Idle/walk/run animations match stick input (loops correctly at any distance from Sora)
- Sora frozen in place while controlling Donald
- Facing persists when stick is released

**Networking layer (M0-M2 — complete)**
- Binary codec for all domain types with little-endian framing
- Host-authoritative session server: version gating, slot assignment, snapshot broadcast
- ENet transport with heartbeat, 3-client integration tested
- Server-side fake physics for protocol testing without KH2

**Runtime bridge (M1-M2 — complete)**
- Attaches to live KH2 via `ReadProcessMemory`/`WriteProcessMemory`
- Entity discovery, room/actor/HP state reads, camera retargeting
- Shared-memory IPC (InputMailbox) between runtime and inject DLL

## Architecture

Two runtime modes on shared infrastructure:

1. **Campaign Co-op** — up to 3 players share one host-authoritative session, each controlling a party slot (Sora, Donald, Goofy) with independent cameras.
2. **Public Realm** (planned) — persistent characters, public hubs, party-formed instances.

See `docs/ARCHITECTURE_MODES.md` for the full breakdown.

```
                    ┌──────────────────┐
                    │   KH2 Process    │
                    │  ┌────────────┐  │     shared memory
                    │  │ inject DLL │◄─┼──── (InputMailbox) ◄── runtime process
                    │  └────────────┘  │                         │
                    │   hooks entity   │                         │ ENet
                    │   update loop    │                    ┌────▼────┐
                    └──────────────────┘                    │ server  │
                                                           └─────────┘
```

## Quick start

```powershell
# Build everything
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config Release

# Run E2E test (no KH2 needed)
.\build\tests\Release\kh2coop_fake_sim.exe

# Build just the inject DLL
cmake --build build --target kh2coop_inject --config Release

# Restart KH2 + rebuild DLL
.\scripts\restart-kh2.ps1
```

See `docs/DEVELOPMENT_WORKFLOW.md` for the full inject/test loop.

## Documentation

| Doc | Purpose |
|-----|---------|
| `docs/CODEBASE_MAP.md` | What's in each directory, key source files |
| `docs/DEVELOPMENT_WORKFLOW.md` | Build/inject/test loop |
| `docs/HANDOFF_FRIEND_CONTROL.md` | Current state of friend control: hooks, structs, addresses |
| `docs/LESSONS_LEARNED.md` | Hard-won RE and hooking insights |
| `docs/IMPLEMENTATION_BACKLOG.md` | Full milestone tracking (M0-M8, Tracks A-D) |
| `docs/pointer_map_v1.md` | Confirmed memory offsets |
| `docs/kh2_three_client_coop_design.md` | Original 3-client co-op design |
| `docs/ARCHITECTURE_MODES.md` | CampaignCoop vs PublicRealm architecture |
| `docs/INPUT_RE_SESSION.md` | Full Ghidra trace of the KH2 input pipeline |
| `docs/OPENKH_REFERENCE.md` | Guide to the sibling OpenKH repository |
| `docs/ACCEPTANCE_TESTS.md` | Pass/fail criteria for each milestone |
| `docs/KH2_CONTROL_CLI.md` | kh2ctl command reference |
| `AGENTS.md` | AI agent rules (CE/Ghidra usage, swarm coordination) |
