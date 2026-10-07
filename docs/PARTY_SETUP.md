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
transport retirement and resync also retire party authority. Exception
(VUH-1786): a resync does not retire stored `PartyIntent`s. They are pinned to a
roster, not a room, and a roster change retires them anyway.

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

VUH-1519 itself added no packet type: it uses `PartyLayout` 42 and `PartyReapply` 43 at protocol 13. Crossing into a second qualified room without an extra reload needs the room-independent `PartyIntent` (VUH-1786, below).

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

## PartyIntent (VUH-1786, protocol 14, type 47)

Default off, under `KH2COOP_PARTY_NATIVE`. A host `PartyIntent` is the intended party for **one target room** (`worldId/roomId/eventProgram`, with no epoch or door). Its fields:
- seats, using the `PartyLayout` encoding and policy;
- per-seat kits (seat 0 = 0, a remote player = Sora `0x54`, AI or empty = 0);
- the authored rule;
- the roster;
- a version in its own namespace per host connection.

It is a 63-byte payload.

**Relay.**
- **Admission:** host only, outside a resync, with the roster equal to the verified roster.
- **Cache:** one intent per target (up to 8), kept across room changes and cleared on host departure or stop. A refusal (full cache, stale version) never raises the version floor.
- **Broadcast:** to all peers, the host included (that copy is the echo).
- **No replay to joiners.** Every verification mints a fresh connection id, so a cached intent's roster can never match the roster a joiner joins. The host republishes for the new roster instead (`HostIntentDue` fires on a roster change). The cache only bounds targets and keeps the version floor.

**Host.**
- **Publication:** one intent per pinned qualified room, GoA `04/1A` and `04/0A` (hb10 has no evt program 0, and the row is DEFAULT). It publishes per generation and roster, only while it is locally ready, and re-sends at most 6 times while no echo has arrived.
- **Echo:** an echo stops the re-sends only when the host adopted it (`accepted`), or refused it as `stale-version` for exactly the version it sent. For example, a `no-generation` refusal during a generation flicker keeps the re-sends going. Layout echoes use the same rule.
- **Repair:** each room-change layout (`reason=RoomChanged`) re-arms both pinned-room intents with fresh versions, at two extra reliable packets per transition. A client that lost its store is therefore repaired at the next transition.

**Client.** A client admits an intent with the same rules as a layout. Room transitions never clear intents. The 8-entry store refuses (and does not forward) a new target once full. The DLL refuses targets outside the pinned rooms.

**DLL.**
- **Load:** at every load, the `3E2EB0` observer takes the most recently accepted plan for the load's room. That is the room-pinned layout, the intent for that room, or a story hold. The load line names it (`source=layout|intent|hold|none`). Every native gate is then re-checked. So the **first** load of `04/0A` applies the party with no extra reload.
- **Publication:** the game thread publishes the plan table to the loading thread as one set, behind a seqlock with a 32-bit counter. Each entry carries the full 64-bit acceptance sequence. A reader that cannot get a stable copy resolves natively.
- **Roster:** any acceptance for a roster that differs from the stored one, in any seat, first retires every plan, intent and hold of the old roster. The version floors are kept for the same generation and host connection.

**Story-forced (rev2).** A `StoryForced` reapply wins exactly **one load** of its room:
- It clears the room-pinned layout plan and sets a hold on its world/room, at any evt, with a fresh sequence. The hold is selected like a plan and resolves natively (`source=hold result=no-intent`).
- The hold is released after one load of that room uses it (`story hold released`), or when a newer layout or intent for that room is accepted. After that, the stored intents apply again.
- While a hold is active, the host does not republish the intent for that room.
- **Rev3 (F1):** after the host's own held load of that room, the host publishes **no layout and no intent for that room for the rest of that visit**, through that epoch's retries, including the S2 re-arm. A post-load host layout would otherwise clear the clients' holds before their own (later) load, leaving the host native and the clients with clones. The quiet visit ends at the next epoch of the room, or when the host enters another room.
- **Rev3 (N-a):** an own intent echo stops the host's re-sends only when the stored target now holds exactly the sent version. This does not depend on echo order across the shared intent floor.
- **Rev3 (N-b):** the load line ends with `misses=N`, the count of seqlock reads that fell back to an empty table (that is, a native load).

