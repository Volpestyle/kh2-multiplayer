# One Mansion-to-Axel diagnostic run

Run `20261027-015517_net_prologue_mansion_axel_trace_1` passed in 237.2 s on
PC1, from the clean Release build of approved source `2aaf1882`. This is a
measurement, not a combat barrier or a landing. The host takes a native Axel
hit **1.828 s before the friend's arrival ACK enqueue**. Arrival lag is
**5.656 s**. Both seats play their own complete native intro.

## Exact boundary receipts

Times are same-PC `GetTickCount64` milliseconds. Parentheses give seconds from
the host's request at 817510843. `H`/`F` line numbers refer to the original
`kh2coop_inject_133568.log` / `kh2coop_inject_249988.log`. Across-seat ties within
one clock tick cannot be ordered. Native row sequence numbers establish the
within-seat order.

| Boundary | Host | Friend |
|---|---|---|
| Native exit blocked | none | 817515531 (+4.688), F2660 |
| Actual native load request | 817510843 (+0.000), H2456 | 817516234 (+5.391), F2809 |
| Native load-task start, `152680` | 817510859 (+0.016), H2465 | 817516250 (+5.407), F2816 |
| Event installed / load finalizers returned | 817511125 (+0.282), H2502/2503 | 817516781 (+5.938), F2884/2885 |
| First native intro update | 817511125 (+0.282), H2507 | 817516781 (+5.938), F2889 |
| Intro releases groups 0 / 1 | 817516140 (+5.297), H4055/4057 | 817521796 (+10.953), F4756/4758 |
| Intro update returns with event context zero | 817516140 (+5.297), H4059 | 817521796 (+10.953), F4760 |
| Arrival / publication | 817516140 (+5.297), H4064 | 817521796 (+10.953), F4764 |
| Arrival ACK | no host-receipt probe/path | queued at 817521796 (+10.953), F4764 |
| First canonical-player HP loss | 817519968 (+9.125), H4839/4840 | 817522921 (+12.078), F5075/5076 |

The host's arrival receipt is successful room publication for epoch 4. The
friend's is successful ACK enqueue after its issued load and native intro;
this does not establish host receipt. Both intros span **5.015 s**, with 302
native update calls. Load-task start to finalizers is **266 ms host / 531 ms
friend**. This task stamp does not claim first disk I/O.

The friend blocks its own exit 609 ms before host publication, then actually
issues its load 94 ms after publication (703 ms after its blocked exit).
It installs the destination intro 641 ms after host publication, and that
intro takes another 5.015 s. Thus the measured 5.656 s lag is the serialized
second load/intro, not a 5.7 s wait at the blocked native exit. Earlier run
193245 had the same lag without that blocked-exit path.

The native hit line immediately preceding H4839 names `B_EX110` (Axel), the
canonical `P_EX110_BTLF` actor, damage 6 and HP before 22. The paired native
delta return reports 16. It is 3.828 s after host publication and 1.828 s before
the friend's ACK enqueue. This is actual HP loss, not just an attack animation.

## What the trace qualifies

Both footers say `reason=trace-deadline dropped=0 lossObserved=0 missedArms=0`:
6,132 host rows and 6,100 friend rows, contiguous sequence numbers. The
extraction checks 2,110 / 2,104 observed adapter return pairs against entry
sequence, family, thread and fiber. It deliberately excludes unpaired point
sites such as `native-request-return`. A dispatcher call still in flight when
the window closes is not invented as a completed pair.

The decisive host order, all on TID171372 and the same field fiber, is:

1. `field-enter` seq1570; intro entry seq1571.
2. `3BF2E0(0)` from caller `3ACC03`, seq1572/1573: frozen mask 3 -> 2.
3. `3BF2E0(1)` from caller `3ACC0D`, seq1574/1575: mask 2 -> 0.
4. Intro return seq1576 clears event context; entity-head seq1577.
5. Host publication seq1578; field return seq1579; next dispatch seq1580.

The friend's equivalent sequence is 1730..1739. A gate applied only at the
next dispatch or after publication misses the current dispatch's release.
Both field dispatch and entity-head service continue during the 5 s intros
with frozen mask 3. This supports investigating retained native group freezes
instead of suspending the entire field mode. It does not prove all combat
workers honor those groups, or service after event teardown while held.

