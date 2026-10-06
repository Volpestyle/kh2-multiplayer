# KH2 Co-op — private friend preview

You need your own Windows Steam Global KH2, Steam running, and private Tailscale relay access arranged with your host. This ZIP includes Python and the co-op tools, but no game or saves. Only the exact verified game EXE is supported; the launcher checks its fingerprint before starting it. Epic and other Steam patches are refused.

## Start and join

1. Install Tailscale and sign in with the account your host approved for the private relay access they shared. Keep Steam running. Unzip to a new folder beside your game folder, **not inside it**. Keep all files together. Double-click **Start KH2 Co-op.cmd**. The co-op package needs no installation or administrator access.
2. Browse to the folder containing **KINGDOM HEARTS II FINAL MIX.exe**. Enter the private relay address and port your host gives you. Choose **join**, your unique name, and **friend1** (or the slot assigned by your host). The host chooses **host** and uses the same relay address. Leave “run the relay here” unchecked on both computers: running the relay locally is unsupported in this preview. Use your host's private relay.
3. Click **Start game**. Once save protection is confirmed, load your existing save normally. Do not save during the preview. Meet your host in the agreed room; wait for their ready signal, check the ready box, then **Connect**. Look for the verified roster and RTT. The HUD is requested on after connection; use **Hide HUD** / **Show HUD** to change it. Your choice survives reconnecting and resets with a new game. The session ends after 30 minutes; it does not close your game automatically.
4. Use **Disconnect**, then **Exit & close game** when finished. Closing the launcher window also closes the game it started. Only owned helpers and that game are closed. Never attach another runtime to the same game.

If **Start game** reports that setup did not finish, press it again **after the previous owned game has closed**. If closure is pending or ownership is uncertain, use **Exit & close game** first. If the game is still open, exit it normally; **do not save**. Follow the launcher's closure guidance before reopening; never guess a process ID or start another copy while closure is pending. Report repeated failures with the package logs; do not modify your game install.

## Saves, warnings and removal

Your usual save folder is `Documents\My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX\` (often under OneDrive). The mod never writes your original saves: SaveGuard redirects write opens there into this package's `build\rig\logs\save_sandbox_<pid>\` and denies deletes/moves/copies. Connection is refused unless its installation is logged. This is a backstop, not permission to save: **do not save in-game during the preview**. Package progress is temporary; never copy sandbox files over your saves.

This unsigned preview injects a DLL into your owned game. SmartScreen or Defender may warn or quarantine files. Stop and contact your host; do not disable protection or add exclusions yourself. Any exclusion needs your host's approval and would be restricted to the package folder.

The package adds no game-folder files, service, account, registry setting or firewall rule. After closing its game and launcher, delete the unpacked folder and ZIP to uninstall, including package logs and sandbox files. Your game and original saves remain in place.

## Preview limits

Follow your host's selected route. If your host sees five Shadows in the courtyard and you see none after joining or reconnecting before a fight, wait: the host takes one short walk away from the circular patterned paving onto the nearby broad stone steps, then stops. The enemies should appear for you, and both games should have the same five living Shadows. Compare both games before fighting. This workaround is tested only in the courtyard, before any kills. If walking does not restore the enemies, **do not save**; report the room, what each player sees and the package logs. Stop there; do not keep walking or continue combat.

After a drop before combat, reconnect and compare both games; the courtyard walk above may be needed. Reconnecting after enemies die is unsupported. Do not reconnect mid-fight, after kills or with a partial pack. Stop and report missing enemies, enemies returning to life or a room mismatch. Populated-room cutscene hold remains unqualified. No public-port forwarding is needed or supported.

When the host starts a story cutscene, your game keeps running: stand still and do not fight or change rooms until the cutscene ends and the host is back in control. Automatic cutscene hold is off in this preview.

The HUD may briefly show **Co-op status unavailable** during reward or menu screens. Wait for normal gameplay and the roster to return before treating it as a disconnect. If the game stutters or crashes with the HUD shown, use **Hide HUD**.

Qualification used two fresh package folders on one Windows PC through the private Mac relay. Desktop-only startup, connection, HUD controls, a room change and an empty-room reconnect passed. Courtyard join/reconnect with the walk above and HUD names/reset passed on the same mod products. Combat, chest and native story delivery/reload passed through the impaired relay using the test rig; those were not desktop-only checks. Separate-PC play is untested. Steam app IDs are child-only; no `steam_appid.txt` is added. Use this short preview only after your host confirms private relay access.
