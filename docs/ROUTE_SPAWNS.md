# Bailey and LoD route spawns (VUH-2343, VUH-2352)

Bailey-13 passes the full two-instance fight: real Creeper births, native pause,
both mission ends, empty matching enemy counts/HP, and egress without a second
end. The default-on implementation passed independent inject review and the
Windows build/regression. VUH-2343 remains open for first-visit Borough enemy
grounding; VUH-2352 still needs its own live LoD run. Failed observation/probe
runs remain attached to the issues. This development fixture uses warps on PC1
and does not accept the D15 two-PC route through real doors.

## Bailey: add the missing actor at the native player boundary

`NativeSpawnController::PolicyWrapper` accepts only the captured fixed-wrapper
caller `3FF727`, NOW `04/08 door0 map52 btl52 evt0`, package `b_40`/808738658,
type1/header55 and its complete four-record group. The first record is raw566;
the gate objects2040/1520 and native Leon1564 remain authored. Caller instructions
at `3FF71B` are pinned; private-status initialization already verifies the complete
fixed wrapper, factory, player factory, Sora derived/base constructor and status safety bodies on Steam
`9002b2de`. Header, records and controller roots are sampled again before entry.

The extra call requires the shipped two-player TARGET_ALL/MIRROR prerequisites,
ready private status and neutral input, a live admitted two-player session,
normal resolved Sora84/form0, no event/cutscene, native admission headroom and a
pending load. It writes no party row, member table, mission result or save file.
The save guard remains the launcher's existing required startup dependency.

`privatestatus::SpawnMissingParty` copies the 64-byte player record into a
process-lifetime pool and changes only objectId to direct Sora84. An explicit
FLS scope selects exactly one factory/constructor/fresh-status allocation. It
reuses shared-lookup exclusion, SAVE commit veto and native retirement. It
neither arms the legacy raw566/raw567 binding nor changes its room profiles.

On normal paths, the added wrapper call precedes one authored raw566 call; the
helper returns that authored call's result and last error. Native exceptions
propagate through finally blocks. Null or ambiguous creation spends the load;
there is no optimistic retry, guessed deletion or record reuse. Sixteen records
are available per process. Capacity refusal delegates the original call only.
An explicit-scope factory refusal before native allocation takes the fixed
wrapper's checked null-return path. Only the observed base allocation at caller
`3A7B1B`, key1/form0 can raise noncontinuable SEH `E0423433` on lost selection or
failed safety checks. The derived Sora constructor assumes its base completed;
returning a fake base-constructor result is unsafe. Other constructor, allocator
and initializer cases record a fault and disarm selection without raising this
exception. A negative fence covers that same exact allocation when its active
native stack has lost the explicit FLS selection. The retained context has only
owned POD and numeric thread/stack identity; another fiber's ordinary calls are
unaffected. This is a negative fence, not creation authority. Such a fault fails the probe and requires
owned rig cleanup; it must never be accepted as a normal gameplay result.

The constructor registers the added body for quarantine before native code can
yield. Publication requires distinct local/clone actors and status records,
private ownership for the clone, ordinary status for the local, the canonical
pointer on the local, and both native controller/record attachments. Failure
disarms further selection while preserving any existing private ownership and
veto. Both clone collection and every puppet-target lookup enforce quarantine.

The observed wrapper ran at construction load4/transition4; native
`CompleteLoad` then advanced to load5. A bound added body becomes eligible only
at exactly construction-load+1, with the same transition, location, session,
canonical local and native roots, and no pending transition. A second completion,
new transition, disconnect or changed roots cannot relabel the body. Old record
storage stays retained even after its selection retires. A private allocation's
owned serial distinguishes a later legitimate private birth at reused actor/status
addresses; only a new nonzero owned serial with healthy private status retires the
old quarantine claim. Pointer reuse alone never supplies that permission.

