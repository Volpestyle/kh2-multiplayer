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
- VUH-1787 stalled-stream hold and resume. AvatarSync now holds a puppet whose owner's stream has been silent for more than 1 s (`staleAfterMs`). It keeps the newest pose, marked `AvatarHeld`, and releases only after `releaseAfterMs`: 3000 ms by default, set with `KH2COOP_AVATAR_HOLD_MS` (1000..10000; 1000 restores the old receiver behaviour only, and the sender's load flag below stays on). Room, cutscene, roster, session and connection exits still release immediately. An owner's own room load is flagged `AvatarInCutscene` by its DLL (on the transition request and while the load is pending), so receivers hide it instead of holding a ghost at the door. A held puppet's velocity is zeroed and it idles after about 15 frames unless downed. Enemy targeting (`CloneCandidate`) and the revive prompt (`TargetCandidate`) skip held poses. A still-visible stream that resumes after the stale threshold blends position and facing from its last rendered pose over 200 ms, instead of interpolating almost directly to the far end of the packet gap. Timeout, room and identity retirement clear that blend. Release hold/relay tests pass 233 checks. Loopback `20261013-053253_net_avatar_stall_1` passes: suspend the owned sender runtime for 2.05 s while both games and viewer run; the native friend puppet stays active, holds with zero positional drift for 65 driven frames, then resumes without release (first step zero, largest resume frame step 5.6 units). The six-second viewer clip shows no drop. This verifies the existing default hold and the new resume blend for this gap; release past 3 s remains intentional.
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

