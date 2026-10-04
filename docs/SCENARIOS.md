# Scenarios (VUH-1488)

A scenario is a JSON script that the runner plays against rig-launched KH2
instances: launch → load the save → warp → inputs → waits → memory
assertions → captures. Every run writes a report with its artifacts. A crash
or hang ends the run with a bundle instead of stalling it. Scenarios are the
regression suite for live behavior, and their reports are the evidence an
issue closes with.

```powershell
python tools/scenario/run.py tools/scenario/scenarios/boot_to_goa.json
python tools/scenario/run.py tools/scenario/scenarios/*.json --repeat 5
python tools/scenario/run.py tools/scenario/scenarios/net_host_transitions_smoke.json --validate
```

Requires the DLL and `kh2ctl` built (`cmake --build build --config Release`)
and the desktop session (KH2 crashes at startup from session 0).

`--validate` only parses JSON, checks supported steps, required fields and
instance references, and compiles expression syntax. It does not evaluate
expressions, acquire the rig lock, inspect processes or saves, or launch
anything. It does not establish live behavior or validate arbitrary CLI args.

## Historical activation replay: single cycle (2026-10-04)

`net_reconnect_shadows_activation_replay.json` is the current **98-step** bounded
fixture: the93-step population derivative plus five post-replay hold checks.
It requires both `KH2COOP_SURVIVING_PACK_PREPARE=1` and
`KH2COOP_SPAWN_TRACE=1`. The original repeated-reconnect fixture and strict
ten-cycle acceptance remain separate and unchanged. The baseline
`reconnect_mark` observation supports a15s bound; extending that observation is
not recovery and does not relax population, HP or identity checks.

