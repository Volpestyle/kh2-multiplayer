# Mission mirror (VUH-2342)

D14 item 3: mission gauges and timers follow the host and are shown, not owned,
on clients. Land of Dragons is the first world that needs it: its morale fights
(`MU01_MS102` in Bamboo Grove 08/01, the Captain's missions) and Shan-Yu's
GATES gauge (`MU09_MS108`, 08/09) run as missions in every game. Without the
mirror each game ran its own mission against its own Sora. The two gauges
drifted apart (host 77.6, friend 90.7 after one minute of Shan-Yu,
probe `20261017-014508`), and each game could fail on its own.

The mirror is on whenever the enemy mirror is on (the player default).
`KH2COOP_MISSION_MIRROR=0` parks it on that machine.

## The mission

`M = *(exe+0x2A0FF68)` (`exe+0x3A3970` returns it). Read for Steam `9002b2de`
with headless Ghidra. TCM-Murray's VERIFIED_OFFSETS named the leads (GPL-3.0,
see `research/PRIOR_ART.md`); no code was copied.

| Field | Offset | Notes |
|---|---|---|
| flags | `M+4` | bit 2: setters refused (`0x3A39F0`); `\|= 0x14` once ended or failed |
| path | `M+0x40` | e.g. `msn/us/MU01_MS102.bar`; only the file name is compared, because the language folder differs between games |
| result | `M+0x120` | 2 once failed (`0x3A3800`, trap 4/11 `mission_failed`) |
| gauge lock | `M+0x6A0` | nonzero: `set_gauge` does nothing |
| gauge *i* | `M+0x250 + i*0x48` | `+0x18` flags (bit 0 in use), `+0x24` max, `+0x38` value (float). Setter `0x3FA310` signals 0x88 at 0 and 0x89 at max |
| counter *i* | `M+0x160 + i*0x50` | the same layout, value an int. Setter `0x3FA230` signals 0xE at 0 and 0x7B at max |
| timer | globals | clock `0xABB850`, start `0xABB858`, duration `0xABB85C`, value `0xABB860`, state `0xABB864` (2 = running), mode `0xABB868` |

The timer tick (`0x1571B0`, a task callback) counts down in mode 1:
value = start + duration − clock. Otherwise it counts up: value = clock − start.

## Wire: `MissionState` (type 52, protocol 23)

Host → relay → every client. Periodic and unreliable: every 3 frames while a
mission runs, every 15 without one. 151 payload bytes:

- room tuple, sequence, host frame
- `active`
- the 24-byte file name
- flags and result
- 3 gauges and 3 counters, each `{flags, max, value}`
- timer state, mode, value and duration
- the end trap the host's mission ran (4/3 or4/22; scoped4/16 for `TT34_MS304.bar`; 0 for none) and its argument

The codec refuses a path (any `/`), a name that isn't printable ASCII, bytes
after the terminator, a non-finite or out-of-range gauge, and an unknown timer
state, or an end trap other than4/3,4/22 or scoped4/16 for `TT34_MS304.bar`. The relay forwards it only from the verified native host, for the
admitted room, with a strictly increasing sequence. It is never cached,
replayed or part of resync.

## Client

Each frame the client reads its own mission. While the host's state is fresh
(≤ 500 ms, the current room, session and host delivery), it follows when both
games run the same mission file and its own mission hasn't ended:

- **Gauges and counters** that differ are set through the game's own guarded
  helpers: `set_gauge` `0x3FBCB0(f, slot)` and `set_count` `0x3FBAE0(n, slot)`.
  They check the slot is in use and the setters aren't refused, so the HUD and
  the 0/max signals behave natively. If the host's gauge reaches 0, the friend's
  reaches 0 the same way.
- **The timer** start moves by the difference when both run the same timer
  more than 15 frames apart.
- **Failure:** when the host's result is 2, the client calls `0x3A3800`
  (`mission_failed`) once.
- **Other ends:** the host's hooks record which end trap ended its mission,
  with its argument. Trap 4/3 completes it: `0x3A33C0(arg)` →
  `0x3A41E0(M, 7 or 0x9C, arg)`, which shows the banner and fires the trigger.
  Trap 4/16, the boss defeat (`0x3A3440(arg)` → `0x3A41E0(M, 0x10, arg)`,
  `re_deadboss`), is deliberately **not** replayed by the generic selector. The
  exact Thorn contract below has its own native owner. The friend's copy of the
  boss has no brain, so a replayed defeat flow never finishes: run
  `20261017-074315` left the friend on a white loading screen for over 100 s.
  When the host's mission ends that way, the friend sees an end it can't
  replay and releases the mission. Native lethal damage can't finish the
  friend's copy (`20261017-070818`: Shan-Yu `9 -> 221` HP), so the enemy
  mirror fades it out with the native leave instead (VUH-2367,
  `docs/ENEMY_PARITY.md`). The friend gets no boss-defeat reward: the leave
  counts no kill, and no defeat flow runs in its game. The 4/16 replay landed briefly in
  `d17016d` and was taken out.
  Trap 4/22 (`0x3A37A0(arg)`) ends it and fires signal 0xF. The client replays
  the same trap with the same argument once. After a replayed end or failure,
  every later end or failure of that mission is dropped, so it never ends twice.
