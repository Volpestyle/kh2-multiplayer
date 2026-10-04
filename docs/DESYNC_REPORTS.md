# Automatic desync reports

The current protocol **9** candidate collects a report when the relay emits its existing
persistent-mismatch notice. It freezes the opaque session, original connection
IDs for every admitted slot, compared hashes, receipt sequences/times and
comparison sequence. The agreeing friend also participates. Receipt counts are
relay observations, not proof of fresh native frames or completed room arrival.

Every expected runtime copies its own bounded metadata, runtime output tail,
explicitly registered inject-log tail and one screenshot. It uploads actual
bytes over authenticated ENet connections; the relay never follows peer paths.
Diagnostic packets 30/31/32 use reliable channel 2; gameplay keeps channels 0/1.
Protocol 6 introduced these diagnostics; protocols 7 through 9 retain their layouts.
Protocol 7 added ordered enemy HP ([HP_ORDERING.md](HP_ORDERING.md)); protocol 8
introduced the separate forced-resync transaction ([FORCED_RESYNC.md](FORCED_RESYNC.md)).
All components need matching protocol 9. AvatarBridge 2 and CaptureChannel 1
remain unchanged; WorldBridge 11 carries captured world context and the separate
operator mailbox. Diagnostic collection itself does not mutate native world state.

The server defaults to `build/rig/desync`; `--desync-dir <path>` changes its
aggregate root. Runtime defaults to `build/rig/desync-local`; its
`--desync-dir <path>` changes the local spool. Pass `--inject-log <exact-path>`
from the launch result. INI keys are `desync_dir` and `inject_log`. Missing
registration produces an explicit unavailable artifact; there is no environment
dump, directory scan or guessed PID log. The scenario runner registers each
owned launch log and supplies separate aggregate/local directories within its run.

The relay writes `<root>/<session>/<report-id>/trigger.json`, `relay.log`,
`manifest.json` and numeric peer artifact filenames. It persists the initial
trigger before emitting requests, then publishes a final manifest. A killed
relay may leave a collecting manifest; scenario linkage presents it as partial.
Per-session `suppression-summary.json` separately records skipped triggers,
bounded witnesses, overflow and storage/backpressure facts. It does not certify
a report or change a finalized manifest. Filesystem failures are surfaced in
relay status/logs even if no artifact could be written.

The summary has one asynchronous writer and one coalesced pending snapshot; it
can lag the latest trigger. It retains the first 16 witnesses with overflow
counts. A dirty summary for an earlier session cannot be silently replaced:
cross-session triggers that cannot be retained increment the lost-trigger count
and preserve the last lost key/time. Arbitrary session churn is not loss-free.

Collection has a fixed 30-second relay deadline. Time spent persisting the
trigger reduces the request's remaining lifetime. Peer clocks are independent;
waiting for a matching roster or finishing local collection grants no extra
time. One report is active at a time, with a 60-second start cadence and eight
reports per session. Extra trigger witnesses are bounded to 16 with overflow
counts. Each peer has a 512 KiB aggregate log/metadata allowance and a 16 MiB
raw PNG allowance. Uploads emit at most four 16 KiB chunks per 16 ms without
catch-up bursts. Byte counts, contiguous offsets, descriptor ranges and exact
raw-byte SHA-256 are checked; conflicting or incomplete data remains partial.

Capture uses only the runtime's attached PID and existing CaptureChannel.
Automatic collection and CLI capture/clip share a per-PID named caller mutex.
Busy or abandoned ownership is explicit. The lease spans submission through
completion/timeout; an outstanding timed-out mailbox sequence prevents reuse.
The requester does not replace that request or force an overlay. Capture
requires the exact checked request/done sequence, success status, one frame,
dimensions and its generated output file. Attempt metadata retains those
witnesses, including renderer/format and failure facts. Missing attachment or
generation still permits logs/metadata, with screenshot unavailable.

Local work uses one owned collector thread. The default local deadline is five
seconds, bounded to eight and the original request lifetime, leaving time to
upload. Tick never waits for disk/capture. If OS I/O outlives that deadline, Tick
publishes timeout status and leaves the one worker busy until it returns. Late
raw files stay local and cannot upgrade the final contribution. Shutdown joins
owned work; a stalled OS operation can delay shutdown. Relay persistence and
coalesced suppression-summary writes are also bounded asynchronous work.

`complete` means every original participant supplied all required artifacts
with checked transfer integrity and bounded PNG structure/CRC. PNGs are not
decompressed by the relay. Truncated tails are identified by ranges and source
sizes; they are not whole histories. Screenshots and metadata are not an atomic
cross-machine snapshot. Transport integrity and a claimed capture witness do
not establish native provenance, convergence or multi-PC acceptance.