`net_hb1_bailey.json` requires both missing-party bindings before combat, native
Leon, private-status health, enemy HP parity, actual `HB08_MS102.bar` start/end,
and a subsequent GoA arrival. It uses development warps and player protection.
Its existing `04/0A` control proves clone arrival, not Shadow grounding. The
retained area-title capture identifies that room as **Marketplace**, despite
older fixtures calling it Borough; installed hb10 has no Shadow302 spawn group.
The targeted `net_hb1_borough_grounding.json` instead uses hb09, `04/09`, ambient
map/battle1 and event0: installed battle1 selects b_10+b_00, including nine
Shadow302 records at several heights. It requires nonempty matching counts/HP,
native walking, identity/position samples, both clips and egress. The final
grounding verdict also requires identity-joined height analysis and visual
review; an empty Marketplace arrival cannot meet it. This first-visit ambient
check is separate from scripted HB09_MS101 at battle51 and D15 real-door play.
First-visit grounding remains unqualified until that live evidence passes.

### Borough height observation and proposed measurement fix

Borough-03 (`20261017-111521_net_hb1_borough_grounding_1`, source `627a3be`)
reaches the Shadow region through installed entrance2. All25 scenario steps,
nonempty count/HP parity and GoA egress pass; final enemies on both peers are
Shadows at8 and14HP. The **separate grounding gate is unresolved**: its
simultaneous actor reads produce101 current-epoch/netId/actor-joined pairs,
including18 absolute height differences over20u (maximum259.496u).

The gap is transient and changes sign. Signed friend-minus-host medians by
netId2/3/4 are0/-0.384/0;83/101 pairs are within20u. All18 outliers occur during
damage-air/landing motions18/19 or extended motion161. Same-motion nonwrapped
samples usually trail by10-12 motion frames (80/84 cases, median11). At9.74s,
ID4 has host motion18/time10/Y=-359.496 versus friend motion18/time0/Y=-100;
at10.74s the friend is instead higher during descent. Ground returns and both
clips argue against a constant Y-base error or accumulating gravity drift.
They do not prove exact frame-aligned parity. No gameplay fix is established.

The existing publisher reads host POS_Y, `Stream::PoseAt` interpolatesXYZ at a
delayed host-frame cursor, and `WritePose` writesY after native actor update.
The configured cursor delay is6frames. The proposed fix is to the measurement:
reuse `KH2COOP_ENEMY_MIRROR_TRACE=1` and `KH2COOP_LATENCY_TRACE=1` to compare
host and observed client positions at the same hostFrame/cursor, excluding
stale, missing, undriven or takeover-blended evidence. Keep grounded-rest
windows and visual review separate from legitimate launch/burrow animation.
If alignedY still diverges, determine whether the sampled pose is wrong or a
later native writer overwrites it before proposing a scoped inject fix and
Quill D14.4 review. Do not add gravity, snap-to-ground or a Y offset based on
the unaligned reads. The lead cleared one matched-frame measurement after the
finding was posted, with unexplained extra latency kept as a separate finding.

`tools/scenario/grounding_verdict.py` reads these existing traces. It joins
current Borough epoch/netId and hostFrame, only across adjacent host publications
(at most3frames). Undriven/takeover-blended samples grant no evidence. The strict
ground-rest gate requires a contiguous12-host-frame window, stable host motion,
at most0.5u host vertical range and matching client motion; matched rest |deltaY|
must be at most2u, separately from the whole-pose5u bound. Each actor needs30
matched and20 rest observations. Synthetic offset, stale-epoch, missing-bracket,
empty, undriven and blended controls fail. The latent extra5-frame case remains
explicitly open even when matched poses agree. On this one PC, QPC joins measure
host-current minus newest-received (publication/pump/transport freshness) and
newest-received minus cursor (buffering/hysteresis); no cross-PC clock claim.

Borough-01 missed the Shadow region because its entrance0 walk hit a wall;
Borough-02 changed only the expected entrance while still requesting0. Both
remain failed setup evidence. Borough-03's preflight executes the real warp
argument builder with a stubbed CLI and verifies entrance2 against the oracle.
All three protected-file/process closures passed; PC1 was released after each.

