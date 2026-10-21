# KH2 Multiplayer

Online multiplayer mod for Kingdom Hearts II Final Mix (PC, Steam Global).
The grand vision is PvP: players fighting each other as Sora, Roxas, Riku and
other characters, in arenas and public realms. The road there runs through
co-op, because PvP needs the same groundwork: every player's character synced
live, shared enemies and rooms, and authority over damage. So the first goal
is story co-op: each player runs their own game, sees the others in the same
room, fights the same enemies and travels the story together.

This is an experimental private preview, not a finished story-co-op release.
Steam relay-only transport has passed between two Windows PCs, including
room follow, shared combat, reconnect and host departure. Package GUI host/join
has technical two-PC acceptance; a physical-controller friend session and the
full story route remain open. Local two-instance runs now cover Roxas+Roxas
and a Roxas host with a Sora friend from the first controllable prologue field
through the Sandlot door. Opening-scene friend representation and two-PC
qualification of those profiles remain open.

See the [Steam preview guide](docs/FRIEND_PLAYTEST_STEAM.md),
[known limits](docs/FRIEND_PLAYTEST_KNOWN_ISSUES.md) and
[plan of record](docs/ONLINE_COOP_PLAN.md). The current route goal starts at
New Game / Roxas Days 1-6, then continues through Sora's Twilight Town,
Hollow Bastion and Land of Dragons first visits via native doors.

## Start here

| What you want | Where to go |
|---------------|-------------|
| Understand the plan and current state | [docs/ONLINE_COOP_PLAN.md](docs/ONLINE_COOP_PLAN.md): decisions, risks, phase gates |
| See what's being worked on | Linear project **KH2 Multiplayer** (vuhlp workspace) |
| Play with a friend | [Steam preview](docs/FRIEND_PLAYTEST_STEAM.md) or [development/ENet guide](docs/JOIN_GUIDE.md) |
| Set up a checkout | [SETUP.md](SETUP.md) |
| Build and test the inject DLL | [docs/DEVELOPMENT_WORKFLOW.md](docs/DEVELOPMENT_WORKFLOW.md) |
| Navigate the codebase | [docs/CODEBASE_MAP.md](docs/CODEBASE_MAP.md) |
| Run live regression scenarios | [docs/SCENARIOS.md](docs/SCENARIOS.md) |
| Look up memory offsets | [docs/pointer_map_v1.md](docs/pointer_map_v1.md) + `runtime/include/kh2coop/KH2Offsets.hpp` |
| Understand RE methodology | [docs/LESSONS_LEARNED.md](docs/LESSONS_LEARNED.md) |

## What works today

**Current source: protocol 29, AvatarBridge 4, WorldBridge 12.** Use matching
DLL, runtime and relay products; older preview ZIPs keep their original
versions and feature limits. The checked-in package validator/pins still target
protocol 18, so current protocol-29 packaging is blocked until they are refreshed
and qualified; see [packaging](tools/packaging/README.md).

- Each player controls a native character in their own game; remote players
  appear as independently owned actors with private status. The launcher offers
  Roxas and Sora profiles; qualification depends on the room and story context.
- Host-authoritative enemy HP/deaths and exactly-once client hit claims have
  bounded live coverage. Enemy mirroring uses the host's AI for admitted,
  bound actors; Creeper 317, Fiery Globe 76 and Lance Warrior 2409 remain denied.
- Native party HUD gauges display remote owner HP/MP and portraits. Native
  Fire/MP spending and refill on both owners have a two-Sora loopback pass;
  reciprocal two-PC HUD acceptance remains open.
- Host-follow, late join/reload, chests and masked shared story progress have
  fixture-specific passes. Personal character data remains local.
- The player/package launcher enables enemy mirroring/population, all-player enemy
  targeting, normal co-op difficulty, friendly fire, event hold, downed and
  revive prompts. Experimental population spawning and remote-only targeting
  remain disabled. A bare DLL launch does not imply the launcher defaults.
- Steam uses authenticated, encrypted Valve relay connections with an explicit
  host allowlist. ENet with a private Tailscale relay remains available for
  development and legacy packages.

**Still open:** full New Game-to-Shan-Yu two-PC route acceptance, friend
representation in the opening's engine scenes, broader playable cast,
populated reconnect/lifecycle consistency, and complete boss, projectile,
reaction-command and whole-party retry coverage. Enabling a feature by default
does not establish full-story qualification.

**Tools**

- `kh2ctl`: launch KH2, inject the DLL, drive input, capture
  screenshots and clips ([docs/KH2_CONTROL_CLI.md](docs/KH2_CONTROL_CLI.md)). Load saves through the
  scenario runner or manually; `boot-load-save` is known broken.
- Scenario runner: scripted multi-instance live tests with reports
  (`tools/scenario`, [docs/SCENARIOS.md](docs/SCENARIOS.md)).
- Host/join launcher (`tools/launcher`) and the self-contained friend preview
  ZIP (`tools/packaging`).
- SaveGuard: the DLL sandboxes writes to the save folder. Still, never save
  in-game during testing.

**Earlier milestones:** F5 friend control (Donald moves and animates under
player input, [docs/HANDOFF_FRIEND_CONTROL.md](docs/HANDOFF_FRIEND_CONTROL.md)) and the M0–M3 network and
runtime layers ([docs/IMPLEMENTATION_BACKLOG.md](docs/IMPLEMENTATION_BACKLOG.md)).

