# VUH-2881: Struggle orb worker receipts

Source candidate; live acceptance remains with the lead. No game, desk, live,
Steam/install or save action is part of this work.

## What the historical run proves

Read-only input: Juno's
`.local/wt/vuh2466/build/scenarios/20261026-192703_net_prologue_struggle_orbs_1`,
main `bc8d9ad`. The host (`6536`) sent 242 birth rows; the friend (`219608`)
received 242. The final host stats (`:2418`) show `births=242 collects=0
... offThread=41208`; the friend (`:2502`) shows `dropsSuppressed=38
picksSuppressed=0 offThread=44591`. Its final display sample shows
`created=242 vanished=0`. Both seats reached the match result.

The shipped `Gate::Admit` returned before every native read and booking callback
whenever the detour TID differed from `NoteGameThread`'s TID. Therefore all those
foreign entries bypassed mirroring, including client-local suppression. Simply
removing the comparison would call EnemySync's vector/book/outbox mutations and
actor mappings from workers; those APIs are owned by the entity thread.

**Limit:** the old logs contain neither per-hook TIDs nor per-hook foreign
counts. They do not prove which hook produced each foreign entry, nor establish
a specific native worker TID. The native facts/pinned prologues identify the
record argument as R8 for `40FF50`, RDX for `410250`, and the flight transition
as update `+0x10` plus collector `+0x48`; these facts alone cannot prove dynamic
thread affinity. The evidence supports fixing the unsafe affinity assumption,
not inventing a thread identity. Exact selected lines and original hashes are
in `tests/fixtures/struggle_orb_threads/sources.json`; they remain failing controls.

## Change

`OrbTick` publishes an immutable snapshot of the admitted session generation,
delivery, epoch, native transition/load, manager, role and collector identities.
A detour on any thread reads that snapshot. Native birth/pickup code runs once
on its original caller; after normal return the detour copies a birth or a
verified new flight transition into the same FIFO. It never invokes EnemySync
or resolves an actor through live containers on a worker. Unknown collectors
are sent unresolved, so the friend fades the orb rather than guessing.

`Pump` on the noted entity thread drains copied receipts into the existing
host book and emission outbox. The drain treats captured record addresses only
as opaque book keys. Births and collects share ordering across native record
reuse. The mailbox holds at most 4096 receipts; overflow is counted and fails
acceptance. Scope/manager/role changes and room resets retire queued and
in-flight receipts, including a manager returning at the same address. No lock
is held over a native call or a callback. Worker client-local drops and picks
use copied policy, rechecking the native manager and live policy before skipping.

Each actual detour (`create`, `hit-drop`, `drop`, `pick-a`, `pick-b`) and
`NoteGameThread` emits `thread site=... tid=... gameTid=...`, bounded to the
first 16 TID changes per site. Periodic per-site totals retain entry, foreign
and change counts. `offThread` now records observed foreign entries; it is not
a failure or a worker relabelled as the entity thread. Native input/output
LastError is preserved around tracing and receipt work.

## Offline verification

- `kh2coop_struggle_orbs_test`: adapter controls and three real producer threads
  submitting 600 receipts concurrently with owner drain. Scope changes,
  unregistered/inactive context, client suppression, policy withdrawal during a
  read, stale return, native unwind, unknown collectors and bounded overflow.
- `kh2coop_struggle_orbs_hook_test`: actual private production detours, native
  reads, TID logger and Pump over owned memory and synthetic native functions.
  Both pickup argument routes pass births/collections through the real codec,
  ledger and client display. The displayed orb flies to its copied collector;
  same-frame pool reuse preserves the newer orb. A blocked native call does not
  block owner policy retirement, and its late return is refused. No hooks install.
- `test_struggle_orb_threads.py`: executes the committed scenario predicates in
  the production runner Context. Historical excerpts fail; synthetic positives
  allow foreign entries only with thread evidence, drain, sent/received collects,
  friend vanish and no overflow. Missing evidence/collect/receive/vanish or either
  seat's overflow fails. Synthetic positives are not a replay of unlogged pickups.

Source `2a2a9d0` on main `c330cfd`: full serial Release build passed.
The full CTest run passed **278/279** in 268.48 seconds, including **43/43 Python
files**, **35/35 actual orb-wrapper checks**, **447/447 adapter/rule checks** and
all five new scenario-predicate tests. The unchanged resync target failed one
combined quarantine assertion in 19.77 seconds, then again in 19.60 seconds.

Its original machine-code section matches the passing main binary, with matching
compiler, build graph and include sets. After restoring temporary diagnostics,
the original binary passed **308/308 checks, 3/3 idle runs**, in **15.97, 15.45,
15.51 seconds**, with the same CWD and no desk job before/after each. The main
baseline passed in 15.55 seconds. The timing-sensitive failure is separately
tracked as [VUH-2894](https://linear.app/vuhlp/issue/VUH-2894); both red logs remain
preserved. The original failures preceded Juniper's base job, so attributing
them specifically to that live run would be unsupported.

The scenario passed `run.py --validate` before rebase; the fixture and validator
blobs are unchanged, so that passing evidence is reused. A fresh recheck was
deferred when the next live job began. All builds/tests used below-normal,
serial execution; every build/test after the live handoff checked gate/jobs
first. Sanitizers are not claimed; this uses the normal MSVC path.

[Offline verification receipt](https://uploads.linear.app/75f1d1f0-542b-4095-9967-fd7b27093472/48673e83-a80e-4621-a359-5732528bc229/8ad77391-5e88-4361-92aa-1adc85490d15)
is attached to VUH-2881 and was downloaded back byte-for-byte. It pins source,
staged DLL, historical provenance, build/test logs, synthetic TIDs and idle
trials. Raw logs remain under this worktree's `.local/`; the evidence-store CLI
is unavailable in this seat. The attached receipt and issue are the shared
record.

## Remaining qualification

Independent inject review, then the lead's `net_prologue_struggle_orbs` rerun.
The new per-site TID lines must name actual native hook threads, nonzero host
collects must reach the friend as flights/fades, and the clone's brush-past
question must be observed. Offline synthetic threads prove that the wrappers
handle foreign callers, not which KH2 scheduler thread will enter them live.
Saves must remain unchanged; no live acceptance or Thorn victory is claimed.