**Open risks for a future StoryForced producer.** No DLL code produces `StoryForced` today. The only source is `NetworkClient::requestPartyReapply`. Hold, release and the quiet visit are therefore proven offline only (policy, end-to-end and mutants), and a producer must close these before shipping:
- **One hold slot:** a second story reapply for another room replaces the first hold, so the first room's story party can lose to a stored intent.
- **Supersession:** a newer layout or intent for the held room releases the hold, per the VUH-1519 "until a newer layout" contract. A producer must send the reapply before the host publishes for that room. Otherwise the reapply arrives after a fresh plan, and the story loses on every machine.
- **Quiet visit:** after the host's held load, the host withholds its layout and intent for that room for the whole visit, by design. The story party rules that visit, and clients get no re-validating layout until the next epoch.
- **Release timing:** the hold is released by the first load of its room at any evt, even if that load was already native for another reason (for example an event program). A story party that needs more than one load must re-send the reapply for each load.
- **Live test:** no live fixture has exercised a hold. The first StoryForced producer needs one, with a hold on a client that loads after the host.
- There is one hold slot: a later story reapply for another room replaces it.
- A story-forced party that shows in the native row, members or event state also wins at any load.
- `RosterChanged` retires the intents and the hold.

**Merge plan (lead decision, 2026-10-07).** VUH-1786 and enemy-target-remote (`RemoteHit` 45, `TargetAuthority` 46) both take protocol 14, and they ship at v14 **together**:
- Never publish a v14 package that contains only one of them. If that happens, bump to v15.
- Whichever lands second merges the `PacketType` enum, the `Protocol.hpp` version comment, the `RS_INNER` line and the `HudNamesTest` message, then rebuilds and re-runs the full CTest and the offline controls on the merged products.
- Each lane's pinned DLL predates the merge.

PartyIntent leaves VUH-1519's own open items open: mixed layouts, empty seats and per-seat kits.

**Static audit: `resolved[]` does not reach SAVE (2026-10-07).** This was owed since the VUH-1513 review. Evidence is in `build/rig/vuh1519-resolved-save-audit-20261007-01/`, run against Steam exe `9002b2de`.

**Method.**
- A byte scan of `.text` for every RIP-relative displacement into `exe+0x2A25200..0x2A25400`, which covers `resolved[18]` at `0x2A25300..0x2A25323` and the MEMT pointer at `0x2A252E0`.
- A scan of `.data` and `.rdata` for absolute pointers into that window. There are none.
- Each candidate was then checked by `dumpbin` disassembly, and every direct caller of every accessor was disassembled.

**Every instruction that touches the array:**

| Function | What it does with the array |
|---|---|
| Resolver `3E2EB0`/`3E2EFA` | Zeroes it, then fills it from MEMT entries. It writes nothing else except the pool-size tail `39E270`, which takes its sizes from MEMT `+8`/`+0xC`, not from members. |
| `3E2E40` | Returns `&0x2A252F0`, the struct that holds the array at `+0x10`. |
| `3E2E50` | Returns the member id for an index. |
| `3E3180` | Returns the member id for a role. |
| `3E2E60`, `3E3680`, `3E3830` | Return objentry `+0x4C`, the **status key**, or a seat index. They never return the id itself. |

**The 16 sites that consume a member id.** Callers of `3E2E40`, `3E2E50` and `3E3180`, plus `3C32E0`/`3C3320`, all of which were disassembled:
- None writes into the SAVE body (`exe+0x9A98B0`, `0x10FC0` bytes) through a RIP-relative address.
- An id is only used for the following:
  - objentry lookup (`3DFEB0`);
  - comparisons, such as checking for Sora `0x54`;
  - the factory (`3C2FC0`);
  - one store into a heap task object (`36B460`: `[rsi+0x20]`, where `rsi` comes from a fresh `14F8A0` allocation).
- `419290` reads the SAVE byte at `SAVE+0x3524` to pick a form member, and reads it only.
- The only party-row writer reached from these sites is `3E38D0`. It swaps two selector bytes of the SAVE row; it never stores a member id.

**What does reach SAVE.** Only the status key that `3E2E60`/`3E3680` derive. Through the status pool it selects a SAVE character record, and `3C2120` commits status bytes into that record. That is the path `NativePrivateStatus` vetoes for private clone records: live, the settle commits were vetoed and the clone's personal SAVE bytes were unchanged across damage.

**Limits.**
- This covers direct references and direct `E8`/`E9` calls only. Indirect calls into the accessors, and a struct pointer from `3E2E40` held elsewhere, were not enumerated.
- The save-file writer itself was not traced. The array lies outside the SAVE body, so it reaches a save file only if something copies it into SAVE, and no such writer was found.

