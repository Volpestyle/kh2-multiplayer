# VUH-1808: remote puppets' attacks make the local player flinch

Branch `fix/vuh-1808-friendly-fire`, based on main `89df9b0`. Offline only: no KH2, no rig.

## Root cause (main 89df9b0)

- The puppet driver writes team 0 on every driven actor so nothing can hit it
  (`inject/src/EntityHook.cpp:2714`). The puppet's own replayed attacks then carry team 0. Their hit mask
  `~((1 << 0) | 1)` = `0xFE` includes the local player's team 1, so the native eligibility check
  `3D2060` allows the hit and `BuildHit` creates a real hit record against the local player
  (`docs/probes/DAMAGE_STATIC_NOTES.md`, "Hit eligibility").
- The ownership veto runs only later, inside the `ApplyHitDamage` hook. `DamagePolicy` returns
  `ZeroHp/RemoteSource` (`inject/src/DamagePolicy.cpp:24`). `TryZeroHp` clears only `hit+0x28`
  (`EntityHook.cpp:943`), and then the original `ApplyHitDamage` runs once with the same victim and hit
  (`EntityHook.cpp:954`). That call and the hit record still drive the reaction: flinch, star burst and the
  red portrait flash.
- A fix already existed: `AllyHit` refuses these pairs at `3D2060` (commit `749c0d9`, live PASS
  `20261007-130721`). It was off by default (`AllyHit.cpp`, `if (n == 0) return true` on main), and
  `tools/launcher/friend_package.py:63` strips every `KH2COOP_*` variable from a friend's game. Players
  never got it. It also recognised only player-class (objentry type 0) attackers, so a friend-slot
  companion puppet (type 1, also on team 0) was not covered even with `=1`.

## Fix

A new default `Mode::Puppet`, used when `KH2COOP_ALLY_HIT` is unset or empty. At `3D2060` it refuses a
native-allowed hit only when all of these hold:

- the attacker (owner `+0x10`, or source `+0x14`) is an actor the puppet driver is driving right now, that
  is `g_puppets[i].actor && applied`, supplied by an EntityHook callback;
- the attacker is not the canonical local player;
- the victim is player-class or a team-1 non-player;
- the atkp kind is not 5 or 6.

Every other answer is native, and with no driven puppet nothing is read. `=0` is the opt-out. `=1` and
`=trace` work as before. On a refused install (byte mismatch, no resolver, no driven source) the hit stays
native and the log says so. `DamagePolicy` is unchanged and stays the backstop.

Files: `inject/src/AllyHitPolicy.hpp`, `AllyHit.hpp/.cpp`, `EntityHook.cpp` (the callback),
`tests/AllyHitPolicyTest.cpp`, `docs/PARTY_SETUP.md`.

## Verified offline

- `kh2coop_allyhit_test` (Release, MSVC): 65 passed, 0 failed (41 before the change). CTest `-R allyhit`
  1/1. The grid covers 432 puppet-mode cells. Hook-body cases: driven clone → local refused, via source
  refused, companion puppet → local refused, puppet → Goofy refused. Native in every other case: enemy →
  local, local → enemy, local → clone, undriven clone, heal kinds, native refusals, released puppet, and
  the local player named as driven.
- 8 hand-written mutants of the new logic: 8 killed.
- `kh2coop_inject.dll` builds. No other test targets were run.

## Still needs live verification

- A live two-player run where a remote's swings overlap the local player. Expect `[allyhit] ... puppet
  ... verdict=refuse` rows, no `[hit]` lines with a puppet as attacker, and no flinch or portrait flash.
- The `3D2060` hook is now installed in every default launch, including solo play. It is inert without a
  driven puppet, but its cost and safety outside co-op have not been measured live.
- The `applied` window: an attack spawned while driven, still live after release, keeps its team-0 mask
  and is no longer refused. This is assumed rare and has not been checked.
- Whether friend-slot companion puppets replay attacks that hit at all has not been observed live. That
  case is covered offline only.
