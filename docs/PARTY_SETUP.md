# Host party setup contract (VUH-1519)

Protocol **12** added `PartyLayout` (type42, 68 payload bytes) and
`PartyReapply` (type43, 25 bytes). Later protocol changes are recorded in
`ENEMY_PARITY.md` and the PartyIntent section below. All session peers must
use matching products; older binaries refuse admission.

The initial implementation established host-authoritative session policy and
the bridge contract. Native application was added later, behind explicit
opt-in flags. The sections below distinguish the original contract from the
layouts qualified live; whole-game party replacement and spectating remain open.

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

The initial native qualification covered: "3 players, no NPCs" in GoA `04/1A`, evt 0, with the native DEFAULT row `00/01/02/12`. On such a load, an observer on PlayerKit's per-load `3E2EB0` resolver post-hook does the following:
- sets resolved members 1/2 (Donald/Goofy) to Sora `0x54`, so both friend seats spawn as player-class Sora clones;
- leaves the save-backed party row and MEMT unwritten.

`NativePrivateStatus` gives both clones private status records. In a party load, the last Sora built is the canonical player.

Every other room, rule or layout resolves natively, and so does any later load: the native party returns by itself.

**Flags for the original path.** `KH2COOP_PARTY_NATIVE`, `KH2COOP_REMOTE_KIT_SLOT` and `KH2COOP_PLAYER_KIT` are mutually exclusive. Party kits use the later `KH2COOP_PARTY_KITS` path instead. Each conflicting side refuses and logs it.

**Timing (scoped lead decision).** The resolver runs during an area load, before the host can publish that room's layout. So each machine applies the **newest accepted layout** of the same generation, roster and local slot, **pinned to that layout's own world/room/evt**. Epoch and door are ignored, so a same-room reload matches. The layout is re-checked against the native row and members read at that load.
- StoryForced and RosterChanged `PartyReapply` clear it.
- The host adopts its own layout only from the relay's echo, and re-sends at most 6 times.

VUH-1519 itself added no packet type: it uses `PartyLayout` 42 and `PartyReapply` 43 at protocol 13. Crossing into a second qualified room without an extra reload needs the room-independent `PartyIntent` (VUH-1786, below).

**Live result, 2026-10-07: PASS.** Run `build/scenarios/20261007-044847_vuh1519_party_native_goa_three_1`, fixture `build/rig/vuh1519-party-native-20261007-01/live-fixture-05`, DLL `d0d28019â€¦` (lane pins `a1466dd3â€¦`). Three games in GoA:
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
  - follow p95 4â€“7.5;
  - no rebinds;
- **restore leg:** after one runtime left and the host reloaded, the remaining machines resolved members 1/2 natively to `0x5C/0x5D`, and Donald/Goofy were back;
- **closure:** clips on all three sides, saves unchanged, SaveGuard on, and no save attempts.

Earlier attempts 01â€“04 failed on fixture expectations, which were fixed one at a time:
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

PartyIntent originally left mixed layouts, empty seats and per-seat kits open. The bounded mixed-party and empty-seat qualifications below supersede that initial status.

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

**Ally hits (VUH-1808, `KH2COOP_ALLY_HIT`, default off; meant on in co-op).** Clones are player-class actors,
and the puppet driver keeps them on team 0 so nothing hits them. Their own replayed attacks then carry
team 0, whose hit mask (`~((1 << team) | 1)` = `~1`) includes the local player's team 1, so a clone's swing
that overlaps the local player creates a native hit. `DamagePolicy` zeroes its HP (`RemoteSource`), but the
reaction still plays (star burst, red portrait flash). Run `20261007-115923` logged 15 on one machine: the
Mickey clone's attack motion 186 lands twice (6, then 2) and reached the host on 8 of 9 swings; Sora's 151
lands once and rarely reached anyone. The local's attacks never hit a clone (bit 0 is never in a mask).
- `=1` refuses every native-allowed hit between two distinct player-class actors (clone â†’ local, local â†’
  clone, clone â†’ clone) at `3D2060`, after the original ran, so no hit record, `[hit]` line or reaction exists.
  Atkp kinds 5/6 (they bypass the mask natively) stay native. Enemies are untouched either way.
