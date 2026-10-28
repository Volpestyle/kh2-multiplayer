# Axel I/II reaction commands: native trigger inventory (VUH-2913)

Static inventory of every native source that can offer, execute or clean up a reaction command (RC) or grab in
the two exact Axel encounters. It is the first acceptance step of VUH-2913 and the input to its code and fixture
plan. Nothing here was observed live.

- **Axel I:** 02/05 battle program 87, mission `TT05_MS404`, object 2229 `B_EX110_SKIRMISH`.
- **Axel II:** 02/14 battle program 137, mission `TT20_MS603`, object 81 `B_EX110`.

**Result:**
- **Axel II** has three RC families: Overtaker 490, Clear Light 443 and Burst Frontier 444. They are offered by
  REACTION collision rows on Axel, run from his BDX reaction callback, play capture motions borrowed from Axel's
  motion set, and start his own cameras.
- **Axel I** has no RC or grab that can be offered. The same BDX and model are used, but its spawn argument vetoes
  the only offer it shares with Axel II, it never reaches the phases that start the other two, and its installed
  resources lack every RC motion and camera.
- Neither fight has a grab separate from these RCs. Neither mission script, battle spawn group or player motion set
  offers a command.

## Method and installed evidence

Everything was read from PC1's installed packages, without launching the game or touching saves. Extraction reused
`.local/thorn/extract.py` from the VUH-2569 worktree, with the copied OpenKH `7a3b945c` encryption reference. BDX
decoding reused OpenKH.Command.Bdxio from `build/rig/native-shadow-bdx-semantics-20261004-01`. Formats follow OpenKH
`7a3b945c`: `Bar.cs`, `ObjectCollision.cs`, `SystemData/Cmd.cs`, `Ard/SpawnPoint.cs`, `Ard/AreaDataScript.cs` and
`docs/kh2/file/anb/anb.md`. Native code was read statically from the pinned PE copy `build/ghidra/kh2fm.exe` with
Capstone, never from a running process. The extracted assets and decodes stay local. Only these findings are
committed.

Each parser was checked against Twilight Thorn's already-qualified data before being trusted:
- The motion-trigger parser recovers Thorn's 297/298/299 (range triggers 20 and 26) and 345 (trigger 26) from
  `B_EX100.mset`. All 30 of its trigger blocks parse.
- The command table names 297/298/299/345 Key Counter, Lunarsault, Break Raid and Reversal.
- The BDX trap table resolves 1/121 to `0x4303A0`, VUH-2912's pinned target search.
- The two event-interpreter jump tables reproduce Thorn's recorded handlers for camera opcodes 6, 8, 15, 16, 18,
  19 and 49.

| Asset | SHA-256 | Finding |
|---|---|---|
| `obj/B_EX110.mdlx` = `obj/B_EX110_SKIRMISH.mdlx` | `69afebc1…3046ca` | Byte-identical: one BDX (`b_ex110`), one model and one collision table for both fights. |
| `obj/B_EX110.mset` (Axel II) | `5a5746f1…cfd445` | 256 entries. 252 `AP10`, 254 `AP12` and 255 `AP13` are motion sets (type 0x14) of player capture motions. |
| `obj/B_EX110_SKIRMISH.mset` (Axel I) | `df118607…047ee43` | 216 entries, all ANBs. It lacks 191–192, 206–210, 216, 241–242 and every capture entry (252/254/255). |
| `obj/B_EX110.a.us` (Axel II) | `bc91c3d6…d56f12` | Two Event (type 0x16) camera streams, `00` (`re_b_ex110-00`) and `10` (`re_b_ex110-10`). |
| `obj/B_EX110_SKIRMISH.a.us` (Axel I) | `a9d916a3…88bc37` | No Event entries: no RC camera exists. |
| `ard/us/tt05.ard` | `e981513f…eb0a2b` | Program 87: Progress 2267, Party 0, Mission 185 `TT05_MS404`, Spawn `b_43`. `b_43` holds players 566/567/568 and 2229 with `Argument1=1`. Every entity's `ReactionCommand` is 0. |
| `ard/us/tt20.ard` | `5a87ffef…718bc1` | Program 137: Progress 2254, Party 0, Mission 189 `TT20_MS603`, Spawn `b_40`. `b_40` holds players 566/567/568 and 81 with `Argument1=0`. Every entity's `ReactionCommand` is 0. |
| `msn/us/TT05_MS404.bar`, `msn/us/TT20_MS603.bar` | `f067c8df…7f5104`, `ff1679f6…842d67` | Both mission BDXs call only mission lock/complete/failed/exit/dead-boss; Axel I's also stops battle NPCs (1/331, 1/332, 6/6). No command, RC, attach, hook or camera call. Their `3a`/`0a` events are mission start/complete cameras (VUH-2916). |
| `03system.bin` `cmd` + `msg/us/sys.bar` | `fd4b8469…af4204`, `ea2ebef9…8e17c72` | 490 Overtaker, 443 Clear Light (`Require 490`, priority 255) and 444 Burst Frontier. All use receiver 0 (the player). The message id is the row's MessageId − 0x8000. |
| Player sets `P_EX110_BTLF`, `P_EX110`, `P_EX100`, `P_EX100_BTLF` (`.mset`) and the two player `.a.us` | listed in the local receipt | No RC trigger (range 6/7/20/25/26) in any player motion, and no Event entries. |

