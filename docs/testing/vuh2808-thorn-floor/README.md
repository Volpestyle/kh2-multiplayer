# VUH-2808: Thorn floor investigation and tracing candidate

The zr103 archive establishes that the host's friend actor is already below the
arena before the visual-bind refusal and motion fallback. It does **not**
establish which native movement/collision operation first allowed that crossing.
This change prepares the next run's tracing and adds the floor regression; it
does not claim a recovery fix. AC1 needs the next trace, AC2 remains open, and
AC3 has an offline archive control.

## Retained evidence

Read the issue's full four-comment thread, including
[Tamsin's interim](https://linear.app/vuhlp/issue/VUH-2808#comment-ce82c6c0-2658-4760-b5ed-c9e19002d8ee) and
[the pause/handoff](https://linear.app/vuhlp/issue/VUH-2808#comment-992825e4-fcef-43cf-b2d4-6456dab08d1e).
Read PC1's authorized handoff and zr103 logs/pilots over SSH/plain SFTP scp into
this worktree's `.local/zr103/`. PC1 was not modified. Raw inject logs and both
pilot streams are retained through [evidence.json](evidence.json).

Original run:
`20261025-035211_net_twilight_thorn_bots_dmgtrace600s254b_trace_react_1`,
on `5c0a85e`. Host log: `kh2coop_inject_176172.log`; friend:
`kh2coop_inject_2608.log`. Line numbers below refer to those unchanged files.

| Host line / frame | Observation | Implication |
|---|---|---|
| 9995 / 19709 | Clone `(282,-181,388)`, motion 16 | Last sampled airborne pose before the role. +Y is down. |
| 10000–10006 / 19712 | Friend role 2147483658 begins; capture and queries qualify with matching D48 | Host native coroutine owns the friend actor. |
| 10020 / 19739 | Clone `(81,70,430)`, motion **252** | Below floor while the capture motion is still present. |
| 10032 / 19769 | Clone `(134,445,441)`, motion **252** | Descent precedes bank fallback. |
| 10034–10036 / 19779 | Motion-end query; native cancel returns with `postD48=0` | Stored role thread no longer equals actor D48. |
| 10037–10039 / 19779 | `role-not-current`, then motion 252 absent from clone's banks, fallback 0 | Consequences of the ended coroutine; cannot explain the earlier crossing. |
| 10044–10045 / 19780 | `coroutine-replaced`, observed D48=0; release | Matches the failed current-role predicate. |

Friend lines 10151–10185 admit the same role and release it at local frame
14995. Its local sample at frame 14969 / approximate mirror cursor 19747 is
`(81,236,438)`, motion 252. Cursor is the boss mirror's sampled display cursor,
not the source frame of the separate actor-owner op4 stream.

Comparison role **2147483650** begins at host frame 17270 and is still current
at retirement frame 17373: D48 equals its admitted thread. It retires because
the host's selected seat actor becomes zero (`host-selected-actor`, L8566),
then cleanup cancels its coroutine. **Neither inject log contains a visual-bound
line for this role.** The premise that this earlier role needed/built a visual
binding is not supported by zr103. It uses motion 257, not the later role's 252.

## Code path

In [ScriptActorOwner.inl](../../../inject/src/ScriptActorOwner.inl):

1. Host `SmOwnerHook` associates its selected native actor/coroutine with the
   owner seat. `OwnsPuppet` reserves that host actor while the role is current.
2. `OwnerPostUpdate` reads the host actor's post-native XYZ/facing and emits
   actor-owner op4. It also emits motion/time with op8.
3. Client `SmApplyOwner` admits the role and caches op4's four floats in
   `role.position`. For the actual owning seat, `OwnerPostUpdate` copies all
   XYZ/facing to the canonical local actor after native update and clears
   velocity, carried displacement and the acceleration block. **There is no
   floor query/check or Y exclusion.** Authority is host-owned.
4. [EntityHook.cpp](../../../inject/src/EntityHook.cpp)'s post-update branch
   skips generic puppet transform and `VisualPostUpdate` while `OwnsPuppet`
   is true. Spectator visual binding is not required to drive the host's
   native command actor. Once D48 is cancelled, that reservation fails and
   generic playback can try binding the still-recorded, now-stale role.
5. `SmOwnerCurrent` checks D48 against the stored thread before other role
   predicates. Thus L10036's D48=0 explains L10037. The diagnostic now includes
   this predicate's reason in the existing refused-bind row, with no repeat
   native read and no change to admission.

Generic puppet placement sets actor `+18C` bit `0x40` to bypass terrain collision,
unless `KeepNativeCollision` preserves native collision. It saves the preexisting
bit. A current host role skips that maintenance. Whether the bad role inherited
that bit, whether native script movement disabled terrain independently, and
whether grounding was restored at exit remain **hypotheses**. Motion 0 fallback
is later than the observed failure and is not a causal fix target on this evidence.

## Next authorized Thorn run

After Ione's full independent review and the owner's separate PC build/live OK,
set **`KH2COOP_THORN_FLOOR_TRACE=1` on both game processes at boot**, alongside
the existing hit/lifecycle tracing. Batch with VUH-2921 as the lead planned.
No launch, PC build, or desk job is authorized by this document.

`[thorn-floor]` samples every actor-update boundary during **friend-owned object
307 roles**, plus 120 frames after the last such role, only in TT 02/22 (decimal
2/34). This covers the analogous 19712–19779 interval without relying on old
process frame numbers. The four stages are:

- `pre-native`: immediately before the original actor update;
- `post-native` (or explicitly `native-held`): immediately after it;
- `post-owner`: after owner pose replay/publication and logical-down pinning;
- `post-puppet`: after generic puppet motion/transform and spectator visuals.

Each row names side, native frame, approximate mirror cursor, load/transition,
updating actor identity, role/seat/connection/epoch, expected and observed D48,
cached role XYZ, current XYZ/motion, collision flags, native move/airborne state,
entity vertical velocity, actor velocity/carried displacement/acceleration block,
capture pointer, and the driver's saved collision bit/stream pose. Cached role
metadata never grants authority or causes a new source/thread pointer follow.
Reads reuse the existing `ThornHitTrace.hpp` per-update admission, including the
clone's current binding/metadata proof. Observation faults cannot skip product
work. The process emits at most 65,536 rows, then one explicit saturation row.

`reads` bits 0–9 respectively qualify collision, move, airborne, entityVy,
velocity vector, carried vector, six-float acceleration block, D48, capture and motion time.
`reads=3FF` means all those reads succeeded; zeros with a missing bit are
unavailable. The six acceleration-block values start at actor `+A48`; they are
the exact block cleared by existing replay, not a new interpretation as XYZ.
Move-state 2 and airborne-sub 0 are native state observations, **not proof a
terrain query ran or found the floor**. The trace does not call a terrain query.

Join rows by side/load/transition/frame/actor and role. Find the first positive
Y while the peer's authoritative actor crosses the arena: a change between
`pre-native` and `post-native` localizes movement to native update; a change
at `post-owner` identifies host-pose replay; `post-puppet` identifies generic
stream/visual placement. A change between frames' final and first samples localizes
movement outside the observed actor update, requiring a narrower native trace.
Inspect collision bit 0x40 and move/airborne transitions
against the preceding good friend-owned role and grounded host control. Follow
the 120-frame exit tail to distinguish a native floor snap from continued fall.
If those observations still cannot qualify the native collision operation,
identify the specific operation before choosing a fix; no speculative Y clamp.

## Regression and checks

[thorn_floor.py](../../../tools/scenario/thorn_floor.py) is called automatically
at scenario closure, after owned pilots stop. It retains `thorn_floor.json` and
adds a verdict to both reports. Either seat's sampled **canonical `playerPos`**
greater than Y=1000 in TT34 field gameplay fails an otherwise passing scenario.
The first crossing and worst sample survive later recovery. Existing scenario
errors/crash status remain intact, with the floor failure also in the report.
Cameras, other rooms, cinematics and loading are excluded. Missing/invalid
observations yield `unavailable`, never a floor pass. Non-Thorn runs are unaffected.
This reports sampled evidence; it does not continuously monitor between samples,
stop a run immediately, or prove an unsampled/missing seat stayed on the floor.

Offline archive command:

```sh
clankie heavy -- python3 -B tools/scenario/thorn_floor.py \
  .local/zr103/host_thorn/pilot.jsonl .local/zr103/friend_thorn/pilot.jsonl \
  --out .local/zr103/thorn_floor.json
```

Archive control: **expected FAIL**, 572 canonical field samples (host290,
friend282). Host max Y=0. Friend first exceeds1000 at pilot line492,
Y=1085.043, about74.9s after fight start; worst Y=87034.672 at line1879.
Output: `.local/zr103/thorn_floor.json`. The seven new floor tests pass,
including either seat falling and later recovering, the threshold/direction,
camera/room/event exclusions, unavailable samples and scenario status retention.

The initial all-target Mac build is unsupported: `kh2coop_factory_return_counter_test`
rejects `__fastcall` under AppleClang's `-Werror`; `kh2coop_field_objects_test`
and `kh2coop_resync_test` use `#define private public` under `-Werror` (also used
by `kh2coop_causal_diagnostic_test`); and
`kh2coop_bridge_test` includes `Windows.h`. These are unchanged baseline targets.
An attempted broad `make -k` sweep was stopped on lead direction (owned wrapper
PID34558, exit130); local warning workarounds were removed. Final validation is
limited to relevant portable targets and scoped ctest.
Mac builds do not compile the Windows inject DLL. Native trace compilation,
live grounding and recovery acceptance require the separately authorized PC run.

With normal warning settings restored, the focused build passes:

```text
Built target kh2coop_thorn_hit_trace_test
Built target kh2coop_script_owner_command_test
Built target kh2coop_script_mirror_test
```

Scoped ctest: **7 passed / 1 unsupported on Mac** (exit8). The three targets
above pass, as do `kh2coop_py_test_thorn_floor`, `kh2coop_py_test_scenario_fight`,
`kh2coop_py_test_scenario_launch` and `kh2coop_py_test_scenario_route_segments`.
The unchanged `kh2coop_py_test_pilot_thorn` fails at import:
`AttributeError: module 'ctypes' has no attribute 'WinDLL'` in the native
route pilot. It was not bypassed or modified. No further full sweep was run.
Commands/output are retained in `.local/vuh2808-focused-build.log`,
`.local/vuh2808-ctest.log` and `build/Testing/Temporary/LastTest.log`.

```sh
clankie heavy -- cmake --build build --parallel 2 --target \
  kh2coop_thorn_hit_trace_test kh2coop_script_owner_command_test kh2coop_script_mirror_test
clankie heavy -- ctest --test-dir build --output-on-failure -j 1 \
  -R '^(kh2coop_thorn_hit_trace_test|kh2coop_script_owner_command_test|kh2coop_script_mirror_test|kh2coop_py_test_(thorn_floor|pilot_thorn|scenario_fight|scenario_launch|scenario_route_segments))$'
```
