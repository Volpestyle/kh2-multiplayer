# Agent Assignments

## Agent 1 — Runtime / Reverse Engineering
**Owns:** live memory mapping and the first usable `IGameBridge`.

### Responsibilities
- find actor structures for slot 0/1/2
- find camera target state and override point
- find input read / consume path for friend slots
- find room/world/transition/cutscene state
- find enemy list root and per-enemy state
- publish `pointer_map_v1.md`

### Must hand off
- symbol / offset notes
- a stable set of getters:
  - `ReadActorState(slot)`
  - `ReadEnemyStates()`
  - `ReadRoomState()`
  - `ReadCameraState()`
  - `WriteCameraTarget(slot)`
  - `InjectOwnedInput(slot, input)`

### Success looks like
The rest of the team can write against `IGameBridge` without doing live memory discovery themselves.

---

## Agent 2 — Networking / Host Server
**Owns:** session authority, packet schemas, relay logic, version gating.

### Responsibilities
- server bootstrap
- session join/leave/reconnect
- build-hash and mod-hash enforcement
- packet sequencing / acks / reliable events
- snapshot cadence
- packet logging for replay/debug

### Must hand off
- `Protocol.hpp`
- `SessionHost`
- test harness that replays fake `InputFrame`s

### Success looks like
A fake simulation can run without KH2 attached.

---

## Agent 3 — Client Replica / Camera / HUD
**Owns:** everything the player actually feels on the non-host clients.

### Responsibilities
- local camera follow override
- smoothing and correction of remote actors
- P2/P3 overlay HUD
- debug overlay for ownership / ping / desync
- cutscene-safe camera disable / restore

### Must hand off
- `CameraController`
- `ReplicaController`
- `OverlayState`

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

## Integration order
1. Agent 1 and Agent 2 work first.
2. Agent 3 starts as soon as camera write + packet feed exist.
3. Agent 4 should avoid expanding scope until Milestone 5 is stable.

## Daily integration contract
At the end of each day, the shared branch should still support:
- fake snapshots without KH2
- runtime attach without netcode
- a clean compile with unimplemented sections stubbed behind feature flags