## Where the offers come from

Axel's model collision table (BAR type 0x17, 39 rows) has eight REACTION rows that carry the commands:

| Command | Collision group | Shape (root bone 16384) | What opens the group |
|---|---|---|---|
| 490 Overtaker | 20 | Sphere at the origin, radius 2800 | Range trigger 4 in motions 30A/32A (entries 16, 18, 20, 21, in both motion sets); `atk_prison` re-enables it (1/54) |
| 443 Clear Light | 21 | Sphere at (666, −430, −150), radius 500, with a group-21 TARGET row | Group 21 is on by the model's default mask. The offer is gated by the command row instead: 443 has flag bit 6 and `Require 490`, so it is offered only while the receiving player's own motion is inside a range-26 trigger with argument 490. That trigger sits at frames 107–172 of `AP12`, the player capture Overtaker plays, in both Sora's and Roxas's sub-motions. |
| 444 Burst Frontier | 23 (×7), 24 (×3) | A line of radius-200 spheres along −z (−150 to −1444), plus three radius-400 spheres above | Range trigger 4 in 58A (entry 209, group 23) and 59A (entry 210, group 24, frames 0–70). Only Axel II's set has these motions. |

**When a collision group is active (native, static):**
- `3BA550` decides each group. It takes the first answer in this order:
  1. the script layer at actor `+0x5A8`: an on bit there means active, an off bit (`+0x5AC`) means inactive;
  2. the motion layer at `+0x5A0`: on, then off (`+0x5A4`);
  3. the model's default mask, the second word of the collision table header.
- Each layer is a pair of words, on and off. The helpers are `3F1FA0` set on, `3F1F80` set off, `3F1FE0` and
  `3F1FC0` test them, and `3F2000` clear both.
- Script traps write the script layer: 1/52 `enable_collision` sets on, 1/53 `disable_collision` sets off, and
  1/54 `reset_collision` clears both.
- Motion triggers write the motion layer through the range-trigger dispatcher `40D810` (jump table `0x40DC98`):
  trigger 4 sets on and trigger 5 sets off. Trigger 10 (attack hitbox) goes through `3DAFE0`, which sets motion-on
  only when the script layer has no off bit for the group.
- Axel's default mask is `0x00200003`: groups 0, 1 and 21. The first header word, 39, is the row count.
- So a script veto (1/53) beats any motion trigger until 1/54 clears it. Groups 20, 23 and 24 are offered only
  while a motion enables them. Group 21 is always on, and the command row gates its offer (below).
- The code that clears the motion layer when a trigger's frame range ends was not pinned. The live trace confirms
  the windows.

**Follow-up commands (native, static):**
- Trigger 26 (`40DB0D`) sets bit 21 of the actor's trigger state and writes its argument to actor `+0x638`.
  Trigger 25 does the same with bit 20.
- The reaction refresh (`407C2B`) checks commands whose flags have bit 6. It offers one only while the receiving
  actor has `+0x18C` bit 21 set and `+0x638` equal to the row's `Require` (row `+0x22`).