## Architecture

Every player is the native player character on their own machine and streams
it; other players appear as puppets in the friend slots. The host's game owns
enemies, rooms and story state. SessionHost owns session/version admission:
it runs in the host runtime for Steam and in the separate server for ENet.
Steam uses an in-game broker and Valve relays. PvP and Public Realm
([docs/ARCHITECTURE_MODES.md](docs/ARCHITECTURE_MODES.md)) build on this
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

*Diagram: ENet development path. Steam uses the game's authenticated Steam
broker and Valve relays instead of `kh2coop_server`.*

## Quick start

```powershell
# From an x64 MSVC developer shell; see SETUP.md
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

See [docs/DEVELOPMENT_WORKFLOW.md](docs/DEVELOPMENT_WORKFLOW.md) for the full inject/test loop and
[docs/CODEBASE_MAP.md](docs/CODEBASE_MAP.md) for every offline test target.

## Documentation

| Doc | Purpose |
|-----|---------|
| [docs/ONLINE_COOP_PLAN.md](docs/ONLINE_COOP_PLAN.md) | Plan of record: current state, decisions, risk register, phase gates |
| [docs/JOIN_GUIDE.md](docs/JOIN_GUIDE.md) | Development/ENet private setup and testing without a friend |
| [docs/FRIEND_PLAYTEST_STEAM.md](docs/FRIEND_PLAYTEST_STEAM.md) | Current Steam private-preview setup |
| [docs/FRIEND_PLAYTEST.md](docs/FRIEND_PLAYTEST.md) | Historical release09 ENet/Tailscale package guide |
| [docs/FRIEND_PLAYTEST_KNOWN_ISSUES.md](docs/FRIEND_PLAYTEST_KNOWN_ISSUES.md) | Limits of the friend preview |
| [docs/NATIVE_PARTY_HUD.md](docs/NATIVE_PARTY_HUD.md), [docs/NATIVE_GUEST_MAGIC.md](docs/NATIVE_GUEST_MAGIC.md) | Native owner vitals display and guest Magic evidence |
| [docs/PARTY_SETUP.md](docs/PARTY_SETUP.md), [docs/PROLOGUE_ENCOUNTERS.md](docs/PROLOGUE_ENCOUNTERS.md) | Character/party construction and encounter qualification |
| [docs/research/PRIOR_ART.md](docs/research/PRIOR_ART.md) | How other projects added online play to single-player games, and the pitfalls |
| [docs/CODEBASE_MAP.md](docs/CODEBASE_MAP.md) | What's in each directory, key source files, build and test targets |
| [docs/DEVELOPMENT_WORKFLOW.md](docs/DEVELOPMENT_WORKFLOW.md) | Build/inject/test loop |
| [docs/KH2_CONTROL_CLI.md](docs/KH2_CONTROL_CLI.md) | kh2ctl command reference |
| [docs/SCENARIOS.md](docs/SCENARIOS.md) | Scenario runner, fixtures and save safety |
| [docs/ENEMY_PARITY.md](docs/ENEMY_PARITY.md) | Matching host enemies to client copies |
| [docs/HP_ORDERING.md](docs/HP_ORDERING.md) | Absolute enemy HP sequencing |
| [docs/FORCED_RESYNC.md](docs/FORCED_RESYNC.md) | Forced world resync and automatic recovery |
| [docs/DESYNC_REPORTS.md](docs/DESYNC_REPORTS.md) | Automatic desync report collection |
| [docs/CAUSAL_RECOVERY_DIAGNOSTICS.md](docs/CAUSAL_RECOVERY_DIAGNOSTICS.md) | Opt-in recovery diagnostics |
| [docs/STEAM_TRANSPORT.md](docs/STEAM_TRANSPORT.md) | Steam transport, qualification and historical implementation notes |
| [docs/LESSONS_LEARNED.md](docs/LESSONS_LEARNED.md) | Hard-won RE and hooking insights |
| [docs/pointer_map_v1.md](docs/pointer_map_v1.md) | Confirmed memory offsets |
| [docs/INPUT_RE_SESSION.md](docs/INPUT_RE_SESSION.md) | Full Ghidra trace of the KH2 input pipeline |
| [docs/OPENKH_REFERENCE.md](docs/OPENKH_REFERENCE.md) | Guide to the sibling OpenKH repository |
| [docs/HANDOFF_FRIEND_CONTROL.md](docs/HANDOFF_FRIEND_CONTROL.md) | F5 friend control: hooks, structs, addresses |
| [docs/IMPLEMENTATION_BACKLOG.md](docs/IMPLEMENTATION_BACKLOG.md) | Milestone history (M0–M3); M4+ superseded by the plan |
| [docs/ACCEPTANCE_TESTS.md](docs/ACCEPTANCE_TESTS.md) | Original milestone criteria; phase gates and Linear now supersede them |
| [docs/kh2_three_client_coop_design.md](docs/kh2_three_client_coop_design.md) | Original 3-client co-op design (authority and actor model superseded) |
| [docs/ARCHITECTURE_MODES.md](docs/ARCHITECTURE_MODES.md) | CampaignCoop vs PublicRealm and PvP architecture |
| [docs/ROXAS_DUAL_WIELD_FORM.md](docs/ROXAS_DUAL_WIELD_FORM.md) | Parked Roxas form design |
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
