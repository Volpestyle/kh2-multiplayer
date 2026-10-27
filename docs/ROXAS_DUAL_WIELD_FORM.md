# Roxas Dual-Wield (`0x323`, `P_EX110_BTLF`)

Two separate things share this body:

1. **The native encounter body (VUH-2911, built).** Axel II, the Day 6 Mansion fight (`02/14 btl137`,
   `TT20_MS603`, `B_EX110` 81), is where the game itself resolves member 0 as dual-wield Roxas. Both seats
   get it there for their own player and their clone of the other seat. It is qualified per site, not as a
   kit. See [the encounter section](#native-encounter-body-axel-ii-vuh-2911).
2. **A free dual-wield form for Roxas players (parked).** A selectable mode comparable to Sora's drive
   forms. Nothing below "Motivation" is built; it is the original design note.

Naming: Axel I is the Day 5 Sandlot skirmish (`02/05 btl87`, `B_EX110_SKIRMISH` 2229, no forced form) and
Axel II is the Mansion fight. Older text called the Mansion fight "Axel I"; key everything by the native tuple.

## Native encounter body: Axel II (VUH-2911)

**Where.** Checked site `tt20-137` (`kh2coop/RouteSpawnPolicy.hpp`, `BasementHall137*`, `encounterBody=0x323`):
NOW `02/14 evt137`, package 808738658, headerId 67, four records (raw566, the absent raw567/568, then Axel).
24 byte-identical receipts from both seats in the FM-02 route runs (`20261027-051908`, `-071731` and their
batches) show the game resolving members `323/5C/5D/819` there.

**Who gets it.** `prologue::SiteBody`: a seat whose selected profile is the default Roxas (`0x5A`) builds
`0x323` at that site, its local (the authored raw566) and its clone of the other seat (the missing-party copy).
A seat that selected Sora keeps Sora there (`ResolveLocal` replaces member 0 `0x323` with `0x54`), and its
clone on the other seat is Sora too. No profile or selection changes: the next checked site builds the selected
profile again. btl137 has no mandatory Roxas-only scripted action in this slice's evidence (reaction commands
and grabs are VUH-2913's), so there is no temporary story role.

**Leaving gives the selection back (Ione 61ea3f02).** The native Continue lands at the Computer Room checkpoint
`02/15 d99`, which is not a checked site, so the game would resolve the prologue's Roxas there for every seat.
`prologue::EncounterExit`: the encounter site's load arms an exit, and the first plain field load after it that is
not a checked site (no event program, event context or cutscene: the kit path's own gate, passed in by the member
resolver) replaces that native Roxas with the seat's other selection, logged as
`[prologue] encounter exit at=02/15 evt=0: restoring the selected body 0x54`. The swap happens in the member
resolver, before the area load builds the player, the same path that gives a checked site its Sora; at the checkpoint
it is covered offline and by the mixed fixtures, not yet by a live run. A scripted load keeps the native member and
leaves the exit armed; another checked site disarms it (it builds the selection itself). A default Roxas keeps the
native Roxas. Later unpinned fields keep the existing unpinned-field rule (native body, solo).

**Admission is scoped, not global.** The `0x323` row in `kh2coop/PlayerKits.hpp` stays unqualified
(`encounter=true`): `qualifiedKit`, `ProfileQualified`, party kits, the solo kit path and the roster gates all
still refuse it. Only `bodyKit(member, encounterAllowed)` and the descriptor checks with `encounterAllowed`
admit it, and `privatestatus::EncounterScope()` grants that only after the site's own missing-party construction
and only while NOW is still exactly that site. It retains the canonical static site row (`EncounterSiteAt`), never
the per-seat copy ingress builds on its stack, and clears it on the first read at another NOW. Any other load
(the native Continue to the Computer Room `02/15 d99`) ends it, and the existing `MissingPartyLoadCurrent` check
refuses every reference to the old clone.

**Weapons and form, from the native constructor (static RE, build 9002b2de).** The player-class base constructor
`3A7A40` stores the constructor's form argument at `actor+0xDE0` (the factory passes the descriptor's `+0x57`
byte, 10 here), allocates the status record through `3C0620(key, form)` (called from `3A7B1B`), attaches weapon
slot 0 from the descriptor, and attaches slot 1 when the form is 1..6 or 10. So the clone gets both keyblades
(`W_EX010_10` and `W_EX010_20`, objectId 104 and its pair) from the same native path as the local; nothing is
spawned or attached by the DLL. `MissingPartyAbort` admits key 14 with form 10 for that one base allocation.
The model name is 13 bytes with its NUL, so descriptor name checks read 16 (status) or 32 (neutral input) of the
objentry's 32 name bytes instead of 8.

