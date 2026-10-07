# Downed / revive network contract (VUH-1504)

The downed contract was introduced in protocol11; the current session protocol
is **12** (party setup), with AvatarBridge **4**. Older network
peers and avatar mappings refuse the new contract. The sealed friend packages
are unchanged. ENet and the default-off Steam backend use the same policy.

The sections below up to "Offline verification" are the network/bridge
contract. The native owner side (death intercept, held downed state, native
revive) is in `inject/` behind its own feature flag; see
[Native owner side](#native-owner-side-inject) at the end.

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

## Native owner side (`inject/`)

The native producer, consumer and requester described above exist in
`inject/src/DownedSpike.inl` (adapter, included in `EntityHook.cpp`),
`inject/src/DownedSpikeState.hpp` (pure rules) and the ReviveRequest admission
in `inject/src/EnemySync.cpp`. The names say "spike" because the live evidence
was gathered under them; they are the feature, not throwaway code.

**Feature flag: `KH2COOP_DOWNED_SPIKE=1`.** Default off. Unless the variable is
exactly `1`, `Install` returns before any byte check, hook, mapping or log;
`Tick`, `StatDelta` and `NoteHit` return on their first test, no publication is
made, no ReviveRequest is applied, and the game-over functions are untouched.
The cost when off is one bool test per frame and per ApplyStatDelta call.

When on (Steam build only; every native site is byte-checked at install, and a
mismatch logs `[downed] configure ... REFUSED` and installs nothing):

- **Intercept.** MinHook detours on the game-over requests `0x3FCD20` (mode 0)
  and `0x3FCAD0` (mode 3) skip the request only for the canonical local player
  (`g_soraActor` == `[0x2A105D0]` == entity-list head), on the game thread,
  after the native death has set the dead flag (`actor+0x9B8` bit 2), with no
  game-over task. Everything else (including mission failure, `arg == 0`) calls
  the original. Sora stays natively dead: DEADSORA action, controller off,
  enemies' damage blocked by the dead flag.
- **Episodes.** Seeded per boot (`nonce << 32`), +1 per intercept, never reused.
  While still downed, the owner **re-mints** a new episode after a native revive
  refusal or after 1,800 frames (~30 s) without an accepted request, so a lost
  or refused request can't strand the player. Requesters must always target the
  latest streamed episode; all the reservation floors above are `<=`, so they
  accept the larger value.
- **Publication.** Every owner frame, `LocalDownedState` goes through
  `AvatarBridge::SetLocalDownedState` with the scope from
  `enemysync::CaptureDownedScope`. Alive is published only after a successful
  native revive; natively dead but not held (refused) publishes `{}`.
- **Consume.** `enemysync::AdmitReviveRequest` admits the source (current roster
  peer, delivery serial, host cut / requester floor, body slot and connection
  match), then `ReviveOwnerGate::Consume` reserves the episode before exactly one
  `TryRevive(episode)`, which reruns every native check (owner thread, same
  episode, canonical actor, outside events, dead flag, live pointers, no task).
- **Revive.** Native `0x3AA8D0(actor)`, with its one `+max` heal rewritten in
  `HookedApplyStatDelta` to land on **25% of max HP (at least 1)**; then the
  game's own ignore-hit timer `0x3D7E40(actor, 120.0f)` (~2 s grace) and one
  idle motion request `0x3C86A0(actor+0x158, 0, 0.0f, 0.0f)` so a networked
  client stands up without input.

**Test channel: `KH2COOP_DOWNED_SPIKE_FIXTURE=1`** (or
`KH2COOP_DOWNED_SPIKE_CONTROL=1`, which implies it). Only then is the
shared-memory channel `Local\kh2coop_downed_<pid>` mapped. It carries per-frame
readouts and three debug commands: `Kill` (native ApplyStatDelta to 0 HP),
`Revive` (TryRevive on the current episode) and `RequestRevive(slot)` (sends a
ReviveRequest for a teammate's streamed episode, the same call the revive
prompt below makes).
`_CONTROL=1` installs only the channel and Kill, so the native game over runs.
Product play never maps the channel. `kh2coop_downed_state_test` covers the
pure rules and pins the 360-byte channel layout (version 7) the live fixture
mirrors.

**Live evidence (2026-10-06, Steam build, one rig).** Single-game treatment
214522: PASS_NOT_EXERCISED (two 24 s holds, one scripted and one natural death,
no game over, no input drift, revive at 6/24 HP, control back). Two-player pair
230440: PASS (friend1 downed and streamed to the host in 0.11 s, host
ReviveRequest forwarded through every hop, consumed once, 6/24 HP, grace 118,
friend1 stood up and moved 474 units, no game over on either game). The pair
run used the final source; the treatment ran before the grace/stand-up/network
round, whose paths it does not exercise.

**Limits.**

- Invulnerability while downed is NOT_EXERCISED: enemies never attacked a
  downed Sora in any run. It rests on the native dead-flag gate (static
  reading of `0x3D60C0`/`0x3D2EB0`) plus party Cures being observed blocked.
- A teammate's Cure does not revive; only `TryRevive` does. Donald will spend
  casts on a downed Sora.
- A room change, actor change or fault while downed marks the feature Refused
  for the rest of the process (no retry, no auto-revive on transition); the
  player may stay natively dead with no game over.
- All-party down, mission deaths (mode 3 with an actor is gated; arg 0 stays
  native), drive forms, summons, the `g_sys400` bit-17 branch and non-Sora
  player classes are out of scope. The debug Kill refuses those branches.
- No range or liveness cross-check on the owner side (the requester's prompt
  checks range), and no refusal ACK back to the requester. A live negative control of the owner gate
  is still open. Only the 1-host/1-friend pair on the local relay is proven;
  three players and Steam cross-account are not.
- The hooks stay installed as pass-throughs until process exit.

## Revive prompt (`KH2COOP_REVIVE_PROMPT=1`)

The player-facing trigger for a ReviveRequest. Default off; it needs
`KH2COOP_DOWNED_SPIKE=1` and is never active in the `_CONTROL` run. When off,
`PromptInput` and `PromptTick` return on their first test and the HUD snapshot
carries `promptKind = 0`, so the overlay is unchanged.

- **Rules** (`inject/src/RevivePrompt.hpp`, pure). The prompt targets the nearest
  fresh (pose within 30 frames), downed, non-cutscene teammate puppet in the same
  world and room, within **150 units**. That's under SessionHost's 200-unit
  admission policy. Hold **Triangle for 60 game frames**. The request fires on
  **release** after a full hold, never while held. The hold must start with a
  fresh press. L1+Triangle (the native shortcut) never counts.
- **Cancels.** The hold resets if you leave range, take damage, open a menu,
  enter an event or transition, go down yourself, or the target or episode
  changes.
- **One request per episode.** Each target episode fires at most once. A
  re-minted episode (see Episodes) re-arms the prompt.
- **Sending and logging.** The one action is the existing `RequestRevive(slot)`.
  Hide reasons and the fire are logged as `[revive-prompt] ...`, capped at
  32 lines each.
- **Input.** `HookedInputCollector` reads the raw slot-0 buttons after the
  mailbox/event-hold apply. That's the same buttons the game consumes, whether
  from the local pad or the kh2ctl/runtime override.
- **HUD.** The prompt is drawn in the downed teammate's own co-op HUD row, whose
  HP is 0. So the 130-px overlay layout is the same with or without it.
  - The HP column shows `Hold △`, then `Reviving` for up to 120 frames after a
    fire.
  - The row's bar fills yellow with the hold progress. `Snapshot` carries
    `promptKind/promptSlot/promptProgress`, and the display scope changes in
    10% steps.
  - Labels must fit `hud::HealthColumnChars` (10 cells), or they are ellipsized.
- **Native reaction command (RC): the prompt yields.** The game's own RC also uses
  Triangle, so a prompt that would show is hidden while the RC is up. The hide
  reason is `native-reaction`, and each suppression is logged as
  `[revive-prompt] yield to native RC 0x2A11162=…`, at most 256 times per process.
  - The address is `offsets::REACT_CMD_STEAM` = `0x2A11162` (u16), calibrated in
    run 004215 at the GoA flag409 chest: it read `0x20` with "Open" showing and
    through its popup, and 0 away from it.
  - KH2Lib's `REACT_CMD` `0x2A110E2` stayed 0 throughout, so it is documented as
    wrong for Steam. It is still recorded in the fixture channel.
  - `0x2A11162` reads a stale `0x37` at boot, so it is trusted only in settled
    field play: the local player is canonical and not downed, with no event,
    menu or transition, for 30 consecutive frames (`NativeReactionFor`). Outside
    field play it counts as 0.
- `kh2coop_revive_prompt_test` covers the rules, cancels, latch, hide reasons,
  L1 exclusion, the RC gate and yield, and the HUD row (fill, label width,
  scope).

**Live evidence (2026-10-07, Steam build, one rig, local relay).** Pair-hold
000112 and 001034: PASS. The host held a scripted Triangle 90 units from the
downed friend1:
- 78 Triangle frames, progress 1000, exactly one fire and one request;
- friend1 revived at 6/24, grace 118, stood and moved 474 units;
- no game over.

In 001034 the host overlay was on, and the clip shows the row's yellow fill
growing, then `Reviving`. That run used the longer labels, which the column
truncated; the shortened labels are offline-checked only.

**Prompt limits.**
- The RC yield has been calibrated on one object RC only (the GoA chest's
  "Open"). Enemy and other RCs are assumed to use the same field.
- A manual pad press is unverified; only the kh2ctl input override was tested.
- The prompt is not hidden while the local player is in an event-hold.
- Tested live (2026-10-07, main `24a00cc`): host revives friend1 (000112,
  001034), friend1 revives the host (003536), and three players where friend2
  revives friend1 while the host observes (003832).
