# Clone neutral input (VUH-1489)

**Stops a native Sora clone in a party slot from moving whenever the local player moves the stick.** The clone is the remote-player puppet.

Default off. Enable it with `KH2COOP_CLONE_NEUTRAL_INPUT=1` (exactly `"1"`). Code: `inject/src/CloneNeutralInput.inl`, included by `EntityHook.cpp`. Test: `tests/CloneNeutralInputTest.cpp` (`kh2coop_clone_neutral_test`). Supported exe: Steam `9002b2de` only.

## Status

- **Proven live: stick and movement suppression.** In GoA with the Friend1 party-leaf Sora clone, one owned game, the clone moved 0.0 units on all three stick pulses. Its mover speed stayed 0, and its action and animation never changed. The local player's movement was unchanged at 8.0 u/frame, within the phase-0 band. Run `20261007-005323`, which was the VUH-1489 lane's live-03.
- **Implemented but unverified: FIELD_COMMAND command suppression.** In GoA the attack (Cross) does not go through FIELD_COMMAND's command record: the record's id was never set while the player attacked, so the verdict was `NOT_VIA_RECORD`. No live run has yet shown the clone executing a command, or that execution being suppressed.

## Mechanism

Static RE, `[GHIDRA]`. Two native paths deliver the local pad to every player-class actor (objentry type 0).

**1. The actor's own pad pointer, `actor+0xDB8`.**
- The player factory `0x3A7CB0` passes `GetPadEntry(0)` to constructor `0x3A7A40`. That is processed entry 0, `exe+0xBF31A0`.
- The constructor stores it at `+0xDB8`.
- Buttons, and the movers for action-flag bit 3 and for kinds 0x18/0x35, read `**(+0xDB8)` and `*(+0xDB8)+0x30..0x3C`.

**2. The single shared FIELD_COMMAND (FC, `exe+0x2A10620`).**
- Every player actor has `+0xDC0` and `+0xDD0` pointing at FC.
- `0x3BFD30` runs two type slots per actor, in order:
  - `+0x18`, the FC tick (`0x3A8980` → `0x3B2340`). It copies entry 0's stick into `FC+0xB50` and rebuilds the command record at `FC+0x08`.
  - `+0x20`, the movement update (`0x3A89A0`). Its mover (`0x3A8FA0`) reads `*(+0xDD0)+0xB50` for ordinary actions (bit 3 clear). It then runs the FC command record on that actor (`0x3D6FC0`).
- So a clone moved exactly with the player even when its own pad pointer was neutral.

**What `actor+0xC` is.** It is the current action's handle, from `SetAction` `0x3B4DA0`. Handles point into a static per-action object table at `exe+0x750950`. Equal handles mean equal action ids, not a shared mover.

## What the module does (flag `1` only)

**Install** (`cloneneutral::Install`, called from `Initialize`). Install is refused, leaving the whole module off with log `[cloneneutral] configured=0 refused=<reason>`, when any of these holds:
- `KH2COOP_PLAYER_KIT` blocks native-Sora puppets;
- the exe timestamp is not `0x669E384A`, or SizeOfImage is not `0x2C2B000`;
- the 48-byte prologue of `0x3A89A0` differs;
- MinHook fails.

On success it allocates a never-freed 4 KiB page and installs the `0x3A89A0` detour. The page holds the neutral pad entry at +0x000 (0x68 bytes) and a `Stats` block at +0x100 (magic `CNI3`).

**Gate** (`cloneneutral::Gate`) runs in `HookedPerEntityUpdate` before each actor's own update, on the owner thread. It only considers player-class actors.
- **Canonical player** (list head or `[0x2A105D0]`): `+0xDB8` is only ever restored to entry 0, and only if it holds this module's neutral pointer.
- **A P_EX100 clone that is not canonical** (objentry id 84, name `P_EX100\0`): `+0xDB8` moves from entry 0 to the neutral entry. This never happens while `warp::TransitionPending()`.
- **Persistence:** once neutral, an actor stays neutral for its whole life. Its neutral state is re-zeroed on each of its updates.
- **Never written:** global entry 0.

**Movement-update detour** (`HookedMovementUpdate`) applies only to an actor that is player-class, has a neutral `+0xDB8`, has `+0xDD0 == FC`, and is not canonical.
- It runs the original with FC's command record neutralised exactly as `0x3D7F00` does: id = 0, flags `&= 0xE0`, target sub-struct = `0x3BDA50`'s `{0,0,0,-1}`. FC's stick copy is zeroed.
- It restores all of these in `__finally`. FC ends up as if the clone's movement update never ran.
- A non-gated `0x3A89A0` call that arrives while a window is open gets the real values and is counted in `nestedPassThrough`. Static analysis found no such path, and none was seen live.

