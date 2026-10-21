# Native guest Magic (VUH-2573)

The extra player-class actor must not tick the local player's shared command
controller. Neutralizing only the clone's own pad and movement leaves a second
consumer of the canonical player's menu input. A single Down then advances
Attack → Magic → Items; Up moves Items → Magic → Attack. This resembles a
disabled Magic command, although the native spell eligibility checks accept it.

## Native contract (Steam Global `9002b2de`, RVAs)

- `0x3BFD30` dispatches each actor's type slot `+0x18` before movement `+0x20`.
- `0x3A8980` loads `actor+0xDC0` and dispatches it to `0x3FD690`.
- `0x3FD690` advances lock-on, the shared command tick `0x3B2340`, and the
  controller's `+0xC40` countdown. `0x3B2340` reads its own pad at `FC+0xAE8`,
  including command button edges. This remains pad 0 when the clone's
  `actor+0xDB8` has been neutralized.
- `FC = exe+0x2A10620`; the player-class constructors share it at `+0xDC0`
  and `+0xDD0`. The native canonical actor is `[exe+0x2A105D0]`.
- `0x401D60` is native command admission. Magic category 2 checks MP/recharge,
  mission restrictions, common flags, form restrictions and `FC+0xBE4` command
  blocks. `0x3C5940` permits casting with less than the spell's MP cost when
  the per-spell charge setting permits it.

**Correction to the original issue diagnostics:** `0x3F3110` fills the Drive
list at `FC+0x1A8`; `FC+0x24` bit 5 concerns that list. Its count at
`exe+0x2A107D8` was mislabeled `spellCount`. Magic is refreshed by `0x3F3460`
from IDs at `FC+0xB68` into the list at `FC+0xA8`; its count is at
`exe+0x2A106D8`. Do not infer Magic eligibility from the Drive list.

## Fix and scope

`CloneNeutralInput.inl` checks the complete 17-byte `0x3A8980` wrapper and
installs a second hook beside its existing movement hook. It skips the command
tick only for an already-neutralized player-class actor whose `+0xDC0` and
`+0xDD0` both point at the shared FC, excluding both canonical identities
(native player and active-list head). Other calls delegate unchanged.

The canonical actor retains the native menu, lock-on, countdown, spell selection,
eligibility, MP cost and recharge paths. The fix writes only a DLL diagnostic
counter; it does not change save bytes, grant spells, change MP or override an
eligibility result. Existing clone movement/input handling remains in place.
Both hooks must install before neutral input is published; partial installation
delegates and attempts to disable every created hook. No protocol change.

## Evidence

Read-only probes `20261020-183800_net_guest_magic_probe_1` and
`20261020-184247_net_guest_magic_probe2_1` reproduced the cursor skips with
protected saves and pinned inputs unchanged. In the second probe both seats'
native callbacks returned Magic 0 and Fire 2 at 10 MP (cost 12, deficit policy 1).
At 100 MP the guest still skipped Magic. Mission, form, system and command-block
flags did not reject it. Neither changing charge nor forcing an eligibility
return is justified by that evidence.

**PASS:** `20261020-185351_net_guest_native_magic_1` on product `9b6287b`,
all 71 steps and three segments. One Down selected Magic (index 1) on each seat;
each owner cast Fire natively, spent 10 MP to 0, and naturally refilled to 10.
The opposite native party HUD matched each drop and refill. The guest log
confirms skipped ticks on its extra Sora while the canonical actor remained
native. The host's Fire regression passed in the same run.

The retained fixture `tools/scenario/scenarios/net_guest_native_magic.json`
uses pad-only input and initial RAM setup of Fire / 80 HP / 10 MP. No MP writes
occur after setup; original Fire bytes are restored. Four protected saves,
801 source files, 14 game-root files and 527 asset files stayed unchanged;
pinned inputs stayed unchanged, owned games closed and relay port 27813 freed.
Release clone-neutral and native-party-HUD tests, clone-neutral Windows ASan,
and independent inject review passed. No UBSan run was available on MSVC.

[Evidence and scope](testing/vuh2573-native-guest-magic/README.md): captioned
single-PC loopback with two Soras. This completes VUH-2568's guest native MP
proof on loopback; two-PC reciprocal acceptance remains a separate follow-up.
