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

## Step 1 implementation (VUH-1502)

`inject/src/EnemySync.cpp`, over the WorldBridge. The role comes from the
runtime's session slot (`WorldBridge::LocalSlot`: 0 = host, 1–2 = client);
`KH2COOP_ROLE=host|client` overrides it for tests.

- **Room instance.** Native transition-request and load-completion hooks in
  `Warp.cpp` advance separate generations. Requests and completed loads clear
  cached enemy actors, including same-room reloads; spawn tracking pauses
  while a transition is pending. A new instance starts on a gameplay frame
  after native completion. NOW changes and frame stalls alone do not establish
  arrival; lifecycle details are in `pointer_map_v1.md`.
- **Host.**
  - Every instance: `epoch++` and a `RoomTransition`. When hosting starts
    mid-room (the runtime connected late), the room and everything already
    spawned go out at once.
  - New enemies go out as `EnemyManifest` (netId = spawn index + 1).
  - `EnemyHp` goes out about every 6 frames.
  - `EnemyDeath` is sent once per netId, when HP first reaches 0.
- **Host despawns.** Some enemies leave the list alive: the courtyard
  Shadow on its ledge spawn point, host only; and Dusks (M_EX900), which
  vanish and reappear as new actors.
  - If a new enemy appears at the same spawn point, the old one was
    superseded and clients re-bind to it.
  - If none appears within 3 s, it's reported as a death, so clients don't
    keep a copy that would hold the room open.
- **Client.**
  - Replays each accepted host epoch through the native transition path.
    Arrival requires a load completion after that request, the playable-state
    gate, and exact world, room, entrance, map, battle and event programs.
    Enemy matching waits for arrival; `TransitionAck` retries if the outgoing
    bridge ring is full.
  - Matches each local spawn by spawn point + objentry to the host's newest
    enemy at that point. Spawn index is the fallback.
  - A new local spawn binds only to a live host entry, and waits for the
    manifest otherwise. A bound copy follows re-binds, including to a dead
    entry, which is how deaths reach it.
  - Holds matched HP at the host's absolute value (never writing 0).
  - Applies host deaths through `ApplyStatDelta(-hp)`, the native death
    path.
  - Zeroes its own hits on enemies (`[drop]`): the host owns enemy HP.
- **Why spawn points rather than spawn order.** Spawn order breaks as soon
  as one machine despawns and refills a point and the others don't.
  Spawn points matched to 0.0 units across instances in the spike above.

Verified 2026-10-02 (`net_enemy_sync_courtyard`, `net_enemy_sync_waves`:
host + 2 clients, relay, host-only damage):
- **HP:** the same on all three after host damage (courtyard 20 → 13,
  12/0B 160 → 153), read 0.4 s later.
- **Deaths:** every host kill killed each client copy exactly once (5 and 6
  deaths per client).
- **12/0B second wave:** spawned on all three, bound to the host's new
  entries and cleared.
- **Battle end:** battle state ended (0) on all three.

**Transition-lifecycle regression evidence, 2026-10-02.** The
[courtyard rerun](../build/scenarios/20261002-145659_net_enemy_sync_courtyard_1/report.json)
failed its after-client-hits population/HP equality check. The original five
Shadows were at 13 HP on every instance after host damage and remained there.
Client 1 independently spawned a sixth Shadow at `(-40,-1,-349)`, with 20 HP
and no host manifest match. Its four subsequent hits targeted that actor and
were zeroed ([client log](../build/scenarios/20261002-145659_net_enemy_sync_courtyard_1/kh2coop_inject_99000.log)).
The failed assertion's message describes changed enemy HP; the raw samples
show a real extra-client-actor mismatch. No further epoch or load occurred
during that mismatch. Keep this failure as unresolved spawn divergence.

