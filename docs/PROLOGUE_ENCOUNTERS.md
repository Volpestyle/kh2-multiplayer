# Prologue encounters (VUH-2466)

The Roxas days' combat and mission programs, from the game's own room scripts. Each encounter is a
battle program (`btl`), not a room: one room runs several, and the same room ID can hold a Dusk
wave, a Struggle match or a job. This is the checklist VUH-2466's two-player work is measured
against.

## Source

The table comes from the installed `ard/us/ttNN.ard` room scripts (all 42 Twilight Town rooms)
and `00objentry.bin`, read from the player's own install and parsed offline with OpenKH's
`AreaDataScript`/`SpawnPoint` layouts (`docs/OPENKH_REFERENCE.md`). No game file is shipped or
written; only program numbers, mission names, object IDs and counts are recorded here.

For each room, a battle program lists its `Party`, `Mission` and spawn sets. The enemy column counts
every non-player entity of a combat type (ZAKO 4, BOSS 3, L_BOSS 12, BTLNPC 9) in those sets. Live
logs name the same program as `btl=N` in `room instance 02/RR btl N`; room numbers there are hex
(`02/13` is room 19).

Programs 1, 2, 4, 5 and 7 are the rooms' free-roam Heartless/Nobody sets for Sora's later visits.
Programs with `Party DEFAULT`, or that only exist for Sora (the Tower, Station Plaza's `TT08_MS701`,
Betwixt and Between, Old Mansion `TT14_MS001`), are outside the Roxas days and left out.

The day column follows the mission number series (`MS1xx` Day 1 ... `MS6xx` Day 6), and the kind
column reads the spawn sets; character names come from model names only where noted. Both are
readings, confirmed only where the live column says so.

## Checklist

| Day | Room | btl | Mission | Enemies (object ID) | Kind | Live |
|---|---|---|---|---|---|---|
| 1 | 07 Tram Common | 95 | `TT07_MS101` | none | scripted, no spawn | checked site, `tt07-armor-shop-104` (VUH-2359/2487) |
| 1 | 07 Tram Common | 97 | `TT07_MS103` | none | scripted, no spawn | checked site (VUH-2487) |
| 1 | 07 Tram Common | 99 | `TT07_MS105` | none | cat lock-on | checked site (VUH-2487) |
| 1 | 04 Sandlot | 76 | `TT04_MS106` | none | scripted, no spawn | checked site `tt04-76` |
| 1 | 04 Sandlot | 77 | `TT04_MS107` | N_EX570_BTL (1656) | one-on-one bat fight | not yet |
| 1 | 04 Sandlot | 78 | `TT04_MS108` | N_EX570_BTL (1656) | one-on-one bat fight | not yet |
| 1 | 14 Old Mansion | 127 | `TT14_MS109` | 1 Dusk M_EX990 (318) | early Dusk | not yet |
| 1 | 14 Old Mansion | 128 | `TT14_MS110` | 1 Dusk (318) | early Dusk | not yet |
| 2 | 02 Usual Spot | 2 | `TT02_MS201` | none | scripted | not yet |
| 3 | 04 Sandlot | 79 | `TT04_MS301` | 1 Dusk (318) | early Dusk | not yet |
| 3 | 32 Station of Serenity | 154 | `TT32_MS302` | 3 Dusks (318) | Dive: weapon tutorial | not yet |
| 3 | 32 Station of Serenity | 155 | `TT32_MS303` | none | Dive | not yet |
| 3 | 33 Station of Calling | 1 | none | 10 Dusks (318) on the path | Dive | loopback from FM-00, both seats fight all 10 (VUH-2466) |
| 3 | 34 Station of Awakening | 157 | `TT34_MS304` | B_EX100 (307), Twilight Thorn | boss | not yet |
| 4 | 03 Back Alley | 180 | `TT03_MS400` | N_EX570_BTL (1656) | one-on-one bat fight | not yet |
| 5 | 04 Sandlot | 181 | `TT04_MS500` | N_EX570_BTL (1656) | one-on-one bat fight | not yet |
| 5 | 05 Sandlot (Struggle) | 84 / 189 | `TT05_MS401` | N_EX500_BTL (1239); 189 adds 3 Dusks | Struggle match | not yet |
| 5 | 05 Sandlot (Struggle) | 85 | `TT05_MS402` | N_EX610_BTL (2156) | Struggle match | not yet |
| 5 | 05 Sandlot (Struggle) | 86 | `TT05_MS403` | 3 Dusks (318) | time-stop Dusks | not yet |
| 5 | 05 Sandlot (Struggle) | 87 | `TT05_MS404` | B_EX110_SKIRMISH (2229), Axel | boss skirmish | not yet |
| 5 | 05 Sandlot (Struggle) | 88 | `TT05_MS405` | N_EX600_BTL (2095) | Struggle match | not yet |
| 6 | 03 Back Alley | 73 | `TT03_MS601` | 2 Dusks (318), 1 Dusk (309) | Dusk ambush | not yet (FM-01 route) |
| 6 | 19 Basement Hall | 134 | `TT19_MS602` | 4 Dusks (318), then 2 (309) | Mansion Dusk ambush | checked site `tt19-134`; loopback from FM-02 |
| 6 | 20 Basement Hall | 137 | `TT20_MS603` | B_EX110 (81), Axel | Axel I | loopback, VUH-2465 |

