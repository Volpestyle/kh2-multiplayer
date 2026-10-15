# Script-call mirror (VUH-2252)

D14 item 3. A client runs no brain for a mirrored enemy (`EnemyMirror.inl`), so
the calls that enemy's script makes on itself never happen on the client: a
boss's tilt, a texture animation, a shadow toggle. The host now sends those
calls, and the client replays them on its own copy.

On whenever the enemy mirror is on (`KH2COOP_ENEMY_MIRROR=1`, the player
default). `KH2COOP_SCRIPT_MIRROR=0` parks it on that machine.

## Calls mirrored (slice 1: actor calls)

bdscript traps, named as in OpenKH's `BdxTraps.cs`. Tables are at
`exe+0x753490` (`*(+8 * table)`), with 16-byte entries `{fn, flags}`. Each is
called as `fn(u64* args)` from the interpreter `exe+0x41B400`. The call list
follows TCM-Murray's world-mirror research (GPL-3.0, see
`research/PRIOR_ART.md`). No code was copied.

| Trap | Table/index | Function | What it does |
|---|---|---|---|
| `trap_obj_set_rot` | 1/1 | `0x42BF60` → `0x3B6560(actor, angle)` | rotation from one angle into `actor+0x680` |
| `trap_obj_set_xyzrot` | 8/2 | `0x537E70` → `0x3B65F0(actor, float4*)` | free rotation (tilt) into `+0x680`, bit 3 of `+0x9B8` |
| `trap_obj_texanm_start` / `_stop` | 1/85, 1/86 | `0x42F820` / `0x42F850` | texture animation on the render object `+0x7B0` |
| `trap_obj_hide_shadow` / `_show_shadow` | 6/45, 6/64 | `0x53E960` / `0x53EE90` | `+0x124` bit `0x200`, or `0x3CD820` |
| `trap_obj_set_scissoring` | 6/65 | `0x53EEB0` | `+0x8C8` bit `0x20` |

The safety check for each call is done statically (headless Ghidra, Steam
`9002b2de`):

- Each trap reads only its argument array (one u64 per argument, with the
  handle in the low word).
- It decodes the actor through `0x4AD270` twice: first the argument, then that
  object's `+4`.
- It then makes one call or one write.
- Neither the trap nor its helpers (`0x3BAC10`, `0x3B7470`, `0x1A87A0`,
  `0x1A8570`, `0x3CD500`) reads the running script thread `exe+0x2A250B0` or
  its VM. So each can run from frame code.

Every table entry, its flags and its full function body are pinned
(`ScriptMirror.hpp`). The host swaps them all or none, and a client replays
nothing until all of them match.

## Wire: `ScriptCall` (type 49, protocol 19)

Each packet is host → relay → every verified client. It is reliable and
scoped to the admitted room. It is never cached or replayed to late joiners.

- **Payload:** `{room tuple, seq, hostConnectionId, hostFrame, count}` (37
  bytes), followed by `count` entries (1–32).
- **Entry:** `{netId, objectId, table, index, u32 effect, u64 args[4]}` (45 bytes each).
  `effect` is the host effect key for the effect calls (next slice) and must be
  zero for the calls listed here.
- **Args** are the call's values after the actor, tagged as the script passed
  them: `'@INT'` (`0x544E4940`) or `'@FLT'` (`0x544C4640`). A vector travels as
  four `'@FLT'`.
- **Codec checks:** the codec accepts only the traps listed in
  `scriptCallShape` (`Protocol.hpp`). Floats must be finite with |v| ≤ 100000,
  and unused args must be zero.
- **Relay checks:** a verified host, the admitted room, a strictly increasing
  sequence, and the host's own connection id.
- **Client DLL checks:** the same host-source admission as `TargetAuthority`,
  plus the admitted host epoch and room.

## Host

The detour always runs the native trap first. It then reads the target actor
the same way the trap does, and maps it to the announced spawn
(`enemysync::HostEnemyNetId`). It keeps only spawns the enemy mirror drives (D14
denylist excluded), and hands the call to the `Sender`:

- **Key:** netId + group. The groups are rotation (`set_rot` and `set_xyzrot`
  share one), texture animation (one key per id), shadow, and scissoring.
- **Repeat:** the same value as the last one sent, or as one still queued. This
  costs nothing. A boss that repeats the same call every frame sends it once.
- **Change:** replaces a still-queued value for the key, or queues in call order.
- **Flush:** at frame start, up to two `ScriptCall`s of 32 records each. Values
  become the new baseline only when the packet was accepted for sending.
- **Reset:** on every room change and every session generation change.

## Client

Each record goes to the copy EnemyMirror drives for that netId this frame, with
the same object id.

- **Scalar calls:** replayed through the native trap, with that copy's own
  handle (`actor+4`).
- **`set_xyzrot`:** goes through `0x3B65F0` with the four floats. Only a
  script-stack handle can carry a vector argument.
- **Spawn race:** a record for a copy that isn't driven yet waits in the `Hold`
  (latest value per key, up to 180 frames). It is applied once the copy is
  driven.

Nothing here writes HP, team, collision, motion or position.

## Evidence

Each machine logs one line per second with activity. These are the per-second
message counts the issue asks for:

```
[script-mirror] host sec frame=… calls=… repeats=… replaced=… queued=… packets=… records=… unmapped=… notMirrored=… full=… sendFailed=… pending=… traps=rot:…,xyzrot:…,texanm:…/…,shadow:…/…,scissor:…
[script-mirror] client sec frame=… received=… applied=… held=… heldApplied=… expired=… refused=… faults=… traps=…
```

- `calls`: mirrored-enemy calls the host saw.
- `repeats`: calls that cost nothing on the wire.
- `records` / `packets`: what was sent.

Offline: `kh2coop_script_mirror_test` covers the codec, relay forwarding and
refusals over loopback ENet, the send-on-change and hold rules, and checks
that the DLL's trap table agrees with the wire allowlist.

## Next slices

1. **Effects:** `obj_effect_start*` (1/21, 1/22, 1/87, 1/149), paired by the
   host's effect handle. Then `effect_set_pos/scale/rot/dir/color` (0/38–41,
   0/67), `effect_pause` (0/66), and the ends (0/44, 0/51, 0/52, 0/85, 1/349).
   An effect handle isn't checked by the game once freed, so a pair must keep
   the effect's `+0x30` part and act only while it is unchanged (Murray hit a
   fault on a replayed fade-out).
2. **Room:** `bg_hide/show` (1/36, 1/37) and lights (1/181–184, 1/251; light 0
   is the fixed slot `exe+0x2AE7B98`), when an enemy's script changes them.
3. **Missions:** `mission_*` gauges and timers follow the host and are shown,
   not owned, on clients.
