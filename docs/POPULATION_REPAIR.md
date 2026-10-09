# Late-join population repair (VUH-1788)

Repair-06 is independently reviewed and lead-ADOPTed. Live run
`20261009-000023_vuh1788_late_join_population_repair_1` passed the frozen fixture's
own oracle and safety closure. The landing candidate is rebased onto main
`079f9a3` in `.local/wt-waves-landing`; that rebased tree still needs its scheduled
build and full serial CTest before landing. The shared root is unchanged.

## Scope and authority

The feature remains default-off under `KH2COOP_ENEMY_POPULATION` and
`KH2COOP_ENEMY_POPULATION_SPAWN`. Protocol16 adds reliable host-only PopulationCut
(message48) carrying bounded complete record definitions, ordinal occurrences,
birth/terminal sequences and actual native birth-window activation arguments.
The relay checks full location, complete manifest/death coverage and source cut
sequence floors; bootstrap preserves authenticated source/target delivery scope.
Resync invalidates cached authority instead of inventing certificates. Producer
publication bookends all five native roots and the announced incarnation.

Late clients receive separate terminal disposal tickets. Owner-frame consumption
requires the newest terminal incarnation with no living occurrence for the same
record, unique exact local content membership, unbound local ownership, current
transport/manifest/native room scope, and five-root bookends. A fresh complete
watched census after the final HP read precedes the verified native ApplyStatDelta
one-shot. Ordinary bound death authority stays strict and unchanged.

A bounded process-lifetime journal keyed by five roots plus local load/transition
preserves one-shot poison across ticket cancellation, cut reauthorization, native
fault and failed readback. It refuses at capacity instead of evicting attempts.
Every call or fault stops the frame after recapture, and subsequent work rebuilds
from a complete current census. HP zero is pending removal; only fresh physical
native-list absence after an attempted call qualifies a disposed receipt.
Per-ticket outcome JSON includes refusal details, call/wait/cancel/disposed states
and explicit receipt loss. Admission usage and controller counts are observed,
never rewritten.

Missing living ordinary records may receive historical native activation inputs
for at most600 resolved frames. Generic factory creation still requires observed
current-load native family readiness. Admission is computed per candidate before
planning and reread immediately before dispatch. A refusal consumes no attempt
or missing-clock reset. No native budget-policy change is included.

## Live evidence and limits

Repair-05 installed cut12 but failed to consume stale terminal copies because
actor+120 bit28 was clear throughout its five samples. See
`.local/repair05-live-230919-diagnosis.json` and the earlier
`.local/pj08-live-203406-diagnosis.json`. Repair-06 removes that predicate dependency.

In the passing repair-06 run, late-client PID289924 authorized and consumed
terminal ID1 at frame159, then confirmed physical absence at frame219. Controller
count fell5 ->4; observed used budget stayed48 ->48. Both complete post-admission
samples matched all three peers at living IDs2/6/7 (Shadow302, HP20/20) and8/9/10
(Hook Bat4, HP28/28), with positive unique stable bindings and439 frames between
endpoints. No late-client admission-wait or budget-wait receipt was logged.
This proves scoped late-join convergence and terminal disposal, not budget
reclamation, reconnect, or forced-host-id spawning. No force-spawn receipt exists.

The oracle preserves immutable host body/incarnation/lifecycle and pre-admission
terminal cut authority. Both source and installed cuts must respect that floor.
Any witnessed living native-list removal fails regardless of HP text, even after
endpoint parity returns. Lost receipts, duplicate/unbound bodies and superseded
installation cannot pass. Sampling/persistence retain90s/3600-frame and overall
900s deadlines. Setup retains the reviewed full-scope placement guard.

All-target Release build and120/120 serial CTest passed on sourcea9c3290; codec
ASan/UBSan passed. Native-helper sanitizer is INCOMPLETE because GCC13 internally
crashed during compilation (compiler bug); Lead waived it as a live gate and a
separate alternate-compiler slot remains required. Native controls exercise the
actual producer/consumer over owned stand-ins, including final-read substitutions,
reauthorization poison, faults, reentrancy and physical absence. Seven socketless
Python methods validate diagnostic continuity; anchored native acceptance remains
the fixture oracle. Packet `build/rig/vuh1788-waves-20261008/population-repair-06/`
retains frozen products, reviews, build receipts, closure and live-verdict.md.
