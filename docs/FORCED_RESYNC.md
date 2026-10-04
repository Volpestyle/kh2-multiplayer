# Forced world resync

The current protocol 9 candidate adds exact ordinary-record content
to the fresh native-state transaction introduced in protocol 8. AvatarBridge
remains 2, WorldBridge is 11 and CaptureChannel remains 1. All components must
be rebuilt together. Older protocol peers or mappings cannot validate this
candidate. The dated protocol-8 native results below retain their original
scope; they are not native validation of protocol 9.

## Native record-content candidate, 2026-10-04

The actual native producer requires coverage **255**: full checked progress and
population plus a complete bounded ordinary-controller catalog and current
actor-to-record membership. Each definition retains its entire 44-byte header
and ordered array of 64-byte records. Portable comparison uses the reviewed
layout identity, six explicit u16 location fields, group/type/header ID/count,
native record index and full raw record ID/content. Exact projected header and
ordered array bytes must agree; hashes alone cannot authorize a match. Only
header byte +0xE is excluded from peer content compatibility, while its raw byte
remains in the snapshot and within-process drift checks. Definition order is
canonicalized in the fingerprint; native record order is significant.

The generic codec retains synthetic coverage **127** with no record witness and
sentinel references. The actual consumer rejects it. A complete native catalog
is capped at 64 definitions, 256 records per definition and 1,024 total records,
with the existing 60,000-byte snapshot limit. Unsupported/partial reads,
duplicates, aliases and unresolved associations cannot become a matching prefix.
Current pointers, network IDs and content hashes are not controller generations
or creation-incarnation identities. The declared layout hash is compatibility
provenance, not a digest of loaded executable memory.

Before the first HP/death application, the consumer qualifies the whole current
catalog and population against the cut, with full catalog/census bookends.
Every desired living HP must be positive and within the qualified local/source
maximum; an already-sampled native HP above max also rejects the entire batch.
The target reread checks actual HP/maxHP/type. The final POD leaf repeats selected
definition bytes, native association and positive bounded HP/maxHP/type/name,
then checks current generation/delivery, role, full location, load/transition,
safe gameplay and arrived epoch immediately before the store or native call.
The lethal delta must equal the qualified positive sample HP. These are sampled
endpoints, not an atomic interval or continuous-lifetime guarantee.

While fenced, no point/index fallback binds an actor. Waiting blocks writes;
Exact denotes the installed exact-only strategy, and a transient missing or
incompatible population clears bindings while retaining the original deadline.
Terminal failures leave Failed through result cleanup; restored bytes alone
cannot reopen the fence. A later native change can still invalidate the batch
after earlier writes, so partial application is possible. An unknown lethal
outcome or failed post-lethal read/census leaves Failed without retry or rollback.
Checkpoint is observation-only: it clears write bindings but preserves its
two-frame observation witness, and performs no HP/death repair, progress write
or additional reload. Masked Bootstrap progress applies under its separate
policy before target-room initialization and local record qualification.

Offline evidence, with zero failures and empty test stderr in both Release and
Windows MSVC ASan configurations:

| Component | Executed checks per configuration | Exact evidence |
|---|---:|---|
| Portable record comparison | 154 | [Fixed reader/common receipt](../build/rig/native-record-content-reader-implementation-fixed-20261004-01/receipt.md) |
| Actual native reader/trace harness | 276 | [Fixed reader receipt](../build/rig/native-record-content-reader-implementation-fixed-20261004-01/receipt.md) |
| Wire codec and transaction harness | 257 | [Release receipt](../build/rig/native-record-content-wire-implementation-20261004-01/receipt.md), [Windows ASan receipt](../build/rig/native-record-content-wire-asan-20261004-01/receipt.md) |
| Actual consumer/write guards | 272 | [Final guard receipt](../build/rig/resync-native-record-write-guard-controls-20261004-01/receipt-final.md) |

These are executed check counts, including repeated setup checks, not independent
scenario counts. Independent reviews are READY for the bounded
[common component](../build/rig/native-record-content-independent-review-20261004-01.md),
[fixed reader](../build/rig/native-record-content-reader-fixed-independent-review-20261004-01.md)
and [fixed consumer](../build/rig/native-record-content-resync-consumer-fix-independent-review-20261004-01.md).
The initial reader/consumer HOLDs, opaque abort, packet-fixture failure and
intermediate 233/269-check results remain preserved; passing final controls do
not rewrite them. Final guard source is EnemySync SHA256
`8E7BBFF246379030578793C42569B45F1E134960C9CC95D2BA3034E77E685F03`.

**Bounded native acceptance, 2026-10-04:** the unchanged 89-step living-population
fixture passed in **201.7 seconds**, on one PC over loopback:
[`20261004-043836`](../build/scenarios/20261004-043836_net_forced_resync_shadows_population_1/report.md).
One Bootstrap targeted both original friends; loads changed 3 to 4, with
Converged checksMask63 on frames 6100/6101 and 3633/3634. Matching relay and
host-runtime terminals corroborate cut3814/SHA/fingerprint and original targets.

The host captured coverage255, all **10 definitions/26 records** and five current
actor references. Both friends performed five unique positive HP stores from
native20 to requested17 under Exact content qualification. The ten returned-store
receipts explicitly have no immediate readback; each binding subsequently has
actual HP17/max20 on both qualified observation frames. Two fresh all-peer hash
samples and two independent complete censuses preserve the original IDs1-5,
object302/type4, damaged HP17 and progress. The [content audit](../build/rig/native-record-content-native-content-audit-20261004-01.md)
and [population/capture review](../build/rig/native-record-content-population-native-independent-review-20261004-01.md)
are independently READY. Catalog completeness is source-conditioned capture
evidence; canonical snapshot bytes were not logged for independent digest
reconstruction. Store frames/loads are absent from the receipt schema.