- `=trace` changes nothing and logs the same pairs. Both log one `[allyhit] f=â€¦ attacker=â€¦(side team=â€¦) ->
  victim=â€¦(side team=â€¦) atkTeam=â€¦ mask=0x.. kind=â€¦ atkp=â€¦ native=â€¦ verdict=â€¦ via=owner|source` line per
  (attack, victim) pair (160 lines), and a stats line every 600 frames while counts change.
- The attacker is the attack's owner (`+0x10`), or its source (`+0x14`) when the owner is not a player, the way
  the native check also excludes both. A player's projectile or magic owned by another object counts (`via=source`).
- Trace budget: rows the native check already refused use at most 32 of the 160 lines, and an attack on its own
  owner is never traced. Every swing is asked about every player before the collision test, so those rows would
  otherwise crowd out the refusals.
- A clone never hits a team-1 non-player either (Donald, Goofy, world allies): its team-0 mask includes team 1,
  which its native party mask `~((1 << 1) | 1)` excludes. Reachable since the two-player party keeps Goofy beside a
  clone. The row shows `victim=â€¦(other team=1)`.
- Not covered:
  - Atkp kinds 5/6 (Cure/CCure) between players stay native, as on main. Check for a double heal before
    networked ally heals ship.
- PvP (reserved): `allyhit::Decide` would return Native for a remote player's attack on the local player,
  and `DamagePolicy`'s `RemoteSource` branch would route that damage on purpose instead of zeroing it.

**Private status.** In kits mode, Roxas (90, key 14) and Mickey (91, key 4, form 11) clones and canonical locals are promoted, each checked against its own key (the build line names it: `key=`). The clone keys must equal the kits written, or `BindFault(16)`.

**Live status:** Roxas passed live in run `20261007-090953`, and Mickey in run `20261007-115923` (see `PLAYER_KITS.md`, Evidence).

**Two players (mixed layout, `one-clone`, live-qualified on the host side).** With two players, `defaultPartyLayout` puts the remote in
seat 1 and keeps Goofy in seat 2. On each machine the observer writes:

- member 0: the other player's kit, built first, as the clone;
- member 1: native Goofy, `0x5D`;
- member 2: own kit, built last, as the canonical local.

The gates are the same as for `two-clones`. Notes:

- **Host publication:** the host publishes with one remote connected. In kits mode it needs only that remote's kit.
- **Private status:** expects one clone, so a stamp logs `party=1/1/1`. Goofy's construction stays ordinary.
- **Puppets:** while a plan is applied they drive clones only. The present remote's puppet drives the one clone, and
  Goofy keeps his native AI.
- **Restore:** when the remote leaves, the roster-pinned intents retire. The next load is native (Donald + Goofy),
  and shutdown restores members 0..2 while still ours.
- **Other layouts:** retaining Donald is qualified by the host-chosen companion runs below. Two players with an empty AI seat have the separate qualified opt-in below.
- **Scope:** run175931 qualified canonical Roxas and the remote-kit private SAVE veto in the two-player GoA/Borough route. Roxas among three players remains unqualified.
- **Command menu:** it is still unguarded with a kit (VUH-1509).
- **Live evidence:**
  - **Run 20261007-175931 (rev5) passed:** host Sora, client Roxas, GoA then Borough 04/0A then GoA again, then the client leaves and the host restores Donald and Goofy natively.
  - **Earlier run 20261007-165859** passed every applied-visit gate on both machines: one-clone shape, puppet to remote kit, Goofy HP/max stable, completed native Goofy AI, and clone-to-ally refusals. Its restore step failed only because the fixture hard-killed the client runtime.
  - **Abrupt runtime death (fixed):** the runtime now holds a writer lease on the world bridge. When the writer dies, for example by TerminateProcess or a crash, the client DLL retires its party plan within a bounded time and restores native on the next load. Run 20261007-201506 (rev6) hard-killed the client runtime and required the client to restore; it passed.

## Offline evidence

`kh2coop_party_test` checks every authored rule with all player-presence sets
and both priorities, codec exactness and refusals, host-only versioned routing,
room/story/roster invalidation, and reconnect identity over real loopback ENet.
The same three-peer network checks run through the actual Steam broker stack
with mocked Valve/IPC boundaries in `kh2coop_steam_broker_test`.

The policy does not establish that engine companions/puppets can be remapped in
these rooms. Native integration and whole-game live qualification remain open.


