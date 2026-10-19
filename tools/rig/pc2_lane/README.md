# PC2 scenario lane (VUH-2338, VUH-2464)

Runs one checked-in scenario on PC2 (`bld`) as a sealed, single-attempt packet under
`docs/PC2_PACKET_RETARGETING.md`. Adapted from boot-smoke07's `smoke.py`; first used by
`vuh2338-bld-20261019-01` (account-B New Game to first control, PASS).

**Products.** A portable static-CRT build of the current revision in `build-pc2/` (git-ignored):

```powershell
cmake -S . -B build-pc2 -G "Visual Studio 18 2026" -A x64 -DKH2COOP_PORTABLE_PACKAGE=ON `
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DCMAKE_POLICY_DEFAULT_CMP0091=NEW `
  -DFETCHCONTENT_SOURCE_DIR_ENET=<build/_deps/enet-src> -DFETCHCONTENT_SOURCE_DIR_MINHOOK=<build/_deps/minhook-src> `
  -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build build-pc2 --config Release --target kh2coop_inject kh2coop_runtime_scaffold kh2ctl kh2ctl_diagnostic
```

Deterministic `/pathmap` flags belong in the `*_RELEASE` flag variables only: in `CMAKE_C_FLAGS` they
break CMake's Debug compiler check (C1090, PDB path remapped). The friend ZIP builder is not used:
`tools/launcher/product_protocol.py` still pins protocol 18.

**Packet.** `python -B tools/rig/pc2_lane/build_packet.py NAME SCOPE SCENARIO BUDGET_SECONDS` writes
`build/rig/pc2-lanes/NAME/` (pins and lead review PENDING). Its `payload/` is a portable package root
(`KH2COOP-PACKAGE`, `package.json`, `bin/` with the portable CLI, `kh2ctl_diagnostic` and ffmpeg),
plus the runner, launcher policy, route pilot and its prologue routes, a filtered CPython 3.11 and
Pillow. Before review, run the offline control from a copy: `payload/python/python.exe -I -X utf8 -B
payload/lane.py --control`. A changed packet is a new NAME (`-02`, ...); keep unused ones archived.

**Run.** After an independent review and the ADOPT receipt, copy the packet and the day's consent
record to `%USERPROFILE%\lead-outbox\<lane>\` on bld (create the folder first), verify the pins there,
write a GO record (`lane`, `runId`, `pinsSha256`, `consentSha256`) and queue
`desktop-lane.ps1 -Go ... -Consent ...` through bld's Session-1 desk agent. The lane checks consent
scope, host/user, account-B's save hash, the game EXE and build, and the console session; takes
`lead-outbox\kh2-rig\rig.lock` and a spent marker; inventories account B, the EXE and the app-ID
file before and after; and closes only games it launched.

**Input and media.** Input is the DLL's pad mailbox only: no window is foregrounded and no OS input
is sent (the route pilot's pad-mode release no longer sends key-ups). `kh2ctl_clip` is locked to an
older lane's contract, so `clip` here is a burst of `kh2ctl_diagnostic capture` calls encoded by the
payload's ffmpeg, about 5 fps: publish it as that, not as 30 fps video. Clip frames go under the
payload root: the game writes each PNG itself and fails past MAX_PATH (a 261-character evidence path
failed every capture in a PC1 rehearsal).

**PC1 rehearsal.** `lane.py --rehearsal --evidence DIR` runs the same lane on PC1 without the bld
identity, consent, GO and seal checks (lock and evidence under DIR, inventory of the local account),
so a paired scenario can be rehearsed with the host's runner before spending the bld attempt.
