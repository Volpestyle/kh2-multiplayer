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
Native-validated source fb8e21d passed its isolated all-target build and full
161/161 serial CTest. Native authority and forced enrollment remain unqualified.

## Parked: authority diagnostic and reconnect-04 (2026-10-09)

The late-join population repair landed on main (2d45f39). Its live run
`20261009-000023_vuh1788_late_join_population_repair_1` passed the fixture oracle
and safety closure: stale terminal copies became physically absent, both complete
samples matched the host living IDs/HP/bindings, and no admission/budget waits
remained. See [population repair](POPULATION_REPAIR.md).

Reconnect works semantically in retained run
`20261009-012620_vuh1788_reconnect_population_1`: the friend rejoined with a new
connection/generation, installed fresh PopulationCuts, and samples 2-7 matched
all peers' living IDs and HP. The original anchors retained their identities.
The canonical result remains FAIL: new host birth ID11 was forced on the friend
with controller=0 and spawnRecord=0. That observation does not prove native
wave-clear counting, lifecycle membership or safe forced enrollment. Receipts:
`build/rig/vuh1788-waves-20261008/population-reconnect-03/live-findings.md`.

Native enrollment authority remains UNKNOWN. The observational diagnostic has
no qualified unwarped courtyard (05/06) start. In
`20261009-092736_vuh1788_native_authority_discovery_1`, ordinary boot loaded
05/01 and the required 05/06 gate refused before route/combat. The sandbox copy
matched its derived hash, but its consumption by Load was unproven. Sandbox
seeding is abandoned; no caching investigation or guessed route is planned.
The diagnostic observed constructor/init calls only; native lifecycle and
creation authority remain UNKNOWN. Safety passed with all four saves unchanged
and owned game/children/encoders closed.

Both the authority diagnostic and reconnect-04 are PARKED. Reconnect-04 is an
unsealed draft, not an acceptance result or a runnable successor. No enrollment
product integration or additional native/live work is queued by this note.

The cheapest future unblock is either a recorded native walking recipe from the
ordinary 05/01 load to 05/06, or a reviewed design change permitting warp before
the diagnostic baseline. Docs and kh2-story have no retained 05/01-to-05/00
walking recipe; the documented courtyard/hall door pulses do not fill that gap.
If the walking path resumes, Lead permits only one further setup attempt; a
route failure parks the diagnostic again. Any live run still needs separate
operator GO. Authority discovery must precede the forced-membership product,
which must precede reconnect-04 acceptance.
