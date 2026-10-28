# VUH-2913: preserve proven native D303 identity (Sorrel, round 6)

Product `c06a26b` extends `544a40b` on `vuh-2913-owner-camera`. Current main
`c615c7d` is merged without rewriting history. All pinned camera compilation
inputs are byte-identical across that merge (`merge-input-proof.json` in the archive). Protocol stays 40. Mac only, no PC/game access.

R5 retained an unread earlier Owner D303 but discarded a later positively Host
D303's readable identity. The older ambiguous matcher then suppressed the later
Host fades/reset. `SmAxelThreadStarted` now keeps a separate native proof after a
Host decision: known D303 label, Axel identity/source, node and decoded thread
incarnation. Each native op must identify the current thread, source and handle
again. A failed observation cannot supply this proof. It does not clear Owner
custody or depend on current executor, scope or role identity.

The native proof has its own original 600-frame lease and identified white-in
completion. Duplicate observations cannot renew it. Hooks also observe native
proofs without an Owner candidate so completion can retire them. A conflicting
Owner node/thread handle takes precedence at admission and matching.

`AxelCameraPendingSequenceProbe.inl` extends the actual-detour matrix with the
older node header unread throughout later Host operations. It covers continuing/
replaced roles, refused send/invalid-name fallback, and comparisons with no older
D303. It then recovers the older header and completes the Owner independently.
Further controls fault current-thread/source/handle reads, replace the handle
incarnation, retire the executor, fault admission reads, contradict an Owner
identity, and check native-proof completion and lease boundaries.

| Older pending D303 | Role | First-start fallback | R5 native out/reset/in | R6 native out/reset/in | Friend records |
| --- | --- | --- | --- | --- | --- |
| present | continuing | refused send | `0/0/0` | `1/1/1` | 0 |
| present | continuing | invalid name | `0/0/0` | `1/1/1` | 0 |
| present | replaced | refused send | `0/0/0` | `1/1/1` | 0 |
| present | replaced | invalid name | `0/0/0` | `1/1/1` | 0 |
| absent (four controls) | either | either | `1/1/1` | `1/1/1` | 0 |

The original ten-site sweep remains in force. Its baseline failures on `aef68f2`
are preserved in the [r4 archive](../vuh2913-owner-camera-r4/evidence.json);
r6 reruns the same assertions on the authored source.

| Read site | Required outcome tested |
| --- | --- |
| VM pointer/source | Unread adoption/matching withholds; recovery completes on original Owner. R6 also faults native proof reads. |
| Axel handle word | Unread start/watch/adoption retains custody; R6 denies native admission on unread handle. |
| Adoption scope | Unread scope cannot discard Owner binding or borrow a replacement role. |
| Unread white-in | Does not complete a pending Owner; R6 also preserves native identity on unread white-in arguments. |
| Lease expiry | Inclusive last frame, expiry on frame tick; r6 duplicate native observations do not renew the original lease. |
| `SmActorOf` chain | Unread object/actor/entry/ID cannot restore native start/watch. |
| `SmCopyArgs` / `SmGameThread` | Unread callback/detour inputs withhold candidates and cannot authorize native identity. |
| Executor-end reset | Owner binding survives; r6 proven Host thread survives independently without a global Host bypass. |
| `FriendRole` null | Original resource/connection retained; replacement role cannot receive older ops. |
| Camera name | Unread reserves custody; known invalid first name and refused send stay Host, including the pending-older matrix. |

Fresh checks, all through `clankie heavy --`:

- Authored pending sequence **108/108**. Exported `544a40b` fails **11** and passes **97**:
  four routing, four executor/scope-proof and three native-proof lifecycle assertions.
- Ione's exact pending-header matrix: baseline **four routing failures, 32 controls pass**;
  regenerated on r6 **36/36**. Her saved r4 main passes **24/24**.
- Existing read sweep **40/40**, cross-sequence **36/36**, r3 identity **18/18**, retirement **9/9**.
- Saved r1/r2 counterexamples/installer/unknown/actual-detour probes **8/11/6/9**.
- Strict CMake rules **138/138**, adapter **154/154**, ScriptMirror **355/355**.
- **136/136 single-change reverts caught**, with all five suites compiled for every mutant.
  Compiler failures are never counted as kills. The added reverts cover discarded Host provenance,
  a global Host bypass, incarnation reads/replacement, admission, conflicting Owner identities,
  completion, original lease expiry and duplicate renewal.

The immutable raw archive is linked from [evidence.json](evidence.json).
All probes extract the actual selected detours, with memory/native/OS stand-ins;
Windows SEH itself is not executed. Reproduce via:

```sh
clankie heavy -- python3 tests/run_axel_camera_read_probes.py --out .local/sorrel-r6/recheck
```

`--root <exported-tree>` uses the same mains against another revision;
`--ione-r3 <saved-r3-directory>` adds Ione's saved identity and retirement mains.
The archive includes pinned sources, generated baseline/head probes and logs,
Ione's regenerated r4/r5 mains, CMake suite logs, and every revert definition/result.

Ione re-review and Kit Windows/SEH rebuild remain open. Native-watch interruption,
load/Continue restoration and delivery after role end retain the existing
[r4 acceptance limits](../vuh2913-owner-camera-r4/README.md). Isolation beyond
the existing 600-frame lease is not claimed. No landing or issue closure.
