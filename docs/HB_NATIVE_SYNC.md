# Host-led native warp and paired HB arrival

**PASS:** acceptance04 (`pc2-hb-host-warp-20261009-04`), independently closed
2026-10-09 18:32:15 UTC and posted to VUH-1496 with both arrival clips.
The 262-second scenario issued one host-owned native warp to HB00. The friend
followed through the product's ordinary follower path, without a fixture friend
warp or pause hold. Both reached the same fresh safe HB endpoint
`[4,10,50,0,0,22]`. Both owned in-renderer clips and PNGs qualified, with complete
causal/media replay, all owned processes and descendants absent, both locks
released, and the complete seven-entry native save/app-ID inventory unchanged.

This accepts the constructed-start host-warp/product-follower path. Natural
world-map travel, simultaneous arrival and a second destination remain
unqualified. Station Plaza post-input work remains parked. Acceptance04 was the
final authorized attempt and is spent; this landing grants no retry or successor.
Historical acceptance02/03 failures and every original packet seal are preserved.

## Accepted source and safety boundary

`tools/rig/hb_native_sync/` retains the accepted source9 native handler, causal
oracle, media orchestration, loaded-copy setup and service dependencies. The
scenario uses a qualified loaded COPY, pre-resume SaveGuard ACK and the same
19-byte in-memory setup with exact before/write/readback receipts. It never uses
save menus or savepoints and never imports a save into the canonical inventory.
Both initial and final safe START bookends include independent pause blockers.

The unchanged oracle verifies original raw prefixes, runtime/session/generation,
the authenticated single completed-baseline retirement, the new ordinary
queue/issue/load/arrival chain, shared progress, nonempty personal hashes and
fresh friend epoch/load/transition. Media must retain the original paired safe
HB scope and causal anchors throughout; same-room replacement, unsafe flags,
supersession or lost service refuses. No reanchoring or gameplay fallback exists.

All media clocks use `perf_counter()` / `perf_counter_ns()` (QPC). H is the
original handler start plus 180 seconds. Both native clip hooks retain
A = min(H, acquisition start + 45 seconds). Source acquires both clips and PNGs
before full source frame replay, then commits
V = min(H, validation start + 30 seconds) once. V includes full artifact/frame
replay, service join, rehashes, serialization and the final source step. In the
passing run acquisition took 34.850 seconds; validation through the final source
marker took 17.748 seconds, leaving 12.252 seconds of V. The handler took 153.900
seconds. These are observations, not future timing guarantees.

`media.deadlineMonotonicNs` remains the original A alias. Native receipt fields
are checked against A; source artifacts are validated under V. Mandatory v2
replay first orders handler/request/acquisition and bounds every retained media
QPC timestamp within H, then authenticates original A/V formulas and completion
before using a fresh read-only replay budget. Each clip retains all 90 lossless
decoded RGB frames, hashes, PTS, integer luma/motion checks, raw CLI/log receipts
and original owned process identities. Source `READY_FOR_CLOSURE` always has
`acceptance=false`; only independent combined closure may accept the run.

## Landing projection and offline verification

Source9 seal: `7be478f98d77c2066af832ce3db1ec1aa61892fe668da79c8e86a31b108f6aa1`.
Acceptance04 combined seal:
`ac75a4320f8d0434997b57a89e3af68a7917dba3cfecf7df3e3a191478439093`.

Twenty-eight source/contract files are copied byte-for-byte. Only `fixture.py`
and `observer.py` are projected: dormant map/selection/travel dispatch, the
unexercised observer and standalone execution CLI are removed. The exercised
install prefix and route clause, setup/ready/final-step and baseline functions
retain their source9 bytes. `source9-provenance.json` records whole-file and
retained-function hashes and source/landing line numbers;
`source9-landing.diff` shows every changed line. The verifier checks both against
the original source9 seal. No native binary, prepared save, evidence image/video,
unfinished route fixture or historical packet is included in this commit.

From the repository root, with the retained source9 packet and Pillow 12.0:

```powershell
python tools/rig/hb_native_sync/verify_source9.py --source-packet build/rig/story-tt-hb-native-sync-20261009-01/rev9
python tools/rig/hb_native_sync/acceptance_offline.py --packet build/rig/story-tt-hb-native-sync-20261009-01/rev9
```

The second command runs the real landing fixture/handler/oracle/media/setup
controls with native/process/network operations denied. It needs the packet's
retained history and source-copy assets; those stay ignored. No live action is
part of either command. The profile requires the separately reviewed PC2 adapter,
owned clip product, original runtime-session binding, complete inventory,
consent/window and independent closure. The landed fixture offers offline
validation only; it cannot launch a standalone PC1 run.

Landing verification on origin/main `4bf8009`: all 380 controls passed on both
system Python and the actual bundled PC2 Python; the current canonical runner
validated the projected install/profile offline. A directory-local
`.gitattributes` preserves exact source/contract bytes across Git checkouts.

## Retained evidence

- Report and complete source evidence:
  `.local/pc2/pc2-hbhostwarp04-evidence/scenarios/20261009-132657_pc2_tt_hb_host_warp_follow_acceptance_01_1/`.
- Independent acceptance/closure receipt:
  `.local/pc2/pc2-hbhostwarp04-closure.json`.
- VUH-1496 publication bundle: `.local/linear-evidence-hostwarp04/`, containing
  both original clips/PNGs and bounded captions. The packaging owner reviewed all
  180 frames for privacy before publication.
- Independent source9 review:
  `build/rig/story-tt-hb-native-sync-20261009-01/codex-review-rev9-review3.md`.

The source/story commit owns this fixture and contract. The separate PC2-owner
commit owns the exercised clip native target/tests, `tools/rig/pc2_clip`, clip
packaging helper, runtime-session integration and PC2 retargeting documentation.
