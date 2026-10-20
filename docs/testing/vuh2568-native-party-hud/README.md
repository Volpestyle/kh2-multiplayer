# VUH-2568 native party HUD evidence

**PASS:** `20261020-175813_net_native_party_hud_1`, product `80bc7f2` on
`c59045a`, protocol 29. Five of five live segments passed; every step passed.
The native friend gauges show owner-colored Sora portraits and authoritative
owner HP/MP. The local gauge remains native. The pre-load host view retains
Goofy; the same-room reload binds fresh human views without duplication.

## Scope and method

This is an explicitly approved **single-PC loopback** with two Sora profiles.
Host: real native test-hit damage 7 (80 -> 73 HP), real Fire (10 -> 0 MP),
then natural recharge. Guest: **RAM-controlled** HP 80 -> 69 and MP
10 -> 3 -> 0 -> 10. Exact owner and remote-view reads match in the report.
Both owners receive RAM setup of 80 max HP / 10 max MP and Fire availability;
Fire's original RAM byte is restored before closure. No in-game saves.

`native-party-hud-loopback-captioned.mp4` places the two renderer captures
side by side at 20 fps. It retains the first 14 seconds of the 20-second
stimulus clips, then visibly cuts to four seconds after recharge/refill.
Numbers come from owner/view memory checks; this is not a latency benchmark.
All 360 presentation frames and the selected stills were inspected: game
pixels and captions only, no desktop/launcher/private content.

The guest's disabled native Magic is separately tracked in
[VUH-2573](https://linear.app/vuhlp/issue/VUH-2573).
**Follow-up:** reciprocal native combat/MP video on two PCs remains unperformed
while PC2 agent play is unavailable ([VUH-2339](https://linear.app/vuhlp/issue/VUH-2339)).
Three-seat mapping, downed/revive, freshness, join/reconnect, NPC selection and
shutdown restoration are covered by unit controls, rather than this live run.

## Checks and safety

- Release build, **199/199** CTest targets passed after rebasing to `c59045a`.
- Windows AddressSanitizer: **45** native-HUD adapter checks passed. No UBSan run.
- Independent inject source review: **ADOPT**, including the portrait-fit delta.
- Closure: protected saves **4**, source files **801**, game-root files **14**
  and asset files **527** unchanged; sealed source/products unchanged; owned
  processes absent; relay port `27808` free.

## Retained artifacts

- `approved-live/`: report, raw clips/captures, inject/runtime/relay logs.
- `presentation/`: the captioned clip, frame-review sheets and edit provenance.
- `safety/`: complete before/after inventories and closure receipt.
- `sealed-inputs/`: pinned product copies, source revision and input hashes.
- `checks/`: build, full suite, ASan and independent source review.
- `re/`: native readers/binding/portrait/scale analysis.
- `failed-attempts/`: earlier failed runs and their closure evidence; they remain
  FAIL. The final approved scope does not claim guest native casting.
- `raw-pins.json`: SHA-256 seal of the retained raw files.

The lead published the bundle to the evidence store and closed
[VUH-2568](https://linear.app/vuhlp/issue/VUH-2568). The returned
[evidence manifest](evidence.json) records each retained object's size, SHA-256
and durable link. Key evidence:

- [Captioned loopback clip](clankie://evidence/sha256/cec8f983927d48ff20c052da3e76f7aa2e55e5af2f7e74c588d1ee433e297b7f)
- [Host HUD with distinct NPC](clankie://evidence/sha256/c9b888a820c681c0688413fc857bda959270051685e8d34a7ed1af84974e2094)
- [Guest HUD after reload](clankie://evidence/sha256/d2f30643e9d63ce8b38ab5ab1f061cf247ef176a343b10086a6ba87068973097)

Only this README and `evidence.json` belong in Git; raw media and logs remain
in the evidence store.