- **Which mission:** a mission is its object pointer, file name and the load
  it was read in (`warp::LoadSerial`). A Continue retry after a failure reloads
  the room, so it counts as a new mission on both sides, even if the object is
  reused. Limit: a mission that keeps running across a room load counts as a
  new one after the load. The LoD missions seen so far stay in one room.
- **Final death ordering (VUH-2884 candidate):** pending checked host deaths
  publish before mission snapshots even during a gameplay hold. The friend
  reconciles deaths before checking the end with a fresh census; empty or
  all-dead scoped rosters permit replay, while living or unavailable rosters
  defer. Bailey and the Basement Hall also log a native-local completion once,
  independently of replay. The 180-frame fallback remains. Offline/live
  qualification is tracked in [the candidate result](testing/vuh2884-native-terminal/README.md).
- **An end the friend can't replay:** sometimes the host's mission ends with no
  end trap the hooks saw (an engine end, or a host without hooks). The friend
  then stops following. If it had dropped its own end meanwhile, it runs that
  end once. A stale packet alone never triggers this. The host's last gauge and
  counter values are applied before any replayed end.
- **The guards expire:** the follow decision and the replay guard both expire
  250 ms after the last tick. A friend whose session ended never keeps dropping
  its own calls.
- **Local calls dropped:** the client's own script calls that change a gauge or
  counter, or end the mission, are dropped while it follows. Those are the
  table-4 traps `mission_complete` 3, `set_count` 5, `inc_count` 6,
  `set_gauge` 8, `add_gauge` 9, `set_gauge_ratio` 10, `mission_failed` 11,
  `dec_count` 20, the end signal 22 and `add_count` 41. The host swaps the
  same entries; its hooks only record the end.
  Their entries are swapped once on the client, all or none. The check covers
  every entry's function, flags and stub bytes, the two setters' full bodies,
  and `mission_failed`'s head. A dropped `mission_failed` runs after the host's
  failure. A follow decision older than 250 ms counts as not following, so a
  load or room change never keeps dropping calls.

**Known gap:** LoD's morale waves aren't host-gated yet. Spawnctl calls the
08/01 room controller (headerId 1652) an unsupported native type, so each game
spawns its later waves itself (run `20261017-021858`). The friend's extras
can't die. That belongs to the spawn controller, not this mirror.

