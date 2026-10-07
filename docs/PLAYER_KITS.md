# Player kits (VUH-1513)

A player kit makes the local slot-0 player a different vanilla player-class
character for the session. The first kit is Roxas (`P_EX110`, objentry 90), using
the game's own model, moveset and weapon bones. No game files are patched or
shipped.

## Flag and scope

**`KH2COOP_PLAYER_KIT=0x5A`** (or `90`). Default off. When the variable is unset,
`inject/src/PlayerKit.cpp` installs nothing and reads nothing, and the avatar
stream's `character` byte is left alone. Any other non-`0` value is refused, and
native-Sora clone puppets stay blocked. A launcher selector is a follow-up; for
now `kh2ctl launch` and scenario `launch` steps pass the environment through.

Qualified scope: **world 4 (Hollow Bastion, including GoA `04/1A`) and kit 0x5A
only**. World 5, dual-wield Roxas (`0x323`) and Mickey (`0x5B`) each need their
own fixture before being allowed.

## How it works

On every area load, `39C860` calls `3E2EB0`. That function resolves the MEMT into
the member array at `exe+0x2A25300` (u16[18]). Member 0 is the player's objentry
ID. The DLL post-hooks `3E2EB0` (33 pinned prologue bytes). On the game's loading
thread it replaces member 0 with the kit, but only when all of these hold:

- the world is qualified;
- the NOW event program is 0;
- no event context or cutscene is active;
- the game resolved plain Sora (`0x54`).

Otherwise it logs a reason and leaves the native value. The MEMT itself is never
written. Each load logs one line:

`[playerkit] load world=… room=… evt=… native0=0x… kit=0x… result=applied|world-not-qualified|event-room|event-active|native-not-sora|already-kit`

`Shutdown` disables the hook and puts member 0 back only while it still holds the
kit. The live actor keeps the kit until the next load. While a kit is requested,
`AvatarState.character` carries the actual native player (0 Sora, 1 Roxas, 2
dual-wield Roxas, 3 Mickey). No receiver acts on it yet.

Addresses and static RE are in `KH2Offsets.hpp` (`MEMT_PTR`,
`RESOLVED_PARTY_MEMBERS`) and in the VUH-1513 evidence packets under
`build/rig/vuh1513-*`.

## Puppets while a kit is set

A selector-0 friend (the VUH-1489 native-Sora puppet) resolves through the same
member 0, so it would spawn as the kit. While `KH2COOP_PLAYER_KIT` is set to
anything but `0`, three things refuse it:

- `PuppetTarget` refuses any player-class target, checked live on every call.
- `NativePrivateStatus` initialization refuses.
- `tools/rig/native_sora/party_leaf.py` refuses the selector write.

Per-puppet member slots belong to the party-setup contract (`PARTY_SETUP.md`,
VUH-1519).

## Evidence

Live on Steam `9002b2de`, 2026-10-06 (`build/scenarios/20261006-233415_vuh1513_player_kit_dll_live_world4_1`):

- **Kit-off control launch:** no `[playerkit]` lines, unpatched resolver, Sora.
- **Kit launch, in order:**
  - the BB boot room stayed Sora (`world-not-qualified`);
  - GoA became Roxas, with member 0 `0x5A`, HP equal to the control, and move and jump;
  - the BB courtyard was Sora after the world change;
  - GoA became Roxas again.

The earlier rig-poke experiments (E1-02/03) showed the same chain from a MEMT
edit, with exact restores. Roxas uses Sora's own status record and HP, so Sora's
stats and progression carry over.

## Limits

- **Never open the command menu with a kit.** Roxas's moveset has no magic motions
  (56–67), and drive, summon and limit would load Sora's forms over Roxas. Kits
  are rig-only until a command guard exists (VUH-1509).
- **Natural door transitions are untested.** A load whose NOW event program is
  non-zero resolves natively (`event-room`), so the player may show as Sora after
  such a door. Fixtures so far use `kh2ctl warp` with `evt=0`.
- **Restore-on-exit is untested live.** A plain kill never runs `OnShutdown`. The
  only state is the in-RAM resolved array; there is no save or disk effect.
- **Combo continuation under automated input is unproven, for Sora too.** In the E1
  attempts, presses about 220–530 ms apart produced only the first attack. The
  best prior evidence is the `sora_move_attack` hit string (`atkp 126→128→147→130`)
  with presses about 380 ms apart and a Shadow in range. The next combo attempt
  should use that cadence with ≥6 presses against an engaged Shadow, judged by
  motion IDs and `[hit]` lines.
- Cutscenes that start later in an applied room still have the kit as the player.
  Story events use the high-poly Sora member, which the kit doesn't touch.