**Open:**
- VUH-1786 PartyIntent: see the PartyIntent section (candidate).
- VUH-1787 stale-puppet hold (candidate). AvatarSync now holds a puppet whose owner's stream has been silent for more than 1 s (`staleAfterMs`). It keeps the newest pose, marked `AvatarHeld`, and releases only after `releaseAfterMs`: 3000 ms by default, set with `KH2COOP_AVATAR_HOLD_MS` (1000..10000; 1000 restores the old receiver behaviour only, and the sender's load flag below stays on). Room, cutscene, roster, session and connection exits still release immediately. An owner's own room load is flagged `AvatarInCutscene` by its DLL (on the transition request and while the load is pending), so receivers hide it instead of holding a ghost at the door. A held puppet's velocity is zeroed and it idles after about 15 frames unless downed. Enemy targeting (`CloneCandidate`) and the revive prompt (`TargetCandidate`) skip held poses. Offline tests only (`kh2coop_avatar_hold_test`); not yet live-verified.
- Per-seat kits: the last-built seat is the local player.
- Mixed layouts: `PuppetTarget` is all clones or all friends.
- Empty seats.
- Restore-on-exit of members 1/2 is untested (owned kill).

## Per-seat kits (party kits, candidate)

Default off. **`KH2COOP_PARTY_KITS=1` on every machine** of a party-native session lets each seat show its owner's chosen kit. The owner chooses with `KH2COOP_PLAYER_KIT=0x5A`. That flag is now allowed with `KH2COOP_PARTY_NATIVE`, but only when `PARTY_KITS` is set. `KH2COOP_REMOTE_KIT_SLOT` stays exclusive. Qualified kits are Sora `0x54`, Roxas `0x5A` and Mickey `0x5B`, in the pinned rooms only.

**Signal.**
- **Owner:** in the combined path, it streams its chosen kit in `AvatarState.character`. That is roster 1 for Roxas, and it is stable per launch, not the per-frame actual member 0.
- **Host:** latches each remote's roster from validated poses only. It publishes intents and layouts only once every remote's kit is known and supported. Every kit vector it publishes goes into `PartyIntent.kits`, with seat 0 being the host itself. A changed vector produces new versions.
- **Wire:** the rule allows any qualified kit in any player seat, and the legacy `kits[0]=0` still means Sora. No protocol change: still v14, no new packet.

**Each machine.**
- Kits belong to the roster. They are stored from the newest accepted intent, retired on a roster change, and a layout plan borrows them. In kits mode a layout plan with no kits yet is Unsupported.
- **Build order in a party stamp (rev3, live run `20261007-084248`).** The game builds member 0 first (raw566) and member 1 next (raw567); both become clones. It builds member 2 last (raw568), and that actor becomes the canonical local player.
- **The observer's writes.** At a qualified load it writes members 0/1 = the two other players' kits (lower slot first) and member 2 = this machine's own kit. It requires member 0 to already hold this machine's own kit, which PlayerKit writes first in the same hook scope. Otherwise `local-kit-mismatch`: native.
- **Kits off.** All three are Sora, so the write is exactly VUH-1519's.
- **Clone neutral input.** In kits mode, `CloneNeutralInput` neutralizes Roxas clones as well.
- A machine without `PARTY_KITS` never applies a roster that holds a non-Sora kit, through any entry. That covers the intent and any later layout that borrows the roster's kits. It is `Unsupported` (native), and no kit bits are packed, so a legacy all-Sora roster is bit-identical to VUH-1786.

**Puppets.** With two clones of different kits, each puppet drives the clone whose objentry equals its owner's kit (`PuppetKit`). The entity-list order no longer matters.

**Private status.** In kits mode, Roxas (90, key 14) and Mickey (91, key 4, form 11) clones and canonical locals are promoted, each checked against its own key (the build line names it: `key=`). The clone keys must equal the kits written, or `BindFault(16)`.

**Live status:** Roxas passed live in run `20261007-090953`, and Mickey in run `20261007-115923` (see `PLAYER_KITS.md`, Evidence).
- **Unknowns:** a Roxas canonical local in a party stamp, a Roxas clone among three players, the key-14 clone's SAVE veto, and 04/0A with Roxas.
- **Command menu:** it is still unguarded with a kit (VUH-1509).

## Offline evidence

`kh2coop_party_test` checks every authored rule with all player-presence sets
and both priorities, codec exactness and refusals, host-only versioned routing,
room/story/roster invalidation, and reconnect identity over real loopback ENet.
The same three-peer network checks run through the actual Steam broker stack
with mocked Valve/IPC boundaries in `kh2coop_steam_broker_test`.

The policy does not establish that engine companions/puppets can be remapped in
these rooms. Native integration and whole-game live qualification remain open.
