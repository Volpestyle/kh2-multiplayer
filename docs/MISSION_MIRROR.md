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
mission runs, every 15 without one. 146 payload bytes:

- room tuple, sequence, host frame
- `active`
- the 24-byte file name
- flags and result
- 3 gauges and 3 counters, each `{flags, max, value}`
- timer state, mode, value and duration

The codec refuses a path (any `/`), a name that isn't printable ASCII, bytes
after the terminator, a non-finite or out-of-range gauge, and an unknown timer
state. The relay forwards it only from the verified native host, for the
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
- **Local calls dropped:** the client's own script calls that change a gauge or
  counter, or fail the mission, are dropped while it follows. Those are the
  table-4 traps `set_count` 5, `inc_count` 6, `set_gauge` 8, `add_gauge` 9,
  `set_gauge_ratio` 10, `mission_failed` 11, `dec_count` 20 and `add_count` 41.
  Their entries are swapped once on the client, all or none. The check covers
  every entry's function, flags and stub bytes, the two setters' full bodies,
  and `mission_failed`'s head. A dropped `mission_failed` runs after the host's
  failure. A follow decision older than 250 ms counts as not following, so a
  load or room change never keeps dropping calls.

The host's mission is what counts. The friend's own orb pickups and hits change
nothing until the host's game sees them (as with enemy HP, the host owns the
outcome).

## Logs and checks

- Host: `[mission-mirror] host frame=… file=… gauges=…` every 120 frames.
- Client: `client ready`, `client follow` / `release`, a `client sec` line per
  second with both games' gauges and the sets, shifts, fails and dropped calls,
  and `client fail like the host`.
- Offline: `kh2coop_mission_mirror_test` covers the codec and the rules.
- Live: `net_lod_morale` (parity and a shared mission end) and
  `net_lod_morale_fail` (the host's morale empties; both fail). The runner's
  `mission(i)` helper reads the same fields.
