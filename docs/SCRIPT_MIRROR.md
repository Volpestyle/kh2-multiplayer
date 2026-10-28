# Script-call mirror (VUH-2252)

D14 item 3. A client runs no brain for a mirrored enemy (`EnemyMirror.inl`), so
the calls that enemy's script makes on itself never happen on the client: a
boss's tilt, a texture animation, a shadow toggle, and the effects it starts,
moves and ends. The host now sends those calls, and the client replays them on
its own copy.

On whenever the enemy mirror is on (`KH2COOP_ENEMY_MIRROR=1`, the player
default). `KH2COOP_SCRIPT_MIRROR=0` parks it on that machine.

## Calls mirrored

bdscript traps, named as in OpenKH's `BdxTraps.cs`. Tables are at
`exe+0x753490` (`*(+8 * table)`), with 16-byte entries `{fn, flags}`. Each is
called as `fn(u64* args)` from the interpreter `exe+0x41B400`, with any result
in `args[0]`. The call list follows TCM-Murray's world-mirror research
(GPL-3.0, see `research/PRIOR_ART.md`). No code was copied.

**Actor calls** (the first argument is the actor):

| Trap | Table/index | Function | What it does |
|---|---|---|---|
| `trap_obj_set_rot` | 1/1 | `0x42BF60` â†’ `0x3B6560(actor, angle)` | rotation from one angle into `actor+0x680` |
| `trap_obj_set_xyzrot` | 8/2 | `0x537E70` â†’ `0x3B65F0(actor, float4*)` | free rotation (tilt) into `+0x680`, bit 3 of `+0x9B8` |
| `trap_obj_texanm_start` / `_stop` | 1/85, 1/86 | `0x42F820` / `0x42F850` | texture animation on the render object `+0x7B0` |
| `trap_obj_hide_shadow` / `_show_shadow` | 6/45, 6/64 | `0x53E960` / `0x53EE90` | `+0x124` bit `0x200`, or `0x3CD820` |
| `trap_obj_set_scissoring` | 6/65 | `0x53EEB0` | `+0x8C8` bit `0x20` |
| `trap_obj_effect_kill_all` | 1/349 | `0x42D690` â†’ `0x2C6490(actor+0x80)` | ends every effect the actor made |

**Effect calls.** `0x2C6B30` makes an effect from the actor's own effect table
at `actor+0x80`, matched by effect id. An effect instance has its flags at
`+0xC` (bit `0x10000` = killed) and its part at `+0x30`. The part holds pos
`+0x00`, rot `+0x10`, scale `+0x20` and color `+0x30`.

| Trap | Table/index | What the host sends |
|---|---|---|
| `trap_obj_effect_start` / `_bind` | 1/21, 1/87 | the start, with a fresh effect id |
| `trap_obj_effect_start_pos` | 1/22 | a 1/21 start (id, a3, a4) plus a 0/38 position. `0x3B66C0` is `0x2C6B30(id, a3, 1, a4, 0)` plus a pos copy |
| `trap_effect_set_pos` / `_scale` / `_rot` / `_color` | 0/38, 0/39, 0/40, 0/67 | the field it wrote, read back from the part, as a float4 |
| `trap_effect_set_dir` | 0/41 | the rotation it wrote (`0x3B7710`), as a 0/40 |
| `trap_effect_pause` | 0/66 | the on/off value |
| `trap_effect_loop_end` / `_kill` / `_fadeout` / `_loop_end_kill` | 0/44, 0/51, 0/52, 0/85 | the call. A kill ends the pairing |

The safety check for each call is done statically (headless Ghidra, Steam
`9002b2de`):

- Each trap reads only its argument array (one u64 per argument, with the
  handle in the low word).