- Clear Light (443) has flags `0xB159` (bit 6) and `Require 490`. Thorn's Key Counter (297) has the same shape:
  `0xB751`, `Require 297`. Overtaker (490) and Burst Frontier (444) have no bit 6 and no requirement.

**Who gets the offer (native, static):**
- REACTION offers come from the native reaction picker: `408391` calls `407B40` for each candidate, and `4088F0`
  scores it.
- The picker chooses only for that game's own canonical player.
- It never calls the command-register functions ScriptActorOwner hooks (`3B2850` general, `3B25C0` captured). So
  neither existing routing path sees Axel's offers: Thorn's role windows (minted from those registrations) and the
  attack-created bindings.
- On the friend's game, the picker offers against the mirrored clone's own collision rows. A press reaches
  `SmReactionCallback` with no role or binding. It is refused there as an unpaired callback, because Axel is a
  mirrored receiver and not a Dusk.

No offer comes from BDX `attack_set_rc` (2/51), `attack_rc_receiver` (2/52), `command_override` (1/165) or
`target_attack` (1/166): Axel's BDX calls none of them. No offer comes from the spawn groups, the mission scripts
or the player motion sets either.

## Axel II execution (object 81, `Argument1=0`)

`REACTION_CALLBACK` (BDX trigger 11) receives the RC's executing actor as an argument and switches on the command.
That argument is what lets each family route to its actual owner. Within the same flows, `rc_run_up`,
`rc_clear_light` and `idle_after_rc` read the canonical player through `sysobj_player` (1/3) instead. On the host,
that canonical player is the host's own Roxas, whoever pressed the RC.

| Family | Callback | Player side (callback actor) | Axel side | Camera | Release |
|---|---|---|---|---|---|
| 490 Overtaker | `L350` → `L409` | Facing/position set beside Axel (1/79, 1/148). Capture motion 254 `AP12` (1/29, arguments Axel, player, 254, 0), then motion push 4 (1/13). | Action `rc_run_up`: `obj_attach` (1/18) with flags 16386 and bone 1 on the pair (Axel, canonical player), then Axel's motion 241 (90A) until it ends | `camera_start_global` (1/164) `00` on Axel; it tail-jumps to `3DB2F0`, the same starter `obj_camera_start` uses | The action returns after motion 241, with no detach in the BDX. During `AP12` frames 107–172 the 490 state opens Clear Light. |
| 443 Clear Light | `L129` → `L140` | Facing/position set; capture 255 `AP13`; push 4 | Action `rc_clear_light`: the same attach, motion 242 (91A) to its end, then `disable_collision(20)` and `W4560 = 0`, then action `knock_down` | `10` on Axel, then thread `D303` (`magic_start_thread` 2/20). That thread waits, whites the screen out (0/31, 70), resets the camera (1/295) and whites it back in (0/32, 30). | `knock_down`, with no detach in the BDX |
| 444 Burst Frontier | `L486` → `L531` | Capture 252 `AP10`, then `obj_hook` (1/112) of the player into BDX coroutine `D563`. `D563` steers the player at Axel (1/4, 1/150, 1/89) every frame. It returns when Axel no longer exists, the player's motion ends, or its capture ID is no longer 252. | `idle_after_rc`, which waits on the canonical player's capture (1/101). The callback also checks whether Axel's own motion is 210 (59A). | `camera_watch` (1/207) on Axel; no Event asset | `D563` returns, ending the hook. There is no detach and no camera reset in this branch. |

**Capture motions (native, static):**
- 1/29 calls `3C6CC0` on the source's motion controller (Axel, whose set holds the capture). It records the
  capture ID at the captured actor's `+0x180`.
- `3C64A0` names the sub-motion `sprintf("%03d0", objentry[+7])` of the captured actor (format at `0x5C5D48`). The
  captured actor's two weapon children at `+0xD60` take that name plus 1 and plus 2.
- `3C65E0` binds the first non-empty duplicate (0 to 3) of that name. If none is authored, it clears the actor's
  motion.
