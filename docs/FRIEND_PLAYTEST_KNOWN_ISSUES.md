# First friend playtest: known issues

The desktop rehearsal passed on two fresh package folders sharing one Windows
game installation through the private Mac relay. A separate friend PC remains
untested; the preview uses the limits below.

- **Supported game build only.** This first package targets the exact tested
  Steam KH2 Final Mix executable. Epic and other Steam patches are unsupported;
  the launcher must refuse them before loading the mod.
- **Private relay access.** The relay currently accepts connections through
  the host's Tailscale network. Friend access needs approval before the internet
  playtest. The package does not configure accounts or networking. Leave
  **run the relay here** unchecked on both computers; the local Windows relay
  option is unsupported in this preview.
- **Unsigned software warnings.** Windows SmartScreen or antivirus may flag the
  launcher or DLL injection. Report the exact warning to your host. Do not disable
  antivirus or firewall protection. Any approved exclusion would be limited to
  the unpacked package folder.
- **Startup can fail during mod initialization.** One observed startup stopped
  after Warp initialization; its cause remains unknown. The launcher refuses
  connection and attempts to close only its owned game. Use **Exit & close game**
  to resolve pending ownership. If the game is still open, exit normally and do
  not save. If Start reports that setup did not finish, press Start again once
  the previous game has closed. Report repeated failures with the package logs.
- **Do not save during co-op.** The mod's saveguard redirects game save writes
  into the package's sandbox and blocks save-file deletion, moves and copies.
  This session's progress is temporary. Close the co-op game before playing
  normally or removing the package.
- **Enemies move independently.** HP and deaths are shared, but enemy movement and attacks run separately on each computer; matching the host's enemy movement and AI remains planned (VUH-1515).
- **Reconnect before defeating a pack.** Recovery with defeated enemy packs is
  unsupported. A courtyard timeout/rejoin also returned without client enemies
  while everyone was alive. One unchanged-product check restored the exact
  complete living courtyard pack after reconnect with the short host walk below.
  Other rooms, partial packs and reconnect after kills are not covered. Stop and
  report missing enemies or enemies returning to life.
- **A populated join can miss enemies.** Courtyard joins have both passed and
  left the joining game with an empty pack, including a trace-off diagnostic.
  An empty pack also occurred after reconnect with that logging off. When the
  host sees five Shadows and the friend sees none before combat, the host takes
  one short walk away from the circular patterned paving onto the nearby broad
  stone steps, then stops. Both games should now have the same five living
  Shadows. This restored the exact pack in four join checks and one reconnect
  check, only in the courtyard before any kills. If they still differ, **do not
  save**; stop and report the room, both screens and logs. No repeat walk or
  rejoin fallback is qualified. Original automatic-join/reconnect failures remain
  recorded; the protocol11 candidate is absent from this package.
- **Names use plain ASCII.** Use different names made from letters, digits,
  spaces, `_`, `.` or `-`; the HUD shows at most 23 characters. MP,
  downed/revive and the full multiplayer HUD remain unfinished.
- **HUD performance.** If the game stutters or crashes with the HUD shown, use
  **Hide HUD**. The HUD may briefly show **Co-op status unavailable** during
  reward or menu screens. Wait for gameplay and the roster to return before
  treating that brief message as a disconnect.
- **Keep the session short and host-led.** Broader story events, populated-room
  cutscene hold, bosses and finishers have incomplete coverage. Automatic event
  hold is off: when the host starts a story cutscene, the friend's game keeps
  running. Stand still; do not fight or change rooms until the host is back in
  control. Report the room,
  what each player did, a screenshot if possible, and the package's log folder
  after a crash, freeze, room mismatch or repeated damage.
