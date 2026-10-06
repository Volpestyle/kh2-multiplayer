# Reusable rig helpers

Copied from retained adopted lanes; historical producers and frozen packets remain unchanged. These commands are for the root live-lane owner, not authorization to start a game or input probe. Use `kh2ctl` for game launch/kill and canonical native input. Read the kh2-rig skill and the scenario/save rules first.

## Private Mac relay

`relay.ps1` calls the existing installed Mac producer; it does not install/build/configure one. Run each action in a separate PowerShell invocation: non-fetch actions retain the original `exit` behavior. Supply the actual installed producer, mac-remote script and new local receipt root:

```powershell
powershell -NoProfile -File tools/rig/relay.ps1 -Action status -RunId UNIQUE -RemoteBase /Users/james/.local/kh2coop/vuh1493-two-game-mac-relay-20261005-01 -MacRemoteScript C:/Users/volpe/.claude/skills/mac-remote/mac.ps1 -ReceiptRoot C:/path/to/current/relay-receipts
```

Actions remain `start`, `status`, `stop`, `fetch`. The installed producer enforces its existing ownership, private bind and cap. `fetch` creates a distinct timestamped snapshot; a running snapshot is not a final exit receipt. Existing Git scp and SSH config locations are unchanged. Nothing automatically retries or changes exposure.

After the owned exact-run stop and fetch:

```powershell
python -B tools/rig/verify-relay-closed.py --run-id UNIQUE --ready C:/path/to/ready.json --fetched C:/path/to/fetched/UNIQUE --output C:/path/to/new-closure-directory --ssh-exe "C:/Program Files/Git/usr/bin/ssh.exe" --ssh-config C:/Users/volpe/.ssh/config
```

This reuses the retained remote hash/exit/ps/lsof check, verifies readiness PID/supervisor against fetched metadata and requires all hashes match, both processes absent, both exits0 and UDP27795 free. It does not stop processes. Numeric PID absence is conservative: reuse can cause refusal; it is not a fresh process-lifetime attestation. This helper is specific to the existing Mac relay schema/port/SSH host.

## Physical stand-ins in the owned Session1 bridge

Invoke once through the existing root bridge; no new queue driver is supplied. Use absolute paths. `RehearsalRoot` is the current explicitly owned rehearsal folder, `GameViewRoot` its existing read-only game view, and `ReceiptDirectory` a new/current owned output directory. Historical packages are not implicitly selected.

```powershell
& tools/rig/rehearsal-key.ps1 -RehearsalRoot C:/path/to/rehearsal -GameViewRoot C:/path/to/game-view -ReceiptDirectory C:/path/to/rehearsal/keys -GamePid 123 -PackageRoot 'C:/path/to/rehearsal/Host Fresh/KH2-Co-op' -Key W -HoldMs 180 -EvidenceTag host-w-01
```

The rehearsal10 key set,80–1500ms bound, package ownership/creation/EXE hash, ready receipt, scan codes, foreground/modifier checks and finally key-UP are unchanged. ALT is **not** in this adopted rehearsal10 allowlist; the separate historical ALT probe is not folded into this promotion. Existing tag refusal remains; serialize invocations as before (the inherited existence check/write is not an atomic cross-worker reservation). Focus can be requested by this keyboard helper.

```powershell
& tools/rig/rehearsal-mouse.ps1 -RehearsalRoot C:/path/to/rehearsal -GameViewRoot C:/path/to/game-view -ReceiptDirectory C:/path/to/rehearsal/mouse -PackageRoot 'C:/path/to/rehearsal/Host Fresh/KH2-Co-op' -GamePid 123 -CreationFileTime 123456 -GameHwnd 123456 -SignedDx 8 -SignedDy 0 -DurationMs 0 -EvidenceTag host-mouse-01 -ValidateOnly
```

`-ValidateOnly` is a pure request check, not live readiness. Only an authorized root probe uses `-Execute`. Exact Host Fresh/Guest Fresh package names, one relative event/absolute sum<=32, zero duration,1s observation/send bound, exact HWND/creation, all-key/button refusal and exclusive append-only receipts remain. Mouse never takes focus. The shared-read startup-log fix retains FileShare.ReadWrite, bounded65536 characters and disposal. Successful SendInput is not proof of camera movement; inspect pixels. Keep the source/method frozen during a persistent bridge session because native Add-Type classes are cached.

## Read-only local safety inventory

```powershell
& tools/rig/check-safety.ps1 -Tag closed -RepositoryRoot C:/path/to/repo -GameDirectory 'C:/path/to/actual KH2 install' -ProtectedSavesBaseline C:/path/to/protected-saves-before.json -ForeignBaseline C:/path/to/foreign-before.json -GameDirectoryBaseline C:/path/to/game-directory-before.json -GameAssetsBaseline C:/path/to/game-assets-before.json -ReceiptPath C:/path/to/new-safety-closed.json
```

Baseline formats are unchanged: save rows `{path,sha256}` with absolute paths; foreign rows with repo-relative paths; root rows `{name,bytes,lastWriteUtc}`; asset rows `{target,path,bytes,lastWriteUtc}`. Root must supply its complete pre-run baselines and a fresh receipt filename. No baseline capture/replacement or save write is provided. This inventories processes but, as before, **does not fail solely because a process exists**; root separately reconciles owned closure and leaves foreign processes alone. Asset size/mtime comparisons are not content hashes. Existing receipt write semantics are unchanged; do not reuse a receipt path.

Lane-specific `prepare.py`/`finalize.py`, acceptance predicates, fixed scenario/PID/fixture pin policies, normalized relay readiness producers and `send-keyNN.py` bridge queues are deliberately not generalized. Their original frozen packets remain authoritative for old runs. New packets pin the canonical helper bytes they actually consume.
