# Bailey and LoD route spawns (VUH-2343, VUH-2352)

The implementation is ready for independent review and a Windows build/live
probe. Portable policy tests pass under ASan and UBSan; these tests do not prove
native constructor, FLS/unwind, attachment, mission completion or live parity.
The preceding observation runs are failures, retained on the issues.

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
wrapper's checked null-return path. After body allocation, a lost constructor or
status scope raises noncontinuable SEH `E0423433` rather than returning a fake
base-constructor result or falling back to shared status. The derived Sora
constructor assumes its base completed. A negative fence also aborts the known
key1/form0 constructor allocation or fresh-record initialization when its active
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

Protocol23 and `SpawnEmission.point` retain their existing controller activation
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