- Objentry byte 7 is 1 for Sora (`P_EX100` and every form) and 14 for Roxas (`P_EX110`, `P_EX110_BTLF`).
- What Axel's captures author:

  | Capture | Sora | Roxas |
  |---|---|---|
  | `AP10` (252) | `0010`, `0011`, `0012` | `0140`, `0141`, `0142` |
  | `AP12` (254) | `0010`, `0011` | `0140`, `0141`, `0142` |
  | `AP13` (255) | `0010`, `0011` | `0140`, `0141`, `0142` |

- Sora's `AP12` body carries the same trigger-26/490 window as Roxas's. Sora's missing `0012` only clears a second
  weapon child that Sora doesn't have.
- So Sora owners are admitted exactly like Roxas owners. An earlier reading of this inventory (that a duplicate
  slot is a model variant, leaving Sora without captures) was wrong and is withdrawn.

**Attachment (native, static):**
- Trap 1/18 decodes `[0]` as the child, `[8]` as the parent, `[0x10]` as the flags, `[0x18]` as the bone,
  `[0x20]` as the callback and `[0x28]` as its data. It calls `3D9FE0(child, parent, flags, bone)`, which writes
  the parent at the child's `+0x6A0`, the flags at `+0x6A4` and the bone at `+0x6A6`.
- Syscall arguments are in push order. Only that order yields flags 16386 and bone 1 from the six values below;
  the reverse would give flags 1 and bone 16386.
- At both attach sites (`rc_run_up`, `rc_clear_light`), `push.d.sp 0` (Axel) comes before `jal 4, L5322`.
  `L5322` pops no argument; it returns the `sysobj_player` (1/3) wrapper. That leaves Axel on the stack as the
  sixth value: (Axel, player, 16386, 1, 0, 0).
- Every other `L5322` call site in the BDX is balanced the same way: the pushes after the call exactly fill its
  syscall.
- So **Axel is the child, attached to the canonical player at bone 1, with no callback**. That is the reverse of
  Thorn's qualified pair, where the player is the child of the boss at bone 5. While attached, Axel's `+0x6A0` is
  nonzero, which EnemySync's pose mirror skips.
- In flags `0x4002`, bit `0x4000` only skips a scale step when the attached matrix is built (`3CCE23`). Bit 2
  keeps the child's own offsets (`3D9FE0`). Neither flag ends the attachment.

**Release:**
- No BDX path detaches Axel. Trap 1/102 is the only BDX trap that reaches the native detach `3DA250`, and Axel
  never calls it.
- The native detach callers are attached-actor checks (`3DA520`) inside state handlers reached through pointer
  tables (`3D4E30` in four table slots, `3EF520` in two), teardown paths (`3A7120`, `3A8F30`, `3C2890`,
  `3C3600`) and re-attach (`3D9FE0` itself).
- Which state handler ends Overtaker's and Clear Light's attachment was not pinned statically. The trace run
  logs `+0x6A0` on Axel through both.

**Camera streams:**
- `00` is 1266 bytes and ends at tick 100 (op 8). Its opcodes are 0 (header), 15, 16, 8, 19, 6, 70 and 49.
- `10` is 778 bytes and ends at tick 90, with the same set minus 70.
- Both end on the zero terminator, and both have a timeline offset (op 16) of 20; Thorn's were all 0.
- Op 49 waits for frame 2 and stores 0, the same mode-0 shape as Thorn. Its 0/1 branch reads a global, not the
  command.
- Op 70 does not occur in Thorn's cameras. Its handler, `2D34A0`, sets `0xB64FA2 = 1` and `0xB64FA3 = 0`
  (argument 0). The only direct readers are in `2D37B0`, the event-camera start (called from `2CC8B2`). They pass
  the bytes as three bools to the camera transition setup `1780D0`. The bytes reset at `2CC534` and `2C86E3`.
  They are a camera blend option, not an input or pause gate.
- Op 16 (`2D9020`) stores its value at `0xB64FBC`. Its only reader is `2CB66A`, on the camera's end path. That
  reader converts twice the value to a float and hands it to `3AC5A0`, which writes it to camera state `+0xA0`.
  It is read as a duration (40 for Axel, 0 for Thorn), not a gate. The end path's wait loop before that read is
  the same one Thorn's cameras use.