**Ally hits (VUH-1808, `KH2COOP_ALLY_HIT`; default puppet mode since 2026-10-09).** Clones are player-class actors,
and the puppet driver keeps them on team 0 so nothing hits them. Their own replayed attacks then carry
team 0, whose hit mask (`~((1 << team) | 1)` = `~1`) includes the local player's team 1, so a clone's swing
that overlaps the local player creates a native hit. `DamagePolicy` zeroes its HP (`RemoteSource`), but the
reaction still plays (star burst, red portrait flash). Run `20261007-115923` logged 15 on one machine: the
Mickey clone's attack motion 186 lands twice (6, then 2) and reached the host on 8 of 9 swings; Sora's 151
lands once and rarely reached anyone. The local's attacks never hit a clone (bit 0 is never in a mask).
- Unset (the default; the friend package strips every `KH2COOP_*` variable, so this is what players get):
  puppet mode. At `3D2060` it refuses a native-allowed hit only when the attacker (owner `+0x10`, or source
  `+0x14`) is an actor the puppet driver is driving right now: from `BindPuppetDrive` until release clears
  `applied`, and only while the actor is still a cached puppet target (`g_clones[]`, `g_friend1/2Actor`).
  That covers a party-native clone and a friend-slot companion puppet. Victims refused: player-class actors and
  team-1 non-players. Atkp kinds 5/6 stay native. With no driven puppet the hook returns the native answer
  without reading anything. Otherwise the victim is read first, and an ineligible victim (an enemy, a prop)
  returns native before the attack or handle lookup is touched. The local player's own attacks, enemy attacks
  and the client claim path are unchanged. Log: `[allyhit] installed mode=puppet`, one
  `f=… puppet attacker=… verdict=refuse` row per refused (attack, victim) pair. Kept checks are never traced, so
  they can't use up the 160-line budget. Stats (`calls`, `playerPairs`, `nativeAllowed`) count only
  driven-puppet attacks on eligible victims, and `puppetRefused=` is shown. `DamagePolicy`'s `RemoteSource`
  zero stays as the backstop. Offline only so far (`kh2coop_allyhit_test`). Live checks still open:
  - A two-player run where a remote's swings overlap the local player. Expect `[allyhit] ... puppet ...
    verdict=refuse` rows, no `[hit]` line with a puppet as attacker, and no flinch, star burst or portrait flash.
  - Solo cost. The `3D2060` hook now installs on every default launch. It is inert without a driven puppet,
    but its cost and safety outside co-op are unmeasured.
  - Attacks still in flight after release. An attack spawned while driven keeps its team-0 mask after the
    driver releases, and it is no longer refused.
  - Companion puppets. Whether friend-slot companion puppets replay attacks that hit at all has not been
    observed live; that case is covered offline only.
  - Skipped team restore. Release restores the saved team only when membership, metadata, canonical and
    lifecycle checks pass. Otherwise it clears `applied` and leaves the actor on team 0. That actor is no longer
    "driven", so its swings reach the local player natively again.
- `=0` is the explicit opt-out: no hook.
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
constructors, already-open Status pages and positive character/form viewing
remain excluded. Equipment ingress has its own bounded qualification below.
Graceful DLL Shutdown remains unqualified live; the fixture used owned process termination. Reused rev8 offline evidence
is138/138 serial CTest,74 native guard modes/1614 assertions,249 frozen socketless
fixture checks and7 intercepted actual-product launch setup plans. The landing
also requires a fresh isolated current-main build and serial integration suite.

### Precautionary first Items player-row Equipment refusal (qualified 2026-10-09)

The guard extends the installed Items admission callback34EE00 to refuse
ordinary Cross on compact player row0 before native Equipment construction.
It is precautionary: native Equipment cleanup can compact canonical personal
equipment arrays, but the static audit does not prove an actual live value
change, crash or save corruption. Positive Equipment construction and cleanup
idempotence remain unqualified; no SAVE exclusion is added.

The new refusal requires exact owned sparse-middle members `[remote,0,local]`,
actual caller2F62EC and selection0, current state1, pending target-1 and stable
callback phase0 or1. Both read brackets must identify the same compact table,
count2, stored original seat0, raw key1 or14 and nonzero status pointer. Raw menu
keys are not ObjEntry IDs and status pointers need not be actor-private or unique.
The guard reads identification fields only. Unavailable or changed authority
adds no new refusal and continues the existing policies/native callback.

`KH2COOP_EQUIPMENT_ADMISSION_TRACE=1` enables a separate process-lifetime cap16
of `[partyempty-equipment-items]` consumed-refusal receipts, including the actual
pending, phase and row identity. Existing Party, Abilities and secondary Items
streams/caps stay separate; no detour is added and all seven hook lifetimes stay
unchanged. Native Cancel, Stock and existing shortcuts retain their prior policy.

The qualified fixture positively identifies the first Items player row and
independent UI count3/Stock tail2, issues actual Cross and requires a fresh consumed
receipt plus unchanged Items state/model and protected SAVE. Observation phase
pairs00,01 or11 are allowed; the native refusal tail does not reset phase and no
phase0-settle requirement is invented. Direct/cached ingress, already-open
Equipment, edits, Stock Cross and Equipment state2/-5 are outside this slice.
Writer-expiry/leave plus next-load restoration reuse the qualified lifecycle;
graceful DLL shutdown remains unqualified live.

Live qualification: `20261009-004702_equipment_first_player_consumed_refusal_1`
passed, with safety PASS and owned closure confirmed. Both machines positively
refused first-player Equipment entry before construction while the protected SAVE
model and inherited menu/restore gates passed. Retained rev9 evidence is146/146
serial CTest,82 native admission modes/1692 assertions and307 socketless fixture
checks; independent review3 ADOPT is recorded in
`build/rig/party-menus-20261008/codex-review-rev9-fixture06-review3.md`.
The landing additionally validates integration with the landed Status guard
using a fresh isolated current-main build and serial CTest suite.


**Mandatory Limit admission (offline implementation, pending product review).** Co-op initialization requires both native Limit restriction hooks and the verified command lookup before input/entity hooks or the bridge are opened. Failed or partial installation permanently refuses admission in that process; callbacks and originals remain mapped if rollback cannot remove a native patch. This closes the installation gap only: cached Limit execution, active-Limit completion and retained-party lifetime semantics remain unqualified. Existing successful-install restrictions are unchanged.

**Save-point menus remain unqualified.** Entry/Cancel can invoke native object/script callbacks before the runtime-event decision. A concrete room/action issuer and a safe refusal/Cancel contract have not been established; shop refusal does not cover this path. No save-point opening, in-game save or SAVE exclusion is qualified by the menu work. See the retained audit-savepoint-ingress-3a6ce68.md packet receipt.

## Story party rows follow the host (VUH-2342, protocol 24)

The world party table `SAVE+0x3534 + 4*world` (`player, friend1, friend2, world
ally`; `0x12` is empty) is per save, and progress sync leaves it alone. A
story-forced party change therefore happened only in the host's game. The case
that showed it is Ping joining in LoD's first visit: the forced party menu runs
while the friend is held. Run `20261017-024539` (VUH-2341, unwarped) shows the
outcome. The friend loaded Bamboo Grove 08/00 with no companions at all,
because its LoD row was still the save's `0x12121200` (Sora only). The host had
Donald and Ping. Probe `20261017-040248` read the same Sora-only row in both
games before the menu.

The host now stamps its row for the room's world onto the `RoomTransition`
packet trailer (`partyRowValid`, `partyRow`). The stamp is taken when the host
announces the room. A friend writes that row into its own save RAM just before
it queues the load, so the room it builds uses the host's party. Two kinds of
world are skipped:

- TT, HB and BC (2, 4, 5): every machine's two-player clone
  (`playerkit::UpdateBcClone`) rewrites those rows itself.
- Worlds 19 and up.

Only the three companion bytes follow the host. Each machine keeps its own
player byte, and every companion byte must be Donald `01`, Goofy `02`, the
world's character `03` (Ping in LoD) or empty `12`. These are MEMT member
indices (OpenKH `Memt.cs`); forms, Mickey and the clone selector `00` are
refused. The codec refuses any byte above `0x12`. Both sides touch only a
loaded save body (`KH2J` at its start). The friend writes the row only after
it has accepted the transition, and logs `[party-row] client world=… local=…
host=… now=…`.

**Persistence.** Nothing here saves the game. But the row lives in the save
body, so a friend that saves later writes the host's companions into its own
save (the story party the host played). Neither progress sync nor the state
hash covers party rows, so a mismatch is never reported or repaired between
transitions.

**Gap:** the friend has already built the room in which the host's story
changed the party, and it builds the new members only at its next load. Making
them appear in that same room needs the friend to construct party members in
place (the party-swap member build). That is the friend-spawn path, specified
for its owner rather than done here.

## Prologue layout: two Roxases from the first field (VUH-2464)

The Roxas prologue is solo by design: TT's party row is `00/12/12/12` and the
resolver gives `[Roxas 0x5A, Donald, Goofy, 0x819]` (probe `20261018-165208`),
so no friend seat exists for a member write. The prologue layout therefore adds
the missing seat at the native player boundary instead of changing any row,
member or MEMT: it is VUH-2343's Bailey ingress generalized to a checked site
table (`routespawn::MissingPartySites`, `docs/ROUTE_SPAWNS.md`). At a prologue
site the fixed wrapper's caller `3FF727` is about to build the authored
`raw566`; the DLL first builds a direct copy of that record with the remote
seat's profile (Roxas 90, key 14, form 0) as a private clone, then lets the
authored call build the native story Roxas, which takes the canonical pointer.
Remote first, canonical local last; no SAVE row, member table or script
actor is touched.

**Seats and profiles (protocol 26, `SeatState` 53).** Every seat's DLL
declares its profile (`KH2COOP_PROFILE`, default Roxas `0x5A`) as soon as its
runtime is admitted, which is during the New Game opening: the DLL is armed
from the first opening frame. The relay admits a report only for the verified
roster, stamps the owner slot and echoes it to every seat, the author included.
A seat acknowledges once it holds every connected seat's profile; the pure
policy is `common/include/kh2coop/PrologueSeats.hpp`. Only Roxas is admitted
to the prologue layout in this slice; a Sora friend refuses as
`unqualified-profile` until VUH-1512 qualifies it.

**Construction and admission.** The clone is built only with an agreed,
acknowledged two-seat vector and the existing two-player private-status and
neutral-input prerequisites. Each seat then reports `Bound` at its exact
location. In a prologue field every local input is held (buttons and sticks
neutral, mailbox automation consumed) until the profiles agree, this seat's
own construction still stands, and every other seat reported `Bound` at the
same world/room/event program. Reasons are logged as `[prologue] hold
reason=...`: `solo`, `too-many-seats`, `missing-profile`,
`unqualified-profile`, `not-acknowledged`, `local-unbound` (including a
refused or faulted construction), `remote-unbound` and `unpinned-field` (see
Sites). A refused construction
never falls back to a Sora stand-in, a second load or a warp: the authored
local still builds and the seat stays held.

**Friend arrival.** A friend admitted at the title runs into its own opening
exits before the host has announced any room, and client authority blocks
them (the movie room's exit to 2/1 evt56). When the host announces its first
field and that target is a checked prologue site, the friend issues it from
that blocked exit (`warp::BlockedExitToPrologueSite`), with the host's
progress applied first, as VUH-2344 does for a matching exit. It does not see
the Usual Spot or title scenes; it lands in the first field and builds the
host's clone there.

**Sites.** Market Street: Tram Common, each group raw566 plus the absent
raw567/568: `tt07-95` (`TT07_MS101`, package `b_40`, group 88), and its two
same-room event reloads `tt07-97` (event 104, `TT07_MS103`, `b_42` group 132)
and `tt07-99` (event 106, `TT07_MS105`, `b_44` group 176). Both seats logged
byte-identical receipts for all three. The first room change, the cat lock-on
tutorial (event 108), loads the Sandlot as `tt04-76` (`b_40` group 49, the
same raw566 plus absent raw567/568 shape); the host takes it natively and the
friend follows the host's announced room. Further prologue fields are added one
pinned group at a time from the observation-only `[prologue-player-spawn]`
receipt. An open prologue field with no site is held with reason
`unpinned-field` until the seat's first release, so a session that starts at
an unpinned field never plays it solo (the friend's blocked opening exit is
outside the scope: it has no player actor, and member 0 is still Sora until
the first field). After the first release an unpinned field plays native solo
and unheld and is logged once as `[prologue] unpinned field` (the Sandlot in run
`20261018-204359`, before it was pinned), which the fixture asserts never
happens. The last frame of an event program or opening scene reads as an open
field while its load is already pending; it is not playable and counts for
neither. `hold ended without release` only means a held seat left the field,
which every host event does.

**Events.** A host event tears down the field actors, the clone included, and
the friend holds with reason `remote-unbound` until the post-event reload
rebuilds both seats. While the prologue scope holds (TT, native member 0 is
story Roxas), puppets drive only a bound missing-party clone and the unit
slot's friend pointers are ignored: events build their own player-class Roxas
actors, and driving one took the host down (run `20261018-175014`).

**Friend presentation during host events (VUH-2359).** The host's event-hold producer publishes
`active` when its event starts and `release` once it is back in field at the reload, and the friend's
native event hold pauses the friend (Pause menu plus the "Waiting for host event..." overlay) for
exactly that span, then follows the host's reload. Two defects kept it off in the prologue: the
friend's runtime binds the control channel during the New Game title/opening, before any actor
update has registered the game thread, and the input hook treated that as a foreign caller and
aborted the channel for the session (`[eventhold-control] aborted reason=8`); and the prologue hold,
running after it in the input hook, would have zeroed its Start press. The channel now waits for the
game thread instead of aborting, and the prologue hold stands aside while the event hold owns input.
Unskipped, the Armor Shop event holds the friend for about 78 s.

**Clone HP (VUH-2482).** A clone's fresh private record is initialized by the game from this seat's
SAVE (`3C03F0` binds SAVE data for key 14), so a rebuilt clone used to show the *local* player's HP
(run `20261018-195224`: after the Armor Shop reload both clones read 19, while the host was at 20).
Each frame the puppet driver drives the clone, it now writes the owner's streamed HP and max HP (the
avatar pose the HUD reads) into that clone's own private record if they differ, through
`privatestatus::MirrorMissingPartyHp`: game thread only, the bound clone's owned record only (never
the local player's), commits still vetoed, so neither SAVE changes. A local hit on the clone is not
authoritative and is replaced by the owner's HP on the next frame. HP 0 is never written (the
clone's binding requires HP above 0); downed belongs to VUH-2465. Each change logs
`[clone-hp] ... hp=H/M result=1`. The streamed HP itself is read from the player's own status
record (`actor+0x5C0`), not the fixed slot-0 record `0x2A23598`: the clone is built first and takes
that record, so the old capture streamed each seat's clone HP as its own (with the mirror, the two
seats swapped values every few frames in the first run). Normally both are the same record.

**Limits.** Sixteen retained missing-party records per process (the Bailey
budget), so a long prologue session runs out; Roxas+Roxas only; the selected
friend is not represented in the opening's engine scenes (the friend skips
them); the Sandlot is the last pinned field, so the Day 1 walk-through doors
after it are not pinned or run yet; combat, downed and
dual-wield belong to VUH-2465/2466/2467. The clone's fresh private record is
status pool index 79, the address older tools call "unit slot 0 HP"
(`0x2A23598`), so a reader of that fixed address sees the clone; the route
pilot's HP reads do.
