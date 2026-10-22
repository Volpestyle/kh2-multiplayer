# Codebase Map

Navigation guide reviewed 2026-10-20 against main `2ed98fa`. Source versions:
protocol **34**, AvatarBridge **4**, WorldBridge **13**, CaptureChannel **1**.
Tables list key components rather than every source file; use the build files
for the complete target list and symbol searches for current locations.
[Setup](../SETUP.md) covers building; the [plan](ONLINE_COOP_PLAN.md) owns current
status, player defaults and qualification limits.

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

| File | What it does |
|------|-------------|
| `src/EntityHook.cpp` | **The big file.** Hook logic, hit ownership, activation and diagnostic initialization |
| `src/EntityHook.hpp` | Public API: `Initialize()`, `Shutdown()`, `OnFrame()` |
| `src/DllMain.cpp` | DLL entry point, Panacea plugin exports, standalone init thread |
| `src/PatternScan.hpp` | AOB pattern scanner for finding functions in the .text section |
| `src/RenderHook.cpp` | D3D12 capture (screenshots/clips) and debug overlay |
| `src/SceneCrown.cpp` | In-renderer native-scene crown drawing, using installed assets; [host crown](HOST_CROWN.md) |
| `src/CrownAsset.cpp` | VUH-2480: loads KH2FM's crown mesh and gold sprite from the player's install on a background thread (`docs/HOST_CROWN.md`) |
| `src/EventStartMirror.cpp` | VUH-2359: records the host's checked in-field event starts (FUN_1403AC350) for the event-hold Acquire; a friend in the same field replays them natively (`docs/PARTY_SETUP.md`) |
| `src/NativePartyHud.cpp` | VUH-2568: owner SeatState vitals feed display-only native friend gauges and portraits; local player and gameplay status stay native (`docs/NATIVE_PARTY_HUD.md`) |
| `src/PrologueParty.cpp`, `src/PrologueParty.hpp` | Profile/readiness exchange, checked prologue party sites, owner-seat identity and vitals publication; see [party setup](PARTY_SETUP.md) |
| `src/CloneNeutralInput.inl` (command hook) | Prevents extra player-class actors from consuming the local command menu twice; [native guest Magic](NATIVE_GUEST_MAGIC.md) |
| `src/Warp.cpp` | Room warp requests via the game's transition function |
| `src/PlayerKit.cpp` | VUH-1513 default-off (`KH2COOP_PLAYER_KIT=0x5A`) player kit: legacy standalone kit path through MEMT resolver `3E2EB0`; current prologue selection uses PrologueParty/PrologueSeats separately (docs/PLAYER_KITS.md) |
| `src/SaveGuard.cpp` | Redirects write opens under the KH2 save folder to a sandbox, denies deletes/moves/copies (installed first at init) |
| `src/CrashDump.cpp` | Minidump on unhandled exceptions, chained ahead of the game's filter |
| `src/EnemySync.cpp` | Checked native census, shared HP/deaths, actual-state hashes and activation leases; default-off all-alive historical-input recovery inside explicit ResyncPlan, exact full-set HP/claim hold, bounded completed-update/live-input hold and diagnostic receipts; automatic cached-world rejoin does not start this path |
| `src/ProgressSync.cpp` | Verified SAVE progress snapshots/deltas and atomic boundary application |
| `src/NativeSpawnController.cpp` | Ordinary fixed-combat activation hook and checked readers; opt-in exact host first-emission input/state recorder, bounded initialization binding,64-slot non-evicting per-controller completed-original receipts and fixed/generated/dispatcher/script traces |
| `src/NativeResourceTrace.cpp` | Default-off exact-body-qualified `107240` package callback observer; raw ABI/SEH forwarding, copied candidate construction parents, bounded child queue, corrected registered trampoline and process-lifetime retention |
| `src/NativeOwnedEmitterCode.hpp/.cpp` | Parked added-dispatch route: finite A/B/C emitter generator and stable280-byte POD, checked branches/unwind descriptions; no current recovery caller |
| `src/NativeOwnedEmitterGateway.hpp/.cpp`, `src/NativeOwnedEmitterGatewayRaw.asm` | Parked direct-dispatch gateway: actual frame identity, one outstanding monotonic invocation, raw RAX and MASM return/SEH retirement; tombstones survive fresh Bootstrap resets |
| `src/NativeOwnedEmitterInstall.hpp/.cpp` | Parked, default-off loaded emitter/unwind preparation with retained RX/RO/RW storage and Windows function tables; no current recovery branch writer or execution permit |
| `src/NativeLifecycleTrace.cpp` | Five independently byte-verified opt-in removal/death/count probes, nested pre/post actor/controller/cache evidence and native unwind/loss reporting |
| `src/NativeHitTrace.cpp` | Opt-in bounded ApplyHitDamage/TakeDamage/ApplyStatDelta observations, plus copied policy decisions and actual operation outcomes; separate incoming-delta/HP and checked-zero evidence, no ownership-rule changes |
| `src/DamagePolicy.cpp` | Copied-facts active-session HP ownership matrix and fault-contained, exact-record amount-zero leaf; EntityHook owns current actor/companion evidence |
| `src/NativePrivateStatus.cpp` (+ `Pins.hpp`, `Scope.inc`) | VUH-1489 default-off (`KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1`) private native status record for the GoA Friend1 native Sora on Steam `9002b2de`; byte-pinned pool/lookup/commit/free hooks, no native pool writes (docs/NATIVE_PRIVATE_STATUS.md) |
| `../common/include/kh2coop/MissionFlags.hpp` | Native mission flag/result constants shared by mission and route admission rules |
| `../common/include/kh2coop/RouteSpawnPolicy.hpp` | Exact Bailey missing-party and LoD generated-wave definitions, retained-record/load/binding policy; `docs/ROUTE_SPAWNS.md` |
| `../common/include/kh2coop/CrownProp.hpp` | Pure crown prop: zlib inflate, OpenKH EGS package, MDLX group/VIF decode, DDS sprite, character table, clip triangles; `docs/HOST_CROWN.md` |
| `src/DownedSpike.inl`, `src/DownedSpikeState.hpp` | VUH-1504 downed/revive owner side behind `KH2COOP_DOWNED_SPIKE=1`: game-over request gate, held downed episodes, LocalDownedState publication, native revive at 25% HP with grace and stand-up; test channel only with `_FIXTURE=1` (docs/DOWNED_REVIVE.md) |
| `src/PuppetHold.hpp` | VUH-1787 pure DLL rules: flag the local avatar `AvatarInCutscene` on the own transition request and while a load is pending (no door ghost); a held puppet idles after `kHeldIdleFrames` unless downed and stops snapping its motion clock |
| `src/RevivePrompt.hpp` | VUH-1504 player-facing revive trigger behind `KH2COOP_REVIVE_PROMPT=1`: pure Triangle-hold rules (range, cancels, one request per episode); drawn in the co-op HUD row (docs/DOWNED_REVIVE.md) |
| `src/CloneNeutralInput.inl` | VUH-1489 default-off (`KH2COOP_CLONE_NEUTRAL_INPUT=1`) neutral input for a native P_EX100 clone: its own pad pointer `+0xDB8` goes to a neutral entry, and a pinned `0x3A89A0` movement-update detour runs it with FIELD_COMMAND's command record and stick neutralised (docs/CLONE_NEUTRAL_INPUT.md) |
| `src/EnemyPopulation.hpp` | VUH-1788 player-default-on (`KH2COOP_ENEMY_POPULATION=1`) population removal/planner; generic factory creation requires separate parked SPAWN flag; includes the cull-hold decision used by EnemyMirror.inl's removal-predicate hook (docs/ENEMY_PARITY.md) |
| `src/EnemyMirror.inl`, `src/EnemyMirrorState.hpp`, `src/EnemyMirrorPose.hpp` | VUH-1515 step 2 (`KH2COOP_ENEMY_MIRROR=1`, player default on) mirrored enemies, every family except the D14 denylist (`kDenied`): host `EnemyMotion` capture (EnemySync), client stream ring/render cursor, per-class brain-thunk skip, motion guard and post-update pose write (docs/ENEMY_PARITY.md) |
| `src/EnemyTargetRemote.inl`, `src/EnemyTargetRemote.hpp` | VUH-1515 default-off (`KH2COOP_ENEMY_TARGET_REMOTE=1`, every machine): host enemies on the allowlist (Shadow 302) choose the nearest live player or remote clone at bdscript `target_search` (`exe+0x755B00` swap); one damage path per mode: Forward forwards zeroed clone hits as `RemoteHit` (type 45) and the owner cancels its local copy only under a fresh `TargetAuthority` (type 46, protocol v14) naming its slot; Mirror mode chooses targets only (docs/ENEMY_TARGET_REMOTE.md) |
| `src/StruggleOrbs.hpp`, `src/StruggleOrbsAdapter.hpp`, `src/StruggleOrbsHook.cpp` | VUH-2466, on unless `KH2COOP_STRUGGLE_ORBS=0`: the host books each Struggle orb and sends its birth and collection as SpawnEmission orb rows (protocol 34); the friend skips its own orb drops and pickups and shows the host's orbs through the native allocator (docs/PROLOGUE_ENCOUNTERS.md) |
| `src/MissionMirror.inl`, `src/MissionMirror.hpp` | VUH-2342, on with the enemy mirror (`KH2COOP_MISSION_MIRROR=0` parks it): host sends its running mission's gauges, counters, timer and result in `MissionState` (type 52, protocol 23); clients on the same mission file take them through the native setters, drop their own gauge/fail trap calls, and fail when the host fails (docs/MISSION_MIRROR.md) |
| `src/ScriptMirror.inl`, `src/ScriptMirror.hpp` | VUH-2252, on with the enemy mirror (`KH2COOP_SCRIPT_MIRROR=0` parks it): host swaps pinned bdscript trap slots (`exe+0x753490` tables) and sends mirrored enemies' calls (tilt, texture animation, shadow, scissoring) in `ScriptCall` (type 49, protocol 19) only when the value changed; clients replay them on the driven copy (docs/SCRIPT_MIRROR.md) |
| `src/KnownDormantScripts.hpp` | Reviewed room-script identity/dormancy rows for the resync-only ordinary view; currently the native-owned FM00 tutorial-game-over program with independently-set local flag40. Raw Script/317 authority denies remain unchanged. |
| `src/ScriptActorOwner.inl` | VUH-2569 actor-role ownership, capture/attachment, owner camera and command routing inside ScriptMirror; host-owned reaction occurrences use completion packet55. Broader captured phases and a Thorn kill remain unqualified. |
| `src/ScriptReaction.hpp`, `src/ScriptCamera.hpp` | Pure reaction identity/freshness/consume and seat-resolution rules, plus scoped camera timeline admission; native adapters retain pinned caller/body and lifetime checks. |

