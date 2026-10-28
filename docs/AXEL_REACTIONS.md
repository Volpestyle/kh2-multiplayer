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
| 443 Clear Light | 21 | Sphere at (666, −430, −150), radius 500, with a group-21 TARGET row | No motion enables group 21. The only related trigger is range 26 (AnmatrCommand 2) with argument 490, at frames 107–172 of `AP12`, the player motion Overtaker plays. OpenKH's table ties trigger 26 to group 21, and `cmd` 443 requires 490: Clear Light is the follow-up offered during Overtaker. This link is inferred, not traced natively. |
| 444 Burst Frontier | 23 (×7), 24 (×3) | A line of radius-200 spheres along −z (−150 to −1444), plus three radius-400 spheres above | Range trigger 4 in 58A (entry 209, group 23) and 59A (entry 210, group 24, frames 0–70). Only Axel II's set has these motions. |

**Collision masks, from the native traps (static):**
- 1/53 `disable_collision` jumps to `3F1F80`, which sets the group's bit in the second actor mask (`+0x5AC`).
- 1/54 `reset_collision` jumps to `3F2000`, which clears the bit in both masks (`+0x5A8` and `+0x5AC`).
- A motion's range-trigger enable is read as setting the first mask while that motion plays. So a group is offered
  while a motion enables it and the script has not vetoed it.
- This rule is inferred: the native REACTION test was not pinned. It is the only reading under which Axel I's
  explicit init veto (below) has any effect.

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

**Capture motions:**
- Each capture entry (`AP10`, `AP12` and `AP13`) holds six sub-motions (`0010`–`0012` and `0140`–`0142`), each with
  four variant slots. Only slot 3 is authored. Thorn's equivalent entries author slot 2.
- The slot is read as the player model variant (Thorn's player is single-blade Roxas; Axel II's is dual-wield
  Roxas). That is inferred. If it holds, a Sora seat (VUH-2911's mixed fixtures) has no Axel capture motion.

**Attachment:**
- Native 1/18 decodes its first two arguments into `3D9FE0(actor, parent, flags, bone)`, which writes the parent at
  actor `+0x6A0` (VUH-2569 attach review).
- Axel's operands are (Axel, a pseudo-wrapper `L5322` builds around the canonical player, 16386, 1, 0, 0). Read
  that way, Axel is the child attached to the player at bone 1.
- That is the reverse of Thorn's qualified pair, where the player is the child of the boss at bone 5. ScriptActorOwner
  admits only (16386, 5) and has no callback here.
- The child/parent order rests on the VM keeping the `jal` argument on the stack for the syscall, which was not
  proven. Confirm it natively before any admission change.

**Camera streams:**
- `00` is 1266 bytes and ends at tick 100 (op 8). Its opcodes are 0 (header), 15, 16, 8, 19, 6, 70 and 49.
- `10` is 778 bytes and ends at tick 90, with the same set minus 70.
- Both end on the zero terminator, and both have a timeline offset (op 16) of 20; Thorn's were all 0.
- Op 49 waits for frame 2 and stores 0, the same mode-0 shape as Thorn. Its 0/1 branch reads a global, not the
  command.
- Op 70 does not occur in Thorn's cameras. Its handler, `2D34A0`, sets event flag bytes `0xB64FA2 = 1` and
  `0xB64FA3 = 0` (argument 0). It has no input or branch, but what those flags gate was not identified.

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

The remaining assumption is the REACTION activity rule above. Even if it were wrong, an Overtaker accepted in
Axel I would start a missing camera and capture a missing motion. The acceptance therefore takes this static
absence plus a live ordinary-attack control for Axel I, as the issue allows.

## Open native questions for the build

Each of these must be resolved before admitting Axel II's RCs:
1. The REACTION group activity rule (mask `+0x5A8` set by motion triggers, `+0x5AC` set by veto), from the native
   reaction refresh (`408520`), so the offer windows above are proven rather than inferred.
2. The attach operand order (Axel as child of the player at bone 1). Then decide whether ScriptActorOwner admits
   that reverse pair.
3. How a callback-free 16386 attachment ends with no `obj_detach`: whether the motion end, the action change or
   `knock_down` clears `+0x6A0`.
4. What event flags `0xB64FA2/3` (camera op 70) gate, and the op 16 timeline offset of 20, before admitting the two
   Axel cameras as finite and input-free.
5. The capture slot rule, which decides whether a Sora owner must be refused.
6. Trap coverage. ScriptActorOwner's traps cover 1/3, 1/112, 1/29, 1/157, 1/18, 1/102, 1/14 and 1/238. Axel adds
   1/164 (`camera_start_global`, which tail-jumps to the same `3DB2F0`), 1/207 (`camera_watch`), 1/295
   (`camera_reset`) and the screen white-out thread (2/20, then 0/31 and 0/32).
