# KH2 Co-op — private friend preview

You need your own Windows Steam Global KH2, Steam running, and an existing Tailscale connection arranged with James. This ZIP includes Python and the co-op tools, but no game or saves. The launcher checks your game automatically before starting it. Epic and unsupported Steam patches are refused.

## Start and join

1. Unzip to a new folder beside your game folder, **not inside it**. Keep all files together. Double-click **Start KH2 Co-op.cmd**. No installation or administrator access is requested.
2. Browse to the folder containing **KINGDOM HEARTS II FINAL MIX.exe**. Enter the private relay address and port James gives you. Choose **join**, your unique name, and **friend1** (or James's assigned slot). Leave “run the relay here” unchecked. James can choose **host**; a local relay binds only the entered Tailscale address.
3. Click **Start game**. Once save protection is confirmed, load your existing save normally. Do not save during the preview. Meet James in the agreed room; wait for his ready signal, check the ready box, then **Connect**. Look for the verified roster and RTT. The session ends after 30 minutes; it does not close your game automatically.
4. Use **Disconnect**, then **Exit & close game** when finished. Closing the launcher window also closes the game it started. Only owned helpers and that game are closed. If startup reports uncertain ownership, exit that game normally; do not start another copy. Never attach another runtime to the same game.

## Saves, warnings and removal

Your usual save folder is `Documents\My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX\` (often under OneDrive). The injected SaveGuard redirects write opens there into this package's `build\rig\logs\save_sandbox_<pid>\` and denies deletes/moves/copies. Connection is refused unless its installation is logged. This is a backstop, not permission to save: **do not save in-game during rehearsal**. Package progress is not a committed save; never copy sandbox files over your saves.

This unsigned preview injects a DLL into your owned game. SmartScreen or Defender may warn or quarantine files. Stop and contact James; do not disable protection or add exclusions yourself. Any exclusion needs James's approval and would be restricted to the package folder.

The package adds no game-folder files, service, account, registry setting or firewall rule. After closing its game and launcher, delete the unpacked folder and ZIP to uninstall, including package logs and sandbox files. Your game and original saves remain in place.

## Preview limits

Follow James's selected route. Populated joins, reconnecting after enemies die, populated cutscene hold and HUD reset/expiry are not all accepted for this preview. Do not reconnect mid-fight; stop and report the room, visible symptom and package logs. No public-port forwarding is needed or supported.

Startup uses Steam app IDs only in the child environment; this does not add `steam_appid.txt` to your game folder. A clean-folder rehearsal without that file is still pending. If startup fails, stop and send the package logs to James instead of modifying the game install.