**The SAVE form row.** For keys 1/14 and form 1..10, `3C0620` sets status `+0x254` to a handle of the drive-form
row `SAVE+0x32F4+(form-1)*0x38` (`3A1410`; for form 10, `SAVE+0x34EC`, next to the current-form byte
`SAVE+0x3524`). A form-0 clone never had one. Every instruction with displacement `0x254` was audited
(headless Ghidra, 71 sites; 7 decode the handle through `4AD270`):
- `3C1D90` (status refresh) and `4014A0` (ability recompute) only read the row; they write the status record.
- `3E8380` (form EXP and level-up) writes the row through `actor+0xDE0` and `+0x254`, but all four of its callers
  (`3E8760`, `3E8780`, `3E8730`, `3E86B0`) run it only for `actor+0xDE0` 1..5 (the drive forms), never 10.
- `3E8AA0` (a forced form level-up) also writes the row; its only reference is its own `.pdata` unwind entry.
- The other `0x254` uses are stores of the handle itself (`3C0620`, `3C03F0`, `3C0010`, `3C26C0`) or other structs.
**Named gap: indirect writers.** Indirect calls (vtables, function pointers) and handles copied beyond the scanned
ten-instruction window were not enumerated, so the static audit cannot rule out a writer reached that way. The live
control is a hard acceptance criterion: the drive-form rows (`SAVE+0x32F4`, `0x230` bytes, including the form-10
row) must be byte-identical on both seats at boot, after the clone hits and after Continue. A passing run is evidence
for those three checkpoints and the actions it exercised, not for the absence of writes in between: a write that native
teardown later restores, or an indirect writer the run never reaches, stays invisible to it.

**Live fixtures.** `tools/scenario/scenarios/net_prologue_mansion_axel_form.json` (two default Roxases, the
delivery run) and its mixed variants `_sora_host` and `_sora_friend`, all generated by
`net_prologue_mansion_axel_form.gen.py`: FM-02, the committed Computer Room to Basement Hall pilot, both Dusk
waves, then Axel II. Each asserts per seat:
- one bound `tt20-137` construction with each seat's expected clone and local (dual-wield `803`/key 14, or Sora
  `84`/key 1), and on each screen one body per seat, with both keyblades per dual-wield body;
- per dual-wield body, native form 10 (`actor+0xDE0`) and a drive-form-row handle at status `+0x254`;
- a 1-HP native hit on each clone inside the host's combat-admission hold. The hold is proven from the host's
  `deferred group=1`/`released` lines around the whole window, and a hold that expired is a setup failure. The hit
  must leave the observer's HP and every byte of the SAVE character records (`SAVE+0x24F0`, `0xE04`) and drive-form
  rows (`0x230`) unchanged, read just before and just after;
- right after the admission release, one logical down and partner revive per owner (`owner_downs`: an Axel
  downing counts, otherwise the DownedSpike Kill; both seats down is a harness failure);
- each owner's move, jump and attacks filmed on both screens, with the clone playing the owner's motions;
- a full-party down and the native Continue to `02/15 d99`, after which each seat holds only its selected body
  (Sora restored by the encounter exit), no dual-wield body or keyblade pair remains, and the old clone is never
  driven again.
`tests/test_axel_form_fixture.py` runs these assertions against synthetic data, with negative controls.

Not captured: which body each keyblade is attached to. `kh2ctl entities` has no attachment-owner field and the
weapon root offsets are not reverse-engineered yet, so per-body keyblade counts, the form/`+0x254` receipts and
the paired clips stand in for ownership.
Rehearsal save; video-only paired clips (lead amendment). Shared boss HP is VUH-2912's, reaction commands
VUH-2913's, hazards VUH-2914's, either-seat recovery and retry VUH-2915's, and the victory exit VUH-2916's.

---

# Free dual-wield form (parked design note)

