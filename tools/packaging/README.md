# Private Windows friend ZIP

`build_friend.py` copies explicitly selected local products and the installed
CPython 3.11 standard library/Tk. It never downloads, installs, launches KH2,
opens a listener or writes the game folder. `friend-products.json` pins the
protocol-18 / AvatarBridge-4 cohort built from `545b550`. DLL, runtime, relay,
avatarctl and portable CLI share one source revision and build ID. Refresh all
five product receipts together; the existing validator rejects mixed versions,
cohorts and packaged hashes. The pinned local copies live under
`build/rig/vuh2234-protocol18-20261013-01/products/`.

This cohort builds all five native products with the static MSVC CRT. Earlier
release01/release02 products remain historical evidence, not current inputs.
Only the direct/transitive CRT import closure from the shipped products is copied
to `bin`; `crt-imports.json` retains each dumpbin output and the kept/omitted set
outside the ZIP. Python's own runtime DLLs remain in its separate directory.

Build the matched native products in one CMake directory with
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
same checkout, then refresh the existing per-role receipt paths and hashes.

```powershell
python -B tools/launcher/test_friend_package.py
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
TARGET_ALL, Normal difficulty and the two event-hold flags. Explicit inherited
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
rendering, or save preservation. Those require the lead's bounded desktop
rehearsal in a fresh unpacked folder. Never test by attempting an in-game save.

The v18 installed-package rehearsal used the extracted ZIP's GameOwner and
Session APIs, without feature environment overrides, on a two-player ENet
loopback. BC HP scaled 39 to 63 on both peers, the friend's real Sora took an
enemy hit, deaths agreed and the room transition produced no desync notice.
Four protected saves were unchanged. This validates the installed native cohort;
it does not establish a clean-machine install or two-PC Steam acceptance.
