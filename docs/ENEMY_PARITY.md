# Enemy parity across instances (VUH-1499 spike)

Question (plan D5, hard problem 3, risk R4): with the same save and room, do
two instances spawn the same enemies, and what key matches a host enemy to
its client copy?

## Open native-wave geometry finding, 2026-10-05

The strict impaired Mac-relay wave run remains unpassed. In `094748`, the
original first-pack damage and exact-ID/HP census gate passed after 2.26 seconds,
but the first permitted 250 ms correction moved outward: radius 563.030 to
605.943. The fixture stopped before another pulse or kill. The approved limit
stays three measured 250 ms pulses, with immediate failure on outward movement
or overshoot; no further input budget is authorized.

Drift also occurs before damage. In `095701`, two qualified neutral endpoint
samples 5.907 seconds apart showed the same host actor moving from radius
58.208 to 224.005 before the first hit. The following read-only run `100816`
collected a 3.5-second interval outside the original hit gate, then intentionally
failed without issuing damage or correction. Its 140 rows included 138 qualified
samples across owner frames 9340Ã¢â‚¬â€œ9549. Those samples retained neutral raw and
processed input, idle motion and zero sampled velocity at actor+0xB98, while
the actor moved 309.349 planar units. Actor, room, controller and camera
identities stayed fixed. Two rows crossed frame boundaries and remain
unqualified; sequential reads do not establish unsampled continuity.

Zero sampled velocity does not identify the position writer or exclude native
collision, carried displacement or other motion terms. The source audit found
no demonstrated local transform overwrite in this launch configuration;
network pose activity alone does not prove a puppet transform was applied.
The existing protection helper writes team membership; its indirect effect on
native collision and motion is unproven. Attribution continues through native-field/static analysis,
with no speculative suppression. All four protected saves stayed unchanged;
the owned relays stopped and fetched receipts match their remote originals.
See the [interval result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/neutral-observe-root02/result.md)
and [source audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-motion-source-audit/result.md).

The bounded caller diagnostic `115734` narrowed the observed writes to the
native position calculator's solved-position copy. All 61 captured candidates
had complete context/value/stack reads at leaf1A8E69, with stack-top candidate
return3B9561 and 21 distinct finite X values. Retained leaf/caller disassembly
supports the copy from actor+700 to actor+670 at3B955C. All 24 surrounding sparse
native samples qualified, including before, during and after the one-second
watch, and movement occurred with neutral sampled input and zero sampled B98.
The originating force remains unknown. The watch lacks a DR6 audit and per-hit
timestamps; the copy does not establish a unique writer or continuous neutral
execution. This intentional diagnostic FAIL and the earlier incomplete112744
FAIL remain intact. See the
[caller result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/caller-motion-root02/result.md)
and [independent interpretation](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/caller-motion50-analysis/result.md).

The private typed-RPM census transport passed live equivalence in `130211`:
all retained fields and entities responses matched the CLI on three paused
games. The paused canonical census kept gameplay eligibility false and skipped
its causal-context branch; this is transport parity only. The unchanged
75-step wave attempt `130654` then reduced the complete two-snapshot pre-hit
census from 16.66 seconds to 0.640 seconds. Host damage needed no correction,
and all original four IDs matched HP153 at a fresh hash and a strictly later
independent native HP snapshot. The full run still failed before kill: radius
268.812 grew to 366.348 after the first 250 ms pulse, triggering immediate
abort. Later-wave/death acceptance remains unexposed. See the
[transport comparison](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/census-equivalence-root01/result.md)
and [wave result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/census-fast-waves-root01/result.md).

Endpoint control `131241` moved 325.571 planar units over a QPC-qualified
16.661754-second gap with no added observer reads. Both endpoints had neutral
raw/processed input, safe gameplay, stable identities and advancing owner
frames. Bulk census reads are therefore not required for movement. The normal
protection actor/team loop, renderer, runtime and network continued; endpoints
do not prove continuous neutrality or causal read-load scaling. Enemy contact,
knockback and terrain remain possible contributors. All four saves stayed
unchanged in these runs; owned processes and relays closed. See the
[endpoint result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/no-observer-endpoints-root02/result.md).

Matched empty controls `132932` (protection OFF, team1) and `134125`
(protection ON, team0) both retained exactly the same position over qualified
16.702387/16.662934-second waits. Both had complete empty native census
bookends on all three peers, matching room/controller state and selected
products/config. Protection alone did not induce movement in this empty
preactivation setup. Its interaction with populated enemies remains open;
the empty starting point/phase differs from the populated control. Earlier
`133550` remains FAIL before the wait because its START read crossed one owner
frame; the fresh ON invocation retained every sampling guard. Saves stayed
unchanged and owned processes closed. See the
[OFF result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/empty-endpoints-root01/result.md)
and [ON result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/empty-on-endpoints-root02/result.md).

Retained `130654` damage receipts account for hittrace unmatched1->4 through
the four fixture HP calls, not enemy attacks. Its sampled camera direction and
nonzero native velocity point inward while position moves outward, arguing
against a simple joystick sign/axis reversal. No host hit reaction or paired
body-contact proof was captured. Keep the correction budget and immediate
outward abort; do not infer a force from sparse velocity/position samples. See
the [contact audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/enemy-contact-drift-analysis/result.md)
and [direction audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/correction-direction-analysis/result.md).

## Region-qualified kill and second-wave binding failure, 2026-10-05

The first-pack kill fixture initially changed to check fresh full original IDs/HP/native
provenance on all peers, gameplay safety, neutral host input and actual native
region bit8 immediately before the canonical kill. The fixed 100-unit radius
was a fixture stability convenience, not a native kill prerequisite. That first
change removed the radius gate and corrective input only for this kill. Sora's
position was observed and uncontrolled; the remaining gates stayed unchanged.
An out-of-region host still fails.
See the [source decision](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-approach-feasibility/result.md).

Run 143735 reached the kill with the original IDs 3 through 6 at HP153 on all three games,
region bit8 true and radius 35.55. Four native kills and each original death's
once-only native application on both clients are confirmed in the raw logs.
The cumulative six host death notices include two earlier alive departures,
not duplicate first-pack deaths. This run happened to remain within 100; it does
not demonstrate an outside 100 kill. Four protected saves stayed unchanged.

The full wave run FAILED at second-pack parity. Friend1 bound two new actors
to ID7 while only the first append manifest was available, then rebound one to
older dead ID1 and killed it. Friend2 later published two different living actors
as ID8. Both clients consumed the later manifests. Ordinary matching in that DLL allowed
spawn-index fallback across mismatched points and has no live-ID reservation;
it can select an older tombstone after a provisional binding. These are product
binding failures, not reasons to weaken the second-pack test or retry movement.
Second-pack combat and reconnect were not reached. Native spawn/removal timing
and same-address incarnation remain separate unresolved boundaries. See the
[run result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/region-qualified-kill-root01/result.md)
and [raw-log attribution](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/second-wave-143735-audit/result.md).

The reviewed ordinary resolver now selects the whole fresh census before native
writes. It removes mismatched-point index fallback, retains each tracked row's
ID high-water mark and holds all conflicting live claimants. A tombstone needs
the established ID and adjacent owner-frame samples with equal current
generation/epoch and actor/object/objentry/status/controller/record roots.
Roots are checked again before writing. One ordinary lethal ends that frame's
write batch; the next frame resolves the remaining bodies afresh. Held bodies
publish unmatched. Exact-content resync and native write leaves are unchanged.

