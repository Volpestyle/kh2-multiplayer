# KH2 Multiplayer Review + Scope Expansion Plan

Date: 2026-03-25

## 1) Current project assessment

### Overall judgment
The repo is in **better shape than a typical 'couple days' prototype**. It has crossed out of pure ideation and into a meaningful split between:

- shared protocol / transport,
- server session logic,
- runtime bridge scaffolding,
- live reverse-engineering notes,
- milestone docs.

That is the right skeleton.

### What is genuinely solid already

#### A. The network foundation is real
`common/` + `server/` are not throwaway placeholders. The protocol, codec, session host, slot assignment, version gating, and fake simulation harness are all coherent. The fake sim is especially useful because it proves your packet and session assumptions before the KH2 process is involved.

This is the best part of the repo because it lowers risk later.

#### B. The reverse-engineering work is materially useful
`docs/pointer_map_v1.md`, `docs/CAMERA_RE_SESSION.md`, and `docs/probes/archive/POSITION_PROBE_SESSION3.md` show real progress, not vague notes. The important win is that you now have:

- room/world/program reads,
- slot-0 transform discovery,
- confirmed writable transform path,
- confirmed camera-follow pointer path,
- a working fake-actor retarget strategy.

That is enough to say Milestone 1 and 2 are **partially real**, not aspirational.

#### C. `GameBridgePC` is farther along than the README claims
The README still describes runtime as scaffold-only, but `runtime/src/GameBridgePC.cpp` already does meaningful work:

- attaches to the KH2 process,
- resolves module base,
- discovers slot-0 entity addresses,
- reads room state,
- reads slot-0 position/rotation/HP,
- retargets camera using fake actor memory,
- applies replica state back into the live process for slot 0,
- dual-writes entity position + buffer position.

That is an important mismatch to fix in docs because new agents will undersell or misunderstand current progress.

#### D. The repo structure is still healthy
The repo is not yet overfit to one bad assumption. That matters. Right now you still have room to support two distinct products on one base:

1. **Campaign co-op** (3-player shared party)
2. **Public realm / MMO-style mode** (many characters, parties, public hubs, instanced combat)

That flexibility is valuable.

## 2) Current weak points / immediate cleanup

### 1. Documentation drift
The biggest visible issue is drift between docs and code.

Examples:
- `README.md` says there is no concrete `IGameBridge` implementation yet.
- `runtime/README.md` still frames the bridge like a future task.
- the code already contains a meaningful Windows implementation.

Fix this first, because stale docs will waste RE time.

### 2. The test harness is strong, but build verification is brittle
The project fetches ENet from GitHub in CMake. That is fine for normal development, but it means cold/offline builds are fragile. For a modding project where people may repeatedly clone / rebuild on constrained environments, consider one of:

- vendoring ENet,
- pinning a submodule,
- or adding an alternate local dependency path.

### 3. The current simulation model is useful but dangerous if it becomes "the game"
`SimulationState` is good as a network harness. But do not let it silently become the long-term gameplay authority model for co-op.

For KH2 co-op, the real authority should eventually be **the host KH2 process**, not the simplified fake physics model.

Use the current simulation only for:
- protocol tests,
- network regression tests,
- headless development before live memory integration.

### 4. Slot assumptions are still very co-op-specific
A lot of the current shape assumes exactly 3 canonical human slots:
- `Player`
- `Friend1`
- `Friend2`

That is correct for **campaign co-op**, but it will not scale to public-multiplayer mode. That does not mean it is wrong. It means you should explicitly treat it as **Mode A architecture**, not the universal actor model.

## 3) My honest read on milestone status

### More advanced than expected
- M0 Environment/observability: mostly there, though build lock artifact still needs to be formalized.
- M1 Pointer map: significantly progressed.
- M2 Camera retarget: meaningfully progressed.
- M4 Networking/session backbone: significantly progressed in fake-sim form.

### Still the real blockers
- Friend1/Friend2 entity discovery
- friend-slot local input injection
- native remote avatar spawning / replication beyond slot 0
- enemy list discovery and enemy authority
- room transition protocol tied to live KH2 state

These are still the gateway tasks.

## 4) Scope expansion: how to think about the "MMO-style" idea

The big recommendation is:

**Do not treat 3-player campaign co-op and public MMO-style multiplayer as one linear feature toggle. Treat them as two runtime modes sharing infrastructure.**

They can share:
- transport,
- protocol primitives,
- identity/account layer,
- persistence layer,
- instance registry,
- content hashing/version checks,
- logging and replay systems.

But they should not share the exact same in-room actor model.

## 5) The two products

### Product A — Campaign Co-op
Goal:
- up to 3 players,
- same party,
- shared story,
- host-authoritative,
- one room timeline,
- one save owner / story owner.

Best actor model:
- canonical slots (`PLAYER`, `FRIEND_1`, `FRIEND_2`)
- one authoritative story state
- one authoritative room
- tight engine coupling

This is the mode your current repo already naturally fits.

### Product B — Public Realm / MMO-style Mode
Goal:
- public joinable server,
- each player has their own character,
- players may roam separately,
- parties form and dissolve dynamically,
- optional PvP,
- persistent character/world services.