F1 records every Constructor/Allocate entry under the missing scope, including
caller, key and form. Settled Bailey gameplay logs the clone's controller and
retained-record roots every60 owner frames; the fixture rejects any failed
attachment sample and retains mission start/end plus combat samples.

## Bailey Creepers: scoped birth authority

Bailey-05 (`20261017-043655_net_hb1_bailey_1`, source `f38dd32`) proves
the missing clone and F1: exactly one Constructor40505B and Allocate3A7B1B,
key1/form0, on each peer, healthy private status and retained attachments.
The fight fails enemy HP parity after75s. Mission `HB08_MS102.bar` stays active
in all71 samples; neither native mission end was reached. Five friend Creepers317
remain unbound at31/31 while the host has one at14/50/netId9. Header58 receipts
were consumed, but the old `RecordFamily` gate omitted their birth tickets.

The global denylist exists for observed failures: Creeper317 failed ordinary
point binding (`no-point-match`, run20261007-220021); FieryGlobe76 had a first-point
mismatch on all five bodies (b6-02); LanceWarrior2409 faulted on a native HB load
even with co-op off. The old317/76 record-authority experiment is default-off and
remains parked: its complete-population/placement qualification did not finish
(`docs/ENEMY_RECORD_BINDING.md`). Do not remove a family-wide fence to fix Bailey.

The new exception requires NOW `04/08 door0 map52 btl52 evt0`, an active native
`HB08_MS102.bar`, a current admitted session, no transition, and the exact whole
`b_40`/808738658 type2/header58 definition (ten records). Only ordinals0–4 are
Creeper317; ordinals5–9 are Dusks and receive no new rights. The full44-byte
header (with +0xE treated as bounded runtime activation) and640 record bytes,
indexed controller/record membership, actor handle, objentry and status roots
are checked again at use. No new wire fields are added.

Only a successful native return around an authenticated one-use host emission
can establish the client's Creeper correspondence. The receipt's original
mission pointer, generation, epoch, connection/delivery, load/transition and
native roots must remain current on continuous owner frames. A gap, absent body,
changed mission/roots or session retires the occurrence; no point fallback,
same-address recovery or new generic factory copy is allowed. Checked status
initialization retains only the existing bounded pre-status warmup. The old
record-authority flags and resync modes do not mix with this exception.

The host publishes motion only for the exact native group; the client buffers317
only in this mission and grants pose/brain/claim/HP/death rights only to its
receipt-bound current body. Terminal deaths can drain the same ended mission;
an ended mission grants no living authority. The generic population planner and
native admission weights are unchanged: observed limit120/used118/weight9 must
wait, never overcommit. Correctly bound original bodies need no repair allocation.
MissionMirror keeps its native once-only end path; this change writes no mission
result, party/member/save row, or admission budget.

Tests were authored before implementation. They exercise exact-byte mutations,
wrong mission/room/group/ordinal rejection, the production receipt ledger and
binder, continuous observation/retirement, unchanged default stream/denylist,
native repair headroom and native mission-end rules. The live fixture requires
five Creeper tickets, both native ends exactly once, empty matching live enemy
counts/HP and egress. Portable tests cannot prove the native driver or mission
end. Independent inject review and the Bailey-13 live run passed before landing;
Bailey-05 remains a failed result, and VUH-2352 still needs its own run.

## LoD: admit only the observed generated waves

`RouteSpawnPolicy.hpp` pins the full header and record bytes for NOW
`08/01 door0 map68 btl68 evt0`:

| Package | Header | Records |
|---|---:|---|
| b_4p / 1882480482 | 978 | Shadow302, mode2/position1 |
| b_4p / 1882480482 | 999 | Shadow302, mode2/position1 |
| b_4s / 1932812130 | 1007 | two Nightwalker10, mode2/position1 |

