# Axel I arrival delay: log analysis and proposed combat barrier (VUH-2897)

Analysis only; rebased onto main `aa0d64c` for the source qualification below.
No build, test binary, game, desk job, or live run was started for this work.
Read-only Ghidra queries use an owned copy of the existing analyzed project.
The four older runs use Juno's pre-fix
builds; Juniper's three use `c330cfd`. Their original directories are read-only.

The host finishes its own Axel intro **before announcing the destination to
the friend**. The friend then loads the room and plays its own intro. This
explains the repeatable 5.64–5.75 s delay. The blocked native exit is how five
of the six friends eventually issue the load, not the reason the destination
announcement is late. Juno's base run has no blocked exit to Axel and still
has the same delay.

## Timing and limits

The table uses the first epoch-4 statehash's **actual GetTickCount64 receipt**
on each seat. Both processes are on PC1, so those ticks share a clock.
`arrival` is the logged ACK/publication boundary, not native load completion.
The clear crown-gate sample is in the same numbered entity frame as the first
hash for the base run and the hosts; the other three friends clear one frame
before their first hash. Periodic sampling does not imply a whole-second
uncertainty here, but none of these lines timestamps the native load request.

| Run suffix | Host PID / friend PID | Host first epoch-4 tick | Friend first epoch-4 tick | Difference | Host first Axel hit after host hash |
|---|---|---:|---:|---:|---|
| 193245 base | 157120 / 188396 | 794568187 | 794573937 | 5.750 s | frame 8314 vs 8309: ~0.083 s |
| 194057 host_down | 238900 / 162672 | 795071640 | 795077312 | 5.672 s | frame 9291 vs 9062: ~3.817 s |
| 194738 friend_down | 99924 / 236068 | 795477859 | 795483500 | 5.641 s | frame 9504 vs 9275: ~3.817 s |
| 201309 friend_down | 156668 / 89568 | 796990281 | 796995968 | 5.687 s | frame 8164 vs 8157: ~0.117 s |
| 215808 base | 206188 / 44128 | 803293796 | absent before teardown | censored | no hit before teardown |
| 220631 host_down | 52796 / 232592 | absent | absent | never loads Axel | full-party down in wave 1 |
| 221605 friend_down | 54640 / 164640 | 804372218 | absent before teardown | censored | no hit before teardown |