The earlier passing courtyard run also had native despawn/refill variation,
eventually settling at four enemies. The new lifecycle intentionally performs
a real initial follow reload even when the location matches; the old fixture
only recorded that first epoch. Player/camera trajectories were not captured,
so these reports cannot establish why the additional spawn appeared. Shared
HP/death checks on fixed scripted waves provide separate lifecycle evidence;
they do not retire the courtyard population mismatch.

The [fixed-wave rerun](../build/scenarios/20261002-150336_net_enemy_sync_waves_1/report.json)
**passed** with the new lifecycle (154.8 s). Four initial enemies changed from
160 to 153 HP on all three instances after host damage. The second wave had
two 160-HP enemies on every instance; each client recorded six native death
applications in total, and battle state ended at 0 everywhere. Four save-file
hashes were unchanged. This verifies fresh bindings and HP/death application
after real join reloads, while leaving spawn convergence open.

**Actual-state hash evidence, 2026-10-02.** The
[nonempty hash run](../build/scenarios/20261002-184220_net_statehash_nonempty_1/report.json)
passed with three instances (180.2 s). Four live native enemies had matching
bindings, object IDs and 160 HP on all peers, then 153 HP after host damage.
The scenario independently recomputed the published enemy hash from each
peer's native observation rows. Unmatched living actors participate in the
hash with net ID 0; missing, extra or duplicate actors cannot be hidden by
hashing the received host cache. Positions are excluded while AI runs locally.

That run used actual native actors observed by the per-entity update hook,
rather than the received host cache. Callback coverage alone did not establish
that every linked native actor entered the hook. The subsequent independent
census below resolves that uncertainty for its sampled courtyard reproduction.

The progress hash reads 8,108 masked, verified SAVE bytes. A client-only
change to chest flag 409 produced a different actual progress hash and the
relay's progress mismatch bit (`fields=4`). Restoring that bit restored
agreement; complete before/after snapshots showed no remaining changes in
the shared ranges or sampled character, inventory, munny and EXP ranges.
All four on-disk save hashes were unchanged. This proves detection and
restoration of a progress mismatch; automatic resync, a deliberately failed
native enemy death, and hash agreement across the full transition suite need
separate evidence. The courtyard population failure above remains open.

The enemy negative-control runs remain **overall failures**. In the
[two-participant run](../build/scenarios/20261002-192119_net_statehash_enemy_negative_isolated_1/report.json),
the detection checkpoint passed twice: a client HP-lock bit prevented the
native lethal call (`153 -> 153`), the actual living target remained in its
hash, and the relay reported `DesyncEnemies` (`fields=2`). The temporary bit
was restored with native identity/epoch checks, preserving other flags.
The later reload reached epoch 2 on both peers but failed because the native
friend pointers were null and no remote Sora actors existed in `12/0B`, battle
program 1. Logical avatar poses alone did not satisfy the native actor check.
This establishes detection/bit restoration, not successful reload recovery.
The [separate party diagnostic](../build/scenarios/20261002-194936_net_party_availability_probe_1/report.json)
passed its observation steps (95.3 s), without combat or the enemy fault.
Both peers had valid companion pointers in the loaded-save room, then null
friend pointers on first native entry into `12/0B` **before networking**.
They remained null at settled connected epoch 1 and identical reload epoch 2.
The world-18 party word stayed `0x12121200` throughout; each actor sample
contained only local Sora, PRIZE and W_EX010, while connected logical puppet
poses were active. All four on-disk saves were unchanged. This establishes
preexisting native companion absence in this fixture, consistent with plan
risk R12; it does not establish native puppet support or change the failed
negative-control recovery result.
The earlier three-participant controls also exposed an independent client-2
spawn of an unmatched 160-HP object 309. Keep those failures and the strict
population/puppet checks; neither spawn convergence nor recovery is complete.

The [20-load hash route](../build/scenarios/20261002-193320_net_host_transitions_statehash_acceptance_1/report.json)
also **failed** (203.3 s), after ten passing fresh hash checkpoints. At route
transition 8, epoch 10, all three peers had arrived at the complete location
`05/06, door 0, map 1, battle 1, event 0`; native puppet checks passed. The
host published no observed enemies, while both clients published five living,
unmatched object-302 actors at 20 HP. Their enemy hashes differed
(`401581688` versus `712275580`, relay `fields=2`), while all progress hashes
were `105337703`. All four on-disk save hashes were unchanged.