The host's mission is what counts. The friend's own orb pickups and hits change
nothing until the host's game sees them (as with enemy HP, the host owns the
outcome). VUH-2466: in the Struggle the friend shows and collects the host's orbs
instead of its own ([the orb rule](PROLOGUE_ENCOUNTERS.md#decision-the-struggle-score-is-the-hosts-world-vuh-2466)).

## Logs and checks

VUH-2343 adds a scoped Bailey completion gate: a successful end replay for
`HB08_MS102.bar` waits for a fresh complete enemy census with no living enemies
in the current Bailey mission. The mission pointer/scope is checked before and
after capture. The end remains pending while the ordinary client frame applies
authenticated deaths through its existing continuous bindings. This prevents
the native end hold from interrupting the last Dusk's death before it reaches
zero HP. It waits at most180 owner frames (about3s at60fps), then restores the
original native end even if an orphan persists. The cap logs every living
orphan's safely sampled netId/HP/actor, or explicitly says the census is
unavailable; it never grants death/HP write authority. The end receipt records
`path=clean-census` or `path=frame-cap`. VUH-2352 extends the same bounded
ordering gate to exactly `MU01_MS102.bar` in Bamboo Grove 08/01, door0,
map/battle68/event0. It uses the existing continuously revalidated morale
mission scope before and after capture; the ordinary death consumer, 180-frame
cap and failure replay stay unchanged. All other mission files retain their
existing end policy. The extension passed D14.4 review and LoD Live08, ending with `path=clean-census` after the final death applied. See [the observed end/death ordering](ROUTE_SPAWNS.md#bailey-terminal-death-across-the-end-hold).
The bounded deferral passed Quill's D14.4 review and the MSVC owned-memory
regression. Bailey-12 ended on both peers with empty matching HP/counts, but
failed the old replay-only oracle before egress. The receipt/oracle follow-up
passed native compilation and Bailey-13 (`20261017-103923_net_hb1_bailey_1`):
one clean-census replay, both native ended flags21/result0, empty matching
HP/counts, and egress with no second end. Tested source `f324f9a` landed as
`0cf47c0`; the Bailey code and regression bytes were unchanged by the rebase.

The friend may complete `HB08_MS102.bar` natively after the final authenticated
enemy death, before a mirror replay. That is valid: a diagnostic-only observer
logs `path=native-local replays=0` once, with the native census availability,
living count and HP sum. It also runs during a native end hold; unavailable
counts are -1, never an empty-census claim. Its replay-attempt counter and
one-shot receipt state do not affect existing gameplay gates. The scenario
accepts exactly one native-local/clean-census/frame-cap path and independently
requires both ended flags, empty matching HP/counts and egress; it rejects any
native-local plus replay combination, including a late replay after egress.

- Host: `[mission-mirror] host frame=… file=… gauges=…` every 120 frames.
- Host: `host end trap=… arg=…` when its mission ends through 4/3 or 4/22.
- Client: `client ready`, `client follow` / `release`, a `client sec` line per
  second with both games' gauges and the sets, shifts, ends and dropped calls,
  and `client fail like the host` / `client end like the host`.
- Offline: `kh2coop_mission_mirror_test` covers the codec and the rules.
- Owned native memory: `kh2coop_nativehit_test` covers the Bailey and MU01 end/death
  ordering, deferred one-shot retry, native ended flags and unavailable-census
  refusal without opening a game or writing a save.
- Live: `net_lod_morale` (parity and a shared mission end) and
  `net_lod_morale_fail` (the host's morale empties; both fail). The runner's
  `mission(i)` helper reads the same fields.

## Twilight Thorn victory (VUH-2569)

For exact mission `TT34_MS304.bar`, the host records native4/16
`mission_dead_boss` after boss signal4 advances the mission to state2. VUH-2807
retains that validated occurrence at admission, before a later inactive snapshot
can replace it while the friend is paused. Its identity includes session,
connections/deliveries, generation, epoch, native mission, load and transition.
It seals once even when the friend resumes with `host=-`. Other mission names
cannot use16 on the wire; the generic end-trap selector still never returns16.

One exact native Thorn sequence owns the initial death camera through315. Only
the actual input collector may start it, requiring fresh unchanged field/menu/
event/context/freeze/blocker safety, matching live control scope and runtime
writer, matching epoch/ACK/convergence, exact mission and native helper pins.
The installed TT34 header, its2628-byte `re_deadboss-1a` timeline and the2760-byte
`00battle.bin` index2 continuation must match their audited content hashes.
The native last-dead-boss focus must be a current census/binding for307 at0HP;
the native bone focus must be present. This preserves the native camera's actor
preconditions rather than inventing an early16 from the host snapshot alone.

The native16 marks ended flags, applies the mission header reward and installs
the first timeline via3FB000→3AD2D0. Its phase2→3→4 continuation waits for native
timeline completion, installs the common continuation, then dispatches signal16
to the authored315. These two fixed timelines do not wait for a BDX boss brain.
The r1 kill also observed the friend's local native flow completing578frames
after an early315 replay; this supports the Thorn-specific contract, not a
change to Shan-Yu's exclusion.

The host recognizes only those exact native prelude/continuation contexts and
does not publish a generic friend PAUSE for them. An unrelated replacement
context still publishes the ordinary hold. A validated host315 joins the owned
sequence instead of replaying315 independently. The native start hook permits
one315, then verifies its loaded scene resource and every descriptor word in
the new context. A qualified native315 that beats the host end receipt is
observed for deduplication only; it grants no authority to call16. Unknown or
partial native results abort the control lifetime. The landed Closing FIFO
deferral, health expiry and ordinary safety predicates remain unchanged.

The reliable host315 may precede the unreliable end16 snapshot, or arrive when
that snapshot is lost. The input consumer refuses generic replay for the exact
qualified Thorn315 in every ordering, even without a native owner. It grants no
end16/prelude authority from315 alone and opens no new PAUSE that could block a
later qualified native sequence. A late admitted end16 can still run that
sequence; an independently qualified local native315 can still be observed for
deduplication. If neither occurs, there is no fabricated victory start; the log
records `generic replay refused; no qualified owner`. This failure remains a
live qualification failure, not a timing pass. Ordinary checked scene replay
and its refusal/Closing behavior are unchanged.

`kh2coop_thorn_defeat_test` covers retained occurrence, scope and one-owner
rules. `kh2coop_nativehit_test` exercises actual paused admission and resume
with `host=-`. Its optional `--thorn-native-contract <audit-assets-dir>` fixture
uses retained exact installed assets over owned memory to check native safety,
pins/focus, one native prelude, one315, context success/refusal and host PAUSE
publication. Only the native16 side-effect leaf is stubbed; no game is opened.

Installed tt34 event program157 maps signal16 to eventASCII `315` (`0x353133`,
flags0x12), then the authored exit world2/room0/door0/programs51. Qualification
checks every semantic descriptor bit; only the uninitialized high padding byte
of params1 is ignored for that exact row. The full raw host descriptor is
published/replayed unchanged. Static derivation and exhaustive descriptor tests
are recorded in [PARTY_SETUP.md](PARTY_SETUP.md); native 315 playback, one victory
and onward control still require live qualification.
