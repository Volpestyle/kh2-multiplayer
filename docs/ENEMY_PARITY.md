# Enemy parity across instances (VUH-1499 spike)

Question (plan D5, hard problem 3, risk R4): with the same save and room, do
two instances spawn the same enemies, and what key matches a host enemy to
its client copy?

## Method

`tools/scenario/spikes/enemy_parity.py` (2026-10-02):
- Boot two rig instances from the same save.
- Per room:
  - Warp both instances there (default battle program, or a forced one with
    `--btl`).
  - Keep both Soras untouchable.
  - Drive both with the same inputs, interleaved, about 1 s apart.
  - Record each enemy's first sighting on each instance: name (objentry),
    entity-list order, actor address and position. Enemies are objentry type
    3/4; F_ objects are excluded.
- Later waves: kill every enemy on both sides with `kh2ctl hit kill`,
  repeating rounds until both lists are empty, then record what spawns
  next.
- Rooms: from `tools/scenario/spikes/combat_rooms.py`, a sweep of 224
  world/room pairs with default programs plus 144 with forced battle
  programs 1–3.
  - On this save, six rooms spawn enemies with default programs. Forced
    programs add many more.
  - 40 rooms were held by events or cutscenes.

## Results (first wave; 14 room setups, 3 worlds)

| room (btl) | enemies | count A/B | name+order match | address match | spawn pos delta median / max (units) |
|---|---|---|---|---|---|
| 05/00 | M_EX520 | 4/4 | 100% | 75% | 13.7 / 13.7 |
| 05/00 btl 3 | M_EX050, M_EX690 | 3/3 | 100% | 67% | 0.0 / 1.6 |
| 05/06 | M_EX020 | 5/5 | 100% | 80% | 0.0 / 0.0 |
| 05/08 | M_EX020 | 3/3 | 100% | 100% | 0.0 / 0.0 |
| 05/09 btl 1 | M_BB010_SWORD, M_EX520 | 3/3 | 100% | 100% | 13.7 / 13.7 |
| 05/0B btl 2 | M_EX520, M_EX690 | 6/6 | 100% | 0% | 6.9 / 6.9 |
| 05/0C btl 1 | M_EX020 | 3/3 | 100% | 100% | 0.0 / 0.0 |
| 08/05 btl 1 | M_EX660 | 5/5 | 100% | 0% | 15.6 / 15.6 |
| 12/03 | M_EX020, M_EX420 | 4/5 | 80% | 40% | 272 / 603 |
| 12/06 btl 2 | M_EX880, M_EX920 | 4/4 | 100% | 100% | 3.8 / 33.5 |
| 12/0B btl 1 | M_EX900, M_EX920 (two sub-waves) | 6/6 | 100% | 100% | 0.0 / 0.5 |
| 12/0C btl 2 | M_EX880, M_EX910 | 4/4 | 100% | 100% | 14.9 / 14.9 |
| 12/0E btl 1 | M_EX950, M_EX990 | 3/3 | 100% | 100% | 0.0 / 0.0 |

Position deltas of a few units come from the instances first sighting an
enemy at slightly different moments of its spawn animation (inputs run about
1 s apart). 08/00 is left out: its type-3 actor `B_MU110` has no status block
(HP reads −1) and isn't a combat enemy.

### Findings

- **Same set, same order, same spawn points.** 12 of 13 rooms matched
  exactly on name and spawn order, with spawn positions equal within the
  sampling noise above. That includes a timed second sub-wave (12/0B:
  four M_EX900, then two M_EX920 about 3.8 s later).
- **The battle program decides the set.** 05/00 spawns 4× M_EX520 with the
  save's program but M_EX050 + M_EX690 with btl 3. So it has to be in the
  key.
- **Actor addresses are not a key.** Addresses matched 0–100% depending on
  the room (05/0B: every address on one side offset by exactly 0xA0, i.e.
  one extra allocation). Actor pool slots depend on allocation history.
- **Behaviour diverges immediately after spawn.** Positions drift apart
  within a second (each instance runs its own AI), as D5 expects. The host
  has to own enemy state.
- **Continuous spawners diverge (12/03).** The first spawns match. After
  that, Shadows keep appearing at times and places that depend on where
  Sora is, and the counts differed (4 vs 5) in both runs.
- **Wave ends.** With every enemy killed on both sides, battles ended the
  same way on both: no further wave in any of the rooms tested. The one
  asymmetric "wave 2" (12/0B, first run) came from a kill round that missed
  two enemies on one side, which kept that side's battle running. With kill
  rounds repeated until both lists were empty, it disappeared.

## Chosen correlation key

`(world, room, battle program, spawn sequence index, objentry name/id)`.
The spawn sequence index is the order in which the room's spawner emits the
enemy, counted from room entry. The key is type-checked on every message,
and the actor address is never used.

Match rate on this data: 12 of 13 rooms at 100% and 1 of 13 (a continuous
spawner) at 80%, so 53 of 54 first-wave enemies were matched by the key.

## Fallback for spawners (scoped, not built)

For rooms whose spawner emits enemies over time based on player position
(12/03 class), use host-commanded spawns:
- Clients suppress that spawner's local emissions.
- The host sends `{spawner key, objentry, position}` per spawn, and clients
  create the enemy through the same spawn call the spawner uses.
- The spawner's identity (room + btl + spawner entry index) still comes from
  the static room data.
- Detection: a room is a "spawner room" if a host spawn arrives with no
  local match within a short window; it can then be marked statically per
  room.

## Limits

- One save (BC first visit), three worlds (05, 08, 12). Forced battle
  programs stand in for story progress.
- Spawns were triggered with the same inputs about 1 s apart; a real
  session has players arriving at different times. That is exactly where
  continuous spawners diverge.
- No boss fight was reachable on this save (see VUH-1501).