The production resolver passed 137 controls normally and under Windows ASan,
including reversed order and split manifests; the full DLL linked. Run 152041
failed before damage because Friend1 never sampled the native 311 pair, despite
receiving the manifest. An unchanged retry, 153018, confirmed all four original
native deaths exactly once on both clients and two fresh matching second-pack
samples: unique IDs8/9, both309 HP160. Two independent native census rounds
joined every current body to its ID, HP and record. That live run did not expose
the worst reversed-order split-manifest timing. It still FAILED before second
damage when the existing correction pulse crossed its fixed target axis.
The whole-wave gate stays open; sampled roots do not prove incarnation or
support deaths across an observation gap. See the
[native binding audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/second-wave-binding-153018-audit/result.md).

The approved fixture extension now uses region bit8 for the remaining hits.
Before each victim hit, reread the complete all-peer pack and require the saved
IDs, expected HP, native actor/controller/record identities, neutral input and
safe gameplay. An inward correction stops as soon as bit8 is true. The budget
remains three 250ms pulses total per damage/kill step, with immediate outward
or axis-overshoot refusal. The original first-pack region kill stays unchanged.
The intermediate six-second post-kill wait can be removed, but the final pack
retains a six-second quiet window followed by a complete all-peer census with
no unexpected IDs and counts matching the host. A later replacement ID10
cannot substitute for saved ID9. This is a declared fixture change that reduces
drift exposure; it does not explain drift or establish a wave PASS.

The first per-victim run,161016, reached exact all-peer HP153 for IDs3/4,
then lost bit8 before ID5. Its first250ms pulse moved outward255.22 to264.32
and correctly aborted before that hit. No kill or second pack was reached.
The next approved fixture batches propagation: a client may lag only through
values already issued for that victim, in order, and each native/publication
observation stream must never regress after a newer value is seen. Full fresh
pack reads and all identity/region/safety guards remain before every hit.
Every lag must resolve at the unchanged exact all-peer phase checkpoints.
The first-pack post-damage shortcut uses one fresh matching hash sample followed
by an independent native HP snapshot that matches it exactly. Pack admission,
next-pack and final checkpoints retain their two-fresh checks.

The batched run,162442, applied all four original hits once and reached HP153
on all three games with no correction pulses. It then lost bit8 before the
first kill, at radius276.74, despite neutral input and qualified safety/identity.
It correctly failed without any kill or second pack. Stop timing trims here;
the next diagnostic is one native R1 lock-on/Cross attack with visual target
confirmation. Damage-free homing is not required: a countable native hit could
become the wave damage step, but this one-game probe cannot prove network claim
handling. Sora's position remains uncontrolled. See the
[per-victim result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/region-qualified-hits-root01/result.md)
and [batched result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/region-batched-hits-root01/result.md).

## Native attack trace receipts, 2026-10-05

The one-game R1/Cross diagnostic captures the enemy lock reticle immediately
and reviews the image after attacking. Missing or unclear reticle evidence is
unqualified. Native departures are recorded; the current member/HP, process,
player, controller and safety bracket must still qualify immediately before
Cross. This changes no original network-wave ID or acceptance requirement.

Requested `KH2COOP_TRACE_HITS=1` now permits an idle summary every1000ms on an
actual owner drain, in addition to existing activity summaries. Appended owner
frame/thread, summary sequence and GetTickCount64 time support fresh log
bookends. The reviewed three-file logger change leaves hooks, producers,
storage, event serialization and damage policy unchanged. Both affected TUs
build warning-clean;34 focused actual-logger checks pass. Independent counter
loads and emission attempts are not an atomic seal or acknowledged log write.

A covered interval requires fresh advancing owner receipts, unchanged requested
configuration/mask7, balanced started/published/drained counters, complete event
groups and no unexplained loss/refusal. Zero covered activity requires all three activity
counters unchanged. That covers ApplyHitDamage, TakeDamage and ApplyStatDelta;
it cannot exclude every HP path, contact or physics push, or certify fiber
ancestry. NativeHitTrace's network `witness` flag describes client enemy-to-player
damage. Sora-to-enemy evidence instead requires the complete normal native
Apply/Stat causal join and HP decrease, including Take when that route uses it.
The observed direct Stat caller3D37D2 under Apply caller410EFF does not have an
enclosing Take token; its retained child explains exactly one unmatched increment.
Only complete supported groups may account for those increments; every other
change refuses qualification, and zero activity still requires unmatched unchanged.
Absent or incomplete rows stay unknown. One Cross may land zero or multiple hits.

Native175242 captured one real R1/Cross attack and one qualified direct-Stat
HP160-to-122 application to current311/record34, with fresh owner bookends and
no other loss/refusal. The final current list kept records14/15/34/35, with record34
at122 and the others at160; no post-Cross additions/removals. Bit8 began true and
ended false, so region restoration remains unexposed. The canonical scenario
retains its intentional diagnostic FAIL and earlier174135 qualification FAIL
remains. All91 sources/inputs/products and four saves unchanged. The production
claim path intercepts at Apply before native Stat and does not depend on a Take
witness. See the
[one-game attack result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-homing-root08/result.md).

Native181752 supplies the first observed real-client button case. Friend1's
direct-Stat call had TakeCalls0, queued connection2/claim1 for attack127/damage5,
and held local HP160 while its damage became0. The host received that claim once
and applied native160-to-155 once. Two fresh all-peer samples and two independent
native censuses retained the original four bindings:3/309/160,4/309/160,
5/311/160,6/311/155. This is one observed production interception and replication,
not general attack-incarnation or wave acceptance. Its canonical FAIL remains:
the fixture demanded a positive claim from a subsequent zero-amount callback.
That complete callback retained HP160 and Native/ZeroAmount policy, with no claim.
Both retained direct-Stat children account for the client's unmatched increments;
all other client loss counters stayed zero. Host replay's orphan Stat counter has
no retained child, so host damage evidence is the claim/application join and HP
convergence, without a whole-host clean-trace claim.

The corrected diagnostic scopes no-death to the admitted original pack and
explicitly retains outside-pack IDs 1/2 despawn receipts. Complete initial/final
native and published samples must exclude outside IDs, extras and replacements;
this is not continuous room-wide lifecycle coverage. The old FAIL is unchanged.
The zero callback used the same observed hit/attack buffer but a different target.
Client suppression does not restore that buffer's amount after native return;
whether this loses intended multi-target damage remains open pending a matched
host-button control. A zero callback is not proof that the second target should
receive zero damage. See the [real-client result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-client-network-root03/result.md)
and [independent review](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-client-real-button-review/result.md).

Fresh183547 passes that corrected one-cycle client diagnostic over the Mac
relay50/10/2: one positive claim/native host application160-to-155, one retained
zero callback, exact original four-ID native/HP convergence, no other client
loss/refusal, and unchanged source/product inputs and four saves. It leaves the
multi-target question open. Host183019 produced native zero callbacks with
suppression off, including attack615/damage38 to311 followed by zero to309;
the exact positive127/damage5 comparison was not exposed, so that control remains
unqualified for the specific client concern. See the [passing client result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-client-network-root04/result.md)
and [host comparison](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-host-buffer-root01/result.md).

