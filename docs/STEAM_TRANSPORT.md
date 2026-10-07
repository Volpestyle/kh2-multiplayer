# Steam transport work (VUH-1493)

Steam connectivity is not available to friends yet. ENet remains the default
packet transport. Native delta `fff108b` has lead re-review ADOPT with no blockers;
cross-account validation waits on James's second account. Protocol10, its channel assignments,
session/cache/resync authority and all existing admission checks are unchanged.

`NetworkClient` and `SessionHost` accept an optional owned `Transport` as their
last constructor argument. With none supplied, they create `EnetTransport`.
The interface handles connection events, packet bytes, disconnect reasons and
link statistics. It is single-thread-owned; received packets retain their own
buffer until released, even if a callback closes the transport. ENet startup
and shutdown remain the calling application's responsibility. Test-only raw
ENet injection still bypasses the interface deliberately to exercise malformed
wire traffic. Its opaque peer handle is not a game/session identity.

The approved next design places a Steam broker inside the game, using the
game's existing app2552430 Steam session. The host runtime retains session,
cache and resync logic. Joining starts with a pasted SteamID and explicit host
admission. Routing must be relay-only with ICE disabled and verified; unsupported
privacy settings refuse. No app480, second Steam initialization or Steam shutdown.
The broker and its runtime link are implemented behind explicit opt-in below.
They do not move session/cache/resync authority into the game.

## Opt-in capability probe

`KH2COOP_STEAM_CAPABILITY_PROBE=1` enables a single bounded diagnostic on the
existing DLL initialization worker after hooks install. It requires an existing
`KH2COOP_LOG_DIR` and creates a new `steam-capability_<pid>.log`. Any other value
leaves it off. It checks the already-loaded shipped flat API/versioned interfaces,
app ID, logged-on SteamID, authentication and SDR relay readiness.

The only connections it creates are one internal-buffer `CreateSocketPair`.
It checks connection-local ICE-off configuration and observes callback dispatch
from the game's existing callback pump after closing one endpoint. It neither
opens a listener nor connects to another account. Both endpoints are closed.
The overall observation budget is 60 seconds, including a maximum 5-second callback
window; synchronous Steam calls cannot be externally cancelled by this deadline.

**Enabled mode pins the DLL until process exit**, protecting queued callbacks
from a code-unload race. Normal shutdown still signals and joins the worker.
Callbacks only touch static atomics. Default-off mode does not pin the DLL or
call Steam. A missing session/export/privacy setting/authentication/callback is
an unavailable result, not evidence of network connectivity.

Lead native-code review and an explicitly assigned live lane are required before
the first game probe. No live result is claimed by the offline checks. An eventual
friend connectivity test still needs two authorized Steam accounts/machines;
single-account socket-pair success cannot prove SDR cross-account routing.

## Opt-in broker (offline-tested, not live accepted)

Set `KH2COOP_STEAM_BROKER=1` **only in the owned game's launch environment**.
Use the reviewed broker DLL and wait for its `steam-broker_<pid>.log` readiness
receipt. Do not enable the capability probe at the same time. The default is off;
there is no new hook or game-memory/input/save writer. The source launcher offers
a default-off Steam (beta) mode; sealed release09 remains unchanged.

Start the matching runtime with its existing config/build/content/mod arguments:

```text
kh2coop_runtime_scaffold.exe --pid HOST_PID --role player --steam-host --steam-allow FRIEND_STEAMID64
kh2coop_runtime_scaffold.exe --pid FRIEND_PID --role friend1 --steam-join HOST_STEAMID64
```

A second friend uses `--role friend2` and a second `--steam-allow` on the host. The allowlist is explicit and immutable for that runtime session (maximum
two public individual SteamID64s). No IP address, public port bind, app480, lobby,
invite service or account creation is involved. The number27795 here is a Steam
**virtual port**, not a UDP listener.

The game uses its already initialized app2552430 session. After authentication
and SDR readiness, the broker exposes one local Windows message pipe scoped by
PID and process creation time, current-user ACL, first-instance ownership and
remote-client rejection. Both ends check OS-reported peer PIDs and retain peer
process handles. This protects against remote clients and stale PID endpoints;
it is not a sandbox against malicious code already running as the same user.
Only packet data and transport operations cross IPC, never native pointers or
memory/input commands.

