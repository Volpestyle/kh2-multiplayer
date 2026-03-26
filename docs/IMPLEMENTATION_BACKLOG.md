# Implementation Backlog

## Milestone 0 — Environment lock + observability `[DONE]`
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

## Milestone 1 — Pointer map / game bridge `[PARTIAL]`
**Goal:** identify the minimum live data needed to observe the three-player session.
**Status:** All 3 actor transforms (Slot 0/1/2) fully mapped — position, rotation, velocity, airborne. HP mapped for all slots. Room state and camera done. Enemy entity struct layout confirmed (stride, moveState) but root pointer and count still missing. MP offset unknown.

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

## Milestone 2 — Local camera retarget `[PARTIAL]`
**Goal:** each machine can follow its owned actor instead of always following slot 0.
**Status:** `WriteCameraTarget()` and `RestoreVanillaCamera()` are implemented via fake actor allocation + pointer redirect. Camera retarget verified live. The runtime scaffold now loads `kh2coop_runtime.ini`, drives `CameraController`, and exposes an `F8` panic toggle, but stronger transition/cutscene gating and live Friend1/2 validation are still pending.

### Tasks
- Add runtime config: owned actor id.
- Override camera follow target to owned slot.
- Keep forced cinematic / cutscene camera paths untouched.
- Add panic hotkey to disable custom camera override.

### Deliverables
- `CameraController` wired into the runtime scaffold
- local config file: `kh2coop_runtime.ini` with `client_role = 0|1|2`

### Exit criteria
- Client A follows slot 0.
- Client B follows slot 1.
- Client C follows slot 2.
- Cutscene/event camera still takes control when expected.

---

## Milestone 3 — Local input injection `[TODO]`
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

## Milestone 4 — Host-authoritative player replication `[PARTIAL]`
**Goal:** 3 clients can share a room and see each other in motion.
**Status:** Server lobby, session state, ENet client/server, `SimulationState`, snapshot ordering guards, and stale-peer timeout handling are all implemented and tested (3-client integration test passes). `ApplyReplicaActorState()` implemented for slot 0 position writes. Not yet tested end-to-end with live KH2.

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

## Milestone 5 — Enemy replication + shared damage `[TODO]`
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

## Milestone 6 — Minimal UX polish `[TODO]`
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

## Milestone 7 — Controlled transitions `[TODO]`
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

## Milestone 8 — Content expansion `[TODO]`
**Goal:** make the prototype resemble the intended fantasy.

### Tasks
- Add fixed trio roster pack (Sora / Roxas / Riku).
- Add friend-slot movesets with direct mapped specials.
- Add limited revive / KO flow.
- Add curated arena encounters.

### Exit criteria
- The vertical slice feels like real co-op combat, not only a networking demo.

## Things to postpone (Track A)
- full campaign
- free-roam world divergence
- deterministic lockstep
- drive forms, summons, limits, and every command-menu feature
- netplay across mixed game versions

---
---

# Track B — Refactor for Scale

Prepare the codebase to support both CampaignCoop and PublicRealm modes.
Do this **after** Track A reaches a stable 3-player vertical slice, but **before** starting Track C.

See `docs/ARCHITECTURE_MODES.md` for the full mode breakdown and `common/include/kh2coop/Types.hpp` for the type definitions.

---

## B1 — Introduce stable identity types `[DONE]`
**Goal:** decouple durable player/actor identity from the co-op-specific `SlotType`.

### Tasks
- [x] Add `PeerId`, `CharacterId`, `ActorNetId`, `InstanceId`, `PartyId`, `RealmId` type aliases to `Types.hpp`
- [x] Add `RuntimeMode`, `InstanceType`, `AuthorityType`, `NativeRole` enums to `Types.hpp`
- [ ] Migrate `SessionActor::ownerPeerId` from raw `std::string` to `PeerId` alias
- [ ] Add `ActorNetId` to `ActorState` as a separate field from `actorId` (currently overloaded)

### Follow-up: strong wrapper types
The current IDs are type aliases (`using PeerId = std::string`). This gives naming clarity but no compile-time safety against mixing up a `PeerId` with a `CharacterId`. Once the IDs are threaded through codec, state containers, and logs, promote them to strong wrapper types:
```cpp
struct PeerId { std::string value; bool operator==(const PeerId&) const = default; };
```
This is a follow-up to B1, not a blocker.

### Exit criteria
- `SlotType` is only used as a mapping inside co-op instances, not as durable identity.
- All new protocol records reference stable ID types.

---

## B2 — Kill handshake overload + split session from instance `[TODO]`
**Goal:** separate lobby/party ownership from active gameplay instance. Fix the overloaded handshake.

This is the **highest-priority Track B task** — it's the bridge between the current co-op prototype and the two-product platform.

### Tasks
- [ ] Implement `ClientHello` codec (encode/decode) — struct already declared in `Protocol.hpp`
- [ ] Implement `ClientHelloAck` / `ServerHello` response
- [ ] Replace the current `SessionState`-as-handshake pattern in `NetworkClient::connect()` and `SessionHost::onReceive()`
- [ ] Stop overloading `sessionId` as the peer name — use `ClientHello::peerId` and `ClientHello::peerName`
- [ ] Add `requestedMode` to wire handshake so the server knows what mode the client wants
- [ ] Add `RuntimeMode` / `ProductMode` to `ServerMain` config (`--mode` CLI arg)
- [ ] Refactor `SessionHost` into `PartySession` (who is grouped) and `InstanceRuntime` (which room is active)
- [ ] Implement or remove `TransitionAck` codec — currently declared as `PacketType::TransitionAck = 2` but has no encode/decode implementation

