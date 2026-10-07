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

**VUH-1519 flag matrix.** `KH2COOP_PLAYER_KIT`, `KH2COOP_REMOTE_KIT_SLOT` and `KH2COOP_PARTY_NATIVE` are mutually exclusive, and each conflicting side refuses. The party-native observer shares this module's `3E2EB0` post-hook. The order per load is kit, then remote, then observer, all inside one SEH scope.

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

## Seeing a remote player's kit (receiver side)

**`KH2COOP_REMOTE_KIT_SLOT=1`** on the receiving game. Default off. It is refused
while the same game has a local `KH2COOP_PLAYER_KIT`, because a receiver that is
itself a kit isn't enabled yet. The flag makes the Friend1 native puppet show the
remote player's kit; the receiver itself stays Sora. Scope: world 4 and Roxas only.

**Layout.** Before the session, the rig or party setup writes Friend1 selector 3,
giving the GoA row `00/03/02/12`. Member 3 is the native world-ally member, which
nothing spawns while the world slot is `0x12`. With two player-class spawns the
game builds them in this order:

1. The player-slot actor, from **member 0**, is built first (raw566). It becomes
   the clone that the puppet driver binds.
2. The Friend1 actor, from **member 3**, is built last (raw567). Every player-class
   constructor stores the canonical player pointer, so this one becomes the
   receiver's own player.

**What the hook writes.** After each area load, the remote branch of the
`3E2EB0` post-hook checks that all of these hold:

- world 4;
- NOW event program 0;
- no event context or cutscene;
- row exactly `00/03/02/12`;
- native member 0 is Sora.

If they do, it writes:

- **member 0 = the remote kit:** `0x5A` when puppet 0's streamed roster byte is 1,
  otherwise Sora `0x54`;
- **member 3 = Sora `0x54`.**

Both originals are recorded. Shutdown restores each member only while it still
holds our value. Each load logs one line:

`[playerkit] remote load world=… row=… roster=… native0=… native3=… set0=… set3=… result=applied|world-not-qualified|event-room|event-active|row-not-remote-layout|native-not-sora|changed-under-us`

**Where the roster comes from.** `PollPuppetPoses` latches the roster byte from
each validated pose. A change is logged as
`[playerkit] remote roster puppet 0: <old> -> <new>`.

**When it takes effect.** The roster only reaches the DLL after the session's
first host-room load has finished: poses are admitted only after the client
arrives. The Roxas clone therefore needs one more **host-led** room load after
the latch. A client can't warp itself while the host owns its room.

`NativePrivateStatus` keeps the clone's status private in this mode (see
`NATIVE_PRIVATE_STATUS.md`).

## Evidence

Live on Steam `9002b2de`, 2026-10-06 (`build/scenarios/20261006-233415_vuh1513_player_kit_dll_live_world4_1`):

- **Kit-off control launch:** no `[playerkit]` lines, unpatched resolver, Sora.
- **Kit launch, in order:**
  - the BB boot room stayed Sora (`world-not-qualified`);
  - GoA became Roxas, with member 0 `0x5A`, HP equal to the control, and move and jump;
  - the BB courtyard was Sora after the world change;
  - GoA became Roxas again.

Remote kit, live on 2026-10-07
(`build/scenarios/20261007-011457_vuh1513_remote_kit_puppet_goa_1`). The host had
`KH2COOP_PLAYER_KIT=0x5A`; the friend had `KH2COOP_REMOTE_KIT_SLOT=1` and private
status. The run showed:

- **Kit reload:** the friend logged `remote load … roster=1 set0=0x5A set3=0x54 applied`.
- **Clone:** the Friend1 clone was Roxas (object 90, status key 14), driven by the
  stream. It played motions 0, 2, 3, 4, 5, 6 and 151 itself.
- **Private status:** one damage on the clone took it 24 → 23 while the friend's
  own Sora stayed at 24/24.
- **Clip:** the friend's clip shows the host as Roxas.
- **SAVE:** the clone (key 14) and the local Sora (key 1) share the same personal
  SAVE pointer. The private-status commit veto for key 14 wasn't exercised in this
  run.
- **Teardown:** the runtimes missed the fixture's natural-exit deadline and were
  stopped. This is a harness timing issue.

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
- **Remote kit limits:**
  - one puppet (Friend1), one room, Roxas only, and a Sora receiver only;
  - the roster latch isn't reset on disconnect;
  - a skipped remote branch leaves the members native (in GoA, member 3 would be
    Riku), so rigs must treat any non-`applied` remote line with the remote row as
    refuse-and-restore;
  - per-puppet member slots for more remotes belong to VUH-1519.
- Cutscenes that start later in an applied room still have the kit as the player.
  Story events use the high-poly Sora member, which the kit doesn't touch.
