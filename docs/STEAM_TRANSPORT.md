# Steam transport work (VUH-1493)

Steam connectivity is not available to friends yet. ENet remains the default
and only implemented packet transport. Protocol10, its channel assignments,
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
This broker and its runtime link are not implemented by the transport extraction.

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