**Gating:**
- **Phases.** Axel's phase word, at `+24` of his state, has exactly three writes: 0 in `battle_start`, 1 in the
  damage handler, and 2 in the damage handler. Both damage-handler writes require an HP-rate drop and
  `obj_unit_arg(1) == 0`. Phases 0, 1 and 2 select `btl_normal`, `btl_hard` and `btl_super_hard`
  (`mode_battle_boss`), and `rvg_*` for revenge.
- **`atk_prison` and `atk_assault`.** These start only from `btl_hard` and `btl_super_hard`, and only while
  `W4564 == 0` and `W4560 == 0`. `atk_assault_revenge` starts only from `rvg_super_hard`.
- **`atk_assault`'s motion.** It picks 209 or 210 at random, so it is the only source of Burst Frontier's
  enabling motions.
- **The flags.** Init sets `W4560 = W4564 = 1`. The phase-2 escalation clears `W4564`, and `rc_clear_light`
  clears `W4560`.

## Axel I absence (object 2229, `Argument1=1`)

- **Overtaker is vetoed from the start.** Init takes the `Argument1 == 1` branch, which calls `disable_collision(20)`.
  Only `atk_prison`'s `reset_collision(20)` lifts that veto.
- **`atk_prison` and `atk_assault` are unreachable.** Both phase escalations require `Argument1 == 0`, so Axel I
  stays in phase 0 (`btl_normal`), which starts neither action. `W4564` also stays 1.
- **Clear Light needs Overtaker.** Its `Require 490` and its only enabling window both depend on Overtaker's
  `AP12` motion.
- **The resources aren't there.** Axel I's motion set lacks Burst Frontier's enabling motions (209/210), the RC
  Axel motions (241/242) and all three capture entries. Its archive has no Event camera.
- **No other source.** Program 87's only spawn group and mission script offer no command, and no player motion
  offers one.

The group rule above is native: the script veto beats Overtaker's motion enable. Clear Light's `Require 490` also
needs `AP12`, which Axel I's set lacks. The acceptance takes this static absence plus a live ordinary-attack
control for Axel I, as the issue allows.

## Phase 0 results and what remains

Phase 0 resolved every static question:
1. **Group activity rule:** native (`3BA550`), above. Clear Light's follow-up gate is native too (`407C2B`).
2. **Attach order:** native. Axel is the child of the canonical player at bone 1. ScriptActorOwner gets Axel II's
   own exact pair rather than widening Thorn's (16386, 5).
3. **Camera op 70 and op 16:** both camera parameters, not input gates. The Axel cameras stay finite on the same
   end path as Thorn's.
4. **Capture slot rule:** native. Sora owners are admitted like Roxas owners.

Still open, and settled by a trace-only live run queued behind VUH-2912's runs:
- the native handler that clears Axel's `+0x6A0` after Overtaker and after Clear Light;
- the motion-layer clear at the end of each trigger range, which closes the offer windows.

Trap coverage the build adds:
- ScriptActorOwner's traps today are 1/3, 1/112, 1/29, 1/157, 1/18, 1/102, 1/14 and 1/238.
- Axel adds 1/164 (`camera_start_global`, which tail-jumps to the same `3DB2F0` as `obj_camera_start`), 1/207
  (`camera_watch`), 1/295 (`camera_reset`) and the screen white-out thread (2/20, then 0/31 and 0/32).

## Owner routing

Rules: `inject/src/AxelReaction.hpp`. Runtime: `inject/src/AxelReactions.inl`, with the role and hook glue in
`inject/src/ScriptActorOwner.inl`. Tests: `tests/AxelReactionTest.cpp` (rules) and `tests/AxelReactionAdapterTest.cpp`
(the adapter against simulated memory).

Slice 1 routed every offer to one owner but let only the host execute. Slice D lets any seat own and execute an RC:
the friend's own window, the host's consumption of its claim, a role that replays it on the friend's player, and a
safe abort for an executor lost mid-RC (below).

**How an offer gets its owner:**
- **Host mint.** While host Axel II is live in 02/14 btl137 (and only then), the host reads his collision masks
  through the `3BA550` rule each frame. For each family:
  - Overtaker (group 20) and Burst Frontier (group 23 or 24) are owned by the living seat nearest Axel that is
    inside one of the family's spheres, using Axel's yaw. Only spheres whose own group is active count: with 58A
    open (group 23, the line of seven), a seat inside a group-24 sphere above Axel cannot take the offer.
  - Clear Light is owned by the current Overtaker's seat, while that seat's actor on the host has `+0x638 == 490`
    and `+0x18C` bit 21.
