# Axel I combat-admission barrier (VUH-2897)

The host no longer starts Axel's fight while a friend is still loading. When the host's
Mansion → Axel intro ends, the host keeps its own player and party frozen and also freezes
Axel. It waits until every admitted friend reports arrival with the host's fresh challenge,
then releases everything at once. If that does not happen within **15 s**, the host logs a
failed barrier and releases anyway, returning native play. It never waits indefinitely and
never fabricates an arrival.

The barrier is offline-qualified only. Kestrel runs the live checks listed at the end of
[LIVE_TRACE.md](LIVE_TRACE.md#live-checks-for-the-barrier) after review.

## Scope

- **Arm:** only the authenticated host's own native request from `02/13 btl134` to
  `02/14 door0 map137 btl137 evt137`, bound to that transition and its load. Solo,
  offline, client, already-published and every other room path stay native.
- **Defer:** only the intro teardown's two group releases, `3BF2E0(0)` returning to
  `3ACC03` and `3BF2E0(1)` returning to `3ACC0D` (3AC750 case 9 inside 3AC000). Both run
  in one native call on the owner thread, while the intro's event context is still set.
  Install pins the bytes at `3ACBFC..3ACC0D`:
  `xor ecx,ecx; call 3BF2E0; mov ecx,1; call 3BF2E0`.
- **Freeze Axel too (lead's decision A, 2026-10-27).** A frozen group is the actor's
  objentry type (`3B5DC0` returns record `+4`: 0 PLAYER, 1 FRIEND, 3 BOSS, 4 ZAKO). Groups
  0/1 hold only Roxas and the party; the intro's event context is what held Axel. At the
  first deferral the lease therefore calls the native setter `3BF2C0` for groups 3 and 4.
  It does so only for bits that are clear, and records them as owned. A bit the game had
  already set is never owned and never cleared by the barrier.
- **Release:** one owner-thread pass, ascending: the deferred 0 and 1 through the original
  `3BF2E0`, then the owned 3 and 4. A bit that is no longer set is skipped. The release
  happens only in the same load, with no event context installed. Owned 3/4 are never
  freed while 0/1 are still held.

## Lease, challenge and ACK

Each lease has a new incarnation and a fresh 64-bit challenge, and it records:
- the world session generation and the host delivery;
- the load and transition serials;
- the intro's event context, as a scalar;
- each admitted friend's connection and delivery at deferral.

The host's RoomTransition packet carries the challenge (protocol 39, packet-only u64
trailer, zero without a barrier). A friend sends `CombatAdmissionAck` (packet 57) once,
only after its own issued load and native intro pass `warp::HostTransitionArrived`. The
ack echoes the epoch, the full destination and the challenge.

The relay admits the ack only from a verified, unquarantined client through its own
envelope, with a per-connection sequence floor. The requester must be that connection, and
the room, epoch and challenge must match the admitted announcement. The relay then stamps
the slot and forwards it to the verified host alone.

The host DLL re-proves that the envelope source is the slot's current connection and
delivery (`RequesterCurrent`). The lease then checks the challenge, the publication epoch,
the full destination, a required seat, the connection and the delivery.

Refused or late ACKs are logged no-ops and never arm anything. An ACK for lease N cannot
release lease N+1 (different challenge).

## Exits

Every exit names its reason in `[combat-admission] decided … outcome=…` and releases
through the same ordered path, except a changed load, which retires. The first decision
wins; later ones cannot rewrite it.

| Outcome | Trigger |
|---|---|
| `all-arrived` | every required seat ACKed after publication (the only admission) |
| `deadline` | 15 000 ms from the first deferral, monotonic; ACK traffic never extends it |
| `seat-left` / `seat-replaced` | a required slot's connection is gone or changed, or its delivery changed |
| `session-retired` | generation, host delivery or host role changed |
| `host-downed` / `peer-downed` | the host's own downed publication (serviced in the same call), or a fresh downed pose of a puppet in this room |
| `native-transition` | any host native request (door, Continue, retry): released before the request is forwarded |
| `native-cleared` | native code cleared 0/1 itself (a room script or a later event's own teardown); the owned 3/4 are still released |
| `shutdown` | DLL shutdown: an unloader thread only signals, and waits up to 500 ms for the owner |
| `load-changed` | a new load landed first: retired without any release |

**Event installed.** A new event installed during the hold blocks every clear, 3/4
included, until its context clears. Its own teardown clears 0/1 through the native code
(forwarded, since the lease is busy), and then only the owned 3/4 remain. Decision A said
"don't touch 3/4" when a new event was installed. I read that as "while installed":
leaving owned 3/4 set after the event ended would freeze Axel until the next load.

**Reload (S2, static).** `3BF810` zeroes dword `2A171E8` (groups 0–31). It is called only
from `39CA10`, which runs in `152CD0`, the task at the end of every field-build load task
`152680`. That zeroing comes before the new room's intro installs, in the same function.
A stale lease is therefore retired, never replayed into the new room. Continue is inferred
to take the same path (every static field build does), not traced.

## Owner thread and liveness (S1)

Release only ever runs on the game thread. That thread runs the field dispatch fibers, the
entity-head frame and the raw-input collector; trace `20261027-015517` shows all of them
on TID 171372. `3BF2E0` and `3BF2C0` are non-atomic read-modify-writes of `2A171E8`, and
every native caller runs on that thread.

The deadline is serviced from the input collector. EventHold already relies on that
callback while a native pause menu is open. It is also serviced from the entity-head
frame. A release is due on the first owner service at or after expiry. The ≤250 ms bound
therefore rests on the callback cadence in the held state, which is still a live check.

Static receipt: field dispatch (`14FC20` → `14FD60`) never reads the mask, so field tasks
keep running while held.

## Publication under the hold (S3)

`SafeNativeGameplay` and the progress gates require `CONTROLLABLE == 0`. Holding the mask
would stop the host's publication, so friends would never load. The only relaxation is
`PublicationHoldQualifies`, which requires all of the following:
- a held, unpublished lease in the same load and transition;
- Axel's room;
- no event context, no cutscene, no menu, in field;
- a mask exactly equal to the lease's own groups.

It is used only by `HostBeginInstance` and by the progress sample in `ProgressSync::Tick`
that precedes it. Progress still goes first and the room is announced once.
`SafeNativeGameplay`, `SafeRecoveryGameplay`, `ProgressSync::SafeGameplay` and
`SafeToWarp` are unchanged, so combat, spawn and recovery consumers stay closed.
`tests/test_combat_admission_scope.py` enforces this.

The host's EventHold release for the new epoch still waits for `SafeNativeGameplay`, so it
is published at barrier release. The friend is not blocked by it: a Transition wakes its
held pause (`EventHoldInputState::Wake`), and arrival needs only menu `0xFF`.

## Static qualification (S2, S4)

Headless Ghidra, read-only copy, 12 queries: `.local/static-qual/` in the branch evidence
bundle. Results:
- **S4: no latching reader.** All 59 references to the mask reader `3BFA40` are one
  per-actor vtable predicate (slot +0x78). Its callers gate per frame or retry a one-shot
  until the actor can act (`3A92C0`/`3AA200` controller activation). Battle mode
  (`2A11404`, recomputed per frame in `3AB6F0`) and the HUD/UI pause pairs never read it.
  28 indirect `+0x78` calls without a nearby handle decode were not classified.
- **Frozen actors still update.** `3BFD30` skips only slot `+0x20`; motion and the other
  slots still run. Collision and damage while frozen were not traced; that is a live check.
- **Minor, inferred:** while frozen, `3A92C0` skips updating `717038` (last controllable
  location). Teardown's HUD resume still runs, so the combat HUD may show during the hold.

## Offline evidence

- `kh2coop_combat_admission_test`: ownership, release order and idempotence, every exit,
  ACK negative controls, the scoped publication proof, the deadline, and the owned-3/4 rules.
  It checks every mask for "never 3/4 alone".
- `kh2coop_combat_admission_native_test`: the real pinned teardown bytes and `3BF2C0`
  setter run in an owned executable image, with MinHook stubbed. It covers deferral, owned
  3/4, release order 0, 1, 3, 4 on the owner thread, the deadline at 15 s, event wait,
  downing, Continue, reload retirement, off-thread refusal, solo, off-thread shutdown, and
  coexistence with the trace (one 3BF2E0 detour).
- `kh2coop_combat_admission_net_test`: exact framing (truncated or suffixed rejected), the
  room challenge trailer, and loopback-ENet relay admission with negative controls.
- `tests/test_combat_admission_scope.py`: the S3 scope and single-detour guard.

## Switch

The barrier is default-on. `KH2COOP_COMBAT_ADMISSION=0` on the host disables deferral
(diagnostics and tests only); everything then forwards natively.
`KH2COOP_COMBAT_ADMISSION_ACK=0` on a friend withholds its ACK, so a live run can exercise
the 15 s failed-barrier path on purpose.
