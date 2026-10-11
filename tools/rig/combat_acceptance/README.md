# VUH-1503 practice A profile

Executable per-role source and post-closure AC2 evidence checks; see
[the protocol, choreography and packet integration contract](../../../docs/VUH1503_ACCEPTANCE_PROFILE.md).
No import starts a process. The packet adapter owns existing launch/transport/
STOP/closure and must be independently sealed. Profile completion is never
all-criteria acceptance; AC3/native coverage is unchanged.

`adapter.PacketAdapter` projects sample/dispatch using only the source profile's
values; generate the packet declaration with `profile.adapter_contract()`.
`adapter.DriverCalls` binds the existing owned, deadline-bounded packet helper
path. Final authenticated transport/identity/media/closure services and actual
bundled Setup replay still need packet integration and review.
