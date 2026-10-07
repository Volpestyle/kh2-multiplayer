# KH2 Multiplayer

Online multiplayer mod for Kingdom Hearts II Final Mix (PC, Steam Global).
The grand vision is PvP: players fighting each other as Sora, Roxas, Riku and
other characters, in arenas and public realms. The road there runs through
co-op, because PvP needs the same groundwork: every player's character synced
live, shared enemies and rooms, and authority over damage. So the first goal
is story co-op: each player runs their own game, sees the others in the same
room, fights the same enemies and travels the story together.

Two real games on one Windows PC pass movement, shared combat, chests and story
progress through a private Mac relay, with and without added delay and loss.
A first private friend preview package is sealed and waiting on handoff. A
human session on separate Windows PCs is still open. See
[private joining and solo testing](docs/JOIN_GUIDE.md) and the
[known issues](docs/FRIEND_PLAYTEST_KNOWN_ISSUES.md).

## Start here

| What you want | Where to go |
|---------------|-------------|
| Understand the plan and current state | `docs/ONLINE_COOP_PLAN.md`: decisions, risks, phase gates |
| See what's being worked on | Linear project **KH2 Multiplayer** (vuhlp workspace) |
| Play with a friend | `docs/JOIN_GUIDE.md` (dev build) or `docs/FRIEND_PLAYTEST.md` (preview package) |
| Build and test the inject DLL | `docs/DEVELOPMENT_WORKFLOW.md` |
| Navigate the codebase | `docs/CODEBASE_MAP.md` |
| Run live regression scenarios | `docs/SCENARIOS.md` |
| Look up memory offsets | `docs/pointer_map_v1.md` + `runtime/include/kh2coop/KH2Offsets.hpp` |
| Understand RE methodology | `docs/LESSONS_LEARNED.md` |

## What works today

**Co-op (protocol 10)**
- Each player is the native Sora in their own game; the others appear as
  puppets in the friend slots, with names, HP and an RTT/loss overlay.
- Shared enemy HP and deaths: a client's hits are sent as claims, the host
  applies each once through the game's own damage routine and broadcasts
  absolute HP. Ordinary combat, including client kills, works.
- Rooms: host-follow, late join and same-room reload across multiple rooms.
- Shared progress: chests, visited rooms and story flags sync from the host's
  save data before the client's room loads, while personal data is kept.
- Private transport over Tailscale through a relay on any machine, including
  a Mac. ENet is the transport; Steam networking is opt-in and unreleased
  (`docs/STEAM_TRANSPORT.md`).

**Still open:** attack-specific effects and boss finishers, enemy spawn
authority for populated rooms, automatic reconnect mid-fight, cutscene hold,
broader story side effects, and separate-PC play.

**Tools**
- `kh2ctl`: launch KH2, inject the DLL, load saves, drive input, capture
  screenshots and clips (`docs/KH2_CONTROL_CLI.md`).
- Scenario runner: scripted multi-instance live tests with reports
  (`tools/scenario`, `docs/SCENARIOS.md`).
- Host/join launcher (`tools/launcher`) and the self-contained friend preview
  ZIP (`tools/packaging`).
- SaveGuard: the DLL sandboxes writes to the save folder. Still, never save
  in-game during testing.

**Earlier milestones:** F5 friend control (Donald moves and animates under
player input, `docs/HANDOFF_FRIEND_CONTROL.md`) and the M0–M3 network and
runtime layers (`docs/IMPLEMENTATION_BACKLOG.md`).

## Architecture

Every player is the native player character on their own machine and streams
it; other players appear as puppets in the friend slots. The host's game owns
enemies, rooms and story state; the relay forwards session traffic and checks
versions. PvP and Public Realm (`docs/ARCHITECTURE_MODES.md`) build on this
model with arena instances and server-validated damage; they start once co-op
passes its friend playtest.

```
  Player's PC                                         Any machine (PC or Mac)
 ┌──────────────────────────┐
 │ KH2 process              │
 │  ┌────────────────────┐  │ shared memory  ┌─────────┐   ENet    ┌───────────────┐
 │  │ inject DLL (hooks) │◄─┼───────────────►│ runtime │◄─────────►│ kh2coop_server│
 │  └────────────────────┘  │  (bridges)     └─────────┘ Tailscale │    (relay)    │
 └──────────────────────────┘                                      └───────▲───────┘
                                              other players' runtimes ─────┘
```

