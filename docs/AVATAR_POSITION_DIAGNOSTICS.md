# Avatar position readback diagnostics (VUH-1508)

The DLL emits bounded `[avatar-position]` receipts for admitted remote puppet targets. This is a local native-readback diagnostic, not a peer position hash or a claim that the rendered scene matches every peer. It adds no resync request, network message, native write, or SAVE exception. Ordinary transform correction remains unchanged with the diagnostic fault opt-in absent.

Each receipt carries a monotonic uptime `GetTickCount64` timestamp in milliseconds for inject-log timeline correlation and identifies actor pointer and native handle, load/transition serials, owner, producer, generation and connection identities, room, target sequence, exact xyz target and observed xyz, Euclidean error, phase, frame and fault streak. Nonfinite coordinates or a contained read failure are explicitly marked `finite=0`; this marker alone does not distinguish those causes. Threshold is greater than 5 world units for three consecutive distinct entity frames; persistent faults repeat at most every 120 frames. Each puppet index has a lifetime cap of 128 fault/recovery receipts shared by between-updates and writeback; informational native corrections have a separate cap of 16. The final receipt in each budget marks `budgetExhausted=1`. Scope changes cannot refill either cap. Sink exceptions are contained and attempted emissions consume their budget even if logging fails.

Phases:

- `between-updates`: sampled immediately before the next original native update, compared with this actor's previous applied target. This catches a later writer between updates without mistaking normal physics inside the original update for a fault.
- `native-correction`: sampled after the native update, before the existing override. This records correction pressure and can occur during normal physics or legitimate target movement; it is not evidence of network desync by itself.
- `writeback`: sampled immediately after the existing xyz stores, using volatile native readback. A persistent mismatch means the write/readback boundary failed.

A quiet sample after a reported fault emits `state=recovered`. Held, inactive, rejected/stale provenance, actor/handle/kit/room/owner or load changes reset continuity. Only consecutive frames compare prior targets. Held poses are still applied by the existing driver, but do not produce position diagnosis. An actor pointer plus handle cannot prove absence of same-address same-handle reuse; room/load and native admission remain the existing lifetime boundary. Read exceptions remain contained by existing SEH and cannot suppress the original update.

Receipts go to the existing inject log. Existing world-desync collection may copy that log when its own trigger fires; position receipts alone do not initiate an all-peer diagnostic bundle. Missing receipts after exhaustion, absent entity updates, rejected admission, stream delay, or a fault wholly between samples do not prove equality. The offline owned-memory controls execute the same read/write/read seam, including deliberately ignored/corrupt writes and later-writer faults; they qualify policy and integration compilation, not live visibility.


## Bounded offline/live diagnostic fault seam (default off)

`KH2COOP_AVATAR_POSITION_FAULT=1` explicitly enables a DLL-owned local mapping,
`Local\KH2Coop_AvatarPositionFault_<pid>`. With the variable absent, no mapping
is created and the ordinary transform stores run unchanged. The DLL refuses an
already-existing mapping, including PID reuse or a precreated mapping.

`avatarctl fault-arm --pid N --activation-target X,Y,Z` reads a fresh DLL offer;
it accepts no actor address. The offer identifies process creation, exact actor
and handle, load/transition, producer provenance, owner, world/room and puppet
index. Admission requires the current genuine native Donald (object 92/type 1)
as Friend1/puppet0, active unheld standalone authority, independently read native
GoA 04/1A, field menu 255, and idle native event/cutscene state. Read failures or
nonfinite native position refuse admission. No other room/menu is qualified.

Separate offer/request seqlocks bracket copies. The CLI atomically claims the
one immutable request slot. Requests cannot renew: malformed input, scope,
control, authority or activation-target changes burn the process-lifetime gate.
A pending request waits at most 10 seconds for the admitted producer target to
match its finite activation target within 0.01 units per axis. It then suppresses
only the three **existing** xyz stores for at most eight distinct consecutive
game frames and at most 1000 ms. Frame gaps or deadlines release immediately;
frame polling also expires a lost producer. Ordinary stores resume on release.
The monitor's read/target/write/read operation is unchanged, so it reports real
writeback and between-update deviations and subsequent ordinary-write recovery.
Rotation, motion, velocity, actors and SAVE receive no additional writes.

`avatarctl synth --world 4 --room 26 --teleport-after-ms 8000 --teleport-delta 40,0,0` optionally moves
the existing producer target once in the same synth lifecycle. Delay is bounded
to 1..60000 ms and finite delta to 100 units per axis. Default synth behavior is
unchanged. A stationary (`--radius 0`) fixture arms before this target change;
it does not rely on synchronizing an eight-frame window with an external process.

`[avatar-position-fault]` arm/firstskip/release receipts carry nonce, monotonic
tick, game frame, full scope, activation and observed position. A qualified
budget release says `reason=budget skipped=8`; the actual drift receipts must
match that scope/window and recovery must follow release. This is a diagnostic
fault injector, not resync behavior or proof of peer convergence. Independent
review and a separately sealed live fixture are required before any game run.

### Producer room contract

Synth accepts paired `--world` and `--room` integer options (0..255, decimal or
base-0 notation). With neither option, its existing stream room remains 0/0.
The fault fixture must explicitly supply `--world 4 --room 26`; the DLL does not
infer or overwrite the producer room from native state. Its independently read
native 04/1A requirement and full immutable request scope remain unchanged.
Claiming GoA in the stream cannot authorize a fault in another native room.

The actual synth helper constructs the pose and standalone provenance used by
cmdSynth. Integration controls pass that helper's output through the same native
PoseScope and FaultBinding helpers into production admission: intended 4/26
passes; legacy 0/0, wrong producer room, wrong independently read native room
and network provenance refuse. This boundary must be tested as a composed path;
separately fabricated compatible producer and native bindings missed the rev4
setup blocker.

## Live qualification (2026-10-08)

Run `build/scenarios/20261008-190806_avatar_position_drift_recovery_1` passed
fixture `build/rig/avatar-position-desync-20261007-01/rev5/live-fixture-04`
in 75.2 s (position smoke 30.11 s), with product code `79bf53d`, DLL `2138157e`
and avatarctl `0e8b4f6a`. The landing on main89579a4 preserves all reviewed
production and test source exactly. Four native normal/ASan executions passed
220 checks, including the actual synth-to-scope-to-binding-to-admission boundary.

The synced standalone GoA puppet baseline emitted zero fault reports. The
explicit default-off fault skipped eight position-store frames beginning at
frame1360. The 40-unit deviation reached streak3 in writeback/native-correction
at frame1362 and between-updates at frame1363. Budget release at frame1368
resumed ordinary stores: writeback recovered with error0 in that frame and the
other phases recovered at frame1369. The qualified between-update lane emitted
one induced report and one recovery report; total output stayed bounded (four
fault/recovery receipts and two informational correction receipts). Final
readback matched the new producer target.

`position-result.json` and `position-log.txt` retain the complete scope, nonce,
readback and receipt timeline. The closure reports canonical exit0, safety PASS
and unchanged disk-save hashes; protected in-memory SAVE checks passed. This
qualifies local native-readback reporting and ordinary-write recovery for this
bounded case. No automatic resync, peer position convergence or automatic
all-peer report collection is qualified: canonical desync collection was NONE.
