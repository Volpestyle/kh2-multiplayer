# Absolute enemy HP ordering

Introduced in protocol **7** and retained in current protocol **9**, each
host-authored `EnemyHp` sample has a nonzero 64-bit source sequence. The wire
payload is epoch, sequence, entry count, then absolute
netId/HP/maxHP entries. The DLL allocates the sequence at the existing checked
native `HostFrame` publication point, before enqueue. Failed enqueues consume
their sequence. Rooms, replacement manifests and world application retirement
do not restart the DLL-lifetime source counter; exhaustion stops HP publication
explicitly rather than wrapping. Runtime and relay do not stamp fresh numbers
onto old samples. Current components must use protocol 9, AvatarBridge 2,
WorldBridge 11 and CaptureChannel 1. The protocol 7 / WorldBridge 8 receipts below
remain frozen historical evidence.

The relay first checks host authority, complete framing and the current nonzero
room/manifest epoch. It then requires a sequence strictly greater than its last
admitted value for that host connection. Room changes and replacement manifests
clear their caches but retain this floor. A new authoritative host lifetime
starts a new sequence namespace. Malformed, wrong-epoch, zero and older packets
cannot modify either the cache or floor.

The client checks HP before raw world forwarding and typed callbacks. It rejects
smaller sequences and equal unreliable records, while allowing an equal
**reliable** replay from the trusted relay. The relay's targeted/late-join cache
is a union of the latest admitted absolute values per netId; it carries the last
admitted source sequence, without claiming all entries were sampled together.
Equal reliable replay repopulates the replacement manifest sent immediately
before it. It cannot lower the client's floor. Reliability is preserved through
the inbound link conditioner. The remembered session/host identity and floor
survive transient invalid rosters; readiness still blocks delivery. Genuine
transport or host-world replacement clears that namespace.

The DLL independently checks complete HP framing, current epoch and its floor
before updating desired HP. Its floor survives room changes, manifest
replacement, role-only retirement and duplicate matching reset markers. An
actual bridge header generation change retires that floor; the matching ordered
reset must still arm native authority before any HP is consumed. The independent
network floor continues guarding packets already in flight. The existing native
census, HP-write identity checks, explicit death path and fresh-actor anti-refill
guard remain in force.

The frozen protocol 6 baseline demonstrates confirmed HP 72 rolling back to 41
after an older same-room packet. Targeted cache replay and a late join also
received 41. Its original source, executable and three failures are retained in
`build/rig/enemy_hp_order_baseline_20261003` and
`hp_order_failed_baseline_receipt_20261003.json`.

Final protocol 7 Release and Windows ASan each passed 1,560 checks: 89 HP
ordering, 118 production native consumer/producer controls over owned memory,
596 desync, 119 capture/lease, 202 world, 127 recovery, 146 avatar, 85 simulation,
61 bridge and 17 activation. Portable ASan+UBSan passed 1,116 (89 HP ordering,
596 desync, 202 world, 127 recovery, 85 simulation and 17 activation); Windows
mailbox/native/avatar/bridge controls are excluded there. Nine prepared scenario
linkage cases and all three actual unattached RuntimeMain PID-0 smoke checks
passed. No sanitizer finding was observed. Runtime/relay/tools and both Windows
DLLs built; no DLL was installed or exercised against KH2. Exact source, logs,
reviews and results are in `build/rig/hp_order_offline_receipt_20261003.json`.

This ordering boundary alone does not make forced resync complete. The transaction introduced in protocol 8
implements fresh checked capture, per-target delivery fencing,
staged bootstrap and a distinct native convergence ACK. Offline controls and bounded local living Bootstrap/progress repair passed;
broader native recovery and remote acceptance remain pending
([FORCED_RESYNC.md](FORCED_RESYNC.md)). Its native world
source cut is separate from the HP sequence and fences delayed old records in
both directions. Reliable equal-sequence cache unions are still not fresh atomic
native snapshots. A source sequence identifies
HP ordering, not actor incarnation, native frame identity, an atomic snapshot or
dead-spawn reconstruction. Real native and remote acceptance remain separate.