- It decodes its actor (the argument, then that object's `+4`) or its effect
  through `0x4AD270`.
- It then makes one call or one write.
- Neither the traps nor their helpers (`0x3BAC10`, `0x3B7470`, `0x1A87A0`,
  `0x1A8570`, `0x3CD500`, `0x2C6B30`, `0x2BE630`, `0x2BE6A0`, `0x2BE8A0`,
  `0x2BE8E0`, `0x2BE8F0`, `0x2BED00`, `0x2C6490`) read the running script
  thread `exe+0x2A250B0` or its VM. So each can run from frame code.

The register use was read from the listings. The start traps pass ints in
`edx`/`r8d`/`r9d`. `effect_fadeout` passes its time as a float in `xmm1`.
Replaying a call through its own trap keeps every convention native.

Every table entry, its flags and its full function body are pinned
(`ScriptMirrorPins.hpp`). The host swaps all of them or none, and a client
replays nothing until all of them match.

## Wire: `ScriptCall` (type 49, protocol 19)

Each packet is host â†’ relay â†’ every verified client. It is reliable and
scoped to the admitted room. It is never cached or replayed to late joiners.

- **Payload:** `{room tuple, seq, hostConnectionId, hostFrame, count}` (37
  bytes), followed by `count` entries (1â€“32).
- **Entry:** `{netId, objectId, table, index, u32 effect, u64 args[4]}` (45
  bytes each). `effect` is the host-minted effect id, nonzero exactly for the
  effect calls.
- **Args** are the call's values after the actor or effect, tagged as the script
  passed them: `'@INT'` (`0x544E4940`) or `'@FLT'` (`0x544C4640`). A vector
  travels as four `'@FLT'`.
- **Codec checks:** the codec accepts only the traps listed in
  `scriptCallShape` (`Protocol.hpp`), with their argument counts and effect-id
  rule. Floats must be finite with |v| â‰¤ 100000, and unused args must be zero.
- **Relay checks:** a verified host, the admitted room, a strictly increasing
  sequence, and the host's own connection id.
- **Client DLL checks:** the same host-source admission as `TargetAuthority`,
  plus the admitted host epoch and room.

## Host

The detour always runs the native trap. Before calling it, the detour copies
the arguments the trap reads: a start returns its effect in `args[0]`. For an
effect call, it also checks the effect while it's still live, because a kill
zeroes the part.

- **Actor calls:** the target actor, read as the trap reads it, must be an
  announced host spawn that the enemy mirror drives (`HostEnemyNetId`, D14
  denylist excluded).
- **Effects:** an effect started by such a spawn gets a fresh id, so a reused
  handle can't be mistaken for the effect before it. Later calls on that handle
  are sent while it's still the same instance. Calls on any other effect are
  counted as `untracked`.
- **Key:** netId + group for actor calls: rotation (`set_rot` and `set_xyzrot`
  share one), texture animation (one key per id), shadow, scissoring. Effect id
  + field for effect fields and pause.
- **Repeat:** the same value as the last one sent, or as one still queued. This
  costs nothing. A boss that repeats the same call every frame sends it once.
- **Change:** replaces a still-queued value for the key, or queues in call order.
- **Events:** starts, ends and `kill_all` always queue, in order, and are never
  repeats.
- **Flush:** at frame start, up to two `ScriptCall`s of 32 records each. Values
  become the new baseline only when the packet was accepted for sending.
- **Reset:** on every room change and every session generation change.

## Client

Each record goes to the copy EnemyMirror drives for that netId this frame, with
the same object id.

- **Actor calls:** replayed through the native trap, with that copy's own handle
  (`actor+4`). `set_xyzrot` instead goes through `0x3B65F0` with the four
  floats, because only a script-stack handle can carry a vector argument.
- **Start:** replayed through the native trap. The host's id is then paired with
  the effect it made here (handle, instance, part).
- **Field:** written into the paired effect's part. **Pause and ends:** replayed
  through the native trap with the paired effect's handle. Either way, only
  while that effect is still the same live instance (`stale` otherwise). An
  effect record with no pair and no held start is `unpaired`.
- **Spawn race:** a record for a copy that isn't driven yet waits in the `Hold`
  (latest value per key, up to 180 frames), and later records for the same netId
  wait behind it. All of them are applied once the copy is driven.

Nothing here writes HP, team, collision, motion or position.

## Evidence

Each machine logs one line per second with activity. These are the per-second
message counts the issue asks for:

```
[script-mirror] host sec frame=â€¦ calls=â€¦ repeats=â€¦ replaced=â€¦ queued=â€¦ packets=â€¦ records=â€¦ unmapped=â€¦ notMirrored=â€¦ untracked=â€¦ full=â€¦ sendFailed=â€¦ pending=â€¦ raw=â€¦ offThread=â€¦ effects=â€¦ sent=name:n,â€¦ seen=name:n,â€¦
[script-mirror] client sec frame=â€¦ received=â€¦ applied=â€¦ held=â€¦ heldApplied=â€¦ expired=â€¦ refused=â€¦ unpaired=â€¦ stale=â€¦ faults=â€¦ effects=â€¦ records=name:n,â€¦
```

- `raw` / `seen`: every detour entry before any filter, by trap. This shows
  whether a fight calls the trap at all.
- `calls` / `sent`: mirrored-enemy calls the host kept.
- `repeats`: calls that cost nothing on the wire.
- `records` / `packets`: what was sent.

Live runs (`net_script_mirror_boss`, BC Thresholder `05/0B` btl 72, three
instances on the package defaults):

- **`vuh2252-boss-04b` (2026-10-14 22:24, PASS) and `-05` (22:44, PASS, branch
  rebased on main `788dd05`).** Thresholder's (353) own script makes
  `obj_effect_start_bind`, `obj_effect_start` and `effect_fadeout` calls. Each
  went out as one record in one packet, about one per second at most. Both
  clients applied every record: a start that arrived before its copy was driven
  waited in the hold and was then applied, and each fade-out reached its paired
  effect. Nothing was refused, stale, unpaired or faulted, and nothing crashed.
- **No tilt in this fight.** Thresholder makes no `obj_set_rot`,
  `obj_set_xyzrot` or `set_pos` calls on its own effects here, so this fight
  shows starts and ends but no tilt or travelling effect.
- **Object 92 is not mirrored.** Most trap entries came from object 92, which
  is not an announced spawn. Its effect starts and one script-moved projectile
  (11 `effect_set_pos` and 11 `effect_set_dir` in one second) were counted as
  `unmapped` / `untracked` and not sent. That's correct for an actor whose
  client copy keeps its own brain.
- **Earlier runs.** `-01` and `-03` (two instances) crashed the client in the
  pause menu (VUH-2289, fixed separately). `-02` logged no `host sec` line,
  before the `raw` counter existed.

Offline: `kh2coop_script_mirror_test` covers the codec, relay forwarding and
refusals over loopback ENet, the send-on-change and hold rules, the effect
registries, and checks that the DLL's trap table agrees with the wire allowlist.

## Next slices

1. **Room:** `bg_hide/show` (1/36, 1/37; thunks to `0x1629B0` / `0x162A00`
   with an int id) and lights (1/181â€“184, 1/251; light 0 is the fixed slot
   `exe+0x2AE7B98`), when an enemy's script changes them.
2. **Effects started on another actor:** `obj_effect_start_bind_other` (1/149)
   needs the other actor mapped as well.
3. **Missions:** done separately as a state mirror, not a call mirror
   (`MissionState`, [MISSION_MIRROR.md](MISSION_MIRROR.md), VUH-2342).

## Reaction commands and actor ownership (VUH-2569; reusable VUH-2418 slice)

This extends the same reliable, ordered source-netId ScriptCall stream. The host
keeps all BDX workspaces, program counters, attack receivers, coroutines and
phase decisions. Native addresses never enter these descriptors. The current
implementation still needs the full Twilight Thorn live qualification described
in [TWILIGHT_THORN.md](TWILIGHT_THORN.md).

Script-created attacks use the existing resource pairing model with native 2/10
start, 2/11 radius, 2/12 position, 7/14 reaction offset, 2/51 RC and 2/13 free. Exact
native constructor/destructor hooks retire pairings before allocation reuse.
Collision resources never silently evict another live pairing; bounded deferred
retirements survive queue saturation. A free cancels a held, unborn start and
can retire an existing resource after source death.

Each attack/command window receives one host-minted 64-bit occurrence. Arbitration
chooses the nearest eligible living, fresh, capable seat in 3D, with stable seat
index breaking ties. The window latches source, command, epoch, owner slot and
connection, expires after 180 game frames and cannot be renewed by repeated
native offers. Observed downing, stale ownership or replacement permanently
revokes that occurrence. Completion reserves consumption before native entry.
Packet 55 `ScriptReactionCompletion` has 53 payload bytes (room, sequence,
requester connection, occurrence, resource, objectId, netId, command, slot).
The relay stamps the actual admitted sender's slot and sends only to the host;
the host rechecks current scope and winner before executing its local receiver.
Native Triangle RC always takes priority over a revive hold.

Normalized table 255 operations carry only these values:

| Index | Descriptor |
|---|---|
| 1 | occurrence low/high, command, owner slot |
| 12 | immutable owner slot and connection low/high |
| 2 | actor role begin: slot, connection low/high, bounded 60-frame lease policy |
| 3 | captured native motion id and original float blend parameter |
| 4 /8 | host player-role position/facing or motion/time |
| 5 | resident source-camera fourcc and player/source target choice |
| 7 | command mode (1 general override, 2 captured motion), command, flags and priority |
| 9 | immutable owning role resource plus native player attack arguments |
| 11 /6 | heartbeat /role end |
| 13 /14 | native owner attachment flags/bone /detach |
| 15 /16 | actor birth /end: occurrence low/high, child netId, child object 317 (header: parent netId, object 307; effect = child netId) |

Script-born children (VUH-2605, protocol 37). Twilight Thorn's script creates its Creepers with
`sysobj_appear` (1/0). The host observes that trap (pinned body, original always called) and records
only an exact 317 return made inside the announced 307's own executing script. It publishes 255/15
only after its own census has announced that exact child, so a client always gets a child manifest it
can wait for. The record repeats on a 60-frame heartbeat while the child is still that announced enemy
and its parent is still a live announced enemy, and 255/16 ends it once. A child netId is published at
most once per room: a return whose census row still names an earlier child (a reused address, a delayed
row) waits for a fresh netId and is dropped unpublished if none appears. A dead parent begins nothing,
but its published children still end; the relay admits 255/16 under a dead parent as teardown, like
attack_free, and still refuses 255/15. Rows are tagged with the session generation and reset when the
host stops or the generation changes. Occurrences never rewind. A client never replays these records as
native traps.

The client owner (`ScriptBirthOwner.inl`) keeps each 255/15 as a receipt in the SpawnEmission ledger. Once both
the parent's and the child's own manifests are current, it builds the child once through the native factory at
the manifest point, under the resident-resource lease (option C: its initializer runs natively, its brain is
host-only). Ownership rules:
- Nothing is built unless the type-4 mob class brain hook is installed first.
- Every non-null factory return is owned, whatever the lease release reported.
- A record pins the census row its body takes when first listed. It retires when that row is no longer listed,
  never on the retained `byActor` index. A different row at the same address is a replacement body and gets
  neither the old witness nor ownership.
- A body whose own handler cannot be hooked is held as a last resort. The entity hook skips its whole native
  update (it has no VM before its first update), it is hidden with the native obj_hide, and every hold is
  logged ("client HOLD"). A held address is quarantined until native room teardown, fail-closed: it is keyed
  by address only and nothing about the body is read, so a same-class body later at that address in the same
  room stays held too. No session, generation or role change releases it, and neither does a transition request
  or a pending load (the old room's bodies still exist). Only a completed native room load (the load serial)
  does. Holds use a fixed array of 16 that never allocates. After any hold, no further
  script child is built in that room, and no child is ever built unless every owned, unheld child could still
  take a hold slot. Whether a held, never-updated body keeps any collision is not proven; the class-hook gate is
  meant to make holds unreachable.

Actor role resources use the high bit, distinct from attack/effect IDs. Source
and selected actor handles, status roots and object-entry roots stay local and
must remain current. The host also requires the exact native coroutine; the
owner refuses a replacement coroutine. A fresh room/epoch/connection and a
one-second heartbeat lease bound every role. Retirement releases only that
role's matching capture/attachment and collision resources; it cannot reset a
replacement actor, coroutine or unrelated attachment.

The selected player's owner replays native capture, camera and command helpers
against its own player and locally bound source. Cameras require a matching
resident BAR asset. Current native attachment qualification is flags 16386,
source bone 5, or, for Axel II's role only, flags 16386 with Axel held at bone 1
(protocol 40, docs/AXEL_REACTIONS.md); owner callback/data arguments are zero. The host alone retains
its BDX attachment callback. Native update `3B2340` clears the script command
slots after every reaction refresh, so a prompt lives only on updates that
re-registered it. A client's own per-frame native captured-motion registration
therefore passes for exactly one case, and only on the captured entry (its own
player's captured motion): the host-offered window of that seat, on
its current connection, live in this epoch, unconsumed, for the same command,
with the role and capture current and the seat not logically down. Every other
client registration is still refused; only the host mints, extends or re-owns a
window (VUH-2617). Op7 replays alone left the friend's REACT_CMD on alternate
updates and dropped presses into the native refusal path. Boss hide/show uses ordinary mirrored actor calls,
separate from shadow visibility. Native owner-created scripted strikes use the
existing BuildHit/D4 claim path; damage on the host's remote clone remains zero.
No client BDX workspace is copied or independently executed.

Actual-owner and spectator playback distinguish the source animation bank from
ordinary player motion. Only the latest authored capture ID qualifies as borrowed;
explicit idle0 returns to the player bank. Unknown post-capture IDs refuse.
The shared native capture leaf preserves blend bits, then corrects the clock
separately after rechecking scope, allocations, handles and requested animation
ID. A consumed RC blocks motion repair and clock writes. Matching ID is not
proof of a nonnull animation asset; the installed Thorn motion inventory remains
the qualified scope.

A spectator retains a separate current network-clone visual binding; it never
becomes the role's command actor. Native D48 must be zero, source/actor roots and
owner connection must match, and the avatar must be live in the same room.
Ordinary clone transform/team/collision maintenance continues; source-bound
visual pose runs last. Callback6/16/17 and command routing suppress this exact
visual target, including guarded cleanup reentry. Cameras and commands remain
on the actual player owner. Player hide/show and temporary helper-model parity
are outside this visual slice.

The existing15-frame heartbeat refreshes idempotent Begin, capture and current
attachment state, recovering an initial packet refused before room arrival.
It does not restart a missed camera or a consumed command. A per-seat resource
floor scoped to the authenticated transport prevents a retired role from being
reopened by a later heartbeat. Identical authored capture retriggers within one
role are not distinguished from refresh; Thorn's inspected distinct inputs
bound this policy. The animation extension still requires live qualification.

All additional native trap bodies and helper boundaries are pinned. Installation
requires all owner trap slots, both native command helpers and the reaction dispatch hook; failure
restores owned slots and disables created hooks, retaining in-flight trampolines
until global teardown. Client BDX damage/motion callbacks remain suppressed even
before motion binding for exact Thorn 307, preserving the one-brain rule.

Native entity cameras (kind 11) keep the admitted role's gameplay scope. A local
proof binds the exact context opened by that role to its resident camera asset,
source transform, source/player roots, coroutine, session, room, load and roster.
The existing gameplay gate, EventHold observer and avatar capture use that same
proof. Menu, frozen and loading guards still apply; a replacement story context
uses the ordinary event hold. This adds no wire record.

BDX stores native resource handles in 32-bit fields. Native work values carry
`@???` (`0x3F3F3F40`), and some callers use `@INT`; both can look up an already
registered resource alongside `@ADR`. Native allocation and owner checks must
still pass. Capturing the pair before the call and checking its incarnation
afterwards protects native reentry. Unregistered values never reach native
handle decoding. These tags remain forbidden on the wire.

Pinned wrappers consume scalar low words independently of workspace tags. The
host normalizes only their proven scalar positions: attack start/radius/RC/strike,
effect start/pause/fade. Vector fields come from checked post-native float4 reads;
actor, resource and vector arguments are never normalized as scalars. Finite
float, range and RC command checks precede clearing a native attack's prompt.
Bounded refusal logs expose invalid values. Live02 exhausted the friend's native
512 KiB effect arena when starts arrived without lifetime calls; live04 exposed
the actual work tag and the separate scalar boundary. Live05 delivered1060 effect-fade and1066 attack-free records without an
exception; native pool occupancy recovery remains unmeasured.

Captured animation commands can enter either native helper (`3B25C0` or
`3B2850`) using the canonical player's controller, even when their animation
actor belongs to another seat. Pinned native caller pairs identify this producer;
the already registered target source selects one current captured role. Ambiguous,
stale or unsupported targets refuse. A qualified captured command uses mode2 and
its owner's local captured helper, always with native category1. General BDX
commands keep VM-first routing and mode1. Mode belongs to the occurrence identity;
changing semantics retires the old window. The wire carries only a checked source
root, so nonroot target parts refuse with a diagnostic. Callback PCs and native
target memory remain local.

Role retirement revokes commands immediately. If its exact registered entity
camera is still current, a small local receipt waits for authored native playback
to finish before releasing the old capture or attachment. Thorn's eight camera
assets end on finite native frame counters independently of BDX or reaction
input; the longest is 608 authored ticks (about 20.27 seconds while unpaused).
No global event cancellation or timeout hard-free is used. A replaced scene,
changed load or changed native roots abandon that receipt without native writes.
A seat with pending cleanup cannot admit a new role; completion is reported
only after native context and timeline idle are observed.

Progress sampling uses the same registered camera proof for its event terms.
The identity-only scope check does not require the sampler's mutable readiness
flag: each progress tick clears that flag before checking gameplay. Requiring it
inside the camera proof created a permission cycle in live03 and retired the
role on the first camera frame. Ordinary gameplay admission still requires a
fresh progress sample, and all menu, frozen, field and load guards remain.

Thorn deliberately hooks the selected player into another native coroutine while
one entity camera may still be playing. A newer Begin can inherit that exact
camera and its verified capture/attachment only for the same seat, connection,
source, player, load and current admitted scope. The host omits the old End for
this continuation; the owner retires the old role's collision resources without
ending the continuing camera. Reaction occurrences and commands remain fresh.

The native timeline destructor sets its state to zero before the separate event
updater clears the entity-camera context. Exact registered camera proof covers
known timeline states0..4, including this closing interval. A changed context or
unknown state still fails the proof; no idle timeline alone grants gameplay.
The actual progress-tick regression covers this ordering, script publication
and census admission, with replacement-context and invalid-state controls.

The native RC cache adapter observes refresh408520 and its last winner408BD0,
then validates the immutable host-owned occurrence at preaccept3B22D0, pinned
to caller3B2340. A fresh selection records all native roots, source/resource
incarnation, seat/connection, scope and four logical TARGET words. Query and
preaccept are idempotent and non-consuming; only the matching native inline
callback consumes the existing occurrence. An unchanged cached HUD command
does not renew that proof. Other native reactions retain their normal path.

Revival and the read-only prompt observation use the same actionable result.
The existing version7 prompt fields expose Triangle ownership without new
network fields; raw cached commands remain diagnostic. The ground callback
uses native TARGET's no-part sentinel UINT32_MAX. Per-command traces retain
continuing late-fight samples of selection, native gates and active request.

All three additional hooks and their caller/body pins must qualify before
this provider registers. Their independent rollback retains the existing owner
traps and dispatch suppression; a proven client source's RC callback refuses
while qualification is unavailable. The rehearsal checks installation on both
seats before the route. An accepted action interrupted without its callback
conservatively fences later mirrored reactions in the same scope/load. Only a
qualified callback or scope/load retirement releases that fence; no blind cache
clear or action relabelling is used. This liveness boundary requires live proof.

Local reaction validation uses the authenticated local seat's canonical actor
and native live roots. The targeting candidate array belongs to the host and
cannot establish a client's local liveness. Shared actor-owner seat selection
keeps the explicit canonical actor for the local seat and uses nonzero host
candidates only for other seats. An unused third candidate with default slot0
cannot erase the host player after an occurrence is consumed. Build17 contains
the regression. Full-source live09 completed opening and ground reactions for
both owners, but stalled at a later friend-owned captured426. The trimmed
build19 passed202/202 tests and its separate exact-binary live10 completed
opening297/298/299 and ground432 for both owners. The friend milestone joins
the host offer, client queued receipt and host completion for resource67/
occurrence31. An earlier queued28/14 has no host completion, so this does not
qualify every window. Captured426, the kill and terminal flow remain unqualified.
See [the limited reaction result](testing/vuh2569-reactions/README.md). Completion traces
use immutable claim snapshots across callback reentry; a client queued receipt
must also join a host completion before it counts as an applied reaction.

### Completion diagnostics (VUH-2569)

The next full-fight candidate traces packet55 through its existing DLL ring,
runtime, network, relay, inbox and host callback boundaries. `[script-rc-hop]`
records use immutable occurrence/owner/requester/sequence identities; callback
state is copied before native reentry. They report the first evaluated refusal
without repeating a stateful proof, consuming twice, clearing a cache, retrying
a command or extending a lease. The wire change to31 belongs to restored TT34
terminal semantics; diagnostics add no packet fields or native ABI changes.

Each stage has a separate process-lifetime budget and emits an explicit
`budget-exhausted` marker. Absence after that marker is inconclusive. Reliable
inbox retention is reported as deferred, and submission/forward attempts are
distinct from native receipt. A local queued receipt alone never proves host
completion. Native cached/active TARGET traces remain read-only. Pilots and
their actionable-RC observation are unchanged for this diagnostic attempt.

Live11 joined all eleven remote completions through host application: nine
succeeded; two late captured426 replies found that the host role had already
retired. Those refusals precede the producer-frame check. The trace did not
saturate, but it did not record the predicate that retired each role. See
[the live11 result](testing/vuh2569-live11/README.md).

The subsequent approved candidate adds one immutable first-failure retirement
row at the existing owner-frame and scope-reset checks. It records only values
read by those checks, preserving their lazy order and both distinct D48 reads.
Unevaluated checks remain distinguishable from failures; reset-entry rows do not
claim to identify every later cleanup. The separate retirement stage has its
own 256-row budget and saturation marker. No retry, retired-role replay, cache
clear, lease extension, native ABI or network change is added. A zero D48 alone
does not establish a normal coroutine end.

Live12 (`20261021-040907`, exact build24, single-PC loopback) exercised these
rows without exhausting a stage budget. All six remote completions reached
host application; four succeeded. Captured426 occurrence63 passed the friend's
actual native acceptance and callback, consumed locally and arrived at the host.
The host had retired its role two frames before applying it: the first D48 read
returned zero against the saved nonzero coroutine. Scope capture succeeded and
the cached role still matched its disposition; selected-actor, host-active and
second-D48 checks were unevaluated. The later refusal was role-missing, before
the producer-frame gate. Ground432 occurrence74 separately found no occurrence.

This establishes the failed lifetime boundary, not its native writer or an
incorrect retirement. Native scheduler completion, hook replacement and the
BDX motion-end/capture-ID exits remain possible until their provenance is
qualified. Generic puppet motion252 before retirement does not identify the
writer. No retired-role replay or lease change follows from these receipts.
The run ended without a kill; terminal/reward/onward qualification remains open.

### Native lifecycle observations (lead40 and43)

The lifecycle adapter observes the actual existing1/14 motion-end and
1/238 capture-ID calls and adds one diagnostic hook at cancel leaf3D6F80.
Full native bodies, the scheduler callsite, the five-byte entry relocation and
the generated trampoline are pinned. Originals execute once with unchanged
arguments, native errors and exception propagation, including unrelated calls.
Installation refusal rolls back only these observers, preserving the existing
six owner traps. Build25 passed the full202-test suite after independent review and installed on
both seats in failed live13. No new wire, retry, replay, ownership or pilot change
is added; installation alone does not qualify complete diagnostic coverage.

Pre-state qualification requires one exact registered actor/source incarnation,
admitted scope and saved VM equal to native D48. Immutable revoked cleanup
metadata is labeled separately and grants no authority. Post-state checks role
replacement and allocation roots before cached reads; unavailable state stays
unavailable. Ambiguous matches refuse association. Invalid per-fiber cleanup
context permanently drops diagnostic attribution while originals pass through.
A returned capture-ID is metadata from the actual1/29 call, never an extra query.

Separate process-lifetime row caps are2048 for lifecycle-query,1024 for
lifecycle-cancel and512 for lifecycle-capture, each with one saturation marker.
Existing stages retain256. Query rows emit changed identity/thread/native
result/pre/post state; cancellations have their own budget for late blocks.
Absence after refusal or exhaustion cannot exclude other native writers. The
acceptance fixture must require the exact successful installation on both seats
before route input. Build24/live12 does not qualify this candidate.

Live13 observed three admitted captured426 cancellations from the pinned native
scheduler caller before their first zero-D48 retirement reads. It does not prove
why the authored program ended. All associated query rows were1/238; the old
argument equality filter missed1/14's distinct operand shape. Absent rows do not
prove that the native query was absent or that motion-end returned false.

For the next human kill attempt, lead43's reviewed correction associates1/14
only through the unique currently executing admitted VM. It rechecks the same
registration/incarnation, scope and saved D48/VM roots; ambiguous or changed
execution refuses association. Raw query input/read availability and actual
native output are logged. The target is explicitly unresolved, with no alias
decoding/dereference, extra getter/hook or authority.1/238 keeps its existing
admitted-handle association. Originals, native errors/exceptions, budgets and
other observation paths are unchanged. This corrected source requires its own
build/full suite and runtime evidence; live13 cannot qualify it retroactively.