- **One window per activation.** It keeps its owner while the offer is up, is consumed at most once, and only the
  next activation opens a new occurrence.
- **A failed observation withholds.** If Axel's masks or transform can't be read in a frame, every open window
  stops being claimable. A failed read of the collision header's default mask counts. Only a collision pointer
  read as zero means no collision data.
  - Likewise, if Clear Light's chain can't be read, its window alone is withheld. That covers the executor actor's
    handle word, `+0x638` or `+0x18C`.
  - A withheld occurrence stays as a tombstone, so reads recovering while the same activation is up mint nothing
    new. Only an observed end of the offer clears it.
  - The host consumes a claim only on the frame of a successful observation or the frame after it.
- **Wire.** Windows travel as reaction identity and offer records, with resource `0xFFFF0000|command` on Axel's
  netId. Owner roles stop minting below that band.
- **Client receipts.** The friend orders offers per command within one authority: the host session, its epoch and
  Axel's netId. A restarted server reusing an epoch starts a new history. The offers and identities are also
  dropped with ScriptMirror's room, load, transition and session resets.

**Prompt and press:**
- Axel II is a host-routed class, so on both seats the existing native RC cache (refresh `408520`, winner
  `408BD0`, accept `3B22D0`) treats his offers as known.
- A prompt shows, and native acceptance proceeds, only with a family-3 proof: this seat's live window for that
  command, on the Axel actor the native target names (`{Axel, 0, collision object, -1}`, built by `3BEE00`).
- **Host press.** `SmReactionCallback` gives every Axel II callback to the Axel path. It executes only on a seat-0
  window, so a host press against a seat-1 window is refused and logged, and never runs with the host's Roxas.
- **Friend press.** On its own window it consumes locally and sends the completion claim, and runs nothing on its
  own game. The host consumes that occurrence once (`AxelConsume`: the owner's seat, connection, epoch and current
  occurrence; a duplicate, stale, replaced or wrong-owner claim consumes nothing) and runs the callback with the
  friend's actor on the host.

**sysobj_player (1/3):** inside Axel's script, while an RC is bound, 1/3 names the executor's own actor on the
host, instead of the nearest seat. So `rc_run_up`, `rc_clear_light` and `idle_after_rc` attach to and wait on the
seat that pressed.
- The executor is bound to that actor, not to its seat's eligibility. A seat that is downed, or leaves the eligible
  table, still owns the RC its actor is in.
- It ends on a confirmed release: Axel's attachment to that actor and its capture of it, having been seen, are
  both read and gone. It also ends on a new epoch, or at its 1200-frame lease if it never engaged.
- An unreadable attachment or capture is unknown, never a release. So is an unreadable executor identity, meaning
  the actor's handle word at `+4` (`AxelReadActor`):
  - the binding stays;
  - the capture counts as unknown (`AxelReadContact`);
  - Clear Light's window is withheld with its tombstone;
  - 1/3 keeps the native result and never falls back to nearest-seat routing.
- Only a handle word that is read and names another actor, or none, makes the actor gone.
- **If the executor's actor no longer resolves** (its seat's actor on the host is gone mid-RC), no other seat is
  chosen in its place: 1/3 keeps the native result and logs `[axel-rc] player unresolved`. 1/3 must still name a
  live actor. Trap 1/18 resolves the parent handle at `42C571` and reads `[rax+4]` at `42C576` without a null
  check, so naming no actor would fault at Axel's attach. That native result is the host's own player, the wrong
  actor for a friend's RC (Ione's probe, VUH-2913 comment `694ad7cd`), so the safe abort below refuses every act on
  it.

