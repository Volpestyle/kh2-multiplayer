# VUH-2971 Mac protocol validation

Based on fetched main `2fd13501e335cff41083985984ce4d157e0f5633`.

Command:

```sh
clankie heavy -- python3 -B -m unittest tools/launcher/test_friend_package.py tools/packaging/test_product_protocol.py tools/launcher/test_steam_flow.py tools/launcher/test_steam_launcher.py
```

Result: 69 tests in 0.199s, OK (2 skipped). The skips are Windows launcher
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

Pending with Kit: one reviewed current-main PC1 build, all five pin entries,
portable CLI check, real ZIP build/self-check, bld install and Steam join to PC1
with verified roster/RTT and save/owned-process closure evidence. Exact receipt
fields and staging paths are in the [packaging checklist](../../../tools/packaging/README.md#pc1-receipt-needed-to-refresh-the-pin-vuh-2971).
The historical `friend-products.json` remains unchanged and correctly refuses.
No PC, SSH or game access was used in this Mac work.
