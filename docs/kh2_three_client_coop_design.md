# KH2 Three-Client Co-op Design (Working Draft)

## Goal
Build a true 3-client Kingdom Hearts II Final Mix co-op prototype where:
- each player runs their own KH2 client,
- each player has their own local camera,
- all three players share one authoritative world/battle/session,
- the roster can be either:
  - normal-party style (Sora + two party members), or
  - keyblade-trio style (Sora / Roxas / Riku or similar).

## Non-goals for MVP
- deterministic lockstep networking,
- full campaign support,
- split-screen,
- cross-version support,
- perfect parity for Drive/Summon/Limit/cutscene systems.

## Platform target
- Kingdom Hearts II Final Mix PC release
- OpenKH Mod Manager + Panacea + Lua Backend for setup/prototyping
- native runtime hook for the final hot path

## Chosen architecture
### 1) One authoritative host, three local cameras
- The host owns room state, enemy state, story flags, transitions, rewards, and save/session truth.
- Every client keeps its own local camera and HUD.
- Camera is never network-synced except for forced event/cutscene states.

### 2) Canonical slot simulation
Use one shared slot mapping across all machines:
- Slot 0 = PLAYER
- Slot 1 = FRIEND_1
- Slot 2 = FRIEND_2

Ownership is assigned per human:
- Player A owns Slot 0
- Player B owns Slot 1
- Player C owns Slot 2

Important: on every machine, the underlying slot identity stays the same. We do **not** change the simulation role per client.

Why:
- keeps all clients closer to the same world model,
- fits KH2's native PLAYER / FRIEND_1 / FRIEND_2 layout,
- reduces desync risk compared with making "self" be PLAYER on every machine.

### 3) Local presentation remap, not simulation remap
Each client locally binds:
- camera follow target -> owned slot
- lock-on target UI -> owned slot
- input injection -> owned slot
- local HUD -> owned slot

This gives each player a personal-feeling camera without rewriting the world simulation identity.

## Runtime components
### A. Asset mod package
Used for:
- roster definitions,
- member table changes,
- object entry edits,
- battle parameter edits,
- spawn/script edits,
- optional custom models/movesets/animations.

### B. Injected runtime hook (`kh2coop.dll`)
Used for:
- memory reads/writes,
- camera retargeting,
- input interception/injection,
- state capture,
- replica/authority mode switching,
- packet encode/decode,
- desync detection.

### C. Host server (`kh2coop-server.exe`)
Used for:
- lobby/session ownership,
- authoritative state relay,
- connect/reconnect,
- logging,
- optional spectator/admin tools.

## Game-side actor model
### Session actor table
Stable across the whole session:
- `ActorId 0` -> canonical slot PLAYER
- `ActorId 1` -> canonical slot FRIEND_1
- `ActorId 2` -> canonical slot FRIEND_2

Each actor also has:
- `Archetype` (Sora, Roxas, Riku, Donald, Goofy, etc.)
- `MovesetPackage`
- `ModelPackage`
- `AbilityLoadout`
- `EquipmentLoadout`
- `OwnerPeerId`

### Recommended MVP roster packs
#### Pack A: Vanilla-ish party
- PLAYER = Sora
- FRIEND_1 = Donald or Riku-friend package
- FRIEND_2 = Goofy or Roxas-friend package

#### Pack B: Keyblade trio
- PLAYER = Sora
- FRIEND_1 = Roxas friend-slot build
- FRIEND_2 = Riku friend-slot build

## Simulation modes
### Host mode
The host simulates:
- all three player slots,
- all enemies,
- all pickups/rewards,
- story/room progression,
- transitions/cutscene gates.

### Replica mode (non-host clients)
On non-host clients:
- remote player slots are replica-driven,
- enemy AI is either disabled or heavily overridden,
- damage/rewards are applied only from host-confirmed events,
- transforms/motion states are corrected from snapshots.

This is the single hardest technical area.

## Networking model
### Principle
- input travels client -> host
- state travels host -> all clients
- important events use reliable delivery
- transforms/snapshots use unordered or sequence-numbered delivery

### Client -> host packets
#### `InputFrame`
- `seq`
- `clientTime`
- `ownedActorId`
- `buttons`
- `leftStickX`, `leftStickY`
- `rightStickX`, `rightStickY`
- `lockOnRequest`
- `special1`, `special2`, `magicSlot`, `guard`, `dodge`, etc.

#### `TransitionAck`
- acknowledges room load / cutscene gate / resync step

#### `Heartbeat`
- latency and health tracking

### Host -> client packets
#### `SessionState`
- game build hash
- mod hash
- current world/room/program
- canonical roster
- story flag hash
- save/session id