Native185918 retained the original all-peer HP153 wave checkpoint, then used
one host R1/Cross and a full five-second observer. The unchanged strict trace
seal qualifies zero covered callbacks across the three damage hooks: eight
advancing owner summaries, activity0 unchanged, unmatched4 unchanged from setup,
all other loss counters0, and no event or unknown rows. All four original IDs
and native identities remain at153 in two fresh samples and two independent
censuses. Its canonical nonempty-hit FAIL stays unchanged. Of81 post-Cross
samples,79 pass the frame bracket; only the last two show bit8 true. The sampled
return is not proof that the attack caused it. See the
[zero-window audit](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-wave-region-homing-zero-audit/result.md).

The approved next wave fixture adds a fixed maximum five-second neutral region
wait at the first kill and second-pack damage/kill gates. It records every
sample and adds no input or writer. A successful wait still requires the full
original pack reread and a fresh immediate bit8/neutral/safety check; a flag
lost during that reread fails without another wait or correction fallback.
Original IDs, HP, deaths, correction bounds and the final six-second quiet
census remain required. Two failures of the same kind end this approach without
changing the wait length; native attack with full HP accounting is next.

Both unchanged attempts,191401 and191801, failed at the first kill's five-second
region deadline. Their46 samples each never showed bit8 true;45 and46 samples,
respectively, passed the frame bracket. Measured radius grew221.58-to-791.84 and
288.34-to-942.98, with no sampled decrease. Neither run killed an enemy or reached
the second wave. Sources and protected saves stayed unchanged. The neutral wait
is retired; the next fixture uses one native attack and accounts for its exact
HP result before the original fresh region/native kill checks. These observations
do not identify the movement writer. See the
[second attempt](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/region-neutral-wait-root02/result.md).

The full-wave native-attack attempt193532 also failed before the first kill.
One R1/Cross sequence completed; the enclosing three-hook trace qualified zero
callbacks, and two fresh all-peer samples plus two independent native censuses
retained original IDs3/4/5/6 at HP153. The later current region bit was false,
so no kill or second wave occurred. No attack retry or timing trim followed.
See [the result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/native-wave-approach-kill-root01/result.md).

Its motion timeline does not show idle with continuing displacement: all61
qualified idle0/0 samples had exactly the same position. Earlier motion41/164
has the retained local REFLECT label; the native selection cause is unknown.
Movement resumed before the later kill gate, outside the sampled attack window.
The follow-up diagnostic kept natural team1 with a one-time HP-only survival budget
before the original3.5s endpoint interval. Both clients also died in142114, so
each owned game's local Sora needs that budget to preserve the all-peer census
guards. This control does not establish protection causality or wave acceptance.

Run195832 exposed356.879 units of displacement during qualified3.5s endpoints
without a team writer. One native48-HP application occurred inside the frame
bracket, but endpoints could not locate movement onset. Run201526 added a
bounded read-only50ms timeline using the identical reviewed HP leaf. All71
samples qualified: Sora remained exactly stationary at600HP through frame6617;
the recorded native hit applied at6618, and frame6619 first showed552HP,
displacement and motion10/DAMAGE_S_FRONT (variant42). Displacement decayed to
a stable position before LAND2 and idle; sampled inputs and B98 velocity stayed
neutral/zero. The later already-applied callback caused no additional HP loss.
This establishes hit-associated onset to the sampled resolution, not the
responsible position writer or an explanation of every earlier protected run.
The external S0/solver debugger probe is parked. See the
[timeline and frame join](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/hp-only-team1-timeline-root01/result.md).

Both full profiles remain FAIL: complete/safe native censuses changed six to
four on all peers, losing original IDs16/17; no native-six guard was weakened.
Protected saves and production sources stayed unchanged. No validated native
invulnerability/no-reaction setter was found; the next fixture accepts native
reaction and restores the required region through bounded native movement,
then retains the original fresh pack/HP/census checks before each kill.

The camera-relative fixture203609 failed before the first kill. Its four
original damage calls reached exact all-peer HP153 with the independent native
checkpoint. The first250ms correction delivered the requested raw bytes140/2,
but sampled processed movement was0/-1 rather than the fixture's expected
0.09377/-0.99559 within0.05 per axis. Neutral release was observed. The pulse
remains unqualified; no retry, kill or second pack followed. Sampled radius
increased279.725-to-348.959, but the closing/direction guard was not reached.
No known hit appeared in the available pulse receipts; they do not seal hit
absence. Check native axis processing and the actual input transform before
loosening the witness or changing steering. The private variant explicitly
adds one shared3x250ms budget across the four first-kill victims; existing
protection and fresh per-victim pack checks remain. See the
[closed run result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/camera-region-approach-root01/result.md).

The bounded input interpretation found that native processing removes the small
X component, while B98 velocity points inward along sampled camera-forward.
The native transfer threshold and full camera/input transform remain unproved;
this supports neither an LY flip nor an explanation for outward position.
The one corrected attempt `211819` used deliberate `(0,+1)` only after fresh
forward/target alignment passed `.95`. Ten qualified samples witnessed raw
left bytes `128/1` and processed `(0,-1)`; neutral release followed in 16 ms.
All 13 samples qualified, but sampled radius increased 282.612 to 310.758, so
the unchanged position-closing guard failed at step 45. All original IDs
3/4/5/6 again matched HP 153 on all three games and the independent native
checkpoint. No kill, second pack or retry followed. Available receipts have
no known hit/reaction, without sealing absence; B98 still points inward and
does not identify total displacement. Saves, sources and the preserved 203609
fixture are unchanged. The interpretation, one correction and one live attempt
are complete; the strict two-pack gate remains open. James selected assessment
of a fully reviewed one-shot fixture position restore, with the original failure
retained. See the
[corrected run and options](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/effective-input-region-approach-root01/result.md).

The reviewed XYZ-only scalar restore control `215309` failed at step 45 before
any kill. Original IDs 3/4/5/6 again matched HP 153 on all three games and the
independent native checkpoint. Three f32 pokes to canonical unparented host
Sora targeted 555/-1100/-2356; each response and axis readback passed. X then
changed from 555 to 556.9389 between writes. The qualified post-Z sample already
differed from the full target, and the final full-XYZ read crossed a native frame
and was refused. A frame-timing correction alone cannot qualify these retained
observations. Native bit8 became true before Z, without a flag write, but the
required later-frame post-XYZ region window and post-restore enemy control were
not reached. No retry or full wave run followed. Saves, sources and pinned inputs
are unchanged, and owned processes are closed. The one-shot scalar assessment
is complete; persistence and the strict two-pack gate remain unproved. See the
[closed restore control](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/position-restore-control-root01/result.md).

The later reviewed single-call XYZ control `004941` passed without kills:
original3/4/5/6 remained HP153 on all three games in fresh hashes and two
independent native rounds. Its unchanged full wave gate `010014` failed the
canonical living-set equality at step 45 after issued kills3/4, before ID5.
The last host native round has original5/6 living at HP153 and3/4 dead at0;
pending client streams had not all settled. The fresh failing canonical reply
is missing, so no mismatch cause is assigned. This leaves final first-wave
death parity, second-wave IDs/HP/deaths and quiet/no-extras unproved. All earlier
FAILs remain. Both runs are closed with saves unchanged. See the [control](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/xyz-single-call-control-root01/result.md)
and [full result](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/xyz-single-call-full-root01/result.md).

