# Carrying a fenced friend into the host's boss arena (VUH-2640, design)

Design, approved by the lead with the decisions below; Leg A is implemented offline (see "Leg A as implemented"). This extends VUH-2628's returning-seat recovery.
VUH-2640's F1 outage covers an expiry *inside* the arena; this covers a friend fenced
in an earlier room when the host moves on into a boss arena. Line references are to main `e7a5d88`.

## The failure (bots05 attempt01, `20261022-080847_net_twilight_thorn_bots05_1`)

1. **The friend's lifetime expires in Calling (2/33, battle 1, not a boss room).** The friend rejoins on a
   fresh pinned connection. The host submits a returning-seat Bootstrap for epoch 2. Its DLL refuses it with
   `[resync-capture-refused] stage=historical-row`, because Calling's dead Dusks are history rows
   (`inject/src/EnemySync.cpp:3201-3204`). Those rows could never pass `RecoverySnapshotSupported`
   (`!deadCount`) anyway. The epoch is now spent (`terminal-needs-new-epoch`).
2. **The relay keeps the friend fenced.** A non-converged result marks it `worldQuarantined`
   (`server/src/SessionHost.cpp:35`). From then on, every host material packet, RoomTransition included, is
   `quarantine-suppressed` (`:2018`). The friend's NetworkClient (`common/src/NetworkClient.cpp:794`) and
   runtime (`runtime/src/RuntimeMain.cpp:1801`) would drop it too. This is correct: a fenced seat must not
   follow a cached or bare transition.
3. **The host's retry in the arena never fires.** At 2/34 (epoch 3), `ReturningSeatHostReady` waits,
   epoch unspent, for as long as `TT34_MS304` runs (`bossMission`, `runtime/src/GameBridgePC.cpp:812`). After
   the kill, the post-Thorn scene chain is qualified scenes, which also wait (`ReturningSeatResync.hpp`
   `unsupported-host-state`). The first capture chance is the field after the chain. That field must also
   pass the known-dormant script table, which today lists FM00 only (`inject/src/KnownDormantScripts.hpp:58`).
   So in practice the friend stays stranded.

**What the friend sees today:** it stays in Calling, uncontrolled by any host state. Avatars are gated both
ways (`RuntimeMain.cpp:1997`), so the host's puppet vanishes. I found no on-screen recovery notice.

## Proposal: the Bootstrap is the transition, and the friend arrives sitting out

There is no new transport or resync. The returning-seat Bootstrap already loads the snapshot's room on
the friend (`warp::QueueHostTransition(resyncRoom, ...)`, `EnemySync.cpp:3346`; `IssueHostTransition`,
`inject/src/Warp.cpp:344-387`). So the host's arena capture carries the fenced friend in, and the
relay's RoomTransition suppression stays exactly as it is.

**There is no "before the boss binds" window for Thorn.** The arena intro (2/34 event program 1, scene
314) is event context, where capture is refused. The fight load (2/34 map/battle/event 157) starts
`TT34_MS304` and spawns object 307 from battle program 157 as it loads. That comes from the ARD
inventory and the bots05 room log; it has not been measured frame by frame. Every field frame the host
can capture in therefore already has the boss. So arrival is always as a D17 sit-out, the outage downing of
F1, whether or not the friend's mirror has bound Thorn yet.

### Two legs, both the existing returning-seat transaction

**Leg A: arena presence (new admission, epoch 3).**

- **Host checks.** The host's room must exactly match a reviewed boss-arena row. The row follows the
  known-dormant table's pattern: exact six-field location, boss object 307, mission `TT34_MS304`, both
  pinned. With that match, `ReturningSeatHostReady` lets `bossMission` through for that row only. Every
  other host fact still waits.
- **Capture.** The capture runs the same stages. `recovery-shape` admits exactly that row's boss as a
  *declared host-owned, not reconstructed* row: the snapshot names the row and carries no boss HP, pose,
  phase or script state. It still requires no dead rows, no active hold and no other enemies.