Offline controls use the production collector, uploader, relay and actual ENet
senders with owned temporary logs, synthetic mailboxes/providers and real PNG
bytes. `kh2coop_desync_test` checks assembly, framing, identity and lifecycle;
Windows `kh2coop_desync_collector_test` checks capture/lease handling. The actual
runtime smoke uses `--pid 0`, which returns before process enumeration or
OpenProcess, and verifies its transferred logs/metadata plus unavailable PNG.
Prepared scenario linkage controls invoke no CLI. The frozen protocol 6
build/test evidence is recorded in `build/rig/desync_offline_receipt_20261003.json`.

In that frozen protocol 6 result on 2026-10-03, Release and Windows ASan each passed 1,353 controls:
596 desync, 119 capture/lease, 202 world, 127 recovery, 146 avatar, 85 simulation,
61 bridge and 17 activation. Portable ASan+UBSan passed 1,027 controls (596 desync,
202 world, 127 recovery, 85 simulation and 17 activation); Windows mailbox and
avatar/bridge controls are excluded there. All three actual runtime smoke checks
passed, as did nine prepared scenario linkage cases. No sanitizer finding was
observed. Builds included current runtime/relay, tools and both Windows DLLs;
no DLL was installed or exercised against KH2. Source reviews and retained
initial compilation/test failures are part of the receipt evidence.

The frozen protocol 7 HP ordering receipt is
`build/rig/hp_order_offline_receipt_20261003.json`. It re-executed all 596 desync
controls in Release, Windows ASan and portable ASan+UBSan, plus 119 Windows
capture/lease controls per Windows configuration, nine scenario linkage cases
and all three actual unattached runtime reporting smoke checks. No sanitizer
finding was observed; native/remote limits are unchanged.

## Local native renderer evidence, 2026-10-03

The automatic report in
`build/scenarios/20261003-135207_net_forced_resync_native_shadows_1/`
retained all three original local peers, connections 1/2/3. Each supplied a
1920x1080 native renderer PNG with nonzero attached PID, stable generation and
matching request/done sequence. Saved local and relay copies matched all twelve
artifact byte counts and SHA-256 digests. Independent PNG framing, CRC and
bounded decompression checks passed; visual inspection shows the courtyard and
Shadows on all three captures. These captures are sampled observations, not an
atomic gameplay comparison or proof of health/population agreement.

The final aggregate correctly remained `partial`: slot 1's inject log changed
during capture and retains `Interrupted` status. The intact transferred bytes
do not establish a stable source tail. No participant was removed to complete
the denominator. See `build/rig/resync_shadows_auto_report_review_20261003.md`.
The enclosing scenario remains failed at positive point stability before any
forced-resync command; automatic diagnostics do not imply automatic recovery.

The later passing population run `20261003-140717` supplied a separate
**complete report 1** from its original connections 1/2/3. All twelve bounded
metadata/log/PNG artifacts had complete source status, matching local/relay
bytes and digests, and successful native capture witnesses. All three 1920x1080
PNGs passed independent integrity/decompression checks. The run-level index
correctly remains `partial` because one active and two cadence-limited triggers
were suppressed; those witnesses remain visible. A complete original request
does not erase skipped later requests or prove whole-run log history.
See `build/rig/resync_population_captures_review_20261003.md`.

That run also saved three final explicit PNGs after native resync checks.
Their stored step results contain only path and PASS, so final request/done
sequence and renderer receipts are unavailable. Earlier automatic capture
witnesses cannot supply those missing final-capture facts.

The progress-fault retry `20261003-152248` provides a further **complete report
1** with the same original three local connections 1/2/3. All twelve bounded
artifact descriptors, local copies, hashes and native capture witnesses match.
Each automatic screenshot has matching request/done sequence 35 and stable
observed generation 2. The enclosing index remains `partial` for five
cadence-suppressed triggers; report completeness does not erase those gaps.

Its three separate final explicit captures retain the new CLI completion
receipts: exact native PID/path, request/done 36, status 0, one frame, renderer
12, format 87 and 1920x1080. All six distinct final/automatic PNGs pass independent
CRC, exact-end and bounded decompression checks. These sequential captures
carry no native frame or world-generation attestation; the earlier automatic
metadata cannot fill those fields. See
`build/rig/resync_progress_native_captures_review_20261003.md`.

This establishes three local renderer contributions and one complete bounded
automatic transaction on one loopback rig. Physical remote delivery and full
native recovery remain open. This feature does not resync gameplay or remove
the fresh-actor anti-refill guard.
