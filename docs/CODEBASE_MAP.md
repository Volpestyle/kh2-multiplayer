# Codebase Map

Quick reference for what lives where and how the pieces connect.

## Build targets

```
cmake --build build --config Release                    # everything
cmake --build build --target kh2coop_inject --config Release   # inject DLL only
cmake --build build --target kh2ctl --config Release           # CLI tool only
cmake --build build --target kh2coop_server --config Release   # server only
cmake --build build --target kh2coop_fake_sim --config Release # E2E test
```

Offline test suites (no KH2): `kh2coop_fake_sim`, `kh2coop_avatar_test`,
`kh2coop_world_test`, `kh2coop_bridge_test`, `kh2coop_recovery_test` — each prints
`ALL CHECKS PASSED`. The recovery target exercises the production monotonic
scheduler plus actual ENet dropped-friend bootstrap and original-session pins.
Windows-only `kh2coop_spawntrace_test`, `kh2coop_lifecycletrace_test` and
`kh2coop_hittrace_test` exercise actual observer helpers with synthetic originals
and test-owned memory. `kh2coop_nativehit_test` exercises the production claim
consumer, incoming-hit context and production generation-guarded world/progress/
warp boundaries over owned memory and a headless transport. The Python
saved-log auditor controls run via `python -B -m unittest discover -s tests -p test_trace_audit.py`.

Dependencies: ENet v1.3.17 (FetchContent), MinHook v1.3.3 (FetchContent, inject only).

## Directory layout

```
kh2-multiplayer/
  inject/          ← DLL injected into KH2 process (friend control)
  runtime/         ← External process that reads KH2 memory + bridges to network
  server/          ← Host-authoritative multiplayer session server
  common/          ← Shared types, codec, protocol, networking, IPC mailbox
  tools/           ← kh2ctl CLI + MCP wrapper, avatarctl, headless Ghidra scripts
  scripts/         ← PowerShell helpers (restart-kh2, run MCP server, ghidra)
  tests/           ← E2E integration tests (no KH2 required)
  docs/            ← All documentation
  content/         ← Content/mod packaging notes
```

## inject/ — In-process DLL (the friend control system)

Loaded into the KH2 process by `kh2ctl launch`/`inject` (Cheat Engine is a fallback). Hooks the entity update loop to replace friend AI with player input.

