# Implementation Backlog

## Milestone 0 — Environment lock + observability
**Goal:** make the build deterministic and make the runtime inspectable.

### Tasks
- Freeze one KH2 PC build and publish its executable hash.
- Freeze one mod package hash.
- Stand up injected runtime loading with structured logging.
- Add a "session banner" log line with build hash, mod hash, world id, room id.
- Confirm the runtime can tick every frame without crashing.

### Deliverables
- `build_lock.md`
- `runtime_boot.log`
- `IGameBridge` stub linked into runtime
- one command-line switch for verbose memory logging

### Exit criteria
- Runtime boots reliably.
- Runtime prints current world/room each frame or on change.
- All clients can prove they are on the same exact build/mod pair.

---

## Milestone 1 — Pointer map / game bridge
**Goal:** identify the minimum live data needed to observe the three-player session.

### Tasks
- Locate the canonical actor records for `PLAYER`, `FRIEND_1`, `FRIEND_2`.
- Locate HP/MP/position/rotation/motion for those slots.
- Locate camera target and camera mode/state.
- Locate world id, room id, transition state, and cutscene state.
- Locate root enemy list / enemy actor array.

### Deliverables
- `pointer_map_v1.md`
- `IGameBridge` methods returning real values for the above
- frame logger dumping actor transforms and room info

### Exit criteria
- On a single client, logs show stable transforms for all three slots.
- Camera target changes can be observed.
- Enemy list length can be read in a test room.

---

## Milestone 2 — Local camera retarget
**Goal:** each machine can follow its owned actor instead of always following slot 0.

### Tasks
- Add runtime config: owned actor id.
- Override camera follow target to owned slot.
- Keep forced cinematic / cutscene camera paths untouched.
- Add panic hotkey to disable custom camera override.

### Deliverables
- `CameraController` wired into runtime
- local config file: `client_role = 0|1|2`

### Exit criteria
- Client A follows slot 0.
- Client B follows slot 1.
- Client C follows slot 2.
- Cutscene/event camera still takes control when expected.

---

## Milestone 3 — Local input injection
**Goal:** non-slot-0 players can directly control their own actor locally.

### Tasks
- Capture raw pad/keyboard state before KH2 consumes it.
- Inject direct movement + jump + attack + guard + dodge into owned slot.
- For slot 1/2, do not attempt full vanilla command menu parity yet.
- Add debug overlay showing the resolved input state for the owned actor.

### Deliverables
- `InputInjector`
- control mapping file
- on-screen debug overlay

### Exit criteria
- In an offline test build, slot 1 and slot 2 can move and attack on demand.
- No accidental cross-control of the wrong slot.

---

## Milestone 4 — Host-authoritative player replication
**Goal:** 3 clients can share a room and see each other in motion.

### Tasks
- Implement server lobby/session state.
- Implement `InputFrame`, `SessionState`, and `ActorSnapshot`.
- Host simulates authoritative state for all three actor slots.
- Replicas smooth remote transforms but hard-correct on large divergence.
- Enforce build hash + mod hash match before session start.

### Deliverables
- `kh2coop-server`
- runtime networking client
- snapshot log / packet trace

### Exit criteria
- Three clients connect.
- All three clients see the same three player actors in one room.
- Positional error stays within a small tolerance after 60 seconds of movement.

---

## Milestone 5 — Enemy replication + shared damage
**Goal:** one room of common enemies can be fought cooperatively.

### Tasks
- Host owns enemy spawn/AI/hit results/kill rewards.
- Replicas display host-confirmed enemy state.
- Implement reliable `Event` messages for spawn, kill, KO, revive, reward.
- Disable or neuter local-only enemy authority on non-host clients.

### Deliverables
- `EnemySnapshot`
- reliable event channel
- shared kill confirmation and reward tests

### Exit criteria
- Three clients can kill the same enemy pack.
- Enemy HP and death timing stay consistent across all peers.
- Rewards do not duplicate.

---

## Milestone 6 — Minimal UX polish
**Goal:** the prototype is playable enough to evaluate, not just technically alive.

### Tasks
- Add P2/P3 overlay HUD.
- Add leash/teleport for actors too far from the host truth.
- Add reconnect / resync flow.
- Add clear on-screen "host authoritative" state indicators.

### Deliverables
- overlay HUD
- resync dialog / banner
- log capture bundle for desync reports

### Exit criteria
- 10-minute GoA run without fatal desync.
- A reconnecting peer can rejoin without restarting the whole lobby.

---

## Milestone 7 — Controlled transitions
**Goal:** move beyond one fixed room without opening the entire campaign.

### Tasks
- Host begins transition and sends transition id.
- Clients freeze input, ack readiness, load the same destination.
- Clients verify world/room/program hash after load.
- Resync actor placement on arrival.

### Deliverables
- `TransitionBegin` / `TransitionAck` / `TransitionComplete`
- room hash verification logs

### Exit criteria
- Selected GoA adjacent rooms can be traversed together.
- No client is left in the wrong room after 20 repeated transitions.

---

## Milestone 8 — Content expansion
**Goal:** make the prototype resemble the intended fantasy.

### Tasks
- Add fixed trio roster pack (Sora / Roxas / Riku).
- Add friend-slot movesets with direct mapped specials.
- Add limited revive / KO flow.
- Add curated arena encounters.

### Exit criteria
- The vertical slice feels like real co-op combat, not only a networking demo.

## Things to postpone
- full campaign
- free-roam world divergence
- deterministic lockstep
- drive forms, summons, limits, and every command-menu feature
- netplay across mixed game versions
