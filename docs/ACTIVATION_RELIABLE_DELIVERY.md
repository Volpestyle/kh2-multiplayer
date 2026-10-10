# Reliable room activation delivery

This separate descendant of the ordinary-lifecycle candidate promotes both
ActivationRequest and HostActivationPoint to the existing reliable ordered
transport channel. It can be omitted by building the lifecycle parent alone.
No packet fields or protocol version change in this delta.

The old NetworkClient helper and relay forward paths used unreliable delivery.
The production WorldPump also submits activation as periodic/unreliable, so
changing only the helper would miss the game path. `sendNativeWorld` now promotes
these two types centrally; the helpers and both relay legs explicitly request
reliability. HP, motion and other periodic traffic retain their existing choices.

There is no application retry, lease renewal, cached activation, rebaseline or
changed sequencing rule. Existing pending-request deduplication and single-use
response routing remain intact. Original connection/session, room/epoch,
incarnation and source-sequence checks still apply. Reliable retransmission may
arrive after the original 500 ms lease: it must then refuse. This improves loss
tolerance; it does not promise activation under unbounded latency or fix all
observed avatar/world failures.

The existing `kh2coop_world_test` adds a deterministic first-unreliable-drop
control and runs the real client/relay request/reply under 100% simulated
unreliable loss on both clients in both directions. It exercises the actual
DLL-style `sendNativeWorld(...,false)` path, duplicate request and response,
stale-room request and foreign source connection. LinkConditioner models
reliable recovery through delivery rather than physically dropping an ENet
datagram; this is not a measurement of real loss or retransmission latency.
Existing expiry, same-room reload, requester reconnect and host-loss controls
remain in that target.

Story has not compiled or run the native test. kh2-steam owns the source-matched
build/CTest slot. A passing deterministic control is not live qualification.
