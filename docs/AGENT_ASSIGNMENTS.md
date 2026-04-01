# Agent Assignments

## Agent 1 — Runtime / Reverse Engineering
**Owns:** live memory mapping and the `IGameBridge` implementation.

### Responsibilities
- find actor structures for slot 0/1/2
- find camera target state and override point
- find input read / consume path for friend slots
- find room/world/transition/cutscene state
- find enemy list root and per-enemy state
- maintain `pointer_map_v1.md` and `KH2Offsets.hpp`

### IGameBridge method status
| Method | Status | Notes |
|--------|--------|-------|
| `Attach()` | DONE | Process name scan + module base resolution |
| `Tick()` | DONE | Room change detection, entity re-discovery, camera re-point |
| `DiscoverEntityAddresses()` | DONE | Camera pointer chain (primary) + vtable scan (fallback) |
| `ReadRoomState()` | DONE | World/room/program/cutscene state |
| `ReadActorState(slot)` | DONE | All 3 slots: position/rotation/velocity/airborne/HP via camera discovery + Slot1 actor pointers |
| `ReadEnemyStates()` | PARTIAL | Traverses active-entity list head `exe+0x2A171C8`, resolves `actor+0xA90` handles via `exe+0x2B0D720`, reads `objectId` from `actor+0x918`, and filters moveState `8/9` plus objentry prefix `B_`/`M_` |
| `WriteCameraTarget(slot)` | DONE | Fake actor allocation + pointer redirect |
| `RestoreVanillaCamera()` | DONE | Pointer restore + VirtualFreeEx |
| `InjectOwnedInput(slot, input)` | TODO | Per-slot input path not RE'd |
| `ApplyReplicaActorState(state)` | PARTIAL | Slot 0: dual-write pos/rot/flags. All slots: HP + camera fake actor |
| `ApplyReplicaEnemyState(state)` | TODO | Needs enemy entity struct discovery |

### Remaining RE blockers (Track A)
- [ ] Per-slot input injection path
- [ ] Enemy HP/spawn-group mapping for full replication
- [ ] MP offset within unit slot

### Success looks like
The rest of the team can write against `IGameBridge` without doing live memory discovery themselves.

---

## Agent 2 — Networking / Host Server
**Owns:** session authority, packet schemas, relay logic, version gating.

### Status
- `SessionHost`: DONE (lobby, version gate, slot assignment, broadcast, stale-peer eviction)
- `SimulationState`: DONE (fake physics, movement/gravity/jump/attack/guard/dodge)
- `ServerMain`: DONE (CLI, 60fps loop, signal handling)
- `Protocol.hpp`: DONE (v1 structs), UPDATED (v2 forward-looking records declared)
- `Codec.hpp/cpp`: DONE (all v1 types serialized)
- `NetworkClient`: DONE (ENet transport, callback dispatch)
- `FakeSimulation` test harness: DONE (3-client integration passes)

### Remaining work (Track A)
- [ ] Wire `NetworkClient` into the runtime (end-to-end with live KH2)
- [ ] Packet logging for replay/debug
- [ ] Reconnect flow with state resync
- [ ] `TransitionAck` codec implementation (declared but not wired)

### Future work (Track B/C)
- [ ] `ClientHello` / `ClientHelloAck` codec (replace SessionState-as-handshake)
- [ ] Session/instance split (`PartySession` + `InstanceRuntime`)
- [ ] Realm service integration for PublicRealm mode
- [ ] v2 protocol record codecs (`RealmSeed`, `CharacterRecord`, `InstanceDescriptor`, etc.)

### Success looks like
A fake simulation can run without KH2 attached. (Achieved.)

---

## Agent 3 — Client Replica / Camera / HUD
**Owns:** everything the player actually feels on the non-host clients.

### Status
- `CameraController`: DONE (per-slot follow, cutscene/transition safety, F8 panic toggle)
- `ReplicaController`: DONE (snapshot dedup per-slot actors, per-netId enemies)
- `RuntimeMain`: DONE (INI config, CLI args, attach loop, mode logging)

### Remaining work
- [ ] P2/P3 overlay HUD
- [ ] Debug overlay for ownership / ping / desync
- [ ] Remote actor smoothing and large-divergence correction
- [ ] Public-realm mode: `RemoteReplica` actor rendering (Track C)

### Success looks like
Given snapshots from Agent 2 and actor writes from Agent 1, each client feels locally coherent.

---

## Agent 4 — Content / Test Harness
**Owns:** the safest possible test environment.

### Responsibilities
- GoA-only room plan
- fixed encounter packs
- fixed roster definitions
- friend-slot keyblade-user test characters
- mod manifest and packaged content

### Must hand off
- `goa_test_plan.md`
- `roster_pack_v1.md`
- content hash used by all clients

### Success looks like
The team can test netcode without campaign variance.

---

## Agent 5 — Realm / Scale (Track B/C, future)
**Owns:** persistent services and public-realm architecture.

### Responsibilities (when activated)
- `RealmService` implementation
- Save-file to `RealmSeed` importer
- Character persistence layer
- Party management
- Instance lifecycle (create/join/leave/destroy)
- Public hub instance

### Prerequisite
Track A must reach a stable 3-player vertical slice before this agent activates.

---

## Integration order
1. Agent 1 and Agent 2 work first (current).
2. Agent 3 starts as soon as camera write + packet feed exist (active).
3. Agent 4 should avoid expanding scope until Milestone 5 is stable.
4. Agent 5 activates after Track A vertical slice + Track B refactors.

## Daily integration contract
At the end of each day, the shared branch should still support:
- fake snapshots without KH2
- runtime attach without netcode
- a clean compile with unimplemented sections stubbed behind feature flags
