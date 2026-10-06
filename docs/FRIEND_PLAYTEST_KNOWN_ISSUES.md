# First friend playtest: known issues

The package and launcher-only rehearsal are still being prepared. This list will
be updated with the rehearsal result before the package is handed to a friend.

- **Supported game build only.** This first package targets the exact tested
  Steam KH2 Final Mix executable. Epic and other Steam patches are unsupported;
  the launcher must refuse them before loading the mod.
- **Private relay access.** The relay currently accepts connections through
  James's Tailscale network. James must approve how the friend reaches it before
  the internet playtest. The package does not configure accounts or networking.
- **Unsigned software warnings.** Windows SmartScreen or antivirus may flag the
  launcher or DLL injection. Report the exact warning to James. Do not disable
  antivirus or firewall protection. Any approved exclusion would be limited to
  the unpacked package folder.
- **Do not save during co-op.** The mod's saveguard redirects game save writes
  into the package's sandbox and blocks save-file deletion, moves and copies.
  This session's progress is temporary. Close the co-op game before playing
  normally or removing the package.
- **Reconnect before defeating a pack.** Recovery with defeated enemy packs is
  unsupported. Stop and report a drop after kills, missing enemies, or enemies
  returning to life rather than continuing that room.
- **Names use plain ASCII.** Use different names made from letters, digits,
  spaces, `_`, `.` or `-`; the HUD shows at most 23 characters. MP,
  downed/revive and the full multiplayer HUD remain unfinished.
- **Keep the session short and host-led.** Broader story events, populated-room
  cutscene hold, bosses and finishers have incomplete coverage. Report the room,
  what each player did, a screenshot if possible, and the package's log folder
  after a crash, freeze, room mismatch or repeated damage.