The complete generated wrapper `3FE650..3FE6E5` and caller instructions at
`3FE96A` are verified before hook installation. This exception qualifies
**emission only**. It never grants general combat activation, type1 support or
position1 support elsewhere. Existing table membership, layout, object class,
identity and lifecycle checks remain required. In particular `m_22`/842162029,
type1/header1652, contains map objects2440 and remains unsupported.

The host runs native updates inside the existing FLS emission scope. The client
requires an exact pending host receipt to tick these controllers, then reserves
the matching record around generated caller `3FE989`. A normal native null
return cancels for retry; a nonnull/ambiguous result spends that occurrence.
Checked actor/controller/record roots supply the existing birth ticket and host
combat netId. Independent native births are blocked on this scoped client path.

The current protocol's `SpawnEmission.point` retains its controller activation
point semantics. This patch does not replay generated spawn coordinates: native
placement may differ until existing host pose mirroring takes over. Identity and
HP parity must therefore be proved through occurrence tickets and native actor
roots, not spatial matching. If live evidence requires generated-point replay,
design it separately without silently changing that wire field's meaning.

The observer reserves its 64-return budget for successful births, with only
eight entry logs; startup null returns no longer exhaust it. The updated LoD
fixture requires successful returns on both roles, matching generated-wave
host/client netId+sequence receipts and final enemy HP parity, alongside the
landed morale/end checks.

## Provenance and checks

The exact definitions were decoded read-only with the retained OpenKH-backed
extractor and BAR/spawn parser. Installed `hb08.ard` SHA256 is
`e383b5a1d59ef89160159f283edb41772c2469ce6f92d167ddf1dbdc18afdbd9`;
`mu01.ard` is
`4810c8bda27a92b25bb8710a450750e32006846bf25b19ef991c4692f4cfdc16`.
No installed asset is edited. Native ingress and the finalizer sequence come
from `20261017-024155_net_hb1_bailey_1`; generated ingress comes from
`20261017-033218_net_lod_morale_spawn_probe_1`. Both reports are on their issues.

`kh2coop_route_spawn_policy_test` checks every one-bit header/record mutation,
cross-package/location/caller rejection, retained-record duplicate/stale/capacity
limits, exact load completion, negative binding conditions, and the production
emission ledger's ordered reserve/cancel/commit behavior for the two-record wave.
It is portable and registered in CTest. Run the Windows build and live scenarios
only in the lead-assigned lane under the main checkout's rig.lock.

### Bailey ticket-admission observation

Bailey-07 (`20261017-070014_net_hb1_bailey_1`, source `3a7ead0`) sent,
received and emitted all five header58 Creepers but retained no317 action
birth tickets. Native returned-root capture did not report refusal. The old
logs cannot distinguish the mission scope from native group/root revalidation;
they contain no live header/record bytes, so no exact sub-check is established.
The sixteen-line, package/header-scoped `[bailey317] admission` diagnostic
reports the first failed mission/native check and the first differing definition
byte, or changed birth fields. It grants no permission. Bailey-08
(`20261017-073817_net_hb1_bailey_1`, `8436a98`) pins the refusal on all five
receipts: metadata and mission pass; native comparison reports
`group-header offset=14 observed=1 expected=0`. No317 action ticket was created,
so the pause gate stopped the run. Neither peer has a retired line or mission
end. Counts6/6 hide different populations/HP. The report and opening combat
clips are on VUH-2343, with unchanged saves and owned process closure.

Read-only Ghidra confirms `3FE320` loads the header from `controller+8`; its
type2 branch writes `header[0xE]=1` when zero, then calls `3FE6F0` to emit.
Installed header58 has0 there: this is native activation state, separate from
group identity. `BaileyCreeperHeaderMatches` excludes **only +0xE** from the
installed definition comparison and bounds its value to0 or1. All43 other
header bytes, all640 ordered record bytes, exact room/package/ordinal, native
roots and lifecycle still qualify. Full raw header equality remains in the
native before/after bookend, so a concurrent activation change still refuses.
The new regression fails for all five activated ordinals with the old comparator,
then passes with the fix. Values2..255 and every other header/record bit mutation
remain refused. Quill review and live mission end plus parity remain required
before landing; the global denylist and defaults are unchanged.

