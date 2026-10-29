# Axel victory and Axel I retry: what to read on PC1 (VUH-2916, VUH-2915)

A read list for one PC1 session. It collects the installed facts the Axel shared
victory (VUH-2916) and the Axel I retry (VUH-2915) still need, the same way Thorn's
victory was qualified statically (`docs/PARTY_SETUP.md`, "Thorn victory";
`docs/TWILIGHT_THORN.md`). Nothing here has been read yet. It was written off-PC
from the repo alone, so every room name and number below is a pointer to check, not
a finding.

## Rules for the slot

- Read-only. No game launch, no live run, no save read or write. A live check
  needs its own go-ahead and goes through `kh2ctl` with the rig lock (`AGENTS.md`).
- Same method and guards as `docs/AXEL_HAZARDS.md` "Read-only extraction": owned
  worktree, full desk gate, rig lock, pinned PE `9002b2de…` and HED hashes, OpenKH
  `7a3b945c` with its `EgsEncryption.cs` hash checked, output only under `.local/`.
- Originals, decodes and PE stay in `.local/`. Commit only hashes, ids, offsets,
  program numbers and derived tables. Pushing or posting the findings waits for
  James's yes.

## Files

`tools/rig/axel_inventory_plan.json` already selects `ard/us/tt05.ard`,
`ard/us/tt20.ard`, `msn/us/TT05_MS404.bar`, `msn/us/TT20_MS603.bar`,
`00objentry.bin`, `00battle.bin` and `03system.bin`. It does not select these,
which this list also needs:

- `ard/us/tt19.ard` (02/13, the Basement Hall: events 135 and 136, door 50 evt2)
  and `ard/us/tt21.ard` (02/15, the Computer Room, Axel II's Continue point).
- The ARD for world 0x12 room 03 (`twtnw03-after-axel`, event 83). The expected
  name is `ard/us/eh03.ard`; confirm it by its HED match before relying on it.
- Each further room the chain after Axel II reaches (section 4). If `.local/`
  still holds the 42 Twilight Town ARDs read for `docs/PROLOGUE_ENCOUNTERS.md`,
  check their hashes against the install and reuse them.
- `07localset.bin`, for the authored exit of each victory event.
- The PE trap table entries for mission traps 4/3 (complete) and 4/16
  (`mission_dead_boss`). The plan's `traps` list has tables 1, 2 and 6 only.

Either extend the plan with these (no prior hash, so `null` constraints) or read
them with the same pinned decoder and record full SHA-256 for each.

## What to read

For each fight: Axel I is 02/05 btl87, `TT05_MS404` (mission 185), object 2229,
`Argument1=1`. Axel II is 02/14 btl137, `TT20_MS603` (mission 189), object 81, type 3,
`Argument1=0`.

1. **End trap.** In the mission BAR's BDX, the state that runs when Axel is beaten:
   which trap it calls (4/3, 4/16 or another) and with what argument. Also the
   fail and exit traps, for the retry. Thorn's answer was 4/16 with argument -1.
2. **Victory event.** In the room ARD, the battle program's event registrations:
   signal and argument, event id and flags (Thorn: signal 16, argument -1, event
   `0x353133`, flags `0x12`). Confirm the `3a`/`0a` start and complete cameras
   named in `docs/AXEL_REACTIONS.md`.
3. **Native path and exit.** Walk the trap through the PE to the event start, as
   Thorn's `4334D0` → … → `3AC350` chain was. Record the event's nine semantic
   parameters and its authored exit (world, room, door, map, battle, event) from
   `07localset.bin`, and which destination bytes the resolver leaves unwritten.
4. **Axel II's chain to Sora's morning.** From the victory event, follow each hop
   through `tt19` events 135 and 136, the world 0x12 event 83, and 02/13 door 50
   evt2 (site `tt19-2`), on to the first room where Sora has control. Per hop: room,
   program, event id, flags, the signal that starts it and its authored exit.
   Nothing after `tt19-2` is mapped in `EventScenes.hpp` today.
5. **Axel I's continuation.** The same as item 4 for Axel I's post-skirmish
   events, until control returns. Nothing after Axel I is mapped today.
6. **Where Axel I's Continue lands.** Whatever the mission BAR and room scripts
   show about the retry point after a full-party down at btl87. Axel II's is known
   (02/15 d99, from live runs). If the static read can't settle Axel I's, say so;
   a live read is a separate ask.
7. **Objects.** The `00objentry.bin` row for object 2229 (its type, checked against what
   the EnemySync census admits) and for object 81.
8. **Rewards and flags.** Any reward rows for these fights in `00battle.bin` or
   `03system.bin`, and any story flag writes visible in the victory events.
   Compare with the flags seen live (`1CE1` bit `0x10`, `1CE9` bits `0x80|0x10`,
   later `1CE1` bit `0x20`; `docs/ONLINE_COOP_PLAN.md`).

## What to bring back

A findings table per fight in the shape of the Thorn tables, with full SHA-256 of
every file read and the PE and HED pins confirmed. Mark each answer as read from
installed bytes or still open. That table is enough to add Axel I and Axel II to
`BossArenas.hpp` and to write the victory and retry code and fixtures off-PC.