**Status:** Parked — revisit after Track A fundamentals are stable  
**Priority:** Post-M5 (after enemy replication works)  
**Tracks:** Track A M8 (content expansion) or Track C (public realm character kits)

---

## Motivation

Sora's drive forms (Valor, Wisdom, Master, Final) don't map to Roxas. In co-op or public realm modes, a player choosing Roxas would either have no form system or an awkward Sora-form reskin. Roxas canonically dual-wields Oathkeeper + Oblivion in scripted KH2 moments (Axel II on Day 6, Roxas boss fight). Giving him a unique dual-wield "form" would:

- Provide Roxas players a meaningful combat mode comparable to Sora's drive forms
- Respect the character's canon abilities rather than force-fitting Sora's system
- Create a distinct gameplay identity for Roxas in multiplayer

---

## What We Know (from Axel Fight RE Session, 2026-03-26; the Day 6 fight is Axel II)

### Weapon attachment objects discovered
- `w_ex010_20` and `w_ex010_10` are the two dual-wield keyblade attachment objects
- They exist as live actor-like objects in the actor pointer array during the Day 6 fight
- Both share a common transform layout:
  - `+0x70` = primary position (X,Y,Z,W)
  - `+0x838` = position copy
  - `+0x878` = interpolated/previous frame position
  - `+0xF28` = additional position copy
- Descriptor-table pointers at `+0x80/+0x88/+0x90` identify the weapon model
- These objects are heap-allocated; addresses change per session

### What we don't know yet
- How the game decides to spawn weapon attachments (form state machine? script trigger?). VUH-2911: for the
  native encounter body it is the base constructor itself: form 10 attaches weapon slot 1 (see above).
- The weapon object creation/initialization function
- How weapons attach to a character's bone/socket hierarchy
- The animation system that loads dual-wield movesets and combo tables
- How input/command menu changes when in dual-wield mode
- Whether the drive gauge or a separate resource governs form duration

---

## Implementation Sketch (speculative)

### Approach A — Hijack existing form system
1. Find the drive form state machine (how Valor/Wisdom/etc. are entered/exited)
2. Map an unused or repurposed form slot to "Roxas Dual-Wield"
3. Configure it to spawn `w_ex010_10` + `w_ex010_20` weapon attachments
4. Load the dual-wield animation set (if separate MSET exists)
5. Drive gauge or custom timer for form duration

### Approach B — Script-trigger replication
1. Find whatever triggers dual-wield in the Day 6 Axel fight
2. Replicate that trigger on demand (may be a simpler flag-set)
3. Less flexible but potentially much less RE work

### Approach C — DLL injection (Panacea-style)
1. Hook the weapon spawn function directly
2. Allocate and initialize weapon objects from scratch
3. Attach to character bone sockets via the game's own attachment system
4. Most flexible, most RE work required
5. Reference: `~/openkh/OpenKh.Research.Panacea/` for injection framework

---

## Prerequisites (will be discovered during core Track A work)

| Prerequisite | Discovered during | Notes |
|---|---|---|
| Actor spawning system | M1 (entity discovery) / M5 (enemy replication) | Understanding how actors are created and managed |
| Form/drive state machine | M3 (input injection) | Input paths will reveal form activation |
| Animation system | M4 (animation sync) | Animation ID offsets and MSET loading |
| Weapon attachment mechanism | M5 (enemy replication) | Enemy weapon/equipment handling may share code |
| Bone/socket hierarchy | Camera RE / entity struct deep dive | Transform attachment points |

---

## Open Questions
1. Does Roxas have a separate MSET for dual-wield combos, or does the game dynamically remap Sora's combo table?
2. Is there a "form" flag on the Roxas entity during the Axel fight, or is dual-wield purely weapon-attachment-driven?
3. Can we trigger dual-wield without the full scripted fight context?
4. Should the form have a duration/gauge, or be a permanent toggle for Roxas?
5. What happens to the command menu during dual-wield — does Roxas get unique reaction commands?

---

## References
- `docs/probes/AXEL_FIGHT_RE_SESSION.md` — weapon object discovery and transform layout
- `~/openkh/OpenKh.Kh2/MotionSet.cs` — animation ID enum (check for dual-wield entries)
- `~/openkh/OpenKh.Research.Panacea/` — DLL injection reference if Approach C is chosen
