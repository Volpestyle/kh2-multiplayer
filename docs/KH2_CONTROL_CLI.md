# KH2 Control CLI

`kh2ctl` is the first local automation surface for KH2 testing in this repo.
It lives inside `kh2-multiplayer` rather than `../kh2-tools` because the first
version needs direct reuse of:

- `runtime/include/kh2coop/GameBridgePC.hpp`
- `common/include/kh2coop/InputMailbox.hpp`
- `runtime/include/kh2coop/KH2Offsets.hpp`

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

- Launch KH2 with the current inject DLL build loaded (no Cheat Engine)
- Track which KH2 processes the rig launched, and kill only those
- Restart KH2 (kill rig instances, rebuild the DLL, launch, inject)
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

- The `load-save`/`boot-load-save` commands are still menu-key driven, so
  they need window focus. `player-press` also works on the title menu and
  the save list (D-pad `down`/`up`, `cross` to confirm) with no focus, since
  the DLL's mailbox timers run on wall-clock time, not gameplay frames
  (2026-10-02). The scenario runner's `boot` step uses that.
- `GameBridgePC::InjectOwnedInput(...)` remains the long-term path for
  an external process API if we decide to move the control logic out of the
  inject DLL later

Friend-slot commands are more reliable because they go through
`InputMailbox -> kh2coop_inject.dll -> friend AI replacement`.

## Commands

All successful commands print a single JSON object to stdout.

### Launch, inject and instances (the rig)

```powershell
kh2ctl launch                   # launch KH2, inject the current DLL build
kh2ctl launch --no-inject       # launch only
kh2ctl inject --pid 1234        # inject into a running KH2
kh2ctl instances                # list KH2 processes and whether the rig owns them
kh2ctl kill --pid 1234          # kill one rig-launched instance
kh2ctl kill --all               # kill every rig-launched instance
```

`launch` starts the exe directly (`steam_appid.txt` in the game directory skips
the launcher; Steam must be running), waits for the game window, then injects
with `LoadLibraryW` from a remote thread. It reports the process id, the
per-PID log and the hooks that installed:

```json
{"ok":true,"command":"launch","processId":70260,"dll":".../build/rig/dll/kh2coop_inject_<ms>.dll",
 "log":".../build/rig/logs/kh2coop_inject_70260.log","hooksInstalled":true,
 "hooks":["PerEntityUpdate hook installed","MovementDispatch hook installed at RVA 0x3D5E50",
          "MotionChainSetAnim hook installed at RVA 0x3C88C0","InputCollector hook installed"],"errors":[]}
```

- **DLL copies.** Each injection loads a fresh copy of
  `build/inject/staging/kh2coop_inject.dll` from `build/rig/dll/`, so the DLL
  can be rebuilt while instances are running. Nothing is written to the game
  directory. `--dll PATH` injects a different build.
- **Logs.** `launch` sets `KH2COOP_LOG_DIR` for the child, so the DLL logs to
  `build/rig/logs/kh2coop_inject_<pid>.log`. `inject --pid` into a process the
  rig didn't launch logs to `kh2coop_inject_<pid>.log` in the game directory.
- **Ownership.** Launched processes are recorded as (pid, creation time) in
  `build/rig/owned.txt`. `kill` only terminates those; anything else is
  reported in `skippedUnowned` and left alone, because it means James is
  playing.
