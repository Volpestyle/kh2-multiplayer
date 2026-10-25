# VUH-2807: native315 lost during pause close

Candidate only. No KH2 or desk run was performed for this change. Live acceptance
and landing await the lead's scheduled evidence run and Sage's exact-range review.

## Decisive retained evidence

Read-only input: PC1
`wt-vuh2338-2/build/scenarios/20261025-015953_net_twilight_thorn_bots_dmgtrace600s254b_trace_react_1/`.
Host inject log `kh2coop_inject_147468.log`; friend `kh2coop_inject_185032.log`.
Line numbers below refer to those retained files, not a new run.

| Boundary | Evidence |
|---|---|
| Host native mission end | Host30114: `host end trap=mission_dead_boss arg=4294967295`. |
| Death-camera hold | Host30116–30118: install caller `3FB0E2`, acquire source75627, frame47455. Friend27290–27298: ordinal12 received, native Start, then owned combat pause `menu=8`. |
| Prior release | Host30214–30215: release source75821, frame48032. Friend27299–27301: ordinal13, close Start, then Closing. |
| Host native315 | Host30220–30225: checked `id=353133 flags=12`, raw parameters `2,2,330033,33,1,FFFFFFFF,3,0,0`, acquire source75825, frame48033. |
| Runtime admission | `runtime_1.log:699–700`: sources75821/75825 admitted at ordinals13/14, both `reason=0`. No lease abort at this boundary. |
| First failed boundary | Friend27302: `hold skipped epoch=3 hostRoom=1 ackEpoch=3 ackFlags=3 menu=8 safe=0 holding=1`; 27303 consumes ordinal14/source75825. No friend native315 start exists. |
| Pause closes | Friend27304: OwnerWait (`phase=6`), `menu=255`, but `ordinal=0`: the skipped Acquire cleared the prior release receipt. |
| Timeout | Friend27485: `owner did not converge 10000 ms after release epoch=0; input returned`. It still cannot invent the lost315. |
| Eventual scene | Friend27617 takes room source75857/epoch4; 27629 starts316 (`id=363133`) in02/00/program51. This is the next scene, not a delayed replay of315. |

The ~22s seen in the issue's clip is therefore not a22s delivery timer. The
friend loses315 and waits for the host's next room. The10s timeout only returns
input; it does not start a cinematic. The PAUSE tutorial text is shown by the
generic hold's Start press, rather than a separate tutorial prerequisite.

## Change and verification

`EventHoldNative.cpp` defers FIFO reads only while the hold owns a pause in
CloseRelease/Closing. It still ticks the close and health checks. After the close
is observed, the next callback reads the original command: existing Supersede
retires OwnerWait and existing Replay checks the same fresh safety conditions.
No extra start cache, wire change, new feature flag or native mission4/16 replay.

The owned-memory adapter regression reproduces the actual Release→315 ordering
in both close phases, holds the menu open for several callbacks, then requires
one replay with the full raw descriptor and no second pause. Refusal controls
cover expiry, scope change, partial collector reads, lost convergence, frozen
control, pause blockers, active event/context, not-in-field, foreign menu and
foreign callback. Existing normal/off/abort/unowned/elsewhere cases remain covered.

Review-zip checks include the Release DLL build, focused CTest output, sanitized
adapter output and three mutants: remove deferral; bypass native safety; bypass
convergence. Each mutant must compile and fail its targeted behavioral assertions.
The zip retains the exact commands, source mutations and outputs.

## Scheduled live acceptance

Use matched products and the existing Thorn pilot cohort in
`tools/rig/thorn_pilot/README.md`, with the normal full-install and protected-save
checks. The lead owns scheduling; this candidate does not queue a job.

- Keep the bot kill and paired recordings. A capture gap at315 cannot qualify timing.
- Require exactly one host mission_dead_boss, one host315 publication and one
  friend `client started the host's event tt34-after-thorn-315` / `client plays`
  receipt. A316 room arrival alone is insufficient.
- From the synchronized two-screen clip, measure the first315 cinematic frame
  on each seat; require ~1s or less skew. Distinguish the earlier boss death-camera
  hold from315 playback, and retain any PAUSE overlay with that explanation.
- Check one victory, shared post-boss story/rewards, cleanup and onward movement,
  plus unchanged protected saves and install. Do not relax the cohort's other gates.

Limits: owned-memory checks stub the native event-start call; they prove consumer
ordering and preserved gates, not game rendering or the ~1s live result. MSVC
AddressSanitizer covers the adapter; UndefinedBehaviorSanitizer is unavailable in
this MSVC toolchain. Full suite/live acceptance are separate from the focused checks.
