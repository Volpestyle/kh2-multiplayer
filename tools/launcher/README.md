# KH2 development launcher

The player-facing launcher uses [friend.py](friend.py) and the separate
[Steam guide](../../docs/FRIEND_PLAYTEST_STEAM.md). The original
[ENet release09 guide](../../docs/FRIEND_PLAYTEST.md) is historical. Its bundled Python needs no install;
it verifies the supported game EXE and starts/injects/closes only its own game
through the portable canonical CLI. Build instructions and provenance rules are
in [tools/packaging](../packaging/README.md). The developer launcher below retains
its existing already-injected-game workflow.

Dependency-free Python 3.10+ / Tkinter launcher for the existing Windows development binaries. Host or join from the UI without editing a runtime config. Use the existing desktop session, after the live-lane owner releases the rig.

```powershell
pythonw tools/launcher/launcher.py
```

The standard CPython Windows installer includes Tkinter. No package install, build, account, firewall or service setup is performed. The GUI refuses Session 0. `python` also works; `pythonw` avoids an extra launcher console.

1. Prepare/load/inject the intended game using the existing `kh2ctl` workflow. This launcher offers no game launch, injection, load, save, restart or kill action.
2. Click **Refresh games**, select the existing PID, and ensure no other runtime is attached to it. The launcher checks that the retained PID handle is a KH2 process and that its AvatarBridge exists. This is not a DLL/version/saveguard attestation.
3. Choose **host** (player slot 0) or **join** (friend1/friend2), a literal private tailnet IPv4 address and matching UDP port, and a unique **peer ID**. Peer ID is supported by `--peer-id`; it is not a distinct in-game display-name feature. No DNS/public/loopback fallback is used.
4. A gameplay host may use an existing remote relay, including a Mac. Check **start a relay here** only when hosting a relay on this PC and entering this PC's existing tailnet IP. That relay receives explicit `--bind`, port and compatibility identifiers. Failure to bind stops startup rather than changing interfaces. All participants must use matching reported game/content/mod identifiers and matching source-protocol binaries; these reported values do not verify binary identity. Current native source is protocol 34, while this UI's plan metadata still imports the historical protocol-18 packaging constant; that label does not attest the selected binaries.
5. Click **Connect**. Config, exact argv, logs and exit receipts go only into ignored `tools/launcher/.local/runs/<stamp>/`. Preferences are in `.local/preferences.json`; PIDs are not remembered. Disconnect/close/time limit affect only this launcher's runtime and optional relay. The game and external relay remain running.

Existing binary paths can be supplied without config editing:

```powershell
pythonw tools/launcher/launcher.py --runtime-exe C:/approved/kh2coop_runtime_scaffold.exe --server-exe C:/approved/kh2coop_server.exe --kh2ctl-exe C:/approved/kh2ctl.exe
```

Defaults are the repository's `build/Release/{kh2coop_runtime_scaffold,kh2coop_server}.exe` and `build/tools/kh2ctl/Release/kh2ctl.exe`. No binaries are downloaded or built. Runtime options preserve the supported CampaignCoop avatar path (`--no-camera`, no legacy replica). Plain config contains `game_build`, `content_hash`, `mod_hash`; runtime role, endpoint, PID and peer ID use their existing flags.

Connection status and application RTT are read from the owned runtime's log. A transport connection is distinguished from its verified roster; neither claims completed native bootstrap. Ping becomes stale after five seconds without a new sample. This launcher does not add an in-game HUD, remote name/HP rendering, or downed/revive prompts.

Ownership: a per-PID launcher mutex prevents duplicate runtimes **from this launcher**. Other tools' runtimes are not discovered/killed; the UI requires the operator to establish that precondition. A live scenario rig lock blocks discovery/start; the launcher never modifies that lock. The canonical `kh2ctl instances` query may prune its own stale `owned.txt` entries as implemented by kh2ctl. It does not launch/kill a game.

