# VUH-2971 Mac protocol validation

Based on fetched main `2fd13501e335cff41083985984ce4d157e0f5633`.

Command:

```sh
clankie heavy -- python3 -B -m unittest tools/launcher/test_friend_package.py tools/packaging/test_product_protocol.py tools/launcher/test_steam_flow.py tools/launcher/test_steam_launcher.py
```

Round 1 result: 69 tests in 0.199s, OK (2 skipped). The skips are Windows launcher
widgets and Windows file-sharing receipts. An earlier run failed in the new
hash-mismatch fixture because product iteration order is unspecified; the
fixture now supplies every product path, and the complete run above passes.

Coverage includes source protocol acceptance, stale coherent receipts, each
mixed/missing role, mixed source/build cohorts, receipt/file hash disagreement,
changed input bytes, missing header inventory, and a complete offline package
build with dummy products. The build test verifies the generated package receipt
and hashed canonical header with the normal package validator; CRT dependency
inspection is stubbed. No game data or native Windows products are test inputs.

`Protocol.hpp` is copied into the package's integrity inventory, and its source
value is carried as `package.json.protocol`. Startup compares every product
receipt to the bundled header; the developer plan reads the checkout header.
The header and manifest are integrity metadata, not binary introspection or a
signature. No second protocol number is maintained in launcher code.

Round 2: 70 tests in 0.336s, OK (2 Windows-only skips), using the same governed
command above. This adds a fresh-interpreter test of the real packaged
`friend.py` entrypoint with missing and inventoried-but-corrupt headers. A fake
Tk error dialog records the same startup error title/message; the process exits
1 before Windows or game operations. Protocol metadata reads now occur during
validation or plan construction, rather than at import.

The pin now records Kit's actual five-product receipt for
`vuh2971-protocol40-20261029-01`, source
`2fd13501e335cff41083985984ce4d157e0f5633`, protocol 40 / AvatarBridge 4.
All five full SHA-256 values and repository-relative staging paths are in
[`friend-products.json`](../../../tools/packaging/friend-products.json).
Receipt source: Kit's direct fleet message `9241bad0-d3fb-4163-af08-e04e43d30b9c`
and his [build report on VUH-2971](https://linear.app/vuhlp/issue/VUH-2971).
Kit reports one portable static-CRT CMake build directory, a test-only MSVC fix
at `bd48a35`, five byte-identical products across that rebuild, and 304/304 serial
ctest results. The full local PC1 `build-receipt.json` stays beside the staged
products; no native bytes or absolute PC paths were fetched or committed here.

Pending with Kit: portable CLI check, real ZIP build/self-check, bld install and
Steam join to PC1 with verified roster/RTT and save/owned-process closure evidence.
The [packaging checklist](../../../tools/packaging/README.md#pc1-receipt-needed-to-refresh-the-pin-vuh-2971)
records the inputs and remaining steps. No PC, SSH or game access was used in
this Mac work. Mac tests validate receipt coherence and offline package wiring;
they do not independently verify Kit's native product bytes.
