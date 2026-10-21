# Downed / revive network contract (VUH-1504)

The downed contract was introduced in protocol11; the current session protocol
is **31** (Twilight Thorn terminal semantics), with AvatarBridge **4**. Older network
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
- The two-PC check rides on the VUH-2464 firewall run.

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
  admission policy. Hold **Triangle for 60 game frames**. The request fires on
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
