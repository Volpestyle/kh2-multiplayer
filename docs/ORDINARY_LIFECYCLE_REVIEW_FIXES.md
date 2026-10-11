# Ordinary lifecycle review disposition

The protocol-17 BC membership path is experimental and defaults off. Both peers
must explicitly set `KH2COOP_ORDINARY_LIFECYCLE_EXPERIMENTAL=1` to exercise it.
Population/repair mode selects its existing schema-1 authority instead, even
when that flag is present. Schema-2 removal never supplies death or kill credit.

There is no production native allocation-lifetime witness for a living host
Shadow leaving the list. The experimental adjacent-frame readable-root check
does not become such a witness. If it fails, membership publication is suspended
for that load immediately; it does not retry the departed allocation. Present
bodies can still publish manifest, HP and motion. Clients retain their original
native admission, exact-release and no-live-relabel guards. This fallback cannot
qualify a replacement or complete a missing lifecycle. Absence or grace expiry
does not generate a death. A new load clears the suspension, not the old binding
into a new load. Production lifecycle acceptance remains blocked on a proper
native removal witness and independent qualification.

Authenticated incoming cuts retain their original transport scope for at most
30 seconds, at most 16 cumulative cuts. Native arrival/catalog availability may
delay admission; it does not silently discard the cut. Tick retries after native
arrival and tracking, with every original admission bookend unchanged. No duplicate
renews a deadline. Session, connection, delivery, host epoch or an already-arrived
native-load replacement retires the queued authority. Expiry/capacity refusal is
explicitly logged and supplies no permission to release claims.

Outside this mode, an unsupported living refill holds its own historical row
without freezing other publication. It neither manufactures a remote retirement
nor relabels an existing live client body. A checked same-actor metadata replacement
supersedes only the old local sampled identity. Schema-1 repair still needs its own
complete certificate; an unresolved living historical row cannot mint one.

The BC absent-party configuration from `ca007d4b` is practice-only and is not
approved for main by this work. The practice six-action pass proves friend claim
application, not a complete host-hit/lifecycle causal graph or issue acceptance.

Native execution of the added consumer/producer controls belongs to kh2-steam.
Offline source checks and Python controls do not substitute for that build.

## Retained suite failures

The exact `6472df95` run was **185/188**, not a clean suite pass. Its raw output
is `.local/rig-operator/practice-native-first-release-control-native-01/run/tests-01-stdout.txt`.

| Suite | Retained failure | Range attribution |
|---|---|---|
| avatar | Lines 285–298: proximity maxima 106–111 units, relay count, loss `4294967295` | Not unique to this range: exact main `3c0f8e4` reproduced proximity, relay and the same sentinel in `.local/rig-operator/practice-main-timing-01/report.md`, three serial repetitions. The range changes only the protocol-version compatibility assertion in AvatarRelayTest; avatar stream/test predicates are unchanged. This rules out the earlier branch-only inference, not every possible interaction. |
| revive | Lines 80–81: accepted request did not reach owner ring, so the dependent single native-attempt check failed | Unadjudicated. Revive request/pump/owner-gate source is unchanged; the test changes only its protocol assertion/label. The range does alter shared NetworkClient/SessionHost code, for activation reliability and membership cuts. Those branches do not process this ReviveRequest (the ephemeral predicate contains only activation request/point), but retained logs do not isolate which routing/timing predicate lost it. No comparable main revive run was found. |
| desync collector | Lines 450/454/458: `worker starts before retirement`; remaining retirement/absence checks passed | No changed collector/test/lease implementation was found in the range. This control gives collection only 10 ms, including spool work before Provider.Begin; the worker skips capture if that budget is exhausted (DesyncCollector.cpp). That explains a possible zero-Begin outcome, but per-job timing is not retained, so it is a hypothesis rather than a retrospective pass. No comparable main collector run was found. |

Revive and collector therefore remain visible unresolved suite failures pending
an operator baseline or explicit lead disposition. None is waived by the practice
pass, source equality, or these offline controls. No suite was rerun for this report.

## Static compile-risk check

The new pending helpers are defined inside EnemyOrdinaryLifecycle.inl before
ReceiveWorldPackets/Tick use them. ClearPendingOrdinaryCuts has an early matching
declaration for RetireWorldSession. The two new global mode/latch fields precede
HostFrame and the include. Existing vector, exception, Windows clock, codec and
scope headers supply the referenced types; no new CMake target or registration
was added. New native-control locals use distinct population/refill/removal/relay/
delayed prefixes; their loop and enclosing scopes were checked for the prior
C4456 shadow class. The same-actor metadata fixture avoids overlapping memcpy.
This is a source inspection, not a claim that MSVC /W4 /WX has compiled the tree.
