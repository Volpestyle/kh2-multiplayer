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

One run of `tools/rig/axel_inventory_extract.py` gets every file this list needs.
On top of the VUH-2914 set (Axel's models, `tt05`/`tt20` ARDs, both mission BARs,
`00objentry.bin`, `00battle.bin`, `03system.bin`), `axel_inventory_plan.json` now
selects:

- `07localset.bin`, required, for each victory event's authored exit. It is kept
  as a BAR if it has the BAR magic and as a standalone table otherwise.
- Optional room scripts: every `ard/us/ttNN.ard` from `tt00` to `tt41` (the chains
  can wander; 02/13 is `tt19`, 02/15 is `tt21`, room names are decimal) and
  `ard/us/eh03.ard` (world 0x12 room 03, event 83). An optional name the install
  lacks goes in `capture.json` `absentOptionalAssets` instead of stopping the run.
  If `tt19`, `tt21` or `eh03` shows up there, stop and report the names before
  reading further.
- PE trap table entries 4/3 (complete) and 4/16 (`mission_dead_boss`). As a check
  on the table read, 4/16's function should be the `4334D0` handler that Thorn's
  path starts from (`docs/PARTY_SETUP.md`).

The new files have no prior pin, so the capture records their first full SHA-256.
The same guards apply as for VUH-2914: pinned PE and HEDs, a new output directory
strictly under `.local/`, and 20 GiB plus 512 MiB free:

```powershell
python tools/rig/axel_inventory_extract.py `
  --install 'C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-' `
  --out .local/vuh2916-installed-01 `
  --key-source .local/openkh-7a3b945c/EgsEncryption.cs
```

Require `complete=true` and every required asset in `capture.json` before reading.
The room scripts are parsed with OpenKH's `AreaDataScript`/`SpawnPoint` layouts, as
for `docs/PROLOGUE_ENCOUNTERS.md`; the capture does not interpret them.

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