Post-load entity-head spacing is median 16 / p95 31 / maximum32 ms on the host,
and 16 / 31 / 63 ms on the friend (two gaps >34 ms). Independent input heartbeats
number 120 per seat, maximum observed gaps 266 / 297 ms. There is no uninstrumented
comparison, so this is not a proof of zero logger overhead. All native rows
have fieldState 1: full-pause liveness remains unqualified. Owner snapshots may
lag and native scalar reads are not atomic. Axel's first action before HP loss
is not instrumented.

## Concrete barrier proposal, before implementation

**Gate placement:** arm only the authenticated, participating host's exact
Mansion 02/13 btl134 -> Axel 02/14 btl137 load. Bind the record to native
load/transition, full destination, destination event identity, session,
generation/delivery, expected epoch and admitted connection identities.
Offline/solo paths remain native. At destination intro teardown, intercept
only the byte-qualified `3BF2E0` calls from `3ACC03` and `3ACC0D` for groups 0/1.
Defer those two original calls, retaining the native freezes already in force;
let `3AC000` finish, clear context and dispose its task normally. Do not replay
or skip either seat's intro, block the event task, zero a global mask, or issue
a late asynchronous Start/pause command. This is a proposed behavior change;
the diagnostic hook currently forwards both calls immediately.

**Progress and publication while held:** add a separate, tightly scoped
admission-readiness proof after the host's real intro teardown, requiring the
current native load/full destination, no menu/load/new event/downing, and only
the exact retained group bits. Use it for post-intro progress sampling and
room publication; keep ordinary combat, spawn, recovery and gameplay mutation
gates closed. Do not globally relax `SafeNativeGameplay` or progress safety.
Both currently reject a nonzero `CONTROLLABLE` mask, so simply suppressing
the unfreezes would otherwise deadlock the existing host announcement. Preserve
progress-before-room ordering and verify the friend still plays its intro.

**Release transport:** extend the existing validated WorldEnvelope route to
return arrival ACKs to the host DLL. Include the full destination and a fresh
host load/announcement challenge, echoed only after the friend's own issued
load and intro pass the existing arrival proof. Validate current authenticated
connection, admitted seat, session/delivery, epoch and challenge. Old, duplicate,
wrong-room, replaced or quarantined-seat ACKs cannot release. The relay currently
only records this ACK; a log line is not a release path. Drain/control service
needs a bounded existing owner callback, without adding a second consumer to
the SPSC world inbox or reading mutable owner containers from worker hooks.

**Deadline and unwind:** start one monotonic **15 s** deadline when the first
intro release is deferred, not when the room finally publishes. No retries or
ACK traffic extend it. Check expiry independently of entity-frame progress via
the existing input/control callback and signal owner cleanup; the implementation
must prove a <=250 ms service/unwind bound in the selected held state before it
can claim the deadline. Input heartbeats in this unheld run alone do not prove
that bound. Success calls the two deferred native releases exactly once, in
their original order, while the retained native identity still matches. Timeout,
disconnect, replaced participation or failed publication logs the exact reason,
missing seats/challenge and elapsed time, then performs the same bounded native
release as an explicit **failed barrier / native-play fallback**. Never fabricate
an arrival or wait indefinitely. Do not retain a pointer to the disposed intro
task; the native lease contains checked scalar identities and pending group
arguments only.

**Downing and Continue:** host downing cancels the hold immediately and restores
its retained native releases before downed/Continue control takes over. A peer
downing during admission is a named failure/unwind, never a readiness ACK; normal
revive/Continue behavior then owns control. Since combat should be held, a new
host damage/downing inside the hold also fails combat-exclusion qualification.
Before forwarding a native Continue/reload/new transition, unwind the current
lease on the owner while its old native identity still matches, invalidate its
challenge and discard queued ACKs. If native teardown has already changed the
load/event identity, retire the old record without replaying releases against
the new room's mask. A repeated 02/14 load is a new incarnation. Session retirement
and shutdown require the same identity-checked cleanup; no generic mask reset
and no cleanup posted against a future load.