`CreateListenSocketP2P` and `ConnectP2P` receive ICE=0 in their **creation options**.
The broker checks the effective int32 value on the actual listen/connection
handles, including incoming connections before acceptance. Unsupported/nonzero
settings refuse and close owned handles. Connected and data-path checks require
an authenticated, encrypted, relayed connection. A missing callback never creates
admission; pending connections time out after15seconds. Steam invokes the callback
through the game's own pump. Its 64-entry ring copies status only, using an O(1) lock-protected section.
Overflow ends the attached session, closes its owned handles, then clears the
lost history and serves a fresh runtime attachment. It does not require a game
restart. Late events cannot adopt unknown handles. Enabled
mode pins the DLL until process exit to keep queued callback code alive.

The host runtime owns `SessionHost` and a local Player transport. Its authenticated
identity is reserved for Player; remote Hello identity must equal the SteamID
from the authenticated connection and cannot claim Player. Existing protocol10,
build/content/mod, roster, epoch, cache, quarantine and resync checks still apply.
Remote protocol bytes retain the original codec inside a small Steam envelope.
Reliable traffic is ordered on one Steam connection (potential additional
cross-channel head-of-line blocking); ENet's three-channel behavior is unchanged.
Steam link statistics sample SDK ping and local end-to-end delivery quality once
per second. Loss is derived from that quality, not an injected impairment setting.
RTT variance is unavailable (zero); initial/missing statistics are also zero, not
measured zero latency/loss. Protocol clock sync still runs.

IPC payloads are capped at64KiB, application packets at65528bytes, and queues at128
messages. SDK send buffering is capped at512KiB per connection. Broker receiving
backs off at a combined broker/pipe backlog of96; pipe and runtime consumers stop
reading saturated queues. Explicit SDK `LimitExceeded` queues ordered sends for
retry, up to128 per peer; other send failures close that peer. Hard queue caps,
invalid authority and IO failures remain fail-closed. No reliable packet is
silently discarded as congestion recovery. Long consumer stalls can still expire
the existing heartbeat; these are bounded queues, not an indefinite spool.

Normal peer churn does not close the host session: queued Send/Close for a
previously admitted, now-retired identity is ignored; a never-admitted identity
is still a protocol error. A failed accept or15-second pending timeout retires
only that peer. An established connection's identity/app/log-on status is checked
once per second; transient SDR/auth availability changes are not an established
identity failure. Initial attachment still requires authenticated SDR readiness.
Runtime heartbeat is1second with a5second expiry; the runtime loop must service
within that limit. Initial IPC hello also has a5second deadline.

The worker compares the game's current Steam pipe to the captured nonzero pipe
before every tick/API group and skips even cleanup API calls after observed pipe
loss. **A residual shutdown race remains:** the game may call `SteamAPI_Shutdown`
after this check and before the following SDK call. There is no shared SDK
lifetime lock. Explicit mod shutdown signals/joins the initialization worker;
ordinary game exit does not promise to do so before the game's own Steam teardown.
A future live run must include graceful game exit with a session active and watch
for hitches/crashes. Synchronous SDK calls and OS cancellation completion cannot
be externally preempted by logical deadlines. No shared global Steam
configuration, Init, Shutdown, RunCallbacks or network-facing IP socket is used.

A terminal Steam IPC/transport failure requires a fresh runtime attachment;
there is no automatic ENet fallback. Existing ENet recovery behavior is untouched.
The gameplay constraints on reconnect/resync and unsupported dead reconstruction
remain unchanged.

Validation covers mocked Steam authentication/admission/ICE failure, actual Windows
pipe ownership/closure, protocol admission, avatars and late-join room/manifest/
progress caching. It does not prove cross-account SDR routing or native callback
liveness. Required next evidence after the adopted native review: two authorized logged-on
Steam accounts with KH2 app2552430 on separate machines, explicit host allowlist,
matched protocol/build/content/mod, ICE verification on real handles, relayed flags,
then version refusal/avatar/world-cache exchange and owned closure/save guards.
No friend-readiness claim.


## Reconnect controls and the listener probe (offline)

`kh2coop_steam_broker_test` also covers reconnection through the real broker core, transport,
NetworkClient and SessionHost (only the Steam API and the OS pipe are mocked). Two cases:

