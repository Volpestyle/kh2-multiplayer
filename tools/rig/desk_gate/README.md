# Gated desk agent (VUH-2339)

The desk driver for a PC that is also someone's everyday desktop, first on `bld` (PC2). It keeps the
`C:\desk` queue protocol from the windows-pc skill: stage a job, move it into `jobs\`, wait for
`done\<job>.out`. Each admitted job's `.ps1` stays in `jobs\` while it runs. Two things are new:

- **Admission.** A job starts only when the gate admits it. A refused job finishes at once with
  `REFUSED gate: <reasons>` in its `.out`, so nothing sits in the queue waiting for the person to leave.
  Poll `C:\desk\gate\state.json` (it refreshes every 2 s) and queue a job once its level is clear.
- **Reclaim.** The job runs as a child process in a Windows job object while the agent keeps watching.
  The job object also holds every process the job starts later. If a person touches the desk, the agent:
  1. ends the job object;
  2. closes every owned game in the configured rig roots through `kh2ctl kill --all`;
  3. stops any leftover process running from a rig root's `build\`;
  4. verifies that all of them exited, then releases the lease.

  The job's `.out` starts with `RECLAIMED gate: <reason>`. A timed receipt goes to `gate\reclaims\`, with
  `ms` from the input to each stage and `withinBudget` against the 2 s requirement.

  A launch-level job sends no OS input: its runs drive pad input through the DLL's mailbox. Any
  injected input during one therefore also reclaims it (`injected-input`). The job object is
  kill-on-close, so if the agent dies, the job's tree dies with it.

  A desk-level job never passed the launch checks: if an owned game or anything from the KH2 install
  appears while it runs, it is reclaimed (`desk-job-launched`).

  After a launch-level reclaim, or after a launch job that exits non-zero, `gate\needs-verify.json`
  refuses further launches. The killed run never
  reached its closing manifest and save checks, so someone has to compare the install and saves and
  then delete the file. If the cleanup can't be verified, the lease is kept, and with it every later job is
  refused until someone inspects the receipt and deletes `gate\lease.json`.

The first line of a job picks its level. `#gate: desk` is for desktop-only work such as a screenshot.
Any other first line means `launch`, the default.

| Refusal | Level | Means |
|---|---|---|
| `recent-input` | both | Less than `idleMinutes` (15) since the last possible person. That covers physical or unattributed input, injected input that wasn't the rig's own, and any moment the context could have hidden someone: another session, the lock screen, a lost hook, or a sleep or stall of more than 5 s. The interval also restarts whenever the agent starts. |
| `locked` | both | The input desktop isn't `Default` (lock screen or secure desktop). |
| `session` | both | The agent isn't in the active console session. |
| `observer` | both | The low-level hooks aren't installed. |
| `fullscreen` | both | `SHQueryUserNotificationState` reports busy, D3D fullscreen or presentation mode. |
| `foreign-game` | both | A game process with a window that the rig doesn't own (same patterns as `desk-scenario.ps1`). |
| `stopped` | both | `C:\desk\gate\stop` exists. Creating it also reclaims a running job. |
| `lease-held` | both | A job is running, or a retained lease is waiting for inspection. |
| `owned-unreadable` | both | A rig root's `owned.txt` couldn't be read. |
| `kh2-running` | launch | Something is running from the KH2 install (the game or the official launcher), or an owned game is still up. |
| `steam-account` | launch | Steam isn't running signed in as `steamAccountId`. The agent never starts Steam or signs anyone in. |
| `lane-lock` | launch | One of the `laneLocks` files exists. |
| `disk` | launch | Less than 10 GB is free on `C:`. |
| `needs-verify` | launch | A launch job was reclaimed and its install and save checks haven't been redone. |
| `gate-error` | both | A check threw an exception. The agent fails closed and logs the error. |

**What counts as a person.** Low-level keyboard and mouse hooks see every event along with its injected
flag, so the agent's own input (`lib.ps1`'s `Click`, `TypeText`, `SendKeys`, `SetCursorPos`) never
counts. Two other things count as physical:
- input that `GetLastInputInfo` reports but the hooks didn't see, as happens after a removed hook or on
  the secure desktop;
- a changed XInput packet number on a connected pad.

The pad check reads buttons, triggers and sticks past the XInput deadzone, so a drifting stick doesn't
count. Only XInput pads are seen, not DualSense or Switch HID pads.

Steam Remote Play and other remote tools deliver their input as injected. Before admission, injected
input that the rig didn't send therefore counts as a person. During a launch job, any injected input
does. During a desk job, injected input can't be told apart from the job's own, so a person using a
remote tool then goes unseen until the job ends.

Only times are kept: never keys, text, pointer positions, window titles or screenshots.

**Availability log.** Once a minute the agent appends one line to
`gate\availability\<utc-date>.jsonl`, with the time, idle minutes floored, whether each level would be
admitted, and the refusal categories. This is the coarse log that the 48-hour schedule proposal is built
from.

## Install and remove

Copy `agent.ps1`, `Gate.cs`, `install.ps1`, the windows-pc skill's `desktop/lib.ps1` and the PC's
config (renamed `config.json`) to `C:\desk\gate\incoming\`. Then, over SSH as the console user:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File C:\desk\gate\incoming\install.ps1 -Action install
powershell -NoProfile -ExecutionPolicy Bypass -File C:\desk\gate\incoming\install.ps1 -Action uninstall
```

`install` refuses while a lease is held, or if Explorer belongs to anyone but the SSH user. It then:
1. compiles `Gate.cs` as a check;
2. exports the existing `desk-agent` task to `gate\desk-agent-before-<utc>.xml`;
3. stops only the `C:\desk\agent.ps1` process;
4. registers `desk-agent` for the user: AtLogOn, Interactive, Limited (not elevated), no time limit,
   IgnoreNew, restart on failure.

`uninstall` first creates the stop file and waits for any running job to be reclaimed, so no job is
ever left without its watchdog. It then unregisters the task and removes the gate code. `C:\desk\gate` keeps the logs, receipts and
exported definitions. To restore an exported definition:
`Register-ScheduledTask -TaskName desk-agent -Xml (Get-Content -Raw <xml>) -Force`.

Don't run the windows-pc skill's `desk.sh install` against a PC that uses this agent: it would replace it
with the ungated one.

## Profiles

| Config | Machine | Idle | Reclaim on |
|---|---|---|---|
| `config.bld.json` | `bld`, someone's everyday desktop | 15 min of physical idle | physical or foreign injected input; a foreign game or fullscreen app for 3 s; stop file, session or observer loss |
| `config.pc1.json` | PC1 (`supedupsilly`), James's gaming PC | none (`idleMinutes` 0) | a foreign game or fullscreen app for 10 s; stop file, session or observer loss. Input doesn't reclaim (`reclaimOnInput` false). |

**PC1 profile.** PC1 runs on pad input while James works there, but never while he games. The lead
read the AC "the same gate guards PC1 launches" this way on 2026-10-23. Setting `idleMinutes` 15 and
`reclaimOnInput` true switches PC1 to the bld behaviour.

Two settings differ on PC1 for the same reason: other agents run tests from their own worktrees on PC1.
- `helperSweep` is false, so the job object and `owned.txt` alone say what is ours.
- Rig roots may use wildcards (`.local\wt-*`).

The 10 s foreign window is there so `desk-scenario.ps1`'s own 5 s reclaim stops a run first, and its
wrapper still runs its closing install and save checks.

On PC1, start build and other no-launch jobs with `#gate: desk`. Launch level also needs Steam signed in
as account A.
