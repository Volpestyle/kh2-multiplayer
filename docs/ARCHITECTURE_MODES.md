# Architecture Modes: CampaignCoop vs PublicRealm

This project supports two runtime modes sharing one infrastructure. This document defines the boundary between them.

## Shared infrastructure (both modes)

| Layer | Location | Notes |
|-------|----------|-------|
| Transport | `common/NetworkClient` | ENet UDP, 2 channels (reliable + unreliable) |
| Binary codec | `common/Codec` | Framed packets, little-endian, 3-byte header |
| Identity types | `common/Types.hpp` | `PeerId`, `CharacterId`, `ActorNetId`, `InstanceId`, `PartyId`, `RealmId` |
| Version/content gating | `SessionHost` | Build hash + mod hash + content hash enforcement |
| Actor state structs | `common/Types.hpp` | `ActorState`, `EnemyState`, `RoomState` |
| Game bridge | `runtime/GameBridgePC` | Memory read/write, entity discovery, camera retarget |
| Camera controller | `runtime/CameraController` | Per-slot follow with cutscene safety |
| Replica controller | `runtime/ReplicaController` | Snapshot dedup and application |

---

## Mode A: CampaignCoop

**Goal:** Up to 3 players share one host-authoritative party, same story, same room timeline.

### Actor model
- Canonical slots: `PLAYER` (0), `FRIEND_1` (1), `FRIEND_2` (2)
- Each human owns exactly one slot for the session
- `SlotType` is the primary identity inside the instance
- `ActorNetId` assigned as `static_cast<uint32_t>(slot)` in v1

### Authority
- Host KH2 process owns room state, enemy state, story flags, transitions, rewards
- Host save file is the session truth
- Non-host clients are replicas: remote actors are snapshot-driven, local AI suppressed

### Session shape
- One `SessionHost` per session
- One `CampaignPartyInstance` active at a time
- Instance lifetime = session lifetime (no instance switching in v1)

### Native role mapping
| Human | NativeRole | KH2 slot |
|-------|-----------|----------|
| Player A | `LocalPrimary` or `LocalCompanion1/2` depending on owned slot | Slot they own |
| Remote peers | Replica-driven via `ApplyReplicaActorState` | Their owned slot |

### Config
```ini
runtime_mode = campaign_coop
client_role = 0  # or 1, 2
```

---

## Mode B: PublicRealm (planned)

**Goal:** Persistent characters, public joinable servers, party-formed adventure instances, optional PvP.

### Actor model
- Local player = `LocalPrimary` (always controls their own character locally)
- Other humans = `RemoteReplica` (custom-spawned actors, not forced into co-op party slots)
- No forced duplicate AI parties per player in v1
- `ActorNetId` is a server-assigned unique ID, not tied to `SlotType`

### Authority
- **Realm service** owns: login, character roster, party membership, instance registry, realm seed metadata, chat/presence
- **Instance authority** owns: player transforms, enemy state, spawn events, room transitions, reward writeback
- V1: client-hosted instances. V2 (future): dedicated instance workers for PvP/competitive.

### Session shape
- `RealmService` manages the persistent world layer
- Instances are created/destroyed dynamically: `PublicHubInstance`, `AdventureInstance`, `PvpArenaInstance`
- Players may be in different instances simultaneously (e.g., one in hub, another in adventure)
- Parties form/dissolve in hubs, then enter instances together

### Instance types

| Type | Description | Combat | Max players |
|------|-------------|--------|-------------|
| `CampaignPartyInstance` | Shared story, 3-slot party | Yes | 3 |
| `PublicHubInstance` | Safe zone, no combat (v1) | No | Higher cap |
| `AdventureInstance` | Party combat room | Yes | Party size |
| `PvpArenaInstance` | Curated arena, opt-in PvP | Yes (PvP) | 2+ |

### Save-to-seed import
A save file becomes a `RealmSeed` (not a live simulation):
- Unlocked worlds, warp points, story flags, cleared bosses
- The realm service uses the seed to determine accessible content
- See `Protocol.hpp::RealmSeed` for the struct definition

### Persistence separation
- **Character persistence:** level, stats, equipment, abilities, archetype, cosmetics
- **Realm persistence:** seed info, public unlock state, hub state, global events
- **Instance persistence:** ephemeral (room, enemies, combat state); writes back at checkpoints

---

## What diverges between modes

| Aspect | CampaignCoop | PublicRealm |
|--------|-------------|-------------|
| Actor identity | `SlotType` (0/1/2) | `ActorNetId` (server-assigned) |
| Session lifetime | One session = one play session | Realm persists across sessions |
| Player cap per instance | 3 | Varies by instance type |
| Story progression | Host save drives everything | Realm seed defines available content |
| AI companions | Native KH2 party AI (suppressed on replicas) | Disabled in multi-human instances (v1) |
| Character persistence | None (host save only) | Per-character record in realm DB |
| Matchmaking | Manual (share server address) | Party forming in public hubs |

---

## Sequencing recommendation

1. **Finish CampaignCoop vertical slice** (Track A) -- this is the current priority
2. **Refactor protocol IDs for scale** (Track B) -- stable IDs, session/instance split, runtime mode enum
3. **Build PublicRealm v1** (Track C) -- realm service, save-to-seed, public hubs, adventure instances
4. **Add PvP** (Track D) -- arena instances, server-validated damage, stronger authority

Do not pivot to PublicRealm before having real multi-actor replication, enemy authority, transitions, and stable room sync in CampaignCoop.

---

## Reference docs
- `docs/kh2_multiplayer_scope_expansion_review.md` -- full analysis and rationale
- `docs/kh2_realm_protocol_sketch.jsonc` -- protocol v2 schema sketch
- `common/include/kh2coop/Types.hpp` -- enum and type alias definitions
- `common/include/kh2coop/Protocol.hpp` -- v1 and v2 protocol records
