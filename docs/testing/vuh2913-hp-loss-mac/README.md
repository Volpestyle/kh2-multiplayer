# VUH-2913: EnemyHp loopback loss, Mac investigation

Rook, 2026-10-29. Compared main `18ce3f5d1fba10c02caa1b41353cecbfc83cd23e`
(protocol 40) with `c615c7dc27740c36f49cfd0a48be385fb3c8f9df` (protocol 39).
No product edits or PC access. The probe uses real ENet 1.3.17 and the real
NetworkClient/SessionHost/codec over IPv4 loopback, with synthetic resync-native facts
from the existing test. This is network evidence, not a native game run.

## Finding

The reproduced missing HP is an **ENet adaptive-throttle discard before UDP**.
The instrumented `enet_protocol_send_outgoing_commands` enters its actual discard
branch when `(counter + 7) % 32 > packetThrottle`. Packet destruction reports
`ENET_PACKET_FLAG_SENT` clear, and that HP never reaches the receiving transport.
Examples exist at both host→relay and relay→unfenced seat 2, on both source versions.

In `probe-v40/natural.log`, run 1 queues HP sequence 12 (800/1000) on channel 1,
flags 8 (`UNRELIABLE_FRAGMENT`), 107 bytes, MTU 1400. ENet discards it with
throttle 18/counter 19 on the host→relay hop. No relay-side rejection can count a
packet that never reached it. Other controlled rows catch relay→seat 2 discards.

ACK RTT changes lower the throttle: `peer.c:enet_peer_throttle` subtracts the
configured deceleration when the new RTT exceeds the retained low RTT plus twice
its variance. `protocol.c` calls this on reliable ACKs. The snapshot/control burst
can therefore lower a peer's throttle before the first post-convergence
unreliable HP. With the throttle held at 32 in the scratch control, all copies in
the matched timing experiment arrive. This control is not a proposed product change.

The one-entry inner EnemyHp frame is 27 bytes; observed wrapped frames are 107 bytes
on both versions, far below ENet's 1,372-byte fragmentation threshold for MTU 1400.
The only common/server source differences are ScriptCall shape/validation and
protocol-version changes in Protocol.hpp/Codec.cpp. EnemyHp, WorldEnvelope,
transport flags and channel selection are unchanged. Reliable EventHold uses
channel 0, HP channel 1: the proposed same-channel reliable-sequence explanation
does not apply to this pair.

## Results

All builds and test runs used `clankie heavy`. Counts are directly logged, not inferred
from elapsed time. These are timing probes, not statistically controlled loss rates.

| Experiment | 18ce3f5 | c615c7d |
|---|---:|---:|
| Unchanged full ForcedResyncTest, original polling |5/5 pass, 308 assertions each|5/5 pass, 308 assertions each|
| Target mode 0, observation hooks, original polling |9/10 pass; 1 throttle loss|10/10 pass|
| Same, wait for HP before EventHold |10/10 pass|10/10 pass|
| Target mode 0, add 4 ms per pump only while host has pending resync |4/10 pass; 6 throttle losses|8/10 pass; 2 throttle losses|
| Same 4 ms control, wait for HP before EventHold |6/10 pass; 4 throttle losses|5/10 pass; 5 throttle losses|
| Same 4 ms control, disable adaptive throttle in scratch only |10/10 pass; no losses|10/10 pass; no losses|

Additional constant 16 ms polling controls, with/without the delayed hold and with
throttle disabled, each pass 10/10 on both versions. Constant delay alone is not
the same as changing polling delay during the transaction.

The stock CMake resync target first failed Apple's `-Werror,-Wkeyword-macro` at the
existing test-only `#define private public`. The unchanged test source was then
compiled directly against CMake's real server/common/ENet libraries with strict
warnings and only `-Wno-keyword-macro` after `-Wpedantic`. All full-test counts above
are actual executions of that unchanged test, not an extracted assertion block.
No Windows-only target or product source was altered to make it compile.

## Test fix and limits

Keep HP as periodic unreliable absolute state. The test must tolerate a lost copy
and send a fresh sequence until the unfenced seat has an update, within a bounded
budget, while retaining all quarantined-seat exclusions. Kit's already-landed
`39562d2` implements that approach. The test comment now describes adaptive
throttling and separate channels instead of attributing the loss to the
subsequent reliable EventHold. The test behavior is unchanged.

This establishes the Mac loss mechanism and reproduces it before protocol 40;
it does not establish the exact hop of Kit's individual Windows losses, their
reported frequency, or why a particular rebuild shifts timing. His original
5/8 delayed-hold delivery result and inferred 1/2/3-send split are not recounted
as new measurements. No MTU/channel/reliability product change is supported by
this evidence. If Windows still fails after the bounded periodic resend, trace
its ENet discard/receive boundaries before making a product change.

## Evidence

`raw.tar.gz` contains the generation/build/run scripts, actual generated probe
sources, original full-test logs, both timing summaries, exact discard logs,
source/binary input SHA256 pins, and the ENet source used for the observed branch.
See `evidence.json` for its immutable evidence-store URI. The local scratch is
`.local/vuh2913-hp-loss/` in `wt-vuh2921-mac`.

The pinned probe executables are the final timing builds. The earlier natural
observations preceded adding the opt-in pending-resync polling-delay branch;
those earlier executables were overwritten. Their logs are retained; the full
original-test executables remain pinned separately. This does not change the
matched timing/discard/throttle-control evidence above.

## Comment correction checks

Before committing on main `2570fd3`, the corrected test passed **308/308** on the
Mac through `clankie heavy`, using the same strict portable compile described
above. All 49 project inputs to the reused networking libraries were checked
byte-identical. Non-comment C++ tokens match the parent exactly. The original
archive's SHA256 and all 67 input pins match. `landing-checks.tar.gz` retains the
check script, compile/test logs, checks receipt and corrected test source.
