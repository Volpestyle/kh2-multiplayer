# Causal diagnostic landing preparation

The separate `story/combat-causal-landing` branch rebases the nine diagnostic
commits through `19aae6cd` onto main `9cf06ec`. The original branch and sealed
rev2 products remain unchanged. Native05 passed all 60 tests on the original
source; that evidence does not qualify this rebased tree. A fresh native build
and both independent reviews remain required before landing.

Integration decisions:

- `CMakeLists.txt` merged automatically. Keep all main targets and its dynamic
  CTest registration, adding the diagnostic test target without restoring the
  old test inventory or hardcoding the previous count of 60.
- `WorldPump.hpp` merged automatically. Keep protocol 15/16 routing and the
  PopulationCut send/log branch. Run the optional noexcept diagnostic observer
  after that branch, preserving main's send results and statistics.
- `EnemySync.cpp` required manual resolutions. Keep the complete record
  population capture/resolution functions before the separate causal helpers.
  Keep main's record-authority HP log/readback and then run the optional causal
  readback. Keep PopulationCut dispatch immediately after the authenticated
  scope gate; add the rejection observation before the gate's existing continue.
- `EntityHook.cpp` merged automatically. Keep main's Limit admission install,
  ready checks, reinitialization refusals and shutdown ownership. Only add the
  existing opt-in diagnostic QPC configuration and enable hit tracing when that
  diagnostic is requested; no old Limit hooks are restored.

The diagnostic engine, NativeHitTrace implementation/header and three diagnostic
control files match the original source byte-for-byte. Claim, HP and death wire
tags/layouts used by the diagnostic are unchanged by protocol 15/16; main adds
manifest record identity and PopulationCut independently. No deliberate change
to causal admission, qualification, bounded raw receipts, loss retirement or
default-off behavior is intended. Main's newer population and record-authority
work can change when observations run; retained byte equality is not a proof
that the combined implementation behaves identically. In particular, main's
record-authority logging precedes the diagnostic post-store readback.

Offline checks: clean original branch still at `19aae6cd`, main ancestry,
range-diff inspection, unchanged diagnostic/control blobs, unchanged main
protocol/codec and Limit admission blobs, preserved main record-population
functions, and `git diff --check`. No native commands, push, PC2 contact or
live actions were performed for this landing preparation.
