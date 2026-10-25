# Thorn defeat receipt (VUH-2807)

`tools/scenario/harvest_thorn_defeat.py` harvests one complete, closed two-seat
Thorn fight. It reads the scenario report, full inject logs and native clip
receipts. It never launches a game, writes to the input run, queues a desk job,
sends input or publishes evidence. Use it on a Linden potion/tether run that
kills Thorn; no separate combat run is needed for VUH-2807.

R3 is on main as `f92f5830` (Sage APPROVE `99fae10..8a53e71`). Its two dedicated
live attempts wiped without a kill: runs `20261025-174850` and `20261025-181016`.
Handoff acceptance is **UNQUALIFIED**, not a failed cinematic fix. Both full
installs and all four protected saves were unchanged. Reports and paired wipe
footage are attached to [VUH-2807](https://linear.app/vuhlp/issue/VUH-2807).

## Inventory a finished run

Run Python at BelowNormal. Supply a fresh output directory outside the input
run; existing output is refused. The script can live in Wren's worktree while
reading Linden's retained run read-only:

```powershell
(Get-Process -Id $PID).PriorityClass='BelowNormal'
python -B C:/Users/volpe/repos/kh2-multiplayer/.local/wt-vuh2807/tools/scenario/harvest_thorn_defeat.py `
  --run C:/Users/volpe/repos/kh2-multiplayer/.local/wt-vuh2338-2/build/scenarios/RUN `
  --out C:/Users/volpe/repos/kh2-multiplayer/.local/wt-vuh2807/.local/vuh2807/harvest/RUN-inventory
```

Host/friend PIDs come from `report.json` in their instance order. Logs are found
inside the run or in that run's checkout `build/rig/logs`; pass `--host-log` and
`--friend-log` for an archive with different paths. Use full logs, not bounded
desync tails. A retry/second kill in one input requires separate complete runs.

Output: `receipt.json`, a short `report.md`, and `review-template.json`. Inventory
does no video decoding. It lists candidate clip receipts and decisive native
lines. No kill yields UNQUALIFIED even when the scenario's combat status is FAIL.
After a kill, zero starts is FAIL, including when neither retained end16 nor
independently native315 occurred. One native315 with missing end16 can pass the
start-count check; it cannot establish timing, overlay or rewards by itself.

## Complete the receipt after a kill

Copy/fill `review-template.json` outside the input run. Keep `inputSha256`
unchanged: it binds the exact report, full logs and capture receipt/sidecar bytes.
Changed inputs invalidate the review.

- Set `timing.samePc` only for one-PC capture. Pick host/friend receipt filenames
  relative to the input run and zero-based `firstVisibleFrame` values. These
  must be the **initial visible death camera**, not the later315. Inspect the
  preceding frames and set `precedingFramesNotDefeat=true` only if they are not
  defeat. A capture gap at onset cannot qualify timing.
- For each visual/progression field, enter `PASS`, `FAIL` or `UNQUALIFIED`, plus
  an `evidence` string with exact clip frames/report assertions. `overlay` passes
  only if absent or shown harmless within the first-visible timing bound.
  `oneVictoryEach`, `storyRewards`, `bossCleanup` and `onwardControl` require the
  actual outcomes, not an inferred count. `logsCompleteThroughOnward` requires
  full logs through the end of that observation, so a later second315 is counted.
- Supply `--closure DIR` containing that run's verified `closure.json` and full
  `install-diff.json`. The closure must name `run` and record true
  `fullInstallDiffPass`, `protectedSavesUnchanged`, `ownedProcessesGone` and
  `relayClosed`, backed by the normal install/save/process checks. The tool
  validates that retained receipt; it does not rehash the live install or saves.

Then rerun to a new output after live closure:

```powershell
python -B C:/Users/volpe/repos/kh2-multiplayer/.local/wt-vuh2807/tools/scenario/harvest_thorn_defeat.py `
  --run C:/path/to/RUN --review C:/path/to/filled-review.json `
  --closure C:/path/to/RUN-closure --decode --out C:/path/to/RUN-final
```

`--decode` uses one-thread ffmpeg/ffprobe on just the two marked sources. Strict
decode, exact frame-count matches and no error diagnostics are required, even
on a zero exit. The r3 wipe media exposed ffprobe errors despite a900-frame
count; equality alone cannot certify media integrity. `clip_timing.py` computes
the complete Present-QPC skew interval: <=1000ms passes; a definite miss
fails; missing/corrupt/ambiguous evidence is UNQUALIFIED. Capture bounds do not
measure physical monitor scanout. Preserve raw MP4s, receipts and timing sidecars.

Overall PASS requires every check, including operator-reviewed outcomes. A
scenario pass alone cannot fill those fields. Exit codes:0 PASS,1 FAIL or
UNQUALIFIED (read `status`),2 invalid inputs/refusal. Attach the final receipt and
a short captioned both-screens handoff clip to VUH-2807; the harvester performs no
external writes. It never weakens the scenario's existing assertions.