EntityHook.cpp also holds the VUH-1501 hit-ownership hooks (BuildHit `0x3D23C0` log, ApplyHitDamage `0x3D3BA0` drop filter + claims, host apply), driven through `common/include/kh2coop/HitChannel.hpp`.
Those diagnostic claims/manual apply requests remain separate from protocol
HitClaim transport. ApplyHitDamage now snapshots an ordinary canonical-player
HP hit before client suppression and submits an immutable typed claim to
EnemySync. The host checks current roster connection, sequence, epoch, binding
and native combat metadata before one byte-verified TakeDamage attempt, then
recaptures the census before HP/death publication. WorldBridge v13 carries atomic
roster/delivery floors, explicit puppet authority, captured producer context, a
separate bounded CAS operator mailbox, and (VUH-2640) the host's returning seats by
connection ID, the writer's incarnation and a terminal-networking receipt bound to
it in a 192-byte header. Protocol history: v9 retained v3 claims and v4 connection-tagged avatar relays;
AvatarBridge v2 introduced pose provenance. Current bridge v4 retains DLL
owner checks even for an unchanged cached pose.
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
RuntimeMain pumps it and heartbeats before attachment waits. Native combat now has scoped live evidence; broad populated reconnect and
attack-specific/boss acceptance remain open.