| File | Lines | What it does |
|------|-------|-------------|
| `src/EntityHook.cpp` | ~2790 | **The big file.** Hook logic, hit ownership, activation and diagnostic initialization |
| `src/EntityHook.hpp` | 28 | Public API: `Initialize()`, `Shutdown()`, `OnFrame()` |
| `src/DllMain.cpp` | 217 | DLL entry point, Panacea plugin exports, standalone init thread |
| `src/PatternScan.hpp` | 126 | AOB pattern scanner for finding functions in the .text section |
| `src/RenderHook.cpp` | | D3D12 capture (screenshots/clips) and debug overlay |
| `src/Warp.cpp` | | Room warp requests via the game's transition function |
| `src/PlayerKit.cpp` | | VUH-1513 default-off (`KH2COOP_PLAYER_KIT=0x5A`) player kit: post-hook on the MEMT resolver `3E2EB0` sets resolved member 0 to Roxas in world 4; blocks native-Sora clone puppets while set (docs/PLAYER_KITS.md) |
| `src/SaveGuard.cpp` | ~520 | Redirects write opens under the KH2 save folder to a sandbox, denies deletes/moves/copies (installed first at init) |
| `src/CrashDump.cpp` | ~90 | Minidump on unhandled exceptions, chained ahead of the game's filter |
| `src/EnemySync.cpp` | | Checked native census, shared HP/deaths, actual-state hashes and activation leases; default-off all-alive historical-input recovery inside explicit ResyncPlan, exact full-set HP/claim hold, bounded completed-update/live-input hold and diagnostic receipts; automatic cached-world rejoin does not start this path |
| `src/ProgressSync.cpp` | ~330 | Verified SAVE progress snapshots/deltas and atomic boundary application |
| `src/NativeSpawnController.cpp` | | Ordinary fixed-combat activation hook and checked readers; opt-in exact host first-emission input/state recorder, bounded initialization binding,64-slot non-evicting per-controller completed-original receipts and fixed/generated/dispatcher/script traces |
| `src/NativeResourceTrace.cpp` | | Default-off exact-body-qualified `107240` package callback observer; raw ABI/SEH forwarding, copied candidate construction parents, bounded child queue, corrected registered trampoline and process-lifetime retention |
| `src/NativeOwnedEmitterCode.hpp/.cpp` | | Parked added-dispatch route: finite A/B/C emitter generator and stable280-byte POD, checked branches/unwind descriptions; no current recovery caller |
| `src/NativeOwnedEmitterGateway.hpp/.cpp`, `src/NativeOwnedEmitterGatewayRaw.asm` | | Parked direct-dispatch gateway: actual frame identity, one outstanding monotonic invocation, raw RAX and MASM return/SEH retirement; tombstones survive fresh Bootstrap resets |
| `src/NativeOwnedEmitterInstall.hpp/.cpp` | | Parked, default-off loaded emitter/unwind preparation with retained RX/RO/RW storage and Windows function tables; no current recovery branch writer or execution permit |
| `src/NativeLifecycleTrace.cpp` | ~340 | Five independently byte-verified opt-in removal/death/count probes, nested pre/post actor/controller/cache evidence and native unwind/loss reporting |
| `src/NativeHitTrace.cpp` | | Opt-in bounded ApplyHitDamage/TakeDamage/ApplyStatDelta observations, plus copied policy decisions and actual operation outcomes; separate incoming-delta/HP and checked-zero evidence, no ownership-rule changes |
| `src/DamagePolicy.cpp` | | Copied-facts active-session HP ownership matrix and fault-contained, exact-record amount-zero leaf; EntityHook owns current actor/companion evidence |
| `src/NativePrivateStatus.cpp` (+ `Pins.hpp`, `Scope.inc`) | | VUH-1489 default-off (`KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1`) private native status record for the GoA Friend1 native Sora on Steam `9002b2de`; byte-pinned pool/lookup/commit/free hooks, no native pool writes (docs/NATIVE_PRIVATE_STATUS.md) |
| `src/DownedSpike.inl`, `src/DownedSpikeState.hpp` | | VUH-1504 downed/revive owner side behind `KH2COOP_DOWNED_SPIKE=1`: game-over request gate, held downed episodes, LocalDownedState publication, native revive at 25% HP with grace and stand-up; test channel only with `_FIXTURE=1` (docs/DOWNED_REVIVE.md) |
| `src/PuppetHold.hpp` | | VUH-1787 pure DLL rules: flag the local avatar `AvatarInCutscene` on the own transition request and while a load is pending (no door ghost); a held puppet idles after `kHeldIdleFrames` unless downed and stops snapping its motion clock |
| `src/RevivePrompt.hpp` | | VUH-1504 player-facing revive trigger behind `KH2COOP_REVIVE_PROMPT=1`: pure Triangle-hold rules (range, cancels, one request per episode); drawn in the co-op HUD row (docs/DOWNED_REVIVE.md) |
| `src/CloneNeutralInput.inl` | | VUH-1489 default-off (`KH2COOP_CLONE_NEUTRAL_INPUT=1`) neutral input for a native P_EX100 clone: its own pad pointer `+0xDB8` goes to a neutral entry, and a pinned `0x3A89A0` movement-update detour runs it with FIELD_COMMAND's command record and stick neutralised (docs/CLONE_NEUTRAL_INPUT.md) |
| `src/EnemyPopulation.hpp` | | VUH-1788 default-off (`KH2COOP_ENEMY_POPULATION=1`) client population planner (force-spawn missing host enemies via the native factory) and the cull-hold decision used by EnemyMirror.inl's removal-predicate hook (docs/ENEMY_PARITY.md) |
| `src/EnemyMirror.inl`, `src/EnemyMirrorState.hpp`, `src/EnemyMirrorPose.hpp` | | VUH-1515 step 2 default-off (`KH2COOP_ENEMY_MIRROR=1`) mirrored Shadows: host `EnemyMotion` capture (EnemySync), client stream ring/render cursor, per-class brain-thunk skip, motion guard and post-update pose write (docs/ENEMY_PARITY.md) |
| `src/EnemyTargetRemote.inl`, `src/EnemyTargetRemote.hpp` | | VUH-1515 default-off (`KH2COOP_ENEMY_TARGET_REMOTE=1`, every machine): host enemies on the allowlist (Shadow 302) choose the nearest live player or remote clone at bdscript `target_search` (`exe+0x755B00` swap); one damage path per mode: Forward forwards zeroed clone hits as `RemoteHit` (type 45) and the owner cancels its local copy only under a fresh `TargetAuthority` (type 46, protocol v14) naming its slot; Mirror mode chooses targets only (docs/ENEMY_TARGET_REMOTE.md) |