The logging-only candidate required a fresh control for its changed signature.
That control `012309` failed the exact XYZ readback before any kill or later
region/enemy qualification. One 12-byte request succeeded, but the qualified
frame 6750 sample was 555.928955/-1100/-2353.774170, not 555/-1100/-2356.
No retry, restore adjustment, tolerance or dependent full gate follows. The
agreed fallback is another two-pack room; a passive host stability observation
alone cannot qualify two waves. All checks and recorded FAILs remain. See the
[closed FAIL](../build/rig/vuh1502-native-waves-mac-relay-20261005-01/xyz-canonical-logging-control-root01/result.md).

Passive `015317` activated BC05/04's exact header25 stage0 pack: records
55/70/71/72/object302 at HP20 and73/object303 at HP98. Five independent fresh
samples through12 seconds kept exact native/publication IDs, HP, actor/status/
controller bindings and full record bytes. Native bit8 was true initially,
false at offset9 and true again at12. The terminal15-second census was unsafe
after a native frame1399 hit took Sora from HP24 to0. The no-writer collection
aborted globally before courtyard05/06. It remains FAIL, with no complete
stability, second-pack or join proof. The reviewed next step is one no-kill
BB04 control with the existing unchanged wave protection; a full five-then-three
gate follows only its PASS. Every fresh region, identity, HP/death and no-extras
guard stays. See the [closed passive result](../build/rig/populated-room-stability-launch-prep-20261006-01/live-root01/result.md).

Protected BB04 control `021001` remains FAIL at the terminal15-second fresh
bit8 check. The same five enemies kept exact native/publication bindings and
HP20/98; Sora remained alive at HP24 and team0. Position moved outside the
region, but the retained sequential reads and logs do not establish its cause.
Its one control is consumed; no full gate or retry followed. All saves and
owned PC/Mac closure passed. The bounded source review finds no region-bit
precondition in the selected diagnostic hit entry; ordinary emission instead
uses region admission with separate cooldown, weight/cache and stage gates.
The advisor clarified the original goal and approved relocating fresh bit8 to
emission points in a new declared fixture. Every HP/ID convergence, original
binding, exactly-once HP0 death, second-pack, no-extra and quiet acceptance
assertion stays. The changed method requires a fresh control; `021001` is not
requalified. No deeper entrance preserving this pack was established by the
bounded native lookup. See the [control FAIL](../build/rig/vuh1502-bb04-control-prep-20261006-01/live-root01/result.md),
[native-versus-fixture audit](../build/rig/vuh1502-bb04-displacement-review-20261006-01/result.md)
and [entrance lookup](../build/rig/vuh1502-bb04-entry-consumer-lookup-20261006-01/result.md).

Fresh emission-scoped control `023926` passed: all six samples retained the
original five enemies and HP, with bit8 required at admission and the later
false value retained. Full `025347` remains FAIL before the third damage call.
The first two host applications were98->91 and20->13; the exact whole-phase
checkpoint, deaths and second pack were not reached. Actual entry weights
were8/15, unchanged. Its two-round reader refused one raw flags9B8 change,
`0x4C83 -> 0x483` (xor`0x4800`), while the other returned fields agreed.
This does not establish the flag cause or final all-peer damage convergence.
Independent review accepted the closed FAIL; saves, execution pins and owned
PC/Mac closure passed. The advisor approved a new fixture comparison using
source-backed native eligibility bits, retaining raw words and every acceptance
assertion, followed by one fresh control and one full only after PASS. Old
FAILs remain. See the [control PASS](../build/rig/vuh1502-bb04-emission-control-prep-20261006-01/live-root01/result.md),
[full FAIL](../build/rig/vuh1502-bb04-emission-wave-launch-prep-20261006-01/live-root01/result.md)
and [independent review](../build/rig/vuh1502-bb04-emission-full-review-20261006-01/result.md).


## Selected native lifetime observer, 2026-10-05

Default-OFF `KH2COOP_LIFETIME_TRACE=1` observes factory3DF930, selected outer
destructor419AB0 and allocator19C470 only under the matching destructor scope
and original return419ACD. Originals, arguments, returns, LastError and native
SEH are preserved. A bounded POD queue drains on the owner frame; no native
actor/header/domain is read after release. Exposed module, hooks, unwind metadata
and MinHook dependencies remain retained until process exit.

Scope storage uses a retained Windows FLS slot and fixed owned POD contexts,
without retained pointers into any fiber stack. Thread/stack/key/token failures,
pool contention/exhaustion and depth overflow refuse observation and report the
first reason. Unknown stack changes and thread migration are unsupported. Windows
FLS initialization can allocate internally; no allocation-free OS claim is made.

The independent source review adopted the change. Normal137 controls and seven
isolated ASan fiber boundary cases passed; the combined ASan fiber-stack-reuse
run remains unqualified with its failures retained. Four current install-failure
modes also pass normally and under ASan after a control-only retention fix.

Live153816 qualified five courtyard Shadows and killed one natively, HP20 to0.
It captured one raw factory-return pair for the actor, then the selected outer
destructor and matching static-arena release pair on the same actual Windows
fiber. Both returned once; no guard refusal, loss, unwind or exception occurred.
No reload was needed; all four protected saves were unchanged. This is selected
path evidence, not death-bookkeeping, native creation authority, OS deallocation,
address-reuse proof or dead-pack recovery.150636 remains FAIL/NOT_EXPOSED.

Legacy `SPAWN_TRACE` and `NATURAL_RESOURCE_TRACE` stayed OFF in153816.
Their earlier TLS stack-scope pointers had a separately demonstrated interleaving
hazard; that did not establish a live crash cause. The separately reviewed legacy
fix now uses retained FLS-owned POD for factory/construction, lifecycle/predicate
and resource scopes. Its PREPARE=0 raw profile disables dispatcher/script hooks
and tick, geometry, enrollment, original-phase and first-emission associations.
PREPARE=1 refuses these diagnostics; core activation update policy is unchanged.

The full Release DLL builds from91 pinned source inputs.168 normal and168
isolated Windows ASan checks, plus nine installer controls, passed. Combined ASan
fiber-stack-reuse remains unqualified. Installed native run164208 ended FAIL/NOT_EXPOSED without a crash. Five HP20
Shadows qualified; no kill or retry was issued, and all four saves and91 source
inputs stayed unchanged. Its parser required initialized actor metadata at
wrapper return, exceeding the raw-return contract. Read-only inspection found
the target's exact fixed-wrapper/factory return,55 construction rows,18 resource
groups and later natural predicate/script return pairs. Wrapper-time status/HP
metadata remains unavailable and cannot be filled from the later current list.
The corrected parser retains that distinction and every scope/record/fiber check;
the original failure remains. Generated-wrapper exposure is absent.

The separately reviewed additive health export appends `flsRefusedCumulative`
and `flsFirstReason` to each factory, lifecycle and resource owner summary.
They sample existing process-lifetime atomics and never reset. They count refusal
calls, not unique fibers or native calls; first reason follows successful CAS
order. Require both zero from all three stores in a later owner summary after
the selected observed returns. Missing/nonzero/unknown/regressing fields refuse
qualification. Independent loads are not an atomic event seal; earlier zero
samples cannot cover later calls. The seven-file delta builds in the full Release
DLL, and18 focused actual-getter/log-format controls pass. No native hook, storage
or activation behavior changes. Fresh no-kill run170022 observed one exact fixed raw return/construction/resource
join and explicit zero count/reason samples from all three stores, with installation
and loss checks passing. Its selected natural predicate was absent within15s,
so the combined profile remains FAIL/NOT_EXPOSED; after-predicate qualification
is not established. No kill, reload, retry or crash; four saves unchanged. See the
[native health result](../build/rig/vuh1508-dead-pack-prep-20261005-01/legacy-health-live-root01/result.md).

