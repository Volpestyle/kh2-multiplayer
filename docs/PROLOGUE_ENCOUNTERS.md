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
| 1 | 04 Sandlot | 77 | `TT04_MS107` | N_EX570_BTL (1656) | one-on-one bat fight (Seifer) | checked site `tt04-77`; both seats rebuild and hit Seifer; a friend that runs ahead waits and follows the host to 78 (VUH-2555) |
| 1 | 04 Sandlot | 78 | `TT04_MS108` | N_EX570_BTL (1656) | one-on-one bat fight | checked site `tt04-78`; both seats rebuild, fight and leave for the Woods together (run `20261020-100758`) |
| 1 | 14 Old Mansion | 127 | `TT14_MS109` | 1 Dusk M_EX990 (318) | early Dusk | checked site `tt14-127`; both seats fight, loopback from FM-01 (below) |
| 1 | 14 Old Mansion | 128 | `TT14_MS110` | 1 Dusk (318) | early Dusk | checked site `tt14-128`; both seats fight and defeat it (below) |
| 2 | 02 Usual Spot | 2 | `TT02_MS201` | none | scripted | checked site `tt02-2`; both seats released (below) |
| 3 | 04 Sandlot | 79 | `TT04_MS301` | 1 Dusk (318) | early Dusk | checked site `tt04-79`; both seats fight, loopback from FM-01 (below) |
| 3 | 32 Station of Serenity | 154 | `TT32_MS302` | 3 Dusks (318) | Dive: weapon tutorial | checked site `tt32-154`; both seats fight; the last death can miss the friend (below) |
| 3 | 32 Station of Serenity | 155 | `TT32_MS303` | none | Dive | checked site `tt32-155`; both seats released (below) |
| 3 | 33 Station of Calling | 1 | none | 10 Dusks (318) on the path | Dive | checked site `tt33-calling` (VUH-2570): both seats see each other and fight all 10, loopback from FM-00 |
| 3 | 34 Station of Awakening | 157 | `TT34_MS304` | B_EX100 (307), Twilight Thorn | boss | not yet |
| 4 | 03 Back Alley | 180 | `TT03_MS400` | N_EX570_BTL (1656) | one-on-one bat fight | checked site `tt03-180`; both seats fight (below) |
| 5 | 04 Sandlot | 181 | `TT04_MS500` | N_EX570_BTL (1656) | one-on-one bat fight | checked site `tt04-181`, then `tt04-204`; both seats fight (below) |
| 5 | 05 Sandlot (Struggle) | 84 / 189 | `TT05_MS401` | N_EX500_BTL (1239); 189 adds 3 Dusks | Struggle match | checked sites `tt05-84` and `tt05-189`; both seats fight (below) |
| 5 | 05 Sandlot (Struggle) | 85 | `TT05_MS402` | N_EX610_BTL (2156) | Struggle match | checked site `tt05-85`, then the field `tt05-4`; both seats fight (below) |
| 5 | 05 Sandlot (Struggle) | 86 | `TT05_MS403` | 3 Dusks (318) | time-stop Dusks | checked site `tt05-86`; both seats fight, loopback from FM-01 (below) |
| 5 | 05 Sandlot (Struggle) | 87 | `TT05_MS404` | B_EX110_SKIRMISH (2229), Axel | boss skirmish | checked site `tt05-87`; both seats fight, loopback from FM-01 (below) |
| 5 | 05 Sandlot (Struggle) | 88 | `TT05_MS405` | N_EX600_BTL (2095) | Struggle match | checked site `tt05-88`; both seats fight, loopback from FM-01 (below) |
| 6 | 03 Back Alley | 73 | `TT03_MS601` | 2 Dusks (318), 1 Dusk (309) | Dusk ambush | checked site `tt03-73`; loopback from FM-01, both seats fight; the late 309 binds by kind (VUH-2550, below) |
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

## Decision: the Struggle score is the host's world (VUH-2466)

