# Companion causal diagnostic: PC1 practice candidate

The ordinary BC membership successor is documented in
[ORDINARY_ENEMY_LIFECYCLE_PRACTICE.md](ORDINARY_ENEMY_LIFECYCLE_PRACTICE.md).
It preserves the source predicates and hard UnknownDelta guard.

The host damage policy already admits a positively observed native AI companion. Causal08 serial101 and causal10 serial151 recorded Goofy as NativeCompanion/HostCompanionAttack, but the separate causal witness only understood the canonical player Apply->Take->Stat path. Both real companion hits used Apply caller0x410EFF and one direct Stat caller0x3D37D2, reducing a Shadow from20 to18. Removing the ally avoids this requirement rather than meeting it.

This candidate adds HostCompanionEnemyHit beside unchanged PlayerEnemyHit. It requires the actual copied production policy decision and authority snapshots to match the event's original host context, roster, actors and hit record. It requires stable type1/team1 source metadata, canonical-player exclusion, one explicitly bound direct Stat under this Apply, exact HP arithmetic and no intervention/claim. NativeHitTrace now records each child's enclosing applySeq. Existing Take/Stat matching and unmatched counters are unchanged: direct Stat is not silently reclassified as a matched Take child. Unknown-source retirement remains unchanged.

CombatCausalTraceTest includes copied facts generated from both retained logs and drives them through actual BeginApply/BeginStat/EndStat/EndApply callbacks. This establishes the new parent token relation in the test; the old logs did not contain applySeq and are not retroactively passed or upgraded. Tests cover missing/mismatched policy, client companion, foreign owner/authority, stale roster/frame, forged child, player masquerade, duplicate source and exact publication. Existing player controls are unchanged. The candidate only extends the in-DLL causal observation; it does not change damage policy, AI ownership, native HP behavior, protocol, party configuration or source05's hard guard.

Scope: unsealed PC1 practice config A (two local instances plus optional Goofy), operated by kh2-steam under its native/live lane authority. No seal or per-change review2 for practice; review2 is required before landing. No PC2 or main edits. Native controls/build status is provided in the external handoff; generated fixtures and static preservation checks alone are not a compiled/native PASS. Existing schema1 continuous/native-coverage limitations still apply.

Config B is the separate frozen c7a92e49 row-only empty-party candidate. Its repeated avatar/world test failures remain real and unresolved; do not blend that product delta into this config A candidate or claim its native qualification passed.

## Combined practice candidate after practice05

Practice05 reached all eight actions. Its first host retirement was Sora, not Goofy: type0/canonical P_EX100, actual HostLocalAttack policy, Apply0x410EFF -> direct Stat0x3D37D2, HP20->17. `HostPlayerDirectStatEnemyHit` is a separate host-only witness for that exact path. It binds the copied policy and authority to the original scope/roster, canonical owner, hit record and new Apply parent token. `PlayerEnemyHit`, `HostCompanionEnemyHit` and the UnknownDelta guard remain unchanged. The original player witness remains unchanged; the separate client direct-Stat extension below binds the actual queued claim. None of these changes reinterprets a retired historical graph.

The copied practice05 event regenerates with `tests/practice_player_retained_fixture.py`. Native controls establish its parent linkage through actual callbacks, reject 33 policy/owner/child/client mutants, account one source in ordinary HP publication, and refuse duplicates. Old logs do not contain applySeq; historical fixtures are not retrospective acceptance.

The friend's other retirement occurred when original netId1/actor7FF7D5DB7550 left a complete census at HP20, before the host hit. The default-off `CausalDisappearance` tracker remembers only the original admitted five targets and selects at most one report per missing actor in the original scope. Incomplete or foreign censuses do not prove absence. The additional EnemySync probe records a watched complete-list traversal, classifier/read/exclusion evidence, actor metadata/HP, original/current controller and record, fixed-size raw actor/controller/header/record bytes, scope bookends, and manifest/HP-sequence lifecycle. Its output is diagnostic only, never admission or gameplay authority. Extra native enemies do not conceal a missing original. Mapping retirement and all gameplay decisions are unchanged.

`tools/scenario/causal_summary_acquisition.py` owns bounded read-only acquisition before the irreversible source05 `Admission.check`. Steam supplies `read_sample(absolute_qpc_deadline_ns)`, the unchanged activation validator, and the original cancellation event. Every CLI/read/worker join must be capped to that deadline. Acquisition is clamped to three seconds/the existing outer deadline and 64 attempts; waits are cancellable at most50ms. Only no new summary or admitted0 may wait. Scope/safety/prefix changes, retirement/loss, cancellation or expiry refuse; no resetting an Admission or re-anchoring. The one-second producer summary cadence remains unchanged; summaries append owner-frame and original scope fields for the fresh admitted proof. Return is the original complete bracket plus retained acquisition attempts, then the caller invokes the same Admission once. Preserve failure attempts, check safety before any later input, and keep dispatch timestamps/freshness classification unchanged.

