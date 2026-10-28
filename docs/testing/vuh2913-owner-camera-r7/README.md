# VUH-2913: native provenance for validated seat-0 callbacks (Sorrel, round 7)

Product `059f802` extends `952e577` on `vuh-2913-owner-camera`. Current
main `6b60e94` is merged without rewriting history. All pinned camera inputs
are byte-identical across the merge (`merge-input-proof.json` in the archive). Protocol stays 40. Mac only; no PC/game access.

R6 observed only friend callbacks, including their refused first sends. Genuine
seat-0 Clear Light runs outside `inFriendCallback`; its native camera start did
not supply a friend camera decision. Its readable D303 thread was therefore
never recorded, and an older unread Owner candidate suppressed its fades/reset.

`SmAxelComplete` now sets separate `inHostCallback` provenance only after the
window, scope, connection and seat actor have passed validation. The 2/20 hook
observes either validated callback type. A seat-0 callback records positive
native D303 identity; friend Owner adoption explicitly requires the friend flag.
Both flags end when the callback returns. A refused completion or an idle host
executor supplies no callback authority. Neither flag bypasses fade matching or
clears Owner custody. Native node/thread/source/incarnation validation, original
lease, white-in retirement and Owner precedence remain unchanged.

`AxelCameraHostSequenceProbe.inl` drives both friend and host Overtaker → Clear
Light through the actual completion path and selected real detours. It includes
Ione's original matrix, plus unread admission/matching, refused validation,
inside-callback Owner isolation, executor retirement, recovery and lease controls.

| Older pending Owner | Old friend role | R6 native out/reset/in | R7 native out/reset/in | Friend records from host D303 |
| --- | --- | --- | --- | --- |
| absent | continuing | `1/1/1` | `1/1/1` | 0 |
| absent | replaced | `1/1/1` | `1/1/1` | 0 |
| present | continuing | `0/0/0` | `1/1/1` | 0 |
| present | replaced | `0/0/0` | `1/1/1` | 0 |

Ione's exact main fails **two routing assertions on exported `952e577`**, with
**28 controls passing**, before the product edit. It passes **30/30** on r7.
The extended authored matrix passes **99/99**; against the same r6 baseline it
fails **15** and passes **84** (routing, callback observation and proof lifetime).
The old node header stays unread through the host ops, then recovers and
completes the original Owner independently.

All ten original read sites retain their controls:

| Site | Required outcome |
| --- | --- |
| VM pointer/source | Unread Owner observations withhold; positive native source is required. |
| Axel handle word | Unread cannot erase custody or authorize native identity. |
| Adoption scope | Unread cannot discard or replace the captured Owner binding. |
| Unread white-in | Cannot complete Owner custody or clear native proof. |
| Lease | Inclusive last frame; frame expiry; no renewal from duplicate/outside-callback observations. |
| `SmActorOf` chain | Unread object/actor/entry/ID cannot restore native start/watch. |
| `SmCopyArgs` / `SmGameThread` | Unread inputs do not authorize native proof; seat-0 observation remains confined to validated callbacks. |
| Executor-end reset | Original Owner and proven native thread survive independently. |
| `FriendRole` null | Original resource/connection retained; no replacement-role delivery. |
| Camera name | Unread reserves custody; known invalid/refused first send keeps its native sequence. |

The adapter's former assertion that host 2/20 must be unobserved is intentionally
updated: it now requires native thread proof and no Owner adoption, preserving
154 checks. This changes only its test main; the probe prefix and generated
passing sources are byte-identical (`adapter-prefix-proof.json` in the archive).

The original `aef68f2` read-site counterexamples remain pinned in the
[r4 evidence](../vuh2913-owner-camera-r4/evidence.json). Verification totals for
this frozen r7 input set, all run through `clankie heavy --`:

- Authored genuine host/friend chain probe **99/99**, Ione's exact main **30/30**.
- Existing read/cross-sequence/pending/r3 probes **40/36/108/18/9**.
- Ione's saved r4/r5 mains **24/36**; saved r1/r2 probes **8/11/6/9**.
- Strict CMake rules **138/138**, adapter **154/154**, ScriptMirror **355/355**.
- Full **143/143 single-change reverts caught**, with all six suites compiled for
  every mutant. Compiler failures are never counted as kills. New reverts remove
  host observation/native custody, authorize before validation, retain callback
  provenance after return, infer it from an idle executor, grant it to friends,
  or add a global host-callback fade bypass.

The immutable raw archive is linked from [evidence.json](evidence.json).

Reproduce the probes through the resource governor:

```sh
clankie heavy -- python3 tests/run_axel_camera_read_probes.py --out .local/sorrel-r7/recheck
```

`--root <exported-tree>` uses the same mains on another revision;
`--ione-r3 <saved-r3-directory>` also regenerates the saved r3 mains. The raw
archive contains pinned inputs, generated baseline/head probes, Ione's exact
host-chain main, suite logs, and every single-change revert definition/result.
Memory/native/OS operations are controlled stand-ins; Windows SEH is not executed.

Ione re-review and Kit's Windows rebuild remain open, behind the lead's PC go.
Native-watch interruption, load/Continue restoration, role-ended delivery and
the existing 600-frame bound retain their [scoped acceptance limits](../vuh2913-owner-camera-r4/README.md).
No landing, issue closure or release is claimed.