Decided by the lead on 2026-10-22 (rule A); James can overrule it. A Struggle match is won on orbs,
and each game keeps its own orbs, so one game's orbs have to be the real ones. They are the host's,
the same rule as the host-owned boss: the friend sees and collects the host's orbs, and the orbs the
friend drops when hit come from the opponent's host-world hits on the friend's Roxas there. The host's
own native counter calls stay the only writers of the score, so the match is no easier than vanilla
(each orb is collected once), and the result stays host-decided (VUH-2606).

The rejected alternative was each game deciding its friend's own pickups and losses, sent to the host
as counter claims. With two sets of orbs, the same drop could then be collected once in each game.

Native facts behind it (read-only Ghidra of the installed exe, nothing copied):
- Every orb change is a direct call to `exe+0x3FB2B0(delta, side)`, not a script call, so the
  friend's mission mirror never sees its own changes; it only overwrites them with the host's. The
  side is `~(actor+0x6C8 bit 7) & 1`: Roxas's side is counter 0, the opponent's counter 1.
- A hit takes orbs at `FUN_140410100` (called from the hit path `FUN_1403d2fe0`): 3 or 15 by attack
  class, capped at the count, and spawns that many orb prizes.
- An orb prize's update (`FUN_14040ff50`, `FUN_140410250`) gives +1 to the side of the first actor
  in the active list (skipping `+0x120 & 0x10080000`) that touches it. The walk takes every actor with `+0x6C8` bit 1 (set by the
  character constructor `FUN_1403d2da0`), except one in motion states 10-39 or 140-150, with `+0x194`
  bit 6, or with `+0x9B8` bit 2 (no contact).
- The friend's Roxas on the host is built by `FUN_140405030` (the clone's logged constructor caller,
  `0x40505B`), which calls the player constructor `FUN_1403a7a40`. That sets bit 7 and, through
  `FUN_1403d2da0`, bit 1. So the clone is on Roxas's side and in the pickup walk: statically, a
  host-world orb it touches counts for the team. No log yet shows the clone collecting one.
- In btl84 (run `20261021-015611`) Hayner hit the friend's clone on the host 8 times of 13, and the
  host's counter 0 fell on those hits: the friend's losses already come off the team score.

How the friend sees the host's orbs (protocol 34; `inject/src/StruggleOrbs.hpp`, `StruggleOrbsAdapter.hpp`,
`StruggleOrbsHook.cpp`):
- The host gives every orb born at its orb manager (`*(exe+0x2AEA190)`, through `FUN_1403dc950`) an ID
  and sends its birth (descriptor, point, scatter) and later its collection (the host's player, the
  friend's clone, an enemy by netId, or Unresolved when it cannot name the collector: the collection
  still counts) as SpawnEmission orb rows, in the same FIFO as controller births. The relay caches them
  per room, so a late friend gets them too. A row is checked against the codec before it is queued, and
  the flush drops any row the codec refuses rather than leaving it at the head of the FIFO.
- Orb IDs are unique through the room. Each orb manager the host sees in a room is a match (1, 2, ...),
  named in every orb row; the friend applies only the rows of the match its own manager shows (its first
  manager shows the newest queued match, each replacement the next). Both games observe their manager every
  frame, rows or not: a manager seen gone ends its match and retires every record the friend holds for it,
  so the next manager is the next match even at the same address. A loss and return within one frame is not
  seen. A friend whose manager is replaced before any orb row has reached it cannot tell which match the new
  manager shows, so it refuses every orb row until the room ends: it shows no orbs there, and the score
  stays the host's. Whether a manager is ever replaced inside one room is not known; each Struggle match seen so far is
  its own battle program.
