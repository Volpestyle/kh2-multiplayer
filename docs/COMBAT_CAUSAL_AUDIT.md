# Saved combat-causal log audit (VUH-1503 AC3)

`tools/scenario/combat_causal_audit.py` reads the original host and friend inject
logs. It accesses files only. It follows the existing `trace_audit.py` pattern:
strict saved-record parsing, line-specific issues, input SHA-256 identities,
JSON output and a nonzero exit for incomplete evidence. It changes no product,
wire format, game behavior or live fixture.

```powershell
python -B tools/scenario/combat_causal_audit.py --host HOST_INJECT.log --client FRIEND_INJECT.log --output .local/combat-causal-audit.json
python -B -m unittest discover -s tests -p test_combat_causal_audit.py -v
```

The intended input is the **pending two-PC shared-combat run**, with the landed
default-off causal diagnostic explicitly enabled on both machines. The bounded
encounter is five object-302/type-4 Shadows in BC `[5,6,0,1,1,0]`, including the
host's genuine native hit, the friend's claim/native HostApply and natural death.
Use the original complete inject logs through the final drain cutoff; keep the
runtime and native-hit logs with the run for separate qualification. Native-06
test output and the historical JOIN27 package are not live input evidence. This
tool neither launches nor authorizes that pending run.

The graph uses these edges:

- A host native `Hit` or `HostApply` is a local source, with checked recorded
  before/after HP and immutable admitted target/scope. HostApply additionally
  joins exactly one client Claim by its full connection/sequence/epoch/target/
  object/attack/damage key, then exactly one client Hit by its local event ID.
- Every host HP/death publication must carry the exact ordered, unused cause
  IDs that explain its change from the preceding published HP. A raw HP batch
  must retain every target entry, including unchanged companions.
- Client accepted-batch and per-target receive rows join the host publication
  using the **entire raw encoded payload**, target and HP sequence. Client HP/
  death application joins a unique preceding receive and checks the recorded
  call/readback and local HP chain. A changed client HP value must resolve to
  exactly one host source with the same before/after values.

QPC ordering and pointer identity are checked within one log only. Host and
friend addresses, local load/transition counters and clocks may differ. Source
event IDs in the report refer to the host log; `sourceRecords` gives their kind,
line and receipt serial. Rejected consumer rows are observations of refusal,
never admitted causes. Their supplements must retain original scope and complete
raw bytes; a truncated prefix does not qualify as a complete record.

`orphanFindings`, `duplicateFindings` and `unqualifiedCoverage` are explicit JSON
lists. Missing admission, scope/root replacement, unqualified operations, loss,
retirement, gaps, malformed records, missing supplements and an undrained tail
refuse consistency. An aggregated client store with several genuine causes is
reported as `multiple_sources_in_application`: it cannot establish AC3's
exactly-one-source condition, but is not called a duplicate native hit. A
no-change periodic publication/readback requires no new damage cause.
Empty, loss-free summaries before the first admission receipt are allowed;
`recordedRanges` identifies the bounded receipt interval being audited. This
does not qualify gameplay before admission.

The top-level result separates recorded graph consistency from coverage:

| Result | Meaning |
| --- | --- |
| `FAIL` | The supplied records contain orphan/duplicate/ambiguous sources or contradictory graph facts. |
| `INCONCLUSIVE` | No graph contradiction was established; inspect `recordedGraphConsistent` and coverage issues for completeness. |

**The current schema can never produce PASS.** Its actual summary hardcodes
`nativeCoverageQualified=0` and `acceptance=0`. Even a complete, consistent graph
returns exit 1, `coverageQualified=false`, `exactlyOneSourceProven=false` and
`acceptance=false`. Forging either summary flag to 1 is an unsupported assertion,
not an override. Exit 2 means a file/invocation error. There is no relaxed mode.
The recorded summary is a drain cutoff, not independent proof of shutdown,
continuous native coverage or absence of later hits. Exact runtime session
authentication, native-hit source validation, full HP-writer coverage and
same-pointer incarnation require separate evidence; local `qualified=1` is not
that proof. This auditor cannot close AC3 by itself.

Synthetic controls render the production `CombatCausalTrace.hpp::Drain` format
strings **and ordered argument expressions**, extracted directly from source by
`tests/combat_causal_serializer.py`. A deliberately small Python interpreter
evaluates those copied-facts expressions; unsupported serializer changes fail
the controls. Receipt facts and packet bytes are synthetic. This exercises the
real serializer's field/argument contract without executing a C++ binary; it
does not run Engine admission, native hooks or a game. Controls cover native hit,
friend claim/application, native-zero death, no-op readback, multiple-source
aggregation, field-by-field omissions and graph/coverage mutations.

Initial self-check: 17 Python test methods and 174 table-driven subcases passed
with subprocess, socket and native-library loading denied by a Python audit hook
(zero attempted external operations). The existing saved-log auditor suite also
passed: 107 methods, one optional historical skip. No native build/test or live
run was performed. These synthetic controls validate the auditor, not AC3.