- **Friend side.** `ReceiveResyncSnapshot` accepts that declaration only for the same row. It loads the
  arena with the host's six-field tuple and converges by the existing rules: a changed Bootstrap load, two
  native frames, progress and a matching fingerprint.
- **Leg A is presence, not recovery.** The host's returning-seat hint stays pending (epoch 3 is spent).
  The friend's runtime keeps `eventHoldRecoveryRequired`, so it gets no new event-control lifetime and
  never arms. Relay and runtime keep the seat quarantined for material traffic, so no RoomTransition,
  EventHold, 315 or progress reaches it. Inbound host avatars are shown (they are ephemeral, so the relay
  already passes them), so the friend can watch. Outbound traffic stays at resync, TransitionAck and
  SeatState.
- **Downing.** On the arena's first settled frame, the friend's DLL begins F1's `OutageDuty`. It is
  anchored to *this* Bootstrap's load serial (`DownedReturn.hpp:465`, `Begin(recoveryAt)`), so the arrival
  load cannot end the duty. The qualifying receipt is the converged snapshot's declaration (request,
  row, generation, room serials) in place of a mirror bind receipt.

**Leg B: the real recovery (unchanged).** It runs at the host's next eligible epoch after the fight, which
is the field after the post-Thorn chain. This is VUH-2628's existing Bootstrap, fresh lifetime, reset-1 ACK
and Arm. Its load serial differs from the anchor, so `OutageDuty::End` returns `Recovered`, and the friend
returns at 25%, D17's return after the boss, exactly as F1 does today. If the runtime goes `Terminal`
first, it resolves solo, as F1 does.

The result is that both expiry cases, in the arena (F1) and before it (this design), leave the arena by
the same Leg B.

## What the friend must not replay

| Host state | On the friend |
|---|---|
| Arena intro: scene 314, Thorn `appear`/`battle_start`/`event` 6466, the party barrier | Not played. The friend loads straight into the 157 fight tuple. |
| Boss HP, phase (`btl_normal/hard/super_hard`), AI, target | Not captured, not mirrored. The friend's local 307 copy, created by its own load, is never bound to host authority. |
| RC windows and winners (297/298/299, Reversal 345/426/432/666/667), script attacks, Creeper occurrences, `atk_thorn` attach/camera | Not captured. The sit-out friend is never an eligible winner (it is downed). |
| `TT34_MS304` signal 4, 4/16 `mission_dead_boss`, 4/3/11/22 | Never replayed (no blind 4/16). The friend's local mission is the load's own and is abandoned by the Leg B load. |
| Native 315 and the post-Thorn scene chain (02/00 51 through 02/15 142) | Not played. The friend has no lifetime, so it cannot receive the EventHold, and the relay suppresses the transitions. Leg B jumps over them, as in F1. |
| Cached history: Calling's rows, the old room's 315, any cached RoomTransition | Never. The quarantine stays until each snapshot is forwarded. Leg A's progress is the host's fresh capture. |

**Accepted product consequence:** the friend misses the Thorn fight, its rewards and the post-Thorn story.
That is D17's "sit out until the fight resolves".

## What the host refuses (waits, epoch unspent, unless noted)

- **Any room that is not a reviewed boss-arena row** while a boss mission runs. This is today's
  `bossMission` wait, kept.
- **The existing facts:** menu or pause open, not controllable (which covers Thorn's player-attach
  cinematic), host downed or unknown, scene or event context, transition pending, unreadable.
- **Thorn dead or the mission ending:** a 4/16 edge observed by the existing MissionMirror host detour
  waits for Leg B's field. Leg A never admits a room whose boss is ending.
- **Proposed: dead history rows and an active hold now wait instead of spending the epoch.** Today they
  are found only inside capture, which spends the epoch (Calling). The outcome is the same, minus one
  doomed transaction.