## Two players without AI companions (live qualified, 2026-10-08)

Set `KH2COOP_PARTY_AI=none` on both DLLs, alongside
`KH2COOP_PARTY_NATIVE=1`, `KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1` and
`KH2COOP_CLONE_NEUTRAL_INPUT=1`. The qualified fixture also sets `KH2COOP_PARTY_KITS=1` and uses the
Sora/Roxas party-kit path. The default mixed party and three-player paths are unchanged.
This opt-in is limited to GoA `04/1A` and Borough `04/0A`, event0, the checked
DEFAULT row and the pinned selected spawn packages. Other rooms, rules,
packages and layouts resolve natively.

Logical seats are Local/Remote/Empty. Physical resolved members are
remote/0/local: the remote clone is built first and the canonical local last.
The save-backed row remains `00/01/02/12`, unwritten. Native selector `0x12`
means absent; the corresponding resolved member ID is zero. Roxas prologue
and native solo programs supply precedents for absent friend seats, but do
not establish that arbitrary sparse rows are safe. Scoped native row/status
projections and read-only ARD qualification are required before admission.

Five byte-guarded hooks and three caller signatures protect the qualified
menu path. Pause caller `3065C8` passes a compact entry index to `2FC5B0`,
which expects an original row seat; this caller alone translates through the
native entry's saved seat at +8. Two companion-only NEW/history readers use
key0 for Sora/Roxas through `2FC6D0`, retaining their native empty-skip and
separate player-history logic. No SAVE or menu table is written. Original
trampolines and the callback module remain retained until process exit.
See `build/rig/party-empty2-20261007-01/rev6/research/consumer-audit.md`.

**LIVE PASS:** run
`20261008-024646_party_empty2_matched_off_on_inventory_1`, fixture
`build/rig/party-empty2-20261007-01/rev6/live-fixture-10`, completed in367s.
Three fresh install controls and the matched feature-OFF native sequence
passed before feature ON. Both machines passed the two-player/zero-AI census,
one private remote puppet, clone damage/private SAVE veto and ally-refusal
gates across GoA, Borough and GoA again. Both passed pause and first Items
character selection; that pinned save exposes two characters plus Stock,
versus three characters plus Stock in the native OFF control. Stock is not
an AI member, and this count is not a general rule for other saves or worlds.
After abrupt client-runtime death, intents retired and both next loads
restored their native party. All seven launched games were cleaned up;
disk saves were unchanged. The report, captures and closure are retained
under the run and fixture paths above.

**SAVE semantics:** native acquired-ability history at428A/B is persistent
and protected. The fixture prepares native menus before the feature baseline
and validates that initialization with typed native semantics, then requires
applied and restore comparisons to remain protected. Matched OFF inventory
also accounts for native playtime timers2444/245C by their typed clock rules;
it does not learn arbitrary byte exclusions. Serialized Drive3528/3529 and
24F0 remain protected throughout the puppet interval. The previously removed
puppet gauge hold is replaced by native command admission gates; it is not
part of this empty-seat patch and grants no SAVE exception.

**Qualification limits:** pause and first Items character selection only.
Party/change, Status, deeper Items and equipment edits remain unqualified;
additional native consumers assume dense companion rows. Shutdown restoration
was not exercised: the fixture uses owned process kills. The qualified restore
is roster/bridge retirement followed by a native load. Solo with Donald or Goofy is separately qualified below; world allies remain pending.
Host-chosen two-player Donald/Goofy qualification is recorded below.


## Host-chosen companion layouts (2026-10-08)

The product from revision5 of `build/rig/party-choice-20261007-01`, code
`3e3dcc6`, qualifies two players or a solo host with chosen Donald or Goofy. It includes
the landed empty-seat/pause guards and read-only puppet Drive/Summon command
admission; it does not reintroduce the former Drive gauge hold or any SAVE write.
The landing on main89579a4 preserves that reviewed party product exactly.

**Two-player live PASS:** Donald run
`build/scenarios/20261008-184452_party_choice_2p_donald_1` (358.1 s), and Goofy run
`build/scenarios/20261008-185124_party_choice_2p_goofy_1` (342.1 s). Both complete
the matched native-OFF inventory, GoA/Borough/GoA applied visits, companion
HP/AI, puppet and ally-hit gates, pause/first-Items menus and next-load restore.
Disk saves remained unchanged; protected in-memory checks had zero changes
outside the typed native model. The shipped DLL is `0aa42342`; full source/product
pins and the 69 passing own-process native executions are in revision5.

