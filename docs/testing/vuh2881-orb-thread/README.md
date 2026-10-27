# VUH-2881: Struggle orb worker receipts

Landed on main at `4194ea7ca72d99ece361690acfe1e0d18a6f9ba8`, after Sorrel's
exact-source approval. The single authorized PC1 Struggle qualification passed;
the deliberate clone brush-past question remains open for the lead.
The reviewed source is based on main `aa0d64c` (VUH-2898's arrival wait).

## Live qualification: 2026-10-27

Run `20261027-001127_net_prologue_struggle_orbs_1` passed all tightened predicates
in **213.5 s**, from a clean serial Release build of main `4194ea7c`.
Host `46464` booked **231 births and 215 collections**; all 215 received
`(epoch, sequence, orb)` identities match the sent set. The friend logged **209
successful native applications**, including **all 131 clone-owned collections**.
Friend local suppression was nonzero: **30 drops / 24,101 picks**. Both seats
reported zero overflow. Six wire collections lack flight receipts; same-batch
birth/collect elision is consistent with that difference, but no per-ID elision
receipt proves the accounting. The budget-capped display summaries are not final
created/vanished totals.

Actual per-site observations (last periodic counters; TID-change tracing is
bounded, so worker lists are not complete lifetime thread censuses):

| Site | Host | Friend |
|---|---|---|
| `NoteGameThread` | TID 233844; 6,600 entries, no foreign entries | TID 97996; 3,600 entries, no foreign entries |
| `create` | 231 entries on 233844 | Unexercised; display uses the original native trampoline |
| `init` | 231 entries on 233844 | 225 entries on 97996 |
| `hit-drop` | 33 entries on 233844 | 30 entries on 97996 |
| `drop`, `pick-b` | Unexercised | Unexercised |
| `pick-a` | 29,035 entries, all foreign | 28,522 entries, all foreign |
| `fly` | 215 entries, all foreign | 5 entries, all foreign, after mission-failure replay |

Observed host pickup/flight worker TIDs: 95376, 127152, 132620, 186584, 205280,
228676. Friend pickup workers: 47872, 137320, 189488, 195388, 218632, 248336;
the five native flight entries used the first, second, fourth and fifth of those.
The 209 mirrored display applications call the original flight trampoline,
so they do not enter that detour. Thus **host `offThread=29250` and friend
`offThread=28527` are expected observations**, exactly their foreign pickup plus
flight entry counts. The exercised creation/drop hooks ran on the frame thread.

[Reviewed six-second paired clip](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/f316165d-bff1-49d3-bf42-f351390b0be2/ad6b65e2-c5c5-4e3d-aa48-7bbad0d892a3):
host left, friend right, showing orb motion during combat. This cuts seconds
12–18 from each native capture and normalizes to 30 fps; independent game clocks
are not synchronized. All 179 published frames were reviewed; the excerpt passes
strict decoding. The retained full friend recording has a decoded-frame fault
outside the published excerpt. The logs establish successful correlated calls;
the clip does not establish an isolated clone brush-past miss rate.

The match reached **host native failure** (`flags=15 result=2`); the friend replayed
one `client fail ... ok=1`. No host end-trap line was logged, so this evidence
does not claim a victory or native MissionEnd trap. The fixture permits failure.

**Safety closure passed:** 541 install files and metadata unchanged; four protected
saves backed up read-only and unchanged; 35 pre-existing main-checkout foreign
files and pinned products unchanged; owned games/build processes gone and relay
port free. Admission had 19,229 seconds physical idle, no HOLD and 27.3 GiB free.
Two harmless pre-launch wrapper refusals and their closure proofs remain retained;
the actual third job launched the scenario once. No Thorn run occurred.

[Portable report, raw logs, collection identities and safety receipts](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/47b2b9fb-f7d9-424d-94d1-a976f0470c7f/d6fb902b-7197-4e28-98ee-2ddb36425c58)
and the clip are attached to VUH-2881 and downloaded back byte-for-byte. The
archive excludes actual save backups. Originals remain in this worktree's run
directory; `.local/live-vuh2881/portable/result.json` records extraction and limits.
The evidence-store CLI is unavailable in this seat, so the Linear archive is the
portable record. Closure stays with the lead: clone collections work, but no
deliberate isolated brush-past opportunity was measured for VUH-2865 question 1.

## What the historical run proves

Read-only input: Juno's
`.local/wt/vuh2466/build/scenarios/20261026-192703_net_prologue_struggle_orbs_1`,
main `bc8d9ad`. The host (`6536`) sent 242 birth rows; the friend (`219608`)
received 242. The final host stats (`:2418`) show `births=242 collects=0
... offThread=41208`; the friend (`:2502`) shows `dropsSuppressed=38
picksSuppressed=0 offThread=44591`. Its final display sample shows
`created=242 vanished=0`. Both seats reached the match result.

The shipped `Gate::Admit` returned before every native read and booking callback
whenever the detour TID differed from `NoteGameThread`'s TID. Therefore all those
foreign entries bypassed mirroring, including client-local suppression. Simply
removing the comparison would call EnemySync's vector/book/outbox mutations and
actor mappings from workers; those APIs are owned by the entity thread.

**Limit:** the old logs contain neither per-hook TIDs nor per-hook foreign
counts. They do not prove which hook produced each foreign entry, nor establish
a specific native worker TID. The native facts/pinned prologues identify the
record argument as R8 for `40FF50`, RDX for `410250`, and the flight transition
as update `+0x10` plus collector `+0x48`; these facts alone cannot prove dynamic
thread affinity. The evidence supports fixing the unsafe affinity assumption,
not inventing a thread identity. Exact selected lines and original hashes are
in `tests/fixtures/struggle_orb_threads/sources.json`; they remain failing controls.

## Change

`OrbTick` publishes an immutable snapshot of the admitted session generation,
delivery, epoch, native transition/load, manager, role and collector identities.
A detour on any thread reads that snapshot. Native code runs once on its
original caller. Workers never invoke EnemySync or resolve an actor through
live containers. Unknown collectors are sent unresolved, so the friend fades
the orb rather than guessing.

`Pump` on the noted entity thread drains copied receipts into the existing
host book and emission outbox. Its book keys are monotonic incarnation IDs,
not reusable record addresses. The mailbox holds at most 4096 receipts and
4096 record-address mappings; overflow is counted and fails
acceptance. Scope/manager/role changes and room resets retire queued and
in-flight receipts, including a manager returning at the same address. No lock
is held over a native call or a callback. Worker client-local drops and picks
use copied policy, rechecking the native manager and live policy before skipping.

Each actual detour (`create`, `init`, `hit-drop`, `drop`, `pick-a`, `pick-b`, `fly`) and
`NoteGameThread` emits `thread site=... tid=... gameTid=...`, bounded to the
first 16 TID changes per site. Periodic per-site totals retain entry, foreign
and change counts. `offThread` now records observed foreign entries; it is not
a failure or a worker relabelled as the entity thread. Native input/output
LastError is preserved around tracing and receipt work.

### Sorrel's ordering finding and the revised boundary

The first candidate's post-return FIFO claim was wrong. An allocator can publish
record R, pause before submitting its birth, and be overtaken by another worker's
collect of R. The owner then drops the unknown collect and eventually mirrors a
permanent orb. Likewise an old pickup's delayed return can name a reused record.
This is an admitted interleaving, not an observed failure in the historical run.

Read-only queries of the existing analyzed program establish a narrower native
boundary (raw decompiles in `.local/ghidra-orb-{review,lifetime,init,base}.log`):

- `3DC950(manager, descriptor, point)` calls `413F00` once and then sets scatter.
- `413F00` gets a pool record, calls `412D40(record, manager, descriptor, point,
  update)`, and only then sets the record's enabled byte `+1A=1`.
- `412D40` calls `4118E0`, clears flags `+28` and collector `+48`, and initializes
  the manager/descriptor and life. `4118E0` initially sets `+1A=0`. There is no
  native incarnation counter in those fields that a delayed outer return can use.
- Both Struggle pickups call `413650(record, actor, 0)`. It unconditionally writes
  that actor to `+48`, establishes flight life/update, and returns. Its arguments
  identify the collection without rereading a potentially reused record later.

The revision therefore hooks the pinned initializer and flight function too:

1. The initializer reserves an incarnation **before native initialization**,
   while the native record argument is known and before the caller enables it.
   The reservation belongs to the outer create call on that native stack; stack
   identity and exact arguments prevent a different fiber borrowing its TLS frame.
2. The outer allocator completes that reservation with copied scatter only if
   the incarnation is still current before and after the native read. A new
   initializer invalidates the old identity before writing any new record fields.
3. The flight detour captures the incarnation and copied collector identity at
   entry, reserves its collect, calls native, then completes that same ticket.
   A delayed return never consults the record again or adopts a newer identity.
4. The owner drains only a completed queue prefix. A collect cannot pass an
   unfinished birth, and a new birth cannot pass a reserved old collection. If
   collection/reuse happens before a birth becomes observable, the adapter elides
   it atomically, including its collect. `elided` reports those births. It neither
   reads flight velocity as birth scatter nor creates an orb already gone.

This preserves native call concurrency; it adds no lock or wait across a native
call. It handles delayed observer returns rather than assuming KH2 never overlaps
two workers on one pool address. All seven hooks apply together or stay inactive.

Controlled wrapper tests force two workers onto **the same owned native record**:
allocation post-write pauses while another thread collects and the owner pumps;
then native flight post-write pauses while another thread reinitializes that
address. The first must elide without any wire birth; the second must send the
old ID's collection and leave the new ID displayed. Sequential reuse remains a
separate control. Scope retirement during the inner flight also must not book
into its replacement.

The scenario now requires nonzero friend drop **and** pickup suppression,
matching sets of sent/received `(epoch, sequence, orb ID)` collections, and a
clone-owned collection with a successful matching friend application receipt.
Zero suppression, mismatched IDs/epochs/sequences, an extra unmatched collect,
missing/failed application and zero clone collections are negative controls.
The receipt proves the native flight/fade call succeeded, not that a video shows
its motion. Per-seat mapping of every site (explicitly naming unexercised sites),
paired visual evidence and a deliberate clone touch opportunity remain live work.

**Delta verification:** after Juniper's batch ended and the lead cleared PC1,
the branch rebased cleanly onto `aa0d64c`. `git range-diff` confirms the two
previously reviewed commits retained their content. Fresh serial/background
Release build passed; **CTest 280/280 passed in 275.89 s**, including **44/44
Python files**, **46/46 actual-wrapper checks**, **456/456 adapter/rule checks**,
and **10/10 scenario controls**. The unchanged resync test passed in 15.83 s.
Gate snapshots show no desk job before/after these jobs. The first focused
wrapper run exposed a test-accounting error (the normal owner collector-snapshot
callback was counted as an unexpected booking); the assertion now counts actual
birth/collect callbacks, and the focused and full suites pass.

Fresh `run.py --validate` checks **118/122** fixtures, including the updated orb
scenario. Its four failures are the unchanged, pre-existing unknown-`do` cases:
`net_player_progress_route`, `net_safe_field_recovery`, `net_twilight_thorn_legA`,
`net_twilight_thorn_postfield`. The delta receipt and native decompile excerpts
are attached to [VUH-2881](https://linear.app/vuhlp/issue/VUH-2881); raw checks and
gate snapshots are in this worktree's `.local/review-*` files. No native affinity,
visual flight/fade or clone brush-past claim follows from these offline results.

### Sorrel's application-retirement finding

The first review delta called `OrbClientFollow()` from the successful-flight
callback inside `ClientDisplay::Tick`. That helper can observe a new runtime
generation and retire the session, clearing the very display queue that Tick
is iterating. Tick would then erase an invalid range. Comparing the epoch
before that helper did not prevent retirement or suppress an earlier stale
receipt from the same tick.

The callback now only invokes the native flight and copies successful requests
into a stack array bounded by Tick's 64-call budget. After Tick returns,
`OrbTick` refreshes authority, then compares the captured generation, epoch,
delivery, transition/load and manager before emitting any application receipts
or display summary. A retired scope discards the whole batch. The display's
callback contract explicitly forbids reentrant mutation/reset.

The owned-memory `NativeHitClaimTest` runs the actual EnemySync callback. It
creates two display orbs, queues both collects, and advances the live writer's
generation during the second successful native-flight leaf. A reset guard
rejects retirement before Tick consumes the queue, stopping a regression before
the invalid erase. The reviewed code fails that guard and leaks the first
application receipt; the fix safely retires the display and emits neither old
receipt. Stable successful flights emit both receipts; failed flights emit none.
The writer remains live and the seat remains a client throughout the race.
This is a controlled native-leaf schedule, not a live game reproduction.

Fresh serial/background Release and **CTest 280/280** passed in **274.41 s**,
including all **44 Python files**. The focused production native harness passed
**1,406 checks**. Red/green logs and gate snapshots remain in `.local/p1-*`;
the portable receipt is attached to VUH-2881. Scenario/runner files are unchanged,
so the prior 118/122 validation (four known baseline failures) is reused.

## Original candidate verification (historical)

- `kh2coop_struggle_orbs_test`: adapter controls and three real producer threads
  submitting 600 receipts concurrently with owner drain. Scope changes,
  unregistered/inactive context, client suppression, policy withdrawal during a
  read, stale return, native unwind, unknown collectors and bounded overflow.
- `kh2coop_struggle_orbs_hook_test`: actual private production detours, native
  reads, TID logger and Pump over owned memory and synthetic native functions.
  Both pickup argument routes pass births/collections through the real codec,
  ledger and client display. The displayed orb flies to its copied collector;
  same-frame pool reuse preserves the newer orb. A blocked native call does not
  block owner policy retirement, and its late return is refused. No hooks install.
- `test_struggle_orb_threads.py`: executes the committed scenario predicates in
  the production runner Context. Historical excerpts fail; synthetic positives
  allow foreign entries only with thread evidence, drain, sent/received collects,
  friend vanish and no overflow. Missing evidence/collect/receive/vanish or either
  seat's overflow fails. Synthetic positives are not a replay of unlogged pickups.

Source `2a2a9d0` on main `c330cfd`: full serial Release build passed.
The full CTest run passed **278/279** in 268.48 seconds, including **43/43 Python
files**, **35/35 actual orb-wrapper checks**, **447/447 adapter/rule checks** and
all five new scenario-predicate tests. The unchanged resync target failed one
combined quarantine assertion in 19.77 seconds, then again in 19.60 seconds.

Its original machine-code section matches the passing main binary, with matching
compiler, build graph and include sets. After restoring temporary diagnostics,
the original binary passed **308/308 checks, 3/3 idle runs**, in **15.97, 15.45,
15.51 seconds**, with the same CWD and no desk job before/after each. The main
baseline passed in 15.55 seconds. The timing-sensitive failure is separately
tracked as [VUH-2894](https://linear.app/vuhlp/issue/VUH-2894); both red logs remain
preserved. The original failures preceded Juniper's base job, so attributing
them specifically to that live run would be unsupported.

The scenario passed `run.py --validate` before rebase; the fixture and validator
blobs are unchanged, so that passing evidence is reused. A fresh recheck was
deferred when the next live job began. All builds/tests used below-normal,
serial execution; every build/test after the live handoff checked gate/jobs
first. Sanitizers are not claimed; this uses the normal MSVC path.

[Offline verification receipt](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/48673e83-a80e-4621-a359-5732528bc229/8ad77391-5e88-4361-92aa-1adc85490d15)
is attached to VUH-2881 and was downloaded back byte-for-byte. It pins source,
staged DLL, historical provenance, build/test logs, synthetic TIDs and idle
trials. Raw logs remain under this worktree's `.local/`; the evidence-store CLI
is unavailable in this seat. The attached receipt and issue are the shared
record.

## Remaining qualification

Independent source review, landing and the single live Struggle scenario are
complete. The clone's brush-past miss rate remains unmeasured; the lead decides
whether to carry that observation in VUH-2865 or require another dedicated run.
No additional live run is authorized by this result.
