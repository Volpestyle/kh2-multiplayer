# Private preview: known issues

> **Historical ENet release09 guide (2026-10-06, protocol 10).** The instructions
> and behavior in the historical section belong to that sealed preview. Use them
> only with its complete original package. Later Steam two-PC checks supersede
> the old separate-PC transport gap, but do not upgrade this package's gameplay.
> See [Steam setup](FRIEND_PLAYTEST_STEAM.md) and the
> [current plan](ONLINE_COOP_PLAN.md) for later evidence and route limits.
>
> Current source uses protocol 33 / AvatarBridge 4 / WorldBridge 12. The checked-in
> package validator and pins still target protocol 18: a current-source ZIP
> cannot be built with them unchanged. See [packaging](../tools/packaging/README.md).
> Do not combine old package products with current DLL/runtime/relay binaries.

## Current source behavior and remaining limits

- **Supported game only.** Windows Steam Global KH2 Final Mix, compatibility
  identifier `1.0.0.10-steam-global`. The package checks the exact executable
  fingerprint; Epic and different Steam patches are refused.
- **Two-PC evidence is bounded.** Steam rev11 covers authenticated relay-only
  admission, room follow, combat, attachment reconnect and terminal host leave.
  UX07 covers launch/title/exit; JOIN05 covers technical package GUI host/join.
  Effective movement with a clearly responding puppet on both package screens,
  a physical-controller friend session and the full route remain unqualified.
- **Current player defaults.** The launchers enable enemy mirroring, population
  handling, all-player enemy targeting, normal co-op difficulty, friendly fire,
  event-hold production/control, downed handling and revive prompts. Explicit
  supported environment overrides can disable these. Experimental population
  spawning, remote-only enemy targeting and ordinary lifecycle experiments stay
  disabled. Bare DLL/manual runtime launches need deliberate configuration.
- **Enemies and bosses.** Admitted, bound enemy copies follow the host's AI;
  this replaces the old preview's independent-enemy behavior. Creeper 317,
  Fiery Globe 76 and Lance Warrior 2409 remain denied. Binding and brain-shape
  checks can leave other copies on local AI. General boss/projectile/reaction
  command and separated-player wave coverage is incomplete. See
  [enemy parity](ENEMY_PARITY.md).
- **Story and reconnect.** Populated reconnect, reconstruction after kills,
  proximity/wave/cull consistency and whole-party down/retry still have gaps.
  Event hold is enabled by the launchers, but that does not qualify every story
  event or populated room. Stop on missing/resurrected enemies, repeated damage,
  a room mismatch or a stuck hold; preserve both screens and logs. Historical
  courtyard walks are not a general workaround for current builds.
- **Characters and route.** The friend launcher offers Roxas and Sora before
  starting the game. Local two-instance evidence covers Roxas+Roxas and a Roxas
  host with a Sora friend at first prologue control and reloads, plus separate
  bounded runs at the checked Day 1-6 prologue sites. A session that starts by
  loading a save in an unchecked room plays solo until the next checked site.
  The Twilight Thorn boss, opening-scene friend representation and two-PC
  profile qualification remain open. Donald/Goofy/Riku and arbitrary host replacements are not admitted
  playable profiles. The target route is New Game / Roxas Days 1-6 through TT,
  HB 1 and LoD 1 / Shan-Yu, not a claimed completed playthrough. See
  [party setup](PARTY_SETUP.md) and the [plan](ONLINE_COOP_PLAN.md).
- **Do not save.** SaveGuard redirects write opens into a sandbox and blocks
  protected-file deletion/moves/copies. Session progress is temporary; never
  copy sandbox files over original saves. Stop at corrupt-save/deletion prompts;
  do not copy another Steam account's whole save container.
- **Network setup differs.** [Steam](FRIEND_PLAYTEST_STEAM.md) needs Steam signed
  in, the host's explicit SteamID allowlist and Valve relays, with no direct-IP
  fallback. ENet development needs approved private Tailscale access and
  an existing relay. No public port forwarding or firewall changes are part of
  either package flow. Sessions are limited to 30 minutes.
- **UI and startup.** Use distinct plain ASCII names (the HUD displays at most
  23 characters). A brief unavailable HUD during menus/rewards can clear when
  gameplay resumes. Hide the HUD if it stutters and report the logs. Startup
  can fail during mod initialization: resolve the launcher's owned-game closure
  before retrying. Stop at antivirus warnings; do not disable protection.


## Historical release09 package limits

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