## Quick start

```powershell
# Build everything
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --config Release

# Offline tests (no KH2 needed); each prints ALL CHECKS PASSED
.\build\Release\kh2coop_fake_sim.exe
.\build\Release\kh2coop_world_test.exe

# Build just the inject DLL
cmake --build build --target kh2coop_inject --config Release

# Launch KH2 with the inject DLL loaded (no Cheat Engine)
.\build\tools\kh2ctl\Release\kh2ctl.exe launch

# Kill rig instances, rebuild the DLL, relaunch + inject
.\build\tools\kh2ctl\Release\kh2ctl.exe restart

# Two real games through a local relay, scripted movement
python tools/scenario/run.py tools/scenario/scenarios/net_two_instances.json
```

See `docs/DEVELOPMENT_WORKFLOW.md` for the full inject/test loop and
`docs/CODEBASE_MAP.md` for every offline test target.

## Documentation

| Doc | Purpose |
|-----|---------|
| `docs/ONLINE_COOP_PLAN.md` | Plan of record: current state, decisions, risk register, phase gates |
| `docs/JOIN_GUIDE.md` | Private play over Tailscale with the dev build, and testing without a friend |
| `docs/FRIEND_PLAYTEST.md` | One-page guide shipped with the friend preview package |
| `docs/FRIEND_PLAYTEST_KNOWN_ISSUES.md` | Limits of the friend preview |
| `docs/research/PRIOR_ART.md` | How other projects added online play to single-player games, and the pitfalls |
| `docs/CODEBASE_MAP.md` | What's in each directory, key source files, build and test targets |
| `docs/DEVELOPMENT_WORKFLOW.md` | Build/inject/test loop |
| `docs/KH2_CONTROL_CLI.md` | kh2ctl command reference |
| `docs/SCENARIOS.md` | Scenario runner, fixtures and save safety |
| `docs/ENEMY_PARITY.md` | Matching host enemies to client copies |
| `docs/HP_ORDERING.md` | Absolute enemy HP sequencing |
| `docs/FORCED_RESYNC.md` | Forced world resync and automatic recovery |
| `docs/DESYNC_REPORTS.md` | Automatic desync report collection |
| `docs/CAUSAL_RECOVERY_DIAGNOSTICS.md` | Opt-in recovery diagnostics |
| `docs/STEAM_TRANSPORT.md` | Opt-in Steam transport work |
| `docs/LESSONS_LEARNED.md` | Hard-won RE and hooking insights |
| `docs/pointer_map_v1.md` | Confirmed memory offsets |
| `docs/INPUT_RE_SESSION.md` | Full Ghidra trace of the KH2 input pipeline |
| `docs/OPENKH_REFERENCE.md` | Guide to the sibling OpenKH repository |
| `docs/HANDOFF_FRIEND_CONTROL.md` | F5 friend control: hooks, structs, addresses |
| `docs/IMPLEMENTATION_BACKLOG.md` | Milestone history (M0–M3); M4+ superseded by the plan |
| `docs/ACCEPTANCE_TESTS.md` | Original milestone criteria; phase gates and Linear now supersede them |
| `docs/kh2_three_client_coop_design.md` | Original 3-client co-op design (authority and actor model superseded) |
| `docs/ARCHITECTURE_MODES.md` | CampaignCoop vs PublicRealm and PvP architecture |
| `docs/ROXAS_DUAL_WIELD_FORM.md` | Parked Roxas form design |
| `docs/archive/`, `docs/probes/` | Retired plans and one-off RE session notes |
| `AGENTS.md` | AI agent rules: safety, scenarios, CE/Ghidra, rig, swarm coordination |

## License

Copyright (C) 2026 Volpestyle. This project is free software: you can
redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. It is distributed WITHOUT ANY
WARRANTY; see [`LICENSE`](LICENSE) for details. SPDX: `GPL-3.0-or-later`.

The license covers this repository's code and docs only. Kingdom Hearts and
Kingdom Hearts II are the property of Square Enix and Disney; this is an
unofficial fan project, not affiliated with or endorsed by them. No game assets,
game files or executables are included or distributed — you need your own copy
of the game. Bundled dependencies (ENet, MinHook, CPython and others) keep
their own licenses, which ship in the player package's `licenses/` folder.