Jobs and minigames (host-owned, solo activities; `Party NO_FRIEND`, no Dusks):

| Room | btl | Mission | Notes |
|---|---|---|---|
| 04 Sandlot | 182 / 183 / 184 | `TT04_MS00` / `MS01` / `MS02` | practice matches vs N_EX500/600/570 |
| 06 Station Heights | 89–91, 190–195 | `TT06_WORK_BAGGAGE` / `PERFORM` / `LETTER` (+ `_01`, `_02`) | the performance job spawns eight BTLNPC performers |
| 07 Tram Common | 100–102, 196–201 | `TT07_WORK_CLEAN` / `WORM` / `POSTER` (+ `_01`, `_02`) | `WORM` spawns 8 F_PO090_TT (2138) |
| 10 Sunset Terrace | 120 / 121 | `TT10_WORK_TIMBER` / `MIRROR` | `MIRROR` spawns a P_EX130 (1876) |
| 12 Sunset Hill | 125 | `TT12_WORK_BOX` | |
| 36 Underground Concourse | 159 | `TT36_WORK_FIGURE` | 14 N_EX610_BTL2 (2256) |

## Rehearsal saves

PC1's rig saves reach only part of this list: FM-00 starts on the Station of Serenity save point
(room 32, evt program 2, chest already open), FM-01 at the Usual Spot on Day 6, and FM-02 in the Computer
Room on Day 6. They're rehearsals; opening-route evidence comes from New Game.

From FM-00 (2026-10-19, PC1 loopback, VUH-2466):
- The Station of Serenity's way on is a door standing at the platform edge, about (60, 600). It is
  map geometry, not an actor; `F_EX030_TT` at (-450, -328) is the chest, already open in this save.
  Walking into the door is the trigger, with no reaction command. The friend is warped along to the
  Station of Calling (`02/21 btl 1 evt 0`).
- The Station of Calling's btl 1 set (`b_00`) places its 10 Dusks along the stained-glass path that
  leaves the platform toward -z, in five groups of 1, 1, 3, 2 and 3. None are on the platform. Walking
  the path in order from (2020, -500) through (2000, -1350), (2200, -2150), (800, -2341), (0, 250)
  and (-50, 2700) meets each group. With both seats' pilots on that route (background pilots), both
  Roxases fought all 10 with Reversal. Neither was hit, and both logs record the same 10 deaths.