**Solo Donald and Goofy are now qualified** by corrected rev7 fixtures:
`build/scenarios/20261008-201415_party_choice_solo_donald_1` and
`build/scenarios/20261008-201829_party_choice_solo_goofy_1`. Both passed all
steps; each closure proves all four disk saves unchanged. The product source
was unchanged: earlier rev5/rev6 failures were fixture identity/source gates.
Rev7 selects layout versus target intent using the accepted pre-load publication
order, rather than requiring target-intent provenance when a newer matching
layout legitimately supplies the first load. Matched OFF inventory, native
companion HP/original-AI receipts, no puppet ownership, sparse census, pause and
first-Items character page and next-load native restore passed. This qualifies
solo session-start choice; live peer-join retirement and full menus remain open.

With the existing native-party/private-status/neutral-input opt-ins, the host
may set `KH2COOP_PARTY_AI=donald` or `goofy` before launch. For two players the
host authors `[Local, Remote, chosen AI]`; on each machine the physical tuple
is `[remote kit, native companion, own kit]`. The host's published layout wins
over a different local client preference. With one host present at session
start, an explicit choice authors `[Local, chosen AI, Empty]` and applies
`[own kit, native companion, 0]`, without clones or puppet ownership. Unset
choice preserves existing defaults; `none` preserves the two-player empty-seat
option and does not opt a solo session into this candidate.

The missing solo member2 uses the same byte-guarded row projection, compact
pause portrait translation, scoped companion-history readers, selected-package
qualification and retained trampoline lifetime as the landed empty-seat code.
The row projection selects only the owned missing member, and preserves the
native companion's original row seat and history key. All admitted loads remain
limited to default-party GoA04/1A and Borough04/0A, event program0 and no active
event/cutscene. SAVE party selectors and MEMT are not written.

The same two proven companion-history callers also skip player keys under an
exact owned paired tuple, since a local Sora/Roxas after the native companion
has no companion history row. Dense paired tuples do not receive empty row or
portrait projection. Both peers install these guards before applying any
one-companion plan; a failed guard refuses the load without writes.

The native companion is excluded from puppet targeting. Positive original-AI
completion receipts identify Donald or Goofy and the current load. Writer
expiry and roster changes retire the standing plan for restoration on the next
qualified load. Solo authoring is a session-start opt-in: once a peer joins,
its later departure cannot automatically repopulate a solo chosen layout;
only a genuinely new host connection can opt in again. Generation-zero hold
semantics remain those of the landed runtime-liveness implementation.

Two-player Donald/Goofy and solo Donald/Goofy have passed. The two-player
Donald case owns the fresh install-control receipt reused by the remaining
unchanged-product fixtures. Rev7 solo Donald and Goofy were independently
ADOPTed in codex-review-rev7-solo-donald.md and codex-review-rev7-solo-goofy.md
under build/rig/party-choice-20261007-01. Reused verification comprises69 native
executions plus208 audited socketless checks per solo fixture. Each fixture first completes a
matched feature-OFF inventory using typed native SAVE semantics; it may not
learn arbitrary byte exclusions. Menus cover pause and the first Items character
page only. World allies, interactive choice during a session, full Party/Status
or deeper Items navigation, and live solo peer-join retirement remain open.

### Chosen companion shutdown proof

Dense paired chosen-companion history guards remain owned if shutdown cannot
positively restore the captured native party. PartyNative snapshots all three
original members before restoration and supplies that tuple only after a normal
restore and matching post-read. The menu guard independently rereads all three,
rejects unreadable or still-owned tuples, and retains its readiness without that
proof. An invalid original snapshot or a restoration exception cannot authorize
guard retirement. Empty-seat row and portrait guards retain their separate
existing rules; a successful native resolver remains an independent retirement
authority. No SAVE bytes are written by this shutdown proof.


### Secondary-player Items entry refusal (qualified 2026-10-08)