That report did not capture an independent native-list snapshot, so it could
not establish host absence. Native spawn appearance-ID caches and controller
state also lie outside the SAVE progress hash. The subsequent checked census
below supplies those missing observations. The strict route remains failed;
matching progress bytes alone do not prove matching enemy activation.

The [subsequent checked census](../build/scenarios/20261002-200856_net_enemy_census_transition08_1/native_enemy_census_transition08.json)
collected three complete native-list/cache snapshots on every peer; the
retained strict hash check still **failed** (208.9 s). The first snapshot had
zero native enemies and an empty active cache everywhere. The later two had
zero host enemies and five living client enemies, agreeing with each published
hash population. This establishes real spawn divergence in this run, without
a hidden host actor in the update registry.

The same type-2 controller (header ID 30) had flags `2` on the host and `10`
on both clients after their appearance, while each client cache gained record
IDs `11,12,13,14,18`. The host cache stayed empty. Its activation actor remained
near `(45,-260,1950)`; the clients moved near `(-1019,-180,1636)` and
`(-923,-206,1638)`. Native analysis identifies controller bit 3 as an accepted
activation region. These observations support local activation as the next
authority boundary to investigate; they do not validate a hook or suppression
strategy. Initially equal caches also rule out differing retained IDs as a
sufficient explanation for this particular reproduction. All four disk save
hashes were unchanged.

**Checked native-list coverage.** EnemySync now commits presence only from a
complete canonical native-list traversal with checked metadata, links, tail,
handle regions and lifecycle. A failed read, cycle, changing list or continuation
beyond the 256-node safety bound makes the entire sample unavailable; it never
publishes a truncated or invented empty roster. Unknown intervals restart
despawn grace and suspend enemy writes/messages/hashes. Checked native target
identity guards HP/lethal calls, and a fresh post-application census supplies
hash rows independently of tracker presence. Unmatched live client actors
remain visible. This corrects coverage; it does not synchronize spawn activation.

The [native-wave regression](../build/scenarios/20261002-201456_net_enemy_sync_waves_1/report.json)
passed (153.6 s) after the change: two real second-wave enemies, six native
deaths on each client and battle state zero everywhere. The
[nonempty hash regression](../build/scenarios/20261002-201730_net_statehash_nonempty_1/report.json)
also passed (166.1 s), including actual HP/hash agreement and reversible
progress mismatch detection. Its later empty second-wave observation remains
separate from the real-wave proof. All four disk save hashes were unchanged.

The [focused native-death detector](../build/scenarios/20261002-202126_net_native_census_death_control_1/report.json)
passed (102.4 s) on the same DLL: the HP-locked client target survived its native
lethal call, appeared in two fresh actual hash samples with relay `fields=2`,
and its temporary bit was restored. All four saves were unchanged. The fixture
ends after detection/restoration; it does not replace the still-failed full
negative-control reload-recovery scenario.

The next activation change needs a host-authored native position sample tied
to the epoch, complete location and session, with source sequence/liveness.
Existing visual puppet poses have only world/room and can remain held across
reloads, so they cannot supply that authority. A scoped native `0x3FF000`
position substitution is a candidate only for validated ordinary combat type-2
controllers with static-position records; broader modes, type 9 and seven
header-30 region geometries still require separate evidence.

Not covered yet:
- Bosses: none reachable on this save.
- Drops and barrier objects (battle state stands in for "barriers lift").
- Continuous spawners.
- Client hits as claims (VUH-1501).
- Hash agreement across repeated native room loads and varied populations,
  plus successful recovery after the enemy mismatch control. The nonempty
  proof covers one fixed-wave room; it does not establish spawn convergence
  or native remote actors after its battle-room reload.
