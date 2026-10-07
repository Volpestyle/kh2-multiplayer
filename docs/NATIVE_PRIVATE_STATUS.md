# Native Sora private status (VUH-1489)

When the game spawns a native Sora in a friend slot (the D3 actor model, see
`ONLINE_COOP_PLAN.md`, "Puppets"), the clone and the local Sora natively share
one status record, so damage to the clone also drops the local player's HP.
`inject/src/NativePrivateStatus.cpp` gives that clone its own status record
from the game's own status pool. It is an experiment for one exact setup, not
general character/status isolation.

## Flag and scope

**`KH2COOP_NATIVE_SORA_PRIVATE_STATUS=1`.** Default off. Unless the variable is
exactly `1`, `privatestatus::Initialize` returns true at once: no image check,
hook, storage or log. `Drain` then returns on its first test and
`RetainsMinHookResources` stays false.

When on, it refuses to install (and logs `[privatestatus] initialization
refused; profile unqualified`) unless every one of these holds:

- Steam executable `9002b2de…`: every hooked body, gateway site and unwind
  record is byte-checked against `NativePrivateStatusPins.hpp`;
- none of `KH2COOP_LIFETIME_TRACE`, `KH2COOP_SPAWN_TRACE`,
  `KH2COOP_NATURAL_RESOURCE_TRACE` or `KH2COOP_SURVIVING_PACK_PREPARE` is set;
- every hook prepares and enables. A partial install never arms selection or
  allocates a private record.

The only construction it selects is the GoA Friend1 native Sora: party row
`00/00/02/12`, descriptor object84/type0/key1/form0/`P_EX100`, built through
the fixed factory return on the native owner thread, in the same load/transition
stamp as a freshly completed local actor, and distinct from it. Anything else
keeps the ordinary native (shared) construction.

## What it changes natively

| Native boundary | Action | Preserved |
|---|---|---|
| `3DF930` factory / `3A7A40` constructor | Fiber-local POD context associating the local actor and Friend1 | Original once; args, result, exceptions, LastError |
| `3C0620` allocator | Only the selected call uses native `3C03F0` | Native allocation, init and actor store |
| `3C0010` init (caller `3C0432`) | Registers the native-popped slot before init | Original initializer, key, SAVE handles, refcount |
| `3C05EB` shared lookup | No-call gateway skips the owned private slot | Ordinary lookup and refcount |
| `3C2120` commit | Veto only for the currently owned private slot | Ordinary commits |
| `3C0830` final free | Clears ownership before the native free-count increment | Native free/refcount order |

It adds no native pool-key, free-list or refcount write. Module, hooks, fiber
storage, gateways and unwind data are retained until process exit. After
`StopNewAllocations` (shutdown), existing ownership and the veto stay until the
native release. Hook points in `EntityHook.cpp`: include, `Drain` at frame
start, `Initialize` after the lifecycle trace, `StopNewAllocations` at
shutdown, and the MinHook-retention checks.

## Evidence

- **Live, 2026-10-06** (run `20261006-222637`, two games, local relay, one GoA
  room): PASS. One native damage on the clone took it 24 -> 23 while the local
  Sora stayed 24/24. A teardown reload retired the private lifetime, exercised
  the save veto (4 -> 7), re-created a fresh private record (clone restored
  24/24), kept the 80-slot partition and the SAVE spans unchanged, and the
  party row was restored exactly. No save attempts; saves unchanged.
- **Offline:** the packet's 26 own-process control modes (pool, lookup,
  stale/unknown init, lifetime reload, unwind through gateways, fibers,
  exceptions, SEH, off, selection, binding refusals and negative controls)
  pass against this source. They are not part of the CMake test suite.

## Limits

- One executable, one party layout, one friend slot, one room profile. Other
  characters, Friend2, other worlds and multi-room play are unqualified.
- `3C1D90` (SAVE-to-status refresh) and `3C1190` (equipment/ability mutation)
  are not suppressed: a native refresh can reset private HP from SAVE. Direct
  SAVE writers that don't enter `3C2120` are not covered; zero observed hits is
  not proof of absence.
- Pool operations are natively unsynchronized; only the registered game thread
  is admitted. A foreign-thread private commit is vetoed but fails health.
- Open follow-ups: the unbound clone reads pad 0; EntityHook drove Donald for
  one frame after a reload.
- Never save while it's active; the SaveGuard and save-hash checks stay
  mandatory.
