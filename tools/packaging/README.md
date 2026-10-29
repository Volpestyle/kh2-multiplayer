# Private Windows friend ZIP

`build_friend.py` copies explicitly selected local products and the installed
CPython 3.11 standard library/Tk. It never downloads, installs, launches KH2,
opens a listener or writes the game folder. The supported wire protocol is read
from `common/include/kh2coop/Protocol.hpp` at package build. That same header is
copied into the ZIP and covered by `package.json`'s file hashes; the receipt's
`protocol` and every product's protocol must match it at startup. There is no
separate launcher protocol number to update. The receipt is an integrity
inventory, not binary protocol introspection or a signature.

`friend-products.json` still pins the historical protocol-18 / AvatarBridge-4
cohort from `bb7122a`. It deliberately remains unchanged pending Kit's one
reviewed PC1 build of current main (`2fd13501e335cff41083985984ce4d157e0f5633`
or a descendant). The builder refuses that historical selection against current
source. Do not relabel old binaries. All five products must share a full source
revision and one build ID; stale/mixed protocols, missing roles, mixed cohorts
and changed bytes remain refusals.

## PC1 receipt needed to refresh the pin (VUH-2971)

Kit owns the native build, package build and bld install/join. From ONE reviewed
PC1 CMake build, retain the full source commit, build ID, protocol read from that
checkout's `Protocol.hpp`, AvatarBridge version, build configuration/portable
flags and review approval. Supply these exact pin entries (paths below are
repository-relative staging paths; replace `<build-id>` with that receipt's ID):

| JSON role | Path | Required hash |
| --- | --- | --- |
| `products.dll` | `build/rig/<build-id>/products/kh2coop_inject.dll` | SHA-256 of DLL bytes |
| `products.runtime` | `build/rig/<build-id>/products/kh2coop_runtime_scaffold.exe` | SHA-256 of runtime bytes |
| `products.server` | `build/rig/<build-id>/products/kh2coop_server.exe` | SHA-256 of server bytes |
| `products.avatarctl` | `build/rig/<build-id>/products/avatarctl.exe` | SHA-256 of avatarctl bytes |
| `cli` | `build/rig/<build-id>/products/kh2ctl.exe` | SHA-256 of portable CLI bytes |

Every entry needs `path`, a lowercase 64-digit `sha256`, integer `protocol`,
full lowercase 40-digit `sourceRevision`, and the identical `buildId`. Top-level
`protocol` must match the source and all entries; `avatarBridgeVersion` must
match the source; `transport` must be `steam`. Actual hashes and build ID are
pending Kit's receipt, not placeholders to commit. Keep absolute PC paths and
build logs in local evidence. Refresh all five entries together after review.
Pass the same staged CLI path and its hash as `--cli` and `--cli-sha256`.

This cohort builds all five native products with the static MSVC CRT. Earlier
release01/release02 products remain historical evidence, not current inputs.
Only the direct/transitive CRT import closure from the shipped products is copied
to `bin`; `crt-imports.json` retains each dumpbin output and the kept/omitted set
outside the ZIP. Python's own runtime DLLs remain in its separate directory.

For a future refreshed cohort, build the matched native products in one CMake directory with
`-DKH2COOP_PORTABLE_PACKAGE=ON`. This changes only the CLI package-root guard,
absolute rig root, portable command allowlist and disabled rebuild/restart path.
Normal developer builds default OFF and retain existing behavior. Use
`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` for this static-CRT cohort. Use installed
ENet/MinHook source via `FETCHCONTENT_SOURCE_DIR_ENET` and
`FETCHCONTENT_SOURCE_DIR_MINHOOK`, with `FETCHCONTENT_FULLY_DISCONNECTED=ON`.
For MSVC, retain its default CXX flags and append
`/experimental:deterministic /pathmap:<absolute-repo>=/source`; pass linker
`/PDBALTPATH:portable.pdb`. Do not package PDBs. Build targets `kh2coop_inject`,
`kh2coop_runtime_scaffold`, `kh2coop_server`, `avatarctl` and `kh2ctl` from the
same checkout, then refresh every per-role receipt path, hash and cohort field
with that build receipt. The commands below are the packaging procedure
once those prerequisites are satisfied; the checked-in v18 selection is
currently rejected. Run checks through `clankie heavy --` on fleet machines.

```powershell
clankie heavy -- python -B -m unittest tools/launcher/test_friend_package.py tools/packaging/test_product_protocol.py tools/launcher/test_steam_flow.py tools/launcher/test_steam_launcher.py
python -B tools/packaging/check_portable.py --cli <private-kh2ctl.exe> --output <new-checks.json>
python -B tools/packaging/build_friend.py `
  --output <new-parent>/KH2-Co-op --evidence <new-local-evidence-dir> `
  --python-home <installed-CPython311-directory> `
  --crt <installed-x64-Microsoft.VC145.CRT-directory> `
  --dumpbin <installed-MSVC-x64-dumpbin.exe> `
  --vs-licenses <installed-VS-Licenses/1033-directory> `
  --transport steam --products tools/packaging/friend-products.json `
  --cli <private-portable-kh2ctl.exe> --cli-sha256 <its-reviewed-SHA256>
<new-parent>/KH2-Co-op/python/python.exe -I -B <new-parent>/KH2-Co-op/tools/launcher/friend.py --self-check
```

The builder refuses existing output/evidence directories. Local source paths
stay in evidence; the ZIP carries only portable file/role hashes, compatibility
labels and the exact game EXE allowlist. `package.json` is an integrity inventory,
not a digital signature. Distribute only the approved complete ZIP through the
lead's chosen channel. Do not mix binaries or substitute a manifest from another
package.

It excludes site-packages, pip/ensurepip, caches/bytecode, tests, debug symbols
and developer logs. CPython's complete bundled-component notice (including its
Microsoft redistribution conditions), Tcl/Tk terms, installed Microsoft redist
notice and third-party notices, ENet and MinHook licenses are retained. No game
assets, game DLLs, Steam files, original saves or local result receipts enter the
ZIP. Every decompressed entry and PE byte stream is scanned for case-insensitive
local user/repository identifiers in ASCII/UTF-8 and both UTF-16 byte orders.

The friend launcher applies the qualified gameplay defaults to both game and
runtime children through the existing `child_environment`: mirror, population,
TARGET_ALL, Normal difficulty, friendly fire, the two event-hold flags, downed
handling and the revive prompt. Explicit inherited
`0`/`off` disables still win. Parked spawn/legacy targeting and developer test
flags remain excluded. The shipped set and scope are in
[`ONLINE_COOP_PLAN.md`](../../docs/ONLINE_COOP_PLAN.md). Steam app-ID environment
overrides are retained; the Steam broker uses its existing guarded app-ID-file
handling. ENet launch does not create an app-ID file. Exact
EXE SHA is checked before canonical launch/injection. SaveGuard must be logged
at this package's absolute log path before connection. A known failed launch
receipt with a PID is closed through canonical retained ownership; a timeout
with no trustworthy PID is never guessed or killed. Helpers retain the existing
creation-time Job Object boundary; the game remains outside that helper job.

Offline tests do not prove clean-machine dependencies, visible UI, startup
without an app-ID file, live guard installation, connect/disconnect, name/HP
rendering, or save preservation. Kit must build from the refreshed pin, retain `provenance.json`,
`package-result.json` and `crt-imports.json`, then run the extracted package's
`--self-check` and normal `friend.py` flow on bld in a fresh folder. Confirm the
intended Steam accounts, SaveGuard and exact EXE, join the PC1 host, capture a
verified roster and RTT, disconnect/close owned processes and verify unchanged
saves on both PCs. Attach the package receipt, ZIP hash and join/closure evidence
to VUH-2971. These checks remain pending; no fight is required for this smoke
join. Never test by attempting an in-game save.

## Historical installed-package evidence (protocol 18)

The v18 installed-package rehearsal used the extracted ZIP's GameOwner and
Session APIs, without feature environment overrides, on a two-player ENet
loopback. BC HP scaled 39 to 63 on both peers, the friend's real Sora took an
enemy hit, deaths agreed and the room transition produced no desync notice.
Four protected saves were unchanged. This validates only that historical installed native cohort;
it does not establish a clean-machine install or two-PC Steam acceptance.

The refreshed `7331a05` cohort also passes installed-package TT rehearsal
`20261013-123603`: launcher defaults on, Driller/Soldier HP21->34 /29->47 on
both peers, real friend HP24->18, deaths4/4, no relay/runtime desync notice,
and four protected saves unchanged. All native launches use the extracted ZIP;
the developer CLI is only a PID/birth-bound rehearsal input/capture aid.
