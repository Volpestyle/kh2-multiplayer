# VUH-2808: second live floor trace

**No floor fall reproduced; AC1 and AC2 remain open.** One PC1 single-PC
loopback attempt, `20261028-172728_vuh2808_thorn_floor_trace_02_1`, 2026-10-28.
Scenario FAIL after722.3s, step59 `pilot_wait`: host `cooperative revive
peer-logical-downed exceeded60s`, friend `own-logical-downed exceeded60s`.
This is neither a passing fight nor a native recovery proof.

Source **c615c7dc**, new owned PC worktree `wt-vuh2808-rook-trace02`.
`git diff d430634 c615c7dc -- inject/ common/` is empty. Reused DLL SHA-256:
`60ED2984E03199BEE0B193E1A2ABF3B7B67D01865F9D54EB19926D4985348ABC`.
It was checked before/after copying; the DLL was not rebuilt. The d430634
Windows297/297 proof applies to this unchanged native source.

The development kh2ctl embeds its source root. Only kh2ctl was rebuilt with
lead18 authority, Release/Idle under the canonical rig lock, parallel1 /
CL_MPCount2; the build lock was released before the live run. Its SHA-256 is
`BC9937DED9DCE880A94FB407161ECEDAB2F0BB81CF25638E18819246BE2DEE43`.
The project definitions and executable bytes contain
`C:/Users/volpe/repos/kh2-multiplayer/.local/wt-vuh2808-rook-trace02`.
The initial configure selected absent VS2022 and failed; configure/build then
passed with installed VS2026. No product source change or broad test rerun.

## Trace findings

| Observation | Retained evidence |
|---|---|
| Host rows | 29,960, frames18907–32515, every `reads=3FF` |
| Friend rows | 29,928, frames14361–27970, every `reads=3FF` |
| Bounds | Neither65,536-row cap saturated; maximum Y0 on both streams |
| Friend-owned roles | 33 host starts; all33 role IDs retained on both sides |
| Host clone while current | All7,523 matching nonzero D48/expected-thread rows have NO_COLLIDE0x40 **clear** |
| Owner replay | 1,094 changed friend XYZ values end at retained host-role positions;272 change Y. All match current host sources at three decimals. This is a value join, not packet-sequence proof. |
| Host bind refusals | 21 `role-not-current currentReason=coroutine-replaced` |
| Motion252 controls | Eight current host friend-owned roles contain motion252; none has a positive-Y trace sample |

A new no-fall Reversal control is role**2147483663**, command426. At host
frame21119, the clone changes inside native update from Y0/motion151,
move2/airborne0, to Y-15.906/motion252, move3/airborne1,
entityVy-15.906. Its admitted D48 remains current; post-owner and post-puppet
keep that native pose. The role's minimum Y is-484.537, above the arena.

At frame21186 (host log lines16569–16580):

| Boundary | Y | D48 | Motion |
|---|---:|---|---:|
| pre-native | -484.537 | admitted thread7FF7C66A97E0 |252|
| post-native / post-owner | -484.537 |0 after native lifecycle cancel|252|
| post-puppet | -452.219 |0|6|

The visual bind then refuses coroutine-replaced; the puppet also reports
motion252 absent and fallback0. These occur without a floor fall, as in
trace01. The earlier zr103 crossing precedes those events, so they cannot
explain its initial crossing. The eight motion252 roles are2147483663,2147483725,2147483726,
2147483727,2147483729,2147483738,2147483739 and2147483740.

Collision/move/airborne fields describe state, not proof that a native terrain
query ran or found the platform. Normal above-floor motion cannot establish
out-of-bounds recovery. No corrective position writes or recovery fix were made.
The initial native crossing and the native recovery path remain unproven.

## Floor verdict and clips

Automatic floor closure reports **UNAVAILABLE**, not PASS. It judges1,265
canonical positions (friend663, host602), maximum Y0 on both, no firstFall.
There are47 unclassified host event-camera samples, zero unreadable samples
and zero qualified camera RC samples. Those coverage gaps remain in the result.

The existing Creeper-first s254b gameplay/profile/assertions were retained with
`KH2COOP_THORN_FLOOR_TRACE=1` on both boots. One paired5s/5fps diagnostic near
block02 fits the disk headroom. Both clips contain25 frames, host4.886s and
friend5.044s; every frame was decoded and privately reviewed. Only game pixels
are visible. The clips show Roxas/Thorn moving on the platform, not a floor
failure or full-fight proof. They are separate seat recordings; do not assume
frame alignment. Capture clock receipts and ffprobe metadata are retained.

## Safety closure

Lead17 authorized one attempt after Kit's B1-inter END8287c3d4. Full gate at
22:26:57UTC: idle4467s, C21.92GiB with clip/log budget, no HOLD/needs-verify,
job/lease/rig lock, builder, KH2/PCSX2/foreign game or OBS. UDP27809 was free.
START91d52e78 preceded enqueue22:27:26. One-attempt marker was consumed.

Closure22:42:01UTC; independent process recheck22:42:14UTC:

- Approved comparator **exit0/pass=true/saveCount4**: baseline hashes,
  retained-copy hashes and full byte comparisons match; no extra/missing files.
- Full install capture/diff **exit0/0**, **541/541 unchanged**, no added,
  removed or changed files.
- KH2/PCSX2 and own build processes absent; registry empty, UDP27809 free,
  canonical/worktree locks released, desk queue empty and job in done.
- No needs-verify, tracked tree clean, C21.78GiB. No restore or Steam/James
  process action, no other-lane action and no second attempt.

END[d957c09d](https://linear.app/vuhlp/issue/VUH-2808#comment-d957c09d-dcac-4ede-abfe-939c4e9bf0a3)
released the PC lane before offline analysis. No further PC attempt is authorized.

## Evidence

[Evidence manifest](../evidence.json), entries under `trace02/`: complete inject
logs, both combat pilot streams, report, short raw clips and metadata bundle.
Metadata includes approved-source/native-diff/DLL/helper-root proof, build logs,
STOP/scenario/wrapper/gates, save hash receipts (no save bytes), full install
manifests/diff, closing process proof, trace analysis code/output, floor verdict,
clip clock receipts and ffprobe results. No game assets or save files published.

PC receipts: `build/rig/operator-logs/vuh2808/thorn-trace-02/` and
`.local/vuh2808-thorn-trace02/` in the new worktree. Mac copies use the same
local tag. AC3 checker is landed; AC1/AC2 need causal evidence before a fix.
