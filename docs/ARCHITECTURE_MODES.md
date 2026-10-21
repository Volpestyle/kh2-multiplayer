# Architecture modes: CampaignCoop and planned PublicRealm

Reviewed 2026-10-20 against main `2ed98fa`. The
[online co-op plan](ONLINE_COOP_PLAN.md) owns product decisions, route gates and
qualification. CampaignCoop is implemented; PublicRealm and PvP are future work.
A runtime enum or schema type does not establish a playable realm service.

## Current CampaignCoop

Up to three session roles (`player`, `friend1`, `friend2`) share the host's
world. Each human controls the native local player at engine slot 0 on their
own PC. Remote humans appear as friend-slot puppets. Session ownership is
paired with connection/generation provenance; it does not mean that friend1
controls native slot 1 on its own machine.

| Layer | Implementation and responsibility |
|---|---|
| Inject DLL | Native hooks, local input/character, puppets, world application, damage policy, SaveGuard and rendering |
| Runtime | Avatar interpolation, shared-memory bridge pumping, transport and bounded recovery |
| SessionHost | Authenticated session slots, owner avatar relay, host-only world traffic/caches and client claim routing |
| Transport | ENet with reliable control/world, unreliable avatars and reliable diagnostics; or opt-in authenticated Steam relay-only broker |
| AvatarBridge / WorldBridge | Versioned DLL/runtime mappings with owner and native delivery provenance |
| Progress mirror | Host-owned allowlisted shared story data, preserving personal data in memory |
| SeatState / native HUD | Selected profile/readiness and authenticated owner HP/MP for display-only native friend gauges |

The host's game runs authoritative enemy decisions. Player-launch defaults
mirror their motion on clients and enable population removal, remote targeting,
co-op difficulty, event hold, friendly fire and downed/revive. General native
factory spawning and the legacy target-forward path remain parked. Direct
DLL/developer launches may have different defaults; consult the
[launch defaults](ONLINE_COOP_PLAN.md#player-launch-defaults-reviewed-2026-10-20).
Enabled behavior is broader than its live qualification: denylisted families,
record binding, bosses, projectiles and story programs retain specific limits.

A client's native player hit produces an authenticated claim to the host;
the host attempts the supported native damage once and broadcasts absolute HP
and deaths. Incoming enemy attacks on the local player remain native on that
player's machine. Puppet HP and HUD display do not authorize remote damage.
See [enemy parity](ENEMY_PARITY.md), [targeting](ENEMY_TARGET_REMOTE.md) and
[damage/revive](DOWNED_REVIVE.md).

### Session and transport shape

ENet uses a standalone `kh2coop_server`, which can run on a PC or Mac. Steam
keeps SessionHost in the gameplay host's runtime and uses each game's broker
and Valve relays, with explicit account admission and no direct-IP fallback.
Neither path moves the host's enemy/story simulation into a dedicated server.
See [ENet joining](JOIN_GUIDE.md) and [Steam transport](STEAM_TRANSPORT.md).

Host loss ends the session. A verified roster or successful reconnect is not
proof of native room/enemy recovery. Prologue profile/readiness agreement gates
construction and input release at checked sites; broader opening-scene identity
and the uninterrupted two-PC route remain open. See [party setup](PARTY_SETUP.md)
and [prologue encounters](PROLOGUE_ENCOUNTERS.md).

### Configuration and persistence

```ini
runtime_mode = campaign_coop
# Session role, independent of local native slot zero:
client_role = 0
```

Protocol **31**, AvatarBridge **4**, WorldBridge **12**, CaptureChannel **1**
are the current source versions. Match all source products; old receipts remain
historical. Package validation has its own version contract; see
[packaging](../tools/packaging/README.md) before assembling a ZIP.

Shared story state is mirrored in memory; personal stats/inventory remain
personal. Automation must never save in-game. SaveGuard redirects writes as a
backstop and grants no permission to persist co-op progress to original saves.

## Planned PublicRealm and PvP

The direction is playable three-player co-op first, then PvP, then a hosted
server mode (plan D14). The older realm design proposes public hubs, parties,
adventure instances, persistent characters and arenas. These are design ideas,
not current commands or supported services.

| Aspect | Current co-op | Proposed realm |
|---|---|---|
| Identity | Three session slots plus connection provenance | Server-assigned actor/instance identities |
| Lifetime | Private play session, host-owned world | Persistent realm with separate adventure instances |
| Joining | Private ENet address or allowlisted SteamID invitation | Public hubs and party formation |
| Persistence | Allowlisted in-memory shared progress | Separate character, realm and instance stores |
| Authority | Host KH2 enemies/story, owner local avatars | Stronger server validation for competitive instances |

A future save-to-seed import would select accessible content, not replay a live
save as a server simulation. No public login, database, matchmaking or reward
writeback service is delivered here. RealmSeed and related retained types are
schema background.

Historical design references: [scope expansion](archive/kh2_multiplayer_scope_expansion_review.md)
and [realm protocol sketch](kh2_realm_protocol_sketch.jsonc). Current contracts
live in `common/include/kh2coop/Protocol.hpp` and the feature docs above.
