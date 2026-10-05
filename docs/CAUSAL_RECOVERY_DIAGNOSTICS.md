# Recovery diagnostics (VUH-1508)

Opt-in receipts connect the actual native StateHash publication, the relay's
comparison, the host's automatic request, and ordinary cached-world admission.
They help qualify a recovery; they do not establish a passing live cycle alone.

Set `KH2COOP_CAUSAL_DIAGNOSTICS=1` in the relay, runtime and injected game's
launch environment. Only exact `1` installs the checked-flush sinks. This flag
is independent of `KH2COOP_AUTOMATIC_RECOVERY`. Disabled or missing streams
mean unavailable evidence and cannot prove zero requests. Enabled diagnostics
allocate, hash, format and flush on owner threads; their cost is unmeasured.

## Recorded seams

| Family | Producer | Recorded facts |
| --- | --- | --- |
| `native-hash-publication` | DLL | Same census and single captured enqueue context as the published H, including lifecycle, full location, connection and source delivery. Only a complete whole-combat-census zero proves selected-empty. |
| `relay-hash` | Relay | Actual admitted scope and receive sequence; both operands of each real comparison; both comparisons responsible for the emitted notice. |
| `resync-request` | Runtime | Each generator/direct submission or caller rejection, origin/outcome, exact allocated key/mask/roster, original owner-local start/deadline when available. |
| `relay-cache-delivery` | Relay | Actual cached body and envelope SHA256, send scope, target/delivery and original send return/disposition. |
| `world-envelope-admission` | Runtime | Original received outer-envelope and accepted inner-body lengths/SHA256, after existing scope/inner admission and before the original raw callback. |

New records use `schema=1`. Automatic seals add request high-water, flush,
drop and availability facts. Escaping request/cache construction exceptions
preserve their original propagation and mark a sticky gap; no key or body is
fabricated. No protocol, authority, native call, network flush, retry, rate or
transaction deadline changes.

Native `bridgeEnqueued=1` proves DLL-to-runtime enqueue only; `relayReceived`
remains unproven until joined to the relay. Use the actual latest compared H,
not an earlier empty hash. Client `hostSource` remains zero: identical H values
are not unique publication IDs. All matching candidates must provide compatible
proof; conflicts stay unproven. A dead-only census with an empty living hash
does not prove an empty combat census.

Cache `enet-submitted` proves submission, not delivery or native adoption.
Suppressed/buffered successful return values prove neither. Receiver hashes
use actual retained bytes, including inbound-conditioned bytes, never typed
re-encoding. `callbackDelivered=unproven nativeConsumed=unproven` retain that
seam's limits. Load, progress, claim hold, replay and final convergence require
their independent lifecycle/causal joins.

## Coverage and closure

Owner streams begin at birth. Events use consecutive `seq`; seals use separate
consecutive `sealSeq`, with `highWater`, `flushedHighWater`, `dropped` and
`unavailable`. Event capacity is65,536 without overwrite/wrap. Overflow, failed
sinks, exceptions and nonthrowing ostream bad/fail state make loss sticky.
Later successful flushes do not repair loss. No sink leaves new counters and
diagnostic allocations at zero.

Runtime request/admission and relay cache/hash streams seal about once a second,
including quiet/pre-attach periods. Native seals follow publication and shutdown.
Complete evidence needs owned producer identity, full raw prefixes, consecutive
event/seal sequences, no omitted/duplicated/truncated rows, zero loss and equal
event/flush high water. Flush success means stream flush, not crash durability.

Collect final native censuses, then actual producer watermarks, then strictly
newer complete live intervals under the original remaining deadline. Retain real
read ordinals; cross-process clocks cannot prove ordering. Terminal seals or
dead producers cannot substitute. Final automatic session/host/roster/generation/
delivery must equal the original binding, with no pending/requested/planned work.
The prospective collector and consumer must establish these facts.

Join the same recovering slot/connection across notice, original mask/roster,
native publication, bound runtime and ordinary cache/admission. Keep host-source
delivery distinct from receiver delivery. The original request's30s deadline
stays fixed.

## Verification and open gates

The composed source passed full private and shared Release builds. Twelve
affected executables passed privately; eight relevant executables, including
all six new boundary targets, passed again after shared integration.
Independent normal/MSVC AddressSanitizer checks cover10 stream
boundaries and31 actual receiver assertions, including retained wire bytes
distinguishable from re-encoding. NetworkClient/Protocol/Codec were instrumented;
reused ENet was not. UBSan was unavailable in the installed compiler set and
was not run. See [integration evidence](../build/rig/causal-root-integration-20261004-01/)
and [independent review](../build/rig/causal-root-independent-review-20261004-01/review.md).

The original private evidence consumer remains BLOCKED on wrong-target,
terminal-tail and changed-final-binding acceptance. A distinct corrected copy
rejects all reproduced cases. Neither it nor the collector has live acceptance.
First automatic qualification and the approved10 original-route plus10 outside
resynced cycles remain open. Natural outside fails without skip/retry; v1 and
historical FAILs stay unchanged.