EntityHook.cpp also holds the VUH-1501 hit-ownership hooks (BuildHit `0x3D23C0` log, ApplyHitDamage `0x3D3BA0` drop filter + claims, host apply), driven through `common/include/kh2coop/HitChannel.hpp`.
Those diagnostic claims/manual apply requests remain separate from protocol
HitClaim transport. ApplyHitDamage now snapshots an ordinary canonical-player
HP hit before client suppression and submits an immutable typed claim to
EnemySync. The host checks current roster connection, sequence, epoch, binding
and native combat metadata before one byte-verified TakeDamage attempt, then
recaptures the census before HP/death publication. WorldBridge v12 carries atomic
roster/delivery floors, explicit puppet authority, captured producer context and
a separate bounded CAS operator mailbox. Protocol v9 retains v3
claims and v4 connection-tagged avatar relays; AvatarBridge v2 carries pose
provenance, checked in the DLL even for an unchanged cached pose.
V5 also requires opaque world-incarnation identity and typed closure semantics.
V6 adds separate authenticated diagnostic request/chunk/done packets on reliable
channel 2. `DesyncCollector` copies actual local artifact bytes on one worker;
`DesyncUpload` paces owner-thread transport; `DesyncCapture` persists the frozen
all-peer aggregate and bounded suppression evidence. See `DESYNC_REPORTS.md`.
V7 adds a DLL-produced nonzero 64-bit sequence to absolute enemy HP. Relay,
network callbacks and the DLL reject older samples while retaining reliable
cache replay; see `HP_ORDERING.md`. `kh2coop_hp_order_test` exercises real local
ENet, reversed delivery, exact cache reconstruction and admission boundaries.
V8 adds fresh-capture transaction fencing, bounded complete native staging and
strict load/two-frame convergence ACKs; see `FORCED_RESYNC.md`. Dead bootstrap
and full native/remote acceptance remain open.
`common/include/kh2coop/ClientRecovery.hpp` owns bounded friend-only scheduling;
RuntimeMain pumps it and heartbeats before attachment waits. Native combat,
reconnect acceptance and attack-specific/boss behavior remain unverified.

### EntityHook.cpp sections

| Section | Lines | What |
|---------|-------|------|
| Function pointer typedefs | ~50-130 | Ghidra-derived signatures for all hooked game functions |
| AOB signatures + RVAs | ~135-205 | Pattern bytes and fallback addresses for hook targets |
| Actor struct offsets | ~207-232 | Velocity, acceleration, follow-timer, animation fields |
| Global state | ~250-370 | Hook trampolines, friend tracking, gamepad state, mode flags |
| Input reading | ~415-760 | KH2 raw input buffer parsing, stick normalization, deadzone |
| Mailbox polling | ~530-630 | Shared-memory IPC from runtime process |
| Camera retargeting | ~815-875 | Redirect camStruct+0x50 to friend actor |
| Movement injection | ~920-1085 | Camera-relative stick-to-world, velocity/facing writes |
| **HookedMotionChainSetAnim** | ~1108-1180 | Blocks per-frame animation resets, guards our own calls |
| **HookedFriendAI** | ~1240-1335 | Skips vanilla AI, sets animation on stick transitions |
| HookedFollowSteering | ~1345-1375 | Returns zero vector to disable tether |
| HookedPerEntityUpdate | ~1510-1645 | Main hook: frame boundary, friend ID, gamepad reads, post-update |
| Initialize/Shutdown | ~1650-1930 | MinHook setup, AOB scan, hook installation, mailbox init |