**Safe abort (`AxelActAdmitted`).** On the host, Axel's own acts on a player go only to the bound executor's
resolving actor:
- 1/18 attach with Axel as the child (`rc_run_up`, `rc_clear_light`): the parent must be that actor;
- 1/29 capture with Axel as the source (the callback's `AP10`/`AP12`/`AP13`): the captured actor must be that actor.

Anything else is refused before the native call (`[axel-rc] attach refused` / `capture refused`): the attach is
skipped, and the capture returns int 0, as `42D1C0` writes its result. That covers the host's actor named by 1/3's
native result, another seat's actor, an argument that does not decode (it would fault at `42C576` or `42D187`), and
any act while no RC is bound. An unread executor identity refuses the act without ending the binding; the next
resolving read admits again. So an RC whose executor is lost plays on with Axel unparented and nobody else captured
or held, and ends on its lease or a confirmed release. The gates sit in `SmAxelAct`, which `SmOwnerAttach` and
`SmOwnerCapture` call before the native trap; other encounters return before any read.

Static facts behind it (Kit's reads, VUH-2913 comment `b034e8cf`):
- `L140`, `L409` and `L531` run synchronously inside the native callback (`3E1840` runs the trigger's thread once,
  and none of them halts). Their 1/29, 1/13, 1/79, 1/148 and 1/112 take the player from the callback argument, the
  executor resolved just before the call.
- Every 1/3 call in these flows runs later, in Axel's action thread (`obj_act_start` only creates it), and
  `idle_after_rc` repeats it every frame. The executor binding, held until a confirmed release or the lease, covers
  them; a binding scoped to the callback would not.
- The only consumers of the 1/3 result are 1/18 at pcs 10189 and 10252 (gated) and 1/101 `obj_motion_is_capture` at
  10056 in `idle_after_rc`, which returns the actor's `+0x18C` bit 14 and writes nothing. It is not gated. It reads
  1/3's result, which is always a live actor. On a lost executor that result is the host's own actor. Axel cannot
  have captured the host's actor during a friend's RC, because the 1/29 gate refuses it, so the read says "not
  captured" and `idle_after_rc` ends, as it does after a native capture ends. Any other capture would only keep Axel
  idling; it acts on nobody. A pin and gate on 1/101 would need its body, which has not been read.

**The friend's view (owner role).** A friend's Overtaker or Clear Light replays on its own player through an
ordinary ScriptActorOwner role (`SmOwnerBeginAxelRole`), begun at completion before the callback, so the callback's
capture reaches it:
- The role has no thread: these RCs hook no coroutine into the player. It is current while the executor stays bound
  to that seat and actor and the actor's `+0xD48` reads zero (`AxelRoleHeld`); then the host releases it
  (`axel-executor-ended`) and the friend retires it. Its puppet is the role's (`OwnsPuppet`) for that time.
- The existing stream carries it: begin, capture (op 3, the borrowed `AP12`/`AP13` motion from the Axel clone's own
  set), position and motion (ops 4/8), end (op 6). Clear Light after Overtaker on the same seat keeps one role.
- Burst Frontier begins no role at completion (`AxelRoleAtCompletion`): its 1/112 hook mints the coroutine role and
  adopts the capture issued just before it (VUH-2611).
- Axel's attachment is the reverse of Thorn's, so the role's attachment record is Axel held by its actor
  (`AxelAttachPair`: flags 16386, bone 1), sent on each change and with the heartbeat; an unread parent sends nothing.
  On the friend, op 13 attaches the Axel clone (the role's source) to its own player, and op 14 or the role's
  release detaches it, only if this role's receipt (`axelHeld`) and the clone's parent both say it is ours. A
  spectator's visual never takes Axel's pair.
- A down or a load retires through the existing release paths. The friend's logical down, lease or scope loss
  releases the role, and its cleanup detaches the Axel clone before the player's own detach and idle (`42C5F0`,
  `42D0E0`) unwind the borrowed capture. On the host, a seat leaving the candidate table releases the role while the
  executor keeps its actor. A room change or load drops the roles with the room.

**Logs:** `[axel-rc] offer`, `offer end`, `observation failed`, `chain unread`/`chain read`, `client offer`,
`local completion`, `complete`, `complete refused`, `press refused`, `executor engaged`, `executor end` and
`player unresolved`, 256 lines per session.

**Tests:** `kh2coop_axel_reaction_test` covers:
- the pure rules, including the encounter gate (`AxelHostFrameAdmitted`), friend execution and consumption
  (`AxelExecutionAdmitted`, `AxelConsume`), the safe abort (`AxelActAdmitted`), the role's lifetime
  (`AxelRoleHeld`, `AxelRoleAtCompletion`, `AxelAttachPair`) and the per-frame step (`AxelWindows::Frame`) the
  adapter calls;
- the adapter's reads, against simulated memory with a failure at each field: masks (`AxelReadMasks`), executor
  identity and contact (`AxelReadActor`, `AxelReadContact`), and the chain (`AxelReadChain`).

`kh2coop_axel_reaction_adapter_test` compiles `AxelReactions.inl` itself against simulated memory: a friend's
completion end to end (consume once, role before callback, 1/3 naming the friend), duplicate, stale, replaced and
wrong-owner completions, the abort's final effect through `SmAxelPlayer` and `SmAxelAct` (lost, unread and recovered
executor identities, no RC bound), the role's lifetime and the hold record, and the friend's offer, proof and press.
ScriptActorOwner is a recorder there: the role stream, its client replay and the hook bodies' one-line calls are
not exercised on the Mac.

**Known limits:**
- **Not run live.** Native release timing, whether a host puppet runs `AP12`'s trigger 26 (Clear Light for a friend
  needs it on the host's copy of its actor, and its own copy needs it for the friend's native picker), and whether
  the friend's borrowed capture and Axel clone attachment look right are all unverified.
