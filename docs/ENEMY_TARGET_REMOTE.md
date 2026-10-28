# Enemy targeting of remote players (VUH-1515)

## Ordinary mirrored waves (VUH-2193)

`KH2COOP_ENEMY_TARGET_ALL=1` requires `KH2COOP_ENEMY_MIRROR=1` and
uses the mirror families (D14: every mob family except the denylist in
`EnemyMirrorPose.hpp`) for the common `target_search` path.
It is default off and can coexist with `KH2COOP_PARTY_NATIVE=1`; leave the
legacy `KH2COOP_ENEMY_TARGET_REMOTE` flag unset. Native-party room and private
status qualification are unchanged. A clone still needs its own status and a
fresh, living owner pose to become a target.

Headless Ghidra confirms selector `0x3BE2B0` mode 2 always chooses canonical
Sora; mode 0 uses the attention list and falls back to Sora. Changing party or
team flags alone cannot change those player-only searches. The existing
byte-checked syscall replacement runs the native search, then chooses among
the host and both eligible remote players and writes the chosen handle to
enemy `+0xBF8`. It preserves the existing nearest-player policy, hold,
hysteresis and per-clone cap. A valid host-side remote candidate keeps its
native team and collision before the first assignment.

This route does not advertise cancellation authority, register a forwarded-hit
callback, forward damage or veto the owner's local hits. The host-side puppet
remains protected by DamagePolicy `RemoteVictim`; a mirrored attack against the
friend's real Sora stays native through `LocalVictim`.

Attacks that bypass `+0xBF8`, scripted player-only targets, projectiles and
bosses are parked: the common search override does not change their separate
targeting paths. Family-specific targeting qualification remains a live check.
No party/status room expansion or package/profile change is included.

### Axel II (VUH-2912)

The searching-actor check admits type-4 mobs and exact boss identities only
(`enemyclass::SearchTargetBoss`): Thorn (307, type 12) and, since VUH-2912,
Axel II (`B_EX110`, 81, type 3). Before that, Axel II was refused as a type-3
boss, so the host's Axel only ever chose the host's Roxas. Nothing else changed:
the same nearest/hold/hysteresis/cap policy and candidate rules (a downed, stale
or held seat is no candidate), Mirror mode only, native team and collision on
the clone only while Axel holds our write, and zeroed hits on the clone
(`RemoteVictim`). The friend takes the attack natively from its own mirrored
Axel. `B_EX110_SKIRMISH` (2229, Axel I) runs the byte-identical script but stays
out until a live run qualifies it (lead decision, VUH-2912; VUH-2916).

The inventory comes from the installed `obj/B_EX110.mdlx` (`b_ex` BDX, decoded
read-only with the OpenKH `7a3b945c` decoder; no game asset is committed). PCs are
halfword addresses after the 16-byte header.

| Use | Where | Effect on targeting |
|---|---|---|
| `target_search` (1/121), through one helper at PC 5869: `target_search(btlobj_target(self), self, mode)` | mode 0 from `mode_battle`; mode 2 from `battle_start`, `mode_battle_boss`, `mode_revenge_boss` | The common seam. Now chooses among the living players. |
| `btlobj_target` (2/23), reading `+0xBF8` | `atk_slice`, `atk_assault(_revenge)`, `atk_disorder`, `move_target`, `move_various`, `idle`, `footwork`, `btl_normal/hard/super_hard`, and the animation-effect/init triggers (`target_dup`) | Follows the chosen seat. |
| `sysobj_player` (1/3), the canonical player, through the helper at PC 5322 | `appear` facing; `mode_battle`'s leave/culling distance (PC 5792); the `atk_burst`/`_fast`/`_revenge` aim (`obj_step_pos` of the player, PC 6497); `idle_after_rc` | Bypasses `+0xBF8`. Left native. The burst aim is measured live (the host's boss hits while Axel targets the friend) before any seam is added. |
| `sysobj_player` + `obj_attach` (1/18) | `rc_run_up`, `rc_clear_light` | Reaction-command attachment: VUH-2913. |
| `attack_new` (2/10, five sites) | script-created attacks | Hazards and fire: VUH-2914. |