Best actor model:
- **local player always gets local primary control pipeline** on their own client,
- other players are remote replicas / custom spawned actors,
- rooms are instanced/sharded,
- realm server persists state and routes players into instances.

This is **not** the same as the canonical 3-slot co-op architecture.

## 6) The critical architectural truth

A single host KH2 save file cannot literally function as a full persistent MMO simulation of many independent rooms unless you build a much larger server-side simulation system.

So the practical interpretation of:
> take a save file and turn it into an open server

should be:

### `Save file -> World Seed`
The save file becomes a **realm seed** that defines:
- unlocked worlds,
- unlocked traversal points,
- story flags,
- boss clear flags,
- optional chest/bonus state,
- optional item pool state,
- optional hub unlock state.

Then the public server uses that as metadata for what content is open.

It should **not** initially mean:
- the server is literally simulating the whole campaign globally in real time.

That distinction keeps the idea achievable.

## 7) Recommended expanded architecture

### Layer 1 — Shared platform layer
Keep and expand the current `common/` package into a stable base for both modes.

Add concepts like:
- `ProtocolVersion`
- `BuildHash`
- `ContentHash`
- `RealmId`
- `InstanceId`
- `ActorNetId`
- `CharacterId`
- `PartyId`
- `ArchetypeId`

### Layer 2 — Realm service (public server)
A lightweight persistent service that owns:
- account/session auth,
- character roster,
- party membership,
- world seed metadata,
- active instances registry,
- chat / presence,
- PvP queue / duel queue,
- persistence checkpoints.

This service is **not** the real-time KH2 battle simulator.

### Layer 3 — Instance service
This is where actual gameplay authority lives per room or per encounter.

Each active instance owns:
- room/world/program identity,
- player roster in that instance,
- current actor states,
- enemy roster,
- event queue,
- spawn state,
- transition rules.

Two realistic versions:

#### Version 1: client-hosted instances
One player client is the authority for the current instance.
Pros:
- achievable sooner,
- compatible with current co-op architecture,
- no need to reimplement KH2 combat server-side.
Cons:
- cheating risk,
- worse for PvP,
- host migration complexity.

#### Version 2: dedicated instance workers
A dedicated worker process or hidden/headless game worker owns the room.
Pros:
- better persistence / fairness,
- better for public servers and PvP.
Cons:
- much harder,
- probably a separate major project.

My recommendation:
- **Campaign co-op:** client-hosted only.
- **Public realm v1:** realm service + client-hosted instances.
- **PvP or truly public competitive play:** delay until dedicated authority exists.

## 8) Instance types to support

Define instance types explicitly.

### `CampaignPartyInstance`
- max 3 human players
- shared story progression
- canonical party slots
- host save is source of truth

### `PublicHubInstance`
- non-story safe zone
- larger player cap than co-op
- combat off or heavily limited at first
- remote avatars only
- chat / party formation / matchmaking

### `AdventureInstance`
- a public-mode combat room or zone
- one solo player or one formed party enters
- may branch from the realm seed but remain instance-local
- rewards and character progression persist back to the realm

### `PvpArenaInstance`
- completely separate rule set
- curated room list
- explicit opt-in
- no campaign story coupling

This is the cleanest way to grow scope without wrecking the co-op mode.

## 9) Actor model: co-op vs public mode

### Campaign co-op actor mapping
Use the current design:
- P1 -> `PLAYER`
- P2 -> `FRIEND_1`
- P3 -> `FRIEND_2`

This is still the right choice for the first playable build.

### Public mode actor mapping
Do **not** force every human into the three canonical party slots.

Instead:
- local human = local primary avatar
- local AI companions = optional, late feature
- remote humans = network replicas / custom actors / spawned battle NPC style actors
- remote players do not need full local command menu integration

This means the MMO/public mode needs a more general actor abstraction like:

```text
NativeRole:
- LocalPrimary
- LocalCompanion1
- LocalCompanion2
- RemoteReplica
- RemoteEnemy
- RemoteNpc
```

That abstraction should sit above the KH2-native slot system.

## 10) Parties in public mode

You mentioned players maintaining their own parties and also joining each other.

That is a cool idea, but there is an important design constraint:

**Do not allow full duplicate AI parties for every player in the same room in v1.**

Why:
- visual clutter,
- native spawn limits,
- duplicated Donald/Goofy problems,
- command/menu ambiguity,
- harder targeting and network load.

Recommended rule set for public mode v1:
- each human enters as their main character only,
- solo players may optionally have limited AI companions when alone,
- when a human joins a party instance, human players replace party slots,
- AI companions are reduced or disabled in multi-human instances.

That will feel much cleaner and be far more achievable.

## 11) PvP reality check

PvP is possible in principle, but it should be treated as a **late separate vertical slice**, not an automatic extension of co-op.

Why it is hard:
- KH2's combat assumptions are asymmetric between player and enemies,
- hit reactions / lock-on / hurtboxes are tuned for enemy actors,
- latency tolerance is lower,
- cheating matters much more,
- public-host authority is bad for competitive fairness.

