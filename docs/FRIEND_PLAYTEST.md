# KH2 Co-op — private friend preview

You need your own Windows Steam Global KH2, Steam running, and private Tailscale relay access arranged with James. This ZIP includes Python and the co-op tools, but no game or saves. Only the exact verified game EXE is supported; the launcher checks its fingerprint before starting it. Epic and other Steam patches are refused.

## Start and join

1. Unzip to a new folder beside your game folder, **not inside it**. Keep all files together. Double-click **Start KH2 Co-op.cmd**. No installation or administrator access is requested.
2. Browse to the folder containing **KINGDOM HEARTS II FINAL MIX.exe**. Enter the private relay address and port James gives you. Choose **join**, your unique name, and **friend1** (or James's assigned slot). Leave “run the relay here” unchecked. James can choose **host**; a local relay binds only the entered Tailscale address.
3. Click **Start game**. Once save protection is confirmed, load your existing save normally. Do not save during the preview. Meet James in the agreed room; wait for his ready signal, check the ready box, then **Connect**. Look for the verified roster and RTT. The session ends after 30 minutes; it does not close your game automatically.
4. Use **Disconnect**, then **Exit & close game** when finished. Closing the launcher window also closes the game it started. Only owned helpers and that game are closed. Never attach another runtime to the same game.

If **Start game** reports that setup did not finish, press it again **after the previous owned game has closed**. If closure is pending or ownership is uncertain, use **Exit & close game** first. If the game is still open, exit it normally; **do not save**. Follow the launcher's closure guidance before reopening; never guess a process ID or start another copy while closure is pending. Report repeated failures with the package logs; do not modify your game install.

## Saves, warnings and removal

Your usual save folder is `Documents\My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX\` (often under OneDrive). The injected SaveGuard redirects write opens there into this package's `build\rig\logs\save_sandbox_<pid>\` and denies deletes/moves/copies. Connection is refused unless its installation is logged. This is a backstop, not permission to save: **do not save in-game during rehearsal**. Package progress is not a committed save; never copy sandbox files over your saves.

This unsigned preview injects a DLL into your owned game. SmartScreen or Defender may warn or quarantine files. Stop and contact James; do not disable protection or add exclusions yourself. Any exclusion needs James's approval and would be restricted to the package folder.

The package adds no game-folder files, service, account, registry setting or firewall rule. After closing its game and launcher, delete the unpacked folder and ZIP to uninstall, including package logs and sandbox files. Your game and original saves remain in place.

## Preview limits

Follow James's selected route. Reconnect support is limited to all-alive packs; this package's reconnect rehearsal is still pending. Do not reconnect after enemies die or mid-fight. Populated-room cutscene hold remains unqualified. Stop and report the room, visible symptom and package logs if anything goes wrong. No public-port forwarding is needed or supported.

Startup without adding `steam_appid.txt` has been checked; Steam app IDs are set only for the child game process. Startup can still fail during mod initialization; use the guidance above. HUD names/reset passed with two games on one Windows PC through the private Mac relay. A complete launcher-only session and reconnect rehearsal remain pending. **This is a rehearsal candidate, not a friend-ready release.**