### Bailey mission-end dispatch

Read-only extraction of installed `msn/us/HB08_MS102.bar` (SHA256
`c7d844eba9e321515b4af0c73d57a61b378cb2b35ab206a1fbce3290f3cc8ca1`)
using pinned OpenKH BDX descriptors shows normal completion at **4/3**:
trigger10's event2 stores selector1 at L68/L71, and the mission thread dispatches
selector1 to `syscall4,3` at L210. The common dispatcher also has selector2
to4/16 at L225, selector3 to failure4/11, and selector4 to4/22. No decoded
writer selects2 in this script; that branch's existence does not prove Bailey
completes through4/16. Main `ae0a918` reverted global4/16 replay because it grants
local rewards. The scoped portable test covers only replayable traps3 and22;
the shared script's4/16 branch grants no mirrored completion. The fixture still requires
one actual host end, exactly one explicit friend completion path, ended flags
on both, and empty matching enemy HP/counts. A native-local completion with
zero mirror replays is valid; it must never coexist with a replay. Bailey-11 observed exactly one host `mission_complete`
(4/3) and one successful friend replay, but final native flags and enemy parity
still differed; see the terminal-death observation below.

### Bailey native pause check

Bailey-09 (`20261017-080636_net_hb1_bailey_1`, main `54f2b94`) passed all five
friend317 admissions and adopted all five exact host netIds. The Windows build
and all three native tests passed. It stopped at the fixture's menu10-only pause
check after Start, before resuming combat; there were no retired lines or mission
ends. Latest separate native samples agree on seven living IDs/HP (five317 at50,
two310 at80), but final parity/end acceptance was not reached.

Read-only native pause dispatch explains why10 is too narrow: `1572B0` checks
pause blockers `ABB878`, a live room and the Start edge, then `3AD540` chooses
the active battle context. With that context, `2E4C20` requests menu8 through
`2E4950(8)`. Existing `EventHoldInputState` already accepts combat8 and field10.
Those are conditional native paths; Bailey-09 retained no post-Start menu bytes,
so its actual menu remains unmeasured. No pause blocker is cleared or bypassed.
The fixture now accepts only8/10, retains post-Start menu/freeze/blocker samples
and attempted-pause screenshots before its assertion, then records a verified
paused state before the two-second hold. Resume still requires255. A new run
must prove the actual pause and report every317 retirement, both native ends
and enemy count/HP parity.

Bailey-10 (`20261017-085339_net_hb1_bailey_1`, `08d669c`) measures combat
menu8 on both peers, blockers0 before Start and1 while paused, then255 on
resume. Both pause-entry and immediate post-resume retirement lists are empty.
Five later friend317 retirements (IDs11,10,8,9,12) each immediately follow the
same actor leaving the native list with lastHP0 and a native cull-pass receipt.
They are completed deaths, rather than observed pause/lease-gap losses; host
has no retirement lines. This qualifies this pause, not every possible skipped
owner-frame condition.

The run still fails mission completion. The resumed helper stops at79.23s of
its150s budget with both `enemiesEnd=0`, while mission flags remain1. The next
five-Dusk wave18..22 then appears on both games. HP parity passes (five80HP
Dusks each at the post-fight snapshot; later both match68/68/68/74/68), but
neither mission ends. The resumed fight now uses its existing bounded `until`
milestone to span those empty wave gaps and requires both native mission ends
with no living enemies. It keeps the150s budget, native input and all ticket,
pause, parity and once-only end checks.

### Bailey terminal death across the end hold

