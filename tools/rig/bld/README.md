# bld as a second KH2 test rig (VUH-2339)

Runs PC1's governed single-PC scenarios (the VUH-2617 bisect replays, route tests) on `bld` (PC2) in
parallel with PC1. Builds stay on PC1, and `bld` runs PC1's products unchanged.

**The neutral path.** The dev `kh2ctl` compiles its worktree path in (`KH2COOP_SOURCE_DIR`). It keeps
`build/rig/owned.txt`, its logs and the staged DLL under that path, so a copied PC1 build would look
under `C:\Users\volpe\...` on `bld`. Both PCs therefore use `C:\kh2rig\wt`:

| | PC1 (`supedupsilly`) | `bld` |
|---|---|---|
| `C:\kh2rig\wt` | detached worktree of `C:\Users\volpe\repos\kh2-multiplayer` | checkout fetched from a git bundle (no GitHub credentials on `bld`) |
| `C:\kh2rig\ops` | `build.py`, `build-job.ps1`, `logs\` | `live.ps1`, `stage.ps1`, `desk-scenario.ps1` (copies from this folder and `tools/rig/`) |
| `C:\kh2rig\products\<sha>\` | written by `build.py`: the five products plus `products.json` | the same folder, copied from PC1 through the Mac |
| other | `C:\kh2rig\deps` (FetchContent sources: enet v1.3.17, minhook) | `C:\kh2rig\bin\ffmpeg.exe` (8.0.1, same as PC1's) |

SSH on both PCs runs elevated, so a folder created over SSH is owned by `BUILTIN\Administrators`. git's
safe.directory check then refuses the non-elevated desk session (`git rev-parse` exits 128; that cost
the first PC1 build on 2026-10-23). After creating anything under `C:\kh2rig` over SSH, run
`icacls C:\kh2rig /setowner <desk user> /T`.

`bld` also has Git (machine-wide), Python 3.11.9 (per user) and Pillow 12.0.0, the same versions as
PC1. The VC++ runtime (14.51.36247) matches too.

## One commit, end to end

1. **Build on PC1.** Check out the commit in PC1's `C:\kh2rig\wt`. The PC1 desk owner queues
   `& 'C:/kh2rig/ops/build-job.ps1' -Commit <sha>` in the PC1 FIFO between live runs. It refuses while
   the canonical `rig.lock` exists, refuses while a foreign game is open, and holds `ctest.lock` while it
   builds. Never run it over SSH: a build during a PC1 live run skews that run.
2. **Ship.** Copy `C:\kh2rig\products\<sha>\` from PC1 to the same path on `bld`. If `bld` lacks the
   commit, send a new bundle (`git bundle create x.bundle origin/main`, then
   `git fetch x.bundle refs/remotes/origin/main:refs/remotes/origin/main`). Check out the commit, place
   any untracked files the run needs (for example `.local/bot01-replay/`), and run
   `powershell -File C:/kh2rig/ops/stage.ps1 -Commit <sha>`.
3. **Validate offline:** `powershell -File C:/kh2rig/ops/live.ps1 -ValidateOnly -Attempt x`. It checks the
   checkout, products and sources, and runs the scenario's `--validate`.
4. **Run.** Watch `C:\desk\gate\state.json` until launch is admissible (no refusals), then queue a one-line
   job through the gated desk agent (`tools/rig/desk_gate/`):
   `& 'C:/kh2rig/ops/live.ps1' -Attempt <name>`.

   `live.ps1` hashes account B's saves before and after the run, and takes the full-install manifest
   before and after in a `finally`. Evidence goes to `C:\kh2rig\wt\.local\evidence\<lane>\<attempt>\`,
   with `terminal.json` as the verdict.

   If the post-boss FMV rewrites a game file's mtime with identical bytes, `check-safety.ps1` flags it.
   `live.ps1` records that as a note when the file lists and sizes are unchanged and the sha256 diff
   matched. Any content change stops further runs.

   **Run without clips on bld.** Two KH2 instances recording 30 fps clips during a fight drop bld (RTX 3080)
   to 17–40 fps, and the replay scores in the weak band (bld01: 0.19 HP/s). With clips removed it holds
   60 fps and matches PC1's strong band (bld02: 1.10 HP/s and a kill; `docs/testing/vuh2339-bld02-noclip-20261024/`).
   Pass a scenario without `clip`/`clip_wait` steps or fight `clips`, with `-Scenario` and `-Report`.

5. **After a reclaim.** The gate killed `live.ps1` before its closing checks and wrote
   `C:\desk\gate\needs-verify.json`, so launches are refused. Over SSH, run
   `powershell -File C:/kh2rig/ops/verify-after.ps1 -Attempt <name>`. It repeats the closing checks against
   that attempt's own baselines and adds `verify-after-<utc>.json` to its evidence. When it passes, add
   `-Clear -MarkerUtc <the marker's utc>` to delete the marker. It never clears an `exit N` marker (a
   failed run): those go to the lead.

## Saves

Account B's `KHIIFM_WW.png` on `bld` is byte-identical to the fleet's 2026-10-07 conversion of PC1's
container (`069eeb52…`), and PC1's container hasn't changed since. So FM-00 and the other rehearsal saves
load from the same save-list cursor on both PCs. Nothing is imported or written; SaveGuard and the before
and after hashes stay on.

## What `bld` still needs from a person

The gate refuses launches unless Steam is already running and signed in as B, and nothing here starts
Steam. The reclaim test needs someone at the desk to touch the mouse or keyboard during a run.