### Hook and feature navigation

`EntityHook.cpp` wires the native frame, input, ownership and feature hooks.
Enemy mirroring/targeting, party construction, event hold and downed behavior
have dedicated modules; historical line ranges no longer locate them reliably.
Player-launch defaults come from `tools/launcher/friend_package.py`; direct
DLL launches retain their own native defaults. Enabled behavior does not imply
whole-route or every-family acceptance.

## runtime/ — External process (memory reader + network bridge)

Attaches to KH2 via `ReadProcessMemory`/`WriteProcessMemory`. Reads game state, manages camera (via fake actor allocation), and connects to the session server.

| File | What |
|------|------|
| `include/kh2coop/KH2Offsets.hpp` | **Master offset map** — all confirmed memory addresses |
| `include/kh2coop/GameBridgePC.hpp` | Concrete `IGameBridge` for KH2 PC |
| `include/kh2coop/IGameBridge.hpp` | Abstract game memory interface |
| `include/kh2coop/CameraController.hpp` | Per-frame camera override with room transition handling |
| `include/kh2coop/ReplicaController.hpp` | Applies incoming snapshots to non-owned entity slots |
| `src/GameBridgePC.cpp` | Process attach, entity discovery, state reads/writes, camera |
| `src/RuntimeMain.cpp` | Main loop: config, per-frame tick, network client, mailbox IPC |
| `include/kh2coop/DesyncCollector.hpp`, `src/DesyncCollector.cpp` | One-worker local log/metadata/capture spool, checked mailbox witnesses and bounded retirement/deadline handling |

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

