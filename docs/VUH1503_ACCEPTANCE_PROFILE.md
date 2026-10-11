# VUH-1503: protocol17 / practice A acceptance preparation

Base/product source: landed main c1b0469e (tree f50043b9), the claim-release
re-hold on top of d9412a21. The two profile commits were re-stacked onto this tip.
This branch changes packaging and source-owned profiles only. No binary build,
PC2 contact, injection, live attempt or AC3/native-coverage change.

`tools/rig/combat_acceptance/product-source.json` binds the candidate source/tree
to Steam's existing `.local/build-practice-claim-release-native-loss-native-01`
products, referencing its actual manifest at
`.local/rig-operator/practice-claim-release-native-loss-native-01/run/products.json`.
The build directory itself has no products.json. The exact completion receipt
records TEST_FAILURE (184 passed,3 failed), not a qualified full-suite PASS.
This binding preserves that result and is NOT a replacement build receipt for
build_friend.py: portable/internal tool qualification and the full coherent
product set still require Steam/review2's disposition. No rebuild or binary
inspection was performed by this lane. Profiles/packaging remain unadopted.

## Protocol metadata disposition (for review2)

The inherited builder's literal protocol10 is stale metadata, not evidence that
its selected DLL/runtime/server binaries speak protocol10. It is unsafe to fix
only the label or reuse historical mixed-source products. The old
friend-products.json is retained as historical pins and now FAILS admission;
it must not be relabeled17. A new reviewed product receipt is required.

The builder reads PROTOCOL_VERSION from this checkout (17), checks its supported
launcher contract, and checks AvatarBridge against the candidate header (4,
also stale in the old builder). It requires exactly DLL/runtime/server/avatarctl
plus the separately supplied CLI to share protocol17, sourceRevision and buildId.
Every row carries its real sha256; the CLI path/hash must match the explicit CLI
arguments. The ZIP records these per-product bindings. The friend verifier
refuses missing, mixed, legacy, changed-hash or disagreeing-path metadata before
launch. These checks run without native tools.

Receipt shape (substitute actual reviewed pins; placeholders are NOT usable):

```json
{
  "transport": "steam", "protocol": 17, "avatarBridgeVersion": 4,
  "products": {
    "dll": {"path":"<DLL>","sha256":"<64 lowercase hex>","protocol":17,"sourceRevision":"<40 lowercase hex>","buildId":"<one reviewed build>"},
    "runtime": {"path":"<runtime>","sha256":"<hash>","protocol":17,"sourceRevision":"<same revision>","buildId":"<same build>"},
    "server": {"path":"<server>","sha256":"<hash>","protocol":17,"sourceRevision":"<same revision>","buildId":"<same build>"},
    "avatarctl": {"path":"<avatarctl>","sha256":"<hash>","protocol":17,"sourceRevision":"<same revision>","buildId":"<same build>"}
  },
  "cli": {"path":"<portable CLI>","sha256":"<hash>","protocol":17,"sourceRevision":"<same revision>","buildId":"<same build>"}
}
```

This is build-receipt validation, NOT binary introspection or attestation. The
reviewed native build/provenance must establish the declared protocol/source
against actual bytes; lying consistently in all receipt fields cannot be
caught by metadata. Runtime handshake remains the final wire-version refusal.
No claim of an independently reviewed coherent product set exists yet.

## Default-OFF decision

Both peers explicitly use KH2COOP_ORDINARY_LIFECYCLE_EXPERIMENTAL=0.
The friend child environment clears inherited KH2COOP switches and sets this
flag OFF; package admission also requires ordinaryLifecycleExperimental=false.
The reviewed acceptance wrapper deliberately sets
KH2COOP_COMBAT_CAUSAL_TRACE=1 on both game AND runtime, after sanitization.
Trace is observation, not the lifecycle experiment. No population repair,
invulnerability, HP write, party mutation or save import is permitted.

## Executable source profile

`tools/rig/combat_acceptance/profile.py` is an executable per-role profile with
a reviewed packet Adapter. It reuses byte-identical canonical-health,
pre-activation and population gates from practice A; source hashes are retained
in practice-provenance.json. It does not import the evolving practice runner or
its absolute developer paths. The copied population module's legacy ACTIONS
and classify helpers are NOT used: profile.authorize owns the exact six inputs.

After the existing host-only BC warp/product-follower arrival, verify the
friend's original canonical actor at (-64.36,-260,1950), radius30, yaw pi +/-0.05.
Then execute ONLY:

1. friend_position_0: friend forward900
2. friend_position_1: friend forward900
3. friend_position_2: friend forward900
4. host_activate_0: host forward900
5. friend_claim: friend Cross80
6. host_hit: host Cross80

Both peers check original identities, native safe tuple and canonical HP>12.
The three movements require no enemy/admission yet and permanently close at
activation. Subsequent attacks require paired immutable typed population
admission. A native hit straddling a bracket permits ONE complete reacquisition
within the SAME3s read budget only when the original typed hit chain explains
the precise HP delta. Identity replacement, unreadable/low HP or persistent
skew stops. No input retry. An ACK is mailbox delivery, not damage.

Paired evidence begins without a new recording-live input barrier. The two
Cross turns each have a passive observation window <=3s, within the ORIGINAL
90s encounter deadline and original JOIN600/cleanup reserve. Nothing renews
these budgets. HP safety can terminate observation at any time. No extra
attacks are allowed to manufacture a kill or an incoming hit.