Recommended PvP path:
1. no open-world PvP at first,
2. add duel requests in hub,
3. launch into `PvpArenaInstance`,
4. use curated kits / archetypes / room list,
5. eventually move PvP to stronger authority.

## 12) Save file import design

If you want "turn a save file into a server", define an explicit importer.

### `RealmSeedImporter`
Input:
- KH2 save file
- build hash
- content hash

Output:
- `RealmSeed` record:
  - seed id
  - source save checksum
  - unlocked world graph
  - accessible room set / world points
  - story flags
  - cleared bosses / gates
  - optional item/chest state

Then all public instances refer to `RealmSeed` as their world baseline.

That is much better than making the save file itself the live world state.

## 13) Persistence model

Separate world persistence from character persistence.

### Character persistence
Per character:
- level / stats
- equipment
- learned abilities
- selected archetype / outfit
- cosmetics
- currency
- quest flags (if you go there)

### Realm persistence
Per realm:
- seed info from imported save
- public unlock state
- hub state
- global events (optional)
- public vendor / unlock config

### Instance persistence
Ephemeral:
- current room
- current enemy set
- current players
- temporary combat state

Write back at:
- room completion,
- checkpoint,
- instance exit,
- safe periodic autosave.

## 14) What to refactor now so the repo can grow cleanly

### A. Separate `Session` from `Instance`
Right now the session host is implicitly both lobby and gameplay room.

Refactor toward:
- `RealmSession` / `PartySession` (who is grouped with whom)
- `InstanceRuntime` (which room is actually active)

That will pay off immediately.

### B. Introduce stable IDs now
Do not rely on `SlotType` as the durable identity of a player.

Add:
- `PeerId`
- `CharacterId`
- `ActorNetId`
- `InstanceId`

Then `SlotType` becomes just a **mapping inside a specific co-op instance**.

### C. Split runtime modes early
Add an explicit runtime mode enum:
- `CampaignCoop`
- `PublicRealm`

The code paths should diverge before actor mapping does.

### D. Define `AvatarArchetype`
This is how you support:
- Sora,
- Roxas,
- Riku,
- custom keyblade users,
- future enemy/NPC-derived avatars.

Character persistence should point at an archetype id, not hardcoded slot lore.

## 15) Proposed milestone tree after current co-op work

### Track A — Finish current co-op prototype
A1. Friend1/Friend2 entity discovery  
A2. Friend-slot local input injection  
A3. Native remote avatar / actor spawn  
A4. Enemy list discovery + shared combat  
A5. controlled transitions  
A6. GoA Arena 3P playtest gate  

### Track B — Refactor for scale
B1. introduce stable ids (`CharacterId`, `ActorNetId`, `InstanceId`)  
B2. split lobby/session from gameplay instance  
B3. add persistence layer interfaces  
B4. add runtime mode enum  

### Track C — Public realm v1
C1. realm server with login/character roster  
C2. save-file -> realm-seed importer  
C3. public hub instance (safe zone, no combat first)  
C4. party forming / joining / follow / warp  
C5. party-created adventure instances  
C6. persist character rewards back to realm  

### Track D — PvP / advanced multiplayer
D1. duel request flow  
D2. instanced arena prototype  
D3. server-validated hit/damage events  
D4. anti-cheat / stronger authority  

## 16) Recommendation on sequencing

My strongest recommendation:

### Do not switch the current repo's main goal away from 3-player co-op yet.
Instead:
- finish the live co-op vertical slice,
- refactor the protocol/runtime IDs so they can scale,
- then branch into public-realm work.

That gives you a playable product *and* keeps the MMO path alive.

If you pivot immediately into the full public-world idea before you have:
- real multi-actor replication,
- enemy authority,
- transitions,
- stable room sync,

you risk turning a promising project into a perpetual architecture exercise.

## 17) Concrete next tasks I would schedule right now

### Immediate cleanup sprint
1. update README + runtime docs to match current code reality
2. add `build_lock.md` with exact target build and mod hash
3. add `docs/ARCHITECTURE_MODES.md` describing Co-op vs PublicRealm
4. add stable ID types to protocol layer

### Next live-engine sprint
5. discover Friend1/Friend2 entity bases
6. confirm whether friend slots can be camera-followed natively or need fake actor strategy for now
7. find input injection path for non-player slots
8. find enemy list root/count/stride

### Pre-MMO architecture sprint
9. add `RealmSeed` schema
10. add `CharacterRecord` schema
11. add `InstanceId` / `PartyId` / `ActorNetId`
12. define `CampaignPartyInstance` vs `PublicHubInstance`

## 18) Bottom line

Your progress is real.

The project has already moved beyond concept work in three important ways:
- a real packet/session backbone exists,
- meaningful KH2 memory/camera work exists,
- the repo structure is still flexible enough to support a much bigger future.

The expanded "MMO-style" vision is plausible **if you reinterpret it correctly**:
- not one giant always-simulated KH2 universe,
- but a **realm server + public hubs + party/adventure instances + persistent characters**.

That version is ambitious, but technically coherent.

And most importantly: it can grow naturally out of the co-op work you are already doing.
