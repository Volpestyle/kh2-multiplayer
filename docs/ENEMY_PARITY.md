# Enemy parity across instances (VUH-1499 spike)

Question (plan D5, hard problem 3, risk R4): with the same save and room, do
two instances spawn the same enemies, and what key matches a host enemy to
its client copy?

## Open native-wave geometry finding, 2026-10-05

The strict impaired Mac-relay wave run remains unpassed. In `094748`, the
original first-pack damage and exact-ID/HP census gate passed after 2.26 seconds,
but the first permitted 250 ms correction moved outward: radius 563.030 to
605.943. The fixture stopped before another pulse or kill. The approved limit
stays three measured 250 ms pulses, with immediate failure on outward movement
or overshoot; no further input budget is authorized.

Drift also occurs before damage. In `095701`, two qualified neutral endpoint
samples 5.907 seconds apart showed the same host actor moving from radius
58.208 to 224.005 before the first hit. The following read-only run `100816`
collected a 3.5-second interval outside the original hit gate, then intentionally
failed without issuing damage or correction. Its 140 rows included 138 qualified
samples across owner frames 9340–9549. Those samples retained neutral raw and
processed input, idle motion and zero sampled velocity at actor+0xB98, while
the actor moved 309.349 planar units. Actor, room, controller and camera
identities stayed fixed. Two rows crossed frame boundaries and remain
unqualified; sequential reads do not establish unsampled continuity.

Zero sampled velocity does not identify the position writer or exclude native
collision, carried displacement or other motion terms. The source audit found
no demonstrated local transform overwrite in this launch configuration;
network pose activity alone does not prove a puppet transform was applied.
The existing protection helper changes team membership, not collision or
motion. Attribution continues through read-only native-field/static analysis,
with no speculative suppression. All four protected saves stayed unchanged;
the owned relays stopped and fetched receipts match their remote originals.
See the [interval result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/neutral-observe-root02/result.md)
and [source audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-motion-source-audit/result.md).

The bounded caller diagnostic `115734` narrowed the observed writes to the
native position calculator's solved-position copy. All 61 captured candidates
had complete context/value/stack reads at leaf1A8E69, with stack-top candidate
return3B9561 and 21 distinct finite X values. Retained leaf/caller disassembly
supports the copy from actor+700 to actor+670 at3B955C. All 24 surrounding sparse
native samples qualified, including before, during and after the one-second
watch, and movement occurred with neutral sampled input and zero sampled B98.
The originating force remains unknown. The watch lacks a DR6 audit and per-hit
timestamps; the copy does not establish a unique writer or continuous neutral
execution. This intentional diagnostic FAIL and the earlier incomplete112744
FAIL remain intact. See the
[caller result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/caller-motion-root02/result.md)
and [independent interpretation](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/caller-motion50-analysis/result.md).

The private typed-RPM census transport passed live equivalence in `130211`:
all retained fields and entities responses matched the CLI on three paused
games. The paused canonical census kept gameplay eligibility false and skipped
its causal-context branch; this is transport parity only. The unchanged
75-step wave attempt `130654` then reduced the complete two-snapshot pre-hit
census from 16.66 seconds to 0.640 seconds. Host damage needed no correction,
and all original four IDs matched HP153 at a fresh hash and a strictly later
independent native HP snapshot. The full run still failed before kill: radius
268.812 grew to 366.348 after the first 250 ms pulse, triggering immediate
abort. Later-wave/death acceptance remains unexposed. See the
[transport comparison](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/census-equivalence-root01/result.md)
and [wave result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/census-fast-waves-root01/result.md).

Endpoint control `131241` moved 325.571 planar units over a QPC-qualified
16.661754-second gap with no added observer reads. Both endpoints had neutral
raw/processed input, safe gameplay, stable identities and advancing owner
frames. Bulk census reads are therefore not required for movement. The normal
protection actor/team loop, renderer, runtime and network continued; endpoints
do not prove continuous neutrality or causal read-load scaling. Enemy contact,
knockback and terrain remain possible contributors. All four saves stayed
unchanged in these runs; owned processes and relays closed. See the
[endpoint result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/no-observer-endpoints-root02/result.md).

