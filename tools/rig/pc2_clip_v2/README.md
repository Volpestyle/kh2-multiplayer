# Internal clip v2

Separate opt-in kh2ctl_clip_v2 and Python adapter; clip04/v1 stays unchanged. Exactly default 3s/90 frames or explicit 16s/480 frames at requested 30fps. No minimum wallclock coverage is promised by frame count.

The 3s path retains inline encoding/full decode under its original 45s caller budget. The 16s path captures RAW ONLY, with native acquisition/helper capped by min(original deadline, start+23s). The release journal is emitted after the actual CaptureLease destructor. The adapter retains 480 BMP hashes and positive original helper closure; no encoder starts during capture.

After gameplay and game/runtime/capture-helper closure, postprocess accepts original identities, a complete independent closure report (protected files, descendants, ports) and a verifier. It starts a separate owned encoder/verifier job under an explicit absolute deadline at most 600s away. This does not renew gameplay, the encounter90s or JOIN600. Unknown closure refuses. Capture admission must preserve the original JOIN cleanup reserve.

Keep one product instance across two local windows. Second capture requires the original canonical receipt, post-destructor release journal, unchanged target/raw hashes and helper absence. Interrupted/unknown prior capture or a third window refuses. Native Acquire still refuses busy. STOP uses the original Event and existing cleanup deadline; exact identity journals/private jobs apply to helper/encoder/verifier. No process-name cleanup, copied registry or checkout CLI fallback.

Strict schemas cross-check seconds/count/products/targets. Long capture is not video success: only post-run full480 decode/metrics/hash can produce that receipt. Missing/uncovered media remains distinct from causal classification; ownership uncertainty is hard STOP.

Measured local1080p480 high-entropy finalization took326.676s. Two sequential finalizations are not assumed to fit600s. Capacity admission counts retained local views and a separate C: floor; no evidence deletion or live readiness is implied.

Targets kh2ctl_clip_v2 and kh2ctl_clip_v2_test have13 authored serial controls, UNBUILT pending authorized slot. Python controls use retained short evidence/generated frames and mocked native leaves, not PC2/native coverage proof. New product requires review2 before landing/use.
