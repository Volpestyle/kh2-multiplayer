# Forced population registration: implementation boundary

The native analysis and forced-membership-01 design require five roots for a
record-backed forced enemy. A generic factory return with controller/record zero
does not qualify. The design is adopted; the implementation is not yet a
runnable convergence product.

## Source preparation

`inject/src/PopulationRegistration.hpp` defines a bounded enrollment ledger and
one-shot dispatch boundary. Enrollment must come from an actual completed native
initialization, an exact calibrated five-record Shadow definition, empty complete
cache and continuous event coverage. Aggregate positive counts do not create
per-record credit. A native death consumes that record's obligation; an observed
alive native removal with unchanged counts and physical absence can release it.
Replaying initialization cannot restore spent credit.

Dispatch requires two current captures of source, census, admission, record
bookends and construction authority. The attempt is retained before the native
call, survives source reauthorization, and requires post-call recapture even on a
fault. Success additionally requires the returned actor itself, all five roots,
subtype parameters, physical census membership and fresh source/scope bookends.
An ambiguous post-call mutation poisons the ledger.

`tests/PopulationRegistrationTest.cpp` exercises this boundary directly. These
are test sources, not a claim of executed native validation. The header is not
connected to EnemySync and cannot currently invoke a native wrapper in-game.

## Missing production authority

The existing native tracing interfaces explicitly do not grant creation
authority:

- `NativeSpawnController::Install` disables policy-qualified first emission and
  enrollment under the FLS raw observation profile.
- `NativeConstructionLineage` distinguishes sampled ancestry from fiber
  continuity, creator exclusivity and global pending exclusion.
- `NativeSelectedOccupancy` does not certify global pending exclusion. The
  active-actor census does not enumerate every pending native creation.
- `RegisterDiagnosticGameThread` establishes thread affinity only.

These observations cannot be converted to true authorization booleans. Positive
dispatch still requires a proven owner activation and construction exclusion
boundary, actual initialization/event ledger production, and a complete pending
creation inventory. Unknown authority must cause zero native calls. No counts,
native pointers or cache entries are written by this preparation.

## Host authority does not eliminate local membership

Host authority covers replicated enemy life and source PopulationCuts, and
`ProgressSync::Tick` publishes the host's allowed SAVE progress results. It does
not replace every native client event or wave decision. In
`NativeSpawnController::RunUpdate`, a qualified client still executes the native
controller update with a copied host activation point; unsupported controllers
and callers pass through to the original update. `EventHoldNative::Install`
explicitly advertises `firstSliceEmptyRoomsOnly=1 noScriptFreeze=1`.

Consequently, it is not justified to discard native count membership merely
because replicated progress is host authoritative. The native death/count and
removal dependencies in forced-membership-01 remain relevant. This conclusion
is from product source; it is not a new live progression proof.

## Validation and packaging

Native build, serial CTest and sanitizers require a distinct reserved operator
slot. No lease or gap waiter is armed for this source preparation. Reconnect04
remains an unsealed offline draft with strict five-root binding and retained
anchor/removal history. Its new-birth publication check still needs the final
product occurrence and native registration receipts before live sealing.
