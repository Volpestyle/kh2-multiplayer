# Native party HUD (VUH-2568)

Remote humans occupy KH2's two native friend HUD slots, ordered by network
owner slot with the local player omitted. With one remote human the remaining
slot keeps a distinct native NPC. Missing or expired human data leaves that
human's reserved slot empty. The local player gauge is unchanged.

Clankie approved this design on 2026-10-20. PC2 is unavailable (VUH-2339), so
the landing gate is unit controls, independent inject review and a labelled
single-PC loopback run. The host uses native damage/Fire/recharge; guest owner
HP/MP is RAM-controlled and labeled. Native guest Magic availability is
tracked separately in VUH-2573. Reciprocal two-PC video remains a follow-up; a
loopback run does not establish two-PC acceptance.

**Guest native MP follow-up:** VUH-2573 fixed the shared command-controller
double tick. Run `20261020-185351_net_guest_native_magic_1` on `9b6287b`
shows native guest and host Fire (10 → 0 MP), natural refill to 10, and matching
opposite HUD values. [Evidence](testing/vuh2573-native-guest-magic/README.md).
The original VUH-2568 clip below retains its explicitly RAM-controlled guest
scope; the new run supplies native guest MP proof on loopback.

## Native contract

Steam Global executable `9002b2de`, image-relative addresses. Static evidence
comes from the analyzed executable; live evidence is recorded separately below.

| Consumer | Address / fields |
| --- | --- |
| Native friend gauge vtable | `0x5B2158` (`dk::GAUGE_FRIEND`) |
| HUD construction | `0x16C3A0` unconditionally allocates both `0x1710`-byte friend gauges; pointers at `0xABD250`, `0xABD258` |
| HUD actor lookup | `0x1689A0(index)` reads `0xABCFE8 + 8*index`; index 0 is local |
| Friend binding | `0x182400` selects friend index `gauge+0x38 + 1`, stores actor at `gauge+0x40`, loads its portrait |
| Friend update | `0x182730`; base update `0x17EE60`, visibility `0x17ECE0` |
| Current/max HP | `0x18E8A0`: `actor+0x5C0` points to status; signed 32-bit HP at `status+0`, max HP at `+4` |
| Current/max MP | `0x18E8D0`: signed 32-bit values at `status+0x180`, `+0x184`; native Sora reports `100/100`, not fixed-point `10000/10000`. Player MP display: `0x181AF0` |
| Player recharge | `0x3C1D10` tests float `status+0x1C0 > 0`; `0x3C20E0` reads floats `+0x1BC/+0x1C0` |
| Portrait | `0x17E980` calls `0x3B57B0`, which resolves the actor's `+0x928` package's `face` resources (BAR types `0x18`, `0x19`) through `0x3A6480` |
| Low HP / downed face | `0x18E920` / `0x3C1D30` test HP against one quarter max; `0x18E940` reads actor flag `+0x9B8` bit 2 |
| Portrait fitting | `0x193310`, `0x193370`, `0x1933D0`: Sprite RectParam is at `+0x20`; scale triple at `sprite+0x198`, transform flags at `+0x108` bit 1. Player portraits `0x54/0x5A` use 65% width/height, restored with tint on release |
| Portrait tint | `0x16ABF0`: Sprite flags bits 9/10 retain RGB at `sprite+0x17C` while inheriting parent alpha; native FACE is `gauge+0x50` |

The adapter checks hook/bind/visibility entry bytes before installation.
Only the checked HUD callers of `0x1689A0` can receive a display view. Index 0
and every other caller delegate unchanged. The actual HUD actor table is never
written.

## Ownership and lifetime

Protocol 29 extends `SeatState` from 45 to 72 payload bytes with owner vitals.
The relay's existing verified connection, roster, sequence-floor and owner-slot
stamping rules apply. Invalid values are refused by the codec. The DLL samples
the canonical local actor's own aligned status-pool record directly; a fixed
slot-zero fallback cannot author another player's health. Logical downed maps
to HP zero without killing a native clone.

Changes send at most once per 50 ms, unchanged available values refresh every
250 ms, and a receiver expires values after 500 ms. A generation, roster or
local-seat change discards every sample. Values also require the current room.
The same channel works when the prologue layout is explicitly off.

Each native friend gauge sees a stable DLL-owned actor/status display view.
The only actor-shaped fields supplied are status, portrait package and downed
flag. HP/MP come exclusively from the owner's accepted SeatState. Portraits
come from the current admitted body's package; duplicate profiles may use the
canonical local body's identical package. Gold, blue and green tints follow
network owner slots 0, 1 and 2, independently of native friend index.

Views require a current network binding and native load serial, no pending
transition or event, and fresh game-thread observation. Known human puppet
targets are excluded from the spare NPC slot even before portrait admission;
player-class bodies cannot be mistaken for NPCs. Only distinct native
party-member actors qualify for NPC fallback. No native actor, status record,
SAVE body or party row is written by this adapter.

Shutdown retires projection first. Pinned cleanup callbacks survive global
hook disable and restore bindings and original portrait tint on the native
HUD thread. Their originals and display storage remain mapped until process
exit, including a manual `OnShutdown` / `FreeLibrary` request.

## Verification

PASS on the `c59045a` integration, product revision `80bc7f2`:

- Release build and all 199 CTest targets passed; all 45 native-HUD adapter
  checks also passed under Windows AddressSanitizer.
- Independent inject source review: ADOPT, including portrait fitting,
  owner-record reads, NPC exclusions and shutdown restoration.
- `20261020-175813_net_native_party_hud_1`: all five segments and every step
  passed. Owner/view reads matched guest HP 69 and MP 3, then MP 0. The host's
  native test hit changed HP 80 to 73; real Fire consumed MP 10 to 0 and the
  guest HUD matched. Natural host recharge and RAM guest refill reached both
  HUDs. A same-room reload bound fresh views. Inspected captures show fitted
  portraits and owner tints; the pre-load host gauge preserves Goofy.
- Closure: four protected saves, 801 source files, 14 game-root files and 527
  asset files unchanged; all pinned inputs unchanged; no owned games or relay
  left running. The adapter does not write real actor/status/SAVE data; the
  fixture's RAM controls are explicitly identified in the clip and report.

[Evidence and method](testing/vuh2568-native-party-hud/README.md) records the
approved loopback scope, retained reports and remaining two-PC acceptance.
Three-seat mapping, downed/revive, freshness, reconnect, NPC preservation and
shutdown are unit controls; this live run covers two Sora profiles on one PC.
