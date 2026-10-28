# VUH-2913: camera custody read sweep (Sorrel, round 4)

Product commit `8a1d158` extends `aef68f221b3b07e3fdc8da3a56702007e30749ee`.
The checkout initially matched `origin/main` `e30ca46`; when main advanced,
`be63def` merged current main `d430634` without rewriting history. Protocol stays 40.
All eleven compiled camera inputs, including the extracted actual detours, are
byte-identical across the merge (`merge-input-check.json` in the raw archive).
This is Mac adapter/detour evidence for review, not a Windows build or live acceptance.

Known-ours may deliver to the captured role, known-not-ours stays native, and
unread is withheld/dropped without clearing custody. The handoff survives executor
retirement and host reset; each unfinished D303 keeps its original authority and
600-frame lease, including when a later callback starts. Only an identified
D303 white-in or lease expiry retires its binding. Scope/role failures cannot
replace the resource/connection captured at handoff.

| Site | Control and expected behavior |
| --- | --- |
| 1. VM pointer/source | Fault either read at admission or during a fade; withhold, recover, complete on the original owner. |
| 2. Axel handle word | Fault at admission and at start/watch; custody survives recovery. |
| 3. Owner scope | Unread at adoption or white-in cannot lose binding or prove completion. |
| 4. Unread white-in | Interleave another Axel thread while adoption is pending; preserve custody for D303. |
| 5. Lease | Last frame stays withheld despite unread scope; next frame expires without needing another fade call. |
| 6. `SmActorOf` path | Fault object resolver, actor handle, object entry or object ID; no native start/watch. |
| 7. Detour arguments/thread | Fault copy or game-thread observation for start/watch/fades/reset and 2/20; no native bypass or unproven completion. |
| 8. Executor end/reset | Late start/watch stay withheld. Overlapping D303s keep separate bindings, including callbacks sharing a continuing wire role. |
| 9. `FriendRole` null at adoption | Recover the role captured at handoff; never bind a replacement. |
| 10. Camera name | Unread resolver/bytes on the first offer reserve custody; known invalid names and send refusals keep all-native behavior. |

Every site has a failing assertion on `aef68f2`. The real detours are extracted
from the selected source tree; the native functions, memory and OS boundaries
are stand-ins. The copy-fault switch models a caught fault and does not execute
Windows SEH. The exported baseline uses the same fault switches and probe main.
Ione's saved sources are read without modification; their mains are regenerated
with the selected adapter prefix and actual detours.

Reproduce the focused sweep (all builds/checks use `clankie heavy --`):

```sh
clankie heavy -- python3 tests/run_axel_camera_read_probes.py --out .local/sorrel/recheck
```

`--root <exported-tree>` checks another revision. `--ione-r3 <saved-r3-directory>`
also adapts Ione's two r3 mains. Her six identity-read and two pending-retirement
failures are reproduced separately on the baseline. The r2 replacement control
is adapted to require native execution **and retained custody**, following this
round's retirement rule.

Observed checks:

- The read sweep passes **40/40**; `aef68f2` passes 5 and fails **35**, covering all ten sites.
- Ione's r3 identity main passes **18/18** (baseline: six failures); her retirement main passes **9/9**
  (baseline: two failures).
- Strict Mac suites: ScriptMirror **355/355**, Axel rules **138/138**, adapter **154/154**.
- **119/119** valid single-change reverts fail assertions; every mutant compiles all three suites. This retains
  the original executor-cache reset control as well as the new independent-custody control. One completion
  substitution initially left an unused local; it was corrected and rerun. Compiler failures never count as kills.
- Saved r1 counterexamples **8/8**, actual installer **11/11**, r2 non-detour **6/6**, actual detour **9/9**.
- Unchanged protocol-pinning checks pass: party intent **31**, prologue seats **151**, revive **119**,
  mission mirror **296**, combat admission net **32**; enemy record binding exits 0.
- Existing Mac limits remain: Avatar/HudNames/NativePartyHud are Windows-only; resync fails to build here
  (`-Wkeyword-macro`); StruggleOrbs has its same seven Mac failures.

The focused checks were rerun after merging current main and still pass.

The immutable raw archive is linked from [evidence.json](evidence.json).
The archive includes baseline/head probe sources and outputs, CMake logs,
revert sources and results, and SHA-256 input pins. No PC/game access occurred.

Remaining acceptance belongs to the lead: Ione's re-review, Kit's Windows
rebuild/SEH coverage, Sable's live watch interruption and load/Continue restore
controls. Delivery after the original role ends remains incomplete and counted
as a drop. The existing lease permits native execution after 600 frames;
isolation for a delayed D303 beyond that bound is not claimed.