The single-kill diagnostic173000 then passed: five fresh HP20 Shadows, one canonical
20-to-0 kill, one normal death-bookkeeping return and selected removal-predicate
returns, followed by later zero-refusal/zero-reason samples from all three stores.
Four living HP20 Shadows remained. The prior171900 pre-kill FAIL is retained; its
fixture compared transient physics bit22 as identity. The corrected fixture permits
only that bit to differ between completed qualifications; pointer, HP, deletion,
membership and safety gates remain. No retry, reload, retired-pointer read or
reconstruction; four saves unchanged. This qualifies the selected raw-return
observer, not a complete emission history or dead-pack recovery. See the
[selected native death result](../build/rig/vuh1508-dead-pack-prep-20261005-01/legacy-health-death-live-root02/result.md).

No lifetime, incarnation, creation or dead-pack recovery authority follows. See the
[selected FLS review](../build/rig/vuh1508-dead-pack-prep-20261005-01/dead-pack-lifetime-observer-fls-review/result.md),
[legacy source review](../build/rig/vuh1508-dead-pack-prep-20261005-01/legacy-native-trace-fls-diagnostic-review/result.md),
[native raw result](../build/rig/vuh1508-dead-pack-prep-20261005-01/legacy-fls-live-root01/result.md),
[parser correction](../build/rig/vuh1508-dead-pack-prep-20261005-01/legacy-164208-audit/result.md)
and [health export review](../build/rig/vuh1508-dead-pack-prep-20261005-01/legacy-fls-health-review/result.md).

## Current resync binding, 2026-10-04

The current protocol9/WorldBridge11 resync path now qualifies the complete
ordinary native catalog and exact actor-to-record membership before any HP/death
write. Full projected header/ordered record bytes and native record index supply
the resync association; spawn point and manifest observation order cannot supply
a fallback while fenced. This does not change the historical ordinary-message
key or establish controller incarnation, continuous lifetime or creation.

