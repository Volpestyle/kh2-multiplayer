# Playing together over the internet (VUH-1493)

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
- A save to load. With the co-op DLL loaded, in-game saves go to
  `build/rig/logs/save_sandbox_<pid>/`, never to your real save (see
  `docs/SCENARIOS.md`, "Save safety").

## Reaching the host: Tailscale (recommended)

Tailscale makes a private network between your PCs, with no router changes.

1. Host and friends install Tailscale and sign in.
2. The host makes their PC reachable for each friend. Either share the
   machine with the friend's Tailscale account (Tailscale admin → Machines
   → the host PC → Share), or invite them to the host's tailnet. That's an
   account action; the host decides.
3. The host finds their address: `tailscale ip -4` (a `100.x.y.z` address).
4. Each friend checks they can reach it: `tailscale ping 100.x.y.z`.

## Host

```powershell
# 1. The relay. The version flags must match what the runtimes send
#    (today's runtime defaults shown).
build\Release\kh2coop_server.exe --port 7782 --build 1.0.0.10-steam-global --content none --mod ""

# 2. The game with the mod, then load your save on the title menu.
build\tools\kh2ctl\Release\kh2ctl.exe launch      # prints the game's pid

# 3. Your runtime, bound to that game. You are the host: role player (slot 0).
build\Release\kh2coop_runtime_scaffold.exe --network --server 127.0.0.1 --port 7782 `
    --pid <game pid> --role player --peer-id <your name> --no-camera
```

The first time the relay runs, Windows Firewall asks whether to allow it.
Allow it on **Private** networks (Tailscale counts as private). Don't allow
it on Public.

## Friend

```powershell
build\tools\kh2ctl\Release\kh2ctl.exe launch      # then load your save
build\Release\kh2coop_runtime_scaffold.exe --network --server <host 100.x address> --port 7782 `
    --pid <game pid> --role friend1 --peer-id <your name> --no-camera
```

A second friend uses `--role friend2`. Each slot can be taken once.

## Checking it works

- The runtime log says `Network: connected to server`, then `SessionState
  ... actors=2` (or 3).
- Walk into the same room as another player: a party member (for now with
  Donald's or Goofy's model) moves where they move and plays their
  animations, and it hides when they leave your room.
- Combat: the host owns enemy HP. A friend's hits play their reaction, but
  the HP change comes from the host (VUH-1502; friend hit claims are
  VUH-1501).
- Round-trip time and loss: shown in the overlay and logged (in progress,
  VUH-1493).

## Troubleshooting

- **`refused by relay: Version mismatch ... (relay expects ...)`**: the
  build, content or mod flags differ between the relay and this runtime.
  The message shows both sides.
- **`Requested slot N is already taken`**: someone else has that role. Pick
  the other friend slot.
- **Never connects**: `tailscale ping` the host. If that works, check that
  the relay is running and the firewall allows it on Private networks.

## Port forwarding (the classic way)

Instead of Tailscale, the host can forward UDP port 7782 on their router to
their PC, and friends use the host's public IP. **This exposes the relay to
the whole internet.** It's the host's own choice, made on their own router;
nobody else (and no agent) sets it up.

## Known limits (dev build)

- The relay listens on every network interface, including the host's LAN.
  A `--bind` option to listen on the Tailscale address only is requested.
- Tested 2026-10-02 through the host's own Tailscale address only
  (`net_tailscale_self`): two instances on one PC reached the relay at
  100.108.214.60. A friend on a different network is the open acceptance
  test.