#### `ActorSnapshot`
For each player actor:
- `actorId`
- `slotType`
- `archetype`
- `position`
- `rotation`
- `velocity`
- `motionId`
- `actionState`
- `comboStep`
- `hp/mp/drive/focus-like resources`
- `targetId`
- `invuln / stagger / airborne flags`

#### `EnemySnapshot`
- `enemyNetId`
- `objectId`
- `spawnGroupId`
- `position/rotation`
- `motionId`
- `aiState`
- `hp`
- `targetActorId`
- `alive/despawned`

#### `Event`
Reliable.
Examples:
- spawn enemy group
- kill enemy
- reward granted
- room transition begin
- room transition complete
- cutscene begin/end
- player KO
- revive
- force teleport / leash
- session resync required

## Why not lockstep?
KH2 has too many hidden timers, stateful battle interactions, story-scripted transitions, and role-dependent behaviors. Host-authoritative snapshots plus reliable events are safer than trying to make three clients simulate identically.

## Camera design
### Normal gameplay
Every client uses a custom camera follow target equal to its owned actor.

### Combat rules
- no camera sync between peers,
- local lock-on only affects local camera presentation,
- target selection request is still sent to host when target-dependent gameplay matters.

### Cutscenes / forced cameras
- host sends `CinematicStart`
- all clients temporarily release local camera override
- remote actors may be hidden, frozen, or snapped to canonical cutscene positions
- host sends `CinematicEnd`
- clients restore owned-slot camera

## HUD design
### MVP
- Player 1 may keep more of the vanilla command experience
- Player 2 / 3 get custom overlay controls:
  - HP/MP/Drive bars
  - lock-on status
  - revive prompt
  - mapped combat specials
  - cooldown indicators

### Long-term
A fully custom 3-player HUD would be better than forcing every player through the vanilla command stack.

## Combat control plan
### Slot 0 (PLAYER)
Can initially keep closer-to-vanilla behavior.

### Slot 1 / 2 (FRIENDs)
Use direct mapped controls:
- Attack
- Jump
- Guard
- Dodge
- Lock-on
- Magic 1
- Magic 2
- Team skill / Limit trigger

This avoids depending on the full PLAYER-only command stack for every human.

## Progression / save policy
### MVP
Host save is canonical.
Clients join the host session and do not attempt to merge full campaign saves.

Use a session manifest for:
- world
- room
- spawn program
- story hash
- roster
- loadouts

### Later
Add optional persistence for:
- levels
- equipment
- ability loadouts
- cosmetics

## Scope cuts for MVP
Disable or postpone:
- Drive forms
- Summons
- Limits beyond a tiny curated subset
- guest-party worlds
- lion / carpet / Atlantica forms
- minigames
- gummi travel
- save syncing
- open campaign joining at arbitrary progress

## First playable milestone
### "GoA Arena 3P"
Use Garden of Assemblage as the first real target.

Requirements:
- 3 clients connect to one host
- fixed room only
- fixed roster only
- movement / jump / attack / guard / dodge
- one enemy pack or one boss room
- host-authoritative enemy HP and KO handling
- own-camera on every client
- reconnect disabled
- no cutscenes

Success condition:
All three players can enter the same room, see each other, fight the same enemies, and finish one encounter without a fatal desync.

## Milestones
### M0 - Telemetry sandbox
- attach hook
- read actor transforms
- write camera follow target
- send actor transforms over network
- spawn remote ghost dummies

### M1 - Shared movement room
- 3 clients in one room
- owned-slot camera override
- actor interpolation
- input path client -> host -> all

### M2 - Shared combat room
- enemy snapshots
- host-authoritative damage
- KO/revive
- reward sync for one encounter

### M3 - Friend-slot playability
- direct controls for FRIEND_1 / FRIEND_2
- custom HUD overlay
- keyblade-friend archetypes

### M4 - Selected room transitions
- transition handshake
- room load ack
- spawn reconciliation
- resync after failed load

### M5 - Limited story co-op
- host-owned story flags
- selected worlds only
- cutscene gating and skip policy

## Biggest technical risks
1. Friend-slot control and camera override may fight vanilla systems.
2. Non-host enemy replica mode may still diverge unless AI is suppressed hard.
3. Spawn limits / actor swaps can crash maps.
4. Cutscenes and story flags can desync clients immediately.
5. Mods/version mismatch between players will cause invalid state.

## Immediate next tasks
1. Prove camera follow retarget for FRIEND_1 and FRIEND_2 on separate clients.
2. Prove client-owned input injection into friend slots.
3. Prove host-authoritative transform replication for 3 actors in one room.
4. Freeze or override non-host enemy AI cleanly.
5. Build the GoA Arena 3P vertical slice.
