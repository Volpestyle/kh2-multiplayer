# VUH-2573 native guest Magic evidence

**PASS:** `20261020-185351_net_guest_native_magic_1`, product/main `9b6287b`,
protocol 29 unchanged. All 71 steps and three segments passed. The guest and
host each select Magic with one Down, cast native Fire (10 → 0 MP), and
naturally recharge to 10. The opposite native party HUD matches both changes.
This supplies VUH-2568's guest native MP proof on single-PC loopback.

[Captioned clip](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/fa6797b8-dc95-4e81-a0a5-b1a5d149547a/05fe1b6f-ec93-4e30-bcba-4d7a8eb6cde5)
shows host left / guest right, native guest Fire followed by native host Fire,
then a labeled cut to natural recharge completed. Initial RAM setup is labeled.
[Passing scenario report](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/63182531-8446-4be0-ad40-923c821321fc/ebd62368-0f4d-485c-b125-5fa483ed2dcf)
is attached with the clip to [VUH-2573](https://linear.app/vuhlp/issue/VUH-2573).

## Mechanism

Magic was eligible. The guest's neutral player-class clone also ticked the
canonical player's shared FIELD_COMMAND through
`0x3A8980 → 0x3FD690 → 0x3B2340`, consuming the same menu edge twice. The
new guard skips that tick only for already-neutralized noncanonical actors
with both command pointers bound to the shared controller. The canonical
native path and spell eligibility remain intact; no save or MP writer is added.

[Native contract and diagnostic correction](../../NATIVE_GUEST_MAGIC.md)
records the addresses. The old `spellCount` and `0x3F3110` lead described the
Drive list, not Magic. Probes before the fix reproduce the skips at 10 and
100 MP while the native callbacks admit Magic/Fire.

## Method and limits

Two Sora profiles, one PC, loopback relay bound to localhost. The fixture uses
pad input without focus changes. Initial Fire availability, 80 HP and 10 MP
are RAM setup. After that, both MP costs and refills are native; no MP writes
occur. Original Fire bytes are restored before closure. No in-game saves.

The 18-second presentation uses the first 14 seconds of the two 18-second
renderer action clips, followed by four seconds after natural refill. No
speed changes; audio omitted. All 360 presentation frames were inspected on
15 labeled sheets: game pixels and captions only, no private desktop content.
The guest clip starts in the Fire submenu; one-Down selection and exact MP
values come from scenario memory assertions. This is not a latency measurement.
Two-PC reciprocal acceptance and other character combinations were not run.

## Checks and closure

- Release clone-neutral and native-party-HUD tests: 2/2 passed.
- Clone-neutral Windows AddressSanitizer passed. MSVC UBSan was not run.
- Independent inject review: ADOPT for live validation, followed by the passing
  run. No blocking defect found in the pinned ABI, canonical pass-through,
  shared-controller ownership, partial-install rollback or save safety.
- Four protected saves, 801 source files, 14 game-root files and 527 asset
  files unchanged. Sealed inputs unchanged; owned games absent; relay 27813 free.

## Retained bundle

- `approved-live/`: report, native clips/captures and exact process logs.
- `presentation/`: captioned MP4, edit commands and all-frame review sheets.
- `safety/`, `sealed-inputs/`: closure inventories, product copies, checks,
  independent review and pinned source revision.
- `probe01/`, `probe02/`, their safety folders and `re/`: causal controls and
  static traces, retained with their original diagnostic labels.
- `raw-pins.json`: retained-file size/SHA-256 seal.

The lead published the qualifying files. The returned [evidence manifest](evidence.json)
records retained objects and durable links. The store takes media and
non-Markdown files of at least 16 KiB; Markdown and small text stay out by design.
The approved-live report's 71/71 PASS and independent review's ADOPT conclusion
are summarized above, and the complete report is attached to the issue.

- [Captioned native guest Fire / reciprocal MP clip](clankie://evidence/sha256/6f5d83b11ac0f215505078ede8adfae23174fcb17e30655a22f12d801912728f)
- [Complete retained-file size/hash seal](clankie://evidence/sha256/60384d4ad0bae7833015c2b7713566b23d1f1d19d4b2c04cfd48c4c430730a62)

Every returned manifest entry was checked against its PC file's size and hash.
Only this README and `evidence.json` belong in Git. Raw media/products stay in
the evidence store; smaller source records remain in the PC bundle.