- The friend, while it is a client in the host's room, skips its own orb drops (`FUN_140410100`,
  `FUN_140410090`) and pickups (`FUN_14040ff50`, `FUN_140410250`). It creates the host's orbs through the
  same allocator, puts the host's scatter on them and marks them to keep (as the native callers do), and
  ends an orb only when the host collected it: it flies to the friend's copy of the collector, or, with
  none (an unresolved collector, an enemy with no living copy), fades where it lies through its own life
  timer (`+0x3C`; the resting update `LAB_140413230` is `FUN_140413a80(record, 1)`). An orb already
  collected by the time its birth applies is never shown. At most 64 native calls a frame.
- The five detours install all or none and act only once all five are applied (one published state)
  and only on the game thread noted each frame: before that, on any other thread, or after a failed
  install, each calls its original once and reads nothing. Hooks that were never applied are removed (a
  failed removal is logged and counted); once the apply has run, a thread may be inside a detour about to
  call its original, so every hook and trampoline is kept, inactive, for the DLL's lifetime.
- The score itself is untouched: the friend's counters still follow the host's through the mission
  mirror. There is no pickup claim. The friend collects by its clone touching the host's orb on the
  host, a position latency behind; whether that misses orbs the friend brushes past is for the first live
  run after the freeze. `KH2COOP_STRUGGLE_ORBS=0` leaves each game its own orbs.
- `kh2coop_struggle_orbs_test` replays btl84's logged counter sequence as host births and pickups: at
  every 120-frame sample the friend shows exactly the host's orbs on the field (10 at the end).

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

From FM-01 (2026-10-20, PC1 loopback, VUH-2466):
- The Usual Spot's way out is the door toward +x, trigger near (369, 83) (`tt1_solo` step 13). It loads
  the Back Alley straight into the Day 6 ambush, btl 73 (`02/03 evt 73`).
- Its player ingress is `raw566` plus the absent `raw567/568` (headerId 90), byte-identical on both
  seats, so it is pinned as checked site `tt03-73` (run `20261020-043937`). In run `20261020-044700`
  both seats rebuilt as Roxas + Roxas there with seat colors, released together, and fought the two
  318 Dusks natively. Both died in 46 s, with host hit claims, and Dusk hits on both Roxases.
- The third enemy (object 309) appears once the two 318s are gone. It spawns at a different point on
  each seat, about 80 to 500 units apart, in this run and in the unchecked probe alike. The client's
  copy fails the ordinary 8-unit point binding (`no-point-match`), so it stays unbound with unscaled
  HP, and the relay flags an enemy-hash desync (VUH-2550). That is the native placement divergence that record
  binding (`docs/ENEMY_RECORD_BINDING.md`) addresses, not a prologue-layout effect.
- VUH-2550: the ordinary binder now binds a body by kind when it is the only fresh, never-bound,
  unharmed living body of its kind with no point match and the host has exactly one living,
  never-claimed entry of that kind without one (`only-unmatched-of-its-kind`), and keeps that binding
  while it stays continuous. In run `20261021-025442` the friend's 309 bound at once, took the host's
  death, and the relay raised no desync.

From New Game (2026-10-20, PC1 loopback, VUH-2466):
- The Day 1 bat duel starts from the Struggle bats lying in the Sandlot (`W_EX010_U0/V0/W0_RTN`).
  Their reaction is "Pick It Up" (555 to 557). The pick opens a description, then "No, I'll use a
  different one / This will do!" with the cursor on No: Down, then Cross, starts the duel (btl 77).
- Its player ingress is a four-record group, `raw566`, the absent `raw567/568` and the opponent
  `N_EX570_BTL` 1656, under key 825515874 (headerId 71). It was byte-identical on both seats (run
  `20261020-071814`) and is pinned as `tt04-77`. In run `20261020-080107` both seats rebuilt as
  Roxas + Roxas there and released together. The battle tutorial boxes need Cross before anyone can
  move. Both seats hit Seifer, with the friend's hits claimed to the host, but about 8 s in the duel's
  event played on the friend alone, which then waited for the host (VUH-2555).