The default-off implementation uses the actual host first-emission float4 for
one exact all-alive five-Shadow controller through its normal scheduled update.
It adds no native call or geometry/flag/cache/count write. Preconditions include
the recorded input/definition/incarnation binding, zero deaths/tombstones and a
fresh client empty cache/census with counts5/5, flags2, cooldown0 and stage0.
The host's observed header+E0 may match client+E0/1 under the reviewed explicit
exception, with both raw values retained and no marker rewrite. See
[FORCED_RESYNC.md](FORCED_RESYNC.md#historical-activation-replay-current-default-off-policy)
for the HARP/v1 trailer, initialization binding and refusal rules.

Historical input permits at most512 **actually completed selected-controller
original updates**, under the same30s deadline. Selected pending/deferred
progress may wait; positive conflicts fail. Hold selected claims until the
whole exact set has reconciled HP. Then restore live host input and require120
actual selected-controller completions, continued exact5/5/5 membership at
HP17/max20, no extras/deaths/tombstones and matching final controller state.
The64-slot per-controller completion table never evicts and fails unavailable
on overflow; global sequence differences and lease-copy attempts are not counts.
A timeout after partial emission stays a partial failure, not a retry.

| Attempt | Result | Scope |
| --- | --- | --- |
| [20261004-132438](../build/scenarios/20261004-132438_net_reconnect_shadows_activation_replay_1/report.md) | FAIL at step81 `reconnect_mark`,204.2s | Failed before reconnect; all four protected saves unchanged. |
| [20261004-133512](../build/scenarios/20261004-133512_net_reconnect_shadows_activation_replay_1/report.md) | FAIL at step81 `reconnect_mark`,200.9s | Failed before reconnect; all four protected saves unchanged. |
| [20261004-134727](../build/scenarios/20261004-134727_net_reconnect_shadows_activation_replay_1/report.md) | FAIL at added step93 replay-verification wait,224.4s | Original93 steps passed, both post-rejoin censuses5/5/5. No ResyncPlan or replay ran; all four saves unchanged. |
| [20261004-140622](../build/scenarios/20261004-140622_net_reconnect_shadows_outside_activation_control_1/report.md) | FAIL outside control at step111,234.4s | First sample5/0/5 outside; second5/5/5 after host entered BOX0. The outside precondition broke; this does not falsify the diagnosis. Four saves unchanged. |
| [20261004-142007](../build/scenarios/20261004-142007_net_reconnect_shadows_outside_activation_control_farther_1/report.md) | FAIL final assertion with `KeyError: runnerBindings`,235.3s | Both complete post-rejoin samples5/0/5 and all seven BOXes outside; native reconnect audit ready. The fixture asked for runner fields absent from `reconnect_check`. Final death/no-replay checks were not executed; four saves unchanged. |
| [20261004-143318](../build/scenarios/20261004-143318_net_reconnect_shadows_outside_activation_control_farther_1/report.md) | PASS all120 control steps,242.1s | Two complete5/0/5 samples with host endpoints outside all seven BOXes, original survivor HP17/max20, exact original identity/load/progress and zero-death/no-replay checks. Four saves and98 sealed inputs unchanged. |
| [20261004-143903](../build/scenarios/20261004-143903_net_reconnect_shadows_outside_farther_forced_activation_replay_1/report.md) | PASS all142 forced-treatment steps,259.3s | Qualified5/0/5 prefix, one slot1 Bootstrap, actual recorded host point,33 completed historical updates and120 completed live-input updates, reconciled1 and two complete5/5/5 HP17/max20 samples. Four saves and98 sealed inputs unchanged. Forced scope only. |

These are observer/setup failures, not new recovery failures or acceptance. The
observer validator's hardcoded WorldBridge10 check was inconsistent with current
WorldBridge11. The exact version correction is integrated;36 offline evidence/
runner controls and22 canonical evidence tests pass. The intact saved peer0
observation now qualifies individually, while both incomplete three-peer artifacts
remain rejected. Run134727 then passed baseline collection and all original
checks; neither older failed run is retroactively passed.

The replay fixture is held because automatic cached-world rejoin does not start
the explicit ResyncPlan carrying historical metadata. All three native opt-ins
were configured, yet no resync-activation receipt appeared. Its repeat candidate
is also held. The separate control uses actual native host movement outside all seven
current BOXes and requires5/0/5 after Friend1's fresh rejoin. Its complete PASS
qualified the separately labeled slot1 forced-resync experiment below. Automatic
integration and both original/forced-outside ten-cycle gates remain later work.
The farther control uses the same bounded native movement toward Z500, with
observed clearance before rejoin. Run142007 remains FAIL; its schema correction
uses returned peer PIDs plus the producer's unchanged mandatory process/argv/
relay guards. Run143318 subsequently passed the complete control. Run143903
preserved that exact120-step prefix and exercised one canonical host request
for Friend1. It accounts for exactly one additional Friend1 load4->5 and
transition3->4, with original processes/session/pins/roster and unchanged
survivor loads. Native ACK, relay/runtime terminal evidence, full three-peer
hashes and independent native censuses agree. Host point bits join the receiver
by firstUpdate55413; client raw point bits and canonical snapshot bytes are not
independently retained. Measured endpoints do not prove continuous outside state.
This is bounded forced recovery, not automatic or ten-cycle acceptance.
The earlier first-rejoin5/0/5 failure and zero completed ten-cycle acceptance
remain. General deaths/waves, barrier/music/room-script parity and physical
remote recovery are unqualified. List unavailable battle/event fields explicitly;
the location's `btl` value alone is not battle-state parity. The older B1 creator,
loader-installation and spatial-bypass work below is parked research for an
added-dispatch alternative, not a prerequisite for this scheduled-update policy.

## Native record-content resync: offline candidate (2026-10-04)

The current candidate uses protocol **9**, AvatarBridge **2**,
WorldBridge **11** and CaptureChannel **1**. The dated protocol-8 results below
remain historical evidence for their exact binaries and fixtures; they do not
validate this new candidate. All components must use compatible rebuilt
versions. The new bounded living-population native result is recorded below;
the broader reconnect and creation acceptance gates remain open.

Actual native snapshots now require coverage **255**, a complete bounded
ordinary-controller catalog with full 44-byte headers/all ordered 64-byte
records, and checked current actor-to-record membership. Exact byte comparison
excludes only header +0xE for peer compatibility; the raw byte remains evidence.
Synthetic coverage127 stays supported by the generic codec and is rejected by
the actual native consumer. Missing, ambiguous, unsupported or partial native
associations cannot fall back to matching object/point/manifest index while
fenced. Whole-current-catalog and population qualification precedes the first
HP/death write, with positive/max bounds for every sampled native and desired
living HP, plus repeated selected bytes/state and a final context endpoint.

Waiting holds writes; Exact is an exact-only strategy that can await natural
population recovery at the unchanged deadline; terminal Failed survives result
cleanup. Checkpoint is observation-only and performs no HP/death repair,
progress write or second reload. Changes after preflight can still leave partial
application. Unknown lethal outcomes or failed post-call reads/censuses remain
Failed without replay. Neither sampled equality nor repeated pointer values
prove atomicity, controller lifetime, incarnation or pending-work exclusion.

Actual offline Release and Windows MSVC ASan executions each passed:

| Component | Executed checks | Receipts |
|---|---:|---|
| Common record comparison | 154 | [Reader/common final receipt](../build/rig/native-record-content-reader-implementation-fixed-20261004-01/receipt.md) |
| Production native reader/trace harness | 276 | [Reader final receipt](../build/rig/native-record-content-reader-implementation-fixed-20261004-01/receipt.md) |
| Wire/transaction harness | 257 | [Release](../build/rig/native-record-content-wire-implementation-20261004-01/receipt.md), [Windows ASan](../build/rig/native-record-content-wire-asan-20261004-01/receipt.md) |
| Production consumer and write guards | 272 | [Final guard receipt](../build/rig/resync-native-record-write-guard-controls-20261004-01/receipt-final.md) |

Counts include repeated setup checks and are not independent scenario counts.
Independent [common](../build/rig/native-record-content-independent-review-20261004-01.md),
[reader](../build/rig/native-record-content-reader-fixed-independent-review-20261004-01.md)
and [consumer fix](../build/rig/native-record-content-resync-consumer-fix-independent-review-20261004-01.md)
reviews are READY within their offline scope. Initial HOLDs, opaque test aborts,
the source-serial fixture failure and intermediate 233/269-check results remain
preserved. Final guard source SHA256, before receipt-only instrumentation, is
`8E7BBFF246379030578793C42569B45F1E134960C9CC95D2BA3034E77E685F03`.
See [FORCED_RESYNC.md](FORCED_RESYNC.md#native-record-content-candidate-2026-10-04)
for exact behavior, limits and evidence lineage.

**Bounded native result, 2026-10-04:** the unchanged fixture
`net_forced_resync_shadows_population.json` passed all **89 steps in 201.7 s**,
[`20261004-043836`](../build/scenarios/20261004-043836_net_forced_resync_shadows_population_1/report.md).
One both-target Bootstrap preserves the original connections/delivery identities;
Friend1 and Friend2 load3 to4 and return checks63/count5/dead0 on distinct frames
6100/6101 and3633/3634, with matching actual relay and host-runtime terminals.
The host's full coverage255 catalog contains10 definitions/26 records/five refs.
Both targets have five unique native20-to-requested17 store returns, followed
by actual HP17/max20 on both content-qualified observation frames. Immediate
store readback and store frames/loads are not logged. Two fresh matching hash
samples and two complete independent native censuses retain all original
bindings1-5/object302/type4/damagedHP17 and progress; no death history appears.
[Content](../build/rig/native-record-content-native-content-audit-20261004-01.md)
and [population/capture](../build/rig/native-record-content-population-native-independent-review-20261004-01.md)
audits are READY within that scope. Current EnemySync source is
`04298BAB3B948BC1E04F3E21F5D18D442D3638F5BCB0F6BBBF01F7C3D54F17AB`;
all101 inputs/four protected saves match through terminal verification, all8
owned processes exited and the rig is idle. The full build, eight affected
regression suites and receipt-instrumented272 Release/Windows ASan controls pass.

Three intact inspected1920x1080 final PNGs show rendered courtyard gameplay,
with overlap/occlusion; count and HP rely on the checked native rows.
Post-resync geometryComplete=false and positive geometric stability is unclaimed.
Progress apply has zero changed spans/bytes and only an apply-boundary personal
witness. Automatic report1 is labeled complete but aggregate collection is
partial (one cadence suppression). The prior first-rejoin population FAIL,
zero completed ten-cycle repetitions and disabled creation remain. This result
does not close absent surviving-set reconstruction, lifetime/creator/pending
closure, dead recovery, natural/personal progress, full-route or physical remote
acceptance. Historical protocol8 results below remain unchanged.

**Natural-resource diagnostic, 2026-10-04:** the same89-step fixture passed in
203.8s with PREPARE, SPAWN_TRACE and NATURAL_RESOURCE_TRACE enabled:
[`20261004-080445`](../build/scenarios/20261004-080445_net_forced_resync_shadows_population_1/report.md).
All173 inputs and four protected saves matched at terminal verification. The
three byte-qualified/pinned package observers retain25 full selected-definition
parent matches and270 exact child joins across the first selected pack on each
peer. Each friend suppresses235 later child receipts after the512 budget cap;
later reload child absence is unqualified. Exact original IDs1-5/HP17/max20,
load3-to4, ACK63 and matching relay/runtime terminals pass. Collection remains
partial, positive point stability unclaimed, and only the runner has a retained
OS exit code. All eight recorded PIDs are absent and the rig/index are idle.
This is bounded natural Bootstrap/observer validation; first-rejoin FAIL,
zero completed ten-cycle repetitions and disabled added creation remain.
See [fixture/lifecycle audit](../build/rig/native-resource-native-20261004-01/lifecycle-audit.md)
and [full-byte parent joins](../build/rig/native-resource-native-20261004-01/parent-content-review.md).

## Fresh forced resync: implementation candidate (2026-10-03)

The then-current protocol **8** / AvatarBridge **2** / WorldBridge **10** /
CaptureChannel **1** candidate implements fresh checked host capture, immutable
targets, delivery/source fencing, complete staged bootstrap and distinct native
convergence ACKs. [FORCED_RESYNC.md](FORCED_RESYNC.md) defines the fixed deadline,
one non-reloading Checkpoint and explicit unavailable outcomes. The verified
offline receipt is `build/rig/resync_offline_receipt_20261003.json`: Release and
Windows ASan each passed 1,897 C++ checks, portable ASan+UBSan 1,340, with no
sanitizer findings. Each includes 205 codec/ENet resync controls; Windows adds
185 production native controls over owned memory. Nine existing linkage and
24 saved-evidence cases, three actual PID-0 runtime smokes and the new scenario
structure check passed. Synthetic native facts remain labeled.

`net_forced_resync_smoke.json` prepares one host command targeting both original
friends in a complete empty GoA room. It requires correlated native and relay /
host-runtime results, changed loads, two native observation frames, then two
fresh independent hash/census joins. It does not establish nonempty, dead or
remote acceptance. Native execution remains separate from the offline receipt.

Its current local execution `20261003-131544_net_forced_resync_smoke_1` passed all
18 steps in 149.2 seconds: both friends loaded serial 3 to 4, produced distinct
native-frame Converged ACKs, and matched both terminal results and two complete
post-resync empty census/hash samples. Saves were unchanged. Progress apply
changed zero bytes; nonempty/dead/changed-flag recovery remains open. The first
attempt is retained as a failure at obsolete readiness log text; the runner now
waits for verified membership while native bootstrap checks remain separate.

`net_forced_resync_native_nonempty.json` retains a separate living-only first-pack
fixture with host-only damage and exact typed native population checks. Its first
run `20261003-132801` failed before requesting resync: current four actors matched
at HP 153, but two earlier positive-last-HP bindings had disappeared and received
transport tombstones. The zero-history gate rejected this setup. This is a
retained prerequisite failure, not a native recovery result or proven lethal
death. The original fixture remains strict and unchanged.

`net_forced_resync_first_activation.json` is a distinct setup with one native
900-ms pulse per peer and the same original zero-history baseline. Run
`20261003-133716` failed at population readiness: all peers stayed empty, before
damage or the command. Its saved evidence and strict expectations remain. Native
activation geometry must establish the next setup; pulse count alone does not.

`net_forced_resync_native_shadows.json` uses measured courtyard activation. Run
`20261003-135207` produced five object-302 Shadows at HP 20 with complete native
joins on all peers, then failed positive host-point stability before damage or
resync. Its 90-step source and failed evidence remain unchanged.

The separate 89-step `net_forced_resync_shadows_population.json` removes only
that positive geometric acceptance assertion and makes no positional stability
claim. All raw collection, negative authority controls, complete population/HP
checks, original zero-death history and recovery requirements remain identical.
Run `20261003-140717` passed in 190.4 seconds: exact HP 20 to 17 on all five
original Shadows, one both-target command, genuine loads 3 to 4, distinct native
Converged frames, matching snapshot/cut/fingerprint and relay/host-runtime
terminals, then two fresh complete native census/hash joins at HP 17. Three
final game captures were saved and four protected saves were unchanged.
This is local living-only Bootstrap convergence. Clients already matched before
the request, and forced progress application wrote zero bytes. Deliberate
stale-client repair, changed progress, dead reconstruction, Checkpoint, physical
remote recovery and positive position stability remain open.

`net_forced_resync_shadows_progress.json` replaces only that fixture's command
at index 79 with `progress_fault_resync`; all other 88 steps remain identical.
Run `20261003-145341` failed its unchanged negative geometry guard before any
fault or command. A single unchanged retry `20261003-152248` passed all 89 steps
in 187.9 seconds. Full raw snapshots retained Friend1's sole shared byte fault
00 to 02, two fresh progress-only mismatch samples and one exact both-target
transaction. An ordered native apply changed one span/byte back to 00 before
the additional loads 3 to 4; complete postchecks restored the original full
masked progress and five Shadow HP17 bindings. Cleanup performed no write.
Four protected saves stayed unchanged. This proves one injected shared-bit
repair locally, with non-atomic context/read limits; it does not prove natural
progression, stale HP, dead history or physical remote repair. Apply-level
personal equality stays separate from post-load differences (both clients'
Goofy current MP at SAVE+0x271E changed from 100 to 90; sampled HP stayed28/28
and maxMP100. OpenKH's FM character layout identifies this as record2,+6.
The causal writer and exact write time remain unestablished. The earlier frozen
receipt's HP label is an error; preserve that artifact and its original bytes).

The three final capture steps now retain their actual CLI completion receipts
alongside the PNG paths. In this run, each exact PID's independent mailbox
reported request/done 36, status 0, one frame, renderer 12, format 87 and
1920x1080. Equal sequence numbers do not mean synchronized game frames. Earlier
reports lacking these fields remain unchanged; no receipt was backfilled.

The operator command uses an existing admitted host mapping and a separate CAS
mailbox; `queued:true` is not relay or native success. Cancellation unarms native
world authority, and lazy attachment preserves bounded post-cut continuation.
The relay defaults to native traffic; legacy `--simulate` sessions cannot supply
native capture. Dead-spawn identity, nonempty native reconstruction and remote
acceptance remain open. Frozen results below retain their original versions.

## Same-process native Friend1 reconnect candidate (2026-10-03)

`net_reconnect_shadows_population.json` preserves the population fixture's first
79 step objects. After two fresh native/hash observations establish the original
five Shadows at HP20/maxHP20 and host-only damage3 leaves HP17, it seals a
three-peer reconnect baseline and interrupts only the original owned Friend1
runtime. The game, host, Friend2 and relay stay alive. The runtime is resumed
after fresh relay retirement and both survivors' matching roster observations;
a watchdog/finally recovery bounds the interruption to 20 seconds on a working
OS/API. No runtime restart, host restart or forced-resync fallback is used.

The strict checkpoint requires intact raw log prefixes, the same original
processes/session/host/epoch/full location, a fresh Friend1 connection and
generation, actual typed close/retry/admission, and ordered new native
progress/apply/issue/load/arrival/activation evidence. Current puppet provenance
must join distinct active native targets with finite poses and bounded error.
Two fresh population/hash samples and two complete native censuses separately
require the original IDs/object302/HP17/maxHP20 and unchanged progress. A second
read-only reconnect check after those censuses catches survivor reloads during
that window, before the original zero-death guard and three renderer captures.

The 93-step fixture passed in 193.5 seconds on the local Session-1 rig
(`20261003-173002`). Its actual pause/resume returned native status0 once each,
with elapsed5.062s and no deadline or emergency cleanup. The original Friend1
runtime retried after a typed timeout: connection2 became4, generation2 became5,
and native load3 became4 under the original session/host/epoch. Host/Friend2
loads stayed2/3. Both recovery checkpoints replay from 21 independently checked
raw log receipts; three independent reviews accepted identity, lifecycle and
population evidence. Five original IDs/object302 retained HP17/maxHP20 and
progress9AC5499B in fresh complete samples, with zero deaths/tombstones. All
three final PNGs have exact PID/mailbox completion receipts; four saves were
unchanged. Automatic diagnostics retain a complete original-roster report and
two cadence-suppressed triggers, so aggregate collection is partial.

The offline prerequisites passed123 Python controls, full CLI structure
validation and an actual native suspend/resume adapter smoke on a created
throwaway process. The initial older fake-context compatibility failure remains
retained; that fixture now has the real owned-process registry and three
duplicate-launch controls. Exact raw sample bytes and partial failed observations
are retained for replay. Full log reads, observations and captures remain
separately sampled, not globally atomic. This single living-only loopback result
does not establish repeatability, general/dead reconstruction, stable-point
geometry or remote recovery. A fresh process launch would test readmission
rather than this same-process automatic recovery path.

The separate ten-cycle `net_reconnect_shadows_repeat.json` retains every
population/progress/death gate and the original host/session throughout.
Run `20261003-175046` failed after352.6s at step161, cycle06's statehash check.
Cycles01-05 have complete recovery, population/census and final evidence.
Cycle06 rejoined as connection9, generation20, native load9/transition8, but
Friend1 stayed empty while Host/Friend2 retained the original five HP17 Shadows.
All three still matched shared progress9AC5499B. Cycles07-10 were not reached;
five passing prefixes do not establish ten-cycle acceptance.

Independent review replayed119 raw receipts, six baselines/after checkpoints,
five final checkpoints and six retirement proofs. Complete native tails show
zero survivor reloads and six Friend1 reloads. All four saves stayed unchanged;
the owned instances exited and the rig is idle. Full failure evidence and frozen
inputs are retained in `build/rig/reconnect-repeat-package-20261003`.

Cycle06 logged30 lease points outside all seven region boxes from cycle05's
saved geometry, with32 subsequent empty native hashes and no local spawn/match
lines. This is a strong activation-geometry hypothesis, not proven cause:
cycle06's own geometry was not collected before the hash gate failed.
That failure required read-only current-load geometry/controller/native census
before the unchanged gate in a separate ten-cycle diagnostic. Keep all original
guards; do not reposition the host, shorten the run or use forced recovery to
turn the retained failure into a pass. Controller records/apply counters alone
do not establish materialized native actors.

`net_reconnect_shadows_repeat_diagnostic.json` is the separately named collection
derivative: all230 original step objects remain in order, plus20 all-peer native
censuses (two samples1200ms apart before pause and after rejoin per cycle) and
ten additional post-census identity checks against each cycle's original mark.
It retains empty native rows, current region/controller bytes and exact accessor
position readbacks; collector completeness is separate from geometry coverage.
The added reads change timing, so a different failing cycle is possible.

Run `20261003-182709` failed after358.0s at step143, cycle04's unchanged hash
gate, after three complete cycles. Fourth-cycle before-pause native counts were
5/5/5, including Friend1 load6/transition5, while the host was already outside
all seven boxes. After actual rejoin, connection7/generation14/load7/transition6
passed both original and added identity checks, but two complete independent
current-load censuses had counts5/0/5. All48 added peer snapshots have complete
current geometry/native identity;105 raw receipts independently replay.

Both stable sampled host points had Y=-1, outside every current Friend1 BOX by
10.028 units in Y. All35 logged new-load lease points also reject those same
load7 regions without edge uncertainty;38 native hashes stay empty with no
spawn/match lines. Friend1's current cache is entirely zero, while controller
currentCount/initialCount still5/5: those fields do not count emitted actors.
The survivors retain the original HP17 pack/progress, with zero observed death
history. Four saves unchanged, original owned processes exited, rig idle.

This closes missing failing-load geometry/census coverage, not actual predicate
return or host-drift causation. A bounded default-off pass-through BOX observer
is the next diagnostic candidate, retaining exact consumed input/native return
and a completed tick even when no spawn wrapper executes. Controlled native
host movement can establish outside-region setup while retaining the original
pack before a fresh reload; it remains separately labelled mechanism diagnosis.
No host freeze, stale-point replay, native spawning/count/cache writes or gate
relaxation establishes recovery. Full ten-cycle and broader gates remain open.

The separately frozen BOX observer now has independent source approval and
Release/Windows ASan controls: each configuration passed167 spawntrace and185
nativehit assertions, with successful DLL builds and no recorded sanitizer
finding. Exact four-source deltas, six binary hashes, final control logs and
byte gates are in `build/rig/spawn-geometry-controls-20261003/handoff.md`.
The first live attempt reports verified MinHook installation on all three peers;
its selected fresh-load predicate receipts remain unexecuted (setup failed below).
Default OFF; arming requires client load4/transition3 and fulltuple
`[5,6,0,1,1,0]`, after a prior lower lifecycle, with64 selected attempts/2000ms
maximum. Zero predicate calls remain unclassified; all-seven rejection requires
seven ordered current region calls with genuine AL0, complete coverage and no
unexplained loss. Rejection of one particular point by all seven additionally
requires identical16-byte input across those calls. Original calls and the final
lease-copy boundary are preserved.

`net_reconnect_shadows_outside_trace.json` is the offline validated/root reviewed
282-step derivative: all260 diagnostic objects/full ten cycles remain exact.
One250ms measured native input probe and one100..2000ms continuation must
qualify the original fiveHP17/current outside geometry/identity before fault.
It has no movement retry or direct transform. Run `20261003-191457` failed setup
at step105 after194.3s: all105 preceding steps passed, but the final host position
readback changed during capture. Original five HP17/max20, progress, identity
and zero-death checks passed; pause106 and every reconnect cycle were unreached.
The original strict fixture and FAIL remain unchanged. All326 geometry summaries
remain Waiting/ticks0; there are no selected tick/region/input/native AL receipts.
Installed hooks and zero unarmed ticks do not establish zero-call rejection or
exercise the64-tick/deadline/nesting/unwind bounds. All four protected saves
were unchanged; all eight owned run processes exited and the rig is idle.
The198-file setup-failure archive retains the entire74-file run, full three-peer
logs,103 input fingerprints (101 external source snapshots), three reviews and
an actual-data chart: `build/rig/linear-evidence-20261003/kh2-outside-trace-setup-failure-20261003.zip`.

`net_reconnect_shadows_outside_endpoints_trace.json` is a separately named,
282-step derivative. Only the two added outside-geometry assertions
95/105 change; every other280 step and all260 original diagnostic objects remain
exact. Original population/HP/progress/death/identity and full ten-cycle gates
stay mandatory. Its additive `sampledEndpoints` results evaluate both retained
16-byte before/after readbacks against the current rooted regions, without new
native reads or writes. It requires two snapshots, all three geometry peers and
seven distinct BOX results per endpoint. Original false position-stability flags
remain visible. Outside endpoints do not prove continuous residence, atomic
sampling, actual native AL or movement cause, and cannot retroactively pass the
old fixture. Independent review accepted the exact helper/fixture;15 geometry
and26 adapter tests pass, saved replay preserves all72 legacy rows, and29
fixture mutations are rejected. Source/control/review evidence is frozen in
`build/rig/linear-evidence-20261003/kh2-sampled-endpoints-offline-20261003.zip`.
Its first sealed native attempt is recorded below. A shorter prefix cannot
establish ten-cycle acceptance.
The separate35-file offline archive is
`build/rig/linear-evidence-20261003/kh2-bounded-geometry-observer-offline-20261003.zip`.
It is distinct from the349-file diagnostic FAIL archive and its67 pre-run inputs.

### Selected native endpoint attempt: first-rejoin population FAIL (2026-10-03)

After James released the desktop, the119-input seal and verified Session1
bridge launched exactly one attempt, `20261003-212208`. It failed unchanged
population/hash gate111 in246.3s after setup and genuine same-process rejoin.
Original Friend1 game528568/runtime388948 recovered connection2 to4,
generation2 to5, load3 to4/transition2 to3. Host531376 and Friend2497064 did
not reload. Two complete post-rejoin censuses show[5,0,5]; Friend1's39 fresh
hash headers remain empty, while survivors retain originalIDs1-5/object302,
HP17/max20 and progress9AC5499B. No complete cycle, final native/final capture
or cycles02-10 were reached. The strict earlier setup failure remains FAIL.

Friend1's actual BOX hook now supplies64 selected receipts. Its `tick` field
is a controller-update sequence, not elapsed milliseconds; per-tick wall-clock
times are not serialized.
Ordinals1-11 were held/incomplete (`originalReturned=0`, calls0); they are not
complete zero-call native ticks. Ordinals12-64 are complete original-returned
ticks with7 distinct current rooted region calls each. All371 genuine results
are AL0 with identical input bytes `BD6AD842000080BF6641B7430000803F`, decoded
float4(108.2084732055664,-1,366.51092529296875,1). The captured window directly
proves native rejection of all seven regions at that supplied point; it is not
a continuous host trajectory or an emitter/readiness decision. TickLimit3
stopped at64, with zero dropped/unwound/foreign/overflow/nesting counters.
Deadline, overflow and unwind behavior were not triggered in this run.

All64 full44-byte headers,320-byte record arrays and448-byte descriptors agree
without normalization. Target record-array SHA256
`447E6D0F4C6E5AD652F96955D9D3DB86373B7675E112FAC39303429DDFD660DF`
matches this run's setup provenance arrays. The observer retains only the
controller's64-byte prefix; this is not a full0x58 controller snapshot or a
continuous incarnation/pending-actor proof. Root independently replayed raw
native fields and corrected its own initial88-byte controller expectation to
the frozen observer's0x40 declaration. Unverified numeric delay metadata from
root replay01 and the timing-unit metadata from replay02 are withdrawn by
replay03; full header bytes remain authoritative.

Three independent reviews cover native geometry, exact population/progress,
and lifecycle/collection. All94 terminal run files, three full native logs and
119 sealed input copies are frozen. Automatic report1 is partial because two
inject tails changed during collection; its three earlier PNGs are complete
transfer/structural evidence. The post-rejoin empty-population trigger was
cadence-suppressed, so no second bundle or final post-rejoin screenshot is
claimed. Root's fresh audit confirms all eight owned processes absent, empty
rig/lock/index, same HEAD and119 inputs unchanged. All four protected save
hashes match the seal's before hashes. No retry, movement tuning, gameplay
repair, cache/count/flag patch or production source change occurred in the run.

Evidence: `build/rig/reconnect-outside-endpoints-package-20261003-01/`,
`build/rig/reconnect-outside-endpoints-root-replay-20261003-03.json`,
`build/rig/reconnect-outside-endpoints-native-terminal-safety-20261003-01.json`,
`build/rig/reconnect-outside-endpoints-native-save-audit-20261003-01.json` and
`build/rig/reconnect_outside_endpoints_native_20261003_02.png`.
At this checkpoint the proposed next step was snapshot-authorized enrollment
with exact native records, bounded partial outcomes and ordered post-cut deaths.
The current policy above instead restricts recovery to all-alive packs and the
recorded host input; death reconstruction and added-dispatch ownership remain
separate, parked work. These AL observations alone close no recovery or
ten-cycle/five-room gate.

### Offline native event-gate observer (2026-10-03)

`KH2COOP_SPAWN_ENROLL_OBSERVE=1` adds a separate, default-off diagnostic
for the genuine no-argument event gate at3ABC80, matched to expected return
address3FF0E5. It requires the existing trace and valid geometry selection,
and shares that selection's64-update/2,000ms budget. The detour calls the
original exactly once and preserves its AL. The protected final lease-copy
boundary is unchanged. This is a callwise witness, not original-phase proof
or enrollment authority; a complete AL0 receipt alone cannot authorize creation.
Requested-but-unavailable, repeated, nested, unwind and dropped receipts remain
explicitly incomplete. The log's expectedCallerRva is a configured filter,
and updateSequence is not elapsed milliseconds.

Release and Windows ASan builds passed. Each configuration passed198 existing
spawntrace controls, extended for the observer, and185 native-hit controls.
A signed/unsigned test comparison initially failed compilation; its one-line
correction and the original failure logs are retained. Independent review also
corrected a misleading observed-caller label in unavailable receipts; both
native-hit executables were rebuilt and rerun after that serializer change.
Evidence is in `build/rig/enrollment-gate-observer-controls-20261003/` and
`build/rig/enrollment-gate-observer-independent-review-20261003.md`.

No native hook was installed by these controls. At that checkpoint native
installation, gate values and live overhead were untested. The subsequent
separate native attempt below supplies installation and selected AL0 evidence;
negative/unwind cases and live overhead remain unmeasured. This candidate is
separate from the earlier sealed endpoint run and creates no actors or recovery
acceptance evidence.
For the parked added-dispatch alternative, pending occupancy, controller
incarnation and a serialized check-to-dispatch window remain unresolved.
The current scheduled-update policy uses its explicit prestate/recorded-input
qualification and does not inherit an added-call B1 requirement.

### Native event-gate attempt: first-rejoin population FAIL (2026-10-03)

The separate130-input attempt `20261003-221650` used the unchanged282-step
endpoint fixture and enabled the reviewed event-gate observer only in its child
environment. Runner457128 executed in Session1 under the existing desktop
bridge; all three live OS-associated injected copies matched the sealed Release
DLL. This is loaded-module association plus on-disk hash, not an in-memory
code digest. All three reported independent gate installation/coverage1.

The run failed the same population/hash gate111 after243.7s, with111 preceding
steps passing and zero complete cycles. Friend1 timed out/rejoined/reloaded in
its original process and stayed physically empty; survivors retained the
original five object302 enemies at17/max20. Its selected load4/transition3
capture contains64 receipts: nine held/no-call/nonreturned captures remain
incomplete, and55 complete single-call gate receipts retain actual AL0.
Each complete receipt joins seven distinct current BOX calls, all385 native
returns AL0. Each tick uses one exact point across its seven calls; seven point
values occur across the window. This is captured-input evidence, not continuous
trajectory, original-phase authority, pending exclusion or a creation fix.

TickLimit64 was exercised. The traced63224..63854 span is630 controller
updates, not milliseconds. All selected ordinals and joins are present, with
no selected nesting/unwind/overflow; periodic dropped/unwind counters stay0.
Global event-gate foreign counters include ordinary unselected calls and do
not mean selected corruption. Native AL1, unwind, overflow and deadline cases
were not exercised, and timing overhead was not benchmarked. Record320 bytes
retain the same exact current-pack digest; the controller receipt remains its
64-byte prefix rather than the full0x58 allocation.

Root's terminal audit confirmed all eight owned processes absent, empty rig/
lock/index, sameHEAD and all130 sealed inputs unchanged during the attempt.
All four protected save hashes match before. The94 run files, three full
native logs, inputs, loaded-module receipt and independent reviews are retained
in `build/rig/event-gate-observer-native-package-20261003-01/`; root raw replay
is `build/rig/event-gate-observer-root-replay-20261003-01.json`. A fresh manifest
arrival does not prove living native population, and a progress receipt's byte
length is distinct from bytes actually changed.

Automatic report1 is complete for the original roster: all12 peer artifacts
have checked integrity, its native log tails are bounded/truncated rather than
interrupted, and its three PNGs precede the pause. Aggregate collection remains
partial because report2 records an active-collection witness and report3 is
cadence-suppressed after rejoin. Neither is a final recovery screenshot or an
independent collected bundle. This differs from the earlier attempt's
interrupted tails and must not inherit that earlier artifact status.

Scheduling RE now resolves the native game-thread startup and synchronous
cooperative task dispatch. The same-thread fiber model makes TLS/thread
ownership insufficient for interval exclusion. Exact allocator/resource
callbacks, pre-link creator affinity and full mutation coverage remain open;
no borrowed lock or safe check-to-dispatch boundary is approved. The next
observational work collects active/deferred occupancy and raw record IDs while
keeping pendingExclusionComplete=false. All original ten-cycle/five-room/
natural-personal/remote population and death acceptance stays open.

### Read-only raw active/deferred occupancy receipt (2026-10-03)

`native_enemy_census` now adds a separate `rawOccupancy` receipt after its
existing census and geometry reads. It walks both active and deferred roots,
retains every node before type/status/mask filtering, decodes native handles,
and reads the u16 record ID at each readable nonnull record pointer. Null
records, unreadable IDs and observed ID zero remain distinct. Checked roots,
bucket bases, links and metadata are read again; changes, missing reads,
cycles, cross-list membership, tail mismatches and the combined256-node cap
leave explicit partial evidence. Work shares the original per-peer deadline.

The CLI keys peek values by address only. Different types at one address would
overwrite one another during JSON decoding, so the raw collector rejects
such conflicts across its accumulated field set before issuing the affected
read. Same-address/same-type labels remain supported. Independent review first
held this defect, then accepted the scoped correction; both reports are
retained. Root executed all27 geometry/raw-occupancy controls successfully,
including byte-backed collision cases and unchanged legacy output/read order.
Evidence: `build/rig/raw-occupancy-controls-20261003-01/` and
`build/rig/raw-occupancy-independent-review-fixed-20261003.md`.

At that offline checkpoint this source was newer than the frozen
`20261003-221650` inputs and unexecuted. Those older saved snapshots lack
deferred roots and cannot supply complete new occupancy receipts. The separate
native attempt below uses the new reader. A checked sample is not atomic and cannot
exclude ABA, foreign creation or pre-link allocations. `pendingExclusionComplete`,
`controllerIncarnationQualified` and `globalControllerIdCoverageComplete`
remain false. No population/readiness/hash result is promoted and no actor
creation is enabled.

### Dedicated raw lifecycle bookends: schema 2, offline only (2026-10-03)

The later schema2 source brackets the raw interval with its own checked
native headers and completed-load/arrival logs. Both samples retain load and
transition serials, epoch, the full six-field location, gameplay state and
handle regions. Each header's active head/tail and all64 regions must also
agree with that phase's raw roots/buckets. Missing, unsafe, changed or
contradictory samples keep the observations and prevent completeness.
The two added79-field header peeks share the original census deadline;
there is no retry, game write or change to the earlier measurement schedule.

Initial32 controls passed, but independent review reproduced stable empty
header roots against a stable two-node raw list, and contradictory bucket0,
both incorrectly marked complete. The correction adds explicit per-phase
joins and missing/mismatch reasons. Root's34 focused controls and an
independent34-test run pass; replay of both original defects now retains the
nodes and marks the receipt partial. Unused bucket63, either/both phase
conflicts, missing fields, lifecycle drift and deadline exhaustion are covered.
The initial HOLD and corrected PASS evidence remain separate:
`build/rig/raw-occupancy-lifecycle-v2-controls-20261003-01/`,
`build/rig/raw-occupancy-lifecycle-v2-controls-20261003-02/`, and
`build/rig/raw-occupancy-lifecycle-v2-independent-review-fixed-20261003-01.md`.

This is post-run source, not an executed schema2 capture. The frozen native
attempt below remains schema1 and FAIL. Matching endpoint observations are
non-atomic and do not exclude ABA, pending creation, incarnation reuse or
global record-ID aliases. All authority flags remain false.

### Native raw occupancy: selected records absent from checked lists (2026-10-03)

The140-input attempt `20261003-230406` executed the reviewed reader with the
unchanged282-step fixture. It failed the original population/hash gate111
after246.7s, zero complete cycles. Friend1's original game/runtime rejoined
and completed one fresh native load; both post-load censuses remained5/0/5,
with the survivors retaining original IDs1-5/object302/HP17/max20 and progress.

The six census files retain36 raw schema1 receipts and500 node observations.
Seventeen receipts have equal checked fields;19 remain partial for34
flags120 changes. All644 raw read-work calls completed. All108 record-ID
observations on status-null nodes survive classification; null records remain
distinct from ID zero or missing reads. Deferred roots are explicitly zero in
every sample; nonempty deferred traversal was not exercised natively.

Both Friend1 after-rejoin raw samples are complete:10 active nodes, zero
deferred nodes, six record IDs3,5,4,9,10,171 and four null record pointers.
Neither sampled list matches the adjacent selected controller, record span or
IDs11,12,13,14,18. This narrows hidden listed-actor alternatives at those
samples. Raw collection follows the older census/cause lifecycle bookends and
does not independently bracket its entire interval with load/transition/tuple
reads. These are contextual checked lists, not a new lifecycle or exclusion
token. The subsequent schema2 source has passed offline controls and review
with dedicated raw lifecycle bookends; it was not used by this attempt.

The new native trace separately has56 complete single returned gateAL0
captures joined to392 BOXAL0 returns, plus eight no-call/nonreturned captures
that remain incomplete. Three actual point values occur across the complete
window. The65324..65954 span is630 controller updates, not milliseconds;
this remains callwise evidence rather than original-phase or creation authority.

Independent raw, gate and population/lifecycle reviews accepted the frozen
evidence. The94 run files,140 inputs and three native logs are preserved in
`build/rig/raw-occupancy-native-package-20261003-01/`. Root's04:11:37UTC audit
found all eight owned processes absent, empty rig/lock/index, unchangedHEAD,
all140 inputs unchanged during the run and all four save hashes unchanged.
An OS module query happened after cleanup, so this attempt has no captured
loaded-module path/hash association; the earlier run's proof is not reused.

First automatic report and aggregate collection are partial:12 received peer
artifacts have checked integrity, but Host's bounded native tail is interrupted
by source-log changes. The three fresh original-roster PNG receipts precede
the pause; there are no final post-rejoin views. Active1/cadence2 witnesses
are suppressed triggers, not additional collected bundles.

Static callback review maps the allocator and model virtual targets, including
a real allocator semaphore that releases before construction. Object-entry
type4, BAR type4 and model discriminator3 are distinct; no current target
binding or full creator/fiber/pending closure follows. See
`build/rig/surviving-enrollment-resource-callback-closure-20261003.md`.
Native resource/allocator binding, controller incarnation, global-ID coverage,
serialized check-to-dispatch and fresh full-set/partial/death semantics remain
prerequisites. All original recovery and combat acceptance gates remain open.

### Allocator/model pointer-binding helper: offline review (2026-10-04)

The separate `capture_native_resource_bindings` helper has passed14 focused
controls and19 independent adversarial cases. It samples the allocator root,
recognized vtable/slot, and qualified object302 actors' first BAR type4 entry,
fresh handle bucket, signed model kind and exact SKL/BG/Multi callback slot.
It retains typed little-endian bytes and rechecks the original addresses,
local lifecycle and parent raw scope. Mixed-type aliases, missing reads,
unsupported shapes, drift and the16-actor/64-BAR caps keep partial evidence.
All reads use the supplied census deadline; no retry or unknown virtual call
occurs. A sampled pointer/slot does not prove an earlier executed callback.

At acceptance the helper had zero production calls, with every existing
production AST unchanged after removing it. Exact source/test snapshots and
root controls are in `build/rig/native-resource-binding-controls-20261004-01/`;
the independent report is
`build/rig/native-resource-binding-independent-review-20261003-01.md`.
The subsequent default-off integration passes18 focused controls and a
separate independent review. `native_enemy_census` accepts the strict boolean
`resourceBindings`; true appends the receipt after raw occupancy using the
same remaining per-peer deadline. Omitted/false preserves the original reader
schedule, and a binding-only failure retains partial evidence without changing
the parent verdict. The separate282-step
`net_reconnect_shadows_resource_bindings_trace.json` fixture enables only its
first six census steps (indices42,64,76,93,104,108); every other fixture field
matches the original. See
`build/rig/native-resource-binding-integration-independent-review-20261004-01.md`.
No native binding capture, callback closure, incarnation, pending exclusion or
creation authority has been established.

A separate desktop wrapper candidate preserves the original runner while
polling fresh rig-owned PID/FILETIME identities for loaded-module path and
disk-copy hash observations. Its PowerShell syntax and embedded C# compilation
pass independent review. A subsequent derivative's read-only Windows API probe
passed against its own PowerShell process; game ownership/ancestry/module
observation and gameplay overhead remain unexecuted. The explicit
future-seal placeholder blocks launch. Missed launcher ancestry, errors or
identity drift keep association unavailable; disk hash is not an in-memory
code digest. See `build/rig/binding-module-wrapper-independent-review-20261004-01.md`
and `build/rig/resource-bindings-wrapper-review-20261004-01.md`.

### Native resource-binding/lifecycle diagnostic (2026-10-04)

The separate `20261004-003916_net_reconnect_shadows_resource_bindings_trace_1`
run executed the accepted integration and schema2 raw lifecycle reader.
It remains **FAIL at step111 /255.0s**, with111 preceding passes and zero
complete cycles. Friend1 kept its original game/runtime, retired connection2,
rejoined as4 and completed load4/transition3. Both post-rejoin native censuses
still report5/0/5; the surviving five object302 actors retain HP17/max20.
Full progress8108bytes precedes load; equal personal hash124AA108 is checked
at its application boundary, not throughout the natural reload.

All36 dedicated raw lifecycle samples and72 active-root/all64-bucket joins
are stable/complete. Raw occupancy remains13 complete/23 partial from33
flags120 changes; lifecycle stability does not erase those partials. The500
retained node observations include two complete Friend1 post-load lists with
ten active nodes, empty deferred roots and IDs3/5/4/9/10/171. Its own64 current
definition blocks identify selected Shadow IDs11/12/13/14/18; neither raw
sample matches the selected controller, exact record pointers or ID/object
tuples. No later refill, pre-link exclusion or incarnation is established.

The36 resource receipts retain allocator root/vtable/slot bookends;13 qualify
complete and23 retain incomplete parent raw scope. Four receipts attempt three
actors each: ten complete ModelSKL observations and two partial from additional
flags120 drift. All observed graphs select first BAR type4, signed model kind3,
vtable M+5B3E00 and slot M+1C21E0. No nonempty actor set has complete binding
coverage; eight coverage-complete receipts explicitly have zero actors.
Friend1 after rejoin supplies allocator evidence only. Sampled targets do not
prove callback execution or safe creation; all authority flags stay false.

The wrapper records current owned PID/FILETIME/launcher/module observations
and matching staged-DLL disk hashes for all three peers, independently reviewed
against the exact final wrapper/launch/ownership receipts. Final native
Creation/Alive and second-lock results are attested by that wrapper's success
branch rather than separately serialized raw calls. This run has54 complete
returned gateAL0/seven-BOX-AL0 captures (378 BOX returns) and ten held/no-call
ticks; held default zeros are not returns. In-memory code digests, original
phase and eligibility remain unavailable. See
`build/rig/resource-bindings-native-module-gate-review-20261004-01.md`.
First and aggregate automatic collection are partial: all12 peer byte hashes
and relay hash verify, but Friend2's bounded native tail is interrupted and
one cadence witness is suppressed. All three1920x1080 PNGs precede the pause,
with the original roster; they are not final post-rejoin views.

Root's05:43:40UTC terminal audit found all eight owned processes absent,
rig/lock/index empty, unchanged HEAD,165 inputs unchanged during the run and
all four protected save hashes unchanged. The94 run files, three whole native
logs and exact inputs are frozen in
`build/rig/resource-bindings-native-package-20261004-01/`. Independent binding
and lifecycle audits are `resource-bindings-native-binding-audit-20261004-01.md`
and `resource-bindings-native-lifecycle-audit-20261004-01.md` under `build/rig/`.
Earlier native packages keep their original schemas and verdicts. All recovery,
combat, ten-cycle/five-room/natural-personal/physical-remote gates remain open.

### Full records/original-phase native attempt: first-rejoin FAIL (2026-10-04)

`20261004-020622_net_reconnect_shadows_controller_records_phase_trace_1`
executes both new observations on the unchanged 282-step parent. It fails at
step 111 after 267.1s: Friend1 retires connection 2 and rejoins as 4, with local
load/transition 3/2 to 4/3, but both post-rejoin censuses remain 5/0/5. Zero cycles
complete; steps 112-281 do not execute. Source is uncommitted and creation stays
disabled. Successful diagnostic collection does not satisfy population recovery.

All 36 samples freshly retain ten supported ordinary definitions and all 26
full 64-byte records, with matching local before/after 44-byte headers and record
bytes. The 936 repeated record observations cover 26 distinct record contents;
they are not 936 actors. Complete ordered record arrays are byte-identical across
all peers/checkpoints. Header 30 alone changes its activation byte +0xE across
checkpoints, so full-header hashing is not an immutable identity. The four
same-group type 2 headers remain distinct 30/31/32/33; group alone is ambiguous.

Combined table/list/lifecycle coverage is 8/36: six initial empty observations
and both empty Friend1 post-rejoin observations. This closes the prior 21-full-
record/15-ID gap in those sampled ordinary scopes. The other 28 joins remain
partial for flags120 drift and parent comparisons; raw-v2 itself is 16 complete/
20 partial with 35 flags changes. Each failing Friend1 active list has ten nodes,
six nonnull references with IDs 3/4/5/9/10/171, four null records and zero deferred
nodes; none matches the five selected Shadow records 11/12/13/14/18. Those five
records nevertheless exist in the full definition array. No sampled raw-u16 ID
collision, zero/high-bit ID or nonempty array alias was observed. This is neither
an executed native lookup nor global/incarnation/pending/atomic/creation proof.

All three peers report the new phase observer configured. Only Friend1 emits
selected load 4/transition 3 rows: 64 sequences 44-107, update counters 73155-73785.
Fifty-five returned calls match the serialized original-update thread bracket,
source 7 and actual update/gate caller RVAs 3A5063/3FF0E5, with 55 gate AL0 and 385
BOX AL0 results. Nine first-lease holds have zero calls/returns and unavailable
default boundaries. Every BOX input repeats one point (-166.228271484375,-1,
403.697265625,1); this is not a varying-point eligibility scan. Decision-time
configuration/coverage atomics remain unrecorded; TLS cannot prove fiber
continuity. All fiber/eligibility/incarnation/pending/creation authority remains
false. Ninety-seven generic trace rows with multiword reasons remain tokenizer
findings outside selected joins; no malformed selected join was discarded.

Current owned PID/FILETIME/launcher-to-this-runner joins qualify all three loaded
DLL paths and staged-copy disk hashes, with no in-memory digest. Automatic
collection remains partial for both reports 1/3; active suppression 1 is retained.
The two reports' 24 artifacts include one interrupted tail each. Six fresh
1920x1080 PNGs are retained as checkpoint images, with no final-state claim.
The terminal desktop audit records all 218 inputs unchanged during the attempt,
four protected saves unchanged, all eight owned processes absent, empty rig/
index and unchanged HEAD. Subsequent root documentation edits do not rewrite
that during-run claim or the frozen input copies.

Frozen package: `build/rig/controller-records-phase-native-package-20261004-01/`.
Record byte/association audit: `controller-records-native-id-audit-20261004-01.md`;
phase raw audit: `controller-records-phase-native-phase-audit-20261004-01/`;
independent native review: `controller-records-phase-native-independent-review-20261004-01.md`.
All original ten-cycle/five-room/natural-personal/remote gates remain open.

The bounded branch deduction and independent review confirm that every genuine
BOX AL0 takes `3FF142 -> 3FF105`, bypassing the ordinary function's sole
`3FE320` call. The seventh node's zero next handle resolves to null and ends
that loop. This is conditional on the checked caller, saved code and sampled
list binding, with no continuous ABA/fiber or whole-program exclusion claim.
The current host point therefore cannot recreate this pack through that ordinary
path. Internal first-failing comparison PCs remain saved-code/operand deductions,
not instrumented execution: six boxes reject on X before Y, one on upper Y.
No geometry, flag or coordinate override follows. Evidence and review are
`build/rig/native-ordinary-box-negative-path-deduction-20261004-01.md` and
`build/rig/native-ordinary-box-negative-path-independent-review-20261004-01.md`.

The opaque execution-context marker design passed independent review but stays
unimplemented; no earlier sample is retroactively qualified. At that checkpoint
portable exact record-content comparison and a bounded native catalog reader
were the next components; their later implementation/results are documented
above. Content correspondence alone remains distinct from lifetime/pending
facts. The stricter added-call exclusion and branch-installation route is parked.

### Offline original-update bracket observer (2026-10-04)

`KH2COOP_SPAWN_ORIGINAL_PHASE_OBSERVE=1` enables an additive diagnostic only
when the existing trace, geometry and event-gate observers are configured and
installed. It adds no native hook. All seven original update call sites retain
their arguments and order; explicit source/hold tags, actual hook callers and
POD invocation tokens distinguish lease work, original entry, normal return and
unwind. `[spawnenrollment] original-phase` and its gate boundary rows share the
existing selected event sequence. Unavailable boundary rows and held default
AL0 bytes are not executed/returned calls. Existing gate/BOX completeness and
production decisions remain unchanged.

The actual production-boundary harness passed232 controls in Release and232
in Windows MSVC ASan; independent review verified exact candidate hashes,
seven compiled original calls, unchanged final lease-to-call arguments with no
added calls, SEH restoration, and the serializer's legacy projection. The
two-fiber control demonstrates that `threadPhaseConsistent` can be true across
an unrelated fiber call: `fiberContinuityProven` and `originalPhaseEligibility`
remain false. Positive caller tests use controlled expected-RVA seams; no native
caller execution is claimed. The production DLL builds and links. The subsequent020622 attempt above
records the new rows in game; broader timing/stability remains untested.

The disabled hook dispatch retains its old source path, while `RunUpdate` gains
SEH/frame/TLS overhead even with the option off. Finalization-time configuration
and coverage atomics are checked by the submitted source but are not individually
serialized; a later raw-row audit cannot fully replay those decision-time values.
None of this establishes eligibility, controller incarnation, pending exclusion
or creation authority. Candidate/checks are frozen under
`build/rig/native-original-phase-implementation-20261004-01/`; independent review
is `build/rig/native-original-phase-independent-review-20261004-01.md`.
The preceding003916 native package and verdict remain unchanged.

### Offline full controller-record inventory (2026-10-04)

A `native_enemy_census` step can set the strictly Boolean, default-false
`controllerIdCoverage` option. When enabled, collection follows the existing
raw schema2 and optional resource-binding diagnostics under their same remaining
deadline, with no retry or new timeout. Disabled steps add no native reads.
The separate `net_reconnect_shadows_controller_records_phase_trace.json` fixture
adds this option only at six census steps (42,64,76,93,104,108); all282 original
steps and acceptance conditions remain intact. The launch wrapper separately
enables the original-update phase observer described above.

The reader freshly inventories every inline ordinary-controller entry and
supported definition's full44-byte header and declared64-byte records. It
retains script/unknown types, ID0/high-bit IDs, duplicates, aliases, failed words,
omitted ranges and before/after changes. Table byte coverage, active/deferred
reference joins and combined sampled coverage are separate results. Both raw
lists, all64 buckets, local lifecycle phases and parent/cause projections must
agree before combined coverage can be complete. A partial parent does not erase
successful table bytes; no-conflict stays unknown without complete sampled
coverage unless an actual duplicate/alias is already observed.

Limits are64 table entries,256 raw nodes,256 records per definition and1024
logical record slots. Reads use the existing checked128-field adapter. Twenty-one
focused controls passed on the final source; independent review added six
adversaries and verified default-off behavior and exact fixture projection.
The unchanged geometry/resource closures reuse their34/18 prior regression
results with the source-hash qualification in the producer receipt. The subsequent020622 attempt above records complete native table/record
collection, partial stronger joins and measured durations; broader timing
behavior remains untested. Pointer agreement cannot establish atomicity or incarnation; all
`globalControllerIdCoverageComplete`, `controllerIncarnationQualified`,
`pendingExclusionComplete`, `atomic`, `creationAuthority` and
`executedCallObserved` flags stay false. This is diagnostic collection, with no
population/readiness decision or creation change. Receipts are
`build/rig/native-controller-id-coverage-root-acceptance-20261004-01.json` and
`build/rig/native-controller-id-coverage-implementation-independent-review-20261004-01.md`.

### Current secondary equipment/map bindings (2026-10-04)

`native_enemy_census` accepts the strictly Boolean, default-false
`secondaryBindings` option. It captures the three bounded object tables, first
matching went/item BAR descriptors and relocated roots, current slot0/1
equipment, ordered item first-match and selected raw mapping cells. Every new
peek checks PID/module identity and uses the census's remaining30-second
deadline and existing128-field batches. Omitted/false preserves the reader
schedule. Caps, malformed counts, missing reads and drift retain partial data;
original disk formats are not substituted for current native layouts.

Consumed handle buckets must agree with both final readback and the independent
lifecycle endpoints. A known parent-scope conflict denies combined stability;
an unavailable parent remains explicitly local-only. The initial independent
HOLD exposed both omissions. The fixed source passed128 affected controls and
the exact original adversarial replays in a separate independent review:
[fixed review](../build/rig/native-secondary-binding-collector-independent-review-20261004-01/fixed/review.md).
All creation/exclusion/lifetime authority flags remain false. These sampled
current selections do not qualify native object bsearch, future equipment or
arbitrary replacement arguments.

`net_secondary_bindings_current_shadows.json` copies the canonical courtyard
fixture's first12 setup steps, then makes one opt-in three-peer census and
requires complete stable bindings joined to its parent scope. Its table
collector is read-only; the existing boot/warp/runtime and player-protection
steps retain their normal behavior. The14-step fixture passed in153.4s:
[`20261004-112324`](../build/scenarios/20261004-112324_net_secondary_bindings_current_shadows_1/report.md).
Independent saved-byte replay verifies all297 secondary batches and their
PID/module identities. Each peer has1900 primary object rows, two observed null
roots,81 nonzero-group rows,87 selected records and174 current slot results.
There are91 nonzero sampled outputs and83 explicit early-zero outcomes per
peer; no sampled raw output is302. Live went is3828 bytes, live item is15272
bytes with535 ordered records. This qualifies current selected cells, not an
all957-word went scan or future replacement-argument domain.

The complete parent censuses contain zero living enemies on all peers. The run
ended before activation and supplies no five-Shadow presence or recovery
acceptance. All160 sealed inputs and four protected saves match; the runner
exited0 and released its owned instances and lock. No OS-loaded DLL association
or in-memory code digest was captured. See the [independent native review](../build/rig/native-secondary-bindings-native-independent-review-20261004-01/review.md)
for exact scope, branch distinctions and preserved authority limits.

### Offline surviving-population assessment (2026-10-03)

This assessment was completed during James's Rivals desktop hold, before the
endpoint attempt above. No gameplay, live census or process operation was part
of this saved-data assessment. The later run used a fresh preflight/input seal;
it does not retroactively extend these historical observations.

The frozen setup logs contain15 fixed-record, nonnull native wrapper returns
with complete AllocationPassed factory receipts. All15 immediate actor reads
were unavailable; two later complete native censuses per actor supply30 current
controller/record/binding correlations. `ReadTraceActor` can short-circuit before
reading controller, record, type or HP, so logged default zeros are unavailable
fields, not proof of native zero, death or failed allocation. The audit's
`readyAtWrapperReturn=0` counts qualified observations; it does not prove every
actor was actually unready. Later same-address matches do not establish
allocation continuity across the gap. Original one-shot trace events remain
unchanged; no delayed readiness confirmation has been implemented.

A fresh complete living set could authorize a new snapshot-defined enrollment
instead of replaying historical creation events. It still needs an exact unique
portable native-record map and current controller incarnations, complete local
record/cache/occupancy inputs and exclusion of pending actors, aliases and
unsupported history. Current manifest `spawnIndex` is observation order and
must not be treated as native record index.

Follow-up actual-data comparison establishes all thirty full64-byte record
receipts across the six setup peer observations are identical without masking
or normalization: the five-record320-byte array hashes to
`447e6d0f4c6e5ad652f96955d9d3db86373b7675e112fac39303429ddfd660df`.
Actual actor/provenance/log joins map IDs1-5 to native IDs11,12,13,14,18.
Full raw headers/controller bytes are unavailable, including initial-delay
header+1C. Equal local pointer values give no relocation-diversity test, and
sequential samples give no atomic capture/lifetime proof. The empty failing
load7 still lacks its own full record array. See the static-record-map report
and its actual-data receipt/producer in `build/rig/`.

Saved-PE review proves the exact type2 branch of3FE320 ignores the region
argument, activates the header and calls the full3FE6F0 native emitter. AL1 is
only the branch result. Actual five-record delay8 causes a positive cooldown
after the first successful emission; dispatcher-only repeats cannot complete
the pack. Retaining the original3FF000 update may supply native cooldown
progression over bounded ticks, but direct dispatch still omits outer event/
disabled gates and accepted-region bit3. A normal update return is not proof
those gates passed. No flag/count/cache patch or predicate override is approved
as a substitute for that proof.

The exact saved3ABC80 event predicate is now independently resolved: it is
read-only, takes no arguments and returns Boolean AL. Nonzero suppresses work;
only zero reaches cooldown/region testing. Its false condition is native flags
2A11400&0x102==0, qword2A11478==0 and dword2A10500==0. This static contract does
not establish those values or actual gate passage on a target native tick.

Accepted-region bit3 is transient geometry state, set before dispatch rather
than as proof of creation. Confirmed consumers include NPC inactivity/removal
logic and a script Boolean query. Raw RTTI and handler bindings distinguish that
NPC path from the current Shadow/ZAKO handler; no Shadow-loss cause is proved.
An outside-region living-set contract may intentionally retain bit3clear while
preserving truthful geometry. Its supported actor/script/lifetime behavior
needs proof; setting bit3 to imitate an accepted path is not justified. See the
event-gate-contract and accepted-flag-consumers reports in `build/rig/`.

The lead accepted this as a stronger candidate for a scoped proof, with fresh
enrollment/reinitialization and pending-actor witnesses, actual eligibility/flag
semantics, bounded partial outcomes, post-cut authority/death handling and later
native readiness/death bookkeeping still unresolved. Partial creation is not
rollback and must not trigger culling, resets or replacement retries. Original
five IDs/HP17/max20, complete no-extra population, shared progress, zero-death,
full ten-cycle, five-room, natural/personal and protected-save gates remain.
Empty rooms retain their separate complete-native-empty readiness contract.

Evidence: `build/rig/surviving-bootstrap-return-readiness-audit-20261003.json`,
the authority, native-enrollment, acceptance-controls and full-batch-review
reports in `build/rig/`, and the frozen saved PE. This assessment changes no
C++/DLL/helper/fixture and establishes no production fix or native acceptance.

## Same-room absolute HP ordering (2026-10-03)

Protocol **7** supplies a nonzero 64-bit sequence from the existing checked DLL
HP publication point. Relay and client admission reject older same-room values;
the client checks before raw bridge forwarding, preserving reliability through
conditioning. The DLL also keeps an independent floor. Reliable equal-sequence
cache replay remains supported. [HP_ORDERING.md](HP_ORDERING.md) describes the
namespace/reset rules, source exhaustion and remaining full-resync boundary.

The frozen protocol 6 baseline had three reproduced failures: confirmed HP 72
became 41 after an older packet, and targeted/late-join cache replay both retained
41. Its real loopback receipts and source are in
`build/rig/hp_order_failed_baseline_receipt_20261003.json` and
`enemy_hp_order_baseline_20261003`. Native and remote acceptance remain open.

The final protocol 7 receipt is `build/rig/hp_order_offline_receipt_20261003.json`:
Release and Windows ASan passed 1,560 checks each, including 89 actual HP
ordering controls and 118 production native consumer/producer controls over
owned memory. Portable ASan+UBSan passed 1,116; Windows-only controls are excluded.
Nine prepared scenario linkage cases and three actual PID-0 runtime smoke checks
passed, with no sanitizer findings. These establish offline behavior, not native
gameplay or physical multi-PC acceptance.

## Automatic all-peer desync reports: offline candidate (2026-10-03)

The then-current protocol **8** / AvatarBridge **2** / WorldBridge **10** candidate
automatically collects bounded logs, metadata and screenshot bytes from the
frozen original roster when the relay emits its persistent-mismatch notice.
Diagnostics use a separate reliable channel and preserve partial/interrupted
results, checked digests, capture attempt witnesses and suppressed-trigger
evidence. Scenario launches register exact inject logs and their own spool
roots; reports link complete and incomplete aggregate artifacts independently
of gameplay PASS. [DESYNC_REPORTS.md](DESYNC_REPORTS.md) specifies limits,
paths, versioning and controls. The frozen protocol 6 receipt is
`build/rig/desync_offline_receipt_20261003.json`. A separate current-protocol
native report from `20261003-135207` supplied intact 1920x1080 renderer captures
from all three original local peers. It correctly remains partial because one
inject log changed during capture. Saved artifact digests, PNG validation and
the bounded review are in `resync_shadows_auto_report_review_20261003.md`.
A later separate report 1 from `20261003-140717` is complete for all original
three peers with matching bytes/digests, stable bounded logs and successful
native capture witnesses. Its run-level index remains partial for three
suppressed triggers. Final explicit captures have path/PASS records but lack
stored request/done receipts; the earlier automatic witnesses cannot replace
them. See `resync_population_captures_review_20261003.md`. Physical remote
contributions and full gameplay recovery remain open; all VUH-1508 acceptance
criteria stay open.

## Relay world cache epoch validation (2026-10-03)

The frozen protocol **5** / AvatarBridge **2** / WorldBridge **8** slice rejected
old, future or zero-epoch holds/manifests/HP/deaths before forwarding or changing
the relay cache. Each must match the current nonzero room epoch; HP/death also
require that room's manifest epoch. All six cached world types require exact
outer framing and complete payload decoding before mutation. Previously,
delayed HP and deaths could be replayed under the current manifest's epoch;
a stale manifest or hold could replace current cached state.

Valid current-room append, pre-append unknown HP IDs and duplicate HP
last-entry-wins semantics remain. An initial HP sample arriving before its
current manifest is discarded; subsequent periodic samples supply fresh HP.
Room-transition monotonicity and progress-version admission remain unchanged.
This patch does not fence old **same-epoch** HP within a forced-resync transaction.

The frozen original relay reproduced **33 failures** in the 54 new controls,
including actual targeted-cache-resend and late-join reconstruction witnesses;
its 148 prior checks passed. The fixed relay passed **202 world, 127 recovery
and 61 bridge checks** each in Release and Windows ASan (390 per configuration).
Portable ASan+UBSan passed **202 world and 127 recovery checks** (329), with leak
detection enabled. Builds and test executable/log hashes are in
`build/rig/world_cache_epoch_offline_receipt_20261003.json`; baseline failures
and the initial source-review correction are preserved. The first candidate
missed an outer frame-length check; independent review caught it before runs,
and the final source includes that guard. Both reviewers accepted the fix.
Unreliable HP callback absence alone is not proof that nothing was transmitted;
reliable cache-reconstruction assertions independently prove the cache invariant.

Native forced resync remains open. At this frozen protocol 5 slice, the host request only resent cache:
a same-epoch transition is ignored, identical progress does not repair live SAVE,
and fresh unbound actors intentionally cannot inherit historical host deaths.
Full recovery needs authenticated transaction/delivery fencing in both
directions, complete bootstrap, a checked new native load and convergence ACK,
and independently verified dead-spawn identity without removing the anti-refill
guard. The protocol 8 implementation now provides the transaction boundary
([FORCED_RESYNC.md](FORCED_RESYNC.md)); it still reports dead-bootstrap ambiguity
unavailable. A reset/reload or empty-room pass cannot substitute for nonempty recovery.

Automatic reports were unimplemented at this frozen slice; the later protocol 6
candidate is described above. Separate frozen audits are in
`build/rig/forced_resync_relay_audit_20261003.md`,
`forced_resync_native_audit_20261003.md` and `desync_bundle_audit_20261003.md`.
No source/design audit certifies native or remote behavior. All VUH-1508 criteria
remain open. No gameplay, installation, save/protection changes, staging or commits
occurred in this offline slice.

## Dropped-friend recovery: offline candidate (2026-10-03)

This frozen offline slice used protocol **5**, AvatarBridge **2** and
WorldBridge **8**. The v5 wire shapes are unchanged from v4; typed closure and
opaque world-incarnation semantics require the version gate. Each accepted
Player lifetime mints an OS-random 128-bit session token. The configured session
name is a label. Friend rejoin pins the original token, host peer/connection
and exact local peer/slot; repeated connection counters after relay restart
cannot identify the old world. Changed/missing host or incarnation is terminal.

RuntimeMain now uses production ClientRecovery before its attachment wait.
There is one outstanding attempt, separate four-second transport and verified
roster deadlines, five retries after 1/2/4/8/8-second delays, and a 60-second
episode limit. Only an already admitted Friend1/Friend2 retries transient
transport/idle/handshake/occupancy/full-lobby loss. Initial failure, Player loss,
explicit shutdown, incompatible/rejected identity, host departure and relay stop
are terminal. HelloReject classification precedes deadline handling. Ten
uninterrupted seconds of admitted membership reset the budget; this is network
stability, not native bootstrap readiness. Heartbeat/clock activity continues
while unattached or awaiting admission, with authoritative traffic quarantined.

Every local/remote retirement clears runtime world/avatar/mailbox state and
advances the mapping generation. The DLL immediately retires cached host HP,
deaths, progress and queued warp on a header change, even when the matching FIFO
reset is deferred behind a full old ring. Only the current reset marker arms
world ingestion. HP/death/progress/warp leaves recheck their captured generation;
the first new full progress snapshot survives the ordered reset. A progress
retirement during application stops further writes and fails readiness; an
already written prefix is not rolled back. These repeated boundaries are not an
atomic transaction with the runtime. Outgoing v8 records retain the producer's
generation, so queued old claims/ACKs/hashes cannot be relabeled after rejoin.

Focused controls use the actual coordinator and ENet clients/relay. Only Friend1
expires while the host and Friend2 survive; reconnect receives the existing
full merged progress, room/hold/manifest/HP/death cache without any post-drop
host resend or cached activation. That relay progress fixture is three bytes,
proving merge/order rather than native full-SAVE readiness. Separate owned-memory
controls include actual EnemySync, ProgressSync and Warp over a complete allowed
progress fixture and synthetic native callbacks. A scripted ENet endpoint emits
real world/avatar/actor/enemy/event witness packets, with ordered ClockPong
barriers and positive callback controls, to test pre-admission and rejection
quarantine. Pin mismatch/missing/truncated roster closes once before publication;
HelloReject1 remains terminal even when the later raw disconnect code is zero.

Release and Windows ASan each passed **127 recovery, 146 avatar, 148 world,
85 simulation, 61 bridge and 99 production-consumer checks** (666 fresh checks
per configuration). The unchanged 125 trace and 166 policy controls were reused
only after matching their executable and passing-log hashes: 957 checks per
Windows configuration in the receipt. Runtime/relay/tool and both DLL builds
passed. Portable ASan+UBSan passed 127 recovery, 148 world and 85 simulation
controls (360), with leak detection enabled and no sanitizer finding. Windows
native SEH modules have ASan evidence and no UBSan proof; AvatarBridge controls
are Windows-only and are excluded from the portable subset.

Portable leak detection exposed a 163-byte ENet packet leak during teardown.
Relay/client send paths now handle allocation failure and destroy caller-owned
packets on failed submission, retaining ENet ownership on success. The original
leak log and clean rerun are retained. Avatar send timing, host-expiry priming and
the repeated-Hello fixture now establish actual admission before their witness
send; all stale-packet, expiry and refusal assertions remain. The evidence
wrapper's incorrect assumption about trace/policy summary format was corrected,
then completed passes were reused by exact hashes. Failed fixture/build and
wrapper logs remain alongside the passing evidence.

Frozen reconnect-slice evidence paths are `build/rig/client_rejoin_offline_receipt_20261003.json`,
`client_rejoin_control_results_20261003.json`,
`client_rejoin_portable_results_20261003.json` and the per-suite/review artifacts.
No candidate is installed live. Native gameplay rejoin, puppet restoration,
full room/program/HP/progress convergence, forced resync, native/remote desync bundles
and remote-network recovery remain open on VUH-1508. Historical v3 native and
v4 offline receipts do not certify this candidate. All acceptance criteria stay
open; no gameplay, save writes, protection changes, staging or commits occurred.

## Avatar connection replacement and cached-pose authority (2026-10-03)

This prior offline slice used protocol **4**, AvatarBridge **2** and
WorldBridge **7**. Its frozen receipt is historical source/build evidence;
the subsequent dropped-friend slice used v5/2/8; its evidence is recorded above.
Historical v3 native receipts below do not certify either candidate.
The 76-byte outbound AvatarState/recording layout and v3 HitClaim shape remain
unchanged; AvatarRelay is now an exact 84-byte connection-tagged envelope.

The frozen pre-change header repro refuted sequence-reset starvation: sequence
1 with a newer timestamp was accepted. It did reproduce retained interpolation
(old position 10, replacement 200, midpoint 105), retained poses on unnotified
roster removal, and timestamp rejection for equal-time replacements. That was
actual header execution over synthetic samples, not a native reconnect failure.

The relay now stamps the sender's full connection ID and slot. NetworkClient
requires a valid host/self roster and exact current owner ID before callback or
loss accounting; duplicate/reordered sequences are rejected within a connection.
Removal/replacement resets that stream; host/self/session/transport changes
reset all. Malformed or invalid SessionState delivers an empty callback, so
runtime and DLL caches retire immediately. Conditioned queues, clock state and
loss windows cannot carry across a transport reconnect.

AvatarSync retains the admitted identity with each buffer and sampled target.
Runtime publishes it with its receiver generation/local slot/self/host binding.
The DLL checks cached provenance on admission and every drive use, including
unchanged/torn bridge reads. Explicit Standalone poses require a positively Off,
never-armed bridge; unknown or disconnected networking never means Off.
Previous drive state is tracked independently for release. Companion restoration
requires fresh repeated native friend-slot agreement; clone restoration reuses
the existing complete canonical census. Both require checked bound/current
metadata, fresh canonical-player exclusion and immediate lifecycle agreement.
Uncertain restoration is withheld; native teardown forgets old pointers.

Release and Windows ASan passed **144 avatar, 148 world, 85 fake-simulation,
54 bridge, 71 production consumer/context, 125 trace and 166 damage-policy
checks each**. Runtime/relay/avatarctl and both DLL builds passed. The avatar
controls exercise real ENet receiver admission with synthetic relay packets,
production relay slot replacement, lower-time/sequence-1 admission, old-packet
rejection, malformed roster invalidation, cached provenance, conditioned
reconnect and v3 rejection. The 71 consumer controls include 17 new authority
capture/current-membership guards over owned memory and mocked transport.
The initial fake-simulation failure was its hardcoded `server=3` rejection
expectation; it now uses PROTOCOL_VERSION and retains the original rejection
assertions. The failure log is preserved. No sanitizer finding was reported.

Evidence is in `build/rig/avatar_reconnect_offline_receipt_20261003.json`,
`avatar_reconnect_control_results_20261003.json`, per-suite logs and two reviews.
These checks do not execute production EntityHook driving/restoration or native
KH2 callbacks. Connection provenance is not native actor incarnation. Same-room
source freshness still uses existing world/room/timestamp semantics; no full
room program/epoch tag was added. Explicit NetworkClient::disconnect retains
its no-callback contract, so callers must retire their consumers (runtime does
so at shutdown). No candidate was installed live, no save/gameplay action ran,
and reconnect/resync/native acceptance remains open on VUH-1508.

## Offline relay session regression (2026-10-02)

`kh2coop_world_test` exercises real ENet loopback peers without KH2. Hit claims
require an established nonzero room epoch matching the host's current epoch;
the relay stamps the verified sender slot. No-room, zero, future and old-room
claims must not reach the host. Valid current claims retain their payload and
reach only the host. These are transport checks, not native damage application.

CampaignCoop host loss now ends all old peer connections. Both clean departure
and host heartbeat expiry must clear membership, actors and cached world/
progress state while keeping the relay listening. Controls preserve a friend
waiting before the host joins, keep friend heartbeats active while the host
goes silent, and separately expire all peers to test notification deduplication.
Fresh joins must receive no old room, hold, manifest, HP, death, progress or
activation state and can start a new epoch/progress version. PublicRealm is
outside this co-op teardown change.

```powershell
cmake --build build --config Release --target kh2coop_server kh2coop_world_test kh2coop_fake_sim kh2coop_avatar_test kh2coop_bridge_test
./build/Release/kh2coop_world_test.exe
./build/Release/kh2coop_fake_sim.exe
./build/Release/kh2coop_avatar_test.exe
./build/Release/kh2coop_bridge_test.exe
```

The new controls reproduced six hit-epoch failures and seven host-loss failures
before the respective fixes. World/fake/avatar/bridge suites passed 111/78/65/44
checks on Windows, then passed with Windows AddressSanitizer. Portable world
and fake suites also passed ASan+UBSan under Linux. Avatar/IPC tests remain
Windows-only. Current evidence is recorded in the
[relay lifecycle receipt](../build/rig/relay_lifecycle_offline_receipt.json).
Native combat, gameplay disconnect/reconnect and remote-session gates remain
separate live requirements.

## Automatic client hit claims: offline candidate (2026-10-02)

Ordinary local-player HP hits now pass from the native client hook through the
runtime and relay to the host's byte-verified TakeDamage callback. Protocol v3
adds typed targets and relay-issued connection IDs; WorldBridge v6 publishes
atomic roster IDs so disconnected or replaced senders cannot keep queued
authority. A claim echoes its original connection, uses a strictly increasing
sequence across rooms, and is consumed before one native attempt. Old protocol
and legacy handshakes, duplicate identities and wrong room/target claims fail
closed. Native calls require current checked combat metadata and a complete
census; each call is followed by another census before HP/death/hash publication.

Release DLL/runtime/relay builds passed. Native-consumer/world/bridge/fake/avatar
suites passed **42/148/54/85/65** checks on Windows, then with AddressSanitizer.
The actual consumer and publisher run over owned synthetic memory in the native
suite: faults, recursive callbacks, expiry, retired connections, metadata
replacement and post-call list/lifecycle changes are exercised. Positive decoded
hash controls establish real post-callback HP, removal and replacement behavior
in that fixture; unavailable post-census cases publish no stale state. This
tests production EnemySync, with mocked transport/lifecycle/progress services;
it does not run native KH2 functions or the EntityHook ownership resolver.
Portable world/fake suites passed **148/85** checks with ASan+UBSan. Native SEH
and IPC suites are Windows-only; Windows UBSan remains unavailable. The native
consumer test compiles with `/W4 /WX`; portable C++ builds use
`-Wall -Wextra -Wpedantic`. Only existing CMake dependency/deprecation notices
remain. Receipt and logs: [D4 offline receipt](../build/rig/d4_offline_receipt.json).

`net_client_hit_claims.json` was prepared and syntax-validated offline. It starts
with nonempty typed enemy parity, drives real combos from both clients without
host attack/manual damage inputs, correlates immutable claim IDs to unique host
attempts, requires an exact nonlethal HP decrease and then strict native applied
HP/death/progress parity. Its first native run passed as recorded below. An unmatched courtyard/refill
population remains a failure, rather than a relaxed readiness condition.

Boss finishers, survival/caps, attack-specific effects, indirect attack ownership
and foreign-thread damage are outside this first automatic path's evidence.
Calculated damage is sent before native survival adjustment; the host does not
replay the full attack record. Native creation identity remains D5 work. No live
game/process/bridge actions or disk-save operations were performed for this
offline validation; the subsequent live result is recorded below. The existing
native-wave failure remains open.

## Automatic client hits: native courtyard acceptance (2026-10-02)

[`20261002-235208_net_client_hit_claims_1`](../build/scenarios/20261002-235208_net_client_hit_claims_1/report.json)
passed all 49 steps in **150.4 s**, through the existing Session 1 desktop
bridge. DLL SHA-256 was
`37C4A724CB5388FBC66DC7560A9DE9ADD1E88835FA742E46000C7A4426FCBE81`;
runtime and relay used matching protocol v3 and WorldBridge v6. The host had
no attack inputs or manual damage commands. Each client performed ordinary
native combos in turn.

The [claim audit](../build/scenarios/20261002-235208_net_client_hit_claims_1/d4_claim_audit.json)
correlates **22 published claims → 22 host receipts → 22 unique native
attempts**, with matching immutable payloads and no duplicates, missing
attempts, rejected claims or unknown native outcomes. All attempts recorded
`nativeOk=1` and `afterHpAvailable=1`: 19 exact nonlethal HP reductions and
three lethal reductions clamped at zero. Client 1 published 12 claims;
client 2 published 10, including the three lethal claims on netIds 2, 1 and 3.
Each corresponding death was applied exactly once on each client, with
positive pre-HP and observed post-HP zero; no failed death applications appeared.

Two fresh matching samples at every checkpoint showed epoch 1, full location
`[5,6,0,1,1,0]`, zero unmatched actors and equal recomputed native enemy hashes.
Five object-302 Shadows began at 20 HP. After client 1, HP was
`[5,20,8,9,13]` by netId; after client 2, only netIds 4 and 5 remained alive
at 9 and 13 HP everywhere. The shared progress hash stayed `2596620699`.
All four disk-save hashes were unchanged, with no save violations.

This establishes ordinary client-only Shadow damage and kills in this fixture.
It does not establish boss finishers, survival/caps, attack-specific effects,
enemy-to-player authority, native reconnect, general wave convergence or full
barrier/reward parity. The fixture does not force a lethal outcome; the three
observed kills are supported separately by the audited native death logs.
The captures are settled stills, not motion clips.

### Incoming player damage: remaining acceptance

The client-kill fixture protects all three Soras, so its pass cannot establish
enemy attacks on a client player. The current step-1 enemy code keeps local
native AI on every peer and passes native damage to Sora through the existing
hit path. Historical Shadow-to-Sora logs establish that path, but lack a
current registered-client and canonical-victim join. They do not prove that
AI-suppressed enemies generate hitboxes from mirrored host motion in step 2.

A victim test needs a fresh client Sora that was never protected, a completed
connected room, one genuine nonlethal native enemy attack, and checked
canonical-player/head/status/HP observations. No manual damage command or HP
write supplies the hit. Static slot-0 HP or an actor name alone is insufficient
to identify the controlled victim. Existing hit/HP logs are capped at 60/40;
missing records do not establish absence, and matching frames do not establish
one unique causal hit scope. Full damage-rule acceptance still needs source,
scope, connection/epoch and actual adjusted-delta/HP evidence, plus rejection
of damage to remote puppets. No new victim fixture has passed live.

Offline investigation: [native contract](../build/rig/victim_damage_native_contract_20261003.md)
and [archived diagnostic coverage](../build/rig/victim_damage_archived_evidence_20261003.json).

The default-off native observer is enabled by setting KH2COOP_TRACE_HITS=1
in the launched game's environment. It wraps the three existing guarded
damage hooks without changing their calls, arguments, returns or authority
filters. Snapshots join the current connection IDs, generation, epoch, load,
transition and full room tuple to checked canonical player/head/tracked
pointers, source metadata and pre/post HP. An ordinary incoming client-local
witness requires one normal ApplyHitDamage scope from return site 3D613C,
one nested TakeDamage/ApplyStatDelta chain, and agreement between the genuine
adjusted negative delta, Stat result and checked HP/clamp. ApplyHitDamage's
raw RAX is never treated as success.

Capture is bounded to eight children, eight nested parent scopes and 128
queued events; the frame drain emits at most 16 events per call. Partial
installation, foreign calls, unreadable or changed facts, nesting, overflow,
unwind and lost observations stay unavailable or ambiguous. Source names
retain the existing F_ exclusion; they do not establish ownership. Client
Sora's identity requires the canonical roots, rather than the name or slot.
The room/session capture reads owned state and fault-contained native memory;
its host-arrival check has no warp-channel writes.

The saved-log auditor reports this schema separately under each log's
nativeHitTrace field. Read complete and witnessCount there; the existing CLI
exit code still describes the combined spawn/lifecycle envelope. A hit-only
log can therefore have a complete native-hit section while returning exit 1
for absent spawn/lifecycle traces. Historical logs without hittrace schema
remain unavailable. A witness establishes that observed ordinary local-AI
damage scope only: no boss behavior, attack lifetime/incarnation, full
remote-puppet veto policy or step-2 host-replicated hitbox acceptance. The
new candidate has not been installed or tested live.

Release and Windows ASan passed the new trace core's 92 controls and the
production claim/context fixture's 51 controls; spawn 75 and lifecycle 59
regressions also passed. The Python auditor passed 85 controls. Both compiled
trace executables emitted a synthetic baseline through the actual production
serializer, and the auditor independently accepted its checked 100-to-93 HP
change with adjusted delta -7. Six serializer/auditor controls passed. This
uses synthetic originals and owned facts, rather than installed game hooks.
Independent reviews accepted the adapters, core, context and auditor after
replacing the warp helper that wrote channel status. The auditor deliberately
withholds full-file certification for any unknown scope or nonzero coverage
counter, including unrelated out-of-scope Stat calls. Such a result does not
establish absence of incoming damage. See the
[native-hit offline receipt](../build/rig/native_hit_trace_offline_receipt_20261003.json).

### Active-session HP ownership gate: offline candidate

The separate DamagePolicy gate runs without the trace opt-in. Its supported
boundary is an available current Host/Client session on the registered game
thread, with a readable, unapplied, nonzero stat-0 HP record. In that boundary,
remote representations and noncanonical player actors cannot receive or cause
HP changes; unknown source ownership also suppresses HP. These exclusions take
precedence over permission for the canonical local victim.

| Supported victim | Supported source | Result |
|---|---|---|
| Canonical local avatar | Positively classified native actor | Existing native damage |
| Host enemy | Canonical host avatar or positively observed native companion | Existing native damage |
| Client enemy | Canonical client avatar, positive damage, checked non-healing kind | One existing claim attempt, then zero HP amount |
| Any other pairing | Any | Zero HP amount, no new claim |

Off/foreign calls, unavailable context, unreadable hit records, applied records,
non-HP records and zero amounts preserve the previous filters and native path.
Unavailable context is distinct from unknown ownership inside a valid context.
Healing kinds 5/6 never become claims; remote HP suppression covers signed
nonzero amounts. The final zero leaf rechecks exact flags/stat/amount and writes
only the calculated damage field. Changed or unreadable observations do not
certify a veto. ApplyHitDamage is still called once with the original arguments
and genuine raw return, preserving native consumption and effects. No second
damage call, direct player HP write, new wire message or fabricated manual claim
was added.

Positive companion permission comes only from the existing original FriendAI
branch, with checked handler, current frame/session/roster and actor metadata.
Fresh native friend pointers must agree with cached targets at capture and use;
cached puppet drivers and currently reserved remote targets stay excluded. The
independent review found and closed the stale raw/cache replacement gap. These
checks are conservative current-control evidence, not authenticated remote
incarnation or native actor lifetime proof. Other checked native source classes
can affect only the canonical local victim. The selected matrix also suppresses
supported HP records for other victims, including companion/world-object
candidates; compatibility outside ordinary enemy/Sora fixtures is unverified.

Release and Windows ASan passed 166 policy/owned-record controls and 54
production claim/context controls. Both DLL builds passed after the companion
membership fix. The policy test executes the actual copied-facts core and
checked zero leaf; its original/claim harness is synthetic. It does not execute
the production membership adapter, native callbacks or installed hooks. The
context fixture tests full-width roster IDs and replacement between reads.
Unchanged trace/auditor evidence above remains valid. No candidate was installed
live, and neither remaining VUH-1503 acceptance criterion is closed. See the
[policy review](../build/rig/damage_policy_cpp_review_20261003.md) and
[policy offline receipt](../build/rig/damage_policy_offline_receipt_20261003.json).

### Recorded ownership decisions and checked-zero outcomes

With KH2COOP_TRACE_HITS=1, each captured Apply scope now carries an optional
five-row damagepolicy schema-1 envelope using that same sequence. It records
the gate's own copied authority context, victim/source and hit facts, all three
full-width roster IDs, evaluated action/reason, and actual claim-attempt/queue,
revalidation and checked-zero outcomes. A queued claim is not host acceptance.
Unattempted result fields are placeholders. No extra native reads, damage calls,
wire messages or authority changes were added to collect these facts. Historical
hittrace schema-1 output without a policy envelope is retained.

The saved-log auditor reports damagePolicyTrace separately. Its decisionCount
checks the copied decision against the frozen matrix; its zeroVetoCount requires
much more: a directly checked noncanonical type-0 player exclusion, matching
authority/enclosing pre/post facts, complete normal hook coverage, recorded
successful revalidation and Zeroed outcome, native record consumption, unchanged
checked victim HP and no nonzero HP child delta. Zero-delta Stat can return zero
without writing HP; its checked HP and ancestry remain required. Fault/Changed,
failed revalidation, manual/sync-only zero and a producer action alone do not
certify a veto. Read event limits even when the recorded envelope is complete.

NativeHitTrace's ordinary incoming-witness checks remain unchanged. A veto is
an expected non-witness there, so incoming witnessCount can be zero while the
separate policy envelope has bounded veto evidence. The CLI still reports the
combined spawn/lifecycle/hit provenance and can return 1 for a policy-only log.
Neither section proves an absence of attacks. The policy audit independently
compares the two pre-captures and raw post-capture; revalidation/Zeroed remain
recorded execution outcomes. There is no logged third-roster post snapshot.
Driver/native-AI stamps and authenticated peer incarnation cannot be reconstructed
from these rows. Type-0 exclusion is not authenticated remote-human ownership.

Release and Windows ASan passed 125 trace/serializer controls; both DLL builds
passed. The Python auditor passed 107 controls. Both real serializer executables
retained byte-identical historical baselines and emitted two synthetic veto
scopes independently accepted by the policy audit. Four actual-format mutations
(fault result, failed revalidation, truncated host ID and mismatched canonical
root) produced no veto evidence. The emitter uses the production policy and
owned-record zero leaf with synthetic original callbacks and copied membership
facts. No production EntityHook callback or native gameplay proof is claimed.
Current source/build/test hashes and review are in the
[policy-observation receipt](../build/rig/policy_observation_offline_receipt_20261003.json).

## What a run does

1. **Takes the rig lock** (`build/rig/rig.lock`, holder pid and command).
   One live lane at a time: a second runner exits with code 3. A lock left by
   a dead process is stale and taken over.
2. **Refuses if a KH2 the rig didn't launch is running** (exit 3): James may be
   playing. It never touches that process.
3. **Hashes the save folder** (SHA-256 of every file under
   `My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX\`, read-only) before and after the
   whole suite. Any change other than Steam's own `steam_autocloud.vdf` fails
   the suite (`saveViolation`).
4. Runs each scenario's steps in order; the first failing step ends that run.
   While a step runs, every instance is watched:
   - process exited → **CRASH**: the bundle has the DLL's minidump
     (`kh2coop_crash_<pid>.dmp`, written by its unhandled-exception filter)
     and the inject log;
   - gameplay frames stalled for 30 s in the field → **HANG**: the runner
     writes an outside minidump (`kh2ctl dump`) and kills the instance.
5. Kills every instance the run launched, pass or fail, and moves on to the
   next scenario.

Output goes to `build/scenarios/<stamp>_<name>_<attempt>/` (`report.json`,
`report.md`, captures, clips, logs, bundle). `build/scenarios/last_suite.json`
holds the suite summary. Exit codes: 0 all passed, 1 a scenario failed or the
save changed, 2 a scenario crashed or hung, 3 rig unavailable.

## Save safety

Two independent layers:

- **The DLL's save guard** (`inject/src/SaveGuard.cpp`) is installed first
  at init and covers everything under the save folder from inside the game.
  - **Write opens are redirected.** A write-capable open through KernelBase
    `CreateFileW/A` or `CreateFile2` goes to the same relative path under
    `build/rig/logs/save_sandbox_<pid>/`. If the open keeps contents
    (`r+b`), the original is copied in first. After that, reads of that file
    go to the sandbox too, so the game sees what it "saved".
  - **Why redirect rather than deny:** the save writer doesn't check
    `_wfopen_s` for NULL before libpng, so a denied open would likely crash
    the game mid-save (repos-60's static read).
  - **Everything else is denied:** copies (`CopyFileA/W/ExW/2`), moves
    (`MoveFileExA/W`, `MoveFileWithProgressW`), `ReplaceFileW`, deletes
    (`DeleteFileA/W`) and `RemoveDirectoryA/W`. Copy goes to NtCreateFile
    without passing CreateFileW, so it's hooked by name. If no sandbox can be
    created, write opens are denied too (fail closed).
  - **Logging:** every operation is logged as
    `[saveguard] redirected|blocked …`.
  - **Self-test:** setting `KH2COOP_SAVEGUARD_TEST_DIR` before
    `kh2ctl launch` guards that directory too and runs a self-test at init.
    It plants a probe file there through the unhooked API, then tries every
    path against it. That includes `_wfopen_s` called through the exe's own
    import thunk (RVA `0x471BC8`), first `r+b` (which must read the original),
    then `wb`, and the `DeleteFileA` the exe imports.
  - **Result on 2026-10-02:** 0 of 14 calls reached the probe. Opens were
    redirected, and `r+b` read the original. Everything else was denied.
- **How the game writes saves** (headless Ghidra, 2026-10-02):
  - The save path comes from the string
    `KINGDOM HEARTS HD 1.5+2.5 ReMIX\Steam\KHIIFM_WW.png` (RVA `0x7164E0`).
  - `FUN_1400fef80` opens the save container read-only (CRT
    `_sopen_dispatch`). If the file reads as empty, it calls `DeleteFileA` on
    it.
  - `FUN_140100810` creates the folder and checks free space.
  - The PNG writer `FUN_140145170` opens the file with ucrtbase
    `_wfopen_s(path, L"r+b")`, then `L"wb"`. Both reach KernelBase
    `CreateFileW` with write access, which the guard redirects.
  - The game changes its working directory (`_chdir` in `FUN_140101000`), so
    the guard resolves relative paths to full paths before matching.
- **The suite's before/after hash**, which catches anything the guard misses.

Never save in-game without James's explicit approval.

## Writing a scenario

```json
{
  "name": "sora_move_attack",
  "description": "What it shows, in one sentence.",
  "protect": [0],
  "steps": [
    {"do": "boot"},
    {"do": "warp", "world": 5, "room": 6},
    {"do": "save", "as": "start", "expr": "pos()"},
    {"do": "input", "ly": 1, "ms": 900},
    {"do": "assert", "expr": "dist(pos(), saved['start']) > 300", "message": "Sora didn't move"},
    {"do": "press", "button": "cross", "times": 20, "gapMs": 300},
    {"do": "capture", "name": "after_combo"}
  ]
}
```

`"instance": N` (default 0) on a step picks the instance; `boot`/`launch`
append a new one. `"protect": [N]` keeps that instance's Sora on team 0 (no
attack hits him) for the whole run, so combat rooms don't end in a game over.
The protector writes team 0 to the first enumerated `P_EX100` each second.
`{"do":"protect","on":false}` only stops that polling; it does not restore
the previous team value. An incoming-damage test must use a fresh naturally
damageable Sora and verify its canonical identity instead of reusing a
protected actor. Protection is best effort during loads.

| Step | Fields | Does |
|---|---|---|
| `boot` | `timeoutSec` | Launch + inject, mute, pick LOAD on the title menu (checked by pixel in captures), load the save list's default slot, wait until the room is live. Menu presses go through the DLL's input collector (`player-press`), so no window focus is needed |
| `launch` | `mute` | Launch + inject only (stays on the title) |
| `warp` | `world room door map btl evt` | `kh2ctl warp`; returns once the room has loaded |
| `input` | `lx ly rx ry ms` | Hold sticks (`player-input`) |
| `align_courtyard_exit` | `instance as blockedExpr` | BC courtyard entrance-0 fixture only: pulse screen-left (`lx=-1`) for 250 ms at a time, recording each start/end position. Stop at `abs(x)<60` or a true `blockedExpr`, with a 6 s limit. Each unblocked pulse must reduce `abs(x)`; otherwise capture and fail with a diagnostic JSON. No host movement |
| `press` | `button ms times gapMs` | Press a button (`player-press`; PS2 names: `cross`, `circle`, `triangle`, `square`, `start`, …) |
| `wait` | `ms` | Sleep while watching the instances |
| `wait_until` | `expr timeoutMs pollMs` | Poll an expression until true |
| `assert` | `expr message` | Fail the run unless the expression is true |
| `save` | `as expr` | Store a value in `saved[...]` |
| `protect` | `on` | Start/stop team-0 protection polling; stopping does not restore the previous team |
| `capture` | `name` | PNG from inside the renderer; retain the actual CLI response under `receipt` alongside the relative artifact `path` |
| `clip` | `name seconds` | MP4 → artifact |
| `crash`, `freeze` | | Runner self-tests: fault or suspend the instance |
| `relay` | `port build content mod args` | Start `kh2coop_server` on loopback for this run; the version gate defaults to what the runtime sends |
| `runtime` | `instance role peerId link args expect timeoutMs` | Start `kh2coop_runtime_scaffold --network --no-camera --pid <instance>` and wait for `expect` in its log (default `Verified membership; native bootstrap remains separate`). Refuse an existing owned runtime/log or live owned writer for the same game PID before opening the log. `link: {latencyMs, jitterMs, lossPct}` sets the runtime's impairment both ways (owner→viewer crosses two runtimes, so 50 ms each = 100 ms) |
| `reconnect_mark` | `as instances timeoutMs` | Seal current original `[0,1,2]` process/session/connection/generation/native-load/provenance observations and exact log prefixes; default timeout10s, maximum15s. Preserve raw inputs on success or failure |
| `runtime_pause` | `instance:1 after as timeoutMs` | Suspend only the original registered Friend1 Popen handle after a qualified reconnect baseline. Wait for fresh exact retirement/survivor evidence, then resume the same process. One pause per baseline; timeout1–20s; retain recovery receipt in finally |
| `reconnect_check` | `after as timeoutMs` | Require the original successful pause receipt and original live process/relay bindings, then strict same-session native recovery against the baseline. Default45s, maximum60s. Retain raw/partial inputs; repeat with a distinct `as` after final population gates |
| `record` / `record_stop` | `as instances` | Sample every party actor's position (with the instance's world/room) on those instances in the background; `record_stop` writes `<as>.csv` |
| `wander` | `instances seconds seed` | Seeded random stick walks with occasional jumps, taking turns (soaks) |
| `hit_all` | `instance op amount` | `kh2ctl hit damage/kill` on every live combat enemy of that instance (host-only damage tests) |
| `kh2ctl` | `instance args` or `argsExpr` | Run the supplied argument array against that instance. `argsExpr` evaluates an expression to an array, for explicit reversible fixture writes |
| `transition_check` | `as after instances target timeoutMs capture` | Wait for completed-load parity, relay ACKs, and restored puppet slots; save structured evidence under `as` (default `transition`). `after` names an earlier checkpoint whose epoch must advance; `target` is the expected `[world, room]`. Default instances are `[0,1,2]`, default timeout 30 s. On timeout, capture every participating instance and preserve the last observation as `<as>_failed.json`. `capture: true` also captures successful checkpoints |

| `progress_snapshot` | `as instances compare` | Save actual allow-listed progress and complete personal byte ranges to JSON. Optional `compare` names a prior snapshot and emits each changed SAVE offset and before/after byte |
| `statehash_check` | `as instances minEnemies timeoutMs` | Require fresh post-arrival hashes on all peers at the same completed epoch and exact six-field location, nonempty native enemy rows, zero unmatched actors, and matching netID/object/HP populations. Save evidence or a failure artifact |
| `dismiss_goa_map_reward` | `as instance` | With flag409 open in GoA, capture the reward popup and poll the unchanged native safe gate; while unsafe, send cross150ms and wait700ms, bounded to10s. Save every gate sample, press count and failure capture |
| `approach_goa_chest` | `as instance` | In GoA on the central platform, walk toward flag409 using camera-basis projections and 200 ms native pulses. Stop within 100 units, require progress over each three-pulse window, fail after 15 s with a trace/capture |
| `courtyard_diagnostic` | `as instance baseline timeoutMs` | Replay the original courtyard client pulse/combo after a positive hash baseline; capture player/camera/spawn bindings and require a persistent extra unmatched actor plus enemy-desync. No reproduction fails explicitly as INCONCLUSIVE; a room/epoch change invalidates the diagnostic |
| `enemy_hash_control` | `as instance` | Separate native enemy fault: temporarily set one matched client's per-actor HP-lock bit, kill its host counterpart through the native hit path, require failed client death and two fresh native hash disagreements with relay enemy-desync, and restore only that bit if actor identity still matches |
| `progress_hash_control` | `as instance minEnemies timeoutMs` | Outside GoA, after a successful progress apply, toggle only chest flag 409 on the selected rig client, verify readback and fresh relay `fields=4` evidence, then restore and verify the original byte in `finally` |
| `progress_fault_resync` | Fixed `instance:0 slot:all as:forced_resync` | With the exact qualified five-Shadow HP17 courtyard setup, fault only Friend1 flag409, retain full native progress/personal reads and two progress-only mismatch frames, then run one strict both-target resync. Require actual one-byte repair before cleanup; preserve original transaction artifact for unchanged postchecks. Non-atomic diagnostic reads/write, with observed context guards and safe masked failure cleanup |

Processes started by `relay`/`runtime` are stopped at the end of the run.

Expressions are Python with these helpers (the instance index is the last
argument, default 0): `room()` → `(world, room)`; `pos(name='P_EX100')` →
`(x, y, z)`; `actor(name)` → the `kh2ctl entities` record (`hp`, `maxHp`,
`team`, `motionId`, `address`, …); `actors()`; `enemies()` (objentry type 3/4);
`peek(rva, kind='u32')`; `log_count(text)` (lines in the inject log, e.g.
`'attacker=P_EX100'` from the hit log); `dist(a, b)`; `saved`;
`enemy_hps(i)` (sorted `(name, hp)` of live combat enemies); `bridge(i)` (the instance's AvatarBridge via `avatarctl peek`: local
frames/s and both puppet slots); `puppet_error(saved['rec'], owner, viewer)`.

`location(i)` returns `[world, room, door, map, btl, evt]` from one NOW sample;
`log_matches(pattern, i)` returns named regex captures from the inject log;
`runtime_log(i)` returns the runtime log. `int` and `str` are available when
working with captures and structured evidence. `chests(i)` resolves native `F_*`
actors through their bounded `actor+0xC00` treasure pointer, validates the
record against the current world/room, and returns native actor state with
its treasure/item IDs, overall completion flag, SAVE byte and opened bit.
It requires the CLI's `peek.moduleBase` metadata; invalid reads fail explicitly.

`puppet_error` scores how well the viewer's puppet follows the owner's
Sora in a recording:
- Each sample from a viewer actor (other than the viewer's own Sora) is
  compared with the owner's positions over the preceding 0.5 s, because the
  puppet renders behind on purpose. The best-matching actor counts as the
  puppet.
- It returns `mean`, `p95` and `max` error in KH2 units (100 = 1 m), the
  fitted `lagMs`, and `ownerTravel`.
- Check `ownerTravel` too. If the owner barely moved, a low error proves
  nothing.
- Only samples taken while owner and viewer are in the same world/room
  count. Puppets are matched by actor name, since addresses repeat across
  rooms.

`jitter_stats(saved['rec'], owner, viewer, err['actor'])` counts "pops": puppet
steps between consecutive samples that exceed the owner's largest step over
the last 0.5 s by more than 30 units. Steps only count while the owner has
been in the viewer's room for at least 1.5 s.
`tools/scenario/spikes/pops.py RUN_DIR OWNER VIEWER NAME` lists each pop
with its context. The sampling runs at about 20–40 Hz per instance, so
single-frame jitter needs a clip.

## Example scenarios

`tools/scenario/scenarios/`:

- `boot_to_goa`: load the save, warp to the GoA, Sora moves.
- `sora_move_attack`: BC courtyard, run in, combo until Sora's hits land in
  the hit log.
- `two_instances_same_room`: two instances load and warp to the GoA; both
  are there and both Soras move.
- VUH-1492 networking (relay on loopback, one runtime per instance):
  - `net_two_instances`, `net_two_instances_impaired` (100 ms + 2% loss),
    `net_three_instances`: every puppet follows its owner.
  - `net_room_hide`: a puppet hides while its owner is in another room.
  - `net_mismatch`: a mismatched peer is refused with a reason.
  - `net_viewer_leaves`: the viewer changes rooms with a puppet active.
  - `net_soak_10min` + `soak_control_no_network`: the 10-minute soak and its
    no-network control.
- VUH-1502 enemy sync (host + 2 clients): `net_enemy_sync_courtyard`,
  `net_enemy_sync_waves`.
- VUH-1496 host-led transitions: `net_host_transitions_smoke`,
  `net_host_transitions_acceptance`, and the calibrated supplementary
  `net_host_transitions_native_exit` fixture (details below).
- `forced_crash`, `forced_hang`: runner self-tests. They're expected to
  report CRASH/HANG with a bundle; don't include them in a pass/fail suite.

## Host-led transition evidence (VUH-1496)

Run the smoke first, then the route, from the desktop live lane:

```powershell
python tools/scenario/run.py tools/scenario/scenarios/net_host_transitions_smoke.json
python tools/scenario/run.py tools/scenario/scenarios/net_host_transitions_acceptance.json
```

Both fixtures boot three games before starting the runtimes. The host starts
in GoA, client 1 starts in a different room, and client 2 connects only after
the host and client 1 have completed another transition. The late join must
arrive and acknowledge the existing host epoch. Session slots must equal
instance indices: start the `player`, `friend1`, and `friend2` runtimes in that
order. Boot uses the save list's default slot, so the room programs and party
availability still depend on that save.

The acceptance route then performs 20 host `warp` requests across five rooms
(`04/1A`, `05/01`, `05/04`, `05/05`, `05/06`). All three Soras are protected
in these fixtures. Each checkpoint requires:

- A new completed-load epoch on all three instances, not just matching IDs.
- Exact equality of world, room, entrance, map, battle and event programs
  between every current NOW sample and its arrival log, and between peers.
- A successful `TransitionAck` for each client's current epoch and room in
  the relay log.
- A live local avatar, both active puppet poses with the expected distinct
  remote owners and room, and both native friend actors within 100 game
  units of their published poses. The latter is a settled-position check;
  continuous motion fidelity remains covered by `net_three_instances`.

Native actors are resolved against the active entity list. Companion targets
come from the two unit-slot-1 actor pointers (`0x2A239B0`, `0x2A239B8`). With
non-head player-class actors present, the DLL selects Sora clones instead;
the checker takes the actual per-slot addresses from `[puppet N] ... actor=`
motion-driver logs after the latest `[warp] load complete` marker. Names or
list ordering do not establish clone ownership. Both targets must be distinct,
nonzero, different from the local actor and present in the current entity
list. Reports retain the pointers, resolution source, candidate clone
addresses and resolved actors. A default `kh2ctl state` friend record is
insufficient: it can contain a zero position when no friend entity exists.

The first live smoke on 2026-10-02 reached matching epochs, locations and ACKs
in Twilight Town `02/00` but failed puppet-position checks. Its report remains
under `build/scenarios/20261002-142449_net_host_transitions_smoke_1/`. The
replacement route avoids that party-capacity fixture and avoids BC `05/02`,
which can open a cutscene. The passing replacement runs below establish two
native companion targets in every route room for the current save; missing
native puppets continue to fail. The late
join checkpoint captures a successful sample on every instance, and every
failed checkpoint attempts captures before writing its diagnostic snapshot.

**Passing live evidence, 2026-10-02 (WorldBridge v4).**

| Scenario | Report | Result |
|---|---|---|
| Smoke | [20261002-143623](../build/scenarios/20261002-143623_net_host_transitions_smoke_1/report.json) | PASS, 139.7 s. Initial host-follow, late join at the existing epoch, and a same-room reload with all three instances and restored native puppet targets. |
| Acceptance route | [20261002-143907](../build/scenarios/20261002-143907_net_host_transitions_acceptance_1/report.json) | PASS, 195.8 s. Twenty route loads at epochs 3–22 across all five rooms; every checkpoint has both client ACKs and six native puppet-position checks. Largest settled error: 37.57 units (limit 100). |
| Native walking exits | [20261002-145121](../build/scenarios/20261002-145121_net_host_transitions_native_exit_1/report.json) | PASS, 153.1 s. A client request for `05/00` was blocked at epoch 3; the host's native walking loads advanced through epochs 4–6 with both client ACKs and a final all-instance puppet check. |

All three runs checked four save-file hashes with no changes or save violations.
These fixtures used native Donald/Goofy companions, not replacement Sora
models. The final two acceptance loads had the exact location
`[5, 1, 0, 0, 0, 20]` with different completed-load epochs. Each report retains
the complete per-instance location, owner, native actor and ACK evidence;
the late join and end checkpoints have screenshots of all three games.

The last two loads have the identical six-field location. Their epochs must
still advance. This exercises native request/load hooks via host CLI warps;
it does not prove a naturally triggered same-room script reload. The smoke
includes the same reload check with two route loads.

Arrival logs come from the native load lifecycle. Static tracing found that
`RequestTransition` (`0x152990`) stages the packet, commit (`0x152BE0`) writes
the entrance as one byte at NOW+2 and resolves programs, and the load task
(`0x152680`) schedules completion (`0x152CD0`) after the loaders. Completion
sets `IN_FIELD` and runs finalizers; the direct equivalent `0x152F40` is also
hooked. The request and both completion hooks check 24 executable bytes
before installation. Client acknowledgement waits for completion return,
the gameplay gate and all six fields to match. A pause/stall alone is not
load completion evidence. These implementation facts still require the
live scenarios to establish behavior on the running build.

`net_host_transitions_native_exit` is a supplementary walking fixture,
**calibrated live on 2026-10-02**. At BC courtyard entrance 0, client 1 first aligns toward the
doorway centerline using 250 ms screen-left pulses (`lx=-1`). Each pulse
records its start/end position and must reduce `abs(x)`; a wrong direction
or obstruction captures and fails. Alignment stops at `abs(x)<60`, after a
new blocked native request, or fails with a capture at the 6 s bound. The
blocked-request baseline is recorded before alignment. The client then
holds backward for 4 s; the host is untouched until its own backward pulse.
It requires a new `[warp] client native exit blocked` log and then unchanged
full locations and host epoch with restored puppets. Staying put without
an intercepted native request fails. The fixture records both starting
positions and captures the client immediately after its walking pulse, before
waiting for the blocked request, so a missed doorway leaves visual evidence.
It derives the destination from the first new blocked request, then gives
the untouched host the same 4-second backward pulse without an intervening
warp. The host must reach that world/room in a newer completed-load epoch;
all clients must arrive, acknowledge and restore their puppets. The successful
host-exit checkpoint captures every instance. It never substitutes a client
CLI warp for walking. The passing run aligned client X from -101.878 to
-25.278 in two pulses, then intercepted `05/00` with unresolved programs
`0xFFFF`. All instances stayed at `[5,6,0,1,1,0]`, epoch 3, after that client
attempt, with zero settled puppet error. The host's held walking pulse crossed
the doorway three times (`05/00` → `05/06` → `05/00`); both clients acknowledged
epochs 4, 5 and 6. The final checkpoint had `[5,0,0,0,1,5]` on all three games,
and maximum settled puppet error 14.14 units. Intermediate native-load puppet
positions were not sampled by that checkpoint. A natural same-room script
reload remains separate.

The first walking attempt (`20261002-144249_net_host_transitions_native_exit_1`)
missed the castle doorway: client start `(-450.7,-260,1950)` reached the wall
at `(-535.8,-260,2045)` without a native request. The captures show the
lateral offset; the host started near center at `(48.5,-260,1950)`. This is
why alignment checks measured progress rather than assuming every client's
spawn remains centered. The subsequent passing run and its per-pulse samples
are retained in the table above.

The transition route evidence above covers transition and puppet behavior;
it does not establish enemy/progress hash acceptance. The applied-state
publisher and the additional nonempty fixture below now provide that check,
but require a passing live report from the current build. Cutscene
hold/resume remains separate.

## Progress and applied-state hash fixtures

- `progress_chest_goa_probe`: one instance boots, enters `04/1A`, captures
  the room and records all actors, native treasure records and flag 409.
  This is exploratory evidence, with no memory write or chest interaction.
- `progress_chest_goa_native_open`: native camera-aware approach and reaction
  press, capture/dismiss the obtained-map popup with native confirm, and wait
  for normal gameplay before checking settled/opened state. This calibration
  fixture requires an initially closed chest and performs no memory writes.
- `net_progress_chest_goa`: three-instance chest acceptance. It clears only
  flag409 in memory outside GoA, verifies closed native initialization, opens
  the host chest through movement/reaction input, checks client next-load
  application, late-join current state and subsequent reload persistence.
  Each client apply must log unchanged personal bytes around allowed writes.
- `net_statehash_enemy_negative`: separate negative fixture using verified
  actor `+0x9B8` bit `0x4` (native ApplyStatDelta HP lock) on one client enemy,
  followed by a native host kill. It requires a FAILED client death log,
  actual surviving positive HP, two distinct fresh hash frame sets and
  relay DesyncEnemies. It restores only the original bit by read-modify-write
  while address/object/status identity still matches, then reloads through
  the host and requires nonempty parity. No shared object descriptor is
  modified. Failure remains failure; it cannot pass from a fabricated packet
  or an attempted mutation alone.
  The current fixture explicitly targets object ID311. Its prior object309
  run [`20261002-185625_net_statehash_enemy_negative_1`](../build/scenarios/20261002-185625_net_statehash_enemy_negative_1/report.json)
  correctly failed: client1 retained native HP153 after a failed mirrored
  death and produced relay `fields=2`, but client2 independently acquired an
  unmatched object309 actor at HP160. Unaffected-peer parity remains strict.
  The control restored flags1159 to1155; that useful negative observation
  does not turn the full failed run into acceptance. Targeting311 is an
  explicit fixture choice, recorded in control evidence, not a population
  filter in the hash checker.
  The three-peer311 attempt also failed in
  [`20261002-190415_net_statehash_enemy_negative_1`](../build/scenarios/20261002-190415_net_statehash_enemy_negative_1/report.json):
  client2 again gained an independent unmatched309/HP160 while the controlled
  client1 target remained alive. Both failed three-peer runs and their
  strict checker remain intact.
- `net_statehash_enemy_negative_isolated` boots exactly two peers (host and
  client1) to isolate the detector control for object311. The checker uses
  every instance in that run, requires two fresh actual native mismatches
  and relay enemy-desync, restores the guarded HP-lock bit and reloads to
  require nonempty parity. This is explicitly two-peer detector evidence;
  it neither replaces the three-peer positive hash proof nor resolves the
  independently observed third-peer spawn divergence.
  The isolated live run
  [`20261002-192119_net_statehash_enemy_negative_isolated_1`](../build/scenarios/20261002-192119_net_statehash_enemy_negative_isolated_1/report.json)
  **failed overall** (134.9s) at its post-reload transition checkpoint.
  Its earlier negative checkpoint passed: two full native observations
  (host/client frames3643/1228 and3703/1289) retained client netID5/object311
  at HP153 while the host counterpart was dead, the native client death log
  reported FAILED153?153, and the relay reported peer1 `fields=2`. Guarded
  restoration verified flags1155?1159?1155.
  Reload reached completed epoch2 and matching full locations, but both
  peers had an active logical puppet with native friend pointers `[0,0]`,
  no clone candidates and only the local actor. The strict native-puppet
  check correctly failed; the later restored nonempty hash check was not
  reached. Thus detector and flag-restoration checkpoints are proven within
  a failed run, while native-puppet reload recovery and the separate
  three-peer extra-spawn divergence remain unresolved. The fixture and
  checks remain unchanged; this run is not represented as acceptance.
- `net_statehash_nonempty`: extends the established `12/0B` battle-program-1
  two-wave fixture. Three peers must agree before and after host damage,
  with at least four living native enemies. It then toggles only client 1's
  in-memory GoA chest flag 409, requires a fresh relay notice with exactly
  `fields=4`, restores the byte and requires fresh agreement again. The
  original wave scenario and known courtyard failure are preserved.

The chest reward popup freezes native gameplay and holds animation `0x99`
(153); bit409 changes before it is dismissed. The fixtures capture the popup,
send bounded native confirmation pulses, then wait for unfrozen gameplay, a live field, menu
`0xFF`, native event state `exe+0xB65210 == 0` and event context
`exe+0x2A11478 == 0`. The retained timer is diagnostic. This preserves the native safe-state gate.
Do not interpret the popup animation as the reloaded opened chest state.
The confirmation pulses are a bounded calibration action, not proof that
confirm is what closes the popup: offline native tracing indicates an
animation/resource-driven lifetime. Gate samples also record UI tick delta
at `0x717484` to distinguish stalled timing from input readiness.
Calibration [`20261002-190126_progress_chest_goa_native_open_1`](../build/scenarios/20261002-190126_progress_chest_goa_native_open_1/report.json)
failed the strict safe-state wait even after the popup disappeared and normal
HUD/gameplay returned: frozen=0, inField=1, menu=255, but the sampled timer
retained90. This is evidence to investigate whether the timer represents
active playback or retained elapsed time; the fixture does not mask it.
Dismissal failures preserve native chest/actor state and gate samples even
while the gate rejects traversal. The runner now uses the bounded native event-state/context predicate above;
the single-instance calibration additionally leaves and reloads GoA to
require the native opened initialization motion `0x98`. Full mirrored-chest
acceptance uses the verified calibration below.

Corrected-gate single-instance calibration passed in
[`20261002-191026_progress_chest_goa_native_open_1`](../build/scenarios/20261002-191026_progress_chest_goa_native_open_1/report.json)
(66.3s). During the popup, native event state3 and nonzero context blocked
traversal with timer45. After2.906s, state/context were both zero while the
timer retained90; native leave-and-return warps succeeded and GameBridge
reported no active cutscene. The natively opened host chest used animation
153 with flag409 set. After native room reload it initialized to animation
152, while the other two closed chests stayed151. The full fixture therefore
requires immediate host153 and reloaded client/late-join152, not one animation
value across both lifecycle phases. Native opened-bit proof comes from the
reaction input, and no memory write changed the event state or timer.

Three-instance chest acceptance passed in
[`20261002-191345_net_progress_chest_goa_1`](../build/scenarios/20261002-191345_net_progress_chest_goa_1/report.json)
(189.3s). Native host opening changed flag409 and progress version1 to2;
the immediate snapshot showed only the host chest byte changed, while both
client bytes stayed closed until their allowed application boundary. Client1
then reloaded GoA at completed epoch3 with opened initialization motion152.
Client2 joined at that existing epoch, applied version2 and also initialized
the chest open. A subsequent all-peer reload advanced epoch4 with the exact
six-field location `[4,26,0,0,0,0]` and opened motion152 on all three peers.
Each transition checkpoint also retained relay acknowledgements and native
puppet checks; captures show next-load, late-join and final reload states.

Client1 applied versions1,2,2,2 and client2 applied2,2. Every immediate
around-write personal comparison reported before/after hash `124AA108` and
`personal_unchanged=1`. Full endpoint snapshots independently showed zero
changed bytes in characters, inventory, munny and EXP on all three peers.
The shared chest difference was exactly SAVE+0x23DF, byte0 to2, on each peer;
client2 also recorded a visited-room difference. All four on-disk
save hashes were unchanged. This proves native chest progress, boundary
application, late-join freshness and personal exclusions for this fixture;
it does not assert that every native story-setter side effect is mirrored.

The hash checkpoint starts a fresh observation window when called; old
matching logs cannot satisfy it. Every hash must follow the latest native
arrival, match its completed epoch and all six current NOW fields, and
include the complete raw row set associated by epoch/frame. It recomputes
`KHE1` FNV-1a from the sorted positive-HP `(netId, objectId, hp)` records and
compares the result with the published hash. Positive checks require zero
unmatched rows and identical populations across all three peers. Dead rows
remain in the artifact, and negative controls retain unmatched rows rather
than filtering them out. An empty room cannot satisfy the default minimum.

`progress_snapshot` records actual bytes and SHA-256 for programs
`SAVE+0x10/0x1C80`, story `+0x1C90/0x260`, visited `+0x22F8/0x98`, chests
`+0x23AC/0x34`, and these complete personal exclusions: characters
`+0x24F0/0xE04`, inventory `+0x3580/0x140`, munny `+0x2440/4`, EXP
`+0x36E0/4`. A comparison reports each differing SAVE offset, old byte and
new byte. Native room initialization may legitimately change personal
state; these snapshots do not assert whole-range equality across loads.
The narrower apply invariant comes from `[progresssync] apply` logs:
`personal_before`, `personal_after`, and `personal_unchanged=1` compare
personal bytes immediately around only the allowed writes, before native
room initialization.

Live probe on 2026-10-02:
[`20261002-182909_progress_chest_goa_probe_1`](../build/scenarios/20261002-182909_progress_chest_goa_probe_1/report.json)
passed. Installed native `F_EX040_HB` (object 2540) resolves to treasure
585, item 592, room index 100, event `0x101E`, overall flag 409. Its closed
motion is `0x97`, at `(0,460,900)`, and `RVA 0x9ABC8F & 2` was zero.
The other two GoA records resolve to flags 410 and 411. This confirms those
installed rows; broader chest-range coverage still relies on the bounded
native/table evidence in `build/rig/progress-ranges.md`. Native host opening and mirrored open initialization are established by the
three-instance acceptance run below.

Applied-state hash acceptance passed live in
[`20261002-184220_net_statehash_nonempty_1`](../build/scenarios/20261002-184220_net_statehash_nonempty_1/report.json)
(180.2 s). All three peers had completed epoch 1 and exact location
`[18,11,0,0,1,0]`, four native matched enemies (netIDs 3?6, object IDs
309/311), zero unmatched actors, and identical recomputed enemy hashes.
Native HP changed from 160 to 153 after host damage, with fresh agreement.
Toggling client1's actual flag409 byte from 0 to 2 after applied version 1
changed only its progress hash and produced relay `fields=4`; restoring 0
restored three-peer hash agreement. The full before/after snapshots had
zero changed bytes in every captured region on every peer, and all four
save-file hashes stayed unchanged. This run proves the hash and reversible
progress-control behavior; its later `second_wave` observation was empty
and is not additional two-wave-spawn evidence.

## Native enemy census diagnostic

The strict 20-load acceptance failed at `hash_transition_08` in
[`20261002-193320_net_host_transitions_statehash_acceptance_1`](../build/scenarios/20261002-193320_net_host_transitions_statehash_acceptance_1/report.json):
completed epoch 10, location `[5,6,0,1,1,0]`, published host count 0 versus
five unmatched object302/HP20 rows on each client, with equal progress hashes.
Those published counts came from the update-hook census; they were not an
independent complete native-list observation.

`net_enemy_census_transition08.json` retains that route through transition08,
then collects an immediate snapshot and two more after 1.5-second intervals
before retaining the original strict hash check. `native_enemy_census` reads
peers concurrently, with a 30-second bound per peer/sample. Its artifact always
retains errors and partial observations. Successful collection proves neither
peer parity nor enemy absence by itself.

The helper treats `entities` as candidates only. Checked reads validate native
HEAD/TAIL, every next handle and its region-table resolution, terminal handle0,
object descriptors including the `F_` prefix, status and actual HP, and stable
identity/location/arrival/lifecycle bookends during safe gameplay. Cycles, cap
hits, unreadable pointers and changing loads remain incomplete. Hash coverage
comparisons require the latest post-arrival hash to match epoch/full location
and complete raw records/recomputed canonical hash; unavailable comparisons
retain raw logs without a coverage conclusion.

Actor identity rechecks now retain `actorIdentityRecheck`: complete before/after
field samples, each changed actor address, field name/address and old/new value,
and unread fields if a later read fails. The checked fields remain next handle,
object descriptor, status pointer, controller at actor+`0x9E8`, and spawn record
at actor+`0x9F0`. A partial or changed recheck still fails completeness; its
provisional enemy count is not absence proof. The existing per-peer deadline
also bounds this recheck. This improves later failure artifacts; it cannot
recover the missing changed-field detail from the earlier transition03 run.

The cache probe accepts only four inline buckets rooted at RVA `2AE5E60`,
reads all 256 u16 record IDs twice, and brackets active pointer/counter reads.
Record IDs are not object IDs. Controller/record shape checks and bounded
ordinary controller-table observations are separate causal evidence: script
entries are skipped, table cap64 is diagnostic rather than a proven native
limit, and incomplete provenance does not erase complete list evidence.
Controller stage/cooldown, tracked activation/player positions, raw spawn
records and table/key membership help distinguish activation history. These
observations remain read-only and do not establish every producer's semantics.

The [canonical controller/lookup ABI reference](pointer_map_v1.md#ordinary-spawn-controller-hook-and-lookup-abi-2026-10-02)
records checked field widths, fingerprints and supported record scope.

The follow-up geometry capture walks actual linked regions for ordinary type2
controllers, prioritizing header30. It reads each rooted 64-byte descriptor and
runtime inverse matrix/extents, accepts only the verified BOX/CYLINDER/INFINITY
vtables, and rechecks links, geometry bytes, controller roots, full location and
lifecycle. Descriptor cap64 is an explicit diagnostic limit. The native
activation float4 follows accessor `3B5B40`: a checked nonnull parent-handle
resolution selects actor+`0x70`; a zero handle selects +`0x670`. Unknown or failed
resolution cannot silently select the latter.

`geometryComparisons` evaluates every sampled peer activation point against the
captured regions with float32 operations, marks near-edge or nonfinite outcomes
uncertain, and preserves whether the point moved during capture. Concurrent
reads are not atomic frame observations or native predicate return values.
New `causeContext.positionReadback` and `positionSelectorReadback` receipts
retain exact little-endian before/after bytes, requested field addresses,
partial reads, unread fields and byte differences from the four existing
reads. No extra read, retry, tolerance or acceptance change was introduced.
`pointStableDuringCapture` remains the exact aggregate comparison over both
activationActor and playerActor; movement in either can fail it. Role labels
that alias one address share the CLI's address-keyed returned value, without
independent timestamps. Empty/incomplete receipt coverage cannot certify a
stable point. Ten actual-collector controls with synthetic memory passed;
historical reports lacking repeated bytes remain unchanged and unexplained.
`geometryComplete` and causal limits are separate from primary census
completeness; a successful list collection cannot prove incomplete geometry.
No native functions are invoked and no game memory is written. The transition08
run below predates region capture; the later partial transition03 result records
the live geometry evidence and its limits.

Live collection succeeded in
[`20261002-200856_net_enemy_census_transition08_1`](../build/scenarios/20261002-200856_net_enemy_census_transition08_1/report.json),
but the retained strict parity check made the overall scenario **FAIL**
(208.9 s). All nine peer snapshots were complete with valid hash comparisons.
The first snapshot had no native enemies and empty caches everywhere; the
later two had zero host enemies and five actual client enemies, matching each
peer's published rows. Type-2 controller header30 had flags2 on the host and
flags10 on clients; client caches gained record IDs `11,12,13,14,18` while the
host cache stayed empty, and activation positions diverged. This establishes
native spawn divergence in this reproduction, not a hidden host update-hook
population. Initially equal caches do not support retained cache history alone
as its cause. See [native findings and inference limits](ENEMY_PARITY.md).
All four disk saves remained unchanged.

With the newer native-census DLL, the unchanged transition08 diagnostic
failed earlier in
[`20261002-204129_net_enemy_census_transition08_1`](../build/scenarios/20261002-204129_net_enemy_census_transition08_1/report.json)
(176.7 s), at retained `hash_transition_03`, before geometry collection. Epoch5
location `[5,6,0,1,1,0]` had zero host rows versus five unmatched object302/HP20
rows on each client, equal progress hashes, and relay enemy-desync reports for
both clients. No geometry artifact was produced; all four saves were unchanged.
This does not replace or reinterpret the earlier transition08 observations.

`net_enemy_census_transition03.json` is a separate diagnostic retaining the
existing transition08 fixture's exact prefix through `transition_03`. It inserts
three native census/geometry snapshots before the unchanged strict hash03
check and assertion, then ends. Prior parity checks remain strict, and the
original transition08 fixture remains unchanged. This collects evidence at the
earlier failure boundary without relaxing acceptance.

The geometry03 run
[`20261002-204742_net_enemy_census_transition03_1`](../build/scenarios/20261002-204742_net_enemy_census_transition03_1/report.json)
was **FAIL** (164.6 s): snapshot0/client1 failed the actor-identity stability
recheck, so its provisional zero-enemy count cannot establish absence. Native
headers, completed epoch, gameplay/lifecycle and cache were stable; the artifact
does not identify which actor field changed. Causal reads for that sample were
skipped, and the trailing strict parity check never ran.

The [preserved geometry artifact](../build/scenarios/20261002-204742_net_enemy_census_transition03_1/native_enemy_census_transition03.json)
contains eight valid native/causal snapshots out of nine. Snapshots1/2 include
complete seven-BOX geometry for header30 on every peer. Across every peer's
captured geometry, the host's sampled activation point was outside all seven
regions and both clients' points were inside descriptor indices5/6, without
edge uncertainty. All seven source descriptors and inverse-matrix/extent bytes
matched across the eight valid captures. These are offline containment results
for sampled native float4 points, not instrumented native predicate returns.
All four saves were unchanged. The
[compact extraction receipt](../build/rig/native_geometry_transition03_summary.json)
retains the missing comparisons, key regions and byte fingerprints. Partial
evidence does not make this collection green, replace earlier failures, or
validate an activation-authority intervention.

The full unchanged-route acceptance subsequently **passed** (246.9 s) in
[`20261002-205416_net_host_transitions_statehash_acceptance_1`](../build/scenarios/20261002-205416_net_host_transitions_statehash_acceptance_1/report.json),
using DLL SHA-256 `62296DA088BDD4AC9023399A1F6FF763091FF7983D245983E2501E1BC54EDCA0`.
Removing only the added hash checks/assertions reproduces all 60 original
acceptance steps exactly. The audit confirmed 20 native loads after late join,
including the identical-location final reload, 23 arrival/binding checkpoints,
130 actual native puppet bindings (maximum sampled pose error 43.472 units), and
23 hash checkpoints containing 134 fresh peer samples with matching full
location/epoch and enemy/progress hashes.

**Every accepted enemy population was empty**, as were all 324 published hash
headers in the logs. This route pass does not prove nonempty native spawn application. At the previously failing `05/06` boundary,
logs show host header30 capture, both clients accepting leases and applying the
host point, with explicit qualified-controller holds between available leases.
The settled point `(54.072,-260,1950,1)` appears in host capture, both client
leases and native apply logs. These logs round to three decimals and are rate
limited; they do not establish byte identity for every response. All logged
cumulative qualification-unavailable counters stayed zero; unsupported native
controllers remained separately visible. Four disk saves were unchanged.
The [independent audit receipt](../build/rig/activation_route_acceptance_audit.json)
retains the route, serials, native binding/hash counts and scoped hook evidence.
This pass preserves the earlier failures and geometry limits; it is not a claim
of general authoritative spawning or renewed nonempty combat coverage.

The focused connected-client expiry control also **passed** (93.5 s):
[`20261002-210128_net_activation_lease_menu_1`](../build/scenarios/20261002-210128_net_activation_lease_menu_1/report.json).
Native host Start input made the combined menu/pause safety predicate unsafe
and stopped usable host samples while both runtimes remained connected and the
client stayed in safe gameplay. The artifact does not retain which gate changed
or prove a visibly open menu. After a wait exceeding one second, repeated
qualified header30 Hold observations increased the
hold counter from 505 to 749 while Apply stayed 388 and no new lease arrived.
A second native Start input restored safe gameplay, source 836 after baseline
source 752, and Apply 389.
Both peers retained epoch 1/full location `[5,6,0,1,1,0]`, and all four saves were
unchanged. This proves unsafe-gate source cessation and connected-client Hold/resume for
the qualified controller. The exact 500 ms deadline is established by portable
lease tests, not direct live deadline telemetry. It does not prove
disconnect/reconnect, nonempty spawn application,
missed short host crossings, or cache/stage/refill authority.

The separate ignored party-availability probe
[`20261002-194936_net_party_availability_probe_1`](../build/scenarios/20261002-194936_net_party_availability_probe_1/report.json)
passed its diagnostic checks: both native friend pointers were already zero on
first native `12/0B` entry before networking and remained zero after epoch1/2;
world18 party word stayed `0x12121200` and all four disk saves were unchanged.
This narrows the earlier isolated negative-control recovery failure; it does
not turn that failed recovery into a pass or establish a network regression.

## Checked native-list coverage regressions

The checked native-list coverage change passed the Release DLL build and
the [native-wave regression](../build/scenarios/20261002-201456_net_enemy_sync_waves_1/report.json)
(153.6 s), [nonempty hash/progress control](../build/scenarios/20261002-201730_net_statehash_nonempty_1/report.json)
(166.1 s), and [focused native-death detector](../build/scenarios/20261002-202126_net_native_census_death_control_1/report.json)
(102.4 s). All four disk saves were unchanged in both suites. The wave fixture
recorded two real second-wave enemies and six native client deaths; the
nonempty fixture's later empty-wave sample is not extra wave evidence.

`net_native_census_death_control.json` preserves the isolated negative fixture
through its failed native lethal/detection/guarded bit-restoration checkpoint
and ends there. It verifies the changed lethal/census path independently of
the known unsupported battle-room native-puppet reload. It does not close
that original recovery failure or the courtyard spawn-authority gap.

The newer activation-lease DLL subsequently failed the unchanged wave fixture
in [`20261002-210512_net_enemy_sync_waves_1`](../build/scenarios/20261002-210512_net_enemy_sync_waves_1/report.json)
(155.5 s). Before damage, the host had four object309 actors, while each client
had two matched object309 actors and two unmatched object311 actors. Equal
counts passed the original count-only readiness check despite this divergence.
All four selected host actors received native seven-point damage; an alive
host despawn/refill then introduced a fresh HP160 object309 actor before the
saved HP sample. The surviving matched client actors had HP153. The original
strict HP assertion correctly failed. The
[archived audit](../build/rig/native_waves_210512_fixture_audit.json) preserves
the sorted populations, native damage receipts, refill and unmatched rows.
This is a real population/lifecycle regression, not an actor-list ordering
artifact or evidence that a matched actor's HP update was lost.

`net_enemy_census_waves_provenance.json` is a strict provenance diagnostic.
Removing its inserted readiness wait and three `native_enemy_census` steps reproduces all 44
original wave steps exactly, including all nine 900 ms native movement pulses,
count-only readiness, host damage, and the original strict HP assertion. The
insertions capture one snapshot before the first movement pulse, three after
the original four-second settle and count readiness, and one after the saved
damage sample but immediately before its strict assertion. Each capture
includes checked native actors, controller/record metadata, rooted cache and
ordinary controller/table evidence where available. It retains extras and
unmatched rows. A repeated divergence must still fail; no expected-failure
exception changes the result. Read windows change timing, and incomplete
collection can fail before the original assertion. Neither settled timing nor
equal counts establishes identity parity.

The first native diagnostic attempt,
[`20261002-234801_net_enemy_census_waves_provenance_1`](../build/scenarios/20261002-234801_net_enemy_census_waves_provenance_1/report.json),
failed after 131.0 s at its first census, before movement or damage. Peer 2 was
still in its runtime-triggered load: native head/tail and inField were zero,
and its latest log was a client-issued transition. Its later copied log
records load completion, arrival and hashes. Peers 0 and 1 had complete stable
censuses. This is incomplete startup evidence, not a combat regression or proof
of native enemy absence. The fixture now waits up to 30 s for current completed
arrival, safe gameplay flags, valid native roots and actor candidates on every
peer before the unchanged census. It does not retry or discard census failures.

The [trace audit](../build/scenarios/20261002-234801_net_enemy_census_waves_provenance_1/native_trace_audit.json)
confirmed all selected spawn and lifecycle probes installed on all three PIDs,
and structurally complete recorded envelopes at their summary cutoffs, with no
recorded drops or native faults. Provenance remained incomplete: 23 fixed
wrapper returns and 36 disposal events were noncombat or had unavailable early
role/stamps/poststate. No object-309/311 production or combat removal was
recorded. This does not identify the prior strict wave failure's cause.

The revised diagnostic,
[`20261002-235825_net_enemy_census_waves_provenance_1`](../build/scenarios/20261002-235825_net_enemy_census_waves_provenance_1/report.json),
passed readiness and the first census, then all nine original movement pulses,
the original four-second settle and count readiness. It failed the second
census after **151.7 s**, before host damage or the original strict HP assertion.
All three host samples reported Python `PermissionError: [WinError 5] Access
is denied` during descriptor acquisition. Traversal had reached seven native
nodes and terminal handle zero, but host classification and after-rechecks
were unavailable. This is not an observed timer/identity change. All six
client samples were complete and stable, with two object-309 and two object-311
enemies at 160 HP. The
[collection audit](../build/rig/wave_235825_census_failure_audit.json) retains all
invalid observations and reconstructs the pending descriptor command without
running it against retired pointers. A future capture needs operation/argv and
traceback evidence before attributing the access-denied cause. All four save
hashes were unchanged, with no save violations.

The revised [trace audit](../build/scenarios/20261002-235825_net_enemy_census_waves_provenance_1/native_trace_audit.json)
again confirms all selected probes installed, structurally complete envelopes
at recorded cutoffs and no recorded drops/native faults. Each peer recorded
four requested object-309 entries (records 14–17) and two object-311 entries
(records 34–35) under header 115, type 2, six records, through callsite `3FE83F`
within a native tick/dispatcher. Initial nonzero return values lacked checked
status metadata; these do not establish creation incarnations. Checked alive
309 removals erased cache records 17 then 16 through `3FFD90` caller `41181D`,
followed by disposal through `3B45C0` caller `3BFD87`. HP stayed 160 and the
controller count stayed six. Subsequent requests for those records returned
zero repeatedly (654 host, 660 per client). No lethal/count-decrement events
were recorded for these removals. Their causes and the zero-return predicates
remain unresolved; the earlier strict native wave failure is still open.

Bounded static follow-up against the saved Steam Global PE explains several
observed phases without selecting their live cause. A nonzero ordinary fixed
return is a constructed actor whose base constructor zeroes status; separate
`411350`/`4112B0` operations assign stats later. Alive mode-2 removal erases its
cache ID without decrementing controller count or changing stage. `411800`
calls removal bookkeeping then tail-jumps disposal, preserving outer return
`3BFD87`; those two observations do not prove separate triggers. Ordinary
type-4 factory zero returns can follow its weight admission check
(`objentry+54 <= float[2A0F7DC] - float[2A0F830]`) or allocation failure.
The [byte-backed static contract](../build/rig/wave_fixed_return_removal_contract_20261003.md)
records exact RVAs, setup/removal predicates and limits. Live predicate
operands, allocation outcome and creation incarnation remain unobserved.

The collector now records up to eight batched-read failures per peer, including
stage, command argv, phase, completed field count, exception type, WinError,
filename and bounded traceback. It preserves the original exception and
incomplete observation without retries. The
[offline error controls](../build/rig/census_read_error_context_controls.json)
cover real local PermissionError/WinError 5, a child-reported tool error and a
malformed response; geometry 33, fixture validation 37 and auditor 37 controls
also passed. This improves diagnosis; it does not fix or explain the native
access-denied event. Probe/entity/log reads outside the batched reader retain
their earlier error reporting.

The next run,
[`20261003-001955_net_enemy_census_waves_provenance_1`](../build/scenarios/20261003-001955_net_enemy_census_waves_provenance_1/report.json),
passed all 48 steps in **170.6 s** with the same DLL. All three census windows
were complete. The settled native pack contained two object-309 and two
object-311 enemies on every peer, with zero unmatched bindings and three fresh
pre-damage samples per peer. Host-only damage reduced all four enemies
from 160 to 153 HP everywhere, satisfying the unchanged strict HP assertion.
The subsequent host lethal call produced four native death applications on
each client. All four real-save hashes stayed unchanged, with no violations.

This is bounded first-pack HP/death evidence. The captured second wave was
`[[], [], []]`, its lethal call hit zero actors, and the final battle-state
check was zero everywhere. It does not prove a second wave occurred or that a
barrier lifted. The [trace audit](../build/scenarios/20261003-001955_net_enemy_census_waves_provenance_1/native_trace_audit.json)
recorded four death-bookkeeping calls per peer, each enclosing one count
decrement: the controller count moved **6 to 2**, while stage stayed six.
Earlier alive removals had not decremented it. The installed probes recorded
no drops or native faults, but creation status and some caller metadata remain
unavailable, so provenance is incomplete. This pass neither explains the
retained earlier divergence nor diagnoses the intermittent access-denied
error, which did not recur. The post-damage census has one sample per peer;
eight retained geometry captures had moving player positions, so structural
geometry completeness does not certify stable positional predicates. The
[census audit](../build/rig/wave_001955_census_acceptance_audit.json) and
[damage audit](../build/rig/wave_001955_native_damage_audit.json) retain these
limits. Six host death announcements include two earlier alive-despawn policy
events, rather than six native kills. General wave, barrier and boss gates
remain open.

For a future run in the desktop live lane, scope creation tracing to the runner
process and its children; do not change global configuration:

```powershell
python -B -c "import os,subprocess,sys; raise SystemExit(subprocess.call([sys.executable,'-B','tools/scenario/run.py','tools/scenario/scenarios/net_enemy_census_waves_provenance.json'],env={**os.environ,'KH2COOP_SPAWN_TRACE':'1'}))"
```

The setting is inherited by all three `kh2ctl` launches and accepted only as
the exact string `1` when the DLL installs. The first attempt is recorded above.
The fixture does not assert trace readiness. Inspect every PID log for all four
spawn hook availability bits (fixed/generated/dispatcher/script) and lifecycle
`installedMask=31`, as well as unsupported callers, unavailable reads, interrupts
and drops. Per-event record/state lines and all eight cache chunks per available
phase must be present. Wrapper snapshots precede caller-owned bookkeeping;
enclosing tick completion is distinct evidence. Script TLS records dynamic
enclosure, even when a native tail jump leaves the dispatcher with an unavailable
DLL caller address. `roleAvailable=0` retains raw observations without a valid
role/serial stamp. Current `bindingEpoch`/netId correlation is not a creation
identity. Count-decrement events have explicit zero actor placeholders.

Audit saved PID logs offline, retaining the JSON report beside the run:

```powershell
python -B tools/scenario/trace_audit.py path/to/host.log path/to/client1.log path/to/client2.log --output build/rig/native_trace_audit.json
```

Exit 0 means the supplied recorded envelope and counter cutoff have no identified
coverage/structure gaps; it is not KH2 acceptance or proof every native producer
was observed. Exit 1 preserves incomplete provenance; exit 2 is usage/file I/O
failure. The report separates structural completeness from provenance
completeness and retains unknowns, dropped/interrupted events, partial hook
installation, cache chunk omissions, unknown ordinary record indices, missing
lifecycle record/classification provenance, unavailable generated positions and
summary-tail coverage. All PID logs and
partial census artifacts survive failed runs. Bounded queues, missing shutdown
drain and unhooked paths remain limits even for a passing audit.

The Windows-only headless trace tests are separate from gameplay:

```powershell
cmake --build build --config Release --target kh2coop_spawntrace_test kh2coop_lifecycletrace_test
./build/Release/kh2coop_spawntrace_test.exe
./build/Release/kh2coop_lifecycletrace_test.exe
python -B -m unittest discover -s tests -p test_trace_audit.py
```

Spawn/lifecycle tests passed 38/24 checks under `/W4 /WX` and Windows
AddressSanitizer using synthetic originals and test-owned memory. They cover
original-call preservation, exception propagation, scope recovery, queue loss
and foreign-thread affinity, not installed hooks or live provenance. The auditor
passed 37 controls and rejects the archived untraced wave logs. Fresh build/test
scope is recorded in the
[expanded offline receipt](../build/rig/native_trace_expanded_offline_receipt.json).

`net_activation_native_nonempty.json` is a separate 78-step courtyard control
at full location `[5,6,0,1,1,0]`. Its first native attempt is an overall failure
with passing movement, negative activation and populated-pack checkpoints.
Clients enter the measured header30 BOX geometry by native input while the
host stays outside; checked native census and fresh hashes must remain empty.
The host then enters and all peers must expose exactly five bound living
object302 actors, with independent native-list and geometry evidence. Native
host damage must reduce each of the same five netIDs by exactly three HP,
with matching client populations. Native death logs must verify HP crossing
to zero for every selected netID on both clients, followed by empty hashes in
the same epoch/location. No transform writes supply movement. Each actor's
250 ms direction probe must pass before a computed 100..2000 ms input pulse;
uncertain movement fails without a fallback. Captured geometry is checked
with offline float32 containment, not native predicate instrumentation.

Offline checks passed for all 36 fixture structures, 33 mocked geometry,
identity/deadline and provenance controls, and 12 fixture expression controls.
The latter evaluated all 78 steps against synthetic state and archived geometry,
matched all four regexes against archived native logs, rejected replacement
netIDs/unchanged HP/extras/empty positive populations/unstable activation points,
and confirmed that the unchanged wave assertion still rejects the archived
failure. Receipts:
[native reader controls](../build/rig/native_geometry_offline_controls_receipt.json)
and [fixture controls](../build/rig/native_activation_fixture_offline_controls_receipt.json).
These checks establish tooling behavior only. They do not calibrate native
movement, establish a live nonempty pass, or prove general spawn authority.

The [first live run](../build/scenarios/20261003-003531_net_activation_native_nonempty_1/report.json)
failed after **159.8 s** at step 63. All three bounded movement probes and
computed pulses passed. Two complete stable captures put both clients safely
inside the measured BOX while the host remained outside all seven regions;
independent native censuses and two fresh matching hash samples remained
empty. After host entry, two fresh hash samples and two complete native censuses
showed exactly five matched object-302 Shadows at 20 HP on every peer, with
zero unmatched actors. However, the second positive geometry capture reported
`pointStableDuringCapture=false` for the host. All sampled containment results
were safely inside; this failed positional stability, rather than demonstrating
an outside host. The assertion now names its combined stability/geometry gate
more accurately, with its expression unchanged. Host HP/death checks were not
reached. All four real-save hashes were unchanged; no save violations occurred.
Retain the unstable capture, the overall failure and the distinction between
sampled float32 geometry and native predicate execution.

`net_enemy_sync_waves_acceptance.json` strengthens the two-pack check in a new
fixture while preserving all 44 original steps and their order. It requires
the first pack to contain two object-309 and two object-311 enemies, then an
actual second pack of two object-309 enemies with fresh, unique current binding
IDs disjoint from the first. Both packs need two fresh matching native hash
samples, independent checked censuses, exactly seven HP of host damage and
positive-to-zero native death logs per ID on each client. Empty second packs
cannot pass. The [offline receipt](../build/rig/waves_acceptance_fixture_offline_receipt.json)
records validation and 27 controls, including empty/wrong-type/reused/duplicate
IDs, changing bindings, wrong epoch/location and receipt-only death negatives.
Independent review demonstrated that `comparisonValid` alone did not establish
current native identity or HP: it validates the logged hash before mismatch
arrays are computed. The three strict census assertions now require both
mismatch arrays empty and unique bidirectional native address/objectId/HP
joins to the latest published living rows. Controls reject changed addresses,
types or HP even with a valid stale hash. IDs remain current bindings rather
than creation identities, and battle-byte/controller counts do not prove barriers.

Its [first live run](../build/scenarios/20261003-005101_net_enemy_sync_waves_acceptance_1/report.json)
failed in **152.0 s**, before host damage, at the first independent census.
Two fresh matching native hash samples showed the expected first-pack bindings
and 160 HP. Four client census samples were complete and stable; both host
descriptor batches failed to start `kh2ctl`. The preserved tracebacks end at
Python `_winapi.CreateProcess` with WinError 5, rather than a native memory API.
Defender events 1116/1117 match that command line and report
`Trojan:Win32/Commando.A!ml` detection/removal. The
[startup audit](../build/rig/waves_005101_child_creation_audit.json) retains the
exact argv/tracebacks and detection evidence. The detection's correctness is
unestablished. Further live runs are held while offline observer work continues;
no exclusion, permission change or executable renaming was used. All four
real-save hashes stayed unchanged, with no save violations. The earlier
descriptor failure lacks this fresh correlation and remains separate evidence.

### Scoped native decision observers: offline candidate

The opt-in trace now observes genuine admission `3A1F00` and type-4 allocation
`152430` returns within each existing wrapper. Raw float bits, actual size/RAX,
scoped call counts and effective coverage distinguish executed admission
rejection from allocator null without replaying either helper. Budget snapshots
remain boundary samples. Missing, remapped, partial, repeated or interrupted
observations retain an unknown/ambiguous outcome; constructed status-zero actors
do not hide otherwise available raw decision facts.

Removal predicate `3DAC30` similarly records its genuine result plus scoped
`3B4420` and `3CE550` child results. Full stable coverage and exact child counts
distinguish five normal branch outcomes; checked operands do not substitute for
executed returns. The predicate finishes before later removal/disposal, so it
does not enclose those later events. Existing lifecycle kinds keep their IDs;
removal-predicate is kind 5, with parent/script/auxiliary bits 32/64/128 and full
installation mask **255**. Earlier mask-31 logs retain their original scope.

The allocator detour fills existing POD only, with no diagnostic allocation,
logging, role callbacks or returned-pointer reads. Originals execute once;
foreign callers pass through and native exceptions propagate after scalar TLS
cleanup. Full byte/caller guards, independent availability and bounded loss
counters remain explicit. Additional detour overhead and live relocation are
unmeasured. Shutdown retains the existing requirement for a quiescent owner.

Windows Release and ASan builds passed; synthetic spawn **75**, lifecycle
**59** and actual-source native-hit **42** controls passed in both configurations,
with no sanitizer findings. The saved-log auditor passed **59** controls
(37 prior plus 22 predicate controls) and gives historical logs no new branch
claims. Initial sanitizer launches lacked the installed MSVC runtime on PATH;
adding its existing directory only to each test process environment resolved
that dependency. No global configuration changed. Cross-reviews accepted both
observers and the removal auditor. The new DLL has **no live hook installation
or gameplay validation**; the preserved earlier native results used the prior
DLL. See the [predicate receipt](../build/rig/predicate_trace_offline_receipt_20261003.json).

## Known limits

- `boot` loads whatever slot the save list opens on (the last used one) and
  doesn't pick a slot. The pixel checks assume 1920×1080.
- The hang check needs the room to be live: it doesn't cover the title
  screen or a load that never finishes (those surface as step timeouts).
- Not proven live: a save attempted from the in-game save menu. The menu
  runs as a scheduled task (`0x1512B0`), so there's no clean call to open it.
  The guard is proven against every API on the game's save path (static
  trace above, plus the self-test through the exe's own imports). James
  authorized one guarded in-game save attempt on 2026-10-04; it has not yet run.
  The guard's save-menu acceptance remains unproven.
