# Safe-field heartbeat recovery regression

Maintained copy of the passing recovery04 fixture, run
`20261021-160822_vuh2628_safe_field_recovery04_1` on build34. The three runtime
helpers and `../scenarios/net_safe_field_recovery.json` retain the passing bytes
except the pause, below. Only the offline check's scenario/output paths are adapted.

**The pause, since VUH-2640.** The event-control lease is 4000 ms (it was 1500 ms
when recovery04 ran its 2600 ms pause). The relay's peer liveness is 5000 ms, and
it counts silence from the last packet it received, not from the suspension (Quill
r1). So the pause has to expire the lease and stay inside the relay's silence, which
is the pre-pause age, plus the actual pause, plus the first packet's delivery. The
budget (`pause.py`, constants checked against the C++ sources) is:

| | ms |
|---|---|
| lease / relay timeout | 4000 / 5000 |
| requested pause (lease + 200 expiry margin) | 4200 |
| pre-pause relay age budget (stated, **not measured**) | 400 |
| measured overrun allowed (suspend to resume) | 200 |
| delivery budget (resume to the relay's next packet) | 100 |
| watchdog (relay − pre-age − delivery) | 4500 |

400 + 4200 + 200 + 100 = 4900 < 5000. The budget is a stated assumption, not a
guarantee. A pause measured outside it is refused (`pause outside qualified duration
(budget)`), and so is a fired watchdog, early or late. The proof is also refused if
the relay log shows `Peer timed out: <the friend's peer>` after the pause, which is
the relay's own drop, as opposed to the friend's retirement (`Peer disconnected`).
That covers a pre-pause age or jitter beyond the budget. A Python Timer is not a
real-time bound, so a late watchdog can only refuse, never pass. No relay timeout is
raised and nothing retries. The 4200 ms pause has **not run live**.

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
original retained Friend1 runtime for 4200 ms (budget above), with same-handle watchdog/finally
resume. It requires actual expiry, pinned fresh reconnect, changed-load
two-frame native convergence, matching relay result, new scoped reset1 ACK and
Arm. Paired clips show the recovered field after that handshake. No gameplay,
HP/team/pose, save or tutorial-flag writes are performed.

Other scripts, all-Script tables, mixed combat, active bosses and owned-pause
expiry remain unsupported. This regression does not prove a Thorn kill,
rewards, onward movement, two-PC play or continuous footage of the fault.

**VUH-2640 Leg A live check.** The same thin entry also runs
`../scenarios/net_twilight_thorn_legA.json`, which is generated from bots05 by
`make_thorn_legA.py` (`--check` verifies it is current). It uses this pause in the
Calling, then carries the friend into Thorn's arena sitting out. See
`docs/FENCED_FRIEND_BOSS_ARENA.md`, "Proof plan". Not yet run.