The receipt-only instrumentation leaves all guard/write leaf bodies unchanged;
current EnemySync SHA256 is
`04298BAB3B948BC1E04F3E21F5D18D442D3638F5BCB0F6BBBF01F7C3D54F17AB`.
The complete Release build, eight affected regression suites and the current
272-control Release/Windows ASan harness passed. All 101 prelaunch inputs and
four protected saves remained unchanged through terminal verification; the
runner and its seven owned game/runtime/relay processes exited, and the rig is
idle. All three OS-loaded DLL-copy paths match staged disk SHA256
`3A42F44A79C0FB8628DB46A0E03283C79ACBF7BBF5241457945079202777F08D`;
this is not an in-memory code digest.

The three inspected final PNGs show rendered courtyard gameplay; overlapping
actors mean screenshots cannot independently prove count or numeric HP. The
post-resync census has geometryComplete=false, so positive geometric stability
remains unclaimed. Progress applied zero changed spans/bytes, with the existing
apply-only personal boundary. Automatic collection labels its first report
complete, while its aggregate remains partial with one cadence suppression.
This validates exact content-qualified correction after a living Bootstrap;
the prior outside-region first-rejoin FAIL, zero completed ten-cycle repetitions
and creation-disabled state remain. Controller lifetime, same-value/address
reuse, foreign/fiber mutation, global pending-work, creator closure, dead
reconstruction and physical remote recovery remain open. The next gate is
safe reconstruction of an absent surviving pack outside initial activation
regions, preserving the strict original five-Shadow and reconnect criteria.

## Surviving-pack preparation, 2026-10-04

### Historical activation replay: current default-off policy

The integrated candidate requires both `KH2COOP_SURVIVING_PACK_PREPARE=1` and
`KH2COOP_SPAWN_TRACE=1`. For one exact type-2 controller whose snapshot contains
all five living Shadow records and zero deaths/tombstones, it temporarily feeds
the host's **recorded first-emission float4** to the client's existing scheduled
original update. There is no synthesized BOX point, extra native call, branch
patch, marker rewrite or synthetic-kill cleanup. B1 creator admission, the
owned gateway and loader-owned spatial bypass are parked alternatives.