- **Strike ownership is claimed for none of them.**
  - Burst Frontier's damage is the captured player's own weapon attacks: `AP10`'s weapon children (`0011`, `0141`)
    carry trigger 10, atk 1262/1263, group 4, frames 47–73 (Kit, `b034e8cf`). On the host the friend's copy runs
    them, and its hits are zeroed (VUH-2910). Whether the friend's own player plays them through the hook role's
    replay, with its weapon's `+0x6C8` bit 0 set, so that they become its ordinary claims, is unverified.
  - For Overtaker and Clear Light no damage source was found in the captures, Axel's RC motions or the RC script.
- **Clear Light for a friend.** The host's copy of the friend's actor ticks native range triggers once Axel's 1/29
  binds `AP12` on it, so trigger 26 sets its `+0x18C` bit 21 and `+0x638` (Kit, `b034e8cf`). Bit 21 is a level
  flag cleared every tick, and the order of that tick against the host's chain read is unproven.
- **Only the camera starts are forwarded.** Axel starts his RC cameras with `camera_start_global` (1/164, `42E0D0`),
  which tail-jumps to `3DB2F0(Axel, name, 0)`: anchored on Axel, with no target, and no result pushed (flags `0x2`).
  ScriptActorOwner hooks its slot (`SmOwnerCameraGlobal`, pinned, optional: refused, the host plays it as before).
  During a friend's Overtaker or Clear Light (its held role) the host skips the start and sends it as the role's
  op 5 (`AxelCameraForwarded`), with the no-target code `kAxelCameraNone`; the friend replays it on its Axel clone
  through the existing op-5 path, so its camera retirement receipt is Thorn's. Still on the host and not sent:
  Burst's `camera_watch` (1/207, `430B80`), Clear Light's white-out thread (2/20 queues D303: 0/31, 0/32) and
  `camera_reset` (1/295). All of these are global per game, not per actor; their bodies are in Kit's `b034e8cf`.
- **Axel's own pose on the friend while held.** The host stops publishing Axel's motion while he is parented, so the
  friend's held clone keeps its last mirrored motion.
- **Host-owned RCs show nothing on the friend's copy of the host.** No role is minted for the host's own executor,
  so the spectator clone does not play its capture.
- **Prompt gating is by proof, not by native suppression code.**
  - A seat without a live host window has its Axel prompt hidden by the existing cache gate. This holds for the
    friend, and for the host while the friend owns the offer.
  - One exception: while the canonical controller does not qualify (`SmRcController`), the query returns the raw
    command, so the prompt shows. Its press is then refused at native acceptance, so it does nothing.
  - The owner rule is geometric. The yaw transform (local −z forward) and the 100-unit seat tolerance are
    unverified live. A seat the native picker would offer, but the host's geometry does not, sees no prompt.
  - Prompt suppression stays open, together with that live check. VUH-2913 is not done while either seat can see
    a prompt that does nothing.