## runtime/ — External process (memory reader + network bridge)

Attaches to KH2 via `ReadProcessMemory`/`WriteProcessMemory`. Reads game state, manages camera (via fake actor allocation), and connects to the session server.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/KH2Offsets.hpp` | 475 | **Master offset map** — all confirmed memory addresses |
| `include/kh2coop/GameBridgePC.hpp` | 146 | Concrete `IGameBridge` for KH2 PC |
| `include/kh2coop/IGameBridge.hpp` | 26 | Abstract game memory interface |
| `include/kh2coop/CameraController.hpp` | 74 | Per-frame camera override with room transition handling |
| `include/kh2coop/ReplicaController.hpp` | 81 | Applies incoming snapshots to non-owned entity slots |
| `src/GameBridgePC.cpp` | 1032 | Process attach, entity discovery, state reads/writes, camera |
| `src/RuntimeMain.cpp` | 996 | Main loop: config, per-frame tick, network client, mailbox IPC |
| `include/kh2coop/DesyncCollector.hpp`, `src/DesyncCollector.cpp` | | One-worker local log/metadata/capture spool, checked mailbox witnesses and bounded retirement/deadline handling |

## server/ — Multiplayer session server (relay)

Accepts client connections, version-gates them and assigns party slots. Relays
owner-authoritative avatars (stamping the owner slot) and host-authored world
sync (room transitions, cutscene holds, enemy manifest/HP/deaths, progress),
which it accepts only from the host (slot Player). Routes authenticated,
current-room hit claims to the host only. Caches world state so late joiners
are caught up. Exact complete frames and current nonzero room epochs guard
hold/manifest/HP/death cache writes; HP/death also require the current manifest.
Old-room values cannot be replayed under a new epoch. Host resync now requests
fresh checked native capture and stages an immutable transaction with delivery
fencing; native recovery acceptance remains pending ([FORCED_RESYNC.md](FORCED_RESYNC.md)).
Co-op host departure/expiry closes old peer connections and
clears those caches while the relay keeps listening. The older
fake-physics `SimulationState` is a test double.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/SessionHost.hpp` | 129 | Session manager with peer lifecycle and slot assignment |
| `include/kh2coop/SimulationState.hpp` | 48 | Server-side fake physics for 3 actors |
| `include/kh2coop/PeerState.hpp` | 43 | Per-peer tracking (slot, status, heartbeat) |
| `src/ServerMain.cpp` | 133 | Entry point, 60fps main loop |
| `src/SessionHost.cpp` | ~1000 | Handshake, avatar/world relay, current-epoch claim routing, late-join state and co-op host-loss teardown |
| `src/SimulationState.cpp` | 213 | Input-driven movement, gravity, action timers |
| `include/kh2coop/DesyncCapture.hpp`, `src/DesyncCapture.cpp` | | Frozen all-peer byte assembly, async manifests, exact digests, partial/error outcomes and coalesced suppression evidence |

## common/ — Shared library

Used by all components. Defines the wire protocol, domain types, serialization, and IPC.

