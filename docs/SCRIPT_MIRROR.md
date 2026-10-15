# Script-call mirror (VUH-2252)

D14 item 3. A client runs no brain for a mirrored enemy (`EnemyMirror.inl`), so
the calls that enemy's script makes on itself never happen on the client: a
boss's tilt, a texture animation, a shadow toggle, and the effects it starts,
moves and ends. The host now sends those calls, and the client replays them on
its own copy.

On whenever the enemy mirror is on (`KH2COOP_ENEMY_MIRROR=1`, the player
default). `KH2COOP_SCRIPT_MIRROR=0` parks it on that machine.

## Calls mirrored

bdscript traps, named as in OpenKH's `BdxTraps.cs`. Tables are at
`exe+0x753490` (`*(+8 * table)`), with 16-byte entries `{fn, flags}`. Each is
called as `fn(u64* args)` from the interpreter `exe+0x41B400`, with any result
in `args[0]`. The call list follows TCM-Murray's world-mirror research
(GPL-3.0, see `research/PRIOR_ART.md`). No code was copied.

**Actor calls** (the first argument is the actor):

| Trap | Table/index | Function | What it does |
|---|---|---|---|
| `trap_obj_set_rot` | 1/1 | `0x42BF60` → `0x3B6560(actor, angle)` | rotation from one angle into `actor+0x680` |
| `trap_obj_set_xyzrot` | 8/2 | `0x537E70` → `0x3B65F0(actor, float4*)` | free rotation (tilt) into `+0x680`, bit 3 of `+0x9B8` |
| `trap_obj_texanm_start` / `_stop` | 1/85, 1/86 | `0x42F820` / `0x42F850` | texture animation on the render object `+0x7B0` |
| `trap_obj_hide_shadow` / `_show_shadow` | 6/45, 6/64 | `0x53E960` / `0x53EE90` | `+0x124` bit `0x200`, or `0x3CD820` |
| `trap_obj_set_scissoring` | 6/65 | `0x53EEB0` | `+0x8C8` bit `0x20` |
| `trap_obj_effect_kill_all` | 1/349 | `0x42D690` → `0x2C6490(actor+0x80)` | ends every effect the actor made |

**Effect calls.** `0x2C6B30` makes an effect from the actor's own effect table
at `actor+0x80`, matched by effect id. An effect instance has its flags at
`+0xC` (bit `0x10000` = killed) and its part at `+0x30`. The part holds pos
`+0x00`, rot `+0x10`, scale `+0x20` and color `+0x30`.

| Trap | Table/index | What the host sends |
|---|---|---|
| `trap_obj_effect_start` / `_bind` | 1/21, 1/87 | the start, with a fresh effect id |
| `trap_obj_effect_start_pos` | 1/22 | a 1/21 start (id, a3, a4) plus a 0/38 position. `0x3B66C0` is `0x2C6B30(id, a3, 1, a4, 0)` plus a pos copy |
| `trap_effect_set_pos` / `_scale` / `_rot` / `_color` | 0/38, 0/39, 0/40, 0/67 | the field it wrote, read back from the part, as a float4 |
| `trap_effect_set_dir` | 0/41 | the rotation it wrote (`0x3B7710`), as a 0/40 |
| `trap_effect_pause` | 0/66 | the on/off value |
| `trap_effect_loop_end` / `_kill` / `_fadeout` / `_loop_end_kill` | 0/44, 0/51, 0/52, 0/85 | the call. A kill ends the pairing |

The safety check for each call is done statically (headless Ghidra, Steam
`9002b2de`):

- Each trap reads only its argument array (one u64 per argument, with the
  handle in the low word).