The unchanged 89-step courtyard population fixture passed in201.7s on one PC
over loopback (`20261004-043836`). The host and both friends qualified all10
definitions/26 records and five living references; each friend returned five
native20-to-requested17 positive stores, then observed all five atHP17/max20 on
two distinct native frames. Fresh independent censuses/hash samples preserve
the original IDs1-5/object302/type4 and progress. All101 prelaunch inputs/four
saves remained unchanged through terminal verification, and all8 owned
processes exited. [Exact result and evidence](FORCED_RESYNC.md#native-record-content-candidate-2026-10-04).

This is living Bootstrap content/HP acceptance. The outside-region first-rejoin
FAIL (5/0/5), zero complete ten-cycle repetitions and disabled creation remain;
the absent surviving pack still needs safe reconstruction. Positive geometry,
dead recovery and physical remote acceptance remain open. The observations and
chosen ordinary key below retain their original historical scope.

## Method

`tools/scenario/spikes/enemy_parity.py` (2026-10-02):
- Boot two rig instances from the same save.
- Per room:
  - Warp both instances there (default battle program, or a forced one with
    `--btl`).
  - Keep both Soras untouchable.
  - Drive both with the same inputs, interleaved, about 1 s apart.
  - Record each enemy's first sighting on each instance: name (objentry),
    entity-list order, actor address and position. Enemies are objentry type
    3/4; F_ objects are excluded.
- Later waves: kill every enemy on both sides with `kh2ctl hit kill`,
  repeating rounds until both lists are empty, then record what spawns
  next.
- Rooms: from `tools/scenario/spikes/combat_rooms.py`, a sweep of 224
  world/room pairs with default programs plus 144 with forced battle
  programs 1Ã¢â‚¬â€œ3.
  - On this save, six rooms spawn enemies with default programs. Forced
    programs add many more.
  - 40 rooms were held by events or cutscenes.

## Results (first wave; 14 room setups, 3 worlds)

| room (btl) | enemies | count A/B | name+order match | address match | spawn pos delta median / max (units) |
|---|---|---|---|---|---|
| 05/00 | M_EX520 | 4/4 | 100% | 75% | 13.7 / 13.7 |
| 05/00 btl 3 | M_EX050, M_EX690 | 3/3 | 100% | 67% | 0.0 / 1.6 |
| 05/06 | M_EX020 | 5/5 | 100% | 80% | 0.0 / 0.0 |
| 05/08 | M_EX020 | 3/3 | 100% | 100% | 0.0 / 0.0 |
| 05/09 btl 1 | M_BB010_SWORD, M_EX520 | 3/3 | 100% | 100% | 13.7 / 13.7 |
| 05/0B btl 2 | M_EX520, M_EX690 | 6/6 | 100% | 0% | 6.9 / 6.9 |
| 05/0C btl 1 | M_EX020 | 3/3 | 100% | 100% | 0.0 / 0.0 |
| 08/05 btl 1 | M_EX660 | 5/5 | 100% | 0% | 15.6 / 15.6 |
| 12/03 | M_EX020, M_EX420 | 4/5 | 80% | 40% | 272 / 603 |
| 12/06 btl 2 | M_EX880, M_EX920 | 4/4 | 100% | 100% | 3.8 / 33.5 |
| 12/0B btl 1 | M_EX900, M_EX920 (two sub-waves) | 6/6 | 100% | 100% | 0.0 / 0.5 |
| 12/0C btl 2 | M_EX880, M_EX910 | 4/4 | 100% | 100% | 14.9 / 14.9 |
| 12/0E btl 1 | M_EX950, M_EX990 | 3/3 | 100% | 100% | 0.0 / 0.0 |

Position deltas of a few units come from the instances first sighting an
enemy at slightly different moments of its spawn animation (inputs run about
1 s apart). 08/00 is left out: its type-3 actor `B_MU110` has no status block
(HP reads Ã¢Ë†â€™1) and isn't a combat enemy.

### Findings

- **Same set, same order, same spawn points.** 12 of 13 rooms matched
  exactly on name and spawn order, with spawn positions equal within the
  sampling noise above. That includes a timed second sub-wave (12/0B:
  four M_EX900, then two M_EX920 about 3.8 s later).
- **The battle program decides the set.** 05/00 spawns 4Ãƒâ€” M_EX520 with the
  save's program but M_EX050 + M_EX690 with btl 3. So it has to be in the
  key.
- **Actor addresses are not a key.** Addresses matched 0Ã¢â‚¬â€œ100% depending on
  the room (05/0B: every address on one side offset by exactly 0xA0, i.e.
  one extra allocation). Actor pool slots depend on allocation history.
- **Behaviour diverges immediately after spawn.** Positions drift apart
  within a second (each instance runs its own AI), as D5 expects. The host
  has to own enemy state.
- **Continuous spawners diverge (12/03).** The first spawns match. After
  that, Shadows keep appearing at times and places that depend on where
  Sora is, and the counts differed (4 vs 5) in both runs.
- **Wave ends.** With every enemy killed on both sides, battles ended the
  same way on both: no further wave in any of the rooms tested. The one
  asymmetric "wave 2" (12/0B, first run) came from a kill round that missed
  two enemies on one side, which kept that side's battle running. With kill
  rounds repeated until both lists were empty, it disappeared.

## Chosen correlation key

`(world, room, battle program, spawn sequence index, objentry name/id)`.
The spawn sequence index is the order in which the room's spawner emits the
enemy, counted from room entry. The key is type-checked on every message,
and the actor address is never used.

Match rate on this data: 12 of 13 rooms at 100% and 1 of 13 (a continuous
spawner) at 80%, so 53 of 54 first-wave enemies were matched by the key.

## Fallback for spawners (scoped, not built)

For rooms whose spawner emits enemies over time based on player position
(12/03 class), use host-commanded spawns:
- Clients suppress that spawner's local emissions.
- The host sends `{spawner key, objentry, position}` per spawn, and clients
  create the enemy through the same spawn call the spawner uses.
- The spawner's identity (room + btl + spawner entry index) still comes from
  the static room data.
- Detection: a room is a "spawner room" if a host spawn arrives with no
  local match within a short window; it can then be marked statically per
  room.

## Limits

- One save (BC first visit), three worlds (05, 08, 12). Forced battle
  programs stand in for story progress.
- Spawns were triggered with the same inputs about 1 s apart; a real
  session has players arriving at different times. That is exactly where
  continuous spawners diverge.
- No boss fight was reachable on this save (see VUH-1501).

## Step 1 implementation (VUH-1502)

`inject/src/EnemySync.cpp`, over the WorldBridge. The role comes from the
runtime's session slot (`WorldBridge::LocalSlot`: 0 = host, 1Ã¢â‚¬â€œ2 = client);
`KH2COOP_ROLE=host|client` overrides it for tests.

- **Room instance.** Native transition-request and load-completion hooks in
  `Warp.cpp` advance separate generations. Requests and completed loads clear
  cached enemy actors, including same-room reloads; spawn tracking pauses
  while a transition is pending. A new instance starts on a gameplay frame
  after native completion. NOW changes and frame stalls alone do not establish
  arrival; lifecycle details are in `pointer_map_v1.md`.
- **Host.**
  - Every instance: `epoch++` and a `RoomTransition`. When hosting starts
    mid-room (the runtime connected late), the room and everything already
    spawned go out at once.
  - New enemies go out as `EnemyManifest` (netId = spawn index + 1).
  - `EnemyHp` goes out about every 6 frames.
  - `EnemyDeath` is sent once per netId, when HP first reaches 0.
- **Host despawns.** Some enemies leave the list alive: the courtyard
  Shadow on its ledge spawn point, host only; and Dusks (M_EX900), which
  vanish and reappear as new actors.
  - If a new enemy appears at the same spawn point, the old one was
    superseded and clients re-bind to it.
  - If none appears within 3 s, it's reported as a death, so clients don't
    keep a copy that would hold the room open.
- **Client.**
  - Replays each accepted host epoch through the native transition path.
    Arrival requires a load completion after that request, the playable-state
    gate, and exact world, room, entrance, map, battle and event programs.
    Enemy matching waits for arrival; `TransitionAck` retries if the outgoing
    bridge ring is full.
  - Matches each local spawn by spawn point + objentry to the host's newest
    enemy at that point. Spawn index is the fallback.
  - A new local spawn binds only to a live host entry, and waits for the
    manifest otherwise. A bound copy follows re-binds, including to a dead
    entry, which is how deaths reach it.
  - Holds matched HP at the host's absolute value (never writing 0).
  - Applies host deaths through `ApplyStatDelta(-hp)`, the native death
    path.
  - Zeroes its own hits on enemies (`[drop]`): the host owns enemy HP.
- **Why spawn points rather than spawn order.** Spawn order breaks as soon
  as one machine despawns and refills a point and the others don't.
  Spawn points matched to 0.0 units across instances in the spike above.

Verified 2026-10-02 (`net_enemy_sync_courtyard`, `net_enemy_sync_waves`:
host + 2 clients, relay, host-only damage):
- **HP:** the same on all three after host damage (courtyard 20 Ã¢â€ â€™ 13,
  12/0B 160 Ã¢â€ â€™ 153), read 0.4 s later.
- **Deaths:** every host kill killed each client copy exactly once (5 and 6
  deaths per client).
- **12/0B second wave:** spawned on all three, bound to the host's new
  entries and cleared.
- **Battle end:** battle state ended (0) on all three.

**Transition-lifecycle regression evidence, 2026-10-02.** The
[courtyard rerun](../build/scenarios/20261002-145659_net_enemy_sync_courtyard_1/report.json)
failed its after-client-hits population/HP equality check. The original five
Shadows were at 13 HP on every instance after host damage and remained there.
Client 1 independently spawned a sixth Shadow at `(-40,-1,-349)`, with 20 HP
and no host manifest match. Its four subsequent hits targeted that actor and
were zeroed ([client log](../build/scenarios/20261002-145659_net_enemy_sync_courtyard_1/kh2coop_inject_99000.log)).
The failed assertion's message describes changed enemy HP; the raw samples
show a real extra-client-actor mismatch. No further epoch or load occurred
during that mismatch. Keep this failure as unresolved spawn divergence.

The earlier passing courtyard run also had native despawn/refill variation,
eventually settling at four enemies. The new lifecycle intentionally performs
a real initial follow reload even when the location matches; the old fixture
only recorded that first epoch. Player/camera trajectories were not captured,
so these reports cannot establish why the additional spawn appeared. Shared
HP/death checks on fixed scripted waves provide separate lifecycle evidence;
they do not retire the courtyard population mismatch.

The [fixed-wave rerun](../build/scenarios/20261002-150336_net_enemy_sync_waves_1/report.json)
**passed** with the new lifecycle (154.8 s). Four initial enemies changed from
160 to 153 HP on all three instances after host damage. The second wave had
two 160-HP enemies on every instance; each client recorded six native death
applications in total, and battle state ended at 0 everywhere. Four save-file
hashes were unchanged. This verifies fresh bindings and HP/death application
after real join reloads, while leaving spawn convergence open.

**Current first-pack evidence, 2026-10-03.** The
[provenance run](../build/scenarios/20261003-001955_net_enemy_census_waves_provenance_1/report.json)
passed in 170.6 s: complete native censuses showed two object-309 and two
object-311 enemies per peer, zero unmatched bindings and shared 160 to 153 HP
after host damage. Four subsequent native kills applied once per client.
Its second wave was empty and the native controller count remained two;
six host death announcements included two earlier alive-despawn policy events.
This is bounded first-pack replication, not multi-wave or barrier acceptance.
The [independent audits and limits](SCENARIOS.md#native-enemy-census-diagnostic)
retain the earlier strict divergence and incomplete creation provenance.

**Actual-state hash evidence, 2026-10-02.** The
[nonempty hash run](../build/scenarios/20261002-184220_net_statehash_nonempty_1/report.json)
passed with three instances (180.2 s). Four live native enemies had matching
bindings, object IDs and 160 HP on all peers, then 153 HP after host damage.
The scenario independently recomputed the published enemy hash from each
peer's native observation rows. Unmatched living actors participate in the
hash with net ID 0; missing, extra or duplicate actors cannot be hidden by
hashing the received host cache. Positions are excluded while AI runs locally.

That run used actual native actors observed by the per-entity update hook,
rather than the received host cache. Callback coverage alone did not establish
that every linked native actor entered the hook. The subsequent independent
census below resolves that uncertainty for its sampled courtyard reproduction.

The progress hash reads 8,108 masked, verified SAVE bytes. A client-only
change to chest flag 409 produced a different actual progress hash and the
relay's progress mismatch bit (`fields=4`). Restoring that bit restored
agreement; complete before/after snapshots showed no remaining changes in
the shared ranges or sampled character, inventory, munny and EXP ranges.
All four on-disk save hashes were unchanged. This proves detection and
restoration of a progress mismatch; forced resync, a deliberately failed
native enemy death, and hash agreement across the full transition suite need
separate evidence. At that checkpoint, the protocol 8 fresh transaction was verified offline; native
acceptance remained pending ([FORCED_RESYNC.md](FORCED_RESYNC.md)); historical
dead targets remain explicitly unavailable without safe identity proof. The
courtyard population failure above remains open.

The enemy negative-control runs remain **overall failures**. In the
[two-participant run](../build/scenarios/20261002-192119_net_statehash_enemy_negative_isolated_1/report.json),
the detection checkpoint passed twice: a client HP-lock bit prevented the
native lethal call (`153 -> 153`), the actual living target remained in its
hash, and the relay reported `DesyncEnemies` (`fields=2`). The temporary bit
was restored with native identity/epoch checks, preserving other flags.
The later reload reached epoch 2 on both peers but failed because the native
friend pointers were null and no remote Sora actors existed in `12/0B`, battle
program 1. Logical avatar poses alone did not satisfy the native actor check.
This establishes detection/bit restoration, not successful reload recovery.
The [separate party diagnostic](../build/scenarios/20261002-194936_net_party_availability_probe_1/report.json)
passed its observation steps (95.3 s), without combat or the enemy fault.
Both peers had valid companion pointers in the loaded-save room, then null
friend pointers on first native entry into `12/0B` **before networking**.
They remained null at settled connected epoch 1 and identical reload epoch 2.
The world-18 party word stayed `0x12121200` throughout; each actor sample
contained only local Sora, PRIZE and W_EX010, while connected logical puppet
poses were active. All four on-disk saves were unchanged. This establishes
preexisting native companion absence in this fixture, consistent with plan
risk R12; it does not establish native puppet support or change the failed
negative-control recovery result.
The earlier three-participant controls also exposed an independent client-2
spawn of an unmatched 160-HP object 309. Keep those failures and the strict
population/puppet checks; neither spawn convergence nor recovery is complete.

The [20-load hash route](../build/scenarios/20261002-193320_net_host_transitions_statehash_acceptance_1/report.json)
also **failed** (203.3 s), after ten passing fresh hash checkpoints. At route
transition 8, epoch 10, all three peers had arrived at the complete location
`05/06, door 0, map 1, battle 1, event 0`; native puppet checks passed. The
host published no observed enemies, while both clients published five living,
unmatched object-302 actors at 20 HP. Their enemy hashes differed
(`401581688` versus `712275580`, relay `fields=2`), while all progress hashes
were `105337703`. All four on-disk save hashes were unchanged.

That report did not capture an independent native-list snapshot, so it could
not establish host absence. Native spawn appearance-ID caches and controller
state also lie outside the SAVE progress hash. The subsequent checked census
below supplies those missing observations. The strict route remains failed;
matching progress bytes alone do not prove matching enemy activation.

The [subsequent checked census](../build/scenarios/20261002-200856_net_enemy_census_transition08_1/native_enemy_census_transition08.json)
collected three complete native-list/cache snapshots on every peer; the
retained strict hash check still **failed** (208.9 s). The first snapshot had
zero native enemies and an empty active cache everywhere. The later two had
zero host enemies and five living client enemies, agreeing with each published
hash population. This establishes real spawn divergence in this run, without
a hidden host actor in the update registry.

The same type-2 controller (header ID 30) had flags `2` on the host and `10`
on both clients after their appearance, while each client cache gained record
IDs `11,12,13,14,18`. The host cache stayed empty. Its activation actor remained
near `(45,-260,1950)`; the clients moved near `(-1019,-180,1636)` and
`(-923,-206,1638)`. Native analysis identifies controller bit 3 as an accepted
activation region. These observations support local activation as the next
authority boundary to investigate; they do not validate a hook or suppression
strategy. Initially equal caches also rule out differing retained IDs as a
sufficient explanation for this particular reproduction. All four disk save
hashes were unchanged.

**Checked native-list coverage.** EnemySync now commits presence only from a
complete canonical native-list traversal with checked metadata, links, tail,
handle regions and lifecycle. A failed read, cycle, changing list or continuation
beyond the 256-node safety bound makes the entire sample unavailable; it never
publishes a truncated or invented empty roster. Unknown intervals restart
despawn grace and suspend enemy writes/messages/hashes. Checked native target
identity guards HP/lethal calls, and a fresh post-application census supplies
hash rows independently of tracker presence. Unmatched live client actors
remain visible. This corrects coverage; it does not synchronize spawn activation.

The [native-wave regression](../build/scenarios/20261002-201456_net_enemy_sync_waves_1/report.json)
passed (153.6 s) after the change: two real second-wave enemies, six native
deaths on each client and battle state zero everywhere. The
[nonempty hash regression](../build/scenarios/20261002-201730_net_statehash_nonempty_1/report.json)
also passed (166.1 s), including actual HP/hash agreement and reversible
progress mismatch detection. Its later empty second-wave observation remains
separate from the real-wave proof. All four disk save hashes were unchanged.

The [focused native-death detector](../build/scenarios/20261002-202126_net_native_census_death_control_1/report.json)
passed (102.4 s) on the same DLL: the HP-locked client target survived its native
lethal call, appeared in two fresh actual hash samples with relay `fields=2`,
and its temporary bit was restored. All four saves were unchanged. The fixture
ends after detection/restoration; it does not replace the still-failed full
negative-control reload-recovery scenario.

The [follow-up geometry diagnostic](../build/scenarios/20261002-204742_net_enemy_census_transition03_1/native_enemy_census_transition03.json)
is **FAIL** (164.6 s): one changing actor identity made the first client sample
unavailable, so its trailing strict parity check never ran. Eight of nine native
and causal snapshots were complete. Their seven BOX descriptors, inverse
matrices and extents matched across peers. In both later snapshots, sampled
host activation was outside all seven regions while both clients were inside
regions 5 and 6, away from the numerical uncertainty bounds. These are checked
reads and offline float32 calculations, not native predicate-return tracing.
They strengthen the local-activation explanation without turning a partial
diagnostic into a passing acceptance test. Four disk saves stayed unchanged.

**Scoped native activation authority.** `NativeSpawnController` hooks verified
`0x3FF000` calls from the ordinary controller task. Qualification checks current
table/key membership and every record: type-2 combat, delayed mode 2, fixed
position mode 0, finite coordinates and actual direct-ID objentry type 3/4,
excluding `F_` objects and dynamic aliases. The controller type is one byte;
header byte 1 is flags. No room, header-30 or enemy-ID allowlist grants authority.

Clients request an exact epoch/full-location host-native float4 every 100 ms.
A response must echo their incarnation and request identity and come from a
native hook capture after the host consumed that request. Its lease ends at the
original client request time plus 500 ms; receipt cannot renew it. This bounds
capture age including bridge, relay and network backlog without comparing PC
clocks. The relay verifies the sender, stamps the requester and routes only
matched challenges. Both packets are ephemeral and never enter late-join or
resync snapshots. Visual puppet poses do not supply activation authority.

Current WorldBridge v11 retains the immediate session generation introduced in
v5 and adds exact generation/delivery reset markers and per-peer delivery floors.
Protocol 9 retains the producer source identity introduced in protocol 8; these fences do not replace
fresh activation challenges. Native use stays unarmed until that exact marker
arrives. Native transition/load changes, including same-room reload, invalidate
the lease. Host capture and response flush require safe gameplay, the announced
epoch/full location and unchanged native serials; flush follows the fresh census
and manifest barrier. A qualified client calls the original update once with a
fresh copied host point, or explicitly holds the whole native tick when authority
is unavailable. Hold pauses flags/cooldown/emission and never catches up skipped
ticks. Host/off roles and unsupported controllers retain native execution.

Qualification read failures pass through with coverage diagnostics: an unknown
controller may produce events, so it cannot be broadly held. Mixed/dynamic or
initializer records, type 9, cache/stage divergence and transient triggers remain
outside this boundary. A recent sampled point does not guarantee every host
spawn decision; host-commanded emission remains D5's fallback. Native census and
strict unmatched-row hashes continue to expose those gaps. Portable lease/relay
tests passed with ASan/UBSan, and Windows bridge tests and the Release DLL build
passed. The [original 20-load route](../build/scenarios/20261002-205416_net_host_transitions_statehash_acceptance_1/report.json)
now passed (246.9 s), preserving all location, native puppet and strict hash
checks, including the previously divergent room. Its accepted hash populations
were empty; this proves the scoped client-only trigger regression, not nonempty
spawn/HP/death convergence or the full P2 hash gate. Four disk saves stayed
unchanged. The [source-expiry control](../build/scenarios/20261002-210128_net_activation_lease_menu_1/report.json)
also passed (93.5 s). START produced an observed native unsafe menu/pause gate;
the client stayed safely in field and both runtimes kept reporting connected
network traffic. After a wait exceeding one second, fresh qualified Hold counts
rose `505 -> 749` while Apply stayed `388` and no new lease was accepted. Resume
accepted a newer source (`752 -> 836`) and Apply advanced to `389`, with the same
controller, epoch, native serials and full location. The report retains the gate
predicate, not its raw values; its screenshot does not prove visible menu UI.
The exact 500 ms expiry is the portable lease contract; this live control proves
cessation and recovery beyond that bound. Four saves stayed unchanged.

The [new native-wave regression](../build/scenarios/20261002-210512_net_enemy_sync_waves_1/report.json)
**failed** (155.5 s) before native deaths or second-wave acceptance. Before damage,
the host had four object-309 enemies while each client had two 309s and two
unmatched object-311 enemies. Equal total counts had admitted unequal identities.
All four selected host actors actually took `160 -> 153`; surviving matched
client netIDs 3/4 also reached 153. A damaged host actor then left alive and
refilled at 160 HP before the snapshot. The strict failure is real population
and lifecycle divergence, not list order, missed damage or demonstrated matched
HP transport failure. The six-record controller 115 was qualified and used
leases in this room, but the run did not capture the 311 producer/provenance or
prove that lease holds caused the difference. Four saves stayed unchanged.
Keep this failure alongside the earlier passing wave run; the scoped activation
change has not established nonempty wave convergence. Creation, removal and
controller bookkeeping need correlated native evidence. Supported nonempty
activation and those new diagnostics are prepared offline, pending live runs.

**Historical offline creation and lifecycle diagnostics.** The following records
the earlier TLS profile, not the current PREPARE=0 FLS profile described above.
Its dispatcher/script/tick associations are not certified by the current build.
Exact opt-in
`KH2COOP_SPAWN_TRACE=1` adds independent byte-verified observers for all callers
of fixed wrapper `3FE590` and generated wrapper `3FE650`, dispatcher `3FE320`
and script callback `42DC10`. Each original executes once with its return value
preserved. Events retain raw record bytes, actual returned actor metadata,
generated point where applicable, full NOW/load/transition stamps and separate
wrapper and enclosing `3FF000` controller/cache snapshots. Script and dispatcher
scope are explicit dynamic enclosure; the script's native tail jump can yield
a DLL dispatcher return address, so unavailable caller ancestry remains visible.

Five independently verified lifecycle probes observe removal bookkeeping
`3FFD90`, disposal `3B45C0`, lethal notification `3D4A40`, death bookkeeping
`3FED10` and count decrement `3FED40`. Nested events retain entry sequence,
parent/depth and checked pre/post actor/controller/cache facts. Disposal may
invalidate poststate; count decrement has no actor argument. Neither missing
poststate nor a later disappearance proves death or allocation reuse. Native
exceptions continue to their original handler, with interrupted poststate and
restored scope. Fault counters count observer filter hits, which can include
several nested observations of one exception.

Only the registered game thread samples role and Warp serial callbacks.
Unknown/foreign threads preserve raw facts with unavailable role/stamps. The
game-thread drain records events even when current correlation is unavailable.
Fresh post-apply native census plus identity rereads establish current membership;
binding additionally requires one sampled generation matching current, activation
and consumed ordered-reset generations. `bindingEpoch`/netId is a current hint,
not a creation incarnation, and is never derived from emission order.

Per-hook availability, partial installation, unsupported callers, unavailable
reads, queue loss and interrupted events remain explicit. Generic factory calls,
unhooked subtype removal, actual deallocation and producers outside these
boundaries are unobserved. Same-address reuse and controller reincarnation remain
unproven. Bounded queues, missing shutdown drain and diagnostic overhead prevent
full coverage claims. This instrumentation changes no creation authority or
activation lease/Hold/Apply policy and does not repair the failed wave. At that offline checkpoint, the
strict wave diagnostic and separate nonempty activation control were prepared;
no live trace run had occurred while James used the desktop for Rivals.

The expanded Release DLL and both Windows headless targets build with native
tests under MSVC `/W4 /WX`. Spawn tests passed 38 checks and lifecycle tests
24, then both passed in the isolated Windows AddressSanitizer build. Tests use
actual observer code, synthetic originals and test-owned memory, including
exception propagation, original-call count, nested/TLS recovery, queue loss and
real foreign-thread controls. They do not execute game functions, install hooks
or validate live ordering/census bindings. The saved-log auditor passed 37
controls and rejects the archived untraced wave logs as incomplete evidence.
Activation deadline evidence is reused for unchanged inputs. Subsequent relay
claim-epoch and co-op host-loss changes have fresh world/fake/avatar/bridge
checks, Windows ASan and portable relay ASan+UBSan evidence in the
[relay lifecycle receipt](../build/rig/relay_lifecycle_offline_receipt.json).
The Windows native trace path has no UBSan evidence.
Fresh logs and source/DLL hashes are in the
[expanded offline receipt](../build/rig/native_trace_expanded_offline_receipt.json).
The [earlier fixed-only receipt](../build/rig/spawntrace_offline_receipt.json)
retains its historical 19-check scope.

Not covered yet:
- Bosses: none reachable on this save.
- Drops and barrier objects (battle state stands in for "barriers lift").
- Continuous spawners.
- Broader client-hit authority (VUH-1503): native courtyard acceptance now
  confirms 22 ordinary client claims and three Shadow kills, delivered once to
  both clients with matching applied HP/population/progress. Boss finishers,
  survival/caps, attack-specific effects, victim-side enemy attacks and native
  reconnect remain unproven. See
  [native D4 evidence](SCENARIOS.md#automatic-client-hits-native-courtyard-acceptance-2026-10-02).
- Hash agreement across repeated native room loads and varied populations,
  plus successful recovery after the enemy mismatch control. The nonempty
  proof covers one fixed-wave room; it does not establish spawn convergence
  or native remote actors after its battle-room reload.