Bailey-11 (`20261017-093331_net_hb1_bailey_1`, `df84ab7`) spans the wave gap
and reaches one host4/3 `mission_complete` and one friend replay (`ok=1`).
It still fails final HP/count parity: host empty, friend one Dusk318 at11HP.
The final paired samples show mission flags21 on the host and1 on the friend;
the replay receipt alone does not prove both native missions finished.
[Report and clips](https://linear.app/vuhlp/issue/VUH-2343#comment-7473215f-907a-476b-b285-e0216e60f168).

The Dusk had exact emission ticket20/header58/record7. The friend receives its
host death immediately before replaying the native end, which enters an event
hold. After that gap the same body remains11HP but cannot regain its terminal
binding (`death-without-continuous-binding`, selected0). Relaxing continuity or
writing through a retained actor pointer would weaken the lifetime boundary.

The fix defers only `HB08_MS102.bar` successful end replay until a
fresh complete current Bailey census has no living enemies, with the mission
pointer and scope checked on both sides. It leaves the replay one-shot pending
so the normal client frame can apply the authenticated final death first. It
does not grant any death/HP authority, change owner-frame ordering, relax
binding continuity, or alter other mission ends or failures. Deferral is capped
at180 owner frames (about3s at60fps), reset with the local mission pointer/load/file.
If a live orphan or unavailable census persists, the cap restores the original
native end call once; it never kills or writes HP on an unbound body. At the cap,
a fresh complete census logs every living orphan's netId (0 if unmatched), HP,
actor and family, or explicitly logs unavailable. The replay receipt reports
`path=clean-census` or `path=frame-cap`. Quill passed the initial deferral's
D14.4 review, including the bounded follow-up. The owned-memory
`kh2coop_nativehit_test` reproduces death plus end in one owner frame and an
immediate native end hold, plus an unbound orphan that forces the capped replay
without a new lethal. Native compilation and the regression passed; Bailey-13
passed final parity/ended flags/egress. A capped end is liveness evidence, not a pass
of the enemy parity gate.

Bailey-12 (`20261017-101224_net_hb1_bailey_1`, `cae373c`) reaches both native
ended flags21/result0 with living enemies0/0 and HP parity passing, after114.23s
of resumed combat. The friend applies the final Samurai7 death (4HP to0) and
finishes without a mirror replay, cap or orphan receipt. The run still fails
the old replay-only assertion before egress; it is not a passing scenario.
The lead accepts native-local completion as a valid path and requires an
explicit zero-replay receipt for the next run.

The diagnostic observer records `path=native-local replays=0` once per local
mission pointer/load/file. It counts actual mirror end attempts separately
from the existing replay guard, which can also mean "already ended locally".
It does not change any completion, following, binding or write decision.
During a native end hold, the native census can be unavailable: the receipt
says so with living/HP sum -1 rather than inventing an empty sample. The
scenario independently requires current empty actor counts and both ended
flags, exactly one of native-local/clean-census/frame-cap, zero replays on
native-local, a successful replay otherwise, and no second path after egress.
Bailey-13 (`20261017-103923_net_hb1_bailey_1`, tested `f324f9a`, landed
`0cf47c0`) passes all86 steps. Exactly one host `mission_complete` and one
successful friend `path=clean-census` replay were logged; there was no
native-local receipt, cap or orphan. Both native flags were21/result0, with
living counts0/0 and empty matching enemy HP. All five real317 tickets were
adopted. Native menu8/blocker1 paused both games for2s and resumed to255;
no receipt retired across the pause. The five later friend retirements each
follow a same-actor list removal at HP0 and a cull-pass. Attachment checks and
egress to GoA4/26 passed, including the no-double-end check after egress.
The native-local and cap alternatives remain covered by owned-memory MSVC
regressions; this live run proves the clean-census path. The reviewed host and
friend clips show resumed combat; reports/logs and completion/egress stills
carry the later end evidence. Safety closure found no changed protected files
or remaining owned processes, and PC1 was released at10:44CDT.
