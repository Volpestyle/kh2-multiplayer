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
admission; pending connections time out after 30 seconds. The broker explicitly pumps
the sockets interface on its worker. Its 64-entry ring copies status only, using an O(1) lock-protected section.
Overflow ends the attached session, closes its owned handles, then clears the
lost history and serves a fresh runtime attachment. It does not require a game
restart. Late events cannot adopt unknown handles. Enabled
mode pins the DLL until process exit to keep queued callback code alive.

### Incoming acceptance and close decisions

The 2026-10-07 rev3 host received the exact allowlisted SteamID on its listener
with Connecting flags=2. The adapter incorrectly treated Unencrypted (bit 2)
as unauthenticated identity, closing before AcceptConnection. Authentication
now reflects only Unauthenticated (bit 1); raw flags travel with the status.
The incoming gate requires a valid allowlisted authenticated identity, available
peer capacity, no duplicate and verified ICE=0. It permits encryption pending
only at Connecting. Accept does not emit Connected or authorize traffic. The
Connected gate still rejects either low flag bit or a non-relay route, and
native send/receive continue checking encrypted/authenticated relayed state.

Every broker close decision records action, named reason, SDK peer identity,
allowlist size and last observed raw flags before closing or clearing authority.
Host configuration and accepted Connecting decisions are recorded too. The
wire close reason remains unchanged. The empty-allowlist probe still closes all
remote peers with incoming-not-allowlisted; it has no bypass flag in the broker.
Offline core and native-adapter tests cover flags=2 acceptance, explicit foreign
peer refusal, absent early admission, and unencrypted Connected refusal.
Valve defines the separate flag bits in
[steamnetworkingtypes.h](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/steamnetworkingtypes.h)
and requires accept-or-close at Connecting in
[ISteamNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/isteamnetworkingsockets.h).

### SDR connect budget and callback diagnostics

Steam uses a 30-second route/connect budget in three places: per-handle SDK
`TimeoutInitial` (config 24), the broker's pending-peer deadline, and the runtime's
`ClientRecovery`. ENet keeps its four-second connect budget. The separate four-second
roster deadline and 60-second bounded rejoin episode are unchanged.

Relay warmup already runs at broker initialization, before READY. The initial
identity gate requires authentication and relay/config/any-relay availability
Current (100), within the existing 60-second startup limit. `relay-init` and
`relay-status` receipts show that call and subsequent numeric status changes;
sampling continues once per second while the broker serves attachments. No extra
warmup is issued at READY, since initialization already happened before that gate.

The connection callback is a per-listener/per-connection creation config (201),
using the game's SteamNetworkingSockets interface, current Steam user and pipe.
The broker calls the flat `SteamAPI_ISteamNetworkingSockets_RunCallbacks` export
on its sole worker at a minimum interval of 16 ms while serving an attachment.
It never calls global `SteamAPI_RunCallbacks` or manual dispatch. No ring lock is
held across the SDK call: callbacks can execute synchronously, take the bounded
SRW lock to enqueue, and are drained afterward by that same worker. Concurrent
callback delivery from the game is safe under that ring lock. A foreign thread
cannot call the broker pump; an ended/replaced Steam pipe stops it. The pipe
check is not an SDK shutdown lifetime lock, so the existing shutdown race remains.
The startup receipt records registration, current user/pipe and dispatch mode;
the first actual pump records its worker thread. Every dequeued callback logs
Connecting, FindingRoute, Connected, ClosedByPeer and ProblemDetectedLocally,
with old state and raw SDK end reason. The receipt prefix remains unchanged.

Valve's header says RunCallbacks invokes configured callback functions, and also
says Steam's default dispatch may make that call unnecessary. Therefore the
header alone does not prove KH2's missing callbacks were caused by absent pumping.
The explicit pump removes reliance on an unverified game dispatch path. The
flat-ABI fake test holds SDK events pending until this production pump runs;
it checks delivery, cadence, foreign-thread refusal and closed-pipe refusal.

`connection-poll` queries GetConnectionInfo once per second on owned connections
and records state/flags/end-reason changes with the callback receipt count. It
also records unavailable results. This is diagnostic only: a polled Connected
state does not manufacture an event or bypass callback admission, identity,
relay-only or ICE checks. It distinguishes SDK connection progression from an
empty callback queue on the next two-PC attempt. Closed handles are removed.
Probe03 is listener-only with an in-process Player endpoint; it never required
a remote connection-status callback, so its PASS cannot qualify callback delivery.