Hit seconds are **60 entity frames/s estimates**, not timestamped hits. The
first four are earlier than the friend's arrival by a substantial margin.
The two Juniper runs that reach Axel stop approximately 0.9/1.1 s after the
host hash (Juniper's teardown audit). They cannot establish a final lag or a
missing arrival. VUH-2898 owns waiting for that arrival in the scenarios.

Each log below is `kh2coop_inject_<PID>.log`, with 1-based lines. `load` is the
native load-complete receipt, **not load start**. Intro start/end means first
observed crown gate `cutscene=2 event=1` / subsequent `cutscene=0 event=0`.
These are observations of native state, not entry/exit detours on the event.

| Run / seat | Load-complete line | Intro start: line, frame | Intro clear: line, frame | Arrival line | First hash line |
|---|---:|---|---|---:|---:|
| 193245 host | 2643 | 2650, 8008 | 2679, 8309 | 2673 | 2677 |
| 193245 friend | 2734 | 2746, 3899 | 3118, 4200 | 3101 | 3107 |
| 194057 host | 2866 | 2874, 8761 | 2903, 9062 | 2897 | 2901 |
| 194057 friend | 2990 | 3000, 6011 | 3349, 6312 | 3337 | 3353 |
| 194738 host | 2769 | 2776, 8974 | 2806, 9275 | 2800 | 2804 |
| 194738 friend | 3332 | 3341, 6575 | 3695, 6876 | 3683 | 3699 |
| 201309 host | 2354 | 2361, 7856 | 2388, 8157 | 2382 | 2386 |
| 201309 friend | 2784 | 2795, 5433 | 3157, 5734 | 3145 | 3161 |
| 215808 host | 2587 | 2594, 8224 | 2624, 8525 | 2618 | 2622 |
| 215808 friend | 2832 | 2840, 5422 | absent | absent | absent |
| 220631 both | absent | absent | absent | absent | absent |
| 221605 host | 2499 | 2506, 8332 | 2534, 8633 | 2528 | 2532 |
| 221605 friend | 2895 | 2903, 5867 | absent | absent | absent |

All ten completed intros span 301 observed entity frames (~5.017 s at 60 Hz).
Using that frame rate as an **estimate**, the four long-run friend intros
start ~0.61–0.73 s after the host's intro clears. The host's intro starts
~5.02 s before its first epoch-4 hash. No exact wall-clock intro start or
host native request time is recoverable: nearby timestamped hashes can be
over 40 s apart because hashes stop outside safe gameplay. Do not turn
line spacing, file mtime, or buffered log tail arrival into timestamps.
Acceptance item 1 is therefore resolved for causal order and observed frames,
but its exact native boundary timestamps remain an instrumentation gap.

## Causal receipts and source explanation

| Run | Friend native exit to 02/14 | Host epoch received | Friend native issue | Load complete |
|---|---:|---:|---:|---:|
| 193245 | absent | 2720 | 2728 (ordinary safe gate) | 2734 |
| 194057 | 2967, count=2 | 2981 | 2985 (own blocked exit) | 2990 |
| 194738 | 3309, count=2 | 3323 | 3327 (own blocked exit) | 3332 |
| 201309 | 2758, count=2 | 2775 | 2779 (own blocked exit) | 2784 |
| 215808 | 2806, count=2 | 2820 | 2827 (own blocked exit) | 2832 |
| 221605 | 2859, count=2 | 2883 | 2890 (own blocked exit) | 2895 |

- `EnemySync.cpp::QueueHostBeginInstance` reserves the next room after a load.
  `HostBeginInstance` waits for `SafeNativeGameplay()` and
  `progresssync::HostReady()` for ordinary rooms. Axel I is such a room.
  Only then does it send `RoomTransition` and log `host arrived`.
- `ProgressSync.cpp::Tick` samples host progress only in safe gameplay.
  In 194738, host lines 2797–2798 sample the intro's progress changes
  (`1CE1:07>0F`, `1CE9:00>40`) immediately before the epoch-4 publication.
  This is an ordering constraint, not an arbitrary sleep to reduce.
- `Warp.cpp::HookedRequestTransition` blocks the friend's native requests.
  `IssueHostTransition` needs a queued host destination and complete progress,
  then permits either safe gameplay or a current-load blocked native exit.
  Removing the block alone would grant the friend independent room authority.
- `Warp.cpp::HostTransitionArrived` requires the issued load, exact destination
  programs, and `SafeToWarp()`. A load-complete receipt alone is not readiness:
  the friend's intro still needs to finish. `EnemySync.cpp` then sends its ACK.
- `SessionHost.cpp` currently records `TransitionAck`; it does not forward that
  receipt into the host DLL as a combat-release signal. A barrier needs an
  authenticated return path, not just a test reading the relay's log.

The 221605 paired Axel captures corroborate the state: the host has the combat
HUD and Axel active; the friend is in the dim native intro with the loading
heart. The 215808 friend's capture is black/loading. The clips end in the
ambush and do not film this handoff.

## Proposed fix, before implementation

Recommend a **scoped native combat-admission barrier** for this Mansion → Axel
handoff. Keep the existing host progress/room ordering and let both native
intros finish. Hold the host's combat until every participating friend has
completed its own issued load and intro and acknowledged arrival.

1. Arm from the host's exact 02/13 btl134 → 02/14 btl137 native transition/load,
   before the destination intro can release combat. Bind it to session,
   delivery generation, host epoch, native transition/load, full destination,
   and the admitted connection identities. Offline/solo play remains native.
2. Let the host intro and the existing first safe progress/room publication
   run. Gate native combat before the first combat update/hit on that same
   release frame. Keep the network/arrival owner draining while held. The
   native pause/admission boundary must be qualified offline before coding;
   an input-only hold, late Start press, or HP protection is insufficient.
   Holding event completion *before* `HostBeginInstance` would deadlock this
   publication and must not be used.
3. Relay a validated arrival receipt to the host DLL. Require the exact current
   connection/delivery, epoch and issued destination; reject old, duplicate,
   wrong-room and replaced-seat receipts. Release only after all current
   required seats finish their native intros. Do not skip events or pre-apply
   the host's post-intro flags into a friend's intro already in progress.
4. Use a 15 s deadline after host-ready (5.75 s observed + margin), measured
   independently of entity frames. On missing/stalled/replaced participation,
   log the exact failure reason and unwind the barrier's own hold within a
   bounded interval, returning native control. This is a failed barrier and an
   explicit fallback to native play, never a successful/fabricated arrival.
   It must not leave a pause open or keep the host waiting indefinitely.
   Reset/leave retires the barrier by its session/connection identity; no cached
   actor pointer survives a load. This incorporates the lead's timeout condition.

An overlapping-load protocol is a valid alternative, but cannot be achieved
by relaxing `HostBeginInstance` globally. It needs a distinct destination
intent and a proven pre-intro progress snapshot, separate from arrived/manifest
authority. It also cannot guarantee readiness before a first hit only five
host frames after release. The scoped combat barrier directly satisfies that
requirement without changing which native intro each seat plays.

Before building: qualify the native combat admission/pause seam and the return
ACK transport in source/static analysis. Add boundary receipts with
`GetTickCount64`, frame, TID, full room, epoch, load/transition and connection
identity at native request, load completion, intro begin/end observation,
host publication, client ACK, and barrier arm/release/failure. This closes the
unrecoverable timestamp gaps on the next authorized run.

Offline checks should exercise the actual barrier owner/transport boundary:
both ACK orders, ring pressure, reset/reconnect, solo, stale/wrong-destination
receipts, missing-seat deadline, and proof that intro updates continue while
combat cannot begin. A new native gate needs independent review. Live proof
remains scheduled by the lead using VUH-2898's bounded arrival wait: both
intros observed, friend arrival before the host's first Axel hit, saved files
unchanged. No live run is authorized by this proposal.

## Source qualification checkpoint (still before implementation)

The authenticated return path is reusable. The **native admission seam is not
yet qualified**; a menu request or actor freeze cannot currently claim that
combat is stopped before the first hit. No behavior-changing patch has been
written. The subsequent approved diagnostic patch is described below.

### Arrival transport

- `Protocol.hpp::TransitionAck` contains epoch, world, room and arrived only.
  It does not carry the full door/program tuple or echo a particular host
  destination announcement. `Warp::HostTransitionArrived` already requires the
  issued native load, exact full destination and safe gameplay after the intro.
  Preserve that proof; a relay log, avatar pose or load-complete line is not ACK
  authority.
- `SessionHost.cpp` validates a `WorldEnvelope` against the verified peer's
  connection/delivery and current session, rejects quarantine/source-floor
  violations, and retains its native source scope while forwarding.
  `sendTo` can target the current host delivery without relabeling a client ACK
  as host-produced. The current `TransitionAck` case only stores/logs the ACK
  and does not send it to the host.
- `NetworkClient.cpp` validates the receiver binding and current source roster
  connection/delivery before `onWorldEnvelope`. The existing `WorldPump`/inbox
  can carry the exact envelope into the DLL. A host ACK consumer must run before
  EnemySync's client-only packet drop, with the current peer identity checked
  again at consumption.
- The release message needs the exact destination/epoch plus an echoed host
  announcement challenge, bound to native load and admitted participants. A
  delayed receipt from a prior same-room/rebuilt load must not release a new
  barrier. Capture that challenge when the host publishes, echo it only after
  the friend's existing arrival proof, and reject replaced/quarantined seats,
  stale deliveries, malformed/suffix payloads and wrong targets. This is a
  protocol change still to implement/test; authentication alone is insufficient.
- The ordinary DLL world inbox is consumed from the entity-head frame service.
  A full native pause can stop that consumer. Reusing the transport does not
  prove hold-time liveness. A separate bounded, scoped control snapshot/channel
  serviced from a callback that continues during native pause is a candidate;
  it must not create a second consumer of the world ring. Existing EventHold's
  input-owner channel is a reference, not proof that this new gate will work.

### Native hold boundary

| Static receipt | Established fact | Limit for this barrier |
|---|---|---|
| `3BFD30`, `3BF5E0` | Current hook is a selected per-entity update inside dependency passes. | Its callbacks are not a complete census; suppressing Axel there does not establish that projectiles, collision or other native tasks stop. |
| `3BFA40`, `3BF2C0`, `3BF2E0` | `CONTROLLABLE` at `2A171E8` is a frozen-group bitset; native setters set/clear a group bit. | It is not a universal simulation pause. Writing it also closes SafeNativeGameplay/progress/publication gates and may stop the current owner service. |
| `1572B0` | The Start watcher checks zero pause blockers at `ABB878` and in-field state, then requests a menu. | `PAUSE_STATUS` is a blocker mask, not a freeze switch. An input-only hold does not stop combat. |
| `2E4950` → `151480` → `1515E0` | A menu request schedules a later native mode-transition task. | Calling it or injecting Start is not a synchronous pre-hit barrier. The observed first hit can be five entity frames after host arrival. |
| `3AC750` case 9 → `3BF2E0(0/1)`, then `3AC000` | Intro teardown can unfreeze groups before the outer completion function clears `EVENT_CONTEXT`. | An event-complete/context-clear observation alone is not proof it precedes all combat work; holding completion too early also blocks host publication. |
| `150820`, `150310`, `1515E0`, `150CA0` | The field mode object is `716860`; its state at `716884` is set to 2 on the native pause path and restored to 1 on return. The generic mode loop dispatches its task group only in state 1. | This identifies a broader native pause mechanism, not a safe direct-write API. It does not prove where a new hold can be acquired relative to an already-running field dispatch or worker jobs. |
| `14FC20` → `14FD60` | Task-group dispatch walks direct callbacks and fiber resumes. | Skipping one entry or guessing a parent task can skip intro/control work or fail to stop work already dispatched. Scope, ordering and an independent ACK/deadline service remain unproven. |

These are decompilation facts, not measured hook TIDs, an executed native pause,
or an assertion that a source-level call list establishes runtime scheduling.
The existing input hook has native pause/unwind support, but currently does not
drain the world inbox. Its diagnostic-thread and reentrancy guards must remain.

### Concrete next qualification

Before selecting a behavior-changing hook, obtain a **scoped observational
trace** of the Mansion → Axel native handoff: request/load completion, event
context and frozen-group release, field dispatch entry/exit, selected entity
update and first damage. Stamp GetTickCount64, actual TID, entity frame, full
destination, native load/transition and current session/delivery identity.
Distinguish nested dispatch/fiber stacks; cap the trace to this one handoff.
It must not call pause, change flags, suppress updates, or synthesize readiness.
Also observe which independent input/control callback continues during a native
pause before relying on it for ACK delivery or the deadline.

The existing logs cannot recover these missing boundaries. The lead required
qualification before coding a hold and owns all live runs. Preparing a
diagnostic-only patch for independent review was approved after the VUH-2881
qualification; the patch is described below. Static audit and a bounded
state-machine test cannot by themselves prove first-hit exclusion or pause-time
callback liveness. No VUH-2897 live execution is claimed here.

## Approved diagnostic trace (before a behavior change)

After VUH-2881 landed and its single live qualification passed, the lead approved
this diagnostic-only patch and a fresh independent review before any trace run.
The code is based on main `4194ea7c`. Following Oriel's R1 review, it requires
its own `KH2COOP_AXEL_BOUNDARY_TRACE=1` opt-in. Only
`net_prologue_mansion_axel_trace.json` supplies it, on both seats; the existing
base, downed, Thorn, Struggle and borough fixtures do not install these hooks
or enable the owner capture. The dedicated scenario preserves the base Mansion
route and both native intros, verifies installation before the route, and waits
for both clean trace footers before cleanup. No pause, group write, readiness
packet or update suppression is introduced. The 15 s behavior-barrier timeout
proposed above is still a future requirement; the probe's separate **30 s**
logging window never holds native control.

The probe arms once per process on an observed native or issued client request
from `02/13 btl134` to `02/14 btl137`, retaining the full destination tuple.
Blocked native exits are observations, not issued loads. Issue does not restart
the window. It records `GetTickCount64`, actual callback TID, fiber identity and
stack address. Paired native entries/returns have call IDs; TLS depth is not used
to invent parentage across fiber switches. The full current native tuple,
event-context identity, frozen-group mask, pause blockers, field-mode state and
group, timeline state and menu are scalar reads under SEH, explicitly not an
atomic snapshot. Native pointers are identities only, never retained actors or
dereferenced event contexts.

The four all-or-nothing, byte-pinned observational hooks are:

- `14FC20(group)`: entry/return of the native field mode's own task group
  (`716860 + 8`). Other groups forward without emitting; this is not a census
  of every worker task or every source of combat.
- `3AC000(task)`: the native intro/event task update, including its call to
  teardown `3AC750` and subsequent context clear. A return with context zero
  can be placed against nested unfreeze and dispatch receipts.
- `3BF2E0(group)`: native group release before/after, with the real caller RVA.
  The original function performs the release exactly once; the probe never
  writes its mask.
- `152680(task)`: `load-task-start` before the native field-load pipeline, with
  paired return/unwind. Static receipts show `152990` schedules readiness/
  fade task `152A90`, which performs old-room teardown and commits NOW through
  `152BE0`, then schedules `152680`. That task runs the load pipeline and finally
  schedules the already observed completion `152CD0`. Thus request-to-task-start
  and task-start-to-finalizers-returned can be separated from the intro's
  install/teardown observations. The stamp is the field-load task's entry, not
  an assertion about the first disk read or every alternate native load path.

Existing hooked paths supply event-start/install entry, native request and
post-finalizer load completion, entity-head service, successful host room
publication and successful client arrival-ACK enqueue. Native HP delta entry
and return carry actor identity, the requested delta and native result; compare
with the independently read canonical player identity, rather than treating
every enemy hit as a hit on the host. These do not claim an ACK reached the host.

An owner-only copied snapshot carries session salt, generation, delivery,
connection/host identity, epoch, load/transition, frame and the existing native
context read mask. Every point includes its separate `ownerMs`: it can lag the
native callback, especially during loading or pause. Load/issue/publication/ACK
points additionally carry their actual serial/epoch argument. Foreign native
callbacks never read EnemySync's mutable bridge, maps or owner globals.

The existing independent input callback contributes a 250 ms per-thread
heartbeat. If this run never enters native field pause, those points cannot
prove service liveness during pause; that qualification remains open. No pause
is requested to manufacture it. A 30 s deadline or 16,384-row budget ends only
the trace, with the reason logged. A later callback observes expiration; there
is no timer thread. Missing boundaries outside that window remain missing.
Logging and snapshot contention use try-locks. Only lost observations while the
window is active count as `dropped`; other task groups are classified through
an immutable image scalar before logging-lock acquisition. Pre-arm snapshot
contention is not loss. A candidate arm missed through contention is counted
separately and logged on the next input callback, successful request/point or
shutdown, even if no window is active; native-read/source/finished refusals also
name the reason. `lossObserved=1` means a scoped drop or an actual rejected
over-budget row; merely reaching the last allowed row is not loss. The fixture
requires deadline stops with `lossObserved=0 missedArms=0`, no missed-arm line,
and native load-task/completion observations on both seats.

No lock crosses a native game call. Try-lock acquisition does not wait, but the
existing logger can wait on its CRT FILE lock and flush I/O. Check entity-head
timestamp spacing in the eventual trace before trusting its timing. LastError and
native exceptional unwinding are preserved. Shutdown disables observations
before logger teardown and leaves in-flight trampolines process-lifetime.

The offline adapter control calls the production wrappers over a read-only
owned image, substituting only native leaves and MinHook installation. It
checks opt-out/pin refusal, partial-install rollback, exact native forwarding,
LastError, entry/return and SEH unwind receipts, copied owner provenance,
nonblocking loss reporting, wrong-target refusal and the independent-input
deadline. Oriel's R2 controls cover exact-budget emission, the next rejected row,
no later rows, shutdown at the exact budget without false loss, both supersession
forms, active shutdown and disabled post-shutdown observations. They parse each
return/unwind `call` against the corresponding entry `seq`, with a wrong-ID
negative control, and require no row from another task group even under logging
contention. Missed-arm flushing while inactive is also exercised. This is not a
live scheduling or first-hit exclusion proof.

For the original `e603b45c` candidate, serial/background Release passed and
**CTest 281/281 passed in 277.67 s**,
including **44 Python files** and the new production-adapter control's **18 checks**.
The unchanged resync test passed in 15.77 s. Offline structure/expression syntax
validation of the unchanged base Mansion fixture passed. The first build's
diagnostic generation narrowing warning was corrected before these checks;
the final DLL was rebuilt after restricting actor observations to entity-head
service. No sanitizers or native game execution are claimed. Raw build/test and
idle gate receipts remain in this worktree's `.local/trace-*` files. The revised
adapter control passes **35 checks**; three fixture controls restrict the opt-in,
compare the route with the base, and execute the real runner footer predicate
against positive/missing/censored trace logs. For the review delta, fresh
serial/background Release and **CTest 282/282 passed in 278.23 s**, including
**45 Python files**; resync passed in 15.52 s. The dedicated trace scenario
passes offline structure/expression syntax validation. Raw delta verification,
five native load-chain/pin query receipts and idle gate snapshots are retained
under `.local/r1-*` and `.local/ghidra-r1-*` and attached to the issue.

The next live trace must show both seats' request/install/teardown/arrival
boundaries and the first player HP loss, with no loss/budget censoring across
those boundaries. Preserve native intros. Only after reviewing that order can
the combat seam and an independent authenticated ACK/deadline service be chosen.
The lead schedules that run after independent source approval; none has run here.
`GetTickCount64` ties remain ambiguous; the probe does not stamp Axel's first
action before HP changes, the host's receipt of the ACK, or a pause that never
occurs. These limits are unchanged by the review delta.

## Approved live trace result

The single lead-authorized run of approved `2aaf1882` passed, with both traces
complete and loss-free. [Exact timing, safety receipts and the concrete bounded
barrier proposal](LIVE_TRACE.md) supersede the earlier instrumentation gap:
the host's first native Axel HP loss precedes the friend's arrival ACK enqueue
by 1.828 s; arrival lag is 5.656 s. Both native intros take 5.015 s. This measures
the defect; a behavior barrier and its held-state qualifications remain open.

## Evidence locations

Juno's four directories are under
`C:\Users\volpe\repos\kh2-multiplayer\.local\wt\vuh2466\build\scenarios\`;
Juniper's three are under
`C:\Users\volpe\repos\kh2-multiplayer\.local\wt-vuh2884\build\scenarios\`.
Their names start `20261026-<suffix>_net_prologue_mansion_hall` and append the
variant from the table and `_1`.

The issue attachment contains selected verbatim boundary receipts, 1-based
line numbers, original directory/PID names, SHA-256 of all 14 source logs,
and neighboring timestamp bounds. It is an extraction of existing logs, not
a replay or a fresh run. The fleet evidence-store CLI is unavailable in this
seat; Linear is the portable copy and the original run files remain local.
