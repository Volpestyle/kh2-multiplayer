# Downed / revive network contract (VUH-1504)

Protocol **11** and AvatarBridge **4** are required together. Older network
peers and avatar mappings refuse the new contract. The sealed friend packages
are unchanged. ENet and the default-off Steam backend use the same policy.

This is the network/bridge implementation. It does not intercept death, change
HP, suppress game-over, choose a button, or invoke a native revive function.
Those actions remain with the native owner and require separate review/live
qualification. No `inject/` or launcher code changed here.

## Native producer hook

On each checked native owner frame (alive **and** downed), publish
`AvatarBridge::SetLocalDownedState(LocalDownedState)` from
`common/include/kh2coop/DownedState.hpp`:

- captured `ProducerWorldContext` generation and delivery serial;
- actual admitted epoch and full world/room/door/map/battle/event tuple;
- `episode`: nonzero, strictly increasing for every new downing over the whole
  game lifetime, including room changes and reconnects. Never wrap or reuse;
- `sampledAtMs = GetTickCount64()` and the checked `downed` state.

The setter is a single-owner bridge publication, with no native reads/writes.
Unavailable native checks must publish `{}`. Runtime projects it into each
new local avatar only when generation/delivery, room, and a maximum 1000 ms
sample age match. Otherwise downed authority is cleared. The streamed fields
are `AvatarDowned`, `downedEpoch`, `downedEpisode`, and captured `downedDelivery`.
The state is not inferred from HP zero. Native integration must keep publishing
while downed; ordinary entity publication liveness is not proved by these tests.

## Request and host validation

The native requester sends an encoded `ReviveRequest` using the existing
`WorldBridge::SendToRuntime(packet, capturedContext)` owner-only ring. It holds
the current full room tuple, its roster connection ID, the observed target
slot/connection/downed episode, and a nonzero sequence increasing across rooms
within its connection. Do not recycle the sequence. `NetworkClient`'s typed
sender is a headless convenience; native code retains its real producer context.

SessionHost checks the existing immutable world envelope first, then requires:

- verified, unquarantined, distinct requester and target in the current room;
- both arrival ACKs for that epoch and world/room, no active EventHold;
- both streamed positions received within 1000 ms, finite XYZ, matching epoch,
  captured delivery serial, world/room, and no cutscene flag;
- living requester, downed HP-zero target, positive valid maximum HP;
- matching target episode, not previously reserved, and Euclidean distance
  at most **200 native world units** (initial policy, not a live-qualified reach).

The requester slot is stamped from its authenticated connection. Authenticated
sequences that reach gameplay validation are consumed even on refusal. Target
episodes are reserved before forwarding, so duplicates or another teammate
cannot authorize a second application. This fails closed if delivery/application
fails; there is no retry/ACK/UI success mechanism in this slice.

The reliable request goes **only to the target owner**, never into the late-join
or resync cache. This uses SessionHost whether it runs in the Steam host runtime
or the ENet relay. It validates owner-reported positions, not independent native
geometry or anti-cheat truth. Freshness here is relay receipt age, not proof of
continuous motion or synchronized native frames.

## Target owner integration

NetworkClient rechecks the current scoped target, room, fresh local downed
avatar, and episode before either typed callback or raw WorldInbox delivery.
Runtime's existing reliable WorldInbox carries the original envelope to the
DLL. It never applies HP or substitutes native ownership.

After normal native world-envelope admission (including source delivery floors),
the native owner passes the request, exact received scope, current session,
producer context, current three connection IDs, local slot and its fresh
`LocalDownedState` to `ReviveOwnerGate::Consume`. Keep this gate for the whole
game lifetime. It reserves the episode **before** one call to
`downed::TryRevive(episode)`. That native function must independently recheck
the actual actor/life/world/safety before applying; a false return is not retried.
Do not call it from the runtime, callback thread, or a raw input thread.

The original owner must publish alive state only after native revive actually
succeeds. A forwarded request is not evidence that healing occurred. All-party
down, mission failure, transition/cutscene handling, revive HP amount and button
UX remain native/policy work.

## Offline verification

`kh2coop_revive_test` covers the codec, actual test-process avatar/world
mappings, runtime pump, consume-once gate, real three-peer ENet loopback,
protocol10 refusal, stale/replayed state and request refusals. The same
`ReviveNetworkFixture` runs through the real Steam broker/transport/client/
SessionHost stack with mocked Valve and IPC-link boundaries in
`kh2coop_steam_broker_test`. No Steam account, game, or save is used.

Existing avatar, world-sync, and HUD-name suites remain required. This evidence
does not qualify the native intercept or a real cross-account Steam session.
