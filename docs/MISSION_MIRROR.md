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
- the end trap the host's mission ran (4/3 or 4/22, 0 for none) and its argument

The codec refuses a path (any `/`), a name that isn't printable ASCII, bytes
after the terminator, a non-finite or out-of-range gauge, and an unknown timer
state, or an end trap other than 4/3 or 4/22. The relay forwards it only from the verified native host, for the
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
  Trap 4/22 (`0x3A37A0(arg)`) ends it and fires signal 0xF. The client replays
  the same trap with the same argument once. After a replayed end or failure,
  every later end or failure of that mission is dropped, so it never ends twice.
- **Which mission:** a mission is its object pointer, file name and the load
  it was read in (`warp::LoadSerial`). A Continue retry after a failure reloads
  the room, so it counts as a new mission on both sides, even if the object is
  reused. Limit: a mission that keeps running across a room load counts as a
  new one after the load. The LoD missions seen so far stay in one room.
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
outcome).

## Logs and checks

- Host: `[mission-mirror] host frame=… file=… gauges=…` every 120 frames.
- Host: `host end trap=… arg=…` when its mission ends through 4/3 or 4/22.
- Client: `client ready`, `client follow` / `release`, a `client sec` line per
  second with both games' gauges and the sets, shifts, ends and dropped calls,
  and `client fail like the host` / `client end like the host`.
- Offline: `kh2coop_mission_mirror_test` covers the codec and the rules.
- Live: `net_lod_morale` (parity and a shared mission end) and
  `net_lod_morale_fail` (the host's morale empties; both fail). The runner's
  `mission(i)` helper reads the same fields.