- **A peer-level Steam drop with the pipe still alive.** The friend sees one disconnect, and the host
  session and the friend's attachment stay up.
  - **As built, an in-place rejoin is terminal.** `NetworkClient::connect()` first calls `disconnect()`,
    and a remote `SteamTransport::close()` fails its hub. So the attempt closes the live attachment, and
    the runtime's bounded recovery cannot rejoin over Steam.
  - The control records this behaviour. A fix (a non-terminal client close that keeps the attachment)
    must flip it deliberately.
- **Loss of the broker attachment (pipe).** That runtime's session ends and cannot reconnect in place. A
  fresh runtime attachment to the same broker (the launcher's next Connect) is re-admitted by the
  unchanged host session.

Neither case is proven live.

A single-account live fixture, **probe 03** (listener only, PENDING), is designed. It starts the friend
launcher's Steam host runtime twice against one game at the title screen and requires these receipts:
- the broker ready receipt;
- `listen ... iceCreation=0`;
- an ICE readback of `value=0 verified=1` on the real P2P listener;
- the runtime's Steam identity equal to the broker's;
- verified membership;
- a detach that closes the listener.

A readback other than 0 stops the run without relaxing admission. A draft Steam playtest text lives in
`FRIEND_PLAYTEST_STEAM.md`; it is unreleased.

## First two-account live-run checklist (not yet executed)

Lead re-review ADOPT is recorded in the **Delta re-review** section of
`build/rig/vuh1493-steam-p2p-20261006-01/broker-candidate/lead-review-findings.md`.
This checklist does not grant the live lane. Wait for James's second authorized
account/machine and a lead-assigned run; use the reviewed, pinned products and
existing ownership/save guards. Do not enable the capability probe alongside
the broker. Keep all prior FAILs.

1. Confirm app2552430 authentication and SDR readiness for both actual accounts,
   the explicit host allowlist and matching protocol/build/content/mod. Retain
   effective ICE=0 receipts on the real P2P listener and connections, plus
   authenticated/encrypted/relayed connected-state flags. No app480 or direct-IP
   fallback.
2. **N6: incoming authentication.** Retain the host's actual Connecting-state
   `callback handle=` record (`state=1`), SteamID, listener and raw flags before
   acceptance. Check unauthenticated/unencrypted bits are clear (`flags & 3 == 0`)
   and that acceptance belongs to that allowlisted account. If Steam reports a
   different state/flag sequence, preserve the refusal; do not relax admission.
3. **N5: callback cadence.** Retain callback state transitions and timestamped
   observation intervals during connection and gameplay. Report observed delivery
   cadence/delays and any pending timeout; untimed line order or auth/SDR readiness
   alone does not prove the game's callback-pump rate. Do not add RunCallbacks.
4. Check version refusal, verified roster/ownership, avatar exchange and the
   existing world-cache/admission behavior. Record each peer departure and the
   remaining session state; do not claim a three-peer result from two accounts.
5. **D3: frame pacing.** Watch for hitches during active traffic, connection and
   departure, retaining available frame-timing/capture evidence. Distinguish
   observed hitches from an attributed lock/IPC cause; do not claim a quantitative
   pacing result without measurements. The callback lock can wait if its worker
   is preempted even though the critical section is O(1).
6. **D1: graceful game exit while the session is still active.** Use the game's
   normal exit without saving, then read the broker log after exit (or with
   write sharing). Require an actual `runtime-detached steamPipeAlive=0` receipt
   to qualify observation of Steam-pipe invalidation. Forced canonical cleanup
   is not this test. Missing/`1` output leaves this guard unverified; process exit,
   lack of a crash, or a final file timestamp cannot substitute. Retain any crash
   and complete owned cleanup/save checks. Even `0` does not eliminate the
   documented check-to-call race.

Optional D2 (stale send during a replacement connection), D4 (non-allowlisted
flood can end an attachment), and D5 (best-effort error delivery under saturation)
remain recorded nits, not claimed fixed or exercised. No new live result or
friend readiness follows from this checklist.

First-party contracts: [Steam sockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets),
[configuration API](https://partner.steamgames.com/doc/api/ISteamNetworkingUtils),
and Valve's [networking type definitions](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h).
The narrow native flat ABI is pinned against the retained Valve headers and
shipped exports in `build/rig/vuh1493-steam-p2p-20261006-01/valve-reference/`.