| File | Lines | What |
|------|-------|------|
| `include/kh2coop/Types.hpp` | ~190 | Vec3, ActorState, AvatarState, InputFrame, SlotType, etc. |
| `include/kh2coop/Protocol.hpp` | ~250 | Session messages, clock sync, world sync (RoomTransition, EventHold, EnemyManifest/Hp/Death, HitClaim, TransitionAck, ProgressUpdate) |
| `include/kh2coop/Codec.hpp` | ~170 | PacketType enum, encode/decode declarations, `isWorldPacket` |
| `include/kh2coop/ByteBuffer.hpp` | 140 | Little-endian byte writer/reader |
| `include/kh2coop/DesyncProtocol.hpp` | | Bounded diagnostic keys, roster/hash witnesses, byte chunks and descriptors |
| `include/kh2coop/ResyncProtocol.hpp` | | Immutable forced-resync request/targets, bounded snapshot staging, native ACK/result and optional SHA-covered HARP/v1 historical-input trailer; metadata excluded from native-state fingerprint |
| `include/kh2coop/WorldContext.hpp` | | Lightweight observation-time generation/delivery/native source context |
| `include/kh2coop/ResyncEvidence.hpp` | | Bounded exact terminal-result evidence with escaped peer errors |
| `include/kh2coop/DesyncUpload.hpp` | | Fixed-deadline, identity-bound upload pacing before game attachment |
| `include/kh2coop/CaptureLease.hpp` | | Per-PID cooperative mutex for automatic and CLI capture/clip callers |
| `include/kh2coop/NetworkClient.hpp` | ~155 | ENet client: callbacks, avatars, clock sync, world sync, raw packets, link-conditioner test hook |
| `include/kh2coop/LinkConditioner.hpp` | ~90 | Seeded latency/jitter/loss per direction for repeatable network tests |
| `include/kh2coop/AvatarInterpolator.hpp` | ~115 | Per-avatar snapshot buffer sampled at a render delay |
| `include/kh2coop/AvatarSync.hpp` | | Current-roster admission, per-connection interpolation retirement and immutable sampled owner identity; VUH-1787 holds a stalled stream's newest pose (`AvatarHeld`, receiver-local) from 1 s until `releaseAfterMs` (3 s default, `KH2COOP_AVATAR_HOLD_MS`) |
| `include/kh2coop/AvatarBridge.hpp` | | Version 2 shared memory DLL/runtime: unchanged local avatar, provenance-tagged puppet poses (seqlock) |
| `include/kh2coop/PuppetProvenance.hpp` | | Platform-free network/standalone pose eligibility against explicit receiver authority and slot mapping |
| `include/kh2coop/PacketRing.hpp` | ~125 | Lock-free SPSC ring of variable-length packets in shared memory |
| `include/kh2coop/RuntimeWriterLease.hpp` | ~32 | Bounded pump heartbeat and expired-generation tombstone |
| `include/kh2coop/WorldBridge.hpp` | ~190 | Shared world rings, immediate session generation and generation-bound ordered reset |
| `include/kh2coop/WorldPump.hpp` | ~160 | Reliable world backlog and expendable activation-lease forwarding |
| `include/kh2coop/ActivationLease.hpp` | ~120 | Bounded challenge state; fixed original-request expiry and replay rejection on a client-local clock |
| `include/kh2coop/ProgressMirror.hpp` | ~145 | Host story-flag diff/snapshot; client allow-list accept + re-assert |
| `include/kh2coop/ProgressAllowList.hpp` | | Verified 8108-byte SAVE progress selection and boundary bit masks; personal data excluded |
| `include/kh2coop/InputMailbox.hpp` | 412 | Cross-process shared memory IPC (seqlock, 3 slots) |
| `src/Codec.cpp` | ~730 | All serialization implementations |
| `src/NetworkClient.cpp` | ~405 | ENet connect/tick/send, clock sync, conditioned send/receive |

## tools/ — Developer tooling

| File | What |
|------|------|
| `kh2ctl/src/main.cpp` (1498 lines) | CLI for KH2 control: process attach, state queries, save loading, input injection |
| `kh2ctl/src/world_resync.cpp` | Existing admitted-host mailbox command, isolated from local HitChannel types; queue receipt only |
| `mcp_kh2ctl/server.py` (372 lines) | Python MCP server wrapping kh2ctl for agent use |
| `scenario/run.py` | Scenario runner: rig lock, save hashing, JSON scenarios, crash/hang bundles, reports; default-off current secondary equipment/map binding capture (`docs/SCENARIOS.md`) |
| `scenario/resync_evidence.py` | Strict native ACK/original-target/relay/runtime joins and independent fresh native evidence; incomplete remains failure |
| `scenario/trace_audit.py` | Saved spawn/lifecycle envelope and predicate auditor, plus separate native-hit incoming witnesses and damage-policy matrix/checked-zero evidence; never overall gameplay acceptance |
| `scenario/scenarios/*.json` | Live controls and runner self-tests, including strict two-pack native HP/death acceptance and positional activation controls |
| `scenario/scenarios/net_reconnect_shadows_activation_replay.json` | Default-off98-step single-cycle historical-input experiment, derived from population93 plus five live-input hold checks;512 historical/120 live completed selected-controller originals under the unchanged30s deadline |
| `avatarctl/main.cpp` | Drive an AvatarBridge without a network: `synth`, `record`/`replay`, `fake-local`, `peek` |
| `ghidra/*.java` | Headless Ghidra scripts behind `scripts/ghidra.ps1` (decompile, xrefs, strings, symbols) |