The first Items character page remains native state1. Under a physically owned
empty-seat layout, the secondary player row is not a native companion: a
byte-guarded native admission callback refuses its deeper menu when the compact
entry identifies original seat2, player key1/14 and a nonnull native status.
The first character, Stock, native-AI rows, Cancel, other pages and restored
native parties retain native delegation. Installation checks the selected-index
setup, input and result-classification calls, refusal branch and native ALfalse
feedback4 contract before hook creation. There is no menu or SAVE write.

Optional `KH2COOP_ITEMS_ADMISSION_TRACE=1` emits at most16 process-lifetime
consumed-refusal receipts with caller, state, selection, menu/status and owner.
The qualifying live run20261008-200037_party_items_secondary_consumed_refusal_1
passed all steps with disk saves unchanged. It verified fresh identified
secondary-player refusal on both machines, positive Cancel, retained sparse
members and native first-character-page/count restoration after writer expiry
and the next load, using the matched native-OFF typed SAVE inventory.

This qualifies the bounded refusal and first-page restoration, not deeper
first-row/Stock/native-companion activation, item use, equipment, abilities,
Party/change, save screens or full DLL-shutdown quiescence. Native menu status
is bracketed independently; actor/private-status equality is not assumed.
Independent review: party-menus-20261008/codex-review-rev5-fixture02.md (ADOPT).
Reusable offline evidence: rev5's36 native modes/664 assertions and98 audited
socketless fixture checks. The landed production file is byte-identical to
that compiled/adopted rev5; current-main chosen-AI A2 restoration ownership is
preserved. The separately qualified ordinary-root Party refusal is described below.


### Ordinary pause-root Party refusal (qualified 2026-10-08)

While the physically owned sparse-middle `[remote, 0, local]` tuple remains
installed, a seventh byte-guarded callback refuses Party/change from the
ordinary pause root (state0). Native Party/change mixes unchecked compact
indices with original row seats; the existing portrait translation cannot make
its builder safe. The callback maps the actual compact input through feature
maskBEEC20 and refuses feature3 before descriptor access or state transition.
State, mask and ownership are bracketed; unowned layouts, trailing empties,
other pages/features and unreadable observations retain native delegation.
The game's input/feedback/common-refusal and Cancel paths remain intact. The
installer pins their complete contract plus native feature mapping and feedback
tables before accepting any empty member. This adds no menu or SAVE write.

`KH2COOP_PARTY_ADMISSION_TRACE=1` observes at most16 process-lifetime consumed
refusals with caller/state/selection/mask/feature/owner, preserving LastError.
Live run20261008-204313_party_root_consumed_refusal_1 passed with all four disk
saves unchanged. Both machines supplied fresh exact consumed-refusal receipts,
retained positive root state/selection/member census and passed typed SAVE gates,
native Cancel and writer-expiry next-load restoration. Before Circle the fixture
returns the root cursor to Items at top0/relative0, then positively verifies
remembered feature0: native Cancel derives that metadata from cursor top, whereas
Cross uses `(top + relative) % rowCount`. Reopening a remembered Party feature
without this preparation is outside the fixture's qualified route.

The live native OFF/restored paths selected Party then Cancelled without Cross;
native Party builder entry is not qualified. Alternate constructors/direct or
save-point ingress, already-open Party pages, companion change, deeper Items,
equipment/abilities/Status and shop/save/command surfaces remain unqualified.
This guard protects the ordinary state0 entry only, not all Party ingress.
The stabilized actual-detour controls now live in PartyMenuAdmissionTest.cpp:
54 process-isolated modes cover native feedback/comparison/root destinations,
mask mapping, Cancel, tracing limits, ownership/read faults, A2 restoration and
seven-hook install/rollback/signatures with the entire synthetic SAVE unchanged.
Boundary doubles stop at native destination PCs; later builder/cleanup side
effects are covered only to the extent of the bounded live route above.


### Bounded Abilities ingress refusal (qualified 2026-10-08)

This slice reuses the seven existing hooks to refuse ordinary pause-root
Abilities feature1 and the Items character page state1 input-5 shortcut while
an exact physically owned sparse-middle `[remote,0,local]` tuple remains installed.
Root input maps through BEEC20 rather than assuming a compact index. The Items
shortcut refuses before native cleanup and its state0F transition; it does not
inspect a deeper character/ability builder. Both new predicates bracket physical
ownership, page and the read-only feature-mask snapshot. Existing root Party
feature3 and secondary-player Items refusal retain their own trace schemas and
budgets. Native Cancel-2/-4 bypass the callbacks; other choices, pages, unowned
or trailing-empty layouts and unavailable reads delegate unchanged. No game,
menu or SAVE data is written, and no SAVE exclusion is added.

