# First friend playtest: known issues

The package candidate is built; the launcher-only session rehearsal is still
open. This list will be updated before the package is handed to a friend.

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
- **Reconnect before defeating a pack.** Recovery with defeated enemy packs is
  unsupported. A courtyard timeout/rejoin also returned without Client enemies
  while everyone was alive. One unchanged-product check restored the exact
  complete living courtyard pack after reconnect with the short Host walk below.
  Other rooms, partial packs and reconnect after kills are not covered. Stop and
  report missing enemies or enemies returning to life.
- **A populated join can miss enemies.** Courtyard joins have both passed and
  left the joining game with an empty pack, including a trace-off diagnostic.
  An empty pack also occurred after reconnect with that logging off. For an
  all-alive courtyard join where the Host sees five Shadows and the friend sees
  none after joining or reconnecting before combat, the Host can take one short
  walk away from the circular patterned paving
  onto the nearby broad stone steps, then stop and compare both games. This
  restored the exact pack in four join checks and one reconnect check. Do not
  keep walking or start combat
  if they still differ; stop and report both screens and logs. This instruction
  is limited to the complete living courtyard pack. The earlier marker-only
  reconnect refusal remains recorded; the adopted fresh-lifetime precondition
  allowed marker1 with empty caches without changing parity assertions. The
  protocol11 geometry candidate is built and transport-tested, still requires
  native live controls, and is absent from this package.
- **Names use plain ASCII.** Use different names made from letters, digits,
  spaces, `_`, `.` or `-`; the HUD shows at most 23 characters. MP,
  downed/revive and the full multiplayer HUD remain unfinished.
- **HUD performance.** If the game stutters or crashes with the HUD shown, use
  **Hide HUD**.
- **Keep the session short and host-led.** Broader story events, populated-room
  cutscene hold, bosses and finishers have incomplete coverage. Report the room,
  what each player did, a screenshot if possible, and the package's log folder
  after a crash, freeze, room mismatch or repeated damage.
