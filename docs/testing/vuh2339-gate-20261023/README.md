# VUH-2339 gate proof on bld, 2026-10-23 (gated agent at e5c6954)

Gate tests run on bld's real console session through the installed desk-agent. No game launch, no
Steam start, no save access.

| Test | Job | Result |
|---|---|---|
| a: refusal on recent input | `#gate: desk`, prints a line | `REFUSED gate: recent-input` (agent had just started) |
| b: launch refusal | launch level | `REFUSED gate: recent-input,steam-account` (Steam not running) |
| c / c2: injected input doesn't reset physical idle | `#gate: desk`; 100 injected 1 px mouse moves there and back over 20 s | admitted after 15 min idle; physicalIdle 924 -> 947 s while injectedIdle = 1 s; no reclaim; finished normally |
| d: reclaim ends the whole tree | `#gate: desk`; job -> powershell -> ping; then `C:\desk\gate\stop` | `RECLAIMED gate: stopped ok=True withinBudget=True`, tree ended at 0 ms, lease released at 47 ms after detection, no ping left |

Test c (before the review fixes, 14:19Z): physicalIdle 942 -> 964 s, injectedIdle 1, no reclaim.
Not covered here: a *physical* reclaim (needs a person at bld), a launch-level run (needs Steam B
signed in), the foreign-game/fullscreen/lane-lock refusals live.

[Raw evidence](clankie://evidence/sha256/96fae656b708e6b8ccabe3c9f82aab22cd4a16658c11f5338089e51b8e0ce781)
(`gate-proof.tar`): the four test jobs, their `.out` files, the reclaim receipt, `agent.log`,
`state.json` and the day's availability log. Only this README and `evidence.json` are committed.