## tests/

Every `kh2coop_*_test` executable (plus each `kh2coop_resourcetrace_test`
failure mode) is registered with CTest; after a Release build run
`ctest --test-dir build -C Release` serially (several tests bind fixed
loopback ports). None launches or attaches to KH2.

| File | What |
|------|------|
| `FakeSimulation.cpp` (772 lines) | E2E test: 3 clients + server, verifies handshake, input exchange, snapshot consistency, event delivery |
| `AvatarRelayTest.cpp` | Avatar codec, LinkConditioner, interpolation, AvatarSync, AvatarBridge; 3 clients at 100 ms + 2% loss with skewed clocks |
| `AvatarHoldTest.cpp` | VUH-1787 pure stall rules: injected 1-3 s stalls held, release past `releaseAfterMs`, immediate room/world/cutscene/roster/session/connection exits, peer cannot set `AvatarHeld`, moved-owner resume, `KH2COOP_AVATAR_HOLD_MS` parsing, own-load door flag and held idle (`PuppetHold.hpp`), seeded 60 Hz 10-minute stall soak |
| `WorldSyncTest.cpp` | Host-only world sync, claim epoch/slot routing, late-join state, co-op disconnect/expiry/rejoin, acks and ProgressMirror policy |
| `ForcedResyncTest.cpp` | Actual codec/staging/ENet transaction, deadline/conditioning/fixed-target controls; native facts explicitly synthetic |
| `WorldWireFixture.hpp` | Test-only authenticated raw endpoint access for malformed and retained scoped records |
| `test_resync_evidence.py` | Saved-evidence positive/negative controls; synthetic fixture facts cannot prove native execution |
| `BridgeTest.cpp` | PacketRing (incl. two-thread stress) and fake DLL to relay to fake DLL world events |
| `ActivationLeaseTest.cpp` | Fixed request deadline, queued/duplicate/reordered replies, exact challenge identity and reset invalidation |
| `NativeSpawnTraceTest.cpp` | Windows-only headless native exception/original-call regression using actual trace code, synthetic callbacks and test-owned memory; no game or hook installation |
| `NativeResourceTraceTest.cpp` | Windows-only actual observer/MinHook installation, retained retirement, ABI/SEH and bounded-queue controls over owned executable memory; copied game identity bytes are never executed |
| `NativeLifecycleTraceTest.cpp` | Windows-only headless nested lifecycle/original-call, unavailable state, thread-affinity and queue/unwind controls using synthetic originals; no installed hooks |
| `NativeHitClaimTest.cpp` | Production claim consumer/publisher and read-only native-hit/damage context with all three full-width roster IDs over owned memory and headless transport; no game or installed hooks |
| `DamagePolicyTest.cpp` | Production policy matrix and owned-record zero-leaf controls; explicitly synthetic original/claim harness, no production membership adapter or game hooks |
| `DownedSpikeStateTest.cpp` | VUH-1504 downed/revive pure rules: gate decision, revive HP rewrite, publish kinds, episode minting/re-minting and the pinned fixture channel layout |
| `CloneNeutralInputTest.cpp` | VUH-1489 clone neutral input: install refusals, `+0xDB8` Gate and the `0x3A89A0` detour (neutralise/restore, exception, nested call, pass-through) over fake game memory with MinHook/warp/playerkit stand-ins |
| `EnemyMirrorTest.cpp` | VUH-1515 `EnemyMotion` codec, loopback relay forwarding rules (host-only, epoch, sequence, no late-join replay) and the pure client stream state |
| `EnemyPopulationTest.cpp` | VUH-1788 population planner (missing clock, spawn gap, attempt/forced caps, retry wait) and the cull/force-remove decisions |
| `EnemyMirrorDriverTest.cpp` | VUH-1515 native mirror driver over fake game memory: brain skip/pass-through, motion guard, death, blend, clamp, class refusal |
| `EnemyTargetRemoteTest.cpp` | VUH-1515 enemy targeting pure policy: allowlist, player form, nearest/hold/hysteresis/per-clone cap, clone candidacy (downed, cutscene, shared status, stale), owner apply refusals |
| `NativeHitTraceTest.cpp` | Production hit trace/policy serializer and owned zero leaf; synthetic original/argument/return/SEH, scope/queue and outcome controls; baseline and policy-veto emitter modes |
| `test_trace_audit.py` | Saved-log envelope/provenance, predicate and native-hit schema, coverage, HP/delta, ambiguity and historical-limit controls |

