# Damage pipeline — static notes (2026-10-01)

Static groundwork for VUH-1501 from the fully analyzed Ghidra project
(`scripts/ghidra.ps1`, Steam Global, RVAs from image base `0x140000000`). Nothing
here is live-verified yet; it narrows where the live write-watch on an enemy's
HP should land.

## Attack objects (RTTI)

| Class | vftable | Slots |
|---|---|---|
| `YS::ATTACK` | `0x5C5E78` | `[0]` `0x3CFCB0` (dtor), `[1]` `0x3D1660`, `[2]` `0x3D1430`, `[3]` `0x3D2130` |
| `YS::ATTACK_OBJ` | `0x5CE6C0` | `[0]` `0x40F4C0`, `[1]` `0x3D1660`, `[2]` `0x3D1430`, `[3]` `0x40F610` |
| `YS::ATTACK_MAGIC` | `0x5D7090` | `[0]` `0x437290`, `[1]` `0x437520`, `[2]` `0x4373E0`, `[3]` `0x4376B0` |
| `gb::ATTACK` | `0x5B5150` | `[0]` `0x1F6860` |

- Slot 1 (`0x3D1660`): returns the attacker's position (resolves the handle at
  `+0x0C`, copies a Vec4).
- Slot 2 (`0x3D1430`): builds a direction vector from the attack source to a
  target position — the knockback / hit direction.
- So an `ATTACK` describes an attack source; the resolver queries it through
  these virtuals rather than the other way round.

## `YS::ATTACK` constructor `FUN_1403cf830`

`ctor(this, attacker, source, atkpA, atkpB, int p6, uint p7)`; called from 8
sites (`0x53AA40`, `0x4372E0`, `0x4371B0`, `0x3D0780`, `0x1B8250`, `0x538920`,
`0x1CAAC0`, `0x40F1C0`) — the places attacks are created.

| Offset | Meaning (inferred) |
|---|---|
| `+0x08` | flags (`0x400` set when `p6 >= 0`) |
| `+0x0C`, `+0x10` | attacker actor handle (`FUN_1404ad240` packs a pointer into a handle) |
| `+0x14` | source handle (`param_3`) |
| `+0x18`, `+0x1C` | the two atkp lookup keys |
| `+0x20` | `p6`, or a default from `FUN_1403cf400` |
| `+0x28` | handle to `attacker + 0x80` (or a target node chosen by atkp flags) |
| `+0x30` | **pointer to the atkp entry**, from `FUN_1403eafc0(atkpA, atkpB)` |
| `+0x38` | attack team = atkp `+0x08` (`3` maps to `4`), or the attacker's team at `actor+0x4DC` |
| `+0x39` | hit mask `~((1 << team) | 1)` — which teams this attack can hit |
| `+0xC0` | `1.0f` (a damage/scale multiplier, by its default) |

- `actor + 0x4DC` holds the actor's team; the hit mask above is built from it.
- atkp `+0x12` bit 1/bit 3 and `+0x2C` change the target node and `p7`.
- OpenKH documents the on-disk atkp layout (`OpenKh.Kh2/Battle/Atkp.cs`): power,
  element, team, knockback, flags. `FUN_1403eafc0` is the runtime lookup into it.

## Corrections to earlier notes

- The "Axel HP copy helper" at `0x41C73E` (`docs/probes/AXEL_FIGHT_RE_SESSION.md`)
  is inside `FUN_14041c6c0`, a **script-VM store instruction** (pops a value off
  an interpreter stack and writes it to a variable), called only by the
  interpreter `FUN_14041b400`. The HP "mirror" it wrote is the AI script's own
  copy of HP, which is why writing it didn't stick. It is not the damage path.
- The debug strings `player attack:` / `friend attack:` / `enemy attack:` are
  labels in a debug-menu table at `0x749A50` (`move speed`, …, `anytime drive`)
  printed by `FUN_14039fa90`. The tunables they label are likely per-side
  attack multipliers, but the value storage isn't traced yet.

## Next (live)

1. Write-watch an enemy's HP during a mob fight → the resolver's RIP.
2. Expect the resolver to read `ATTACK+0x30` (atkp: power/element) and
   `ATTACK+0x39` (hit mask), and to call slot 2 for the knockback direction.
3. Decompile it with `scripts/ghidra.ps1 -Decompile <rip>` and name the
   (attacker, victim, ATTACK*) boundary the damage rule hooks.

## HP boundary (2026-10-02, VUH-1501)

Live: enemy HP is `*(actor+0x5C0)+0` (i32), max HP `+4`; every HP write in a courtyard fight came from one
instruction, at `0x3C0884` (the trap reported `0x3C0888`). Decompiled from there (`[GHIDRA]`):

| RVA | Role |
|---|---|
| `0x3C0860(stats, delta, idx)` | clamped stat add on 12-byte triplets `[cur, max, min]`; `idx 0` = HP. Damage is a negative delta |
| `0x3D2EB0(actor, delta, idx, reactFlag)` | ApplyStatDelta, the single HP funnel (~28 callers). Skipped when `actor+0x9B8` bit 2 is set (HP lock). Damage → `0x3DCC10`, heal → `0x3DCBB0`. HP 0 on idx 0 → `vtable+0xB0(actor)` (death) |
| `0x3D5E50(actor, delta, idx, reactFlag)` | TakeDamage virtual (thunks `0x3C2C10`, `0x1B03D0`): adds drive gauge to the victim (`0x3D3CF0`, scaled by `status+0x22C`), then `0x3D2EB0`. `actor+0x18C` bit 14 suppresses the react flag |

The attacker-side resolver calls TakeDamage through a vtable; find it from a stack trace in a `0x3D2EB0` hook.
Puppet attack motions produced no HP writes (live), so hitboxes come from attack logic, not animation.

## Hit resolution (2026-10-02, VUH-1501; from live stacks through `0x3D2EB0`)

Live stack, player hits on Shadows: `3D37D2 <- 3D3CD5 <- 410EFF <- 3D1937 <- 3D00C1 <- 14FDFE ...`;
enemy hit on Sora: `... 3D3CD5 <- 3D613C <- 3A8DC5 <- 3D1937 ...`.

| RVA | Role (`[GHIDRA]`) |
|---|---|
| `0x3D1730(ATTACK** atk, victim, contact)` | ResolveHit. `A = *atk`: `+0x30` atkp, `+0x10` owner handle, `+0x94` credited actor (its `vtable+0xE0` runs). Builds the hit record, runs `0x3D1B40` (likely knockback), atkp `+0x2F` → owner `vtable+0xE8`, then victim `vtable+0xC0` (OnHit), then frees the record via `0x3CE950` |
| `0x3D23C0(A, victim, atkp.id, atkp+1)` | builds the hit record, including damage (the damage calculation) |
| `0x410D60` | enemy OnHit: reaction via `0x3DADF0(victim, reactId*1000+…, attacker)`, damage, hit effects |
| `0x3A8DB0` → `0x3D60C0` | Sora's OnHit |
| `0x3D3BA0(victim, hit)` | ApplyHitDamage: once per record (`hit+0x18` bit 1), damage `hit+0x28` i32, stat `hit+0x25` u8, survive-at-1 (`vtable+0xA8`), then `vtable+0xE8` → TakeDamage |

Hit record: `+0x18` flags, `+0x1C` attack object handle (`+0x10` owner), `+0x20` atkp-like handle (`+0x04` type, `+0x12` flags),
`+0x25` stat index, `+0x28` damage. D4 client rule: pre-hook `0x3D3BA0`, claim, zero `hit+0x28`.
