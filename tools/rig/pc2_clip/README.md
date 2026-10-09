# Internal owned clip product

ADOPTed clip-product-04, exercised by passing PC2 host-warp acceptance-04.
Records one owned game window for three seconds at requested 30 fps, then fully
decodes and retains all 90 lossless frames. Measured timing, RGB metrics, raw CLI
receipt and process logs remain independently verifiable. Engineering black and
motion thresholds do not identify gameplay or prove arrival.

`kh2ctl_clip` is a separate explicit target, operationally restricted to `clip`.
The normal CLI, portable and diagnostic profiles retain their existing commands.
Native checks bind the package registry, PID, creation time and image through
retained handles. The adapter checks original target/module and product bindings,
cancellation and QPC deadline immediately before resuming a suspended helper.
Unknown identity or closure never establishes absence. Exited-process absence
uses creation time and native wait state without querying its image; live image
failures retain exact Win32 errors.

Build `kh2ctl_clip` and `kh2ctl_clip_test` explicitly. Configure
`KH2_CLIP_OFFLINE_CONTROLS=ON`, `KH2_CLIP_TEST_ENCODER` and
`KH2_CLIP_TEST_ENCODER_SHA256` with independently
verified ffmpeg, then run `ctest -C Release -R '^kh2clip_' -j 1` (eight tests).
Follow native-slot and canonical `ctest.lock` procedure before builds or tests.

Offline Python controls (Pillow required):

```powershell
python -m tools.rig.pc2_clip.controls --retained-native <encode_decode-evidence-directory> --output <receipt.json>
python -m tools.rig.pc2_clip.real_process_controls --output <receipt.json>
```

The first requires the retained `native-control.json`, logs, video and decoded
frames from the real generated-frame `encode_decode` control. Supply its directory
explicitly; the sealed-product default layout is not the tracked source layout.
The second queries itself and a normally exiting owned Python child, never KH2.
`lifecycle_controls.py` supports the synthetic suite.

`tools/packaging/add_rig_clip.py` creates a fresh ZIP from a verified friend
package plus explicit clip CLI, ffmpeg and module paths. It pins added bytes and
rejects replacing existing products. The verifier uses the same pinned ffmpeg
in a separately owned process. No binary or encoder installation is vendored.

The three JSON schemas are unchanged native contracts. Source owns safe native
and causal brackets, orchestration, artifact replay and final acceptance after
independent closure. Verified absolute product paths must be committed before
the sole warp; no full CLI fallback is allowed.

Retained validation: clip04 native build and 8/8 serial tests, 41 synthetic and
five real-process controls; acceptance04 PASS with two 90-frame clips, full source
replay, all six media identities absent and unchanged native inventory. Sealed
packets remain immutable evidence outside these tracked sources.
