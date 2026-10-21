# Development co-op over a private ENet/Tailscale relay

The relay (`kh2coop_server`) can run next to the host's game or on a separate
machine, including a Mac. Players join the relay's address. The host's KH2
game runs the enemies and story; the relay forwards the session's traffic.
There's no central service.

This guide covers the **current ENet development path**, run from a matching
built checkout with `kh2ctl`. For the Steam private package, use
[the Steam guide](FRIEND_PLAYTEST_STEAM.md); it needs no Tailscale or separate
relay. [Historical package instructions](FRIEND_PLAYTEST.md) cover the sealed protocol-10
ENet preview; those are not current-source package instructions. The checked-in
package validator/pins target protocol 18, so current protocol-31 packaging is
blocked pending refresh and qualification ([details](../tools/packaging/README.md)).

Current source uses **protocol 31, AvatarBridge 4 and WorldBridge 12**. Rebuild
all participating products together. The current launchers enable gameplay
features listed in [known limits](FRIEND_PLAYTEST_KNOWN_ISSUES.md); the raw
commands below do not reproduce those environment defaults automatically.
Use the launcher for that player configuration, or deliberately supply the
same `SHIPPED_DEFAULTS` from `tools/launcher/friend_package.py` to the game and
runtime. Keep profile admission and room qualification in view.

The development checkout also has a [host/join launcher](../tools/launcher/README.md): run `pythonw tools/launcher/launcher.py` in the Windows desktop after preparing and injecting the game with kh2ctl. It selects the loaded game and manages the runtime and optional private relay. The manual commands below remain useful for a friend's setup and troubleshooting.

## What everyone needs

- KINGDOM HEARTS HD 1.5+2.5 ReMIX on Steam (Global, game build
  1.0.0.10). The relay refuses a different build.
- This repo built in Release (`cmake --build build --config Release`):
  `kh2ctl.exe`, `kh2coop_inject.dll`, `kh2coop_runtime_scaffold.exe` and,
  for the host, `kh2coop_server.exe`.