`evidence.qualify_incoming` runs the UNCHANGED candidate native-hit auditor,
requires a complete qualified enemy-to-canonical-friend hit in the original
scope, canonical HP before/after, matching provenance-bound host puppet HP,
and BOTH exact-hash game-only impact/coverage/privacy reviews. It cannot infer
visual consistency from mailbox ACKs, cached slot HP, one view or missing media.
It emits only QUALIFIED_RECORDED_HIT for AC2, never all-criteria acceptance.
Full graph/AC3 still belongs to review2; its flags and auditor are untouched.

The friend_claim path remains before host_hit for AC1; retain full claims,
unique host native applications, HP publications/application and death logs.
Six inputs do NOT guarantee a client-only kill. If none occurs, AC1 is
INCONCLUSIVE for this run, with no added Cross/retry. Historical AC1 stays
separate. Final profile marker is INPUTS_COMPLETE_EVIDENCE_PENDING, acceptance
false until independent safety/media/criterion replay.

## Packet adapter contract and remaining runnable gates

Run as `python -I -m tools.rig.combat_acceptance.profile --adapter <sealed-module>
--role host|friend --config <fresh-config>` with this source root explicitly in
the bundled runtime _pth, or call Profile(adapter).run() from the packet.
The adapter is a SOURCE integration prerequisite, not a supplied arbitrary
operator script. Its exact bytes must be pinned and reviewed before any GO.

Required adapter members:

- contract: exactly `profile.adapter_contract()`, generated from this source at
  packet assembly. Both Profile and PacketAdapter refuse a mismatching budget or
  action sequence before native I/O. Do not maintain another action/budget table.
- role/run_id/protocol, exact game+runtime environment; owned and peer_owned
  ORIGINAL {pid,creationTicks,moduleBase,imageSha256}; authenticated
  runtime_session; ORIGINAL local encounter_started_ns (host warp preparation,
  friend earlier diagnostic-ready) and deadline_ns in local QPC domain.
- clock_ns; check_stop_and_ownership(original); original_identity(); ram with
  READ-ONLY base/value(address,format), using retained owned handles/modules.
- sample(deadline_ns): complete native/raw-log/bridge bracket, including fresh
  owner-frame summary for full admission, within the supplied ORIGINAL budget.
- inject_log(); retain(name,raw_record) writes outside immutable packet.
- exchange(stage,payload,deadline_ns): existing authenticated nonce/run/role
  transport, never cross-peer QPC arithmetic, no duplicate/replayed stage.
- dispatch_once(action,argv,final_gate,deadline_ns): existing canonical-owned
  local slot0 CLI. Call final_gate AFTER blocking transport/media work, then
  final memory-only STOP/deadline, actual QPC stamp and single helper spawn;
  retain original helper identity/argv/ACK. No logging between final stamp and
  spawn. Cancellation must stop owned processes even if a callback fails.
- action_acked(action,result): advance the adapter's source-ordered history only
  after Profile validates the paired ACK. A local ACK requires its original
  single dispatch attempt; failure never enables another attempt.
- begin_paired_evidence(deadline_ns), passive_observe(end_ns): reads/media only;
  retain both endpoint native+canonical samples and game-only recording/QPC.
- stop_owned_and_retain(): original Event first, existing owned game/job stop,
  media joins, original cleanup deadline; preserve errors/UNKNOWN for independent
  canonical inventory/descendant/job/port/protected-file closure.

The source-owned projection is `adapter.PacketAdapter`. It reads the sample cap
and action table directly from profile.py. Every bracket read and summary wait
receives the SAME absolute deadline, capped by SAMPLE_BUDGET_NS and the original
run deadline. The inherited diagnostic.sample five-second wait is not called.
Dispatch calls the existing Driver diagnostic hooks directly, never the inherited
Encounter.dispatch eight-action authorizer. The durable attempt precedes the
final native gate; process_journal invokes the memory-only final QPC stamp at
actual creation. Hook cleanup is unconditional and an attempted dispatch cannot
repeat after gate failure, cancellation or ACK loss.

`adapter.DriverCalls` supplies the actual Driver.call/avatarctl/read-log leaves.
It refuses phase.child's unowned fixed-timeout fallback: the original installed
Reclaim.child must own helpers. It temporarily narrows Driver.deadline by a QPC
remaining-duration conversion, restoring the original budget afterwards; it
never compares QPC and monotonic epochs. All other packet services (authenticated
exchange, original read-only RAM/identity, media, retention, STOP and independent
closure) remain required and must be bound/reviewed in the final sealed packet.
Neither helper starts a process at import.

Still required: actual coherent source-matched protocol17 native products;
final packet service binding and full actual bundled Setup/child/import replay;
independent review2 SAFE/lead adoption/new sealed packet; positively free PC2,
standing availability consent/session/account/window and fresh PC1 gate; paired
impact footage and closure; AC3 coverage solution separately. No old seal,
result or GO is reusable. Today's practice A itself had record-only population
oracle refusals and no client kill; it proves choreography, not acceptance.
This profile keeps those safety gates HARD, so an unresolved candidate admission
failure will stop rather than inherit practice's record-only disposition.

## Offline checks

```
python -m unittest discover -s tools/launcher -p test_friend_package.py
python tools/packaging/test_product_protocol.py
python -m unittest tools.rig.combat_acceptance.test_profile tools.rig.combat_acceptance.test_evidence
python -m unittest tools.rig.combat_acceptance.test_adapter
python -m tools.rig.combat_acceptance.retained_controls --evidence <retained-A> --output <outside-packet.json>
```

These are metadata/source boundary and retained-data checks. Native/transport
leaves in orchestration controls are standins; there is no native build, wire
compatibility, paired renderer or live acceptance qualification from them.

Final local validation:26 launcher tests +6 packaging tests +11 profile/evidence tests PASS;5 retained-A checks PASS. No full ctest requested or run. Independent review2 disposition remains pending.
