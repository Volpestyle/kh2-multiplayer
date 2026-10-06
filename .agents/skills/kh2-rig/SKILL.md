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

- **Window/desktop recorders:** ffmpeg `gdigrab` records **black** frames from the D3D12 game. Use `kh2ctl clip`. On a package-launched game, the rig recorder can be declared as a rehearsal aid: bind `--pid N` to the launcher's retained owned process and creation time. It writes the mod's capture channel, not gameplay or save memory; it adds no proof of desktop-only gameplay control. Never use auto-selection or capture an unowned game.
- **Synthetic keys:** KH2 reads scan codes. A VK-only `SendInput` (scan 0) does nothing. Set `wScan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)`, add `KEYEVENTF_EXTENDEDKEY` for arrows, and bring the game to the foreground. `kh2ctl` does this already (`main.cpp` `SendVk`).
- **Combat by stand-in:** one key per process invocation, with deliberation between keys, is too slow, and Sora dies. Live combat should come from scenarios, not hand-driven rehearsal.
- **Lone Alt** can put a window into menu mode. Check pixels before sending more keys, and don't press Escape blindly (it's also pause).
- **Reading the inject log while the game runs:** open it with `FileShare.ReadWrite`. A plain `ReadAllText` fails on the logger's lock.
- **`boot-load-save` is broken.** Load saves through the scenario `boot` step.
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
