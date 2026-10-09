---
name: kh2-rig
description: Capability index for driving the KH2 co-op rig and producing evidence. Covers launching or killing games, screenshots and video clips, scenario runs, the Mac relay, keyboard/mouse stand-ins for a human, safety closure, and publishing media to Linear. Use before any live KH2 run, rehearsal, demo or evidence post in kh2-multiplayer, and whenever about to build a new helper for one of these. It is not the plan of record (docs/ONLINE_COOP_PLAN.md) or the acceptance protocol (AGENTS.md).
---

# kh2-rig

Most rig capabilities already exist. Before writing a new helper, look here, then in `docs/KH2_CONTROL_CLI.md` and `docs/SCENARIOS.md`. When you build something reusable, put it in `tools/` and add a line here. Helpers left only under `build/rig/<lane>/` get forgotten: that's how video capture was "lost" for a day.

## Capabilities

| Need | Use | Notes |
|---|---|---|
| Launch / list / kill games | `kh2ctl launch`, `instances`, `kill --pid N` | The only sanctioned way to start or stop KH2. Ownership is (pid, creation time) in `build/rig/owned.txt`. |
| Screenshot | `kh2ctl capture --pid N [--out x.png]`; scenario step `capture` | In-renderer (hooks `Present`), so it works with the window in the background and contains game pixels only. |
| **Video clip** | `kh2ctl clip --pid N --seconds S --fps 30 [--out x.mp4]`; scenario step `{"do":"clip","instance":i,"seconds":S,"name":...}` | Also in-renderer. It needs `ffmpeg` on PATH. Examples: `build/scenarios/20261002-*_net_soak_10min_1/puppet_view_1.mp4`. |
| Overlay / HUD | `kh2ctl overlay --pid N on` | It's drawn into the backbuffer, so it shows in captures. |
| Scripted runs | `python -B tools/scenario/run.py <scenario.json>` | Steps, assertions, report and captures go to `build/scenarios/<stamp>_<name>_<n>/`. |
| Puppet without network | `avatarctl` (`synth`, `record`/`replay`, `peek`) | Usage is in `tools/avatarctl/main.cpp`. |
| Mac relay (private, Tailscale) | `tools/rig/relay.ps1 -Action start -RunId UNIQUE` (also `status`, `stop`, `fetch`), with required producer/script/receipt paths from `tools/rig/README.md` | Existing installed producer only. Stop, then fetch, by exact run ID. The helper exits its PowerShell host: use separate invocations for subsequent checks. |
| Physical keyboard stand-in | `tools/rig/rehearsal-key.ps1` | Explicit rehearsal/game-view/receipt paths; exact owned process, scan/focus/modifier checks and finally-UP. Rehearsal10 allowlist,80–1500ms; no Alt. |
| Relative mouse stand-in | `tools/rig/rehearsal-mouse.ps1` | Pure `-ValidateOnly`, authorized root `-Execute`; exact owned HWND, one nonzero event with total counts<=32, no focus takeover. Shared-read startup-log fix retained. |
| Local safety inventory | `tools/rig/check-safety.ps1` | Explicit complete pre-run save/foreign/game-root/assets baselines and fresh output path. Process inventory alone does not assert closure. |
| Final private relay receipt | `python -B tools/rig/verify-relay-closed.py ...` | After exact owned stop/fetch: explicit run/ready/fetched/output/SSH paths; hashes, exits, PID/supervisor absence and UDP27795. Never stops anything. |
| Friend package | `tools/packaging/build_friend.py`, `tools/launcher/friend.py` | The portable kh2ctl only allows `launch / instances / kill / overlay`. |

## Gotchas (each cost real attempts)

- **Desktop-job UTF-8 BOM:** read `.ps1` source explicitly as UTF-8 (`utf-8-sig` accepts one BOM), never with the locale default. Emit at most one real BOM (`EF BB BF`); reject duplicate BOMs and the mojibake prefix `C3 AF C2 BB C2 BF` before sealing. Windows PowerShell5 may parse that mojibake as code instead of treating it as a signature. Check raw bytes as well as parser syntax. Preserve a sealed/live packet; repair only the writer or a new revision.