## Current resync binding, 2026-10-04

The current protocol9/WorldBridge11 resync path now qualifies the complete
ordinary native catalog and exact actor-to-record membership before any HP/death
write. Full projected header/ordered record bytes and native record index supply
the resync association; spawn point and manifest observation order cannot supply
a fallback while fenced. This does not change the historical ordinary-message
key or establish controller incarnation, continuous lifetime or creation.

The unchanged 89-step courtyard population fixture passed in201.7s on one PC
over loopback (`20261004-043836`). The host and both friends qualified all10
definitions/26 records and five living references; each friend returned five
native20-to-requested17 positive stores, then observed all five atHP17/max20 on
two distinct native frames. Fresh independent censuses/hash samples preserve
the original IDs1-5/object302/type4 and progress. All101 prelaunch inputs/four
saves remained unchanged through terminal verification, and all8 owned
processes exited. [Exact result and evidence](FORCED_RESYNC.md#native-record-content-candidate-2026-10-04).

This is living Bootstrap content/HP acceptance. The outside-region first-rejoin
FAIL (5/0/5), zero complete ten-cycle repetitions and disabled creation remain;
the absent surviving pack still needs safe reconstruction. Positive geometry,
dead recovery and physical remote acceptance remain open. The observations and
chosen ordinary key below retain their original historical scope.

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

**Current first-pack evidence, 2026-10-03.** The
[provenance run](../build/scenarios/20261003-001955_net_enemy_census_waves_provenance_1/report.json)
passed in 170.6 s: complete native censuses showed two object-309 and two
object-311 enemies per peer, zero unmatched bindings and shared 160 to 153 HP
after host damage. Four subsequent native kills applied once per client.
Its second wave was empty and the native controller count remained two;
six host death announcements included two earlier alive-despawn policy events.
This is bounded first-pack replication, not multi-wave or barrier acceptance.
The [independent audits and limits](SCENARIOS.md#native-enemy-census-diagnostic)
retain the earlier strict divergence and incomplete creation provenance.

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
restoration of a progress mismatch; forced resync, a deliberately failed
native enemy death, and hash agreement across the full transition suite need
separate evidence. At that checkpoint, the protocol 8 fresh transaction was verified offline; native
acceptance remained pending ([FORCED_RESYNC.md](FORCED_RESYNC.md)); historical
dead targets remain explicitly unavailable without safe identity proof. The
courtyard population failure above remains open.

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

The [follow-up geometry diagnostic](../build/scenarios/20261002-204742_net_enemy_census_transition03_1/native_enemy_census_transition03.json)
is **FAIL** (164.6 s): one changing actor identity made the first client sample
unavailable, so its trailing strict parity check never ran. Eight of nine native
and causal snapshots were complete. Their seven BOX descriptors, inverse
matrices and extents matched across peers. In both later snapshots, sampled
host activation was outside all seven regions while both clients were inside
regions 5 and 6, away from the numerical uncertainty bounds. These are checked
reads and offline float32 calculations, not native predicate-return tracing.
They strengthen the local-activation explanation without turning a partial
diagnostic into a passing acceptance test. Four disk saves stayed unchanged.

**Scoped native activation authority.** `NativeSpawnController` hooks verified
`0x3FF000` calls from the ordinary controller task. Qualification checks current
table/key membership and every record: type-2 combat, delayed mode 2, fixed
position mode 0, finite coordinates and actual direct-ID objentry type 3/4,
excluding `F_` objects and dynamic aliases. The controller type is one byte;
header byte 1 is flags. No room, header-30 or enemy-ID allowlist grants authority.

Clients request an exact epoch/full-location host-native float4 every 100 ms.
A response must echo their incarnation and request identity and come from a
native hook capture after the host consumed that request. Its lease ends at the
original client request time plus 500 ms; receipt cannot renew it. This bounds
capture age including bridge, relay and network backlog without comparing PC
clocks. The relay verifies the sender, stamps the requester and routes only
matched challenges. Both packets are ephemeral and never enter late-join or
resync snapshots. Visual puppet poses do not supply activation authority.

Current WorldBridge v11 retains the immediate session generation introduced in
v5 and adds exact generation/delivery reset markers and per-peer delivery floors.
Protocol 9 retains the producer source identity introduced in protocol 8; these fences do not replace
fresh activation challenges. Native use stays unarmed until that exact marker
arrives. Native transition/load changes, including same-room reload, invalidate
the lease. Host capture and response flush require safe gameplay, the announced
epoch/full location and unchanged native serials; flush follows the fresh census
and manifest barrier. A qualified client calls the original update once with a
fresh copied host point, or explicitly holds the whole native tick when authority
is unavailable. Hold pauses flags/cooldown/emission and never catches up skipped
ticks. Host/off roles and unsupported controllers retain native execution.

Qualification read failures pass through with coverage diagnostics: an unknown
controller may produce events, so it cannot be broadly held. Mixed/dynamic or
initializer records, type 9, cache/stage divergence and transient triggers remain
outside this boundary. A recent sampled point does not guarantee every host
spawn decision; host-commanded emission remains D5's fallback. Native census and
strict unmatched-row hashes continue to expose those gaps. Portable lease/relay
tests passed with ASan/UBSan, and Windows bridge tests and the Release DLL build
passed. The [original 20-load route](../build/scenarios/20261002-205416_net_host_transitions_statehash_acceptance_1/report.json)
now passed (246.9 s), preserving all location, native puppet and strict hash
checks, including the previously divergent room. Its accepted hash populations
were empty; this proves the scoped client-only trigger regression, not nonempty
spawn/HP/death convergence or the full P2 hash gate. Four disk saves stayed
unchanged. The [source-expiry control](../build/scenarios/20261002-210128_net_activation_lease_menu_1/report.json)
also passed (93.5 s). START produced an observed native unsafe menu/pause gate;
the client stayed safely in field and both runtimes kept reporting connected
network traffic. After a wait exceeding one second, fresh qualified Hold counts
rose `505 -> 749` while Apply stayed `388` and no new lease was accepted. Resume
accepted a newer source (`752 -> 836`) and Apply advanced to `389`, with the same
controller, epoch, native serials and full location. The report retains the gate
predicate, not its raw values; its screenshot does not prove visible menu UI.
The exact 500 ms expiry is the portable lease contract; this live control proves
cessation and recovery beyond that bound. Four saves stayed unchanged.

The [new native-wave regression](../build/scenarios/20261002-210512_net_enemy_sync_waves_1/report.json)
**failed** (155.5 s) before native deaths or second-wave acceptance. Before damage,
the host had four object-309 enemies while each client had two 309s and two
unmatched object-311 enemies. Equal total counts had admitted unequal identities.
All four selected host actors actually took `160 -> 153`; surviving matched
client netIDs 3/4 also reached 153. A damaged host actor then left alive and
refilled at 160 HP before the snapshot. The strict failure is real population
and lifecycle divergence, not list order, missed damage or demonstrated matched
HP transport failure. The six-record controller 115 was qualified and used
leases in this room, but the run did not capture the 311 producer/provenance or
prove that lease holds caused the difference. Four saves stayed unchanged.
Keep this failure alongside the earlier passing wave run; the scoped activation
change has not established nonempty wave convergence. Creation, removal and
controller bookkeeping need correlated native evidence. Supported nonempty
activation and those new diagnostics are prepared offline, pending live runs.

**Offline creation and lifecycle diagnostics.** Exact opt-in
`KH2COOP_SPAWN_TRACE=1` adds independent byte-verified observers for all callers
of fixed wrapper `3FE590` and generated wrapper `3FE650`, dispatcher `3FE320`
and script callback `42DC10`. Each original executes once with its return value
preserved. Events retain raw record bytes, actual returned actor metadata,
generated point where applicable, full NOW/load/transition stamps and separate
wrapper and enclosing `3FF000` controller/cache snapshots. Script and dispatcher
scope are explicit dynamic enclosure; the script's native tail jump can yield
a DLL dispatcher return address, so unavailable caller ancestry remains visible.

Five independently verified lifecycle probes observe removal bookkeeping
`3FFD90`, disposal `3B45C0`, lethal notification `3D4A40`, death bookkeeping
`3FED10` and count decrement `3FED40`. Nested events retain entry sequence,
parent/depth and checked pre/post actor/controller/cache facts. Disposal may
invalidate poststate; count decrement has no actor argument. Neither missing
poststate nor a later disappearance proves death or allocation reuse. Native
exceptions continue to their original handler, with interrupted poststate and
restored scope. Fault counters count observer filter hits, which can include
several nested observations of one exception.

Only the registered game thread samples role and Warp serial callbacks.
Unknown/foreign threads preserve raw facts with unavailable role/stamps. The
game-thread drain records events even when current correlation is unavailable.
Fresh post-apply native census plus identity rereads establish current membership;
binding additionally requires one sampled generation matching current, activation
and consumed ordered-reset generations. `bindingEpoch`/netId is a current hint,
not a creation incarnation, and is never derived from emission order.

Per-hook availability, partial installation, unsupported callers, unavailable
reads, queue loss and interrupted events remain explicit. Generic factory calls,
unhooked subtype removal, actual deallocation and producers outside these
boundaries are unobserved. Same-address reuse and controller reincarnation remain
unproven. Bounded queues, missing shutdown drain and diagnostic overhead prevent
full coverage claims. This instrumentation changes no creation authority or
activation lease/Hold/Apply policy and does not repair the failed wave. At that offline checkpoint, the
strict wave diagnostic and separate nonempty activation control were prepared;
no live trace run had occurred while James used the desktop for Rivals.

The expanded Release DLL and both Windows headless targets build with native
tests under MSVC `/W4 /WX`. Spawn tests passed 38 checks and lifecycle tests
24, then both passed in the isolated Windows AddressSanitizer build. Tests use
actual observer code, synthetic originals and test-owned memory, including
exception propagation, original-call count, nested/TLS recovery, queue loss and
real foreign-thread controls. They do not execute game functions, install hooks
or validate live ordering/census bindings. The saved-log auditor passed 37
controls and rejects the archived untraced wave logs as incomplete evidence.
Activation deadline evidence is reused for unchanged inputs. Subsequent relay
claim-epoch and co-op host-loss changes have fresh world/fake/avatar/bridge
checks, Windows ASan and portable relay ASan+UBSan evidence in the
[relay lifecycle receipt](../build/rig/relay_lifecycle_offline_receipt.json).
The Windows native trace path has no UBSan evidence.
Fresh logs and source/DLL hashes are in the
[expanded offline receipt](../build/rig/native_trace_expanded_offline_receipt.json).
The [earlier fixed-only receipt](../build/rig/spawntrace_offline_receipt.json)
retains its historical 19-check scope.

Not covered yet:
- Bosses: none reachable on this save.
- Drops and barrier objects (battle state stands in for "barriers lift").
- Continuous spawners.
- Broader client-hit authority (VUH-1503): native courtyard acceptance now
  confirms 22 ordinary client claims and three Shadow kills, delivered once to
  both clients with matching applied HP/population/progress. Boss finishers,
  survival/caps, attack-specific effects, victim-side enemy attacks and native
  reconnect remain unproven. See
  [native D4 evidence](SCENARIOS.md#automatic-client-hits-native-courtyard-acceptance-2026-10-02).
- Hash agreement across repeated native room loads and varied populations,
  plus successful recovery after the enemy mismatch control. The nonempty
  proof covers one fixed-wave room; it does not establish spawn convergence
  or native remote actors after its battle-room reload.