### Deliverables
- `ClientHello` / `ClientHelloAck` on the wire
- Server knows requested mode from handshake
- `TransitionAck` either works or is removed from the enum
- Clear separation between "who is in my party" and "what room are we in"

### Exit criteria
- A session can exist without an active instance (lobby state).
- Instance creation/destruction is an explicit operation.
- All existing CampaignCoop tests still pass (Track B regression gate).

---

## B3 — Add persistence layer interfaces `[TODO]`
**Goal:** define the data contracts for character and realm persistence without implementing storage.

### Tasks
- [ ] Define `CharacterRecord` load/save interface
- [ ] Define `RealmSeed` import interface
- [ ] Add in-memory stub implementations for testing

### Deliverables
- `ICharacterStore` interface
- `IRealmStore` interface
- In-memory implementations for test harness

### Exit criteria
- Public-realm code can be written against the interfaces without a real database.

---

## B4 — Add runtime mode to config and startup `[DONE]`
**Goal:** the runtime and server know which mode they are operating in.

### Tasks
- [x] Add `RuntimeMode` enum to `Types.hpp`
- [x] Add `runtime_mode` to INI config and CLI args
- [ ] Gate mode-specific code paths behind runtime mode checks
- [ ] Server startup logs the active mode

### Exit criteria
- `--mode campaign_coop` and `--mode public_realm` are recognized at startup.
- Code paths that only apply to one mode are clearly gated.

---
---

# Track C — Public Realm v1

Build the public-realm product on top of the shared infrastructure. Requires Track B to be complete.

See `docs/kh2_multiplayer_scope_expansion_review.md` sections 5-13 and `docs/kh2_realm_protocol_sketch.jsonc`.

---

## C1 — Realm server with login and character roster `[TODO]`
**Goal:** persistent service that manages accounts and characters.

### Tasks
- [ ] Implement `RealmService` with login/logout
- [ ] Implement character creation, roster listing, character selection
- [ ] Implement session auth token flow
- [ ] Add `CharacterRecord` persistence (in-memory first, then file/DB)

### Exit criteria
- A client can log in, create a character, and select it.
- Character persists across reconnects.

---

## C2 — Save-file to realm-seed importer `[TODO]`
**Goal:** turn a KH2 save file into a `RealmSeed` that defines accessible content.

### Tasks
- [ ] Parse KH2 save file structure (reference: OpenKH `SaveDataFinalMix.cs`)
- [ ] Extract unlocked worlds, warp points, story flags, cleared bosses
- [ ] Produce `RealmSeed` record
- [ ] Store realm seed in realm service

### Exit criteria
- A save file can be imported, and the resulting `RealmSeed` correctly reflects the save's progression.

---

## C3 — Public hub instance `[TODO]`
**Goal:** safe-zone room where multiple players can see each other without combat.

### Tasks
- [ ] Implement `PublicHubInstance` type
- [ ] Spawn remote players as `RemoteReplica` actors
- [ ] Replicate position/rotation/animation for all visible players
- [ ] No combat, no enemy spawns

### Exit criteria
- Multiple clients can join a hub room and see each other moving.
- No crashes or fatal desyncs in 5 minutes of testing.

---

## C4 — Party forming, joining, follow, warp `[TODO]`
**Goal:** players can form parties in hubs and move together.

### Tasks
- [ ] Implement party create/invite/join/leave messages
- [ ] Implement party follow (auto-warp party members)
- [ ] Implement party HUD (member list, status)

### Exit criteria
- Two players can form a party in a hub and follow each other.

---

## C5 — Party-created adventure instances `[TODO]`
**Goal:** a formed party can enter a combat room together.

### Tasks
- [ ] Implement `AdventureInstance` creation from a party
- [ ] Map party members to instance actor bindings
- [ ] Instance authority handles enemy spawns, combat, rewards

### Exit criteria
- A party enters an adventure instance and completes one encounter together.

---

## C6 — Persist character rewards back to realm `[TODO]`
**Goal:** experience, items, and progression from adventure instances write back to the character record.

### Tasks
- [ ] Define reward writeback messages
- [ ] Implement instance-exit persistence checkpoint
- [ ] Validate no duplicate rewards

### Exit criteria
- A character's level/equipment changes persist after leaving an adventure instance.

---
---

# Track D — PvP and Advanced Multiplayer

Separate vertical slice for competitive play. Requires stable instance architecture from Track C.

See `docs/kh2_multiplayer_scope_expansion_review.md` section 11.

---

## D1 — Duel request flow `[TODO]`
- Implement duel challenge/accept/decline in hubs
- Launch into `PvpArenaInstance` on accept

## D2 — Instanced arena prototype `[TODO]`
- Curated room list for arenas
- Fixed kits/archetypes (no arbitrary loadouts in v1)
- Round-based or timed match format

## D3 — Server-validated hit/damage events `[TODO]`
- Move damage calculation to instance authority
- Clients submit attack events, authority confirms/rejects
- Prevents basic client-side damage cheating

## D4 — Anti-cheat and stronger authority `[TODO]`
- Dedicated instance workers for PvP
- State validation and anomaly detection
- Report/ban system

---

## Sequencing priority

**Current focus: Track A (finish co-op prototype).** Do not switch to Track C/D until Track A reaches a playable vertical slice with 3-player combat in a fixed room.

Track B refactors should be done opportunistically as Track A work stabilizes — the type definitions and runtime mode are already in place (B1 partial, B4 partial).