From the Day 1 route and FM-01 (2026-10-20, PC1 loopback, VUH-2466):
- The Woods' way to the Old Mansion: from the entrance, (0, -500), (900, 800), (1400, 1600), then north
  through the exit near (1430, 2060) (minimap; run `20261020-155349`). The Mansion front loads as evt1.
- The Day 1 Dusk fight starts from the Zipper Dusk (`M_EX990_ZIPPER_RTN`, object 2317, an inert object
  by the gate): its reaction 559 (Triangle) plays an event and loads btl127 (run `20261020-224246`). The
  gate itself only says "It's locked tight."
- btl127 and btl128 are both raw566, the absent raw567/568 and the Dusk 318, pinned as `tt14-127` and
  `tt14-128`. In run `20261020-230628` both seats released with both Roxases at each stage, fought the
  Dusk, and the fight's own stage change moved both to btl128, where both logged the same Dusk death.

From FM-01 with a host warp (2026-10-21 UTC, PC1 loopback, VUH-2466):
- btl79 (the Day 3 Sandlot Dusk) is raw566, the absent raw567/568 and the Dusk 318: `tt04-79`. Its fight
  ends by script and both seats go into the Dive, loading the Station of Serenity at door 50 evt2, which
  is `tt32-2` (two entrance triples; FM-00's save loads the same room at door 99, which stays unpinned).
  Run `20261020-234113`: both seats released with both Roxases at each.
- btl154 (Serenity's weapon-tutorial Dusks) is raw566, raw567/568 and three Dusks 318: `tt32-154`. Run
  `20261020-234512`: both seats released with both Roxases and fought. The host's third death arrived on
  the friend just as the mission-complete event closed its gameplay gate and was not applied (the Dusk
  stayed at 15 HP there; the relay captured a desync). The unpinned probe applied all three.

Day 5 Sandlot fights, FM-01 with a host warp (2026-10-21 UTC, PC1 loopback, VUH-2466):
- btl86 (three time-stop Dusks 318, headerId 122), btl87 (Axel's skirmish, B_EX110_SKIRMISH 2229,
  headerId 144) and btl88 (the Struggle match, N_EX600_BTL 2095, headerId 166) are each raw566, the
  absent raw567/568 and their enemies: `tt05-86`, `tt05-87`, `tt05-88`. In runs `20261021-005228`,
  `-005604` and `-005934` both seats released with both Roxases and fought.
- The rig's fight bot loses btl86 and btl87 on the host (native game over: the downed spike is off in
  these fixtures), and the friend fights on alone.
- The Struggle match used to end out of step: a mid-match event closed the host's gameplay gate, the
  host stopped publishing its mission, and the friend released and ran its own clock (run
  `20261021-005934`). The host now publishes through that hold (VUH-2606); in run `20261021-022350` the
  friend followed to the end and finished with the host's result (`client fail like the host`).

More sites, FM-01 with a host warp (2026-10-21 UTC, PC1 loopback, VUH-2466), all from byte-identical
receipts on both seats; in each run both seats released with both Roxases:
- `tt03-180` (Day 4 Back Alley bat fight, N_EX570_BTL; run `20261021-014249`).
- `tt04-181` (Day 5 Sandlot bat fight) and its second stage `tt04-204`, whose enemy 1314 is record 0
  and raw566 record 1 (run `20261021-014920`).
- `tt05-84` (N_EX500_BTL; run `20261021-015611`), `tt05-85` (N_EX610_BTL) and the Struggle field after
  it, `tt05-4` (door 53 evt4, five entrance triples, door 53 uses index 9; run `20261021-015949`), and
  `tt05-189` (three Dusks; run `20261021-015242`; one Dusk read 37 HP on the friend and 60 on the
  host at the end: its friend copy came up 23 units from the host's and stayed unbound and unscaled,
  the case VUH-2550's kind rule now binds; `OrdinaryStruggle189` replays it).
- `tt02-2` (the Day 2 Usual Spot, reached by a transition; run `20261021-014623`) and `tt32-155` (the
  Station of Serenity, evt155; run `20261021-020257`).
