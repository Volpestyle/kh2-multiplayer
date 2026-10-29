# VUH-2914 portable lifecycle result

Mac part (a) fixes two failures in the existing ScriptMirror adapter. It adds no
native calls, reads, offsets or Axel phase policy.

- Duplicate effect starts that pass packet admission now preserve the current
  binding and make no second native birth, including held replay.
- Room retirement snapshots attack identities before native free, then checks
  the current resource/handle/allocation/owner binding before each free. Native
  destruction may erase bindings without invalidating traversal.

The baseline on unchanged product `89242559` failed three duplicate-effect
assertions and aborted with AddressSanitizer heap-use-after-free in
`ClientEffects::Each` after `SmForgetAllocation` erased the current iterator.
[Baseline log](clankie://evidence/sha256/2ac68d279ad363a720c2c181cba8a1473e99ce5e946dcc1f59c4ae117aee2f8b).

The fixture compiles verbatim production bodies for `SmApplyRecord`,
`SmDrainHold`, `SmReplay`, `SmResetRoom`, `SmRetireClientAttacks`,
`SmForgetAllocation`, native identity checks, `SmDrivenActor`,
`EnemySync::AdmitScriptCall`, and the `NetworkClient` ScriptCall admission block.
Native calls, memory and the bridge/session context are stand-ins; unrelated
owner/reaction handlers are stubs. CMake regenerates the extracted bodies when
source changes. A separate real ENet relay/NetworkClient loopback control in
`ScriptMirrorTest.cpp` confirms that repeated resource IDs in increasing packet
sequences reach the client. These are adapter tests, not a live-game substitute.

Existing admission/lifecycle controls pass: old packet sequence, epoch, host and
delivery rejection; held attack cancellation; ordered held effect start/end;
old attack end against same-handle/address replacement; unread identity refusal;
final attack end after source loss; namespace separation; and reset disposal.
Old packet start/end candidates are already refused upstream; no tombstone,
phase state, timer or new native behavior was added for them. Repeated IDs in
newer packets establish adapter admission, not evidence that Axel's host script
emits duplicate starts in live play.

Checks (through `clankie heavy --`, serial ctest):

```sh
cmake -S . -B .local/build-vuh2914 -DCMAKE_BUILD_TYPE=Debug
cmake --build .local/build-vuh2914 --target kh2coop_script_mirror_test kh2coop_script_lifecycle_test --parallel 2
ctest --test-dir .local/build-vuh2914 -R '^kh2coop_script_(mirror|lifecycle)_test$' -V -j 1
```

See [VUH-2914](https://linear.app/vuhlp/issue/VUH-2914) for the fixed-run
receipts and exact-head independent review.

No PC access, inject DLL build, installed-byte qualification or live Axel run.
Part (b), phase/hazard parity and issue acceptance remain open for the lead's
scheduled PC work.
