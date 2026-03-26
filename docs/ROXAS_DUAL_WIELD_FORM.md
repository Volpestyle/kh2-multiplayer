# Roxas Dual-Wield Form — Feature Design (Parked)

**Status:** Parked — revisit after Track A fundamentals are stable  
**Priority:** Post-M5 (after enemy replication works)  
**Tracks:** Track A M8 (content expansion) or Track C (public realm character kits)

---

## Motivation

Sora's drive forms (Valor, Wisdom, Master, Final) don't map to Roxas. In co-op or public realm modes, a player choosing Roxas would either have no form system or an awkward Sora-form reskin. Roxas canonically dual-wields Oathkeeper + Oblivion in scripted KH2 moments (Day 6 Axel fight, Roxas boss fight). Giving him a unique dual-wield "form" would:

- Provide Roxas players a meaningful combat mode comparable to Sora's drive forms
- Respect the character's canon abilities rather than force-fitting Sora's system
- Create a distinct gameplay identity for Roxas in multiplayer

---

## What We Know (from Axel Fight RE Session, 2026-03-26)

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
- How the game decides to spawn weapon attachments (form state machine? script trigger?)
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
5. Reference: `../openkh/OpenKh.Research.Panacea/` for injection framework

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
- `../openkh/OpenKh.Kh2/MotionSet.cs` — animation ID enum (check for dual-wield entries)
- `../openkh/OpenKh.Research.Panacea/` — DLL injection reference if Approach C is chosen