**Stats** (read-only from outside by RPM): `neutralized`, `restored`, `refusedPointer`, `transitionSkips`, `hookInstalled`, `rezero`, `movementGated`, `commandSuppressed`, `nestedPassThrough`.

## Native reads and writes

| Address | R/W | When |
|---|---|---|
| `actor+0x918` → objentry `+0x00/+0x04/+0x08` | R | Gate, every updated actor (player-class test, descriptor) |
| `actor+0xDB8` | R/W | Player-class actors only. Write: entry 0 → neutral on a non-canonical P_EX100; neutral → entry 0 on a canonical actor |
| `exe+0x2A171C8`, `exe+0x2A105D0` | R | Canonical test |
| `exe+0xBF31A0+0x40..+0x67` | R | Copied into the neutral entry when neutralising |
| `actor+0xDD0` | R | Detour gate |
| `FC+0x08` (u16), `FC+0x0C` (u8), `FC+0x10..+0x1F`, `FC+0xB50..+0xB5F` | R/W | Inside a gated clone's `0x3A89A0` only. Saved, neutralised, then restored |
| exe header, `0x3A89A0..+0x2F` | R | Install verification |

## Live evidence

The VUH-1489 lane's private rig packets:

| Run | Lane | Result |
|---|---|---|
| `20261006-233109` | live-01 | `+0xDB8` swap alone: the clone still moved. This found the FC path. |
| `20261007-003232` | live-02 | Clone stopped. One pulse was flagged as a player slowdown, but frame-by-frame it was the same 8.0 u/frame; the input pulse was just held for fewer frames. |
| `20261007-005323` | live-03 | `PASS_STICK_ONLY`, three pulses per phase. Phase 0 (flag unset) confirmed the mechanism: equal movers, FC branch, bit 3 clear. |

## Known gaps

**FC consumers outside the `0x3A89A0` window still see the real stick and record.** These were found statically and not classified:
- `0x3AA4C0`/`0x42FE30` (→ static `exe+0x756B48`), `0x3AA700`;
- the `0x4034B0`/`0x403550`/`0x403BE0`/`0x403D70` form family;
- world-map `0x395970`/`0x396310`/`0x3964C0`;
- other `+0xDD0` users;
- command consumers other than `0x3D6FC0`.

In live-03 none of them changed the clone's action or animation in GoA.

**Scope.**
- GoA plain Sora (`P_EX100`) only. Forms and world skins are never neutralised. That fails safe: the clone reads pad 0 as before.
- The `+0xDB8` scan covers literal displacements only.

**A neutral clone has no native controller.** It stands still unless the puppet driver moves it. A future "release to native" path would need Friend AI.

## Follow-ups: the shared FIELD_COMMAND (not changed by this module)

**1. Double FC tick per frame.**
- With a clone in the room, FC ticks twice per frame: in the canonical player's update and in the clone's, both via `+0xDC0` → `0x3A8980`.
- A tick does a lot of work:
  - rebuilds the command record and copies the stick;
  - ORs press flags into `FC+0x0C`;
  - runs FC `vtable+0x10` (`0x3F29E0`, menu and command logic on the bound actor `+0xAE0`) and `0x408520`;
  - runs `0x3DD280` (lock-on);
  - decrements the `+0xC40` countdown.
- Menu state, lock-on and countdowns may therefore advance twice. It is not checked whether a press can trigger twice, or be cleared before the canonical consumes it.
- **Ruled out:** nulling the clone's `+0xDC0`. `0x3FD5A0`/`0x3FD5F0`/`0x3FD7E0`/`0x3FD720` dereference it without a null check.
- **Candidate:** a gated `0x3A8980` early-out for neutral clones, after an audit of every tick side effect.

**2. FC calls the clone makes on death, revive or menu toggles.** These run through the clone's own `+0xDC0` and act on the shared FC:
- `0x4150E0`/`0x415B40` (status `[+0x5C0]==1`) → `0x3FD5A0`;
- `0x3A7330`/`0x3A81C0` → `0x3FD5A0`;
- `0x3AA9E0` → `0x3FD7E0`/`0x3FD720`;
- `0x3A92C0` → `0x3FD7E0`;
- `0x42FF50` → `0x3FD5F0`.

A clone that dies, is revived (VUH-1504) or changes form can therefore reset or toggle the canonical player's command menu.

**3. Teardown.**
- `0x3A7120` and `0x3A8F30` unbind FC (`0x3FD610`). When `[0x2A105D0]` is non-zero they also clear it, whichever actor runs them.
- `0x3A9FA0` and `0x3AAB00` unbind and null `+0xDC0`.

If the clone runs these, they could detach FC from the real player or clear the player pointer. **Suggested first step:** a read-only probe of FC `+0xAE0/+0xC38/+0xC30` and `[0x2A105D0]` around a clone death, revive or retirement.
