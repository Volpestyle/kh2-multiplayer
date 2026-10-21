# Safe-field heartbeat recovery regression

Maintained copy of the passing recovery04 fixture, run
`20261021-160822_vuh2628_safe_field_recovery04_1` on build34. The three runtime
helpers and `../scenarios/net_safe_field_recovery.json` retain the exact passing
bytes. Only the offline check's scenario/output paths are adapted.

Recovery04 preserved all539 pinned inputs before landing edits in
`.local/thorn/recovery04-publish/source-inputs.tar` (SHA256
`c5c6fe8cd8d374498993ffa76c879231d70e493106f1ade9d2bbf3ebf44efb3a`). The
original local fixture remains under `.local/thorn/recovery04-fixture/`.
The run passed35 steps and closed its owned processes, relay and wrapper;
four protected saves and twelve movie hashes were unchanged.

From the repository root, these commands are offline only:

```powershell
python -B tools/scenario/safe_field_recovery/check.py
python -B tools/scenario/safe_field_recovery/run.py --validate tools/scenario/scenarios/net_safe_field_recovery.json
```

The check writes `.local/thorn/safe-field-recovery-checks.json`. The thin entry
registers the bounded `friend_heartbeat_pause` test step and delegates ordinary
steps, rig ownership and save checks to the existing scenario runner. Use this
entry for this fixture; the ordinary runner alone does not register the step.

Do not launch as part of offline validation. A future live regression must use
the existing reviewed Session1 desk queue/governor and wrapper, with exclusive
live-lane ownership, exact products and retained processes, protected-save and
movie baselines, and verified owned game/helper/relay closure. Configure that
wrapper to invoke this thin entry. Do not run from Session0 or take desktop
focus. A runner exit alone does not certify complete safety closure.

Scope is single-PC loopback in the full FM00 Serenity tuple
`[2,32,99,0,0,2]`, with each seat's own tutorial flag40 already set. The exact
reviewed dormant tutorial script stays native-owned. The test pauses only the
original retained Friend1 runtime for2600ms, with same-handle watchdog/finally
resume. It requires actual expiry, pinned fresh reconnect, changed-load
two-frame native convergence, matching relay result, new scoped reset1 ACK and
Arm. Paired clips show the recovered field after that handshake. No gameplay,
HP/team/pose, save or tutorial-flag writes are performed.

Other scripts, all-Script tables, mixed combat, active bosses and owned-pause
expiry remain unsupported. This regression does not prove a Thorn kill,
rewards, onward movement, two-PC play or continuous footage of the fault.
