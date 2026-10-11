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
does not generate a death inside the supported experiment. A new load clears the suspension, not the old binding
into a new load. Production lifecycle acceptance remains blocked on a proper
native removal witness and independent qualification.

Authenticated incoming cuts retain their original transport scope for at most
30 seconds, at most 16 cumulative cuts. Native arrival/catalog availability may
delay admission; it does not silently discard the cut. Tick retries after native
arrival and tracking, with every original admission bookend unchanged. No duplicate
renews a deadline. Session, connection, delivery, host epoch or an already-arrived
native-load replacement retires the queued authority. Expiry/capacity refusal is
explicitly logged and supplies no permission to release claims.

Outside this mode (including default OFF and schema-1 population mode), host
absence/refill follows main `3c0f8e4`: a newer same-object/same-point spawn
supersedes the old host row immediately; population mode additionally publishes
its existing schema-1 terminal history. Other absent rows publish EnemyDeath
when last HP is zero or the existing 3000 ms grace expires. This is the retained
main compatibility policy, not a schema-2 non-kill membership certificate. No
client live-body relabel or release-predicate relaxation is added. Schema-1
repair retains its existing independent complete-certificate requirement.

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

The later operator baseline in
`.local/rig-operator/practice-review-fixes-results-01/README.md` resolves the
missing comparison: revive and desync_collector pass on exact main `3c0f8e4`
and on `b0b3fadd`; avatar fails on both (170 and 233 per mille respectively).
The older failures remain recorded above; they are not reproduced regressions
in this candidate. The b0b3 suite is still failed, because nativehit and world
failed their required assertions. No rerun was performed by story.

## b0b3fadd failed-control diagnosis

**Native bootstrap:** the control delivers RoomTransition and the schema-2 cut,
then native arrival, but never the host manifest. RoomTransition intentionally
clears `g_host.enemies`; TryReceiveOrdinaryLifecycle requires every cut row to
exist there with the same object ID. SeedPreparationShadows creates native
actors, not an authenticated host manifest. The failed positive therefore asks
native arrival to authorize missing remote membership. This is a fixture error,
not permission to weaken that predicate. The correction first asserts that
arrival alone still holds the original cut, then delivers the five-entry host
manifest through HostEnvelope/ReceiveWorldPackets and retries that same cut.
There is no second host membership event, direct cache write or re-anchor.

**Old-room HP:** the control sends periodic absolute HP exactly once on the
unreliable channel and requires that single packet to arrive. It received the
old manifest, failed this HP receipt, then successfully received current-room
HP later. This establishes missing receipt, not the hop or reason. Retained
output has no packet-specific trace that distinguishes loss from rejection;
a retrospective claim of a particular transport drop would be unsupported.

The HIGH-1 ordered-transition change cannot execute in kh2coop_world_test:
CMake links that target only to server_core and common, not the inject consumer.
All common/ and server/ inputs, CMakeLists.txt, WorldWireFixture.hpp and the
world test itself are byte-identical between `6472df95` and `b0b3fadd` (checked
as Git blobs). Thus no changed product branch in that range affects old-room
HP delivery. This does not claim that every existing transport/admission path
is bug-free; it refutes attribution of this observed failure to HIGH-1.

The correction primes both room caches with periodic absolute HP, still using
NetworkClient::sendEnemyHp's unreliable channel, within the original 3-second
wait (50 ms cadence, at most 60 sends, strictly increasing source sequences).
Success now requires the exact epoch, net IDs, HP/maxHP and a sequence from
that acquisition. The original positive receipt assertions and subsequent
stale/wrong-epoch/cache checks remain. A deterministic control drops the first
actual HP send using the existing outbound LinkConditioner and requires later
ordinary ENet publication to recover. No reliable promotion, wider timeout,
synthetic receive or product change is included. Operator native execution is
still required; source inspection alone is not a native pass.

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

The b0b3fadd parent compiled clean in the operator slot. The successor changes
only native bootstrap/HP priming test setup, these notes and source projections.
New locals use relay/periodic prefixes; lambda and enclosing scopes were checked
for /WX shadowing, existing headers supply the types, and no test target or
registration changed. The successor has not been compiled by story.

## Default-off absence/refill compatibility correction

Review2's b0b3 re-review identified that its global positive-HP hold could leave
an absent default-off manifest row unresolved for the entire epoch. The fix
restricts both the refill hold and the no-grace-death policy to
OrdinaryLifecycleRoom: exact experiment ON, BC 05/06/00/01/01/00, population OFF.
Every other configuration restores main's HostFrame refill branch and grace
predicate. The existing client binding, release, reason21 and native retirement
functions are unchanged. A supported experiment whose removal proof fails still
suspends membership once per load, keeps publishing current bodies, and cannot
turn a living absence into death even after the compatibility grace.

