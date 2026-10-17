# Bailey and LoD route spawns (VUH-2343, VUH-2352)

Bailey-05 proves native private clone construction and client emission, while
the full fight fails enemy parity and never reaches mission end. The scoped
Creeper candidate below still requires independent review, a Windows build and
a passing live run. The base portable policy passed ASan/UBSan and the Windows
build; portable tests alone cannot prove the native driver or mission completion.
Failed observation/probe runs remain attached to the issues.

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
Its existing `04/0A` control proves clone arrival, not Shadow grounding: installed
hb10 contains no Shadow302 spawn group. First-visit grounding remains unqualified.
F1 records every Constructor/Allocate entry under the missing scope, including
caller, key and form. Settled Bailey gameplay logs the clone's controller and
retained-record roots every60 owner frames; the fixture rejects any failed
attachment sample and retains mission start/end plus combat samples.

## Bailey Creepers: scoped birth authority, pending review/live qualification

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
header and640 record bytes, indexed controller/record membership, actor handle,
objentry and status roots are checked again at use. No new wire fields are added.

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
end. Independent inject review and that passing live run are required before
landing; Bailey-05 remains a failed result, and VUH-2352 still needs its own run.

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
The sixteen-line, package/header-scoped `[bailey317] admission` diagnostic now
reports the first failed mission/native check and the first differing definition
byte, or changed birth fields. It grants no permission and leaves every pin,
lease, denylist and default unchanged. In particular header activation byte0xE
is still compared exactly; changing it to1 is an offline negative control,
not a measurement from that run. The logged five receipts replay as spent in
`BaileyCreeperAuthorityTest`; all5472 definition-bit mutations remain refused
and report the correct byte. A new reviewed live probe is required to name the
actual refusal before changing authority.