- It decodes its actor (the argument, then that object's `+4`) or its effect
  through `0x4AD270`.
- It then makes one call or one write.
- Neither the traps nor their helpers (`0x3BAC10`, `0x3B7470`, `0x1A87A0`,
  `0x1A8570`, `0x3CD500`, `0x2C6B30`, `0x2BE630`, `0x2BE6A0`, `0x2BE8A0`,
  `0x2BE8E0`, `0x2BE8F0`, `0x2BED00`, `0x2C6490`) read the running script
  thread `exe+0x2A250B0` or its VM. So each can run from frame code.

The register use was read from the listings. The start traps pass ints in
`edx`/`r8d`/`r9d`. `effect_fadeout` passes its time as a float in `xmm1`.
Replaying a call through its own trap keeps every convention native.

Every table entry, its flags and its full function body are pinned
(`ScriptMirrorPins.hpp`). The host swaps all of them or none, and a client
replays nothing until all of them match.

## Wire: `ScriptCall` (type 49, protocol 19)

Each packet is host → relay → every verified client. It is reliable and
scoped to the admitted room. It is never cached or replayed to late joiners.

- **Payload:** `{room tuple, seq, hostConnectionId, hostFrame, count}` (37
  bytes), followed by `count` entries (1–32).
- **Entry:** `{netId, objectId, table, index, u32 effect, u64 args[4]}` (45
  bytes each). `effect` is the host-minted effect id, nonzero exactly for the
  effect calls.
- **Args** are the call's values after the actor or effect, tagged as the script
  passed them: `'@INT'` (`0x544E4940`) or `'@FLT'` (`0x544C4640`). A vector
  travels as four `'@FLT'`.
- **Codec checks:** the codec accepts only the traps listed in
  `scriptCallShape` (`Protocol.hpp`), with their argument counts and effect-id
  rule. Floats must be finite with |v| ≤ 100000, and unused args must be zero.
- **Relay checks:** a verified host, the admitted room, a strictly increasing
  sequence, and the host's own connection id.
- **Client DLL checks:** the same host-source admission as `TargetAuthority`,
  plus the admitted host epoch and room.

## Host

The detour always runs the native trap. Before calling it, the detour copies
the arguments the trap reads: a start returns its effect in `args[0]`. For an
effect call, it also checks the effect while it's still live, because a kill
zeroes the part.

- **Actor calls:** the target actor, read as the trap reads it, must be an
  announced host spawn that the enemy mirror drives (`HostEnemyNetId`, D14
  denylist excluded).
- **Effects:** an effect started by such a spawn gets a fresh id, so a reused
  handle can't be mistaken for the effect before it. Later calls on that handle
  are sent while it's still the same instance. Calls on any other effect are
  counted as `untracked`.
- **Key:** netId + group for actor calls: rotation (`set_rot` and `set_xyzrot`
  share one), texture animation (one key per id), shadow, scissoring. Effect id
  + field for effect fields and pause.
- **Repeat:** the same value as the last one sent, or as one still queued. This
  costs nothing. A boss that repeats the same call every frame sends it once.
- **Change:** replaces a still-queued value for the key, or queues in call order.
- **Events:** starts, ends and `kill_all` always queue, in order, and are never
  repeats.
- **Flush:** at frame start, up to two `ScriptCall`s of 32 records each. Values
  become the new baseline only when the packet was accepted for sending.
- **Reset:** on every room change and every session generation change.

## Client

Each record goes to the copy EnemyMirror drives for that netId this frame, with
the same object id.

- **Actor calls:** replayed through the native trap, with that copy's own handle
  (`actor+4`). `set_xyzrot` instead goes through `0x3B65F0` with the four
  floats, because only a script-stack handle can carry a vector argument.
- **Start:** replayed through the native trap. The host's id is then paired with
  the effect it made here (handle, instance, part).
- **Field:** written into the paired effect's part. **Pause and ends:** replayed
  through the native trap with the paired effect's handle. Either way, only
  while that effect is still the same live instance (`stale` otherwise). An
  effect record with no pair and no held start is `unpaired`.
- **Spawn race:** a record for a copy that isn't driven yet waits in the `Hold`
  (latest value per key, up to 180 frames), and later records for the same netId
  wait behind it. All of them are applied once the copy is driven.

Nothing here writes HP, team, collision, motion or position.

## Evidence

Each machine logs one line per second with activity. These are the per-second
message counts the issue asks for:

```
[script-mirror] host sec frame=… calls=… repeats=… replaced=… queued=… packets=… records=… unmapped=… notMirrored=… untracked=… full=… sendFailed=… pending=… raw=… offThread=… effects=… sent=name:n,… seen=name:n,…
[script-mirror] client sec frame=… received=… applied=… held=… heldApplied=… expired=… refused=… unpaired=… stale=… faults=… effects=… records=name:n,…
```

- `raw` / `seen`: every detour entry before any filter, by trap. This shows
  whether a fight calls the trap at all.
- `calls` / `sent`: mirrored-enemy calls the host kept.
- `repeats`: calls that cost nothing on the wire.
- `records` / `packets`: what was sent.

Live runs (`net_script_mirror_boss`, BC Thresholder `05/0B` btl 72, three
instances on the package defaults):

- **`vuh2252-boss-04b` (2026-10-14 22:24, PASS) and `-05` (22:44, PASS, branch
  rebased on main `788dd05`).** Thresholder's (353) own script makes
  `obj_effect_start_bind`, `obj_effect_start` and `effect_fadeout` calls. Each
  went out as one record in one packet, about one per second at most. Both
  clients applied every record: a start that arrived before its copy was driven
  waited in the hold and was then applied, and each fade-out reached its paired
  effect. Nothing was refused, stale, unpaired or faulted, and nothing crashed.
- **No tilt in this fight.** Thresholder makes no `obj_set_rot`,
  `obj_set_xyzrot` or `set_pos` calls on its own effects here, so this fight
  shows starts and ends but no tilt or travelling effect.
- **Object 92 is not mirrored.** Most trap entries came from object 92, which
  is not an announced spawn. Its effect starts and one script-moved projectile
  (11 `effect_set_pos` and 11 `effect_set_dir` in one second) were counted as
  `unmapped` / `untracked` and not sent. That's correct for an actor whose
  client copy keeps its own brain.
- **Earlier runs.** `-01` and `-03` (two instances) crashed the client in the
  pause menu (VUH-2289, fixed separately). `-02` logged no `host sec` line,
  before the `raw` counter existed.

Offline: `kh2coop_script_mirror_test` covers the codec, relay forwarding and
refusals over loopback ENet, the send-on-change and hold rules, the effect
registries, and checks that the DLL's trap table agrees with the wire allowlist.

## Next slices

1. **Room:** `bg_hide/show` (1/36, 1/37; thunks to `0x1629B0` / `0x162A00`
   with an int id) and lights (1/181–184, 1/251; light 0 is the fixed slot
   `exe+0x2AE7B98`), when an enemy's script changes them.
2. **Effects started on another actor:** `obj_effect_start_bind_other` (1/149)
   needs the other actor mapped as well.
3. **Missions:** `mission_*` gauges and timers follow the host and are shown,
   not owned, on clients.
