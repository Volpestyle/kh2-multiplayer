# Population authority diagnostic

`KH2COOP_POPULATION_AUTHORITY_TRACE=1` is an independent default-off,
observational profile for one native game with no network runtime. It observes
normal controller constructor/init/update/teardown, fixed/generated wrapper,
primary factory, death/count and removal/disposal calls through the existing hook owners. It
does not call a creator, pause a controller, deny a native call, acquire a
gameplay lock or write count/cache/actor data. Incompatible experiment flags
refuse this profile before enabling its additional hooks.

Default off performs no diagnostic installation, dynamic allocation or native
read. Enabled storage is a fixed process-lifetime pool. It samples thread/fiber
and stack-bound identity without FLS allocation/set calls. Context equality is
not uninterrupted fiber execution or creation authority. Coverage loss, queue
contention/exhaustion, changed return identity and the180s/10800-frame bound stop
new recording irreversibly; native calls still pass through once. Native LastError,
arguments, raw returns and SEH propagation are preserved. Terminal receipts for
already-entered calls are retained where queue capacity permits.

The profile validates saved native entry runtime-function spans and their native
unwind identities before setup. These finite spans are not a complete creator
domain or whole-program function graph. Before enabling each added detour it
checks MinHook's exact stolen instructions, continuation and relay layout, then
registers retained trampoline/relay unwind metadata. Unsupported layouts refuse.
The conditional leaf3FED10 is checked separately from stack-adjusting entries.
Setup allocations/module/trampolines/unwind tables remain valid until process
exit after exposure, including partial installation.

`[population-authority]` events carry immutable enter/exit sequence, invocation,
parent, context, caller, native result and independent validity-tagged raw
controller/header/record bytes, five-record definition samples, counts, roots
and location. Lifecycle serials are sampled only on the registered frame thread;
foreign-thread serials are unavailable. No stale post-disposal actor pointer is
dereferenced. Returned actor roots and physical absence require an independently
fresh runner census; a pointer return itself does not establish membership.

All authority conclusions remain UNKNOWN. The first diagnostic covers twelve
native seams, not all primary/secondary/resource/queue ingress. It cannot prove
global pending closure, creator exclusivity, continuous fiber execution, count
credit or a reusable forced-spawn permit. Exhaustive indirect/secondary/resource
coverage, a native pending baseline/barrier, native invocation-side census and
complete lifecycle qualification remain further discovery work. Quiet observed
open counters never fill those gaps.

Read-only log analysis:

```powershell
python -B tools/rig/population_authority_evidence.py <inject-log>
```

The joiner retains missing/duplicate chunks, changed returns, broken ancestry,
recorder loss and incomplete hook masks. It always reports INCONCLUSIVE with
creationAuthority=false. `receiptGraphComplete` refers only to the supplied
observer receipt graph and does not qualify the requested native lifecycle or
permission to create actors.

Source controls include the fixed recorder, actual update/wrapper call-through
and SEH adapters, and actual MinHook relocation/Windows virtual-unwind controls
over owned scratch code. No saved game body is executed by those controls.
Native build/CTest results are pending an explicit kh2-steam reservation. The
later single-PC fixture needs reviewed frozen products, actual pinned setup,
natural route/combat, independently fresh census and owned safety closure; none
of these is supplied by the log joiner or this source preparation alone.
