# Twilight Thorn across seats (VUH-2569)

Design approved by Clankie on 2026-10-20 (lead message 2); inventory baseline
`fec5d8e`, native implementation based on `edf2f2d`, rebased onto docs-only main
`e053a9e` (combined wire v30). This records the inventory, ownership contract
and limited live qualification below. Live runs are single-PC, two-seat
loopback, per the lead's instruction.

The original v30 reaction-only landing deferred native mission4/16 observation
and suppression, event315 qualification and exact307 boss-leave fallback.
Build19/live10 qualified that trimmed binary's opening and both-seat ground
reactions. The v31 cohort now restores the reviewed host-only terminal bundle
and includes logical-down XZ holding, lifecycle observations and safe-field
recovery. Build34 passed the full203-test suite. Historical attempts below retain
their own binary and result boundaries.

Bot01 (`20261021-113329_net_twilight_thorn_bots_1`, sealed build28, single-PC
loopback) achieved a native host kill and three natural friend-to-host revives,
but failed shared exit: the friend's expired control lifetime rejected315 and
left it behind. Its original safety is not PASS: a movie mtime changed, although
the bytes match the installed depot reference. [VUH-2628](https://linear.app/vuhlp/issue/VUH-2628)
tracks the recovery boundary. Build34/recovery04 now proves a fresh authenticated
rejoin in empty FM00 with a dormant tutorial script; see the
[bounded result](testing/vuh2628-recovery04/README.md). It does not prove the
parent's shared kill, rewards, cleanup or onward movement. `VUH-2617` holds the
bot series; the sealed human14/build28 packet remains an optional baseline.
VUH-2569 stays open for the kill and the captured426 cause.

Lead22 split script-created Creeper parity into
[VUH-2605](https://linear.app/vuhlp/issue/VUH-2605/mirror-twilight-thorns-script-created-creepers).
The existing native cull predicate executes child BDX before its authority
check; a replica that skips initializer BDX needs a pre-cull fence and qualified
native disposal first. Reviewed resource/factory/typed-ledger boundaries remain
uncommitted; no child construction is enabled and the global317 deny remains.
Creeper population desync is a known gap on VUH-2569, not a kill-proof claim.

The current full-fight cohort uses protocol v31, because limited-RC v30 peers
reject the restored terminal semantics. Its original host-only terminal bundle
observes signal4, mission4/16 and the
authored315 event. Clients never blindly replay4/16. The full type55-only
diagnostic scope is approved to locate the captured426 failure without changing
wire, native ABI, retries, leases or pilots. The limited landing evidence above
does not qualify the terminal bundle. The later failure chronology below is
historical; it does not override the current status above.

## Installed evidence

Read from PC1's installed packages without launching the game or changing saves.
Local extraction receipts, decoded scripts and the previous attempt are under
`.local/thorn/` in the VUH-2569 worktree. Only findings are committed; extracted
game assets remain local.

| Asset | Evidence |
|---|---|
| `00objentry.bin` | Object 307, `B_EX100`, large boss type 12. Existing EnemySync census only admits types 3 and 4. |
| `ard/us/tt34.ard` | Battle program 157: party opcode 15 argument 0, mission opcode 21 (`TT34_MS304`, id 181), spawn `b_40`. Its single group, header 13, contains object 307 at (0,0,800), serial 2, and player raw566 at (0,0,-860), serial 3. |
| `tt34.ard` events | Programs 1/267 reference scene 314; 268/157 reference 315. Program 157 is the boss defeat event program. Preserve the native event descriptor and its authored exit; do not fabricate an exit tuple. |
| `obj/B_EX100.mdlx`, `b_ex` BDX | 33,910 bytes; script `b_ex100`, work 5,216 bytes, stack/temp 512 each. Triggers: reaction 11, animation-effect 16, animation-callback 17, damage 6, main 1, init 0. 39 registered actions. |
| `msn/us/TT34_MS304.bar`, `ms_b` BDX | Script `ms_boss`: signal 4 locks the mission and selects state 2; main state 2 invokes `mission_dead_boss` (4/16) with argument -1. Other states dispatch 4/3, 4/11 or 4/22. |
| `03system.bin` `cmd` + `msg/us/sys.bar` | 297 Key Counter, 298 Lunarsault, 299 Break Raid; 345/426/432/666/667 are Reversal variants. All eight use player receiver 0. |

The extracted ARD SHA-256 is
`286a3bba6311910013e17aaa707f839c83bc76a830cf87cf3628fc0cb432b40e`;
mission `255c50ffd283a981cb635e294238e255ca37299b4e09edd900ae11ffcca26dce`;
model `271ffe64db2f7d0c23669ab3c44befde9bf1f222bdd18c1c4661f8fa28f22c2f`.
BDX PCs below are halfword addresses after the 16-byte header, as in OpenKH's
BDX decoder. The reused local decoder is OpenKH `7a3b945c`; command layout and
message encoding are documented in
[Cmd.cs](https://github.com/OpenKH/OpenKh/blob/master/OpenKh.Kh2/SystemData/Cmd.cs)
and [InternationalSystemDecode.cs](https://github.com/OpenKH/OpenKh/blob/master/OpenKh.Kh2/Messages/Internals/InternationalSystemDecode.cs).

## Native phase inventory and ownership

Names and entry PCs come from `trap_act_table_add` (1/6) in the boss's BDX.
Visual names group those actions; they do not assert a fixed order or a new
replacement boss state machine.

| Phase / native actions | Owner and friend behavior |
|---|---|
| Admission / intro: `appear` 6149, `battle_start` 16284, `event` 6466 | Host releases the existing party barrier and owns event progression. Add the exact tt34-157 ingress to the checked prologue-site table after both-seat native receipts match. Both living players remain combat participants. Harness dismisses first-hit combo/tutorial boxes on both seats with Cross, including boxes whose menu flag is zero. |
| Ground combat: `btl_normal` 15896, `btl_hard` 16094, `btl_super_hard` 16210; `idle` 8883, `idle_thorn` 8910, `footwork` 9113, `move_side` 9144, `move_side_thorn` 9257 | Host alone runs AI and chooses target/phase. Extend existing enemy classification to the exact known large-boss identity, then reuse manifest/netId, mirror motion/pose, host HP, hit claims and deaths. Do not enable all type-12 objects (the type is also called WORLD_MAP in older documentation). Native hit claims retain their existing deduplication and owner validation. Existing target_search adapter considers living, fresh local/remote actors. |
| Ground attacks / projectiles: `atk_brush` 9340, `test_head` 9371, `atk_ground` 10208, `atk_final` 13394 | Host owns attack creation and random positions. Reuse ScriptMirror's host effect IDs and ordered starts/fields/ends; extend its checked call shapes for the calls this boss actually needs. Script-created attacks need corresponding trigger identity and owner routing, not a second boss brain on the friend. Each attack uses exactly one owner-damage mode; do not combine native mirrored damage with RemoteHit forwarding. |
| Thorn cinematic/platform sequence: `atk_thorn` 15376, `rc_thorn_wait` 7683; `_1` 7700 / `_1_s` 7759; `_2` 8213 / `_2_b` 8264 / `_2_s` 8382; `_3` 8439; `_finish` 8615 | Host owns the action progression and success/failure branch. The script obtains the canonical player at PC1075, moves it, starts cameras, attaches/detaches it, and starts the three player RC commands. These operations must resolve a network seat to its native actor on each machine. Replay the chosen actor's scripted role on its owner; other humans stay in the arena with their own camera/control whenever native scripting permits. Do not broadcast host player handles or attach every player to the boss. |
| Key Counter / Lunarsault / Break Raid: callback cases 297/298/299 at PC34, common success PC156 | Treat each offered window as one host-owned trigger/sequence, with one eligible living winner: nearest to trigger, stable seat tie. Latch winner for the multi-step role; a downed/disconnected winner retires its window and releases attachment/control. Owner executes native command and sends completion for the same trigger; host applies the callback once. Non-winners cannot advance the boss. Native RC input cancels revive hold. |
| Reversal: callback 345 ->167, 426 ->2347, 432 ->2425, 666 ->2908, 667 ->3451 | These resolve `target_attack` (1/166) then `attack_rc_receiver` (2/52), so the trigger can be a script-created attack, not just the boss actor. Host arbitrates each attack occurrence using the same nearest/living/stable-seat rule. Mirror the paired visual effect and route the winning response to the actor owner. Boss/netId alone is insufficient to identify repeated projectiles. |
| Death (deferred design): `dead` 8759 / helper 8766 -> signal 4 -> mission state 2 ->4/16 | Not implemented by the reaction-only slice. The future design is: host alone emits victory and event315. Extend MissionMirror's end observation/suppression for this exact boss mission; do not blindly replay 4/16 on a brainless client. Existing Shan-Yu evidence shows that can strand loading. Retire the client copy via the checked boss-leave path, then let existing host-led event/warp/progress machinery deliver the authored post-fight scene and exit once. |

The boss also has `mode_battle` 6672, `mode_revenge` 6935,
`mode_battle_boss` 6977, `mode_revenge_boss` 7043, `revenge` 7109,
`near_wait` 7505, `near` 7583, `rc_restart_test` 7818, `freeze` 6449,
`leave` 6389, `idle_time` 6486, `talk` 7168, `info1` 8859 and `info2` 8871.
All decision actions remain host-owned, including test/debug actions if reached.

`attack_set_rc` (2/51) is called with Reversal 345 (800/500 bounds),
432 (300/300 and 600/300), 666 and 667 (600/300). `command_override` (1/165)
is the direct player-command path. The main cinematic combines `obj_hook`,
`obj_camera_start`, `obj_attach`, `obj_detach`, player motion capture/sync,
hide/show and movement. At the inventory baseline, ScriptMirror covered effects and rotation, but
none of those actor-owner or attack-trigger operations. Adding object 307 to the
census alone would disable the friend's independent script without providing them.

## Implementation boundaries and checks

Reuse `EnemySync`, `EnemyMirror`, `EnemyTargetRemote`, `ScriptMirror`,
`MissionMirror`, and the established room/event/seat bindings. VUH-2418's generic
RC/actor-owner routing is still Backlog; it is a missing capability, not an
existing API. Build the minimum reusable routing inside the script mirror:
host-minted occurrence + trigger sequence + admitted owner slot/connection,
bounded lifetime, room/load/session scope, owner acknowledgement and consume-once
completion. Never transmit native actor/effect/attack pointers. Receipt loss,
replacement, death and reconnect cannot replay a previous command or retain an
attachment. Unknown/unqualified calls refuse with a specific diagnostic.

Start with a focused census/classification regression and real native-owned-memory
tests. Then extend the codec/relay/ScriptMirror tests for trigger pairing,
nearest living selection, seat ties, duplicate completion, changed actor/epoch,
timeout, downed and native-RC-over-revive behavior. Deferred mission support
will need tests for the single host4/16 observation and exactly one post-fight
release before it returns to the product. Native trap
replays require static ABI/thread-context checks, pinned bytes and independent
D14.4 review before live execution. No BDX workspace copying or guest AI fallback
is proposed as a solution to phase synchronization.

Use a checked-in FM-00 rehearsal scenario through real doors. Film both screens
through the full kill, caption phase/RC owner and victory from receipts, and
retain parity/hit/RC/mission samples. Require both-seat contributions, incoming
hits, arena downed/revive, one victory, the same post-fight event and onward
control. Full-party down follows VUH-2465 native Continue with fresh authority.
The prior VUH-2466 attempt (`20261020-002047`) is only a failure baseline: host
Game Over while the friend continues, no Thorn census/HP evidence. Protected saves and existing desk/run locks remain mandatory.

## Implementation status

The reaction-only implementation extends the existing enemy census, one-brain
suppression and script resources. Mission end observation, event315 qualification
and exact307 boss-leave fallback are deferred; generic main mission/event/boss
behavior remains unchanged. Reusable
reaction occurrences and actor-owner capture, attachment, camera and command
routing live inside ScriptMirror. Protocol v30 preserves the landed owner-vitals
layout. Current main's checked entrance-record matching also selects Thorn's
raw566 at record1; the boss at record0 cannot supply a player template.

The combined candidate on main650f9ea passed build15: all targets compiled and
all 201 registered tests passed. This includes the actual-owner/spectator
source-bank correction, native motion policy regressions and main's entrance
selectors. Independent native reviews covered resource lifetime/scalar
normalization, both command helpers, the complete motion patch and its two
enum-conversion corrections. Runtime qualification is partial; the live07 findings below retain the remaining gates.

Live06 (`20261020-205110`, single-PC loopback) reached Thorn through real doors
and reduced shared HP390 to232 with native host damage and actual friend hit
claims. Key Counter297, Lunarsault298 and Break Raid299 completed through host
validation; the host recorded17 completions in total, including actual
friend-owned ground345/432. The host was naturally downed four times and returned
through native revival three times, before both pilots timed out on the fourth
rescue. There was no kill, mission16, event315 or victory clip. Both inject logs
contain no exception. Safety closure passed with four protected saves and484
source inputs unchanged, no remaining owned processes and the relay port free.

The same run produced an enemy-population desync: after native boss-script
creation of Creepers317, the host counted three enemies while the friend counted
only Thorn for more than12 seconds. Progress hashes agreed. Creeper317 already
has a global point-binding exception; turning on that family or the parked
general population factory is not a qualified correction. Exact host-owned
script birth and binding must extend the existing paths before acceptance.

Live07 (`20261020-213855`, build15, single-PC loopback) reached shared225/390HP:
45 damage from the three opening RCs and120 from ordinary combat, including69
from12 accepted native friend claims. The host was downed once; neither seat
revived. Both pilots reached their60-second cooperative timeout. No revive hold
or blocked-approach probe began. The friend's raw and visible Reversal432
persisted while the host later offered345; native revival also yielded to432
inside100 units. Callback eligibility after those roles ended was not recorded,
so prompt lifetime is under investigation rather than classified as pilot skill.
There was no kill, mission16 or event315. Safety closure passed: four saves,
498 source inputs,14 game-root entries and527 assets unchanged; owned instances
closed and relay port free. The exact507-pin source archive is retained with the
run under `build/rig/vuh2569-live07/`.

Ground Reversals are authored evasive movement, not boss damage. The three432
callbacks inspected in live05 each advanced and released their host and friend
roles. A plateau alone therefore does not establish a stuck callback; live07's
persistent prompt and native revival starvation are the more specific evidence.

The friend-side borrowed-motion gap is tracked on
[VUH-2580](https://linear.app/vuhlp/issue/VUH-2580), with the
[analysis](https://linear.app/vuhlp/document/vuh-2580-nonowner-captured-motion-analysis-8b2caf134c94).
The actor-role path now selects the captured source's motion bank for both the
actual owner and a separately validated spectator clone, retaining owner-only
cameras/commands and callback-free visuals. Live07's read-only trace observed
93 host-owner and93 clone samples with Thorn's borrowed roots, followed by
restoration to each actor's own roots. Sparse footage leaves the captured clone
mostly occluded or cropped, so this does not qualify its full rendered skeleton
or a friend-owned cinematic capture. A later paired frame also shows a standing,
arms-horizontal remote actor while its host is downed; that separate pose mismatch
remains unqualified. Temporary hidden B_EX200 and hide/show parity remain open.

The approved script-child design keeps the global317 deny and extends existing
birth, manifest and mirror ownership. Once adopted, a child follows its own
authenticated manifest, HP and death; parent retirement stops new births only.
A fresh reconnect after the parent has gone is refused in this initial scope.
That is a known limit, not permission to infer a historical parent or relabel an
old native body. Resource reservation and constructor/initializer boundaries
must be qualified before enabling the new admission.

Earlier run181620 exhausted the friend's native effect arena after missing
stored-resource cleanup calls. The own-PID dump attributed the original fault to
allocation failure, beneath secondary CLR exception recursion. Registry-first
stored-handle admission and wrapper-specific scalar normalization now deliver
cleanup: live05 applied1060 effect-fade and1066 attack-free records without an
exception. These are delivered-call receipts, not a measurement of recovered
native pool occupancy. Camera admission also now uses the exact role-owned native
context, preserving story/loading/menu/frozen guards; retirement waits for the
finite authored camera and native idle.

Acceptance still requires a successful two-seat kill, one host mission16 and
native315 publication, the same authored exit and onward control, qualified
owner/clone motion and cleanup, and a captioned two-screen clip explicitly
labelled single-PC loopback. Existing30-second recordings have encoder gaps;
they must not be presented as uninterrupted footage.

Live08 candidate extends the existing RC path with native last-winner receipts
and preaccept validation before action installation. Revival and both pilots
observe that same product-qualified actionable reaction through the existing
read-only version7 prompt fields. This changes the observation source only:
routes, reach, timing and strategy remain live07's. Both seats must report the
new hooks installed before leaving the boot room. Full native ABI/pin/refusal
review and the governed build precede the attempt; no passing run is claimed.
An interrupted accepted action without a qualified callback can conservatively
fence later same-scope reactions. Late decision/cache/active-request traces
cover that remaining runtime risk. Creeper birth integration remains separate
from this kill attempt and is still required for final population parity.

Live08 (`20261020-225646`, build16, single-PC loopback) reached shared235/390HP.
Opening297/298/299 passed the native acceptance and inline callback checks.
The host was naturally downed three times and returned through native revival
twice; the friend was then downed once and both pilots stopped after the
full-party timeout. Neither log contains an exception. There was no kill,
mission16, event315 or victory clip. Four protected saves,525 source inputs,
14 game-root entries and527 assets stayed unchanged; owned processes closed
and the relay port was free. The534-pin source archive is retained with the run.

This attempt exposed two product defects, independently verified against that
archive. Ground completion consumed the host occurrence, then an unused null
third candidate with default ownerSlot0 overwrote its valid canonical player.
Client acceptance used the host-only targeting table for its local-seat
liveness, refusing even received owner1 occurrences. Build17 uses shared
canonical seat resolution and checked local roots for both attack and captured
command proofs, with regressions for two/three seats and empty client tables.
Pilots remain unchanged. The lead's revised landing gate allows qualified
product fixes after the full suite and live09 reaction completions on both
seats; VUH-2569 stays open for the kill and Creeper parity.

Lead17 requires a newly built full suite and exact trimmed-binary short reaction
proof before landing the qualified RC slice. Existing full-source run history
above is retained as history and cannot be transferred to that trimmed binary.
The archived live09 scenario and its strict kill/reward/exit assertions stay
unchanged; the separate short proof fixture belongs to the lead. Deferred static
terminal findings remain recorded below and require their own future qualification.

Live09 (`20261020-233058`, build17, single-PC loopback) reached shared158/390HP.
Opening297/298/299 and ground reactions completed for both owners: host432
and345, friend345. A later friend-owned captured426 passed local native
acceptance and callback checks but has no host completion record. Both pilots
eventually stopped at the120-second no-progress guard; the friend remained
below the arena. The available trace cannot distinguish a failed send from a
silent host refusal, so this remains a product coverage gap, not an established
pilot failure. There was no kill, reward, mission16 or event315 proof.

All six paired clip blocks finalized. Four protected saves,531 source inputs,
14 game-root entries and527 assets stayed unchanged, with no exceptions,
remaining owned processes or occupied relay port. The exact540-pin source is
archived with the run. Build18 removes the reviewed terminal bundles and adds
diagnostic completion receipts from immutable claims, because native callbacks
can retire or reuse their role/binding before returning. Its full suite and
separate short no-kill reaction proof must qualify that exact binary before
landing. Live09 evidence does not qualify the trimmed build.

Build18 passed202/202 tests. Before its short proof, main added the Day3
Sandlot/Serenity encounter rows; build19 integrates those additions and repeats
the full suite for the final trimmed binary. The native RC code is unchanged.
The separate short fixture has independent offline review of exact occurrence
joins, two-pilot stopping, neutral release, clip drain and a sampled prelethal
boss-HP guard.

Build19 passed202/202 serial tests (153.01s). Exact trimmed live10
(`20261021-001728_net_twilight_thorn_reactions_1`, single-PC two-seat loopback)
passed all34 steps. Host-minted opening297/298/299 completed; ground432
completed for owner0 and owner1. The owner1 completion joins the exact host
offer, client queued receipt and host callback for resource67/occurrence31.
Inputs stopped at93.531s, both neutral releases succeeded, and the four raw
clips finalized as two30-second paired blocks with recording gaps. Native
completion records do not imply every event is visible in those blocks.

One earlier client queued432/resource28/occurrence14 has no host completion;
its cause remains unknown. The host naturally went down once and no revive
occurred. This qualifies the selected reaction milestones, not every reaction
window, sustained survival, captured426, Creepers or a kill. All four protected
saves,575 source inputs,14 game-root entries and527 assets stayed unchanged;
closure found no remaining owned processes or relay listener. Independent
result review also verified all584 pins and the575-file source archive.
See [the limited reaction result](testing/vuh2569-reactions/README.md).

### Deferred terminal static evidence

The installed mission routes boss signal4 through4/16 to native event315. Its
source is world2/room34/event157, event ID0x353133, flags0x12, with semantic
parameters `{2,2,00330033,33,1,FFFFFFFF,3,0,0}` (hex). Native39FBC0 leaves
destination byte3 unwritten; the pinned3A4BD0 copies
the destination into the descriptor. Only params1 high byte was ignored in the
deferred exact-row qualification; full raw host publication/replay was retained.
The authored exit is world2/room0/door0/programs51. These installed-data/native
findings are not a runtime victory start receipt and do not enable the row in
this slice. The full prior implementation/tests/docs are retained in the
terminal-trim21 baseline and deferred reverse patch.

### Full-fight candidate and live11

The subsequent full-fight candidate restores the reviewed host-only terminal
path under protocol31: signal4, mission4/16 and the exact authored315 event.
Clients do not blindly replay4/16. These semantics remain unqualified by a kill.

Build23 passed all202 tests. Live11 (`20261021-020601`, single-PC loopback)
ended in native full-party defeat at step39 with shared boss HP268/390.
Earlier revives worked in both directions. The host's later downing remained
at Y=-251.985 while the friend was usually near0, outside the unchanged3D
revive range. In this coordinate system negative Y is upward; earlier wording
describing a negative-Y player as below the arena was incorrect.

All eleven remote reaction completions reached host application. Nine applied;
two captured426 replies were refused after host-role retirement, before the
producer-frame guard. The retirement trigger remains unknown. There was no
trace saturation, kill, mission16/315, reward or cleanup proof. Four protected
saves and all source/game baselines were unchanged; closure was clean. See
[published reports and stills](testing/vuh2569-live11/README.md).

The approved next candidate holds only downed XZ and preserves native Y. Static
qualification shows the old XYZ hold prevented settling away from the lethal
height; it does not establish that this alone caused live11's loss. Additional
observational retirement rows retain the original reads and order. The combined
full suite and a short FM-02 Computer Room grounded down/revive regression must
pass before Thorn live12. Pilots and3D revive range stay unchanged. Creepers
remain the documented VUH-2605 gap; VUH-2569 stays open for the kill and426 cause.

Build24 passed all 202 tests. Its separate Computer Room regression
`20261021-035356_net_downed_grounded_pair_1` passed all 105 steps in 163.6 seconds.
Each owner entered logical downing through a real lethal `hit damage`, retained
grounded native XYZ during attempted movement, then revived once through the
normal Triangle/network path to 5/22 HP, Ready state, fault0 and 120-frame grace.
Both regained ordinary movement. Four protected saves and all recorded baselines
were unchanged; closure and the exact 538-file source archive passed independent
review. Four native stills were inspected. This calm check has no actual enemy
shove or airborne landing; Thorn live12 remains required on the same binary.

Live12 (`20261021-040907_net_twilight_thorn_rehearsal_1`, exact build24,
single-PC loopback) failed step41 after five paired combat blocks. Both seats
ended at boss176/390. Opening297/298/299 and four friend-owned ground reactions
completed; eleven accepted friend hit claims contributed64 damage. The host
was naturally downed twice and revived once through the normal request/native
path to5/20 HP, Ready state and fault0. The final recovery never reached another
hold or request: sampled separation remained492-1175 units while native RCs
retained their intended priority. All56 sampled host HP1 rows had Y0. No lethal-Y
trajectory or native grounding/attachment state was recorded, so this does not
prove an airborne landing or a new height defect.

All six remote reaction completions reached host application; four succeeded.
Captured426 occurrence63 was accepted by the friend's native leaf and callback,
but found its host role already retired. The new immutable row identifies the
first D48 read returning zero against its saved coroutine, two frames before
host application. Later predicates were unevaluated. This is a remaining product
qualification investigation, not proof that retirement was wrong, that a packet
was lost, or that it alone caused the loss. The native writer/BDX exit remains
unproven; the whole run cannot yet be certified pilot-only. Pilots are unchanged.

The known enemy-only Creeper317 population desync recurred with equal progress
hashes. Its automatic bundle was partial because of a changing log tail and
busy capture leases. There was no kill, mission16/315, reward, final role/camera
cleanup or onward-control proof. Four protected saves and all540 source inputs,
14 game-root entries and527 assets were unchanged; closure left no owned
processes or relay listener. The exact540-file/553-pin source archive predates
edits and passed independent verification. Subsequent Ivo VUH-2580 source changes
are integrated on mainf42c3f9; live12 does not qualify that rebuilt composition.


Live13/build25 failed after523.5s, boss194/390 on both seats. Both pilots
reached the60-second recovery guard; no normal revive request or native return
occurred. All seven remote reaction completions reached host application; five
succeeded and two found their occurrence missing. Three captured426 roles reached
the native scheduler cancel leaf before the first zero D48 retirement read.
This identifies that cancellation path, not the authored cancellation reason or
the earlier live12 writer. The whole run remains unqualified as pilot-only.

[Published live13 evidence](testing/vuh2569-live13/README.md) includes the bounded
two-screen failure clip, decisive logs/reviews and exact pre-edit source archive.
The actual1/238 observer and sole3D6F80 cancel hook installed on both seats; no
associated1/14 row was captured. Its argument-based association missed a different
operand shape. Four protected saves and all recorded baselines stayed unchanged;
owned processes and relay listener closed. No kill/16/315/reward/final cleanup or
onward-control proof follows from this run.

Lead43 approved an observation-only1/14 correction: select the unique currently
executing admitted VM, log raw input/output, and label the target unresolved.
Never decode/dereference that alias or infer command authority from it. The
correction has independent offline reviews; its next build and runtime proof
remain required. See [Script Mirror](SCRIPT_MIRROR.md#native-lifecycle-observations-lead40-and43).
The next kill attempt is human-piloted FRIEND/seat1 with the unchanged HOST bot,
using a reviewed handoff and exact owned-process abort. No extra bot run is
scheduled for diagnostics. Ivo's main724ddcb residue changes are composed and
independently reviewed; live13 does not qualify that later composition. Pilots,
protocol31 and3D revive range stay unchanged; Creepers remain VUH-2605.

Mainc07d8da (VUH-2610r2) extends only spectator playback for chained captures.
A changed capture remembers its previous motion until the first heartbeat naming
anything else. A still-previous heartbeat selects the newer authored capture and
cannot write its old motion time. Once caught up, a later old ID is judged by
the unchanged borrowed-bank/refusal rule. Consumed reactions remain refused;
camera, commands, local-owner native calls and wire31 are unchanged. Independent
composition36 preserves the corrected observation and existing cleanup paths.
Build27 and the human14 packet refresh qualify the combined inputs separately;
old build26 evidence does not become a result for that new binary. Missing262/263
capture descriptors and Creeper population parity remain separate gaps.

Combined build27 on c07d8da passed all202 tests with its320 native inputs
unchanged. The refreshed human14 packet remains held by lead51: Ivo's separate
VUH-2611 visual12 candidate achieved a kill, then HOST crashed in clr.dll after
the post-boss event began. Its cause and applicability to build26/27 are unknown.
No human14 run or baseline is claimed. Packet postvictory checks introduce no
managed call into KH2; process-exit detection is independent of the inject crash
filter, but no dump or recovery is guaranteed. Investigate before asking James.

### After the kill: the post-Thorn scene chain (VUH-2617, bot04)

Bot04 (`20261021-192441`) killed Thorn and both seats played native315, but the friend stayed in 02/34.
After315 the host loaded seven event-only locations in a row, each started by the previous one's native
start: 02/00 51, 02/04 83, 02/02 67, 02/06 168, 02/02 167, 02/15 142, then 02/01 54 (map, battle and
event program equal in each). None reached field play: the gameplay gate never opened, so the host's
census and progress samples never ran. An unqualified room is announced only from field play, so the
host announced none of them, and the friend waited at its own blocked native exit to the first one,
02/00 51 (the authored exit above). The first six are now qualified scenes (`EventScenes.hpp`): the host
announces each at its load and the friend loads and plays it natively, as in the prologue's post-Axel
chain. 02/01 54 was still in its event when the run ended, so its exit is unobserved and it stays
unqualified; the next live run shows whether it is another scene or the field.
A returning seat's recovery capture is refused in a qualified scene (it waits for a later field), so
post-Thorn recovery now waits past this chain too.

The same run's other failure (step69: the friend's own HP never fell during Thorn) is not explained yet.
The friend's mirrored Thorn built no hit at all on the friend (`[enemy-mirror] hit` 0, `[hit]` attacker
B_EX100 0, with 18 of the 60 per-room rows unused), while the host's Thorn hit the friend's clone 12 times (eleven
attack 1014 during motion 202 at the arena edge, one 448). 1014 and 448 are not script attacks (Thorn's
script creates 631, 1305, 1015 and 1041, which hit the host); in bot02 the friend was hit natively by
motion attacks 1034/1035 (motions 204/226). Thorn was never retargeted at the friend (bosses keep their
own targeting), the friend never guarded, and its ScriptMirror replay kept pace with the host.