- A save to load. **Do not save in-game during testing.** The DLL's save
  sandbox is a backstop, not permission to save (see
  [save safety](SCENARIOS.md#save-safety)).
- Matching source revisions on both PCs (`git rev-parse HEAD`). The relay
  compares reported versions; it does not inspect game executables or prove
  the DLLs match.

A Mac can run the relay or a headless client for transport checks. Each human
player needs a Windows KH2 game. For automated tests, the rig can run two or
three real games on this PC and script each player's controls.

Steam transport and package GUI host/join have since passed separate two-PC
technical checks (rev11 and JOIN05). Those do not establish a physical-controller
friend session, the current prologue profiles on two PCs, or full-story co-op.
The older ENet/Mac checks below retain their original limited scope; their
artifacts are local ignored evidence, not files shipped by a fresh clone.

## Testing without a friend

The scenario rig can launch two real KH2 games and drive both native players.
Each has its own runtime and sees the other as a puppet. The existing
`net_two_instances.json` checks movement in both directions;
`net_two_instances_impaired.json` adds 100 ms of configured delay across the
two runtimes, 10 ms jitter and 2% loss per runtime. Run from the desktop session:

```powershell
python tools/scenario/run.py tools/scenario/scenarios/net_two_instances.json
python tools/scenario/run.py tools/scenario/scenarios/net_two_instances_impaired.json
```

These fixtures start a local relay. Putting the relay on the Mac lets both
real games exercise the Tailscale path to another machine and back. This
checks real game networking while James plays alone; it does not check a
second Windows installation or another person's experience of the controls.

The bounded Mac-relay runs `20261004-221407` and `20261004-221645` both passed:
more than 270 matched native puppet samples per direction, mean position errors
below 3 game units, and all four protected saves unchanged. The second run
confirmed both active player slots on the Mac and the configured impairment
in both runtime logs. See the [result, captures and rerun instructions](../build/rig/vuh1493-two-game-mac-relay-20261005-01/lead-result.md).

For a cheaper transport check, `kh2coop_fake_sim` runs test clients without
KH2, and the cross-machine probe can send synthetic avatar poses. Those
clients do not run KH2's enemies, story or combat. Renting a server would
provide another relay location, but would not replace the host's game.

## Reaching the relay: Tailscale (recommended)

Tailscale makes a private network between your PCs, with no router changes.

1. Use the existing, authorized Tailscale installation and check
   `tailscale status` on each machine.
2. The relay owner makes the relay machine reachable for each player, including
   the gameplay host. Share that machine with the friend's Tailscale account
   or invite them to the owner's tailnet. That is an account action;
   James/the owner decides. Check that existing tailnet policy permits the
   players to reach the relay on **UDP 7782**, or the chosen relay port.
3. On the relay machine, find its address with `tailscale ip -4`
   (a `100.x.y.z` address).
4. Each friend checks they can reach it: `tailscale ping 100.x.y.z`.

Tailscale ping checks the tunnel, not the game's UDP port. Both direct and
Tailscale-relayed connections can work; see Tailscale's
[connection types](https://tailscale.com/docs/reference/connection-types) and
[firewall guidance](https://tailscale.com/kb/1181/firewalls).

## Matching runtime configuration (both PCs)

Use **protocol 31** binaries for relay and runtimes. Protocol version is
compiled in; there is no `--protocol` flag or runtime config key to change it.
The game-build identifier below is the reported compatibility value, not the
Windows executable's file-version field. Always specify the port: relay and
runtime defaults differ (7782 and 7946 respectively).

The runtime config uses plain `key=value` lines. Omit INI section headers.

Run all commands from the repository root. Create this explicit config on
each PC, using a different filename if it already exists:

```powershell
New-Item -ItemType Directory -Force build/rig/private-join | Out-Null
@'
game_build=1.0.0.10-steam-global
content_hash=none
mod_hash=none
'@ | Set-Content -Encoding ascii build/rig/private-join/runtime.ini
```

Both runtimes below load this file. Using `none` on both ends avoids
`--mod ""`, whose empty argument can disappear in Windows PowerShell 5.1.
For different content/mod identifiers, update both runtimes and relay together.

## Host

```powershell
# 1. The relay, listening only on your Tailscale address. The version
#    flags match the explicit runtime config above.
build\Release\kh2coop_server.exe --port 7782 --bind <your 100.x address> `
    --build 1.0.0.10-steam-global --content none --mod none

# 2. The game with the mod, then load your save on the title menu.
build\tools\kh2ctl\Release\kh2ctl.exe launch      # prints the game's pid

# 3. Your runtime, bound to that game. You are the host: role player (slot 0).
#    With --bind, connect to your own 100.x address (127.0.0.1 isn't listening).
build\Release\kh2coop_runtime_scaffold.exe --config build/rig/private-join/runtime.ini `
    --network --server <your 100.x address> --port 7782 `
    --pid <game pid> --role player --peer-id <your name> --no-camera
```

Run the relay and runtime in separate terminals. If the relay is on a Mac
or another machine, run only the game/runtime commands on the gameplay host
and give **every runtime the relay machine's address**. The accepted Mac
checks used `100.103.220.58:27795`; 7782 above is just this guide's example.
Replace all angle-bracket placeholders before running. On a Windows relay,
verify the listener:

```powershell
Get-NetUDPEndpoint -LocalPort 7782 | Select-Object LocalAddress,LocalPort,OwningProcess
Get-NetConnectionProfile | Select-Object InterfaceAlias,NetworkCategory
```

The relay must listen only on the relay machine's Tailscale address, not `0.0.0.0` or
`::`. Inspect the actual Tailscale network profile; do not assume it is
Private. Check the existing inbound firewall rule for the exact relay path,
UDP port and active profile. Any necessary rule should be scoped to the
Tailscale local address and approved peers. Do not disable the firewall,
change profiles or forward router ports to test. 

Only the designated live operator launches games from a desktop session,
after checking the rig lock. For automated save loading, use the scenario
runner's `boot` step; `kh2ctl boot-load-save` is known broken. Headless
network tests do not need the rig or a game launch.

## Friend

```powershell
build\tools\kh2ctl\Release\kh2ctl.exe launch      # then load your save
build\Release\kh2coop_runtime_scaffold.exe --config build/rig/private-join/runtime.ini `
    --network --server <relay 100.x address> --port 7782 `
    --pid <game pid> --role friend1 --peer-id <your name> --no-camera
```

Start the host runtime first. Each PID is local to its own PC; use distinct
peer IDs. A second friend uses `--role friend2`. Each slot can be taken once.

## Checking it works

- The runtime log says `Network: connected to server`, then `SessionState
  ... actors=2` (or 3).
- Walk into the same room as another player: a party member (for now with
  Donald's or Goofy's model) moves where they move and plays their
  animations, and it hides when they leave your room.
  Move each player in turn, capture both directions, and keep relay/runtime logs.
- Combat: the host owns enemy HP. A friend's accepted hit claims are applied
  once by the host, which broadcasts absolute HP/death state. With launcher
  defaults, admitted enemy copies also follow the host's mirrored AI.
- Round-trip time and loss: the runtime logs `Net: rtt=...` every 5 s.
  `enet_loss` counts reliable traffic; `avatar_loss` counts gaps in received
  avatar sequences once sampled. The optional `kh2ctl overlay on --pid <pid>`
  shows application RTT and avatar sequence-gap loss in-game. The reviewed
  render-queue fix passed a combined impaired two-game Mac-relay run with both
  overlays on (`012155`); captures show the actual values. See its
  [result and limits](../build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined6-result.md).
  Earlier overlay crashes and startup capture failures remain in the evidence.


If a player drops and returns with missing enemies, stop and retain both logs.
Populated reconnect and reconstruction after kills remain incomplete; old
courtyard walk workarounds are not general recovery guarantees. Stop runtimes and
relay with Ctrl+C afterward; only the live operator stops rig-owned games
through `kh2ctl`.

## Troubleshooting

- **`refused by relay: Version mismatch ... (relay expects ...)`**: the
  build, content or mod flags differ between the relay and this runtime.
  The message shows both sides.
- **`Requested slot N is already taken`**: someone else has that role. Pick
  the other friend slot.
- **Never connects**: `tailscale ping` the relay machine. If that works, check that
  the relay's bind address/port, firewall rule and tailnet policy allow the chosen UDP
  port (7782 in these examples). `Test-NetConnection -Port` tests TCP, not ENet/UDP.
- **Two verified peers but no puppet**: check the room, local game PID and
  matching DLL. Keep the logs for the live lane.

## Historical ENet checks and current limits

The dated runs below used their original products. Their counts are historical
evidence, not protocol-31 regressions. Current gameplay limits are maintained
in [known issues](FRIEND_PLAYTEST_KNOWN_ISSUES.md).

- Without `--bind`, the relay listens on every network interface,
  including the host's LAN. With `--bind 100.x`, only on Tailscale
  (verified 2026-10-02).
- The earlier 2026-10-02 `net_tailscale_self` check used the host's own
  Tailscale address: two instances on one PC reached the relay at
  100.108.214.60. That fixture launches two games; only the live lane runs it.
  It now binds the relay explicitly. Its address is machine-specific: update
  both runtime `server` values and relay `args` together on another host.
- Headless loopback regression: `build\Release\kh2coop_avatar_test.exe`
  passed all 146 checks on 2026-10-05 UTC without KH2.
- Cross-machine headless PASS, 2026-10-05 UTC: Windows relay
  `100.108.214.60:27795`, Windows synthetic player, Mac synthetic friend at
  `100.103.220.58`, using the production NetworkClient and ENet. Both avatar
  directions and mismatched-build/protocol rejection passed without games
  or firewall changes. Evidence and bounded rerun commands:
  `build/rig/vuh1493-offline-20261005-01/result.md` (local evidence).
- The one-game/Mac-synthetic leg passed (`pc-leg-02`, 120 seconds), followed
  by two real games on one Windows PC through the Mac relay: normal movement
  `221407` and impaired movement `221645`. These supersede the earlier
  own-address-only transport coverage; they do not test a second Windows PC
  or a different household's internet connection.
- One combined impaired combat-and-chest session passed in `20261005-001439`,
  **with both overlays off**: enemy HP matched, two client hits applied once
  each, the targeted death applied once per game, and the native chest opening
  persisted on the client through reload. All four protected saves stayed
  unchanged. See the [result](../build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined2-result.md)
  and [acceptance review](../build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined2-acceptance-review.md).
  Earlier setup failures and overlay crashes retain their original outcomes.
- A physical-controller friend session and full route remain unqualified.
  ENet access still requires the owner's authorized Tailscale setup. Steam's
  later two-PC result does not create Tailscale access or qualify these older
  ENet products. Current package builds require explicit matching product pins.

A later combined run (`012155`) passed with both overlays ON after the render
queue fix. Five client claims applied exactly once; native HP/death and chest
persistence checks passed, and both captures showed RTT/loss. All four saves
were unchanged. See its [independent acceptance review](../build/rig/vuh1493-combat-progress-mac-relay-20261005-01/combined6-acceptance-review.md).