## scripts/

| File | What |
|------|------|
| `restart-kh2.ps1` | Kill/rebuild/relaunch KH2 bypassing Steam launcher |
| `run-kh2ctl-mcp.ps1` | Launch the kh2ctl MCP server |
| `ghidra.ps1` | Headless Ghidra: `-Setup`, `-Decompile`, `-Xrefs`, `-Strings`, `-Symbols` by RVA |

## How the pieces connect

```
                    ┌──────────────────┐
                    │   KH2 Process    │
                    │                  │
                    │  ┌────────────┐  │     shared memory
                    │  │ inject DLL │◄─┼──── (InputMailbox) ◄── runtime process
                    │  └────────────┘  │                         │
                    │        │         │                         │ ENet
                    │   hooks entity   │                         │
                    │   update loop    │                    ┌────▼────┐
                    │                  │                    │ server  │
                    └──────────────────┘                    └─────────┘
                                                                │
                                                           ENet │
                                                                │
                                                        ┌───────▼───────┐
                                                        │ other clients │
                                                        └───────────────┘
```

- **inject DLL** runs inside KH2, hooks entity updates, reads input from mailbox or local gamepads
- **runtime** runs outside KH2, reads game state via ReadProcessMemory, writes input to the mailbox, connects to server
- **server** relays avatars and host-authored world sync between all connected clients
- **kh2ctl** is a developer tool that talks to the runtime's mailbox and KH2's memory

Avatar and world paths (plan D2/D9; the DLL sides of both bridges are live-lane work):

```
 KH2 + inject DLL                 runtime                       relay
 ----------------                 -------                       -----
 local avatar --AvatarBridge--> sendAvatar ---AvatarState----> stamp slot +
                                                               connection ID,
                                                                forward to
 puppet poses <-AvatarBridge--- AvatarSync <--AvatarRelay----- the others
                                (interpolate)
 world events --WorldBridge---> WorldPump ----world packets--> host-only check,
 (transition, enemies, claims)                                  cache, forward
 act on them  <-WorldBridge---- onWorldEnvelope <--------------- (claims -> host)
```

The opt-in two-player empty-AI layout adds `inject/src/PartyEmptySeat.{hpp,cpp}` for scoped native row/status and pause/history projections, and `PartyEmptyPackage.{hpp,cpp}` with pinned payload data for read-only selected-ARD qualification. GoA/Borough, pause, first Items selection and next-load restore passed live in run20261008-024646, fixture `build/rig/party-empty2-20261007-01/rev6/live-fixture-10`. See `docs/PARTY_SETUP.md` for flags and qualification limits.

- `inject/src/PuppetCommandGuard.{hpp,cpp}`: mandatory puppet-only Drive/Summon command admission, native exact-byte guards and retained callback/trampoline lifetime; no status or SAVE writes.

Host-chosen Donald/Goofy party layouts: `build/rig/party-choice-20261007-01/rev5`, code `3e3dcc6`. Two-player Donald/Goofy passed live in runs `20261008-184452` / `20261008-185124`; shared solo support remains UNQUALIFIED pending corrected fixtures. Policy/install/A2 controls and source/product pins are retained in rev5. See `docs/PARTY_SETUP.md`.
