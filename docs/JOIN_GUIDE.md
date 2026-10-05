# Playing together privately over Tailscale (VUH-1493)

Hosting works like a Minecraft server. The host runs the relay
(`kh2coop_server`) next to their game, and friends join by the host's
address. There's no central service.

This guide covers the **current dev build**: run from a built checkout with
`kh2ctl`. Player packaging (P4) will replace these steps.

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

A Mac can run a headless client for the transport check. Actual two-player
gameplay requires a second Windows KH2 machine.

## Reaching the host: Tailscale (recommended)

Tailscale makes a private network between your PCs, with no router changes.

1. Use the existing, authorized Tailscale installation and check
   `tailscale status` on each machine.
2. The host makes their PC reachable for each friend. Either use a share of the
   machine with the friend's Tailscale account (Tailscale admin → Machines
   → the host PC → Share), or invite them to the host's tailnet. That's an
   account action; James/the owner decides. Check that tailnet policy permits
   the friend to reach the host on **UDP 7782**.
3. The host finds their address: `tailscale ip -4` (a `100.x.y.z` address).
4. Each friend checks they can reach it: `tailscale ping 100.x.y.z`.

Tailscale ping checks the tunnel, not the game's UDP port. Both direct and
Tailscale-relayed connections can work; see Tailscale's
[connection types](https://tailscale.com/docs/reference/connection-types) and
[firewall guidance](https://tailscale.com/kb/1181/firewalls).

## Matching runtime configuration (both PCs)

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

Run the relay and runtime in separate terminals. Replace all angle-bracket
placeholders before running. Verify the relay's listener:

```powershell
Get-NetUDPEndpoint -LocalPort 7782 | Select-Object LocalAddress,LocalPort,OwningProcess
Get-NetConnectionProfile | Select-Object InterfaceAlias,NetworkCategory
```

The relay must listen only on the host's Tailscale address, not `0.0.0.0` or
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
    --network --server <host 100.x address> --port 7782 `
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
- Combat: the host owns enemy HP. A friend's hits play their reaction, but
  the HP change comes from the host (VUH-1502; friend hit claims are
  VUH-1501).
- Round-trip time and loss: `kh2ctl overlay on` shows `rtt … ms  loss …%`
  in the corner once your runtime is connected, and the runtime logs
  `Net: rtt=…` every 5 s. `enet_loss` counts reliable traffic;
  `avatar_loss` counts gaps in received avatar sequences once sampled.

If a player drops and returns with missing enemies, the current playtest
workaround is for the host to leave and re-enter the room. Stop runtimes and
relay with Ctrl+C afterward; only the live operator stops rig-owned games
through `kh2ctl`.

## Troubleshooting

- **`refused by relay: Version mismatch ... (relay expects ...)`**: the
  build, content or mod flags differ between the relay and this runtime.
  The message shows both sides.
- **`Requested slot N is already taken`**: someone else has that role. Pick
  the other friend slot.
- **Never connects**: `tailscale ping` the host. If that works, check that
  the relay's bind address/port, firewall rule and tailnet policy allow UDP
  7782. `Test-NetConnection -Port` tests TCP, not ENet/UDP.
- **Two verified peers but no puppet**: check the room, local game PID and
  matching DLL. Keep the logs for the live lane.

## Known limits (dev build)

- Without `--bind`, the relay listens on every network interface,
  including the host's LAN. With `--bind 100.x`, only on Tailscale
  (verified 2026-10-02).
- Tested 2026-10-02 through the host's own Tailscale address only
  (`net_tailscale_self`): two instances on one PC reached the relay at
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
- Next is the live lane's one-game/Mac-synthetic check. Two-player visible
  gameplay still needs James's chosen second Windows KH2 player. This
  headless result does not establish a different internet connection.