- **Inside capture, everything stays terminal for that epoch, as now:** a census mismatch, a second
  enemy, a catalog row other than the declared boss, or changed context.

## What the friend sees meanwhile

1. **Fenced, before the host's arena settles (seconds).** It stays where it was, with input its own, and
   sees no host puppet. A short on-screen notice ("Reconnecting to the host") is proposed; none exists.
2. **Leg A load.** It shows a normal native load into the arena.
3. **In the arena.** The friend is downed in its held pose, with input neutral and no damage. Its D17
   downed camera shows the host's puppet fighting. The friend's *own* local Thorn copy is also there and
   does not match the host's (decision 2).
4. **Leg B load.** It loads into the host's field, returned at 25%, and normal co-op resumes.

## Reuse (no second system)

Every piece below already exists and is extended, not duplicated:

- `ReturningSeatResync` hint and epoch policy;
- `ReturningSeatHostReady`;
- the returning-seat Bootstrap plan, snapshot, ACK and result, with relay quarantine and continuation;
- `warp::QueueHostTransition` and recovery Bootstrap load attribution (`g_recoveryBootstrapLoad`);
- F1's `OutageDuty`/`OutageFrame` and D17's held sit-out;
- the known-dormant reviewed-row pattern;
- VUH-2628's fresh lifetime, reset-1 ACK and Arm, for Leg B only.

**New pieces:**

- one reviewed arena row;
- a snapshot declaration of the host-owned boss, which changes the wire (protocol 35);
- the presence-only result handling on host and friend runtimes;
- an outage receipt from that declaration.

## Decisions (Clankie, 2026-10-22)

1. **Carry into the arena:** Leg A plus Leg B, as designed. Waiting in place until Leg B was rejected.
2. **The friend's local Thorn copy keeps its own brain, as in F1 (option a).** A trace check for the
   `atk_thorn` attach moving the downed local player is added and flagged for the next live run. Making
   the copy inert (option b) was not chosen.
3. **Protocol 35.** The host attests the sit-out in the snapshot; the friend never infers it from the row.
4. **The first slice accepts a friend sitting out in the arena after the fight** until the post-Thorn field
   has its own reviewed row. That row is its own follow-up issue under VUH-2640.

**Status:** this doc is committed first; Leg A is then implemented offline and goes to Quill as its own
packet. `inject/src/DownedSpike.inl` is coordinated through the lead (Ivo is editing it).

## Leg A as implemented (offline)

Where the code is:

- **Reviewed rows:** `common/include/kh2coop/BossArenas.hpp`. One row, `tt34-twilight-thorn`:
  `{2,34,0,157,157,157}`, object 307, type 12, `TT34_MS304.bar`.
- **Wire (protocol 35):** `ResyncSnapshot::bossSitOut`, a 16-byte tagged trailer (`BSIT`) that excludes
  the activation trailer. The codec admits it only for the exact row's room and boss, with a nonzero boss
  netId, complete coverage, no enemy row, no record definition and no active hold. It is covered by the
  snapshot SHA but not by the native fingerprint, because it is transport attestation, not native state.
- **Host policy:** `ReturningSeatHostReady` lets `bossMission` through when the hint's room is a row
  (`bossArena`).
  - `Terminal(result, sitOut)`: a converged sit-out keeps the seat pending. The host's `NetworkClient`
    remembers which key it sent as a sit-out.
  - After one presence, any arena waits with `sit-out-presence-held`, so a retry's new epoch can't
    re-carry the seat. A failed sit-out is an ordinary failure, and a new connection starts fresh.
- **Host capture:** `CaptureHostResync` takes its own branch for a row. It needs:
  - event control on, and the row's mission file running on the host;
  - exactly one spawn and one native enemy, and that is the row's boss, alive;
  - a repeated census and progress, and an unchanged scope.

  It captures no record catalog and no enemy row (refusal stages `sit-out-*`).