The host logs `[enemy-target] boss-target frame=... objectId=81 target=<k> owner=<slot> mode=<m> valid=<mask>
changed=<0|1> at=<x>,<z> tickMs=<GetTickCount64>` (own budget, 256) whenever a search-target boss changes its chosen candidate, back to
the host's player included, or the set of candidates changes (`valid` bit k: candidate k, 0 the host). The
rewrite log above prints only clone wins, within a 64-line budget shared with every mob.
With `KH2COOP_TRACE_HITS=1`, each seat also logs `[hitwitness] seq=<hittrace seq> hitFrame=<f> hitTickMs=<t> ...
motion=<before>-><after> ignoreHit=<before>-><after> reads=<mask>` four frames after a boss's applied hit lowered its
own canonical player's HP (reads only, 64 lines). A native hit leaves the reaction motion and the ignore-hit timer
(`+0xD70`) running; an HP-only write leaves neither. `reads` has bits 1, 2, 4 and 8 for the motion and timer reads
before the hit and then; a failed read leaves its field at 0xFFFFFFFF or -1.0, and the ledger requires all four.
The line then adds the victim's reactions until it is logged (VUH-2912, the same trace setting): `reactions=<n>
refused=<n> reaction=<kind> reactRequest=<id> reactFrame=<f> reactHooked=<0|1>`. They come from a read-only hook on
`0x3D6FC0`, which applies an actor's pending reaction request (it clears `actor+0x120` bit 25, which the hit entry
`0x3D61D0` sets) and returns the reaction table entry, or 0 when it applied none (the actor refused it, or the
entry's kind is 0); `refused` counts those with a request (the actor update `0x3A89A0` calls it every update, with
request id 0 when there is none). An HP-only write never reaches it. `reaction` is the first applied entry's kind (`+2`), -1 with none; `reactHooked=0` means the hook
is not installed (its bytes didn't match), so no count can be read as absent. The ledger does not use them yet.
After those (lead #76, after vuh2912-hp-09 logged `reactions=0 refused=0` on all 14 Axel hits while each hit's
motion changed): `callsAll=<n> calls=<n> callRequests=<n> callApplied=<n> callIdentity=ok|unread|changed
preArm=none|found|unavailable preArmKind=<kind> preArmRequest=<id>` (`inject/src/ReactionCalls.hpp`). Each witness
counts the calls while it is pending, and no history outlives it (Ione 10d8b494). `callsAll` counts calls on any
actor. `calls`, `callRequests` (a nonzero or unread request) and `callApplied` count calls on the victim. They read
-1 unless `callIdentity=ok`. The inject has no native incarnation id, so the victim is its pointer plus its
objentry pointer (`+0x918`), with the canonical-player bookends at arm and log. `changed` means a call at that
pointer carried another or an unread objentry, or, at the final bookend, the pointer was no longer the canonical
player or its objentry had changed (Ione e33b049c). `unread` means the objentry was unread at arm or at log. An
earlier `changed` or `unread` is never restored. NOTE: a reuse by an actor of the same kind within the 4-frame
window is not detected. These fields are diagnostic only, never reaction or targeting acceptance proof; that would
need a verified allocation or lifetime generation. `preArm` looks back only into `hitFrame`, at the
last 16 applied calls. `found` means the victim's reaction was applied in the hit's frame before the witness
armed, with its kind and request. `unavailable` means the victim's objentry was unread, that frame applied more
than 16 reactions, or another or unread objentry applied one at the victim's pointer. So `callsAll=0` means the
hook never ran in the window, `calls=0` with `callsAll>0` that it never ran on the victim, and `calls>0
callRequests=0` that it ran without a request.
`tools/scenario/boss_target_ledger.py` joins the line to the hit's scope, the friend's driven mirror
hit and the boss-target choice. `net_prologue_mansion_axel_hp.json` is the live check (docs/SCENARIOS.md).

### Dusks hit the friend natively (VUH-2466)

In the prologue's Basement Hall (both Roxases bound at site `tt19-134`), host targeting sends Dusks
at the friend's clone (79 rewrites in run `20261019-171742`). The friend's mirrored Dusks then hit the
friend's own Roxas natively. Run `20261019-192627` had both seats fighting with no stand-in
protection: `[enemy-mirror] hit ... driven=1` on the friend for attacks 18, 17 and 18 (6, 3 and 3
damage), with its own reaction and i-frames.

An earlier reading said the opposite. In runs `171742`–`192214` the friend's mirrored Dusks built no
hits, but every one of those fixtures carried the runner's `"protect": [1]` stand-in aid, which keeps
the friend on team 0. A forwarding change was designed, reviewed (D14.4, r1–r4) and tested on that
reading. It would have forwarded the host's zeroed clone hits for Dusks to the friend as `RemoteHit`s,
with a one-kind-per-swing guard and a flinch rule. It was dropped unlanded once the unprotected run
showed native hits. The reviewed diff is kept at `build/rig/vuh2466/forward-dropped-r4.diff`. Its
measurements still hold if a family is ever found that truly builds no client hits:
- A pose-driven clone never flinches, so it takes a whole combo. Dusk 309's attack 73 landed
  4+4+4+2 on the clone, where a native Roxas takes the first hit or two.
- HP-only forwarded damage has no reaction or i-frames.

### BC Soldier loopback, 2026-10-12

Run [201627](../build/scenarios/20261012-201627_net_enemy_target_all_soldier_1/report.md)
passed using the qualified BC clone leaf and mirror mode. The
[friend clip](../build/scenarios/20261012-201627_net_enemy_target_all_soldier_1/friend_soldier_attack_1.mp4)
shows Soldiers attacking the friend's real Sora. Host search assignments name
owner 1 and object 301. Client native hit records have `driven=1`, with attack
motions 201/202 entered during the mirror run. Friend HP went 24 to 12 to 0;
the host puppet stayed at 24 despite incoming host-side hit records. No
forward, veto or remote-hit records occurred. Four protected files, tracked
files and installed game files were unchanged; owned cleanup completed.

Focused CTest passed 2/2 (targeting and DamagePolicy); the existing production
adapter harness passed 75/75, including all-mode refusal of owner veto/apply
and all-mode coexistence with native party configuration. The first live
attempt stopped during friend bootstrap; the scenario now waits for both
actors and their matching full location before placement.

Limits: this run qualifies Soldier's common search path. Other mirror families
remain unqualified live; attacks that bypass the common slot stay parked.
The initial run separated the players; close contact is covered below. Native-party coexistence
was checked offline; BC is outside that mode's qualified room scope.

### Close contact, 2026-10-12

Run [202545](../build/scenarios/20261012-202545_net_enemy_target_all_contact_1/report.md)
passed with ALL and MIRROR enabled. The host hit a nearby Soldier for 4 native
damage (39 to 35 HP), then was placed 30 units from the friend's puppet and
walked in both directions. The
[host clip](../build/scenarios/20261012-202545_net_enemy_target_all_contact_1/host_contact_0.mp4)
shows no reaction worth fixing or stuck/push loop. Host movement covered about
706 units on the first leg and 648 on the return. Puppet HP stayed 24 after the
swing and contact; no forward, veto or remote-hit records occurred. All four
protected files and installed game files were unchanged; owned cleanup passed.
KeepNative was left unchanged. This is a short Soldier smoke check, not broad
collision qualification. A preceding attempt stopped before the contact actions
on an unnecessary owner-target assignment wait; that wait was removed from the
bounded contact fixture.

Proposed play flags are `KH2COOP_ENEMY_TARGET_ALL=1` and
`KH2COOP_ENEMY_MIRROR=1`, with legacy `KH2COOP_ENEMY_TARGET_REMOTE` unset.
Existing party/private-status qualification still applies. The lead decides
promotion and where these flags belong; no play profile was created.

`KH2COOP_ENEMY_TARGET_REMOTE=1` (exactly `1`; default off). Set it on every machine of the
session. With the flag off nothing is hooked, swapped, written or sent. This flag and
`KH2COOP_PARTY_NATIVE=1` refuse each other (no joint fixture yet): enemy-target's `Configure` and
`PartyNative::Install` each stay off when the other flag is set.

## One damage path per mode

The host's `KH2COOP_ENEMY_MIRROR` decides the mode. The host carries the mode in every
`TargetAuthority` advertisement, so both sides agree.

| Mode | Host | Owner (client) | Who applies a Shadow's hit on the friend |
|---|---|---|---|
| **Forward** (mirror off) | Chooses targets, keeps the clone's team, forwards a zeroed clone hit as `RemoteHit` | Applies `RemoteHit`; cancels its local copy of the family's hits **only** while it holds a fresh Forward advertisement naming its slot | The host's simulation (`RemoteHit`) |
| **Mirror** (`KH2COOP_ENEMY_MIRROR=1`) | Chooses targets, keeps the clone's team. **No forward** | **No cancel** | The mirrored Shadow's own hitbox against the friend's real Sora, natively, so guard, dodge, i-frames and reactions apply |

## Host

- **The seam.** The bdscript syscall `target_search` (bank 1 index 121, `0x4303A0`) is replaced
  through its `.data` entry `exe+0x755B00`. The swap happens only after an exact 75-byte match and
  lasts for the host session.
- **What the replacement does.** It always runs the native selector first. Then it applies the
  policy only when the searching actor is a live, allowlisted enemy (type mob, team 2, HP > 0)
  **and** the native result is the canonical player form (mode 0 or 2, aux 0, naming the
  canonical player).
- **The write.** When a clone wins, the replacement writes `{clone handle, part 0}` into that
  enemy's `+0xBF8`. It saves the native handle and part, and puts both back on release.
- **Why the Shadow follows it.** Its `mode_battle` measures its attack distances to that slot in
  the same brain step: under 300 `btl_attack`, under 600 `btl_short`, otherwise `btl_long`.
- **Native team and collision.** A clone keeps its native team and collision **only while at least
  one tracked enemy holds our write naming it**. Otherwise it stays team 0 with no-collide, as on
  main, so non-allowlisted AI and bosses cannot hit it. While it is kept, any enemy can hit it, but
  those hits are zeroed (`RemoteVictim`), and only allowlisted ones are forwarded.
- **Forward mode.** A hit on a candidate clone from an allowlisted enemy is still zeroed by the
  existing DamagePolicy (`NonLocalPlayer -> ZeroHp/RemoteVictim`). After the zero succeeds it is
  forwarded once to the clone's owner as `RemoteHit`.
- **`TargetAuthority`** (reliable). It is sent on every change and at least every 60 frames.
  - It carries `{room tuple, seq, hostConnectionId, slotMask, familyMask, mode}`.
  - `slotMask` bit *s* is set only while the host is active, the syscall is swapped and slot *s*'s
    clone is a valid candidate.
  - On stop an empty mask is attempted, best effort only: in most stop cases the send is refused
    (the role is no longer Host, there is no session, or the generation is new). The guarantee is
    the owner's 1.5 s age-out. Per-slot changes while the host is active are sent at once.
  - The relay holds advertisements during an event hold (S8), as it drops RemoteHit there, so the
    owner stops cancelling within 1.5 s of an event.

## Owner (client)

- **Applying an admitted `RemoteHit`.** It goes to the canonical local player through the hooked
  native stat funnel: `ApplyStatDelta 0x3D2EB0`, with delta `-damage`, idx 0, no reaction.
- **Refusals.** A `RemoteHit` is refused when:
  - the player is not canonical;
  - a transition is pending;
  - an event or cutscene is running;
  - the player is dead or downed, or HP is 0;
  - the damage is outside `[1, 9999]`;
  - the ignore-hit timer `+0xD70` is above 0. The one exception is the i-frame episode our own
    cancel started: the timer was 0 before the cancelled hit, rose within 2 frames, and has only
    counted down since. That episode never applies during DownedSpike's revive grace. A guard,
    another hit or the revive grace raising the timer again ends the episode (S3/N2);
  - **Stale:** we have not been covered continuously for the last 1.5 s. This covers
    a delayed burst of RemoteHits after a downlink stall, whose local copies already landed (S7).
- **Local cancel** (`DamagePolicy Reason::HostEnemyAuthority`) requires all of:
  - role Client;
  - an allowlisted source family;
  - an advertisement from the current host connection, for our admitted epoch and room, at most
    1.5 s old, in Forward mode, with our slot bit and the family bit set. Its age is checked again
    at the cancel itself, and a pending transition clears the held view (N1);
  - **continuous coverage of at least 1.5 s** (S9), the same clock that gates applying a RemoteHit.
    During a warm-up, or after any lapse, local hits stay native and RemoteHits are dropped as Stale.
    Neither path then applies the attack twice, and the owner is not left immune.

  Anything else, including a missing, stale or refused advertisement, the host flag off, a refused
  swap, missing private status, a stale or ineligible clone, or Mirror mode, leaves the local hit
  native. Apart from the in-flight edge cases under Known limits, the owner is never immune.

## Policy (pure, `EnemyTargetRemote.hpp`)

**Candidates.**
- The local player, if canonical and live:
  - listed (`+0x120 & 0x10080000` clear);
  - not dead (`+0x9B8` bit 2 clear);
  - HP > 0.

  A downed local player is skipped.
- Remote clones 1 and 2. Each one needs all of:
  - an active, player-class puppet;
  - a native status distinct from the local player's;
  - a live actor;
  - a fresh pose (within 30 frames, and not an `AvatarHeld` pose from a stalled stream, VUH-1787) from the same world and room;
  - an owner that is neither downed nor in a cutscene, in slot 1 or 2;
  - a list handle that resolves back to the clone.

**Choice.** Deterministic, at each native search:
- nearest by horizontal distance;
- a 180-frame minimum hold;
- 150 u hysteresis;
- at most 3 enemies per remote clone (the local player is uncapped);
- 64 tracked enemies (when full, the native result stands);
- ties go to the lower index.

## Families

Legacy `KH2COOP_ENEMY_TARGET_REMOTE` allowlist by object id: **Shadow `302` (`M_EX020`)** only.
`ENEMY_TARGET_ALL` uses the mirror families instead (see the top section). The Shadow is verified two ways:
- **Statically**, from its BDX: m_ex020 PC 2275/2290/2078.
- **Live**, in VUH-1515 attempt07, run `20261007-033207`.

Other ids keep the native behaviour. `get player` stays native, because the Shadow uses it only in
its leave check. No pre-update write is needed: nothing else rewrites the slot between searches.

## Private status

A clone must have its own native status. With the flag on,
`privatestatus::EnableEnemyTargetScope()` adds **verified rooms only**. Each room has its exact leaf
row (`SAVE+0x3534+4*world`): today only BC courtyard 05/06, with row `00/00/02/12`.
`KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1` is still required on the host.

The VUH-1513 Roxas-kit row `00/03/02` is not covered. With that kit, a clone outside GoA is never a
candidate. Since the advertisement then never names the slot, the friend's local hits stay native.

## Teardown

| Event | What happens |
|---|---|
| Room change (transition or load serial, or a pending transition) | Assignments are forgotten without writes, because the enemies are being torn down. |
| Role leaves Host, the session ends, or the generation changes | An empty advertisement is attempted (best effort). On the game thread, every slot that still holds exactly our write gets the saved native handle and part. The syscall entry is restored by compare-exchange. A runtime that is killed outright does not reset the world bridge, so the DLL keeps its role and nothing is restored until a graceful end (relay stop or a handled signal) or Shutdown. |
| Shutdown | The syscall entry only. No actor writes off the game thread. Handles we wrote may stay in live enemies' slots; once the clone handle no longer resolves, `0x4319D0` re-selects the player. |

## Known limits (Forward mode)

**Deliberate choices:**
- **The host hits a neutral-input, latency-delayed clone.** The friend's own guard, dodge,
  i-frames, Second Chance and Once More are not consulted. Mirror mode keeps all of these.
- **Damage is computed against the clone's stats**, not the friend's. This is accepted for now.
- **No flinch on the owner** (`reactFlag 0`).
- **A lethal `RemoteHit` in a drive form or summon** takes the native, non-gated death branch, the
  same branches DownedSpike's Kill refuses. That matches a native lethal hit, but it is not
  "downed".
- **Local replica Shadows on the owner are cosmetic for the family** while the advertisement
  names the owner.

**Known windows:**
- **Double-damage window (S5).** When the owner's damage context cannot be captured,
  DamagePolicy returns Native before the cancel is consulted, while the host may still forward.
  This is rare: it happens only while the session or roster is changing.
- **Small immunity window.** When a clone stops being a candidate on the host, the owner keeps
  cancelling until the new mask arrives, about one RTT. It cancels at most until the advertisement
  ages out, 1.5 s.
- **Coverage cutover (S7/S9).** Cancel and apply share one 1.5 s coverage clock. Before it is
  ready, local family hits land natively and RemoteHits are dropped as Stale. After that, local
  hits are cancelled and RemoteHits apply. The remaining edges are the in-flight pairs that
  straddle a cutover. A host swing whose local copy landed just before readiness, and whose
  RemoteHit is processed just after, can apply twice. One whose local copy was cancelled just
  before a lapse, and whose RemoteHit arrives just after, is lost. Both are bounded by one RTT
  per cutover.
- **Host advertisement period.** It is time-based (500 ms), so a low frame rate on the host does
  not cause lapses.

## Fixture

`build/rig/vuh1515-enemy-target-remote-20261007-01/live-fixture-01/`: runner, judge, controls and
an offline dry run. Mode A is Forward and mode B is Mirror.