Network helpers start inside an owned kill-on-close Job Object using Windows 10+ creation-time `PROC_THREAD_ATTRIBUTE_JOB_LIST`, with private hidden consoles. Only private NUL-input and log-output handles are inherited; the job handle is not. Unsupported/failed attribute or process creation stops startup with no uncontained fallback. This closes the abrupt-parent-loss gap before later job assignment ([Microsoft explanation](https://devblogs.microsoft.com/oldnewthing/20230209-00/?p=107812/)). Disconnect sends Ctrl+C only after confirming the private console contains just the helper and this launcher, allowing the runtime's normal world reset/disconnect path. A three-second signal wait falls back to termination through the retained creation handle. `exit.json` records forced termination and whether the actual shutdown log marker appeared; absence of graceful cleanup is not accepted as a clean native disconnect. No bridge/header repair is attempted. Review native state before reconnecting after a forced stop. The wall timer (30â€“3600 seconds) is independent of Tk, runtime also uses `--max-ticks`, and job closure contains only spawned helpers. Root validated Session1 GUI disconnect, close and exact time-limit behavior on the earlier creation path. The replacement passed Session0 private-relay graceful stop, actual post-stop discovery and abrupt-parent-loss probes; post-fix Session1 GUI run053047 also passed with matching two-game native arrivals, graceful helper exits and unchanged saves. Evidence: `build/rig/vuh1507-launcher-20261005-01/creation-job-prep/`.

Offline command/config inspection (no GUI, native processes, files or networking):

```powershell
python -B tools/launcher/launcher.py --plan --mode join --pid 1234 --endpoint 100.103.220.58 --port 27795 --slot friend1 --peer-id James --seconds 120
python -B tools/launcher/launcher.py --plan --mode host --local-relay --pid 1234 --endpoint 100.64.1.2 --peer-id Host --seconds 120
```

These are examples, not adopted endpoint/readiness/PID receipts. Runtime uses structured argv without a shell. Offline syntax/config checks and the earlier actual Session1 GUI roster/ping/disconnect/close/time-limit runs passed. The new creation-time helper path has separate headless evidence above; the post-fix Session1 GUI check053047 also passed. Source/data map and preparation result: `build/rig/vuh1507-launcher-20261005-01/`.


## Player launcher and Steam

`friend.py` is the packaged player UI. It starts, injects and closes only its
owned game through the portable canonical CLI, verifies its package and exact
supported EXE, and requires the SaveGuard receipt before connection. See the
[player guide](../../docs/FRIEND_PLAYTEST_STEAM.md) for the complete flow.

Its source UI starts with **Steam (beta)** selected. At package startup the
manifest's `defaultTransport` selects Steam or ENet (a missing value falls back
to ENet). Current source is protocol **34**; the checked-in packaging validator
and product selection remain protocol **18**. They are historical inputs, so
building a current package requires a reviewed validator update and a coherent
new product cohort. See [packaging](../packaging/README.md). Source UI behavior
does not establish that a current packaged build has shipped.

Choose transport and player character before **Start game**. Steam enables the
broker only for that owned game. Confirm that **Your SteamID** is the intended
account, optionally verify it against **Expected SteamID**, and explicitly
confirm the host allowlist. Host accepts up to two distinct friend SteamID64s;
join accepts the host SteamID64 or a `kh2coop:steam:` invitation. Invalid IDs,
duplicates and self-admission are rejected. The missing `steam_appid.txt` helper
requires the visible consent checkbox; it creates only the expected app-ID file
and removes its owned file after game closure.

Connect builds `--steam-host --steam-allow ID ...` or `--steam-join ID` with the
owned PID and matching compatibility/runtime settings. Steam uses Valve relays
without Tailscale or an ENet server. ENet fields and the local-relay checkbox
are hidden and unused in Steam mode. There is no automatic transport fallback.
Disconnect before editing an active allowlist; ending hosting ends its friends'
session. Changing game transport requires closing and restarting the owned game.

The launcher reads the owned broker log with write sharing, bounded reads and a
creation-time floor. Missing, locked, stale or incomplete receipts cannot supply
an identity. Account readiness and verified roster status do not establish
native gameplay convergence. Steam terminal errors are surfaced and stop the
owned helper session. Helpers retain their Job Object ownership boundary;
the game remains separately owned by the canonical CLI.

`child_environment` applies player defaults to both game and runtime children:
enemy mirror/population/all-target, Normal difficulty, friendly fire, event hold,
downed handling and revive prompts. Explicit inherited values for enabled flags
win; developer diagnostic flags are filtered and general population spawning
and legacy targeting remain parked. Play-as choices use admitted profiles. See
[the plan](../../docs/ONLINE_COOP_PLAN.md#player-launch-defaults-reviewed-2026-10-20)
for current scope and limitations. The developer launcher also supplies these helper-process defaults through
`windows_owned.py`; its already-running game retains its original environment.

Historical launcher receipts under `build/rig/` describe their recorded builds
and routes. They do not qualify a newly rebuilt protocol-34 package or establish
clean-machine/two-PC acceptance for that package.