**Qualification still required:** show native actor, collision/damage and worker
coverage under retained freezes; prove entity-head/publication plus independent
deadline/ACK service after intro teardown while held. If those cannot be shown,
reject this seam rather than claim that freezing groups is universal. Add
production-adapter tests for exact release ownership/order and every lifecycle
exit (including cross-thread ACK/expiry and Continue incarnation changes), plus
protocol negative controls. Independent review and a lead-scheduled live run
must then show both arrivals before the first Axel hit, preserved intros,
timeout/downing/reload liveness and unchanged saves.

This is more than a couple of hours: estimate **8-16 engineering hours (one to
two working days)** for qualification, transport/readiness changes, native
ownership/unwind and offline tests, plus separately scheduled review/live proof.
Uncovered worker behavior could increase that estimate. No barrier code is
implemented here; the handoff is suitable for a Claude implementation seat.

## Safety and retained evidence

START/END are on VUH-2897; the exact tracker timestamps are 06:52:40Z / 07:03:21Z
(the manually rounded minute labels in their bodies are approximate). One run,
no rerun or main landing. Session1 setup control passed. Launch idle 25,458 s,
no HOLD, disk 27.25 GiB before queueing. Save backups were copied out read-only;
all four protected saves, 541 install contents/metadata, 35 foreign files and
pinned products matched afterward. Owned games/build processes are gone and
UDP27843 is free; wrapper safetyPass=true and desk exit0. No Steam/install/save
writes or Thorn victory were attempted.

Raw logs/reports/stills and original 30 s Hall clips remain under the named
`build/scenarios/` directory. Safety manifests/pins/backups remain under
`.local/live-vuh2897/attempt01/`; save contents are excluded from published
evidence. The portable evidence contains original logs, reports, safety receipts,
the read-only extraction script/JSON and reviewed Axel stills. It does not
contain save contents or the game.

The paired Axel stills are sequential captures once both native NOW tuples
read 02/14: host combat HUD active, friend in a dim native intro. They are not
simultaneous or an image timestamp proof. The published Hall excerpt is source
3-7 s, 3.985 s/121 frames, resized to 960x540 without audio; every frame was
reviewed for private content and the excerpt passed strict decoding. The full
source host recording emitted a decoder warning during overview extraction;
the original is retained, and no clean-full-video claim is made. There is no
Axel transition video in this fixture. Logs establish the hit/arrival ordering.

## Live checks for the barrier

The behavior change is described in [BARRIER.md](BARRIER.md). It is offline-qualified
only. These checks need a lead-scheduled run of the Mansion → Axel fixture on the barrier
build, with the trace flag on both seats. Judge from the raw `[combat-admission]` and
`[axel-boundary]` rows.

1. **Admission.** The host logs `deferred group=0` and `deferred group=1`, `owned=18`
   (groups 3 and 4), `published`, the friend's `client ack queued`, then `ack … accepted`
   and `released admitted`. The host's first canonical-player HP loss comes after
   `admission-release`. Both seats' arrivals precede the first Axel hit, and both native
   intros complete.
2. **No damage during the hold.** Neither Axel nor any projectile changes the host's HP
   between `admission-defer` and `admission-release`, and no `hp-loss-during-hold` row
   appears. The hold freezes BOSS/ZAKO groups 3/4 through the native setter; frozen actors
   skip only their control slot, and collision/damage under the freeze is unproven offline.
3. **Normal start after release.** Axel's AI starts normally on both seats: first action
   and hits after release, no stuck idle. The HUD and battle music come up normally (S4).
4. **Timeout path, once.** A run with `KH2COOP_COMBAT_ADMISSION_ACK=0` on the friend
   logs `outcome=deadline failed=1 missingSeats=2` about 15 s after the first deferral.
   Then it logs `released failed-barrier native-fallback`, and play continues natively.
   The gap between `admission-decided` and `admission-release` shows the owner service
   bound (expected ≤250 ms). Repeat with the pause menu open across expiry, if the rig can
   hold it there.
5. **Saves.** Saves and install are unchanged (the standard closure).
6. **No event wait.** No `release-waiting-event` or `release-via` row is expected in
   Axel I. If one appears, record the event context and
   the time until `admission-release`.

Downing, Continue, disconnect and reload exits are covered offline and need no dedicated
run, unless one happens naturally.
