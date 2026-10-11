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


## 0111f2e6 native-control failure diagnosis: cached release

Evidence: `.local/rig-operator/practice-default-off-fixture-native-01/run/tests-01-stdout.txt`.
The three remaining failures are wrong fixture expectations about the release
cache, not a new default-off or experiment release regression. This conclusion
is from the exact C++ sequence and comparison with main `3c0f8e4`; the corrected
native assertions have not yet been executed.

`EnsureClientClaimScope` is identical to main. A previously released hold is
rearmed when the manifest **revision** changes, not merely when HP packet
sequence advances. The HP consumer increments that revision only if a row's
HP becomes known or its HP/maxHP changes. Every survivor remains 1000/1000 in
this fixture. `TrackSpawns` does not rearm the hold. `ReleaseClientClaims` then
executes `if (!h.held) return true` before capturing a census. Main does exactly
the same under these inputs. Its full gate differs only by the experiment's
removed-row exclusions, which are inactive here: all removed flags are false.

Frame-by-frame replay (each mode starts from a fresh `prepare`):

| Phase | Host membership / native census | Revision and release outcome |
|---|---|---|
| Host 42 | Five distinct live members; manifest and five HP rows published | Bootstrap packets retained |
| Host 43 | Fourth body unlinked and status invalidated; four survivors | No periodic HP; no terminal |
| Host 48 | Four survivors at 1000/1000, missing original still live in authority | Survivor HP packet, no terminal |
| Host 54 | OFF emits net4 death after grace; ON stays suspended with no death | Expiry packets retained separately |
| Client bootstrap 42 | Five host live rows, dead=false/removed=false, five bound native bodies, known HP1000/max1000 | Full readback releases and caches revision R |
| Client leave/48 | Host still five live rows, native census four; local net4 present=false | TrackSpawns leaves revision R and released hold unchanged |
| Client survivor HP/48 | Same five host live rows; four native bodies; survivor HP values unchanged | HP sequence advances but revision remains R. Main and candidate return cached true without new census readback |
| Explicit test-only rearm/48 | Same five-live/four-native inputs | Fresh full readback refuses: no present net4 binding and cardinality mismatch |
| Client expiry/54 OFF | Net4 dead=true, removed=false; four matching survivors | Death advances revision; exact four-survivor readback releases |
| Client expiry/54 ON | Net4 dead=false/removed=false; four matching survivors | Test hold stays armed; unchanged HP cannot satisfy missing member, so refusal persists |

Thus the two pre-terminal failures were cached true, not successful five-vs-four
full readbacks. The experiment's final failure was the same cache carrying
through unchanged survivor HP. OFF's final assertion already passed because
the real EnemyDeath advances the revision. The prior comment claiming a fresh
survivor HP packet necessarily rearms the gate was incorrect.

The corrected fixture explicitly checks and prints all host rows, dead/removed/
HP-known flags, native cardinality, local net4 presence, revision, released
revision and HP sequence. It first asserts the inherited cached result, then
calls `InvalidateClientClaims("test-absence-fresh-readback")` as an explicit
**test-only** reset before the original missing-member refusal assertions.
This does not claim a production rearm occurs at native leave. No product
source, gate, packet, grace policy, or assertion about the held gate is relaxed.

The inherited cache behavior means a true return alone is not a fresh census
certificate. Any requirement to revoke a released hold immediately on native
membership loss would be a separate main behavior change, not a fixture fix or
proof that this range weakened main. It is recorded here rather than hidden.

Why Python missed this: the source projection verified the unchanged gate and
the fixture's text, but did not evaluate the cache branch or the HP consumer's
conditional revision update. New checks compare the entire applicable gate
and scope function to main, prove unchanged-HP revision behavior, and enforce
that the native fixture exposes the cache before explicitly testing held
readback. The native compiler/test remains the execution authority.


## Native membership loss now revokes a cached claim release

Lead51 authorizes fixing the inherited release cache as a separate product
delta on `fix/claim-release-native-loss`, based on landing `d9412a21`.
Previously, unchanged HP did not rearm a released revision when a native body
left. The preceding historical diagnoses remain accurate for those commits.

`EnsureClientClaimScope` now checks cached authority against a fresh complete
native census in the original admitted scope. Every live host ID must have
exactly one present, non-killed binding with matching actor/objentry/status
roots, object and battle program. Native cardinality and reverse coverage must
also be exact. An unavailable census or changed scope refuses as well. Loss,
extra bodies, or a root replacement re-holds the release without changing its
scope or granting any new authority. This runs at both existing claim admission
and final publication gates, not only after `TrackSpawns`.

Only the original full `ReleaseClientClaims` HP/identity/cardinality/bookend gate
can re-earn release; its body is unchanged. The new helper is read-only and
can only revoke a cache hit. It adds no message, native call, kill, relabel, HP
writer, terminal or membership admission. Default-off is **strictly safer**
than main at this boundary, not behaviorally identical: it now holds missing
or extra native membership even with an unchanged host revision. Main's
supersession/3000ms grace/EnemyDeath policy and population mode remain intact.

The e5 absence fixture now has no explicit test rearm. In both OFF/ON modes it
checks stable cache preservation, same-cardinality foreign status rejection,
extra-body rejection, native unlink rejection before stale tracker flags are
updated, and re-earning release after exact native restoration. The original
five-live/four-native state with unchanged survivor HP must re-hold through the
production claim scope gate and refuse full release. OFF then receives its
original grace death and re-releases exactly four survivors; ON stays held
without a kill or relabel. Existing negative controls and source comparisons
for reason21, native retirement, HP/death admission and the full release body
remain unchanged.

This adds complete read-only census work on cache hits at the existing scope
gates. The encounter sizes are bounded by the existing census limits; no
new native hook or asynchronous observer is introduced. Native execution and
the delta review remain pending kh2-steam/review2, not inferred from Python.
