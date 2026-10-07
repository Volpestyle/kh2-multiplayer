# Host party setup contract (VUH-1519)

Protocol **12** added `PartyLayout` (type42, 68 payload bytes) and
`PartyReapply` (type43, 25 bytes). AvatarBridge remains4. All session peers
must use the same protocol, now **13** (VUH-1515 `EnemyMotion`; contract and
history in `ENEMY_PARITY.md`); older binaries refuse admission. Sealed packages and
native products have not been rebuilt or changed for this task.

This is a host-authoritative session policy and bridge contract, **not native
party application or spectating**. It does not bench an actor, alter puppet
mapping, disable controls, write MEMT/save data, or prove a scripted fight.
There are no inject/launcher changes.

## Source and default policy

The retained `build/rig/whole-game-coverage-20261006-01/openkh-census.md`, A1,
records the seven authored Party values from MEMT/ARD. The selected program's
Party/SetMember value and current world-ally object must be supplied by the
future checked native producer. No world/room heuristic infers these values.
Unknown rules and missing required ally identities refuse publication.

| Actual native rule | Default logical friend seats |
|---|---|
| DEFAULT / optional W_FRIEND | Both legal seats go to present remote players; with one player, player + Goofy; without players, Donald + Goofy |
| W_FRIEND_IN / W_FRIEND_FIX | Required world ally in seat1; one remote player in seat2, or Goofy if none |
| W_FRIEND_ONLY | Required world ally in seat1; seat2 empty; remote players benched |
| NO_FRIEND | Both friend seats empty; remote players benched |
| DONALD_ONLY | One remote player in seat1, or Donald if none; seat2 empty |

Players occupy every legal available seat before AI or empty choices. The
optional world ally is always benched under the lead's chosen policy. The
host can choose which remote gets limited capacity and choose remaining legal
Donald/Goofy/empty seats. A required ally cannot be displaced. One player/AI
cannot occupy two seats. `partyBenchedMask` explicitly identifies present
players without seats; it grants no local spectator behavior by itself.

`defaultPartyLayout` in `common/include/kh2coop/PartyLayout.hpp` accepts the
full room tuple, version, reason, actual native rule, required ally object,
current three connection IDs, and remote priority `{1,2}` or `{2,1}`. It returns
unavailable for invalid inputs. The host can instead construct a layout that
passes the same `validPartyLayout` policy. An object ID is a host assertion;
the server does not independently read native models or room programs.

## Ownership and publication

Logical seat0 is `LocalPlayer` **from the host's viewpoint**, network slot0.
`RemotePlayer` entries refer to stable network slots1/2. Native projection on
each machine must keep that machine's canonical local player at engine slot0
and translate other entries accordingly. Do not reassign network ownership to
match the native seats. A benched player's input/representation policy is a
separate required native implementation, particularly in NO_FRIEND/ONLY rooms.

Each layout pins **the whole roster**, including benched peers, plus the exact
epoch/world/room/door/map/battle/event tuple. Version is nonzero, strictly
increasing over the host connection lifetime; do not reset it on room changes
or wrap it. A new host connection starts a new namespace.

Native host producers use the existing owner-only
`WorldBridge::SendToRuntime(encode(layout), capturedContext)` FIFO with a real
host source serial. `NetworkClient::sendPartyLayout` is the headless runtime
API. SessionHost accepts only the verified host, current room and roster,
valid policy, and a newer version outside active resync. It reliably broadcasts
the accepted layout to **all** peers, including the host. NetworkClient repeats
scope/room/roster/version validation before callbacks or native bridge delivery.
`partyLayout()` exposes the accepted session snapshot; empty means unavailable.

## Reapplication events and retirement

Room transitions and roster changes invalidate the old layout. SessionHost
publishes a relay-authored `PartyReapply` with the current room and version
floor. It is **not** permission to apply the previous room's rule/model. A
newer checked `PartyLayout` must follow. A late join/rejoin starts unavailable;
old cached layout is never relabeled with the new roster. Host retirement,
transport retirement and resync also retire party authority.

For a story-forced native party change within the same room, the host first
sends `PartyReapply{currentRoom,currentVersion,StoryForced}` through the same
captured native context (headless API: `requestPartyReapply`). SessionHost
checks host authority and exact version/room, retires the layout and forwards
the notification with relay provenance. The host then supplies a newer layout
using the newly checked Party/SetMember rule and model. A newer complete layout
may also directly replace the old one, with reason `StoryForced`.

Runtime forwards both admitted native-authored layouts and the narrowly allowed
relay-authored PartyReapply envelope through the existing WorldInbox. Future
native code must hook these messages **after normal scope/delivery admission**,
clear on room/roster/reset/resync, and recheck actual actor/program safety before
any application. It must not reapply on duplicate versions. No apply ACK or
success inference is present; receipt is not proof of native application.

## Player kits and puppet member slots (VUH-1513)

A local player kit (`KH2COOP_PLAYER_KIT`, VUH-1513) replaces resolved member 0
(`exe+0x2A25300`) after each area load. A friend slot whose party selector is 0
(the VUH-1489 native-Sora puppet) resolves through that same member, so with a
kit active it would spawn as the local kit. Until this contract assigns each
remote puppet its own member index, the DLL refuses native-Sora clone puppets
whenever `KH2COOP_PLAYER_KIT` is set to anything but `0`. VUH-1519 owns that
per-puppet member slot (for example a spare index such as 3 where the room has
no guest) and the kit each remote puppet must show, carried in
`AvatarState.character` (0 Sora, 1 Roxas, 2 dual-wield Roxas, 3 Mickey).

## Offline evidence

`kh2coop_party_test` checks every authored rule with all player-presence sets
and both priorities, codec exactness and refusals, host-only versioned routing,
room/story/roster invalidation, and reconnect identity over real loopback ENet.
The same three-peer network checks run through the actual Steam broker stack
with mocked Valve/IPC boundaries in `kh2coop_steam_broker_test`.

The policy does not establish that engine companions/puppets can be remapped in
these rooms. Native integration and whole-game live qualification remain open.