- **Python text output on the rig:** the bridge console is cp1252. A `write_text`, `open` or `print` without `encoding='utf-8'` crashes the runner on characters like U+FFFD (empty-seat fixture-05, b8-06). Profiles must write UTF-8 or copy bytes. The lead also wraps bridge jobs with `$env:PYTHONUTF8="1"`.
- **Bridge job output:** the bridge's `.out` keeps only the job's thrown error, so a native child's stderr is lost ("Fixture failed/refused: 1" with no reason). Have wrappers tee stdout and stderr to a file in the packet.
- **Launcher paths:** a revised packet's `desktop-*.ps1` must point `--packet` and the profile at its own directory. Chosen-AI rev4 was ADOPTed while its launchers still ran rev3. Reviewers check this.
- **Desk-bridge jobs must never prompt:** a job runs inside the hidden Session-1 bridge process. Calling a script with a `Mandatory` parameter but no value makes PowerShell wait for console input, which freezes the bridge and every queued job behind it (2026-10-08 19:42, Items fixture). Always pass every mandatory parameter (`-ExpectedBridgePid 522932`), or run the script as a child `powershell -NonInteractive -File`. Restarting the frozen bridge needs James.
- **Operator logs go outside packets:** a wrapper that tees into `<packet>/...` can overwrite a pinned file and make the packet refuse (pj08, 2026-10-08). Write run logs under `build/rig/operator-logs/<job>/`.
- **Copy products into the packet:** pin copies under `<packet>/products/`, never live `build/` outputs. Any main rebuild rewrites `build/inject/staging/kh2coop_inject.dll` and makes externally pinned packets refuse (world map rev2, 2026-10-08 20:26).
- **Consent dates are UTC:** the two-PC consent gates compare against the UTC date. After about 19:00 Central, use the next calendar date (JOIN10).
- **Two-PC GO:** copy the lead's ADOPT `lead-review.json` to PC2's packet, and verify it there, before sending GO. The PC2 worker correctly refuses a PENDING receipt (JOIN07).
- **Window/desktop recorders:** ffmpeg `gdigrab` records **black** frames from the D3D12 game. Use `kh2ctl clip`. On a package-launched game, the rig recorder can be declared as a rehearsal aid: bind `--pid N` to the launcher's retained owned process and creation time. It writes the mod's capture channel, not gameplay or save memory; it adds no proof of desktop-only gameplay control. Never use auto-selection or capture an unowned game.
- **Synthetic keys:** KH2 reads scan codes. A VK-only `SendInput` (scan 0) does nothing. Set `wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)`, add `KEYEVENTF_EXTENDEDKEY` for arrows, and bring the game to the foreground. `kh2ctl` does this already (`main.cpp` `SendVk`).
- **Combat by stand-in:** one key per process invocation, with deliberation between keys, is too slow, and Sora dies. Live combat should come from scenarios, not hand-driven rehearsal.
- **Lone Alt** can put a window into menu mode. Check pixels before sending more keys, and don't press Escape blindly (it's also pause).
- **Reading the inject log while the game runs:** open it with `FileShare.ReadWrite`. A plain `ReadAllText` fails on the logger's lock.
- **`boot-load-save` is broken.** Load saves through the scenario `boot` step.
- **Cross-account Steam saves:** copying another account's whole `KHIIFM_WW.png` can block startup on a corrupt-save dialog before any Present receipt. With explicit save-handling approval, import raw entries into the target account's own native container, preserving its header, footer and XOR key (Kingdom Save Editor's `PcSaveArchive` format). Work on copies, retain the rejected container, hash originals and every target file, and verify title/capture without loading or saving. PC2 conversion evidence: `.local/pc2/save-conversion-result.md` (2026-10-07); startup passed, save loading remains untested. Do not dismiss the dialog blindly: it announces deletion and new save creation.
- **Staging a packet on PC2** (`ssh pc2`, a Windows standard user):
  - `scp -r <packet> pc2:<lane>/` copies the packet's *contents* into `<lane>` when `<lane>` doesn't exist yet, so `<lane>/<packet>/` is never created. This happened twice on 2026-10-07.
  - Create the lane first with `powershell New-Item -ItemType Directory -Force`; cmd `mkdir` over SSH failed silently.
  - After copying, check that `<lane>/<packet>/pins.json` exists and its sha256 matches.
  - Remote `herdr agent prompt` keeps only the first line, so send instructions as a file in `C:\Users\ccroc\lead-inbox\`. In a shell heredoc, write literal paths instead of `$VARS`.
- **Steam-init boot crash:** KH2 can crash in its own `GetMySteamId` (null Steam interface, about 15 s into boot). Steam then spawns the official "KINGDOM HEARTS HD 1.5+2.5 Launcher". That launcher isn't kh2ctl-owned and blocks the next run: close it normally through the Session 1 bridge (`CloseMainWindow`, guarded by its start time), and don't kill it. Cause (2026-10-08, four Drive smoke attempts): the launch passed `--game-dir` pointing at `kh2-readonly-view-DO-NOT-RECURSE`, which has no `steam_appid.txt`. Launch with kh2ctl's default game directory (the real Steam folder) and pin the exe hash separately, as the enemy fixtures do. Also: a helper that passes `env={...}` with only its own keys replaces the whole environment; always pass `{**os.environ, ...}`.
- **Relay lifetime** is about 12 minutes (720 s). Don't explore routes on the relay clock; work routes out offline first.
- **MP4s can't be decoded until finalized** (the `moov` atom). Stop a short sample before checking it.
- **Clip-helper cleanup:** if a runner-owned `kh2ctl clip` is terminated during encoding, its ffmpeg child can survive. At closure, query children by the retained helper PID, creation time and clip command; stop only verified owned children and confirm their exit. Never kill every ffmpeg process.
- **Clip timing:** `kh2ctl clip` and the scenario `clip` step block while recording and encoding. To film an action, start the canonical clip command as a runner-owned helper before that action, and collect it before any screenshot on the same instance. Each clip is limited to30 seconds. Check finalized early frames for real game motion; retain the reported before/during capture frame rates.

## Safety (unchanged rules, so you don't re-derive them)

- **James's saves:** never write to `OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX/`. Saveguard redirects writes, but treat it as a backstop.
- **Closure checks:** after every live run, check that the four protected saves, the foreign files, the game-root entries and the asset targets are unchanged. Owned PIDs must be gone, the relay stopped and the port free.
- **Private game view:** `kh2-readonly-view-DO-NOT-RECURSE` contains junctions to the real Image/STEAM folders. A recursive delete can delete their real contents. Remove each junction only as a link, never recursively, and verify both targets afterwards.
- **One rig lock:** only live runs are serialized.

Canonical helper argument examples and limits: `tools/rig/README.md`. Pin the promoted helper bytes in new packets; old frozen packets retain their original lane paths and hashes.

## Publishing evidence to Linear

- In lead/advisor setups only the advisor writes to Linear, as the Clankie app. Verify the actor with `linear_get_user me` first.
- **Media:** `linear_prepare_attachment_upload`, then an immediate `curl -X PUT --data-binary @file` with every signed header verbatim (the URL expires in 60 s), then `linear_create_attachment_from_upload`. Embed the asset URL in the comment as `![caption](assetUrl)`.
- **Uncertain call:** reconcile by `receiptId` and a read-only check before any retry.
- **Include a short clip** for notable live results. James expects video, not just stills. Review every frame for private content first: launcher windows show user paths and the relay address.
- **Captions** state only what's visible. If game memory confirmed more than is in frame, say so.
