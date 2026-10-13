# Late-join population repair (VUH-1788)

Repair-06 is independently reviewed and lead-ADOPTed. Live run
`20261009-000023_vuh1788_late_join_population_repair_1` passed the frozen fixture's
own oracle and safety closure. The repair landed in `2d45f39`; the population
cull hold and gated factory landed earlier in `a91c301`.

## BC Soldier play-route check, 2026-10-12

The landed code passed [run 203547](../build/scenarios/20261012-203547_net_population_soldier_late_join_1/report.md)
with `KH2COOP_ENEMY_MIRROR=1` and `KH2COOP_ENEMY_POPULATION=1`;
`KH2COOP_ENEMY_POPULATION_SPAWN` was unset. The host triggered three Soldiers
at the west activator and killed one before Friend1 joined. The friend stayed
over 2,000 units from that point and received the wave through the existing
native activation lease. PopulationCut authorized one native stale-copy death
at frame 345 and confirmed physical absence at frame 406. Both peers matched
living IDs 2/3, object 301, HP 39. Killing the remaining host Soldiers applied
native deaths on the friend; both complete actor lists then contained no enemies.
The [host clip](../build/scenarios/20261012-203547_net_population_soldier_late_join_1/host_shared_soldiers_0.mp4)
shows the surviving wave; the report/logs establish the friend's matching set
and subsequent absence.

An earlier [run 203145](../build/scenarios/20261012-203145_net_population_soldier_late_join_1/report.md)
also passed with SPAWN enabled, but made no factory calls. These runs validate
the existing native death/removal and late-join paths; no additional native
removal implementation was needed. Focused population planner, mirror driver
and native repair tests passed 3/3. Four protected files and installed game
files were unchanged, and owned cleanup completed for both runs.

Proposed play flags: MIRROR and POPULATION enabled. Generic SPAWN stays parked
because neither run exercised creation and its native family-readiness gate
still applies. Alive-despawn/supersession, reconnect and other families remain
unqualified by these checks. The existing ordinary alive-despawn path reports
a death after its three-second grace; this fixture exercised host kills instead.
The lead owns promotion; no profile or new authority machinery was added.

### Alive-removal attempt, 2026-10-12: inconclusive

[Run 204320](../build/scenarios/20261012-204320_net_population_soldier_alive_despawn_1/report.md)
failed its living-removal event wait. The
[Soldier walk clip](../build/scenarios/20261012-204320_net_population_soldier_alive_despawn_1/host_walkaway_0.mp4)
and position samples show camera-relative lateral input orbiting near the west
wave instead of walking far away. All three Soldiers stayed at 39 HP. A fallback
within the same owned session to BC btl1 also left its two Shadows and three
Hook Bats alive. Neither path witnessed a living removal or supersession, so
this does not establish a removal failure or prove Soldiers never cull at any
reachable distance. No native behavior change was made.

Four protected files and installed game files were unchanged; all owned games,
runtimes, relay and clip helpers exited, and UDP 27796 was free. PC1 was released
for James after this last run. Alive-removal acceptance remains open until the
lead authorizes another live window; the next fixture needs measured outward
world movement rather than a sustained sideways pulse.

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
