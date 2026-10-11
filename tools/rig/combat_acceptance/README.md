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

Product-source candidate: c1b0469e/tree f50043b9, recorded in product-source.json
with references to Steam's existing products/completion receipts. No native
rebuild; the retained TEST_FAILURE disposition is unchanged. This candidate
binding does not qualify missing package tools or grant live authority.
