# VUH-2913: current callback authority (Sorrel, round 5)

Product `efef3e5` extends `675a648` on `vuh-2913-owner-camera`, with current main
`d430634` already merged. Protocol remains 40. Mac only; no PC/game access.

R4 kept an earlier Overtaker Owner snapshot, then let a later Clear Light
adopt D303 through literal `Owner` even when its first send was refused and
its start ran natively. The later fades/reset then went to the friend or were
dropped. Retaining the old snapshot did not authorize the new callback.

`SmAxelThreadStarted` now passes the current callback's `cameraCustody` into
`AxelFadeAdopted`. Host and undecided callbacks cannot adopt a new D303; old
unfinished bindings retain their original roles, identities and leases.

`AxelCameraSequenceProbe.inl` drives the actual start → 2/20 → white-out/reset/
white-in detours. Its eight routing cases cover older D303 absent/present ×
continuing/replaced role × send refusal/known-invalid-name fallback. On exported
`675a648`, all eight routing assertions fail and **28 independent controls pass**.
The fix passes **36/36**, including execution and retirement of the older D303
independently of the later Host callback. Native/OS boundaries are stand-ins.

Native counts below are white-out/reset/white-in, each observed through its actual detour.

| Older D303 | Role | First-start fallback | `675a648` native / friend records | R5 native / friend records |
| --- | --- | --- | --- | --- |
| absent | continuing | refused send | `0/0/0` / 3 | `1/1/1` / 0 |
| absent | continuing | invalid name | `0/0/0` / 3 | `1/1/1` / 0 |
| absent | replaced | refused send | `0/0/0` / 0 | `1/1/1` / 0 |
| absent | replaced | invalid name | `0/0/0` / 0 | `1/1/1` / 0 |
| present | continuing | refused send | `0/0/0` / 3 | `1/1/1` / 0 |
| present | continuing | invalid name | `0/0/0` / 3 | `1/1/1` / 0 |
| present | replaced | refused send | `0/0/0` / 0 | `1/1/1` / 0 |
| present | replaced | invalid name | `0/0/0` / 0 | `1/1/1` / 0 |

Other fresh checks through `clankie heavy --`:

- Existing ten-site read sweep **40/40**.
- Ione r3 identity **18/18**, pending retirement **9/9**.
- Ione's r4 cross-sequence main, regenerated on the authored fix: **24/24**.
- Full revert runner **120/120**: all four suites compile for every mutant and at least one assertion fails.
  The added revert restores literal `Owner` and is caught by the cross-sequence matrix; compiler failures are not kills.
- Strict CMake rules **138/138**, adapter **154/154**, ScriptMirror **355/355**.

Reproduce both authored probe mains:

```sh
clankie heavy -- python3 tests/run_axel_camera_read_probes.py --out .local/sorrel-r5/recheck
```

`--root <exported-675a648-tree>` runs the same mains against the baseline.
`--ione-r3 <saved-r3-directory>` also regenerates Ione's r3 mains. Her original
files are read without modification.

The raw archive in [evidence.json](evidence.json) contains generated sources,
baseline/head output, input pins, CMake logs and the complete revert runner/results.

Ione's re-review and Kit's Windows/SEH rebuild remain open. Native-watch
interruption, load/Continue restoration and delivery after role end retain the
[r4 acceptance limits](../vuh2913-owner-camera-r4/README.md). The existing
600-frame lease remains; isolation beyond that bound is not claimed.