Replay still runs only inside a host-origin ResyncPlan. ClientHello rejoin sends
cached world state; it does not itself invoke the snapshot producer or replay
receiver. The integrated host-only notice path can now submit the existing
ResyncRequest after an enemies-field DesyncNotice, but is default-off and requires
exact `1` for all three environment variables: `KH2COOP_AUTOMATIC_RECOVERY`,
`KH2COOP_SURVIVING_PACK_PREPARE` and `KH2COOP_SPAWN_TRACE`. Run134727, before
this integration, passed the original rejoin
checks at5/5/5 but had no replay receipts; it cannot validate this method.
Run143318 passed the120-step host-outside5/0/5 control. Run143903 then passed
the142-step forced experiment: one slot1 Bootstrap restored5/5/5 atHP17/max20,
with33 completed historical returns followed by120 completed returns on live
input and reconciled1. The original host point receipt joins receiver
firstUpdate55413; no independently retained client raw-point or snapshot-SHA
recomputation is claimed. Exactly one additional Friend1 load4->5/transition3->4
and native/relay/runtime completion were checked; all four saves and98 sealed
inputs stayed unchanged. See [the scenario results](SCENARIOS.md#historical-activation-replay-single-cycle-2026-10-04).
The notice policy uses combined `requestedResync_ || resyncPlan_` busy state and
two bounded pending friend slots. Dedupe captures session/host, target connection,
epoch and exact fields; identical pending notices coalesce and changed fields
explicitly supersede. A terminal permits deferred drain outside receive callbacks,
after revalidating the captured binding, delivery, full room and roster. Both
immediate and drained submissions require running, admitted, non-invalidated,
world-ready runtime ownership. Submitted receipts join the actual generated
transaction key; failed attempts stay consumed. There is no implicit retry,
replacement target or deadline reset.

The generic native hold is always on for every client's outgoing hit claims,
independent of automatic/replay opt-ins. It checks the current generation and
ordered delivery before any old release state can publish. Release requires a
complete framed replace-manifest or checked Bootstrap, trusted HP/maxHP for
every living entry,
one unique current native binding each, no missing/extra combat rows and actual
positive native HP/maxHP equality through repeated reads and scope/manifest/HP
bookends. Unknown or empty universes stay held; scope or manifest/HP changes
rearm the hold. A replay also needs its exact reconciliation and LiveHold/Verified
phase; claim release need not wait for the separate 120-update acceptance check.

`[automatic-resync]`, `[runtime-world-cause]`, `[client-claims]`/`[client-claims-row]`,
`[resync-activation-key]` and `[load-cause]` receipts retain actual producer
identities, transactions where available, full scope and native load/transition
facts. Ordinary claim receipts lack a session/request identity in WorldBridge and
need an exact runtime identity join. Ordinary load receipts retain admitted
envelope scope; identifying that load as cached rejoin still needs independent
runtime/relay admission evidence. Runtime and native interval seals expose their highwaters/state/gaps;
shutdown or EOF alone is insufficient. After the final independent native census,
read the current runtime seal watermark and require a strictly newer complete
seal with full suffix coverage and no pending/requested/planned work. Native
seals require their own coverage checks; process clocks are separate domains.

The full Release build and **2,298 affected offline checks pass**, including
59 notice/generator, 44 claim-hold, 66 load-receipt and 365 canonical native-hit
checks. The [combined receipt](../build/rig/automatic-recovery-root-integration-20261004-01/cpp-validation.json)
retains executed results; private sanitizer checks are scoped separately.
The first combined build failed on a test adapter declaration; the corrected
explicit-argument calls retain all assertions and pass the final build/run. No live
automatic result is claimed. Both original and forced-outside ten-cycle gates
remain open. Proposed v2 natural/resynced episode accounting preserves v1 and
historical FAILs and still needs James's gate sign-off.

The recorder copies the exact native RDX input inside the qualified host update
and joins it by update identity to the first actual nonnull fixed-wrapper return
and normal original completion. Pending actor metadata stays explicitly pending;
the returned update must show the exact first record cache attachment. Natural
controller initialization may be observed while the network role is off. Its
first qualified host update may bind once to birth load or exactly birth+1,
with unchanged transition/NOW, native pointers and full definition; it cannot
restamp a later update or revive a refused record. Missing/ambiguous capture or
subsequent lifecycle/content mutation leaves historical input unavailable.

The actual host prestate is counts5/5, empty cache, flags2, cooldown0, stage0,
header+E0. Counts are enrollment/death state, not an emitted-so-far counter.
Fresh client prestate must match under the explicit header+E0/1 exception:
the saved type-2 body sets0 to1, then both paths call the same emitter. Both raw
values remain evidence; normalization does not authorize a native write. Exact
ordered record/catalog content and unique current local association remain
required. This limited policy does not establish historical replay equivalence.

An optional **HARP/v1 1128-byte trailer** carries historical metadata under the
snapshot SHA. Legacy snapshot bytes remain unchanged; historical metadata is
excluded from the native-state fingerprint. Protocol9/WorldBridge11 and the
existing full native coverage255 requirements remain. Absence of a usable
trailer cannot silently select a guessed or live point as historical evidence.

The receiver permits at most **512 actually completed selected-controller
original updates**, within the unchanged absolute **30s** transaction deadline.
Lease-copy attempts, holds and global update-sequence gaps are not completed
updates. The per-controller completion table has64 fixed slots, never evicts,
and becomes unavailable on overflow. Expected selected emissions and deferred/
pending readiness may progress under the same deadline; positive duplicate,
unknown or conflicting records fail. Partial native creation at timeout stays
a truthful failure, with no retry or undo claim. Claims for the selected set
remain held until exact full-set HP reconciliation completes. A fresh Bootstrap
may reset its transaction state but never clears owned-gateway tombstones.

After five-ready and reconciliation, restore the live host lease and require
**120 actually completed selected-controller originals**, continued5/5/5 exact
membership and HP17/max20, no extras/deaths/tombstones, and final controller
flags/cache/count/stage agreement. Compare dynamic accepted-region bit3 after
live input resumes. General death recovery, waves, barrier/music/room-script
parity and original replay equivalence remain unqualified; unavailable battle
observables must be named rather than inferred from the location's `btl` field.

The current98-step single-cycle fixture and its two pre-reconnect observer
failures are recorded in [SCENARIOS.md](SCENARIOS.md#historical-activation-replay-single-cycle-2026-10-04).
Neither run reached recovery. The previous first-rejoin5/0/5 failure and no
completed ten-cycle acceptance remain. See the
[precondition review](../build/rig/activation-replay-precondition-review-20261004-01/decision.md),
[bounded marker review](../build/rig/header-activation-consumers-review-20261004-01/decision.md)
and [recorder review](../build/rig/host-first-emission-independent-review-20261004-01/review.md)
for the policy's evidence boundaries; the latter predates the per-controller
completion-table correction.

### Earlier preparation and occupancy evidence

The earlier offline addition used `KH2COOP_SURVIVING_PACK_PREPARE=1` to prepare
the exact five-record Shadow intent. It does not dispatch an emitter. The
receiver retains the immutable cut/deadline, five sticky pending/outcome slots
and a terminal tombstone through transport cleanup. An empty ready census
cannot imply complete raw/deferred/cache/pending absence. Existing full-set
matching remains responsible for HP reconciliation and two-frame convergence.

The opt-in installs pass-through cancellation hooks at ordinary-controller
construction, initialization and teardown. They invalidate tickets before
native mutation, including same-address/same-byte reinitialization; unwind,
partial installation, overflow and shutdown poison their namespace. This is
coverage of three known entries, not controller-incarnation or creator exclusion.
The first ticket is attached only after the target's fresh arrived load, so
normal Bootstrap initialization does not invalidate a pre-load ticket.

For this first scope, admitted material post-cut HP, death, manifest, progress
or event changes end preparation as Unavailable before subsequent enemy writes.
Equal HP/full-manifest replay and progress-version-only replay remain valid.
This avoids claiming the old snapshot fingerprint after newer HP was applied;
completion during changing combat requires a separate settlement barrier.
Default-disabled behavior and the existing wire/ACK versions are unchanged.

This addition is not part of the first frozen 89-step native input seal.
Release/Windows ASan verify the production receiver over owned native memory
and the actual mutation pass-throughs. A later installed-hook run is described
below; the original first-rejoin FAIL and all recovery acceptance criteria
remain. Offline evidence:
[`surviving-pack-integration-20261004-01`](../build/rig/surviving-pack-integration-20261004-01/receipt.json).

The subsequent sampled occupancy join reads active and deferred lists before
combat filtering, checks every retained node twice, and retains the full
256-slot appearance cache and selected controller state. The receiver resolves
the selected definition by content, checks duplicate reference counts before
folding masks, and joins actor/object/status/controller/record/index plus
object ID/type to independently checked ready membership. Both cache room tags
must match; every nonzero cache ID must resolve uniquely in the whole catalog.
Contradictory combat metadata, deferred actors and pending/unclassifiable rows
remain held. Null-provenance noncombat exclusion is sampled class evidence
within a conservative native type envelope, not rooted object-table identity.

Release and Windows ASan passed115 reducer and324 receiver controls; the raw
reader separately passed347 in each configuration. The successful full Release
build includes the join. These counts include setup assertions, not independent
gameplay scenarios. Input snapshots, build failures and successful terminal
streams are retained in
[`surviving-pack-occupancy-integration-20261004-01`](../build/rig/surviving-pack-occupancy-integration-20261004-01/terminal-checks-partial-node-final.json).
Bounded `[resync-pack]` diagnostics expose the sampled masks and remaining
limits. Global pre-link coverage and native execution authority remain missing
even for a complete five-ready set; no native emitter is called.

### Parked added-dispatch, B1 and loader-installation route

This section preserves the prior branch proof and controls. Its creator and
patch-admission requirements apply to that added-dispatch alternative, not the
current historical-input policy's normal scheduled update. The owned generator,
gateway and preparation remain built but off; no branch installation is part of
the current recovery path.

The supported creator investigation binds `42B130` to a synchronous script
VM call and its normal scene/actor scheduling paths. The apparent registration
at `2BC03C8` is PE unwind metadata. Immediate/exit scripts inherit callers and
can nest; this correction does not prove continuous exclusion from factory
entry through active publication and subsequent record/cache attachment. See
the [bounded handler decision](../build/rig/shadow-handler-owner-20261004-01/decision.md).

The next finite review binds the actual script dispatch through table755370
and interpreter call41C4FA to `42B130`'s factory call42B16D. Its argument can
request unflagged object302 without either ordinary wrapper or C/R/cache
attachment. Twelve direct factory callers are enumerated; lower-constructor
bypasses remain separately bounded. A `3DF930` entry/return bracket would add
negative cancellation evidence, not exclude an entrant after an empty sample.
This is saved-code reachability, not an observed script spawn or foreign race.
See the [creator closure review](../build/rig/native-selected-creator-closure-20261004-01/decision.md).

A bounded follow-up exhausts all256 byte indices of the saved `3F8170` table:
none resolves to302 or a dynamic remap under the inspected lookup semantics.
This removes the earlier metadata-index uncertainty for that saved-image path;
it does not prove current in-memory table identity. The remaining constructor
frontier includes data-derived secondary actors through3D7C70/3EF8B0, with a
different native class/kind. Raw ID, class and ordinary record membership remain
distinct. See the [byte-domain review](../build/rig/native-creator-bypass-byte-domain-20261004-01/decision.md).

The secondary mapper is data dependent: a valid readable mapping can return302,
although the retained native census did not capture the table needed to show
that it currently does. Its kind12 constructor is a different class from the
ordinary Shadow constructor; raw302 before publication still cannot be excluded
on that basis. See the [typed mapping review](../build/rig/native-secondary-creator-id-domain-20261004-01/decision.md).

The installed object-entry table is now retained: object302/M_EX020 has
secondary group+4E=0, while81 other records have nonzero groups, including Sora,
Donald and Goofy. This narrows the selected Shadow's own secondary path; it
does not exclude secondary creation by other parents or bind current loaded
tables, equipment choices or their lifetime. See the
[installed-asset receipt](../build/rig/native-shadow-script-asset-inspection-20261004-01/receipt.md).

Blanket null-return fencing is rejected. Saved bytes show that552430 forwards
its incoming ECX to the factory, including a possible unflagged302, then
dereferences the result without a null guard. The earlier decompiler-based
constant-zero classification in the frozen caller matrix is incorrect. Both
secondary allocation paths also dereference null, and denying42B130 consumes
the VM instruction rather than retaining its intent. These are native failure
semantics, not a safe deferral mechanism. See the [admission feasibility review](../build/rig/native-resync-creator-admission-feasibility-20261004-01/decision.md).

The later552430 entry review found no saved direct/literal caller or exported
entry; the apparent pointer is its PE unwind record. Its unsafe return behavior
is conditional on entry, not evidence of a live creator. This bounds that
ingress under the declared saved direct/literal-entry model without claiming
universal dead code. Known secondary and VM failure semantics still reject
blanket null fencing. See the
[entry-binding review](../build/rig/native-creator-552430-owner-binding-20261004-01/decision.md).

The parked admission candidate is the existing synchronous scene callback at
14FDFC. It preserves sibling script/event/equipment requests while the current
callback runs, but positive B1 remains unavailable. A prior suspended creator or
a synchronous resource/CRT callback can still enter a conflicting creator. A
queue cannot safely defer a callback on which the owned call itself depends:
waiting can deadlock, while returning early consumes native intent. That route's
unresolved prerequisite is ownership of the resolved malloc57B7A8/_callnewh57B7B0
runtime state and handlers, alongside the retained callback and prior-span
obligations. A null-handler sample does not reserve that state. See the
[admission decision](../build/rig/native-creator-admission-next-mechanism-20261004-01/decision.md).

One emitter invocation also permits several attempted records: an actual null
wrapper return at3FE98C skips cooldown assignment and advances the record loop;
3FEB50 can initiate a further scan. A dispatcher-return tombstone therefore
cannot stop later attempts inside that invocation. The parked conditional
design uses only the taken no-region edge3FF11E to3FF157, preserving native
cooldown, admission, attachment and truthful bit3clear behavior. It requires
continuous relevant creator ownership and a stop before another owned record
attempt. See the [execution design](../build/rig/native-surviving-pack-execution-design-20261004-01/design.md).
That added-call production seam and creator boundary remain unimplemented and
unqualified. Its missing32/256/disabled-creation disposition and the original
first-rejoin failure remain historical results; they do not prescribe the current
historical-input experiment.

The per-attempt stop now has a finite offline modification contract: three
branch gates bind the exact live emitter frame, stop an owned null result through
the existing epilogue, and preserve the native delay8 store before stopping a
nonnull Pending result. The original fixed-wrapper CALL remains in place;
natural invocations retain their branches. Cancellation revokes permission while
retaining frame identity until actual return/unwind. Independent review finds
that this frame association can reject inherited TLS from nested or other live
fiber frames without claiming uninterrupted execution. The complete private
A/B/C component now has independent Release/Windows ASan replays, each with426
passing checks. Actual Windows unwind checks and executed SEH faults cover all
161 island instruction starts, alongside17 native epilogue contexts, non-LIFO
fibers, cancellation, stage rescans and full-RAX/AL0 returns. A reserves the
actual record before the unchanged wrapper call; B stops an owned null return;
C stops after the original cooldown store, including delay0 paths.

`NativeOwnedEmitterCode.hpp/.cpp` extracts that same generator and280-byte POD
into the inject target. Independent Release/Windows ASan replays each pass455
checks: the426 machine controls plus29 generator guards/default controls. The
finite all-or-empty plan checks extents, overlap, relative reach and common-base
unwind alignment. It does not dereference addresses, install patches, register
tables, call native code or supply creation authority. The state is externally
serialized; it is not a concurrent admission ledger. Production installation,
complete content, creator exclusion and reconstruction remain unqualified. See
the [A/B/C machine review](../build/rig/native-owned-emitter-abc-independent-review-20261004-01/review.md),
[shared-generator review](../build/rig/native-owned-emitter-code-independent-review-20261004-01/review.md),
[root integration receipt](../build/rig/native-owned-emitter-code-root-integration-20261004-01/receipt.json),
[owned-stop contract](../build/rig/native-owned-emitter-null-stop-design-20261004-01/contract.md)
and [independent frame review](../build/rig/native-owned-emitter-frame-review-20261004-01/review.md).

The internal direct gateway is now versioned and compiled into the inject target.
It publishes the actual pre-CALL frame, calls the supplied original dispatcher
without an intervening helper, preserves full dispatcher RAX, and retires live
ownership at the first return instruction or actual MASM SEH unwind. One linked
component admits one outstanding, strictly increasing invocation; rejected calls
leave the live state intact. Release and Windows ASan each pass512 controls,
including34 actual gateway unwind PCs and five published-range faults. An
independent compiled-MASM boundary probe passes28 assertions. The staged DLL's
149-byte gateway and handler metadata exactly match the tested component.
There is no production caller; target, content, lifetime, serialized cancellation
and B1 remain external prerequisites. See the [gateway review](../build/rig/native-owned-emitter-gateway-independent-review-20261004-01/review.md)
and [linked DLL check](../build/rig/native-owned-emitter-gateway-root-integration-20261004-01/linked-gateway.json).

The default-off preparation component now verifies the loaded810-byte emitter,
60-byte parent unwind and actual OS function tuples before allocating one near
20-KiB block. It initializes RX code, RO sorted function tables/chain metadata
and a separate RW inactive state before registration and readback. A true
preparation result means `PreparedCommitHeld`; disabled/failed calls publish no
view. Stop rejects future use without clearing a live state or freeing retained
code/metadata. Release and Windows ASan each pass460 controls, with an independent
40-assertion Windows allocation/registration/lifetime check. Positive public
preparation in KH2 has not been tested. There is no production branch writer:
suspending a thread census does not prevent an in-flight/new thread from entering
during a partial patch. Mechanical commit, B1 and recovery remain held. See the
[preparation review](../build/rig/native-owned-emitter-install-independent-review-20261004-01/review.md).

The saved ordinary Shadow handler has no NPC-style direct five-frame bit3
removal test. Its real update/removal chain can run actor scripts, and the native
script query42DBB0 reports bit3 clear as0 and set/null-controller as1. The actual
installed Shadow BDX is now retained and decoded with the pinned official
OpenKH parser. No bank1/index239 query encoding occurs at any aligned word pair;
its leave decision instead tests global force-leave bit7/no-leave bit2, then
player distance/culling, with initial thresholds1000/2000. The two leave/dead
calls bind to42C810's fade/removal request. Loaded script identity, room scripts,
actual action delivery and three unprovided native auxiliary action cells remain
unknown; this is not outside-region liveness acceptance. Preserve truthful bit3
and native removal/cache/count behavior. See the
[native handler review](../build/rig/native-shadow-outside-region-semantics-20261004-01/decision.md)
and [installed BDX review](../build/rig/native-shadow-bdx-semantics-20261004-01/decision.md).

Complete mode/filename/resource-route telemetry is required only when the
execution argument relies on excluding those routes or binding their targets.
It is not an intrinsic requirement to preserve the unmodified native chain
under a separately qualified ownership boundary. D5 permits a new host-defined
living set without historical constructor timing or a positive BOX result.
The next work targets actual creator admission and per-attempt stopping rather
than a larger observer budget. See the [resource-to-execution decision](../build/rig/native-resource-to-execution-gap-20261004-01/decision.md).

The default-off scenario `secondaryBindings` collector now samples current
equipment/map bindings under the existing census deadline. Its fixed source
passed128 affected controls and independent replay, including rejecting consumed
bucket/lifecycle contradictions and known parent-scope conflicts. The separate
14-step courtyard diagnostic passed in153.4s, with independently replayed raw
bytes for522 current slot results across three peers. None is raw302; each peer
has1900 primary rows,81 nonzero-group rows and535 item records. Live went/item
declared sizes are3828/15272 bytes. Parent living counts are0/0/0, with no
activated five-Shadow presence or recovery inference. All160 sealed inputs and
four saves match; actual runner exit0 and idle rig are retained. Only selected
went cells were read: future arguments, current actor ownership, continuous
table identity and creator exclusion remain open. Original disk bytes do not
qualify relocated live
tables: the original item header's native signed-count interpretation exceeds
its declared extent. No disk-format repair, creator exclusion or new creation
authority follows. See the [collector review](../build/rig/native-secondary-binding-collector-independent-review-20261004-01/fixed/review.md)
and [disk inspection](../build/rig/native-secondary-mapping-disk-inspection-20261004-01/receipt.md).
The [native review](../build/rig/native-secondary-bindings-native-independent-review-20261004-01/review.md)
accepts the current-selection diagnostic only; first-rejoin FAIL and disabled
creation remain unchanged.

The opt-in then passed the unchanged89-step living Bootstrap fixture in196.8s,
run `20261004-062311_net_forced_resync_shadows_population_1`. All three peers
installed the three negative mutation hooks (mask7, poison0). Each receiver's
11 retained samples show one sampled-empty state, five pending holds, four
partial sets and a final five-ready set with listed/cache masks31. Ordinary
fresh-load spawning produced this progression; the preparer made no native
creation attempt. All22 samples retain missing288 (global pre-link32 plus
execution256), revision99 and creationAuthority0. Mode716750 bookends were1;
they do not close the conditional pre-active resource path or its callbacks.

Both original friends loaded3 to4, then made five exact positive HP20-to17
stores each and observed all five at17/20 on two distinct frames: Friend1
5843/5844, Friend2 3354/3355. Matching ACK63, relay and host-runtime results
share request1/cut3654. The full host catalog remains10 definitions/26 records.
All105 prelaunch inputs and four protected saves stayed unchanged through
terminal verification; all eight owned processes exited. Independent reviews
accept this installed-hook/sample/content behavior only. First-rejoin recovery,
ten cycles and added creation remain unaccepted. Native evidence:
[`pack-preparation-native-receipt-20261004-01`](../build/rig/pack-preparation-native-receipt-20261004-01.json).

The next offline addition is a natural-construction POD bridge in
`NativeSpawnController`. `KH2COOP_NATURAL_RESOURCE_TRACE=1` additionally
requires `KH2COOP_SPAWN_TRACE=1` and both wrapper hooks. It records the actual
wrapper C/R and five-row definition, ordinary table, lifecycle and negative
mutation samples before/after the single existing native call. Children copy
the current parent once; terminal records use a separate32-entry queue. Full
queue, nesting overflow, read failure, duplicate association, native unwind
and generation drift remain explicit. Thread-local context remains only a
candidate parent: actual two-fiber controls demonstrate inherited TLS, while
fiber continuity and creation authority stay false.

The bridge is now connected in source to the single `107240` callback observer.
The shared CMake targets passed387 spawn checks and63 resource checks in each
of Release and Windows ASan; the integrated consumer passed365 checks in each.
The saved package registration resolves `585FB0` slot0 to `107240`, whose body
sleeps, recurses and performs I/O/allocation. Isolated controls exposed a real
unwind failure in the stock MinHook jump tail and verified its specific corrected
tail with the actual MinHook library. The production observer checks all984
body bytes before installation and all33 generated trampoline/relay bytes after
the correction. It preserves raw RAX/AL and three arguments through normal and
exceptional returns.

This opt-in pins the actual observer module before enable and retains its hook,
trampoline, unwind table and metadata until process exit, including a failed
enable attempt. Stop retires recording; reinitialization and global MinHook
release are refused while those resources may be referenced. Process restart is
required to unload this diagnostic. Child records are bounded and publication
waits for the enclosing construction to end; thread ancestry remains a candidate,
with fiber continuity, actual indirect operand, complete routing, continuous
mode, lifetime, pending exclusion and creation authority explicitly unproven.
The native result below qualifies the sampled DLL pin and a bounded set of
actual callback/content joins. First-rejoin FAIL and all recovery acceptance
gates remain unchanged. Evidence:
[`native-construction-lineage-controls`](../build/rig/native-construction-lineage-controls-20261004-01/review-inputs-05.json),
[`finite resource targets`](../build/rig/native-resource-slot-targets-20261004-01/targets.md),
[`callback design`](../build/rig/native-resource-dispatch-design-20261004-01/design.md),
[`production observer controls`](../build/rig/native-resource-trace-controls-20261004-01/receipt.md),
[`independent module review`](../build/rig/native-resource-lineage-review-20261004-01/production-module-independent-review.md).

**Natural-resource native diagnostic, 2026-10-04:** the unchanged89-step fixture
passed in203.8s: [`20261004-080445`](../build/scenarios/20261004-080445_net_forced_resync_shadows_population_1/report.md).
All173 sealed inputs and four protected saves matched at terminal verification.
All three OS-loaded DLL-copy paths match staged disk SHA256
`8CA16B41C8B1AF1E3819E7981CC2E819AE265BB091BD76B105E825513C5C7398`;
their resource summaries report exact-body installation, modulePinned1 and
retained resources. This is not continuous or full in-memory module attestation.

Full construction receipts retain25 selected sampled parents: five on the host
and ten on each friend across the original and reload populations. Their raw
44-byte header(+0xE1), all five ordered64-byte records and indexed actual C/R
match the historical full-byte reference. Logged children join only the first
five selected parents on each peer:90 callbacks per peer,18 per record. Each
record has two nine-call sequences: root packages0-3 return AL0 with lowercase
recursive AL0, then package4 returns AL1 for `M_EX020.a.us`. All logged callback
mode samples are1. These samples do not observe the actual mode-getter branch,
indirect operand or complete106940 final return.

Host logging retains279/279 published children. Each friend published/consumed747
but logged512, suppressing235 whole receipts at the DLL-lifetime cap. Their
later five selected construction parents have no logged children; that absence
is unqualified. Across1303 logged receipts,725 caller sites are106A17 and578
are recursive1074D9; AL totals are1156 zero and147 one, with no logged unwind.
Unparented and foreign callbacks remain separately counted. No complete routing,
fiber continuity, global pending exclusion, creator lifetime or atomic authority
is established.

Both original friends load3 to4 and converge with checks63, exact original
IDs1-5/object302/type4 and HP17/max20 on two actual frames each. Relay and host
runtime terminals agree. Fresh desktop queries found all eight recorded PIDs
absent, an idle rig and empty Git index; only the runner has a retained exit
code. Collection remains partial and positive point stability remains unclaimed.
This validates the existing natural living Bootstrap with the observer enabled;
first-rejoin FAIL, zero ten-cycle repetitions and disabled added creation remain.
See [`native evidence and independent audits`](../build/rig/native-resource-native-20261004-01/terminal-inputs.json).

## Live empty-room smoke, 2026-10-03

The current candidate passed all 18 steps of the three-instance empty-GoA smoke
in `build/scenarios/20261003-131544_net_forced_resync_smoke_1/` (149.2 seconds).
Both original friends performed a genuine additional same-room load from serial
3 to 4, then returned Bootstrap Converged checksMask 63 on distinct native frames
3372/3373 and 765/766. Their phase, cut 8, snapshot SHA and fingerprint matched;
actual relay and host-runtime terminal results retained both original targets.
Two fresh statehash samples and two independent complete native censuses matched
the full location, progress and empty population. All four protected save files
were unchanged, and the rig released its own instances and lock.

This is local empty-room convergence. Progress application wrote zero changed
spans/bytes; no nonzero flag repair, living enemies, deaths, Checkpoint or physical
remote recovery was exercised. Those acceptance requirements remain open.
The original attempt `20261003-130706_net_forced_resync_smoke_1` is retained as a
failure: verified membership succeeded, but the scenario default awaited an
obsolete connection log string. The readiness check now matches the existing
verified-membership message; native arrival/bootstrap checks remain separate.
The normal census collector succeeded in this run. That does not establish the
cause of the earlier Defender command-line detection; protection was unchanged.

The first living-only fixture `20261003-132801_net_forced_resync_native_nonempty_1`
failed in 163.0 seconds at step 43, before the resync command. Its current four
actors (two object 309 and two 311) passed exact host-only HP 160 to 153 and two
complete independent census joins on all peers. Earlier bindings 1 and 2 had
disappeared with last sampled HP 160; host disappearance tombstones and matching
client records made the zero-history prerequisite fail. These records do not
establish native lethal deaths or their cause. No resync Plan, ACK or terminal
result exists for this attempt. The strict fixture and failed evidence remain
unchanged; moving the history baseline would hide the prerequisite failure.

The distinct first-activation fixture `20261003-133716` then failed in 158.9
seconds at population readiness, before damage or resync. After one native
900-ms movement pulse per peer, all peers remained empty; the host's observed
activation point was (1029, -1100, -1647.425, 1). No manifest, disappearance or
resync records were emitted. A qualifying living population has not been
established by these two setups. Further setup must use checked native geometry
and fresh population evidence; neither failure becomes recovery acceptance.

## Live living-population Bootstrap, 2026-10-03

The measured courtyard fixture `20261003-135207_net_forced_resync_native_shadows_1`
produced five object-302 Shadows at HP 20 on all three peers, with complete
independent census and hash joins. It failed at step 66 in 169.9 seconds because
the second positive host-point capture was unstable, before damage or resync.
Its 90-step source and failed result remain unchanged. This is a positional
control failure; it does not establish a failed native recovery transaction.

The separate `net_forced_resync_shadows_population.json` narrows acceptance to
fresh complete population and HP recovery. It removes only that positive
point-stability assertion, retaining all other 89 steps in exact order. Raw
positive geometry, the strict client-inside/host-outside negative control,
complete typed population, original zero-death history and every native resync
and post-reload check remain. Positive positional stability is explicitly
unclaimed. Recovery uses the checked native population rather than inferring it
from a stationary activation point.

Run `20261003-140717_net_forced_resync_shadows_population_1` passed all 89 steps
in 190.4 seconds. Exact host damage reduced all five original Shadows from HP 20
to 17 on every peer before one command targeted both original friends. Each
friend performed a genuine additional load from serial 3 to 4. Bootstrap
Converged witnesses used distinct native frames 5459/5460 and 2933/2934,
checksMask 63, cut 3115 and the same snapshot SHA and fingerprint. Independent
relay and host-runtime terminals retained slots/connections 1/2 and 2/3 with
delivery 2. Two fresh post-resync hashes and two independent complete censuses
matched all original typed bindings at HP 17 with no extras or unmatched actors.
Three final rendered captures were saved. All four protected saves were
unchanged; the rig released its own instances and lock.

This proves local living-only Bootstrap convergence for that five-Shadow
population. Clients already matched HP 17 before the request. Forced progress
apply changed zero spans/bytes; earlier ordinary bootstrap writes are separate.
It does not prove deliberately stale-client repair, changed-progress repair,
Checkpoint, dead reconstruction, creation-incarnation identity, positional
stability or physical remote recovery. All three original Linear criteria
remain open. The saved parser audit is
`build/rig/resync_population_saved_audit_20261003.json`; independent review is
`build/rig/resync_shadows_population_review_20261003.md`.

## Deliberate progress diagnostic

The separate `net_forced_resync_shadows_progress.json` adds a bounded diagnostic
control to the qualified living-population fixture. Only its command step is
replaced: `progress_fault_resync` toggles Friend1's verified shared flag409 bit,
requires two fresh progress-only mismatch samples with unchanged exact five
Shadow HP rows, then invokes the same one both-target transaction. Full shared
and personal snapshots bracket the fault. Acceptance requires an ordered target
Bootstrap plan, exactly one verified byte applied with personal equality,
matching issued/new-load/ACK evidence and authoritative progress restored before
cleanup. The unchanged downstream population checks still apply.

The fixed rig-only poke and multibatch reads are non-atomic. Pre-write logs
establish observed session/peer/PID/load/arrival/version continuity; numeric
connection/generation facts are joined retrospectively from the actual
transaction. Cleanup preserves all other bits and writes only while the
original observed context remains identifiable. Cleanup cannot turn failed
repair evidence into a pass. Its isolated provider tests remain distinct from
the following native result.

Run `20261003-152248_net_forced_resync_shadows_progress_1` passed all 89 steps
in 187.9 seconds with the fixture and assertions unchanged from the preceding
attempt. Full raw snapshots show only Friend1's shared byte at SAVE+0x23DF
changing from 00 to 02; its independently computed KHP1 hash changed from
9AC5499B to 9CC54CC1. All other shared and personal bytes stayed identical
around the fault, and two fresh native samples plus the relay detected only
progress mismatch. The fault was still present immediately before the single
both-target command.

The transaction retained session `04a166f152376ad94d057473354f7ef7`, host 1,
request 1, original targets (slot 1, connection 2, delivery 2) and (slot 2,
connection 3, delivery 2), epoch 1 and the complete courtyard tuple. Friend1's
ordered plan/reset/full/queue/apply/issued/load/ACK chain contains exactly one
changed span and byte, restoring hash 9AC5499B before the native reload. Both
friends loaded from serial 3 to 4 and converged on native frames 5637/5638 and
3196/3197, with checksMask 63 and cut 3427. Matching relay and host-runtime
terminals retain both targets. Full post-repair shared snapshots restored the
original byte and hash before cleanup; cleanup declined a write after the
changed lifecycle. Two further fresh hashes and complete censuses retained the
same five object-302 Shadows at HP 17 with no extras or unmatched actors.

Production `personal_unchanged=1` brackets the apply's own writes. It does not
mean personal state stayed unchanged across the whole experiment: both clients'
post-load Goofy current MP byte at SAVE+0x271E differs from the initial
snapshot (100 to 90); HP stayed 28/28 and max MP stayed 100. The frozen receipt's
HP label was incorrect. The writer and exact timing are unestablished; the
host's personal snapshot stayed unchanged. This is repair of one deliberately injected shared
progress bit on one PC; natural progression, stale HP, dead reconstruction,
Checkpoint, stable positive geometry and physical remote recovery remain open.
All four protected saves stayed unchanged, and owned processes and the rig lock
were released. Three final PNGs retain actual matching request/done sequence 36,
status 0, one frame, renderer 12 and 1920x1080 dimensions for their exact PIDs.

The preceding unchanged attempt `20261003-145341` remains FAIL at step 45,
before any progress fault or resync. Friend2's within-capture position stability
was false in both geometry samples. The saved initial Y points differ by one
float32 step, but repeated position bytes were discarded; that inter-sample
change cannot identify the within-capture delta or its writer. No guard or timing
was relaxed for the single retry. Native review and the fresh saved parser audit
are `build/rig/resync_progress_native_review_20261003.md` and
`build/rig/resync_progress_saved_audit_20261003.json`.

## Reproduced boundary

The frozen protocol 7 loopback baseline primed two friends with HP22, then
changed a synthetic host provider to HP73 without publishing it. The old request
returned seven cached records to each friend, including HP22. Two fresh-state
assertions failed; ten other checks passed. Arrival ACKs did not eliminate the
remaining mismatch. This is executed transport evidence, not native gameplay.
See `build/rig/forced_resync_failed_baseline_receipt_20261003.json`.

## Operator and authority

Use `kh2ctl world-resync --pid N --slot 1|2|all` with an explicit admitted host
PID. The command opens the existing compatible WorldBridge; it neither creates a
replacement mapping nor reads or writes native game memory. Its separate bounded
CAS mailbox preserves the captured generation, delivery serial and host
connection. The runtime checks that identity before creating the request. A busy
mailbox is an explicit failure. The DLL remains the sole producer of its SPSC
packet ring; the operator never writes that ring.

The JSON `queued:true,nativeConvergence:false` is a queue receipt. Success requires
the completed transaction and actual native observations described below.
The ordinary relay runs native traffic by default. `--simulate` explicitly
enables the legacy simulation, whose sessions refuse native capture before
fencing any target.

## Fresh snapshot and delivery

One request freezes its opaque session/host/request key, full six-field room and
epoch, original target connections and target denominator for at most 30 seconds.
Selected targets receive new delivery serials. Each actual native producer
retains its observation-time context; the runtime cannot relabel queued old
records with current headers. The host DLL supplies a nonwrapping lifetime source
counter, distinct from the existing absolute-HP sample sequence.

Capture reads the checked complete native enemy census and the full 8,108-byte
masked progress allow-list, bracketing native load/transition/generation and
gameplay availability. It conveys typed living enemies, checked HP/maxHP and the
complete native record witness described above. Absent actors, including cached
dead history, cannot supply fresh membership and make actual host capture
unavailable; the wire representation still supports typed dead rows, which the
actual target rejects before staging. An unavailable census is
distinct from a complete empty census. The native fingerprint covers room, hold,
masked progress, typed population/health and canonical record content; transport
SHA covers the exact snapshot including full raw headers and capture stamps.

The receiver stages at most 60,000 bytes, four parts and 1,024 enemies. Complete framing,
canonical decode, offsets, counts, coverage and SHA must pass before a single
native snapshot is published. Different snapshots within one phase fail. Exact
duplicates cannot repeat publication or renew the original deadline.

Bootstrap queues the immutable fence, a strictly newer exact 12-byte generation
and delivery reset, and the complete snapshot in order. Newer material world
updates preserve producer ordering through a bounded continuation of at most
256 records / 512 KiB per target. A target that attaches late retains those updates
separately until the new bootstrap FIFO is installed. Actual submission failure
or overflow terminates the transaction. Terminal failure immediately retires
native authority and queued world warp/progress/HP work; quarantine stays in
place until another authorized bootstrap. Already-entered native calls cannot
be undone by this cross-process invalidation.

## Completion and limits

`Received`, `Arrived`, transport End, ordinary StateHash and TransitionAck are
not completion. Native `Converged` requires a changed Bootstrap load, exact full
room, fresh complete typed census/HP, fresh matching full masked progress and
two distinct observed native frames. Every original target must acknowledge the
same phase, cut, snapshot SHA and fingerprint. The relay checks the reported
facts; native evidence must independently establish that the DLL observed them.

A material value change after Bootstrap permits one fresh Checkpoint using the
same key, target serials and original deadline. Checkpoint performs no reset,
progress/HP/death write or second reload. Further material change after its cut fails.
Unchanged periodic HP sequence advances alone do not require another checkpoint.

Dead history does not supply safe native creation-incarnation identity. A target
with dead/bootstrap ambiguity explicitly reports unavailable before desired
state mutation or reload. The fresh-actor anti-refill guard stays in force.
Controller/script reconstruction, dead reconstruction, general nonempty enemy
and progress recovery, remote PCs and all original VUH-1508 acceptance remain
open. The local five-Shadow convergence and single injected progress-bit repair
above are bounded native evidence for those specific cases.

## Evidence records

`build/rig/resync_offline_receipt_20261003.json` records 1,897 passing C++ checks
in both Release and Windows ASan, and 1,340 in portable ASan+UBSan. These include
205 actual codec/ENet transaction controls in each configuration and 185 Windows
production native controls over owned memory. Nine existing scenario-linkage and
24 saved-evidence cases passed, along with three actual PID-0 runtime smokes and
the new scenario's structure check. No sanitizer findings were observed. The
original failing baseline, build/test failures and independent reviews remain
separate evidence. The initial combined delayed-witness failure is unisolated;
the final same-channel reliable marker strengthens its positive flush proof,
without claiming a diagnosed production fix.

DLL `[resync] plan` and `[resync] ack` records carry immutable identities and
actual observation facts. ACK logging occurs at serialization: `bridgeSent=0`
and `relayAccepted=unknown` honestly identify that boundary. Bounded
`[resync-result]` records in relay and runtime include the full transaction key,
original targets, status, applied cut and fingerprint. Errors are hex encoded to
keep peer text on one line. Only correlated native observations plus relay and
host-runtime terminal records can support a live convergence claim.

Source reviews and owned-memory/loopback controls are offline evidence. A real
game reload, rendered capture and physical multi-PC acceptance require separate
live receipts; synthetic native ACKs in transport tests cannot satisfy them.
