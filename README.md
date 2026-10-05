# KH2 Multiplayer

Online co-op mod for Kingdom Hearts II Final Mix (PC). The goal: each player runs their own game, sees the others in the same room, fights the same enemies and travels the story together — then plays as other characters. Two real games on one Windows PC now pass movement, shared combat and chest sync through a private Mac relay with added delay and loss. The in-game debug overlay still crashes some runs; testing uses it off. A human session on separate Windows PCs and player packaging remain open. See [private joining and solo testing](docs/JOIN_GUIDE.md).

## Start here

| What you want | Where to go |
|---------------|-------------|
| Understand the plan | `docs/ONLINE_COOP_PLAN.md` — decisions, risks, phase gates |
| See what's being worked on | Linear project **KH2 Multiplayer** (vuhlp workspace) |
| Build and test the inject DLL | `docs/DEVELOPMENT_WORKFLOW.md` |
| Navigate the codebase | `docs/CODEBASE_MAP.md` |
| Continue friend control work | `docs/HANDOFF_FRIEND_CONTROL.md` |
| Milestone history (M0–M3) | `docs/IMPLEMENTATION_BACKLOG.md` |
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

Planned model (`docs/ONLINE_COOP_PLAN.md`): every player is the native player character on their own machine and streams it; other players appear as puppets in the friend slots; the host's game owns enemies, rooms and story state. Public Realm and PvP (`docs/ARCHITECTURE_MODES.md`) are parked until co-op is playable.

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
.\build\Release\kh2coop_fake_sim.exe

# Build just the inject DLL
cmake --build build --target kh2coop_inject --config Release

# Launch KH2 with the inject DLL loaded (no Cheat Engine)
.\build\tools\kh2ctl\Release\kh2ctl.exe launch

# Kill rig instances, rebuild the DLL, relaunch + inject
.\build\tools\kh2ctl\Release\kh2ctl.exe restart
```

See `docs/DEVELOPMENT_WORKFLOW.md` for the full inject/test loop.

## Documentation

| Doc | Purpose |
|-----|---------|
| `docs/ONLINE_COOP_PLAN.md` | Plan of record: decisions, risk register, phase gates, autonomy rules |
| `docs/research/PRIOR_ART.md` | How other projects added online play to single-player games, and the pitfalls |
| `docs/CODEBASE_MAP.md` | What's in each directory, key source files |
| `docs/DEVELOPMENT_WORKFLOW.md` | Build/inject/test loop |
| `docs/HANDOFF_FRIEND_CONTROL.md` | Current state of friend control: hooks, structs, addresses |
| `docs/LESSONS_LEARNED.md` | Hard-won RE and hooking insights |
| `docs/IMPLEMENTATION_BACKLOG.md` | Milestone history (M0–M3); M4+ superseded by the plan |
| `docs/pointer_map_v1.md` | Confirmed memory offsets |
| `docs/kh2_three_client_coop_design.md` | Original 3-client co-op design (authority and actor model superseded) |
| `docs/ARCHITECTURE_MODES.md` | CampaignCoop vs PublicRealm architecture |
| `docs/INPUT_RE_SESSION.md` | Full Ghidra trace of the KH2 input pipeline |
| `docs/OPENKH_REFERENCE.md` | Guide to the sibling OpenKH repository |
| `docs/ACCEPTANCE_TESTS.md` | Pass/fail criteria for each milestone |
| `docs/KH2_CONTROL_CLI.md` | kh2ctl command reference |
| `AGENTS.md` | AI agent rules (CE/Ghidra usage, swarm coordination) |