| File | What |
|------|------|
| `include/kh2coop/SessionHost.hpp` | Session manager with peer lifecycle and slot assignment |
| `include/kh2coop/SimulationState.hpp` | Server-side fake physics for 3 actors |
| `include/kh2coop/PeerState.hpp` | Per-peer tracking (slot, status, heartbeat) |
| `src/ServerMain.cpp` | Entry point, 60fps main loop |
| `src/SessionHost.cpp` | Handshake, avatar/world relay, current-epoch claim routing, late-join state and co-op host-loss teardown |
| `src/SimulationState.cpp` | Input-driven movement, gravity, action timers |
| `include/kh2coop/DesyncCapture.hpp`, `src/DesyncCapture.cpp` | Frozen all-peer byte assembly, async manifests, exact digests, partial/error outcomes and coalesced suppression evidence |

## common/ — Shared library

Used by all components. Defines the wire protocol, domain types, serialization, and IPC.

| File | What |
|------|------|
| `include/kh2coop/Types.hpp` | Vec3, ActorState, AvatarState, InputFrame, SlotType, etc. |
| `include/kh2coop/Protocol.hpp` | Session messages, clock sync, world sync (RoomTransition, EventHold, EnemyManifest/Hp/Death, HitClaim, TransitionAck, ProgressUpdate) |
| `include/kh2coop/EnemyClass.hpp` | Shared combat/boss classifier; admits exact Thorn307/type12 without globally admitting other type12 or script-created actors. |
| `include/kh2coop/PrologueSeats.hpp` | Qualified profile/readiness, solo-wait and seat-color rules; checked native site identities live in `RouteSpawnPolicy.hpp` |
| `include/kh2coop/EventScenes.hpp` | Checked host-led story scene and in-field event mappings |
| `include/kh2coop/Codec.hpp` | PacketType enum, encode/decode declarations, `isWorldPacket` |
| `include/kh2coop/ByteBuffer.hpp` | Little-endian byte writer/reader |
| `include/kh2coop/DesyncProtocol.hpp` | Bounded diagnostic keys, roster/hash witnesses, byte chunks and descriptors |
| `include/kh2coop/ResyncProtocol.hpp` | Immutable forced-resync request/targets, bounded snapshot staging, native ACK/result and optional SHA-covered HARP/v1 historical-input trailer; metadata excluded from native-state fingerprint |
| `include/kh2coop/WorldContext.hpp` | Lightweight observation-time generation/delivery/native source context |
| `include/kh2coop/ResyncEvidence.hpp` | Bounded exact terminal-result evidence with escaped peer errors |
| `include/kh2coop/DesyncUpload.hpp` | Fixed-deadline, identity-bound upload pacing before game attachment |
| `include/kh2coop/CaptureLease.hpp` | Per-PID cooperative mutex for automatic and CLI capture/clip callers |
| `include/kh2coop/NetworkClient.hpp` | ENet client: callbacks, avatars, clock sync, world sync, raw packets, link-conditioner test hook |
| `include/kh2coop/LinkConditioner.hpp` | Seeded latency/jitter/loss per direction for repeatable network tests |
| `include/kh2coop/AvatarInterpolator.hpp` | Per-avatar snapshot buffer sampled at a render delay |
| `include/kh2coop/AvatarSync.hpp` | Current-roster admission, per-connection interpolation retirement and immutable sampled owner identity; VUH-1787 holds a stalled stream's newest pose (`AvatarHeld`, receiver-local) from 1 s until `releaseAfterMs` (3 s default, `KH2COOP_AVATAR_HOLD_MS`) |
| `include/kh2coop/AvatarBridge.hpp` | Version 4 shared memory DLL/runtime: local avatar, provenance-tagged puppet poses (seqlock) |
| `include/kh2coop/PuppetProvenance.hpp` | Platform-free network/standalone pose eligibility against explicit receiver authority and slot mapping |
| `include/kh2coop/PacketRing.hpp` | Lock-free SPSC ring of variable-length packets in shared memory |
| `include/kh2coop/RuntimeWriterLease.hpp` | Bounded pump heartbeat and expired-generation tombstone |
| `include/kh2coop/WorldBridge.hpp` | Shared world rings, immediate session generation and generation-bound ordered reset |
| `include/kh2coop/WorldPump.hpp` | Reliable world backlog and expendable activation-lease forwarding |
| `include/kh2coop/ActivationLease.hpp` | Bounded challenge state; fixed original-request expiry and replay rejection on a client-local clock |
| `include/kh2coop/ProgressMirror.hpp` | Host story-flag diff/snapshot; client allow-list accept + re-assert |
| `include/kh2coop/ProgressAllowList.hpp` | Verified 8108-byte SAVE progress selection and boundary bit masks; personal data excluded |
| `include/kh2coop/InputMailbox.hpp` | Cross-process shared memory IPC (seqlock, 3 slots) |
| `src/Codec.cpp` | All serialization implementations |
| `src/NetworkClient.cpp` | ENet connect/tick/send, clock sync, conditioned send/receive |

