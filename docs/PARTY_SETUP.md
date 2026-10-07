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

## Native application (VUH-1519)

`inject/src/PartyNative.*` applies the host layout natively. It is **default off**: `KH2COOP_PARTY_NATIVE=1`, which also requires `KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1` and `KH2COOP_CLONE_NEUTRAL_INPUT=1`, and the module's live private-status and neutral-input state.

It qualifies one case only: "3 players, no NPCs" in GoA `04/1A`, evt 0, with the native DEFAULT row `00/01/02/12`. On such a load, an observer on PlayerKit's per-load `3E2EB0` resolver post-hook does the following:
- sets resolved members 1/2 (Donald/Goofy) to Sora `0x54`, so both friend seats spawn as player-class Sora clones;
- leaves the save-backed party row and MEMT unwritten.

`NativePrivateStatus` gives both clones private status records. In a party load, the last Sora built is the canonical player.

Every other room, rule or layout resolves natively, and so does any later load: the native party returns by itself.

**Flags.** `KH2COOP_PARTY_NATIVE`, `KH2COOP_REMOTE_KIT_SLOT` and `KH2COOP_PLAYER_KIT` are mutually exclusive. Each conflicting side refuses and logs it.

**Timing (scoped lead decision).** The resolver runs during an area load, before the host can publish that room's layout. So each machine applies the **newest accepted layout** of the same generation, roster and local slot, **pinned to that layout's own world/room/evt**. Epoch and door are ignored, so a same-room reload matches. The layout is re-checked against the native row and members read at that load.
- StoryForced and RosterChanged `PartyReapply` clear it.
- The host adopts its own layout only from the relay's echo, and re-sends at most 6 times.

**A room-independent PartyIntent message (VUH-1786) is required before any second qualified room or mixed layout.** No new packet types or protocol version: it uses `PartyLayout` 42 and `PartyReapply` 43 at protocol 13.

**Live result, 2026-10-07: PASS.** Run `build/scenarios/20261007-044847_vuh1519_party_native_goa_three_1`, fixture `build/rig/vuh1519-party-native-20261007-01/live-fixture-05`, DLL `d0d28019…` (lane pins `a1466dd3…`). Three games in GoA:
- **controls:** flag off, no private status, and party + remote kit;
- **apply:** host layout echo, one host same-room reload, then on every machine:
  - the build order clone, clone, local;
  - two private bindings, with no reason 15;
  - a census of three Soras and no Donald/Goofy;
  - puppets on the two clones;
  - neutral input on both;
- **interval:**
  - one damage1 per clone, on two machines, with the clone's personal SAVE commit bytes unchanged;
  - an unchanged in-memory SAVE hash, outside the enumerated exclusions;
  - follow p95 4–7.5;
  - no rebinds;
- **restore leg:** after one runtime left and the host reloaded, the remaining machines resolved members 1/2 natively to `0x5C/0x5D`, and Donald/Goofy were back;
- **closure:** clips on all three sides, saves unchanged, SaveGuard on, and no save attempts.

Earlier attempts 01–04 failed on fixture expectations, which were fixed one at a time:
- 01: kill ordering;
- 02: client post-load match ordering;
- 03: a single hit never commits through `3C2120`;
- 04: a benign stream-gap puppet rebind.

**Open:**
- Proof that `resolved[]` never serializes into SAVE (xref audit).
- VUH-1786 PartyIntent.
- VUH-1787 stale-puppet hold. A host stall over 1 s makes AvatarSync (`staleAfterMs`) release and rebind remote puppets; the rebind keeps the same clone.
- Per-seat kits: the last-built seat is the local player.
- Mixed layouts: `PuppetTarget` is all clones or all friends.
- Empty seats.
- Restore-on-exit of members 1/2 is untested (owned kill).

## Offline evidence

`kh2coop_party_test` checks every authored rule with all player-presence sets
and both priorities, codec exactness and refusals, host-only versioned routing,
room/story/roster invalidation, and reconnect identity over real loopback ENet.
The same three-peer network checks run through the actual Steam broker stack
with mocked Valve/IPC boundaries in `kh2coop_steam_broker_test`.

The policy does not establish that engine companions/puppets can be remapped in
these rooms. Native integration and whole-game live qualification remain open.