Exact opt-in `KH2COOP_ABILITIES_ADMISSION_TRACE=1` produces separately capped
16-per-process `[partyempty-abilities-root]` and `[partyempty-abilities-items]`
consumed-refusal receipts. Each identifies sequence/tick, callback caller,
state, actual compact/input selection, mask, feature1 and owned members. The
installer additionally pins the native accepted-5 decision, cleanup and state0F
request, plus the root feature1 routing. Actual native controls exercise the
original helper, feedback/comparison and action/refusal destinations. Terminal
boundary doubles do not claim later cleanup or builder safety.

Live run20261008-213653_abilities_root_items_shortcut_consumed_refusal_1
passed in458 seconds:7/7 launch-absence checks,4/4 disk saves unchanged and
closure safety PASS. Both machines supplied fresh ordinary-root Abilities Cross
and Items Square/-5 consumed-refusal receipts, positive Cancel and restored
native next-load behavior with the matched native-OFF typed SAVE inventory.
This qualifies only the bounded refusal routes. Equipment state2/-5, direct or
cached initial constructors, already-open Abilities pages and ability toggles
remain excluded. Native companion-history lookups and serialized ownership
need a separate design before allowing ability/equipment changes. Current
live qualification also includes the separately recorded Items and Party slices.
Independent review: party-menus-20261008/codex-review-rev7-fixture04-review3.md.
The compiled rev7 production source is retained byte-for-byte; reusable offline
evidence is127/127 serial CTest,64 native guard modes/1328 assertions and205
audited socketless fixture checks. The landing additionally checks current-main
integration with a fresh coordinated build and serial CTest suite.


### Precautionary ordinary-root Status refusal (qualified 2026-10-08)

The Status guard reuses the existing seventh callback to refuse feature4
from ordinary pause-root state0 while the exact physically owned sparse-middle
`[remote,0,local]` tuple remains installed. Selection is projected through the
low eight bits of BEEC20; caller, page, mask and ownership are bracketed before
refusal. Native Cancel, unowned or dense parties, trailing empties, other pages
and failed observations retain native delegation. Existing Party, Abilities and
Items guards keep their trace schemas and budgets. No game or SAVE data is written.

This is precautionary: native Status construction can mark selected-form
history, and native cleanup copies temporary personal and Drive fields back to
canonical SAVE records. Writer activity does not establish changed values or
corruption, and the same native writers exist without co-op. Character-only
entry has not established idempotence across all six temporary Status aliases,
clamps and cleanup lifetimes. No SAVE exclusion is introduced.

Exact `KH2COOP_STATUS_ADMISSION_TRACE=1` emits at most16 process-lifetime
`[partyempty-status-root]` consumed-refusal receipts, identifying sequence,
tick, caller, state, selection, mask, feature4 and owner. Installation additionally
pins native feature projection, Status routing and the state17 request call.
Controls exercise actual input, feedback and refusal bodies; accepted routing
stops at a state-request boundary double before constructor execution.

Live qualification: `20261008-232911_status_root_consumed_refusal_1` passed
in489.9s. Both machines supplied fresh consumed Status-root refusal receipts
and unchanged protected SAVE intervals. The matched native OFF inventory,
inherited menu guards and writer-expiry/leave plus next-load restore gates
passed. Closure reports canonicalExit0, all7 planned launches absent,4/4 disk
save hashes unchanged and safetyPASS. The canonical report and closure receipt
are the retained evidence; no separate verdict.json was produced.

This qualifies only the bounded ordinary-root refusal. Direct or cached Status
constructors, already-open Status pages, positive character/form viewing and
Equipment ingress remain excluded. Graceful DLL Shutdown remains unqualified
live; the fixture used owned process termination. Reused rev8 offline evidence
is138/138 serial CTest,74 native guard modes/1614 assertions,249 frozen socketless
fixture checks and7 intercepted actual-product launch setup plans. The landing
also requires a fresh isolated current-main build and serial integration suite.
