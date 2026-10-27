# Downed / revive network contract (VUH-1504)

The downed contract was introduced in protocol11; the current session protocol
is **33** (D17 native difficulty and returns; v33 the host's mission end), with AvatarBridge **4**. Older network
peers and avatar mappings refuse the new contract. The sealed friend packages
are unchanged. ENet and the default-off Steam backend use the same policy.

The sections below up to "Offline verification" are the network/bridge
contract. The native owner side (death intercept, held downed state, native
revive) is in `inject/` behind its own feature flag; see
[Native owner side](#native-owner-side-inject) at the end.

## Native producer hook

The horizontal-only native downed hold is part of the reviewed VUH-2569 cohort.
Offline qualification preserves native update order and tests independent falling
heights, horizontal shoves and the two-unit boundary. Build24 passed all 202 tests.
Computer Room run `20261021-035356_net_downed_grounded_pair_1` passed 105 steps:
both canonical owners held their native grounded XYZ during attempted movement,
revived normally to 5/22 HP, and regained movement. Saves and teardown were clean.
The calm room supplied no enemy shove; pin writes were zero. Thorn airborne
settling remains pending. The prompt's 150-unit and relay's 200-unit 3D ranges
are unchanged.

Later bot01 on sealed build28 recorded three natural friend-to-host revives and
a native host kill, but failed shared post-boss travel; it is not a complete
Thorn acceptance. The calm regression above remains the grounded behavior proof.

On each checked native owner frame (alive **and** downed), publish
`AvatarBridge::SetLocalDownedState(LocalDownedState)` from
`common/include/kh2coop/DownedState.hpp`:

- captured `ProducerWorldContext` generation and delivery serial;
- actual admitted epoch and full world/room/door/map/battle/event tuple;
- `episode`: nonzero, strictly increasing for every new downing over the whole
  game lifetime, including room changes and reconnects. Never wrap or reuse;
- `sampledAtMs = GetTickCount64()` and the checked `downed` state.

The setter is a single-owner bridge publication, with no native reads/writes.
Unavailable native checks must publish `{}`. Runtime projects it into each
new local avatar only when generation/delivery, room, and a maximum 1000 ms
sample age match. Otherwise downed authority is cleared. The streamed fields
are `AvatarDowned`, `downedEpoch`, `downedEpisode`, and captured `downedDelivery`.
The state is not inferred from HP zero. Native integration must keep publishing
while downed; ordinary entity publication liveness is not proved by these tests.

## Request and host validation

The native requester sends an encoded `ReviveRequest` using the existing
`WorldBridge::SendToRuntime(packet, capturedContext)` owner-only ring. It holds
the current full room tuple, its roster connection ID, the observed target
slot/connection/downed episode, and a nonzero sequence increasing across rooms
within its connection. Do not recycle the sequence. `NetworkClient`'s typed
sender is a headless convenience; native code retains its real producer context.

SessionHost checks the existing immutable world envelope first, then requires:

- verified, unquarantined, distinct requester and target in the current room;
- both arrival ACKs for that epoch and world/room, no active EventHold;
- both streamed positions received within 1000 ms, finite XYZ, matching epoch,
  captured delivery serial, world/room, and no cutscene flag;
- living requester, downed HP-zero target, positive valid maximum HP;
- matching target episode, not previously reserved, and Euclidean distance
  at most **200 native world units** (initial policy, not a live-qualified reach).

The requester slot is stamped from its authenticated connection. Authenticated
sequences that reach gameplay validation are consumed even on refusal. Target
episodes are reserved before forwarding, so duplicates or another teammate
cannot authorize a second application. This fails closed if delivery/application
fails; there is no retry/ACK/UI success mechanism in this slice.

The reliable request goes **only to the target owner**, never into the late-join
or resync cache. This uses SessionHost whether it runs in the Steam host runtime
or the ENet relay. It validates owner-reported positions, not independent native
geometry or anti-cheat truth. Freshness here is relay receipt age, not proof of
continuous motion or synchronized native frames.

## Target owner integration

NetworkClient rechecks the current scoped target, room, fresh local downed
avatar, and episode before either typed callback or raw WorldInbox delivery.
Runtime's existing reliable WorldInbox carries the original envelope to the
DLL. It never applies HP or substitutes native ownership.

After normal native world-envelope admission (including source delivery floors),
the native owner passes the request, exact received scope, current session,
producer context, current three connection IDs, local slot and its fresh
`LocalDownedState` to `ReviveOwnerGate::Consume`. Keep this gate for the whole
game lifetime. It reserves the episode **before** one call to
`downed::TryRevive(episode)`. That native function must independently recheck
the actual actor/life/world/safety before applying; a false return is not retried.
Do not call it from the runtime, callback thread, or a raw input thread.

The original owner must publish alive state only after native revive actually
succeeds. A forwarded request is not evidence that healing occurred. All-party
down, mission failure, transition/cutscene handling, revive HP amount and button
UX remain native/policy work.

## Offline verification

`kh2coop_revive_test` covers the codec, actual test-process avatar/world
mappings, runtime pump, consume-once gate, real three-peer ENet loopback,
protocol10 refusal, stale/replayed state and request refusals. The same
`ReviveNetworkFixture` runs through the real Steam broker/transport/client/
SessionHost stack with mocked Valve and IPC-link boundaries in
`kh2coop_steam_broker_test`. No Steam account, game, or save is used.

Existing avatar, world-sync, and HUD-name suites remain required. This evidence
does not qualify the native intercept or a real cross-account Steam session.

## Native owner side (`inject/`)

The native producer, consumer and requester described above exist in
`inject/src/DownedSpike.inl` (adapter, included in `EntityHook.cpp`),
`inject/src/DownedSpikeState.hpp` (pure rules) and the ReviveRequest admission
in `inject/src/EnemySync.cpp`. The names say "spike" because the live evidence
was gathered under them; they are the feature, not throwaway code.

`Tick` snapshots the entity-list head every frame. It follows the status,
controller and dead-action pointers only when the head's objentry is
player-class and the status pointer is an exact record of the native status
pool (VUH-2464): during an event the head is another actor whose `+0x5C0` holds
data, and a guarded read of it still killed the game through the in-process
CLR (`docs/LESSONS_LEARNED.md`, 2026-10-18).

**Feature flag: `KH2COOP_DOWNED_SPIKE=1`.** On in the player package
(`tools/launcher/friend_package.py`, VUH-2269); off for direct developer
launches. Unless the variable is
exactly `1`, `Install` returns before any byte check, hook, mapping or log;
`Tick`, `StatDelta` and `NoteHit` return on their first test, no publication is
made, no ReviveRequest is applied, and the game-over functions are untouched.
The cost when off is one bool test per frame and per ApplyStatDelta call.

When on (Steam build only; every native site is byte-checked at install, and a
mismatch logs `[downed] configure ... REFUSED` and installs nothing):

- **Intercept.** MinHook detours on the game-over requests `0x3FCD20` (mode 0)
  and `0x3FCAD0` (mode 3) skip the request only for the canonical local player
  (`g_soraActor` == `[0x2A105D0]` == entity-list head), on the game thread,
  after the native death has set the dead flag (`actor+0x9B8` bit 2), with no
  game-over task. Everything else (including mission failure, `arg == 0`) calls
  the original. Sora stays natively dead: DEADSORA action, controller off,
  enemies' damage blocked by the dead flag.
- **Episodes.** Seeded per boot (`nonce << 32`), +1 per intercept, never reused.
  While still downed, the owner **re-mints** a new episode after a native revive
  refusal or after 1,800 frames (~30 s) without an accepted request, so a lost
  or refused request can't strand the player. Requesters must always target the
  latest streamed episode; all the reservation floors above are `<=`, so they
  accept the larger value.
- **Publication.** Every owner frame, `LocalDownedState` goes through
  `AvatarBridge::SetLocalDownedState` with the scope from
  `enemysync::CaptureDownedScope`. Alive is published only after a successful
  native revive; natively dead but not held (refused) publishes `{}`.
- **Consume.** `enemysync::AdmitReviveRequest` admits the source (current roster
  peer, delivery serial, host cut / requester floor, body slot and connection
  match), then `ReviveOwnerGate::Consume` reserves the episode before exactly one
  `TryRevive(episode)`, which reruns every native check (owner thread, same
  episode, canonical actor, outside events, dead flag, live pointers, no task).
- **Revive.** Native `0x3AA8D0(actor)`, with its one `+max` heal rewritten in
  `HookedApplyStatDelta` to land on **25% of max HP (at least 1)**; then the
  game's own ignore-hit timer `0x3D7E40(actor, 120.0f)` (~2 s grace) and one
  idle motion request `0x3C86A0(actor+0x158, 0, 0.0f, 0.0f)` so a networked
  client stands up without input.

**Test channel: `KH2COOP_DOWNED_SPIKE_FIXTURE=1`** (or
`KH2COOP_DOWNED_SPIKE_CONTROL=1`, which implies it). Only then is the
shared-memory channel `Local\kh2coop_downed_<pid>` mapped. It carries per-frame
readouts and three debug commands: `Kill` (native ApplyStatDelta to 0 HP),
`Revive` (TryRevive on the current episode) and `RequestRevive(slot)` (sends a
ReviveRequest for a teammate's streamed episode, the same call the revive
prompt below makes).
`_CONTROL=1` installs only the channel and Kill, so the native game over runs.
Product play never maps the channel. `kh2coop_downed_state_test` covers the
pure rules and pins the 360-byte channel layout (version 7) the live fixture
mirrors.

**Live evidence (2026-10-06, Steam build, one rig).** Single-game treatment
214522: PASS_NOT_EXERCISED (two 24 s holds, one scripted and one natural death,
no game over, no input drift, revive at 6/24 HP, control back). Two-player pair
230440: PASS (friend1 downed and streamed to the host in 0.11 s, host
ReviveRequest forwarded through every hop, consumed once, 6/24 HP, grace 118,
friend1 stood up and moved 474 units, no game over on either game). The pair
run used the final source; the treatment ran before the grace/stand-up/network
round, whose paths it does not exercise.

**Limits.**

- Invulnerability while downed is NOT_EXERCISED: enemies never attacked a
  downed Sora in any run. It rests on the native dead-flag gate (static
  reading of `0x3D60C0`/`0x3D2EB0`) plus party Cures being observed blocked.
- A teammate's Cure does not revive; only `TryRevive` does. Donald will spend
  casts on a downed Sora.
- A room change, actor change or fault while downed marks the feature Refused
  for the rest of the process (no retry, no auto-revive on transition); the
  player may stay natively dead with no game over.
- All-party down, mission deaths (mode 3 with an actor is gated; arg 0 stays
  native), drive forms, summons, the `g_sys400` bit-17 branch and non-Sora
  player classes are out of scope. The debug Kill refuses those branches.
- No range or liveness cross-check on the owner side (the requester's prompt
  checks range), and no refusal ACK back to the requester. A live negative control of the owner gate
  is still open. Only the 1-host/1-friend pair on the local relay is proven;
  three players and Steam cross-account are not.
- The hooks stay installed as pass-throughs until process exit.

## Logical downing in the prologue (VUH-2465)

Roxas (any `P_EX110` model, so dual-wield Roxas in the Axel fights too, read from the local player's
own objentry) is never held natively dead. The
native dead bit (`actor+0x9B8` bit 2) stops the room's spawn evaluation, so a later wave would never
come while either player is down. Sora keeps the held-death path above.

How it works (all in `DownedSpike.inl`, behind `KH2COOP_DOWNED_SPIKE=1`):
- **Intercept before death.** In the `0x3D2EB0` funnel (`StatDelta`), a lethal stat-0 delta on the
  local player (the native player pointer, in or out of an event) is clamped to leave **engine HP 1**.
  The death handler never runs, and the dead bit stays clear. A new episode starts and the player plays
  the death motion `0x36`. The log reads `[downed] gate intercept mode=4 logical=1 ...`.
- **While downed.**
  - No stat-0 change lands: damage, a Cure or an item is skipped.
  - An HP orb, which heals outside the funnel, is put back to 1 through the funnel.
  - Every hit is refused at `3D2060` (AllyHit, counted as `hitsRefused`), and the game's ignore-hit
    timer is kept up.
  - Input is neutral. The body holds its downing XZ against enemy shoves, while Y follows
    the existing native update. A horizontal displacement beyond two units restores XZ only.
    Holding the lethal Y could suspend an airborne player outside a teammate's revive reach.
  - The host's target search no longer offers a downed local player. Enemies retarget to a living
    teammate's clone where one exists.
  - The stream and HUD carry HP 0 with the existing episode contract, so SessionHost's HP-zero revive
    check holds.
- **Revive.** An admitted request heals through the funnel to 25% of max (at least 1), sets the
  120-frame grace and requests the idle motion. Range, freshness, epoch, connection, consume-once and
  episode guards are unchanged.
- **Doors.** An ordinary host door (field play: no event, cutscene or game-over task) is not taken
  while a fresh teammate pose in the room is downed. The overlay reads "A teammate is down: revive
  them before leaving".
- **Forced transitions.** A forced transition (a host-led load or a story event's room change) ends
  the downing. The old actor and episode retire, so a late request finds nothing to revive, and the
  player is resolved at 25% on the first settled field frame.
- **Full-party down.** On the host, once every fresh teammate pose in the room has been downed for one
  second, the host lets its own native death run (the game-over gate passes it). The game's game over
  and **Continue** retry the room from the checkpoint, with no save loaded or written. Friends follow
  that host-led reload, which resolves their downing.

PC1 loopback evidence: run `20261019-151151` passes as a whole (FM-02, Day 6 Computer Room, Basement
Hall and Axel's room; real lethal hits through `kh2ctl hit damage` and enemy hits; native kills stand
in for combat):
- **Computer Room revive:** the friend is downed and the host revives it with the Triangle prompt in
  2.6 s. The friend comes back at 5/22, moves and takes 1 damage (5 -> 4).
- **Friend down:** wave 1 finishes, and wave 2 (two Dusks 309) spawns on both peers.
- **Full party:** the host is downed too and takes the native game over. Continue retries at the
  Computer Room checkpoint with both at 22/22, and the retried hall spawns wave 1 again on both.
- **Host down:** wave 2 spawns on both while the host holds HP 1, and every wave finishes on the host.
  The post-fight story transition resolves the downing.
- **No native death anywhere:** across the run there were 3 logical downings on the host and 4 on the
  friend, including dual-wield Roxas in Axel's room, and no native held death (`gate intercept
  mode=0`) on either seat. `coop_verdict` passes over 7 rooms.

Limits:
- Enemies retarget away from a downed player only to a teammate's clone. An unchecked field such as
  FM-02 has none, so the Dusks keep attacking the pinned body (about 7,000 hits refused in one hold).
- The native kills replace the players' own attacks in these proofs.
- When the last Dusk of the hall dies, the fight-end event can close the room before its death is
  published, which leaves a friend's mirror alive (run `20261019-150147`). This is an enemy-sync race,
  not a downed-state one.
- The `bc8d9ad` Mansion batch reproduced missing wave-two death accounting and
  delayed end reconciliation (VUH-2884). Its [offline candidate and log
  analysis](testing/vuh2884-native-terminal/README.md) do not qualify a downed
  rerun; base, host-down and friend-down live acceptance remain open.
- The two-PC check rides on the VUH-2464 firewall run.

## Downed rules by difficulty (D17, VUH-2632)

James's decision (D17 in `ONLINE_COOP_PLAN.md`): the quick revive above is for Beginner and automation;
Standard revives more slowly; one player down never ends the run, and an unrevived player sits out until a
boss/encounter defeat or a save point.

**Done (offline, local):**
- `inject/src/DownedPolicy.hpp` (pure, `kh2coop_downed_policy_test`) maps the session's native difficulty
  to `{revive allowed, hold frames}`:

  | Difficulty | Partner revive | Hold |
  |---|---|---|
  | Beginner, unknown, or `KH2COOP_REVIVE_POLICY=beginner` | yes | 60 frames (~1 s) |
  | Standard | yes | 300 frames (~5 s); `KH2COOP_REVIVE_HOLD_STANDARD`, 60..1800 |
  | Proud, Critical | no (`KH2COOP_REVIVE_HARD=off`, default) | `standard` switches to Standard's rule |

- The difficulty is the save's byte at save + 0x2498 (0 Beginner, 1 Standard, 2 Proud, 3 Critical; save
  base `0x9A98B0`, checked by its `KH2J` magic). The host reads its own. A friend uses Beginner rules until
  the host's value arrives (the per-room host value is the next step). The offset still needs a live read.
- The revive prompt takes its hold from the policy and hides with `revive-off` when the rule disallows a
  revive. The owner also refuses an admitted request then (`[downed] revive-request refused by policy`),
  whoever sent it. Both log the policy at start (`[revive-prompt] policy difficulty=...`).
- The scenario runner forces `KH2COOP_REVIVE_POLICY=beginner` for `playerDefaults` runs unless the
  scenario sets `"revivePolicy": "native"`.

**Protocol 32 (wire):**
- The host's native difficulty rides each RoomTransition packet. A client's rule follows the host's last
  room; until one arrives, Beginner rules apply.
- `ReviveRequest` has a kind (51 bytes):
  - a partner revive keeps the relay's 200-unit range;
  - a return needs no reviver beside the target. `EncounterDefeat` is accepted only from the host
    (`kind-not-host` otherwise); `SavePoint` from any living seat.
- The owner applies a return on every difficulty and a partner revive only where the rule allows. One
  return per target episode.
- `kh2coop_revive_test` and the shared network fixture cover each kind through the real relay.

**Sit out and return (native, `DownedSpike.inl`; pure rules in `DownedReturn.hpp`, `kh2coop_downed_return_test`):**
- **Doors.** An ordinary door waits for a downed teammate only while the rule allows a partner revive.
  On Proud/Critical the survivors leave.
- **Room changes.** A downed Roxas whose room changes is downed again on the new room's first settled field
  frame, under a new episode (`[downed] sitting out in the new room`). This applies to a door the
  survivors took and to a story transition. It is resolved at 25% instead only when everyone is down: every
  teammate it saw in the old room was down too (the host's Continue retry), or it has never seen a teammate
  in this session (solo: nobody could return it).
  A teammate seen earlier in the session but not during the downing ("unavailable") does not count as solo:
  the player sits out. "The session" is the relay session's identity (the hash of its id the runtime
  publishes with each generation): a same-session resync or a runtime liveness gap keeps the history, a
  replacement session or the session's end clears it. Only a teammate pose that the current puppet
  authority owns (its generation and connections) adds to it, and only when the session identity read
  before that authority capture equals the one read after the count. So neither a replacement session's
  first frame nor a replacement that lands between the reads can import the old session's pose.
- **A lone host.** A host downed in its room that has never seen a teammate in this session is everyone
  down by itself: after one second it takes the native game over and Continue, as the full party does.
  VUH-2640 (the lead's #176 and #178, extending #143): so is a downed host every other seat of which is
  absent on real evidence: no connection in the host's current world roster (the relay dropped it, its
  heartbeat expired), or a returning seat quarantined until its recovery converges. The host runtime
  publishes each pending returning seat on the world bridge (v13) as the connection ID its quarantine
  belongs to, in the roster transaction and after each recovery pump; the DLL reads those IDs before and
  after its authority capture and counts a seat as returning only when the ID is that seat's connection in
  the captured roster, so a newer connection never inherits an older one's quarantine. Pose silence alone
  never counts: a lagging or loading teammate keeps its seat. Unknown evidence counts nothing. D17's "everyone down, game over"
  applied to who is actually there; otherwise the host waits forever and its recovery capture (not
  controllable) never returns the seat.
  Scope limit (accepted, VUH-2632): "never seen" is not "no teammate rostered". A host downed before a
  rostered teammate's first avatar arrives takes that game over too.
  - The teammates are latched each downed frame while the old room is current. The transition erases
    every teammate pose before the hold is found lost, so they cannot be counted then.
  - The log names the case: `teammate-up`, `full-party`, `solo`, `unavailable`, or `returned` (an owed
    return, below).
- **A link that expires in a boss fight (VUH-2640).** No mid-fight rejoin: a boss's script and AI state
  come only from host capture. A friend whose event-control lifetime expires during a host-owned Thorn
  fight sits out as a logical downing tagged `outage` (`[downed] outage expiry ...`, `[downed] outage sit-out
  ...`). The proof is a retained receipt: its mirror bound Thorn to the host's authority within two seconds,
  under the session identity and generation it last had live and in the room generation still current, and
  its own native mission is TT34_MS304. Its local Thorn runs its own brain after the retirement; the hold
  keeps the friend from fighting or winning it, or being killed by it. If it was not down yet, the downing
  begins on its first settled frame in that room with the menu closed; a room change or a recovered
  lifetime drops it first. The outage is this seat's obligation, not one hold's: it outlives every hold it
  spans, and any loss while it is open (an actor or handle replaced, another room, an actor gone during a
  load) sits out again, still the outage. It ends only on positive evidence, checked every frame whatever
  the hold saw: a recovery Bootstrap load completed since the expiry (warp attributes it to the host's
  resync Bootstrap; it may replace actor and room, land in the same room, or complete after the actor was
  already gone) resolves it at 25%, D17's return after the boss (`outage`); this seat's runtime stopping
  networking for good (ClientRecovery Terminal, a world-bridge receipt of that runtime's PID, process
  incarnation and current generation; a later runtime, even one with a recycled PID, never inherits it)
  resolves it solo. An unavailable, expired or retired runtime writer lease is neither, and the friend keeps sitting
  out.
  If the expiry left the event hold's own pause open (menu 8 or 10), an owner service on the game thread
  (run from the input collector, which keeps running while those menus are open) closes it with the hold's
  close sequence from its own fully validated native sample, the owned menu and the event context checked at
  every step; the raw apply only emits the prepared press. Any other menu, a partial read or a changed
  context stops it and leaves the menu to the player. The downing waits for the menu to be closed. A seat
  that was already downed at the expiry keeps its hold, and the collector's last step, the downed
  neutralization, passes exactly the press the orphan service emitted in that callback (never the player's
  input), so its pause still closes.
- **Encounter defeat.** On the host, a forced battle (`BATTLE_STATUS` `0x2A11404` = 2) that ends (0) in
  field play, within one room generation, returns every downed teammate in the room:
  `[downed] encounter defeated`, then `[downed] return encounter-defeat ...`.
  - The returns go only to the teammates downed, fresh and in the room at that edge, each claimed with its
    seat's connection and downed episode. A teammate downed later, one already returned, or another player
    in the seat is not returned by that encounter (`encounter-defeat claim dropped`).
  - Or the host's mission ends with `mission_complete` (table-4 trap 3) within one room generation
    (`[downed] encounter defeated: mission complete`). A wave fight that is a mission can end that way and
    go straight into an event and a scripted room change without ever clearing its cage in field: FM-02's
    Basement Hall (TT19_MS602), whose friend sat out into the Axel fight (B_EX110, TT20_MS603) before this (run
    `20261021-164443`). The mission mirror's host detour counts it, once per mission. Trap 22 (end
    signal), a failure, a boss end and a complete in a transition's or load's window (a window open at
    either sample, its serials stable or not, or one aborted between them) never count; that count is
    consumed. With
    the mission mirror parked (`KH2COOP_MISSION_MIRROR=0`) only the cage edge remains. A cage edge and a
    mission edge for the same fight arm one set of returns (`encounter defeated again ...`).
  - A host that sat it out comes back locally at 25% first.
  - Its peer returns wait until it has published itself living for 30 frames. The relay refuses a requester
    whose avatar is downed (`requester-not-alive`).
- **Save point.** Any living seat whose settled reaction command is the save point's "Save" (55) returns
  the downed teammates in its room (`[downed] return save-point ...`). They come back at 25% where they
  lie; walking onto the save point heals them natively. It also waits for 30 living frames.
- **Retries.** A target still downed with the same episode is asked again, 90 frames apart, at most three
  times. The relay and the target's owner still accept one return per episode.
- **A return during an event is owed.** The owner's native revive refuses in an event (`InEvent`), and the
  relay has consumed the episode by then. So a return (never a partner revive) refused only for that is
  owed to the downing (`[downed] return owed`), with no episode remint. It stays scoped to the relay
  session's identity and to that downing for its whole life:
  - In the room, it is applied out of the event (menu closed, no transition or load) through the one
    revive entry (`[downed] owed return ... applied`). A refusal before the native funnel (`not ready`) is
    checked again every 30 frames. The funnel itself runs at most once per receipt: applied, or a fault or
    an incomplete revive retires it (`retired`), as do a wrong state or the spike off.
  - When the event ends in a room change, the return is carried in the receipt (`returned`), not turned
    into the generic resolve. The player comes back at 25% on the new room's first settled field frame
    (`logical resolve after transition ... owed return`).
  - Each use needs the session identity to read known and equal to the receipt's; unknown keeps the
    receipt and authorizes nothing. Another known identity (0, the session's end, included) forgets it
    (`owed return forgotten`), or, once carried, revokes it (`owed return revoked`), and the room change's
    own rule applies instead: sit out, or resolve when everyone was down.
  - A new downing or the full party's game over clears it, and a downing revived some other way forgets
    it. The generic solo/Continue resolve is unchanged. This covers a downed host's own encounter return
    too.
- **What 2 means** (static analysis, VUH-2632; not observed live):
  - 2 is the script "command cage". It is set by `0x3AB690`, reached from BDX `trap_command_cage_on`
    (table 1 #117) or from a battle program flagged forced.
  - In the field it is cleared by `0x3AB4A0`, reached from `trap_command_cage_off` (#118) or from the battle
    program's end (table 2 #143).
  - On a load it is cleared by the area reset `0x3ABAB0`. That is a new room generation, so never an edge.
  - Fleeing or a transition therefore cannot fire a return. A room script that drops the cage without a
    win (a timed or event-ended fight) would.
  - If hostiles are still near, `0x3AB4A0` goes straight to 1, and the return waits for 1 → 0.

**Limits:**
- When the host is the one sitting out, the survivors get only in-room returns (the encounter, a save
  point in that room). Rooms stay host-led.
- Observed live (runs `20261021-163826` and `-164443`, FM-02 Basement Hall): `BATTLE_STATUS` read 2 on both
  seats in wave 1, between the waves and in wave 2. Assumed from Ghidra and not yet observed live:
  - that the save byte is at + 0x2498 (FM-02 reads 3, Critical, through the same read the policy uses);
  - that the re-down in a new room is clean.
- **The host's mission end on the client (v33, VUH-2656).** In FM-02's Basement Hall the host's event
  starts on the same frame as `mission_complete` (5568, run `20261021-174207`): the host's mission edge
  fires but its claim is refused (`connection=0 claimed=0`), because the downed scope, and so every revive
  request, needs field play. So a client judges the host's mission end itself. An admitted MissionState
  (the host's connection and delivery, relay-scoped, this client's current room) whose end trap turned 3
  for its mission is recorded once (a resend is not another), bound at admission to what holds then: the
  downing this seat holds (EntityHook runs the downed owner before the packet drain, so an end admitted
  while alive names no downing), the session identity and generation, the room generation and the host
  producer. The owner judges it once, on its next tick (`[downed] host mission complete ... verdict=...`).
  It is this downing's only when that same downing is still held (an episode remint is not a new one),
  no transition or load window was open at admission, the room generation is the downing's and unchanged,
  the producer and the session are still the ones at admission and at the downing, and the mission is the
  one the host was running here when the downing began (captured then; unknown fails closed). A seat
  downed before the host's first MissionState for its mission arrives (VUH-2660, run `20261021-184352`)
  captures its own native mission at the downing instead, since the client runs the same mission script
  (`[downed] encounter at downing source=host|local|unknown`); with neither, nothing is owed. Then it owes
  the return (`verdict=owes owed=1`): the same receipt as a relayed return, applied out of the event or
  carried through the room change to 25% on the next room's first settled field frame; another session
  revokes it. Any other verdict grants nothing, and that end is never judged again.
- The cage edge (2 -> 0 in field) has not fired live yet: no run so far had a forced battle end in field.
- A boss whose post-fight transition comes before its forced battle clears is VUH-2628's path. A boss
  that ends a mission through its boss end (Twilight Thorn's TT34 trap 16) is not a mission-complete edge.

## Downed spectator candidate (VUH-1819; pending live qualification)

`KH2COOP_DOWNED_SPECTATE=1` is default off and depends on the installed
VUH-1504 held-down mode. In a settled, admitted network room, **R3** cycles
living teammate puppets and your own downed body. The button must first be
released after entry, and one press advances once. It is consumed only while
the local held-down state is eligible. Roster slots and the applied party/kit
plan resolve teammates; a native AI companion is not a spectator target.
Cached actor pointers are compared only: slot, pose, plan membership and the
current native census must pass before any guarded actor/kit metadata read.
The current metadata must still match the puppet's original bound identity.

The candidate borrows the camera actor pointer only inside native mode-0
normal follow (`0x165110`), then restores it before returning. Its separately
byte-checked action-camera predicate hook (`0x3BA300`) suppresses the native aim
branch only during that borrowed call, preventing mode-1 entry and writes to
the teammate's actor+0xA10. Other callers and camera modes remain native.
No fake actor is allocated and no HP/control/party state is changed.

Shutdown closes new spectator access and disables the follow detour, then drains
active callbacks before any canonical actor, census or lifecycle state retires.
Aim suppression remains installed until every borrow finishes restoration.
The DLL is pinned and its trampolines remain process-lifetime even when no other
feature retains MinHook. Delayed detour entries use those retained originals.
A reentrant shutdown inside a camera callback defers actor/context retirement
(rather than waiting for itself); those resources remain until process exit.

A downed, stale (over 30 game frames), held, departed or replaced target is
skipped for the next valid slot. Room/load/generation changes release the
selection; native revive releases before calling the revive function. Events,
menus, an unavailable runtime lease, non-normal cameras and the F5 debug camera
yield. Episode re-mints alone do not change the selected actor. Native VUH-1504
refusal on a room retry is unchanged; spectating never repairs or heals it.

`KH2COOP_DOWNED_SPECTATE_FIXTURE=1` additionally publishes the read-only
`Local\\kh2coop_spectate_<pid>` v1 receipt (104 bytes): the actual pointer during
normal follow, its before/after values, selected slot, scope and call counters.
An out-of-call `kh2ctl peek` sees the restored native pointer by design.
Implementation: `inject/src/DownedSpectate.inl`; pure policy and controls:
`DownedSpectate.hpp`, `tests/DownedSpectateTest.cpp`. Adapter lifetime/kit ordering
and deterministic shutdown interleavings use `DownedSpectateAdapter.hpp` and
`tests/DownedSpectateAdapterTest.cpp`.
No downed spectator behavior is live-qualified yet. Pending fixture and limits:
`build/rig/vuh1819-downed-spectate-20261007-01/README.md`.

## Revive prompt (`KH2COOP_REVIVE_PROMPT=1`)

The player-facing trigger for a ReviveRequest. On in the player package,
off for direct developer launches; it needs
`KH2COOP_DOWNED_SPIKE=1` and is never active in the `_CONTROL` run. When off,
`PromptInput` and `PromptTick` return on their first test and the HUD snapshot
carries `promptKind = 0`, so the overlay is unchanged.

- **Rules** (`inject/src/RevivePrompt.hpp`, pure). The prompt targets the nearest
  fresh (pose within 30 frames), downed, non-cutscene teammate puppet in the same
  world and room, within **150 units**. That's under SessionHost's 200-unit
  admission policy. Hold **Triangle for 60 game frames** on Beginner; the hold and
  whether a revive is offered at all follow the difficulty (D17, below). The request fires on
  **release** after a full hold, never while held. The hold must start with a
  fresh press. L1+Triangle (the native shortcut) never counts.
- **Cancels.** The hold resets if you leave range, take damage, open a menu,
  enter an event or transition, go down yourself, or the target or episode
  changes.
- **One request per episode.** Each target episode fires at most once. A
  re-minted episode (see Episodes) re-arms the prompt.
- **Sending and logging.** The one action is the existing `RequestRevive(slot)`.
  Hide reasons and the fire are logged as `[revive-prompt] ...`, capped at
  32 lines each.
- **Input.** `HookedInputCollector` reads the raw slot-0 buttons after the
  mailbox/event-hold apply. That's the same buttons the game consumes, whether
  from the local pad or the kh2ctl/runtime override.
- **HUD.** The prompt is drawn in the downed teammate's own co-op HUD row, whose
  HP is 0. So the 130-px overlay layout is the same with or without it.
  - The HP column shows `Hold △`, then `Reviving` for up to 120 frames after a
    fire.
  - The row's bar fills yellow with the hold progress. `Snapshot` carries
    `promptKind/promptSlot/promptProgress`, and the display scope changes in
    10% steps.
  - Labels must fit `hud::HealthColumnChars` (10 cells), or they are ellipsized.
- **Native reaction command (RC): the prompt yields.** The game's own RC also uses
  Triangle, so a prompt that would show is hidden while the RC is up. The hide
  reason is `native-reaction`. It also reports actionable Triangle ownership
  when no revive target is nearby; the prompt remains hidden. Field, event,
  menu, transition and local-down guards still take precedence. Each actual
  in-range revive suppression is logged as
  `[revive-prompt] yield to native RC 0x2A11162=…`, at most 256 times per process.
  - The address is `offsets::REACT_CMD_STEAM` = `0x2A11162` (u16), calibrated in
    run 004215 at the GoA flag409 chest: it read `0x20` with "Open" showing and
    through its popup, and 0 away from it.
  - KH2Lib's `REACT_CMD` `0x2A110E2` stayed 0 throughout, so it is documented as
    wrong for Steam. It is still recorded in the fixture channel.
  - `0x2A11162` reads a stale `0x37` at boot, so it is trusted only in settled
    field play: the local player is canonical and not downed, with no event,
    menu or transition, for 30 consecutive frames (`NativeReactionFor`). Outside
    field play it counts as 0.
  - VUH-2569 live07 exposed a further limit: this address is the canonical
    controller's cached CMDINFO command, which can survive a skipped native
    refresh. ScriptMirror now supplies an occurrence-qualified reaction before
    the settled-field gate. Unrelated native targets retain native behavior;
    a known mirrored target needs its fresh selection, current native identity,
    host-owned occurrence and owner admission. The completion callback remains
    consume-once. Native acceptance and this read-only query must share that
    proof; no global HUD command is cleared. Runtime qualification is pending.
  - The existing version7 fixture remains360 bytes and records both raw RC
    addresses unchanged. Its fresh `promptHide` reports the product-qualified
    ownership decision for the pilot's input observation; this changes no
    network fields, combat strategy or revive timing.
- `kh2coop_revive_prompt_test` covers the rules, cancels, latch, hide reasons,
  L1 exclusion, the RC gate and yield, and the HUD row (fill, label width,
  scope).

**Live evidence (2026-10-07, Steam build, one rig, local relay).** Pair-hold
000112 and 001034: PASS. The host held a scripted Triangle 90 units from the
downed friend1:
- 78 Triangle frames, progress 1000, exactly one fire and one request;
- friend1 revived at 6/24, grace 118, stood and moved 474 units;
- no game over.

In 001034 the host overlay was on, and the clip shows the row's yellow fill
growing, then `Reviving`. That run used the longer labels, which the column
truncated; the shortened labels are offline-checked only.

**Prompt limits.**
- The RC yield has been calibrated on one object RC only (the GoA chest's
  "Open"). Enemy and other RCs are assumed to use the same field.
- A manual pad press is unverified; only the kh2ctl input override was tested.
- The prompt is not hidden while the local player is in an event-hold.
- Tested live (2026-10-07, main `24a00cc`): host revives friend1 (000112,
  001034), friend1 revives the host (003536), and three players where friend2
  revives friend1 while the host observes (003832).
