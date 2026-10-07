# KH2 development launcher

The self-contained friend preview uses [friend.py](friend.py) and the separate
[one-page guide](../../docs/FRIEND_PLAYTEST.md). Its bundled Python needs no install;
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
4. A gameplay host may use an existing remote relay, including a Mac. Check **start a relay here** only when hosting a relay on this PC and entering this PC's existing tailnet IP. That relay receives explicit `--bind`, port and compatibility identifiers. Failure to bind stops startup rather than changing interfaces. All participants must use matching reported game/content/mod identifiers and protocol-10 binaries; these reported values do not verify binary identity.
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


## Steam (beta), opt-in source UI

The friend launcher source now offers **Connection: Steam (beta)**. It defaults
to ENet / relay every time. This is not in sealed release09, and no new package
has been built. A future private package must select the reviewed broker DLL and
matching runtime; old products do not qualify a Steam broker receipt.

Choose the connection mode **before Start game**. Only that canonical launch
child receives `KH2COOP_STEAM_BROKER=1`; inherited diagnostic switches remain
filtered. The launcher still verifies the game/package, saveguard and retained
ownership. Changing modes requires closing the owned game and starting it again.

After the game authenticates with Steam, **Your SteamID** displays the complete
app-2552430 broker-ready receipt for that owned PID. Copy shares that ID. Host
mode accepts one or two comma-separated friend SteamID64s; join mode takes the
host SteamID64 and the existing friend slot. Both friends can copy their own ID
for the host allowlist. IDs/URLs/friend codes outside the exact desktop-account
format, duplicate entries and self-admission are refused. IDs are not discovered
from another process or from the clipboard.

Connect builds `--steam-host --steam-allow ID ...` or `--steam-join ID`, with
explicit owned PID and the existing campaign/compatibility/runtime limits. Steam
uses Valve relays only and does not launch an ENet server. The existing relay
fields retain their values and behavior in ENet mode; they are hidden and unused
by Steam, including the local-relay checkbox. There is no automatic fallback.

The owned broker log is read with write sharing, a bounded 8-KiB prefix and a
creation-time floor from the launch request. Missing, locked, stale or incomplete
receipts cannot supply an identity. Runtime/broker authentication remains the
connection authority. A ready ID or roster is not native gameplay acceptance.
Busy/close handling, helper jobs, HUD controls and retained game cleanup are
unchanged. Offline tests and real Tk sample-ID screenshots are in
`build/rig/vuh1493-steam-p2p-20261006-01/launcher-candidate/`. Two-account Steam
connection and the future matched package still require live qualification.