- Options: `--game-dir DIR` (or `KH2_GAME_DIR`), `--window-timeout-ms`,
  `--settle-ms` (wait after the window appears, default 1500),
  `--init-timeout-ms` (wait for the DLL's hooks, default 15000).
- Injection happens once the window is up, not suspended before game init.
  Startup hooks would need the early path; multi-instance doesn't (below).

### Several instances

KH2 has no single-instance lock: `launch` can be run repeatedly, and three
instances have run in-game at once (VUH-1484). Each uses ~640 MB of RAM.

- **Pick an instance with `--pid`.** Every game command (`state`, `wait-*`,
  `focus`, `tap-key`, `player-*`, friend input, …) takes `--pid N`. With more
  than one KH2 running and no `--pid`, kh2ctl refuses instead of guessing,
  so it can't drive an instance James is playing.
- **Unfocused instances keep running** and accept `player-*` / friend input
  through their own PID-keyed mailbox. Keyboard commands (`tap-key`,
  `load-save`) focus the target window first, so menus are driven one
  instance at a time.
- **Audio.** `kh2ctl mute --pid N` mutes an instance's Windows audio session
  (`--off` unmutes). The session exists once the game has played sound, so mute
  after the title screen comes up.
- **Controllers.** Every instance still reads every physical pad, focused or
  not. Per-instance pad assignment isn't built yet.
- `restart` and `boot-load-save` kill **all** rig instances before launching
  one.

### Mute

```powershell
kh2ctl mute --pid 1234          # mute
kh2ctl mute --pid 1234 --off    # unmute
```

### Screenshots, clips and the overlay (in-renderer)

```powershell
kh2ctl capture --pid 1234                     # PNG -> build/rig/shots/<pid>_<ms>.png
kh2ctl capture --pid 1234 --out shot.png
kh2ctl clip --pid 1234 --seconds 4 --fps 30   # MP4 -> build/rig/clips/<pid>_<ms>.mp4
kh2ctl overlay --pid 1234 on                  # pid, frame, world/room, fps box
kh2ctl fps --pid 1234                         # present rate over 2 s
```

The inject DLL hooks the swapchain's `Present`/`Present1` and copies the
backbuffer on the GPU, so captures work while the window is behind other
windows or unfocused. Minimized windows are untested (the game may stop
presenting). KH2 renders with D3D12 (1920×1080 `B8G8R8A8_UNORM`, 3-buffer
flip model, presents via `Present1`); the D3D11 path isn't implemented.

- Requests travel through `Local\kh2coop_capture_<pid>`
  (`common/include/kh2coop/CaptureChannel.hpp`).
- Copies run on the game's own direct queue, captured by hooking
  `ExecuteCommandLists`, and are read back 1–2 frames later, so the render
  thread doesn't wait on the GPU. PNG/BMP encoding runs on a worker thread.
- `clip` writes BMP frames, encodes them with `ffmpeg` (must be on `PATH`),
  then deletes the frames (`--keep-frames` keeps them). It reports
  `gameFpsBefore` and `gameFpsDuringCapture`; measured 2026-10-02: 66 → 68
  fps for a 4 s, 30 fps clip at 1920×1080.
- The overlay is drawn into the backbuffer, so it shows on screen and in
  captures. It's off by default.

### Warp (load a room)

```powershell
kh2ctl warp --pid 1234 --world 4 --room 0x1A               # Garden of Assemblage
kh2ctl warp --pid 1234 --world 5 --room 6 --btl 2          # override one program
kh2ctl peek --pid 1234 --rva 0x717008:u8,0x9BA928:u64 --samples 40   # RE aid
```

`warp` hands the target to the DLL, which calls the game's own transition
request (the function room-script `Jump`s use, RVA `0x152990`) on the game
thread at the start of a frame. Programs left out (`0xFFFF`) come from the
save's per-room table. It returns once the room has loaded: the DLL's frame
counter stalls during the load (~0.55 s) and must resume in the target room
for 30 frames. World/room in `NOW` change as soon as the request is made, so
they alone don't show the load finished. Measured 2026-10-02 with the
safe-state gate: 50/50 warps across 13 rooms in Twilight Town, Hollow
Bastion (incl. GoA `04/1A`) and Beast's Castle, including two combat rooms
(`05/00`, `05/06`; 4 enemies each spawned on the first visit), 1.0–3.3 s
each. Sora could move after every one.

- **Rooms that open on a cutscene** (`05/02`, `05/08` with default
  programs) load, but Sora can't move. Avoid them as fixtures.
- **Combat rooms:** the battle program alone doesn't spawn enemies; mob
  rooms spawn them when Sora walks in. On the BC-first-visit save, the
  Entrance Hall (`05/00`) and courtyard (`05/06`, doors 0–1) spawn 4–5
  Heartless after ~2 s of running, and `05/04` spawns 1. Battle state
  `0x2A11404` reads 1 (regular) or 2 (forced). Scripted fights come from
  event programs: `--world 5 --room 1 --evt 1` (Parlor Ambush, 8 Shadows,
  forced battle). `--world 8 --room 0x0C --evt 1` (Attack on the Camp) plays
  an ~85 s cutscene with no fight on that save. Mob rooms hurt Sora, so
  don't idle in them.
- **Test fixtures:** `kh2ctl poke --pid N --addr <actor+0x4DC> --type u32
  --value 0` makes Sora untouchable (team 0 is in no attack's hit mask), so
  combat-room tests don't end in a game over. `poke` only writes to
  rig-launched instances.
- **Enemy detection:** `kh2ctl state` lists actors whose objentry type
  (`objentry+0x04`) is 3 (boss) or 4 (mob). `kh2ctl entities` lists every
  actor with name, type, team (`actor+0x4DC`: 1 party, 2 enemy) and move
  state.
- **Safe-state gate:** a request is handed over only when nothing is
  frozen (`0x2A171E8` == 0), the room is live (`0x9BA8D0` != 0), no menu is
  open (`0x7435D0` == `0xFF`), timeline state is idle (`0xB65210` == 0)
  and the active event-context pointer is null (`0x2A11478` == 0).
  The elapsed timer (`0xB64F98`) remains a diagnostic: a native chest event
  left it at 90 after completion, so it cannot indicate whether an event
  is active. It's never handed over mid-load either, because hand-over only
  happens inside entity updates. Verified live: a warp requested in the
  pause menu was held, then cancelled at its timeout. During an event the
  frozen bitset reads 3 and the cutscene timer counts up. A held request is
  cancelled when `--timeout-ms` runs out, and the error reports the gate
  inputs.

### Crash evidence

```powershell
kh2ctl dump --pid 1234 --out hang.dmp   # minidump of a live (e.g. hung) instance
kh2ctl crash --pid 1234                 # fault a rig-launched instance on purpose
```

The inject DLL writes `build/rig/logs/kh2coop_crash_<pid>.dmp` itself when
the game dies from an unhandled exception (its filter is chained ahead of
the game's own). `crash` starts a remote thread at address 0 and only
targets instances the rig launched; it exists to test the scenario runner's
crash bundles (`docs/SCENARIOS.md`).

### Hit ownership (VUH-1501)

```powershell
kh2ctl hit --pid 1234 drop --on --enemies                  # client: zero all hits on enemies
kh2ctl hit --pid 1234 drop --on --attacker 0x7FF6... --victim 0x7FF6...
kh2ctl hit --pid 1234 claims --last 16                     # what the dropped hits were
kh2ctl hit --pid 1234 damage --victim 0x7FF6... --amount 7 # host: apply a claim
kh2ctl hit --pid 1234 kill --victim 0x7FF6...              # synthetic killing blow
kh2ctl hit --pid 1234 drop --off
```

Actor addresses come from `kh2ctl entities`. The DLL logs every hit as it's
built (`[hit]` lines in the inject log: attacker, atkp id, victim, damage)
from a post-hook on the hit builder `0x3D23C0`. The drop filter pre-hooks
`ApplyHitDamage` `0x3D3BA0(victim, hit)` and zeroes the hit's damage
(`hit+0x28`) when the attacker/victim match (0 = any; `--enemies` limits it to
objentry type 3/4 victims). Each dropped hit goes into a 64-slot claim ring
with its original damage. `damage` calls TakeDamage `0x3D5E50(victim,
-amount, 0, 1)` and `kill` calls ApplyStatDelta `0x3D2EB0(victim, -hp, 0, 0)`,
both on the game thread at the next frame start, and only for an actor in
the current entity list. Verified 2026-10-02 in the BC courtyard: with
`--enemies`, a 20-press combo left all Shadows at 20/20 and recorded 12
claims; replaying them with `damage` left each Shadow at exactly 20 minus
its claimed total; `kill` took a Shadow from 20 to 0, and it died and left
the entity list; filtering one Shadow → Sora zeroed only that Shadow's hit.

Notes: attack owner handles (`ATTACK+0x10`) are resolved with the engine's
own `0x4AD270(handle)` (byte-guarded). If that function doesn't match, the
fallback matches the handle's low 24 bits against actor addresses, which is
what they carry. The hit builder also builds
heals: Goofy's atkp 1525 "damage" 20 restored Sora without going through
ApplyStatDelta, so `--enemies` (or an explicit victim) keeps heals out of
the filter.

### Restart

```powershell
kh2ctl restart              # kill rig instances, rebuild the DLL, launch + inject
kh2ctl restart --no-build
kh2ctl restart --kill       # kill rig instances only
kh2ctl restart --no-inject
```

`restart` refuses (`"phase":"preflight"`, exit 1) when a KH2 process the rig
didn't launch is running. It never runs `scripts/restart-kh2.ps1`, which kills
every KH2 process and is only for humans.

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

Keys are sent with their real scan codes (extended flag for arrows and
navigation keys). KH2 reads scan codes; before 2026-10-01 kh2ctl sent scan
code 0, so every key, Enter included, reached the game as the same key and
the menu macros misbehaved.

Title menu (verified 2026-10-01): `down` then `enter` on a clean title opens
the save list. The save list opens on the most recently used slot, not slot 1.

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
kh2ctl boot-load-save --slot 2 --no-build
```

This is the first end-to-end "get me to a playable room" command. It:

1. Runs `restart` (refuses if an unowned KH2 is running)
2. Waits for KH2 to reach title/loading state
3. Drives the save menu
4. Waits until KH2 is in a live room again

**Known broken (2026-10-01).** Step 2 passes as soon as the world id reads
`0xFF`, which is also true during the boot logos and intro, so the menu keys
land before the title menu exists. The title menu (NEW GAME / LOAD / BACK)
also wraps and doesn't always start on NEW GAME, so a fixed key count can't
reach LOAD reliably. The save list opens on the last-used slot, so counting
`down` presses from slot 1 picks the wrong save. It needs a title-menu-ready
signal and the cursor indexes from memory (asked of the offline lane on
VUH-1488). Until then, drive the menu step by step with `tap-key` and check
each step with a screenshot. From a clean boot, the cursor starts on NEW GAME.

`wait-ingame` has a false positive too: the title's idle demo (the opening
monologue) reads world 1 / room 1, and that value stays in memory after the
demo is skipped, so `wait-ingame` can pass at the title.

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

**Raw slot = PS2 DualShock 2 layout (fixed 2026-10-02).** The right stick
comes first (`+0x02/+0x03`), then the left (`+0x04/+0x05`). Buttons use PS2
bit order: Select `0x0001`, L3 `0x0002`, R3 `0x0004`, Start `0x0008`, Up
`0x0010`, Right `0x0020`, Down `0x0040`, Left `0x0080`, L2 `0x0100`, R2
`0x0200`, L1 `0x0400`, R1 `0x0800`, Triangle `0x1000`, Circle `0x2000`, Cross
`0x4000`, Square `0x8000`. `KH2Offsets.hpp` and kh2ctl's button names now
follow it. Verified: `--ly 1` moves Sora, and `player-press --button start`
opens the pause menu (open-menu id `0x0A`). Before the fix, the stick labels
were swapped and "start" was D-pad Up.

The DLL now retries the mailbox every ~6 frames (was ~120), so short pulses
are no longer missed.

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
$env:KH2CTL_BIN="<repo>\build\Release\kh2ctl.exe"
```