Actual native controls author five original host rows, unlink the living fourth
body and invalidate its status, then check both sides of the grace. The elapsed
interval is simulated by setting the fixture's prior goneSinceMs; no sleep or
native wall-clock claim is implied. The original host manifest/HP packets, fresh
survivor HP and eventual default-off death pass through HostEnvelope and the real
client consumer. Fresh HP rearms the original release guard: before terminal
authority it refuses the missing live member; default OFF subsequently releases
only the four exact surviving bodies. The experiment-ON counterpart remains held
with no death/native kill/relabel. Existing different-allocation refill cases now
assert main's supersession disposition, including the population-only terminal
message. No changes to the native bindings or exact release function are needed.

Source projections compare the restored refill statements and death-publication
body against main and preserve the experimental admission implementation exactly.
New absence-prefixed locals were checked for nested shadowing and declaration
order; no new headers or CMake registration are used. Native compilation and
execution belong to kh2-steam; these controls are not yet native passing evidence.

## 5170d399 native-control failure diagnosis

The operator run is `.local/rig-operator/practice-default-off-native-01/run/tests-01-stdout.txt`;
summary `.local/rig-operator/practice-default-off-results-01/README.md`.
It compiled clean but nativehit failed six assertions. These new-control failures
have two concrete fixture causes; no product correction is indicated by them.
The other required suites passed. The failed run remains failed.

Both main `3c0f8e4` and the candidate publish HP only when
`frame % HP_INTERVAL_FRAMES == 0`, with interval 6. The fixture called frame 40
for its bootstrap and 41 for its pre-grace observation, neither a publication
frame. SeedPreparationShadows also leaves every Spawn.spawnPos zero. After the
fourth body leaves, the fifth body has a higher spawn index, the same object and
the same point. Main's exact refill predicate therefore supersedes that fourth
row immediately; without population mode it intentionally sends no EnemyDeath.
Changing goneSinceMs later cannot turn that already-superseded row into a grace
death. The clock itself is not mismatched: both fixture and HostFrame use the
real GetTickCount64; the post-grace predicate check passed in both modes.

| Failed assertion | Mode/count | Exact cause and main expectation |
|---|---|---|
| Living leave before grace keeps survivor publication/no premature death | OFF and ON, two failures | Neither frame41 call can publish periodic HP. OFF additionally takes main's same-point refill branch, so its deathSent bookkeeping is already true. ON correctly does not fake a death, but HasHp is still false. |
| Only OFF grace emits the original terminal packet | OFF, one failure | The coincident-point fixture already superseded net4; main does not send a nonpopulation refill death. This was not an unrefilled grace test. ON's no-packet assertion passed. |
| Original five-member client readback releases | OFF and ON, two failures | Frame40 bootstrap contained a manifest (and experimental cut), but no HP. The unchanged release guard requires a nonzero HP source sequence and known positive HP for every living member; main's frame40 also supplies no HP. |
| OFF four-survivor release versus ON missing-membership hold | OFF, one failure | With no net4 death packet, that host member remains living in the client cache; no baseline HP was delivered for it either. Refusal is correct. ON's held/no-kill/no-relabel counterpart passed. |

The fixture correction retains every outcome assertion. It assigns five
well-separated initial points to both the owned native actor transforms and
historical Spawn rows, and checks complete census/pairwise point separation.
That makes the disappearance genuinely unrefilled. Calls at frames42/48/54
supply baseline, pre-grace and expired periodic HP; constexpr/static_assert ties
them to the actual interval. Frame43 remains the adjacent removal-proof step,
with an explicit assertion that it correctly emits no periodic HP. Baseline
publication now must contain actual HP for both a survivor and net4 before
feeding those original packets through the real client consumer. No direct host
cache authority, synthetic terminal packet or weakened release is introduced.
Existing separate refill controls continue to test main's intentional refill
behavior. Product bytes are unchanged from 5170d399.

The Python checks passed because their source projection correctly compared
production statements with main, while their fixture checks only checked the
presence of calls and assertions. They did not execute NativeHitClaimTest,
evaluate the chosen frames modulo the publication interval, or inspect the five
seeded spawn points. Those omissions allowed invalid positive fixtures to pass
source review. New offline fixture checks cover the actual six-frame schedule
and point-seeding prerequisites; compiled assertions and actual packet/census
checks independently enforce them in C++. Neither form of source check is
reported as native passing evidence. Native execution remains pending with the
operator; the original six failures are not retrospectively promoted.