## tools/ — Developer tooling

| File | What |
|------|------|
| `kh2ctl/src/main.cpp` | CLI for KH2 control: process attach, state queries, save loading, input injection |
| `kh2ctl/src/world_resync.cpp` | Existing admitted-host mailbox command, isolated from local HitChannel types; queue receipt only |
| `mcp_kh2ctl/server.py` | Python MCP server wrapping kh2ctl for agent use |
| `scenario/run.py` | Scenario runner: rig lock, save hashing, JSON scenarios, crash/hang bundles, reports; default-off current secondary equipment/map binding capture (`docs/SCENARIOS.md`) |
| `scenario/scenarios/net_twilight_thorn_reactions.json` | Short opening/ground RC proof for the exact trimmed build; stops the existing pilots at the joined milestone and finalizes paired clips. Separate from the full-kill rehearsal. |
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
| `PrologueSeatsTest.cpp` | Profile/readiness, solo-wait and seat-color pure controls; no native-site or live route acceptance |
| `ScriptCameraTest.cpp`, `ScriptOwnerCommandTest.cpp`, `ScriptReactionCacheTest.cpp` | Stable camera/owner-command and native-reaction receipt policy controls, including canonical-seat selection and empty client candidate tables; no live boss acceptance. |
| `SteamBrokerTest.cpp`, `SteamNativeAdapterTest.cpp`, `SteamPipeTest.cpp` | Broker policy, native adapter and authenticated local IPC controls; live two-account evidence is separate |
| `NativePartyHudTest.cpp` | Display-only owner vitals, gauge views and retirement controls over owned memory |
| `CrownPropTest.cpp` | Installed-asset crown parsing and geometry controls |
| `FakeSimulation.cpp` | E2E test: 3 clients + server, verifies handshake, input exchange, snapshot consistency, event delivery |
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
| `test_desk_stop.py` | VUH-2617 desk stop: `tools/rig/desk-scenario.ps1` copies in throwaway checkouts refuse any run while the stop file exists |
| `test_trace_audit.py` | Saved-log envelope/provenance, predicate and native-hit schema, coverage, HP/delta, ambiguity and historical-limit controls |

## scripts/

| File | What |
|------|------|
| `restart-kh2.ps1` | Legacy human-only helper that kills every KH2; agents use guarded `kh2ctl` launch/restart/kill |
| `run-kh2ctl-mcp.ps1` | Launch the kh2ctl MCP server |
| `ghidra.ps1` | Headless Ghidra: `-Setup`, `-Decompile`, `-Xrefs`, `-Strings`, `-Symbols` by RVA |

## How the pieces connect

The diagram shows standalone ENet. In Steam mode SessionHost stays in the
host runtime; the game brokers exchange authenticated relay-only traffic
through Valve instead of starting `kh2coop_server`. The host's KH2 remains
world authority in both modes. [Steam transport](STEAM_TRANSPORT.md) records
its two-PC technical qualification; [native HUD](NATIVE_PARTY_HUD.md) and
[prologue encounters](PROLOGUE_ENCOUNTERS.md) retain their separate loopback limits.


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