Valve documents relay warmup and callback dispatch in
[ISteamNetworkingUtils](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/isteamnetworkingutils.h)
and [ISteamNetworkingSockets](https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/isteamnetworkingsockets.h).
The 2026-10-07 join failure therefore established premature four-second closure
and absent callback receipts; it did not establish SDR cold start as the sole cause.

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
configuration, Init, Shutdown, global SteamAPI_RunCallbacks or network-facing IP socket is used.

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
  - **G8 fix (candidate):** while the attachment is healthy, a joiner's close is per peer. It sends
    `Op::Close` for each live peer and keeps servicing the hub, so broker heartbeats continue between
    rejoin attempts.
  - The broker lets a joiner Join its **same** host again once the previous connection is gone. Another
    host, a second Join while connected, and a Join from a hosting attachment are still refused.
  - Every Close a joiner sends, from `close()` or `disconnect(peer)`, owes one answer. The broker answers
    each Close in order, before any later Join. So while an answer is owed, every frame for that peer
    (Connected, Data, Disconnected) belongs to the old connection and is dropped. The owed Disconnected
    clears the debt.
  - A Connected that overtook the Close at the connect deadline cannot leave a debt that would later
    swallow a genuine drop. Old Data that arrives after an immediate re-connect cannot fail the hub.
  - IPC errors and transport destruction still end the attachment.
  - The runtime's bounded recovery therefore re-Joins over Steam.
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
   acceptance. Require the unauthenticated bit clear (`flags & 1 == 0`) and
   that acceptance belongs to the allowlisted account. Connecting may have bit 2
   (encryption pending); Connected and data still require `flags & 3 == 0` plus
   relay. If Steam reports a
   different state/flag sequence, preserve the refusal; do not relax admission.
3. **N5: callback cadence.** Retain callback state transitions and timestamped
   observation intervals during connection and gameplay. Report observed delivery
   cadence/delays and any pending timeout; untimed line order or auth/SDR readiness
   alone does not prove callback delivery cadence. Retain the explicit sockets-pump receipt.
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


### Terminal host leave (rev7)

A graceful host stop queues RelayStopping (5), or HostSessionEnded (4), on the
runtime-to-broker pipe. The host transport flushes queued/overlapped writes and
waits up to 2.5 seconds for the broker Disconnected receipt before detaching.
This includes main's d63c1b0 flush, absent from the previously pinned rev6 products.
The broker queues a reliable private session-end envelope behind existing sends,
waits for its bounded pending queue and Steam pendingReliable/unacked bytes to
reach zero, then closes with native application reason 1005/1004, debug string
`KH2 host left`, and linger enabled. Drain is bounded at 2 seconds; timeout is
explicitly logged as `host-left-drain-timeout`, never claimed delivered. Native
quality queries and connection closes run only on the existing broker worker;
no game callback lock is held across SDK calls.

The joiner accepts a session-end envelope only from its authenticated target
connection, never from a friend on a host listener. It translates that message,
or the native application close reason, into the existing terminal client
recovery path (no rejoin). Reason 0, native generic 1000, and SDK transport
problems such as 5003 remain TransportLost and use recovery. This separates G8
host/session end from the G7 reusable broker attachment.

Valve's CloseConnection contract says application reason/debug reach the peer,
linger attempts to flush remaining reliable sends, and unread incoming data is
discarded on close. Consequently the terminal native reason is required even
when the reliable end message was sent: a status callback may overtake dequeue.
Reference: https://github.com/ValveSoftware/GameNetworkingSockets/blob/master/include/steam/isteamnetworkingsockets.h


Rev7 review corrections: terminal state and its single 9-byte end envelope are
kept outside the MaxQueue gameplay FIFO. IPC command consumption remains enabled
at a full send queue so Close/Ping are not starved by congestion; gameplay Send
still has the existing hard MaxQueue bound. The end envelope is sent after FIFO
gameplay drains. Timeout diagnostics report undelivered broker messages/bytes,
and attachment cleanup preserves an already requested terminal reason and linger.

An admitted peer's host-left terminal callback (reason 4/5) is retained while queued SDK messages drain
(up to 16 per tick, respecting IPC output room), before closing its handle and
emitting one Disconnected. A 2-second receive drain limit is explicitly logged;
IPC saturation continues to backpressure delivery. The native adapter permits
ClosedByPeer/ProblemDetectedLocally receive only on an owned handle whose earlier
Connected callback established encrypted authenticated relay, and checks each
retained message's handle and identity. It checks eligibility before dequeuing,
so an ineligible terminal handle cannot consume and discard a queued message.
Connection destruction/reset clears that trust. A session-end message and a
terminal callback coalesces into one disconnect; all earlier reliable gameplay
is emitted first. Admission and live Connected security gates are unchanged.

Generic transport loss/local disconnect keeps its existing immediate retirement
and stale-rejoin handling; delaying those would reject an incoming G7 rejoin as
a duplicate while its old connection was retained.
