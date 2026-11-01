# Axel hazard inventory (VUH-2914)

**Partly qualified against installed bytes** (VUH-2914 comment of 2026-10-29,
capture `vuh2914-installed-02`). The installed Axel AI, BAR entry type 3 `b_ex.bdx`,
is byte-identical in `B_EX110.mdlx` and `B_EX110_SKIRMISH.mdlx`; its label-assisted
OpenKH decode matches the public text below in 6785 instruction lines with 0
differences, label PCs included. Status marks:

- **Q-AI:** the claim comes from the AI text and holds for the installed AI. Any
  asset, native or encounter-wide dependency named in the same row stays U.
- **Q:** also qualified against installed bytes by that comment: the ARD spawn
  argument split, the ATKP rows for 427/429/432/434/436/503, and the 0.75/0.4
  phase thresholds.
- **U:** still unqualified. This includes which MSET/ANB triggers fire 427, PAX
  identities of effects 10/11/12/38/47, collision rows, native trap ABI, thread
  cancellation and retry/load teardown, and the ATKP table's 277-byte trailer.

No game behavior is enabled by this document.
The source is the [public B_EX110 disassembly](https://github.com/thundrio-kh/kh2-ai-decomp/blob/b89a02966494aab1b5feeb80a75e503fd6d80e93/bdscript/obj/B_EX110/b_ex.bdscript),
commit `b89a02966494aab1b5feeb80a75e503fd6d80e93`. Its Axel I and II text files
are byte-identical (SHA256 `5f6f28de99f362cf99e3f3b2d0de19136307371748d99f6913d62dd9f3963617`).
This hash identifies public text, not an installed BDX or a qualified executable.
Raw source and future extracted game content stay in ignored `.local/vuh2914/`.
Only hashes, derived tables and our extraction/check scripts may be published.

**Q (ARD spawn split, 2026-10-29 VUH-2914 comment):** the installed ARDs give Axel II as
`tt20.ard/b_40`, object81, Argument1=0, and Axel I as `tt05.ard/b_43`, object2229,
Argument1=1 (both SpawnType3, SpawnArgument0), and both MDLXs carry the same AI.
**U:** the remaining identities recorded by [the earlier RC inventory](AXEL_REACTIONS.md),
Axel II `02/14 btl137` / `TT20_MS603` and Axel I `02/05 btl87` / `TT05_MS404`,
were not rechecked by that capture.

## Phase decisions

| Status | Derived predicate or action | Public code labels | Installed confirmation |
|---|---|---|---|
| Q | Initialize phase word +24 to0, HP-rate thresholds +64/+68 to0.75/0.4 | `L4842`, `L11946`, `L11992`, `L12236` | Q (2026-10-29 VUH-2914 comment): the installed AI, the same in both MDLXs, pushes0.75 and0.4 at `L4842` into `L11946`. U: sheet selection |
| Q-AI | Damage trigger: phase0 and HP-rate strictly below0.75 and argument1=0 → phase1; reset action counters and restart boss mode | `TR6`, `L3227`, `L3393`, `L3435` | Q-AI: installed `TR6` (`OBJ_DAMAGE`) path. U: native traps1/84,1/230 and rate denominator/scaling semantics |
| Q-AI | Otherwise phase1 and HP-rate strictly below0.4 and argument1=0 → phase2; clear `W4564`, reset counters, restart boss mode | `L3295`, `L3447` | Q-AI: same installed AI paths. Exactly-at-threshold controls must remain in prior phase |
| Q-AI | Phase selects normal/hard/super-hard battle and revenge actions; client must receive consequences, never run these selectors | `L5935`, `L6001`, action table in `L3554` | Q-AI: both AI entries. U: installed motion-trigger data |
| Q | Axel I cannot take either phase escalation because argument1=1 | `TR6` and only two escalation stores | Q (2026-10-29 VUH-2914 comment): ARD spawn args and the complete installed AI |

**Q-AI:** The public hard-mode branch starts `atk_assault` even when the prison
predicate fails. Only the prison re-start in that branch needs both `W4560==0`
and `W4564==0` plus its counter gate (`L10615`–`L10706`). The earlier RC document's
claim that *both* actions require those flags is too broad for this public source.
The installed AI now matches this text (2026-10-29 VUH-2914 comment); the earlier RC
document has not been updated here.

## Fire floor, boundary and other damage sources

| Status | Owner / occurrence | Start and change | Damage or geometry | End / confirmation bytes |
|---|---|---|---|---|
| Q-AI | Fire-floor workspace, child thread `D12313`, parent state+112 | Created only on argument1=0. State+64 starts at2. Battle-start and `atk_prison` call `L9049` → state0. State0 changes to1 and starts Axel effects11/12 plus auxiliary effect10/attack429. | While active, canonical-player HP is reduced through1/155, not an attack hit. Accumulator adds0.005 per loop only when `obj_is_star` is false; subtract integer portion when accumulator>1 and HP>1; clamp at1. No spatial/floor query or i-frame timer is visible in this drain path. | Clear Light `L10307`→`L10321` sets state2. Fade effect11 over1, end effect12 loop, wait for both inactive, kill retained handles, clear active flag. `L12328`–`L13047`. Q-AI: installed AI. U: PAX effects11/12 and native1/155/1/225; qualify them before any implementation. |
| Q-AI | Boundary watcher `D13075`, parent state+116 | Created only on argument1=0, independently of floor state. Watches player when source exists, source is not in damage motion and player fails `obj_is_star`; damage motion imposes30 wait. Eligibility includes `680 - playerXZDistance < 300`; that comparison supplies no outer-distance limit. | Projects toward radius670, lowers proposed Y by100 and performs native ground query1/141 with10000 range. Spawns an eruption at the resulting point. This is a conditional watcher; it is not a continuously colliding ring volume. | Source-not-existing or workspace stop field exits loop. Workspace destructor frees target/work memory. Native thread cancellation on retry/load is not established by the text. `L13058`–`L13500`. Q-AI: installed AI. U: native1/141/6/63 and workspace/thread lifetime. |
| Q-AI | Boundary eruption `D13526`, Axel-owned effect47 and attack503 | Effect47 starts and is positioned with0/38. Wait20 frame-time units, then create503 and issue radius arguments100/500 at the same position. | After60 more units free503; native attack kind, shape, element, damage and i-frames require ATKP503 and the native radius/strike paths. **503 is not the floor HP drain.** | Effect-active loop bounds lifetime; cleanup `L13692`→`L3117` kills effect and frees a retained attack. Q-AI: installed AI. Q: ATKP503 is `PierceArmor`, element1, Power0 (2026-10-29 VUH-2914 comment). U: effect47/PAX, attack ABI, native shape and i-frames. |
| Q-AI | Floor-on auxiliary `D12512`, effect10 / attack429 | Starts from floor-on helper `L12435`, position supplied through a workspace vector. Effect10 first; after20 units create429, radius arguments100/500; free after40 more units. | Separate from direct HP drain; actual collision shape and damage need ATKP429 and native radius semantics. | Effect lifetime and shared cleanup `L3117`. Q-AI: installed AI. Q: ATKP429 is `PierceArmor`, element1, Power100. U: effect10, collision shape and native radius semantics. |
| Q-AI | Ground-trail helper `D2883`, effect38 / attack432 | Reached through effect-trigger37 (`D2419`), with computed trail points. Effect38 starts at the point; an effectively immediate positive delay precedes432; radius arguments100/800; free after35 units. | Movement/point selection belongs to the host coroutine. There is no separate projectile actor creation in this helper. | Shared cleanup `L3117` kills effect/frees attack. Q-AI: installed AI, trigger37 included. Q: ATKP432 is `PierceArmor`, element1, Power25. U: installed motion triggers and effect38. |
| Q-AI | Moving attack family `D1659`, attack434 | Effect-trigger17 (`D1239`) emits up to30 workspaces, passing434 and duration36 into generic constructor `L1616`; position and radius arguments50/300 are updated by its host coroutine. | The effect-trigger and helper movement need the installed ANB/MSET and ATKP434; the text alone does not establish the visible asset or native collision shape. | Generic end `L1796`/`L1823` frees attack; parent tracks children. Q-AI: installed AI. Q: ATKP434 is `PierceArmor`, element1, Power75. U: ANB/MSET, every parent cancellation/destructor path and native thread teardown. |
| Q-AI | Expanding strike wave `D1964`, attack436 | Effect-trigger26 creates436 at the triggering effect position. Radius variable starts160 and increases by23×frame-time toward500; loop deadline20 units. | Explicitly loops native party slots0–2 and uses2/17 to strike eligible grounded actors in a radial band `[r-100,r]`, vertical distance<100, gated by attack-is-strike2/18. This scripted strike differs from a passive radius update. | Effect-active loop or deadline exits, then `L2293` frees436. Q-AI: installed AI. Q: ATKP436 is `PierceArmor`, element1, Power75. U: MSET trigger26, effect association and strike ABI; NPC/remote-player membership cannot be inferred from the text. |
| Q-AI | Motion-driven attacks including observed ATKP427 | No literal427 occurs anywhere in the public BDX, which matches the installed AI. | Cannot identify427, its geometry or element from these scripts. Installed MDLX collision rows, ANB triggers and ATKP records are required. | Q: ATKP427 is `NormalAttack`, element0, Power75 (SubId0) and25 (SubId1) (2026-10-29 VUH-2914 comment). U: which MSET/ANB triggers fire it, and its collision geometry. This is an unresolved inventory item, not proof of absence. |

**Q-AI:** Ring/floor state, light state, boundary watcher and individual attacks are
separate lifetimes. Clear Light cooling the floor does not by itself stop the
independently created boundary watcher in this text. Attack503 therefore cannot
be used as an oracle for whether the direct HP drain is active.

**Q-AI:** The public BDX has no separate actor-creation/appear syscall. Its hazard
helpers allocate workspaces (0/47), copy the source wrapper, duplicate a target
and start native child threads (2/20); their effect/attack owner remains Axel.
**U:** That does not prove there are no ARD/mission/engine-created hazard actors.
The installed complete spawn groups, mission AI and effect assets remain needed
for the requested encounter-wide absence proof.

**Q-AI and Q (ARD), Axel I control:** The argument1=1 branch creates neither floor workspace+112
nor boundary watcher+116, and vetoes collision group20. That proves absence only
of those two helpers in this public control flow. It does **not** prove Axel I
has no ordinary attack434, motion-trigger strike436, trail432, lighting or fire
visuals. **U:** installed motion/effect assets and both mission scripts must settle them.

## Lighting and mirror coverage

| Basis | Consequence | Current repository boundary |
|---|---|---|
| Q-AI | `atk_disorder` creates light0 (1/181), sets flag1 (1/182), color80/80/80 over60 (1/183), then fades over30 (1/184) in its callback | These four traps are absent from `ScriptMirror.hpp::kTraps`; new native pins/ABI are required. No public-BDX room bg-hide/show or bind-other-effect call was found. |
| Q-AI | Assault/disorder paths and their callbacks set sheet element-key6 rate to0 and restore100 through1/311 (`L7075`, `L7708` and later disorder/callback paths) | Qualify the installed sheet, element-key meaning and native call. No new client phase logic, scaling override or HP exception follows from this provisional read. |
| Q-AI + code inspection | Effect starts/positions/ends and attack new/radius/position/free are used by the listed hazards | Existing `kTraps` covers1/21,1/22,1/87,0/38,0/44,0/51,0/52 and2/10–13. Their existence does not prove live pairing of these installed resources. |
| Q-AI + code inspection | Direct floor HP drain uses1/155 | Not mirrored. No generic native HP-delta replay or client floor timer may be added from this provisional read. Canonical ownership, down eligibility and native invulnerability semantics need qualification. |
| Q-AI + code inspection | Expanding436 uses explicit enemy→party2/17 strikes | Existing `SmObserve`/`SmReplay` strike handling admits owner-routed player attacks against the paired enemy; it deliberately refuses an ordinary enemy-owned attack (`ownerSlot==0xFF`). It must not be generalized before qualifying this native path. |

**Code inspection:** Existing constructor/destructor hooks retire collision allocations before native
reuse. Resource IDs and held calls still require independent stale-start/end
controls; any proposed generic hardening must reproduce a bug in current adapter
code without relying on these provisional hazard semantics.

## Qualification and implementation gate

Ione's early judgement permits a separate portable lifecycle fix only after a
current implementation counterexample, with actual start/end wiring exercised,
valid lifecycle controls and unchanged authority/incarnation boundaries. It is
not approval of an unseen patch. New HP, light, boundary or native-call behavior
waits for installed byte/version/hash/ABI qualification and full review.

The Mac lifecycle fixture now reproduces duplicate effect-start replacement/orphaning
and a use-after-free when native attack free reenters allocation-forget during
room retirement. The scoped patch refuses an already-paired effect start before
native creation and snapshots/revalidates attack identities before retirement.
See [portable lifecycle evidence](testing/vuh2914-script-lifecycle/README.md).
This establishes adapter failures, not installed Axel hazard semantics.
The public text contains five `attack_new` call sites. Absence assertions above
cover the complete public text for those named traps, not the complete installed
encounter. Weapon-bound visual effects39–44, assault effects13–15 and leave
effect1 also need installed PAX/motion association; visual effect numbers do not
establish damage volumes.

The read-only extraction script must capture original assets, BAR entry ranges,
MSET/ANB triggers, collision rows, relevant ATKP rows and the selected native trap
entries/functions into ignored `.local/`. Required files and currently known hash
constraints are in its input plan. The selected original PE is expected to hash
`9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed`
(from the retained full install manifest, not a new PC read). Several older asset
hashes are available only as prefix/suffix constraints: they cannot certify full
byte identity. Record full hashes on capture and retain that qualification gap.

The portable lifecycle patch is separate from installed-byte qualification. No live
result is claimed. The PC is held by James; no PC read, extraction, build or run
is authorized by this inventory work.

## Next product work: Mac now versus installed-byte qualification

**(a) Mac now: existing ScriptMirror resource lifecycle.** A fresh seat can own
the following product patch and its failing controls without interpreting new
Axel native calls. This is a bounded implementation assignment, not a claim
that a live Axel failure or reachable stale packet has been reproduced.

| Files / functions | Concrete product work and control |
|---|---|
| `inject/src/ScriptMirror.inl`: `SmReplay` effect-start branch; `inject/src/ScriptMirror.hpp`: `ClientEffects::Put/Find/Erase` | Reproduce repeated current effect-start ID through the actual adapter. The current effect branch calls native start then unconditionally replaces that ID's pairing; attack-start already refuses an existing ID. If the adapter control proves replacement/orphaning, make effect-start admission preserve the current binding and produce no second birth. Preserve the existing effect/attack namespaces, owner, session, epoch and incarnation gates. This is lifecycle work on already-supported calls, not a new hazard hook. |
| `ScriptMirror.inl`: `SmApplyRecord`, `SmDrainHold`, `SmReplay`; `ScriptMirror.hpp`: `Hold::Put/CancelResource/Drain`, `ClientEffects` | Exercise start→end→late-start, end-before-held-start, old end after a replacement, owner/source loss and room/reset retirement through the real record/hold/replay wiring. Add retirement/admission state only for a demonstrated current failure. Unknown/unread identity stays unknown. Do not infer packet reachability from a map-only model: `EnemySync.cpp::AdmitScriptCall` and `common/src/NetworkClient.cpp` already reject old packet sequences. |
| `ScriptMirror.inl`: `SmRetireClientAttacks`, `SmForgetAllocation`, `SmResetRoom`; `ScriptMirror.hpp`: `ClientEffects::Each/ForgetAllocation/Reset` | Test reentrant destruction with a fake existing free callback that invokes the current allocation-forget path while retirement is visiting bindings. The current retirement loops the live map across a native callback; make traversal safe against that callback invalidating entries if the real-wiring control demonstrates it. Snapshot/revalidate existing identities; add no native reads, calls, offsets or teardown interpretation. |
| `tests/ScriptMirrorTest.cpp`, a focused actual-adapter lifecycle fixture if needed, and `CMakeLists.txt` only for its target | Keep valid current start/update/end, same handle/address reuse, duplicate start, stale start/end, unread identity, unrelated owner/resource and reset controls. A portable state model alone is insufficient: the actual adapter must call the tested admission/retirement path. Build only `kh2coop_script_mirror_test` plus the focused adapter target and run their selected ctests through `clankie heavy --`. |

Reproduce before changing behavior; if a candidate is already excluded by actual
admission, record that result rather than add speculative state. This work must
remain independent of D12313/D13075 semantics. Ione's full independent review
still gates the exact inject patch before game build/live use. The original
inventory checkpoint contained no patch; the portable lifecycle result above
records the separate Mac follow-up.

**(b) Wait for PC qualification:** new handling of1/155 floor HP drain;
1/181–184 lighting;1/311 element changes; enemy-owned2/17 party strikes;
boundary1/141 ground-query interpretation; new attack/collision/scene-object
owners or pins; any per-phase policy, shape/team/element/i-frame interpretation;
and native thread/cancellation/teardown changes. These require the installed
BDX, collision/MSET/ANB/PAX/ATKP and full PE/caller evidence described below.
Current static text justifies hypotheses and byte requests, not those product
behaviors. Canonical damage authority, down eligibility and native scaling stay
within the established ownership design.

## Read-only extraction for the next authorized PC slot

The bounded reader is [axel_inventory_extract.py](../tools/rig/axel_inventory_extract.py),
with the exact names, hash constraints and offset rules in
[axel_inventory_plan.json](../tools/rig/axel_inventory_plan.json). With no arguments
it prints that plan and reads no installation. It selects the plan's required original
assets (fourteen since VUH-2916 added `07localset.bin`) and reports any absent optional
ones (`absentOptionalAssets`) by exact UTF-8 name MD5 across the six KH2 HEDs; it never bulk-extracts a
PKG or its remastered textures/audio. It retains the pinned PE, every selected
original, each BAR entry range/hash, AI subentries, selected ATKP row ranges and
native trap table/file offsets. Full originals retain collision, PAX and all
MSET/ANB data for subsequent qualification; this script does not interpret them.

After a **new explicit GO**, create an owned PC worktree and recheck the full
desk gate, including idle ≥1800 seconds, no foreign game/OBS or desk job, no
HOLD/needs-verify, free rig lock and C: ≥20 GiB with headroom. Hold the canonical
rig lock for this first step of the future slot, before the VUH-2921 fixture.
The script also refuses less than20 GiB plus512 MiB headroom. It is not a desk
gate, a lease or permission to access PC1. The 2026-10-29 `vuh2914-installed-02` capture (top of this
document) used this reader.

Supply a local copy of the pinned OpenKH `EgsEncryption.cs` (URL/full hash in the
plan). Verify the extractor commit and the reference hash before running. From
that owned PC worktree, the command is:

```powershell
python tools/rig/axel_inventory_extract.py `
  --install 'C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-' `
  --out .local/vuh2914-installed-01 `
  --key-source .local/openkh-7a3b945c/EgsEncryption.cs
```

The output must be new and strictly within that worktree's real `.local/`.
Input packages open read-only. Any PE/HED pin or prior partial asset constraint
mismatch stops capture; a partial capture is never qualified. Read `capture.json`
and require `complete=true`, every required original, full hashes and expected
BAR/ATKP entries before proceeding. A capture is still **unqualified against
installed semantics** until decoded/native evidence is checked.

`capture.json` emits argument arrays for the exact OpenKH commands, without
executing them:

```text
OpenKh.Command.Bar.exe unpack <local-original> -o <new-local-unpack-dir>
OpenKh.Command.Bdxio.exe decode -b [-l <halfword-PC> ...] <local-AI.bdx> <local-output.bdscript>
```

These options are from OpenKH at `7a3b945c`; record the actual executable hashes
and version before decoding. The additional Axel labels are hints from the
public text, not proof that installed offsets match. Compare normal decode and
label-assisted decode before interpreting unrevealed instructions. At AI-relative
offset `16 + 2*PC`, inspect the installed instructions for every listed predicate
and lifetime. PE trap mappings use RVA-to-section conversion; the captured
256-byte function hash windows identify bytes, not full ABI or caller proof.
Native ctor/dtor RVAs `0x3CF830`/`0x3CFB10` and full PE are retained for lifecycle
analysis, including cancellation on retry/load.

Copy the ignored capture directly to this Mac worktree's `.local/vuh2914/` using
plain `scp` when authorized. Keep public disassembly, original files, PE,
unpacked data and decodes local; **never upload them to Linear, the evidence
store or Git**. Publish only hashes and derived inventory tables. Synthetic
offline parser controls are in `tests/test_axel_inventory_extract.py`; they do
not qualify any installed byte or native behavior.
