# KH2 Control CLI

`kh2ctl` is the first local automation surface for KH2 testing in this repo.
It lives inside `kh2-multiplayer` rather than `../kh2-tools` because the first
version needs direct reuse of:

- `runtime/include/kh2coop/GameBridgePC.hpp`
- `common/include/kh2coop/InputMailbox.hpp`
- `runtime/include/kh2coop/KH2Offsets.hpp`
- `scripts/restart-kh2.ps1`

Once the command surface stops moving, it can be extracted if needed.

## Build

```powershell
cmake --build build --target kh2ctl --config Release
```

Expected output path on Windows is usually one of:

- `build/Release/kh2ctl.exe`
- `build/kh2ctl.exe`

## What It Can Do

### Process and state control

- Restart KH2 through the existing restart script
- Attach to the KH2 process
- Read room state and actor state
- Wait for title/loading
- Wait for in-game
- Wait for a specific world/room
- Focus the KH2 window

### Menu/title interaction

- Send keyboard taps and holds to KH2
- Run a basic `load-save` macro that uses confirm/down keys and then waits for
  KH2 to enter a room

### Friend-slot gameplay control

- Send mailbox-driven input pulses to `Friend1` and `Friend2`
- Send convenience `move` and `press` actions for friend slots

This reuses the existing inject mailbox path. The inject DLL must be loaded for
friend-slot movement/button commands to have any visible effect.

### Player / slot-0 gameplay control

- Send native slot-0 raw pad pulses through the inject DLL's input collector
- Move Sora with left-stick pulses
- Drive camera with right-stick axes
- Press raw controller buttons like `confirm`, `cancel`, `cross`, `circle`,
  `triangle`, `square`, `l1`, `r1`, `start`, and `dpad` directions

These commands use the mailbox + input-collector hook path rather than
foreground-window key injection.

## Current Limitation

That means:

- Save/title automation is still menu-key driven rather than migrated to the
  new native slot-0 commands
- `GameBridgePC::InjectOwnedInput(...)` remains the long-term path for
  an external process API if we decide to move the control logic out of the
  inject DLL later

Friend-slot commands are more reliable because they go through
`InputMailbox -> kh2coop_inject.dll -> friend AI replacement`.

## Commands

All successful commands print a single JSON object to stdout.

### Restart

```powershell
kh2ctl restart
kh2ctl restart --no-build
kh2ctl restart --kill
kh2ctl restart --copy-dll
kh2ctl restart --steam
```

### State and waits

```powershell
kh2ctl state
kh2ctl wait-title
kh2ctl wait-ingame
kh2ctl wait-room --world 2 --room 1
```

### Window and key input

```powershell
kh2ctl focus
kh2ctl tap-key --key enter
kh2ctl hold-key --key down --duration-ms 750
```

### Save-load macro

```powershell
kh2ctl load-save --slot 1
kh2ctl load-save --slot 3 --confirm-key enter --down-key down
```

This macro is intentionally configurable because exact menu timing is machine
and game-state dependent:

- `--wake-presses`
- `--wake-delay-ms`
- `--step-delay-ms`
- `--post-select-delay-ms`
- `--final-confirm-presses`
- `--load-timeout-ms`

### Full boot macro

```powershell
kh2ctl boot-load-save --slot 1
kh2ctl boot-load-save --slot 2 --no-build --copy-dll
```

This is the first end-to-end "get me to a playable room" command. It:

1. Runs `restart-kh2.ps1`
2. Waits for KH2 to reach title/loading state
3. Drives the save menu
4. Waits until KH2 is in a live room again

## Player Input Commands

### Raw slot-0 pulse

```powershell
kh2ctl player-input --buttons confirm --duration-ms 120
kh2ctl player-input --lx 0.0 --ly 1.0 --rx 0.5 --ry 0.0 --duration-ms 500
```

Accepted player button names include:

- `confirm`, `cross`, `a`
- `cancel`, `circle`, `b`
- `square`, `x`
- `triangle`, `y`, `menu`
- `l1`, `lb`
- `r1`, `rb`, `lockon`
- `start`, `select`, `back`
- `dup`, `ddown`, `dleft`, `dright`

### Convenience wrappers

```powershell
kh2ctl player-move --x 0.0 --y 1.0 --duration-ms 750
kh2ctl player-press --button confirm --duration-ms 100
```

## Friend Input Commands

### Raw mailbox pulse

```powershell
kh2ctl input --slot friend1 --lx 0.0 --ly 1.0 --duration-ms 500
kh2ctl input --slot friend2 --buttons attack,jump --duration-ms 120
```

### Convenience wrappers

```powershell
kh2ctl move --slot friend1 --x 0.5 --y 1.0 --duration-ms 750
kh2ctl press --slot friend2 --button attack --duration-ms 100
```

## MCP Wrapper

A thin stdio MCP server is provided at:

- `tools/mcp_kh2ctl/server.py`
- `scripts/run-kh2ctl-mcp.ps1`

It shells out to `kh2ctl` and returns the parsed JSON result from each command.

Run it directly with:

```powershell
python tools\mcp_kh2ctl\server.py
# or
.\scripts\run-kh2ctl-mcp.ps1
```

If the server cannot find the built CLI automatically, set:

```powershell
$env:KH2CTL_BIN="C:\Users\volpe\kh2-multiplayer\build\Release\kh2ctl.exe"
```