Offline verification: `tests/practice_combined_controls.py <practice05 evidence> --oracle <source05 oracle.py>` runs the actual acquisition helper with retained no-summary brackets, constructed later producer summaries, cancellation/deadline/scope/loss mutants and exact gameplay/witness preservation projections. Constructed successor summaries are explicitly not retained native evidence. The C++ copied-facts controls require Steam's native build slot; no build or game run is performed by these Python checks.

Sampler failures expose `AcquisitionFailed.attempts` and `.last_sample` for the operator's external failure retention. The reader must retain every raw bracket independently. The helper checks each read started after its local request and finished before observation on the same QPC clock; cached/mixed-clock brackets refuse. Acquisition failure must not fall through to an input using the previous sample.

Known separate base-test regression: Steam's retained `practice-base19a-01` bisect rebuilt exact19aae6cd without the BC config delta, then avatar/world failed all six serial runs. This rules out the BC delta as necessary to those failures, not a specific earlier commit. The combined candidate adds no avatar/world/runtime behavior and does not repair or waive those failures. Report new causal copied-facts controls separately; do not label an unrun or failing full suite PASS.

## Client direct-Stat extension (Lead16)

The same Apply0x410EFF/direct Stat0x3D37D2 family occurs on the client in practice05 events7 and9. Both copy the real LocalAvatar/ClientClaim/ClaimThenZeroHp decision, revalidate successfully, queue a claim, zero the amount, and leave local enemy HP unchanged. `ClientPlayerDirectStatEnemyHit` is a distinct client-only suppression witness, never a host damage cause. It requires the authenticated canonical source, original scope/roster, copied actual decision/authority, exact Apply-parent child, zero delta/result readback and unchanged local HP. The authority snapshot precedes SyncDropsHit: its syncDrop is false while the observed hit's is true. This single documented phase difference is checked explicitly; all other identity fields must agree. `ZeroResult::Changed` is expected when the existing sync-drop filter already zeroed the amount; Changed alone never qualifies without amount0, unchanged HP and the exact zero-delta child.

A fixed16-entry diagnostic-only association table counts actual Engine::Claim calls by Apply token, including rejected attempts. The witness consumes exactly one validated encoded queued claim with the same frame, connection, epoch, target, attack and amount. Missing, duplicate, foreign, malformed or reused associations refuse. Capacity exhaustion retires the diagnostic. Client event tokens cannot be reused and no client hit enters the host cause ledger or changes expected HP. This adds no queue operation, native write or authority rule; the existing player/companion/host-direct predicates and UnknownDelta guard remain unchanged.

`practice_client_retained_fixture.py` copies both real events and exact queued keys. Compiled controls bind the parent through actual NativeHitTrace callbacks, check36 mirrored predicate mutants per event and17 real-codec/Engine association cases per event, including zeroed amount, unchanged HP, missing/rejected/duplicate/mismatched claims and repeated event refusal. The original practice05 observer was already retired; these fixtures do not retroactively qualify that run. Native compilation/execution remains Steam-owned. The whole candidate includes the host direct-Stat witness, disappearance probe and bounded fresh-summary acquisition from the preceding combined change; no separate live retry is implied.

## Main-based practice successor (Lead17)

The practice-only three-commit stack is rebased onto exact main3c0f8e422ec6ac1b819e1a6bc2b95222b5a9f7a8, which already contains the causal diagnostic. Git range-diff reports all three patches identical; the prior19aae-based branch and856f09d remain intact. This retains main's networking/protocol15/16 behavior rather than carrying the older branch integration. Preservation controls now compare against the corresponding main-based commits. The avatar/world regression is not diagnosed or waived by this rebase: kh2-steam must run kh2coop_avatar_test and kh2coop_world_test and require both PASS, alongside kh2coop_combat_causal_test, before using this combined candidate. No native build, live run, PC2 action or landing is implied by the offline handoff.


### Continuous ordinary death binding (practice successor to ca007d4)

OrdinaryEnemyBinding now retains an authenticated terminal binding through
consecutive dead observations. Only the original netId, sampled roots,
generation and epoch may continue, with exactly one frame between observations.
A missing frame, changed/reused identity, unknown dead body or resurrection still
refuses. A refusal clears the current binding; terminal history remains so the
same tracked body cannot recover as a new living candidate. The Mapping hard
guard, native death application, player witnesses and UnknownDelta are unchanged.

The 856 A friend log records established death at frame 984, qualified native
DeathApply HP 2?0, then identical roots rejected at frame 986. The copied-fact
regression uses those retained roots and frames, with an explicitly constructed
continuous dead frame 985 (not retained in the log). It passes frame 986 under
that continuous precondition; a missing-frame variant refuses. This does not
retroactively qualify the spent run. Controls in EnemyRecordBindingTest cover
living?dead?dead?dead, every root, netId, epoch/generation, gap and resurrection,
including refusal on subsequent attempts. Native execution is pending the next
kh2-steam build; no live authority follows from this source handoff.
