# Twilight Thorn: limited two-seat reaction proof

VUH-2569 remains open. This result qualifies the living reaction/actor-owner
slice; it does not qualify a synchronized boss kill.

Build19 passed all202 serial CTest checks in153.01s. Its native inputs are based
on `edf2f2d`; the landing integrates docs-only main `e053a9e` without changing
those317 inputs or rebuilding. The DLL tested in live10 is the newly rebuilt
trimmed binary. Earlier full-source live09 evidence does not qualify it.

Live10, `20261021-001728_net_twilight_thorn_reactions_1`, passed34 steps in
single-PC two-seat loopback. Both seats used the real-door route and existing
pad pilots. Opening commands297/298/299 completed. Ground432 completed for
owner0 and owner1; the latter joins the host offer, client queued receipt and
host completion for resource67/occurrence31. Both pilots stopped at93.531s,
both neutral releases succeeded, and four raw clips finalized in two paired
30-second blocks. The footage has recording gaps; log-confirmed completion
and visible screen evidence are separate claims.

Independent result review verified all584 packet pins, the575-file exact source
archive and safe closure. Four protected saves,14 game-root entries and527
assets were unchanged; no owned instances or relay listener remained.

One earlier queued432/resource28/occurrence14 has no host completion and its
cause is unknown. The host was naturally downed once with no revive. There is
no kill, reward, mission16, event315, onward-travel, captured426, Creeper-count,
complete clone-visual or two-PC proof. Terminal support was deliberately
excluded from this landing. Those requirements remain on VUH-2569.

Published on [VUH-2569](https://linear.app/vuhlp/issue/VUH-2569/twilight-thorn-boss-sync-the-fight-across-seats-one-host-owned-boss):

- [Captioned paired-screen clip](clankie://evidence/sha256/b94398c49fe5d87bb3bc419a4e57640ea7070cef9d1f439b76421f54ca988940):
  59.27s,30fps, host left/friend right, game-only views. Two requested30-second
  blocks at ordinary speed, recording gap omitted, no audio or hardware frame
  synchronization claim. Sampled raw and final frames were visually inspected.
  Host Key Counter and later ground Reversal are visible; not every logged
  completion is filmed. The issue embeds the playable clip.
- [Decisive reports archive](clankie://evidence/sha256/602cefb76c190aa9289cf3de09ef01f6b16dcd0a81a5d03d07ecf5baa1295aa8):
  unchanged `live10/report.json`, `report.md`, `rc_proof.json`, independent
  `reviews/live10-result.md` and `checks/terminal.json`, `safety-closed.json`,
  `instances-closed.json`. Small reports are bundled because the evidence store
  excludes individual non-media files below16KiB and Markdown.
- [Exact source archive](clankie://evidence/sha256/52ae2432f8b281dd5eb31f302f11395744482e586477a2b2172dee0b29116a46).
- [Full build and202/202 checks](clankie://evidence/sha256/cab16553f5aff08305efe57d2452a8a780bb9b5cf73cc346625e82c5e3ea6be9).
- [Evidence index](evidence.json): all published raw clips, native/runtime logs,
  read-only observations, contact sheets and composition checks.

Original local run: `build/scenarios/20261021-001728_net_twilight_thorn_reactions_1/`;
safety/source closure: `build/rig/vuh2569-live10/`. Only this README and the
evidence index are committed; raw artifacts remain in the evidence store.
