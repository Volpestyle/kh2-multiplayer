# Damage pipeline — static notes (2026-10-01)

Static groundwork for VUH-1501 from the fully analyzed Ghidra project
(`scripts/ghidra.ps1`, Steam Global, RVAs from image base `0x140000000`). The
later hit-resolution section records live stacks and the diagnostic/manual
damage spike. Automatic ordinary client hits now have native courtyard
acceptance: 22 unique host applications and three client-only Shadow kills.
Enemy attacks against a connected client's local Sora, the full damage rule
and boss-specific behavior remain open.

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

## Remaining live evidence

The resolver and HP funnel below are already identified. The next ordinary
damage check must bind one genuine enemy attack to the registered client's
canonical local Sora and its native HP change in the same completed room.
Historical solo Sora damage and same-name remote puppets do not establish
that ownership. Step 1 uses each machine's native enemy AI; whether mirrored
enemy motions generate hitboxes in step 2 remains a separate requirement.

Legacy hit/HP diagnostics are bounded to 60/40 records per process and lack
an explicit canonical-victim and session join. Missing lines do not prove
that no incoming attack occurred. The passing client-kill fixture does not
test incoming player damage. Preserve that distinction when extending its
acceptance evidence.

The new default-off `KH2COOP_TRACE_HITS=1` observer uses the existing guarded
ApplyHitDamage, four-argument TakeDamage and ApplyStatDelta detours. It captures
checked canonical player/head/tracked pointers, actor metadata and HP, complete
read-only room/session facts, actual nested arguments and genuine returns.
One normal, fully covered ordinary enemy-to-client-local-player scope can be
classified from its adjusted negative delta and checked HP/clamp agreement.
Raw ApplyHitDamage RAX remains opaque. This is an offline candidate, with no
live hook-installation or incoming-damage evidence yet. The observer itself
does not implement ownership vetoes or step-2 replicated hitboxes.

The separate active DamagePolicy gate now evaluates checked current
session/roster, canonical roots and victim/source ownership for readable,
unapplied, nonzero HP records. Remote victims/sources and unknown sources are
suppressed; host enemies require canonical-host or positively stamped native
companion sources; canonical client attacks retain one claim before zero.
Positive companion stamps require the actual original FriendAI branch and
fresh native/cached friend-pointer agreement. The checked zero leaf changes
only hit+0x28; the original ApplyHitDamage still runs once and consumes the
record. Unsupported/changed facts retain the existing path, rather than proving
a veto. Release/ASan policy and context controls plus independent source review
are offline evidence; production adapter execution, general world-object
compatibility and native damage acceptance remain open. See
[the bounded policy contract](../SCENARIOS.md#active-session-hp-ownership-gate-offline-candidate).

The opt-in hit trace also carries the actual copied policy decision and operation
outcomes in a separate damagepolicy envelope keyed to the enclosing Apply scope.
The saved-log audit distinguishes a matrix-consistent decision from checked
noncanonical type-0 exclusion, successful zero, native consumption and unchanged
HP evidence. A zero-delta Stat no-write result can be zero; it is not a failed
HP application. Driver/AI membership, revalidation and third-roster post state
are not independently reconstructed. Current ordinary incoming-witness checks
remain unchanged. Serializer controls are synthetic and no live acceptance is
claimed; see [recorded policy outcomes](../SCENARIOS.md#recorded-ownership-decisions-and-checked-zero-outcomes).

## HP boundary (2026-10-02, VUH-1501)

Live: enemy HP is `*(actor+0x5C0)+0` (i32), max HP `+4`; every HP write in a courtyard fight came from one
instruction, at `0x3C0884` (the trap reported `0x3C0888`). Decompiled from there (`[GHIDRA]`):

| RVA | Role |
|---|---|
| `0x3C0860(stats, delta, idx)` | clamped stat add on 12-byte triplets `[cur, max, min]`; `idx 0` = HP. Damage is a negative delta |
| `0x3D2EB0(actor, delta, idx, reactFlag)` | ApplyStatDelta, the single HP funnel (~28 callers). Skipped when `actor+0x9B8` bit 2 is set: the native dead flag, set by `0x3D2E80` in the common death path and cleared by the player revive `0x3AA8D0` (VUH-1504 spike result). Damage → `0x3DCC10`, heal → `0x3DCBB0`. HP 0 on idx 0 → `vtable+0xB0(actor)` (death) |
| `0x3D5E50(actor, delta, idx, reactFlag)` | Four-argument TakeDamage helper: adds drive gauge to the victim (`0x3D3CF0`, scaled by `status+0x22C`), then tail-jumps to `0x3D2EB0`. `actor+0x18C` bit 14 suppresses the react flag |

The attacker-side resolver calls TakeDamage through a vtable; find it from a stack trace in a `0x3D2EB0` hook.
Puppet attack motions produced no HP writes (live), so hitboxes come from attack logic, not animation.

Saved-PE review distinguishes that helper from the handler virtual. Dispatcher
`3D3790` supplies five arguments to handler `vtable+E8`: handler receiver,
actor, delta, stat and a stack-passed flag. Its return address is `3D37D2`.
The four-argument helper ABI must not be applied to that virtual. Player hit
processing `3D60C0` calls `3D3BA0` at `3D6137`, returning at `3D613C`.
`ApplyHitDamage`'s forwarded RAX is not a semantic success/HP result;
`ApplyStatDelta` can return zero without a write. Checked native HP and actual
adjusted delta are required to establish application. Full saved-PE evidence
and the limits of historical Sora logs are in the
[victim contract](../../build/rig/victim_damage_native_contract_20261003.md).

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

Automatic ordinary HP claims require an unapplied record, stat zero and positive
damage; resolved `hit+0x20` types 5/6 are healing and cannot become damage claims.
The verified resolver follows `hit+0x1C` to ATTACK, then its `+0x10` owner handle
to the canonical local player. Diagnostic last-hit caches and low-address handle
fallbacks do not establish this ownership. The host callback uses the verified
`3D5E50` prefix `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57` and exact
ABI `void(actor, int delta, int statIdx, uint8 reactFlag)`; it is available only
after successful hook installation. This carries calculated damage through
TakeDamage, bypassing the full attack resolver and its survival/atkp behavior;
it does not prove those semantics or boss finishers. Offline results and the
passing native ordinary-Shadow claim/kill fixture are in
[SCENARIOS.md](../SCENARIOS.md#automatic-client-hits-native-courtyard-acceptance-2026-10-02).