- **Relay:** for a sit-out, the target stays quarantined after the snapshot is forwarded. Continuation is
  dropped, there is no further continuation buffering, and the relay's cache is not refreshed from the
  snapshot (it keeps the host's live state). The boss's netId in `EnemyHp`/`EnemyManifest` is not a
  material change; any other enemy, or the boss's death, is.
- **Friend runtime:** a sit-out snapshot leaves `worldQuarantined` set and puppets unavailable. Its
  Converged result logs `[eventhold-recovery] sit-out presence ... controlFenced=1` and never feeds the
  recovered-lifetime handshake (`RecoveryResultMatches` refuses a sit-out).
- **Friend DLL:**
  - `ReceiveResyncSnapshot` refuses a sit-out unless this is the client and its own lifetime expired.
  - `ObserveNativeResync` binds nothing for a sit-out: the census may hold only copies of the declared
    boss, and the observed state has no enemy row.
  - `ClientFrame` matches nothing while a sit-out is in force. That counts as a completed frame, so the
    observation still runs; stray-copy and population repair stay fenced by the resync write fence.
  - The relay's Converged result records `SitOutArrival`: its request, arena, generation, transition,
    load, and the recovery Bootstrap serial it anchors to.
- **Outage:** `downedreturn::SitOutArrivalQualified`, applied in `DownedSpike.inl` as a separate commit.
  It sets F1's pending outage from the arrival. A held outage downing that the native update moves more
  than 200 units is logged once per episode (`[downed] outage held-player-moved`), for the `atk_thorn`
  attach check on the next live run.

Departures from the design above:

- **No record catalog.** The capture is a dedicated branch, not "the same stages", because nothing is
  rebuilt on the friend. This also avoids depending on whether the arena's spawn catalog qualifies, which
  is unknown offline.
- **Host avatars are not shown** to the sit-out friend in this slice. Puppets stay unavailable while it
  is quarantined. Showing inbound avatars is a follow-up.
- **The boss ending is a terminal refusal for that epoch,** inside the capture (the boss down, or the
  mission gone), and a material change at the relay. Either way, Leg B's later epoch carries the seat.
- **The proposed early wait on dead rows and an active hold is not implemented.** The refused epoch is
  spent as before.

Offline evidence (Release, serial ctest):

- **`kh2coop_resync_test`:** the codec round trip, the refusals and the relay transaction (fenced target,
  boss HP not material, another enemy or the boss's death material).
- **`kh2coop_automatic_notice_test`:** the host policy.
- **`kh2coop_event_hold_control_test`:** no recovered lifetime from a sit-out.
- **`kh2coop_downed_return_test`:** the arrival rule and the duty anchor.
- **`kh2coop_nativehit_test`:** the actual native path over owned memory. It runs the host arena capture
  and its refusals, refuses a live seat, then takes the fenced seat through staging, load and two-frame
  convergence, and records the arrival with the old lifetime still expired.

Not proven: any live run, including the friend's own Thorn copy, its intro event on the friend, the
`atk_thorn` attach, the friend's progress matching mid-fight, and Leg B's post-Thorn row (VUH-2674).

## Proof plan

**Offline:**

- arena-row and declaration tests on the existing resync and `EventHoldRecovery` fixtures;
- `OutageDuty` anchored at Leg A, not ended by it, and ended by Leg B;
- relay continuation and quarantine for the presence result;
- refusals: a mismatched row, a second enemy, dead rows, a boss ending.

**Live (Ivo's desk):** the bots05 route with a forced friend-runtime pause in Calling. That pause needs its own
qualified fixture: the old 2600 ms one no longer expires a 4000 ms lease, and a longer one must
account for relay age since the last packet (Quill r1). Pass: the friend
arrives in 2/34 sitting out, one host victory, no friend 315 or 4/16, Leg B at the next field returning
at 25%, and protected saves unchanged.
