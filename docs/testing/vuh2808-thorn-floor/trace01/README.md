# VUH-2808: first live floor trace

Run `20261028-154348_vuh2808_thorn_floor_trace_01_1`, 2026-10-28, PC1 single-PC
loopback. Source and DLL are the approved **d430634** checkpoint, subsequently
landed on main. DLL SHA-256:
`60ED2984E03199BEE0B193E1A2ABF3B7B67D01865F9D54EB19926D4985348ABC`.
Windows Release build and serial ctest passed **297/297** before this attempt.

**Result: native cause remains open; no recovery fix.** The scenario failed at
runner index50 after both pilots reported `cooperative revive full-party-down
exceeded 60s` (606.9s total). The host took the native full-party game over.
The retained observations did not reproduce the zr103 floor fall. This run
supplies a control and confirms that the position/grounding trace works.

## What the trace establishes

| Observation | Evidence |
|---|---|
| Host boundary trace | 32,508 rows, frames20974–28343, all `reads=3FF`, no saturation |
| Friend boundary trace | 31,832 rows, frames16357–23625, all `reads=3FF`, no saturation |
| Friend-owned roles | 37 host starts; the friend trace retains36 of those roles |
| Position replay after native work | 1,402 changed friend XYZ samples end at the retained role position; all match a current host-role position at the trace's three-decimal precision. Of these,607 change Y. This is a value match, not a packet-sequence join. |
| Native collision flag during current host roles | All8,809 host clone samples with a nonzero matching D48/expected thread have `ACTOR_NO_COLLIDE` (0x40) clear |
| Refused visual binding | 23 host refusals name `role-not-current currentReason=coroutine-replaced` |
| Heights | Maximum recorded Y is0 for both trace streams; no recorded below-floor position |

For example, role2147483686 emits host frame20975 position
`(-371.518,-25.068,357.457)`. At friend frame16359, the native result is
`(-349.874,0,391.188)` and post-owner becomes exactly that host position,
including Y. The source path remains `OwnerPostUpdate` in
`inject/src/ScriptActorOwner.inl`: host op4 XYZ/facing emission, client role
position receipt, then a three-float memcpy and dynamics clear after native
update. There is no floor query in that replay path.

The same role supplies an important no-fall control. Host frame21041:

| Boundary | D48 | Y | Motion |
|---|---|---:|---:|
| pre-native | expected coroutine `7FF7278006D0` | -405.352 | 252 |
| post-native / post-owner | 0 | -405.352 | 252 |
| post-puppet | 0 | -395.206 | 6 |

The native lifecycle-cancel receipt at frame21041 clears D48. The visual bind
then refuses `coroutine-replaced`, exactly the first identity predicate in
`SmOwnerCurrent`. The next puppet lines also say motion252 is missing from the
clone banks and it shows0; the final post-puppet snapshot reads motion6. Thus
both the refused bind and missing-bank fallback can follow a coroutine end
without a floor fall. Neither alone identifies the initial floor failure. The
zr103 chronology from the parent report still matters: its clone was already
below the floor before that cancellation/refusal/missing-bank fallback.

The no-collision flag and native move/airborne fields are state observations;
they do not prove that the engine ran a terrain query. The original hypothesis
of an ungrounded Reversal clone remains unproven for zr103. This control does
not establish native out-of-bounds recovery and does not justify a Y clamp.

## Floor regression coverage

`thorn_floor.json` judges749 pilot positions (friend440, host309), with no fall
observed and maximum judged Y0 on either seat. Its overall result is
**UNAVAILABLE**, because the host has56 unclassified event-camera observations
and52 unreadable observations. No camera RC qualifies in these retained pilot
samples. Do not call this a passing floor acceptance run.

The profile is the existing Creeper-first s254b noclip scenario with the floor
trace flag on both boots. Gameplay routes and assertions are unchanged. A
paired15s/10fps clip near fight block02 replaces the usual large paired blocks
for disk headroom. START: Linear `5dc4f68d`; END: `fabeddf0`.

## Safety failure and independent closure

The scenario runner checked four saves and found no changes. The outer
PowerShell closure nevertheless exited1 with `Safety closure failed`. Its
`@(Get-Content ... | ConvertFrom-Json)` retained the four baseline records as
one nested array. The failed receipt reports `saveFiles=1` and flags that
wrapper as changed. That receipt is preserved; it is not evidence of changed
save bytes.

Independent recheck at20:58:22UTC:

- **4/4** actual saves match the pre-run SHA-256 and the retained copies' hashes;
  full byte comparisons also match. There are no extra or missing save files.
- Full install comparison: **541/541 unchanged**, no added/removed/changed file;
  capture and diff exit0.
- KH2, PCSX2 and this worktree's remaining processes are absent; UDP27809 is free,
  the worktree lock is absent and its tracked tree is clean.
- The run released its canonical lock. Kit's subsequent separately authorized
  build owns the new canonical lock; it was not touched.
- Lead13 authorized clearing only this run's matching needs-verify record after
  verification. It was cleared at21:00:41UTC, with before/after receipts retained.
  No save or install file was restored. No second attempt was launched.

The corrected closure delegates comparison to `tools/rig/save_manifest.py`,
checks its exit and JSON verdict and, for this PC1 run, also requires four saves.
The helper accepts nonempty one- or multi-row manifests, hashes actual files and
copies, performs full byte comparisons and requires the same file set. Empty or
wrapped arrays fail. The archived corrected wrapper is for review before reuse;
it has not been used for another PC run.

Offline validation through `clankie heavy -- python3 -B tests/test_save_manifest.py`
passes **6/6 test methods (10 cases)**. Identical and one-modified actual saves
pass/fail as required at both1 and4 rows. Modified copies, empty baselines,
wrapped arrays and missing/extra saves all fail. The original false-failure
receipt supplies the live counterexample to the old PowerShell comparison.
No inject/common code changed in this follow-up; the Windows297/297 result
belongs to the unchanged d430634 native source, not a new full build.

## Evidence

Raw logs, pilots, report and a metadata bundle are listed under `trace01/` in [the evidence manifest](../evidence.json).
The metadata bundle retains the original false closure, independent checks,
install/save hash baselines, START gate, exact scenario, analysis script/output,
original and corrected wrappers, and offline test output. Actual save files and
copyrighted install assets are excluded.

PC records remain under this worktree's `build/rig/operator-logs/vuh2808/thorn-trace-01/`
and `.local/vuh2808-thorn-trace01/`; scenario artifacts remain in the run directory.
Mac working copies: `.local/vuh2808-thorn-trace01/`.
