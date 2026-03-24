# KH2 Co-op Handoff Pack

This pack is meant to bridge the gap between the design draft and actual implementation work.

## What is in here
- `docs/IMPLEMENTATION_BACKLOG.md` — milestone plan from "can attach to game" to "3P GoA slice".
- `docs/AGENT_ASSIGNMENTS.md` — recommended split for parallel coding agents.
- `docs/ACCEPTANCE_TESTS.md` — concrete stop/go checks for each milestone.
- `docs/RE_PROBE_CHECKLIST.md` — what the reverse-engineering agent should find first inside the live game.
- `docs/agent_tasks.json` — machine-friendly task list.
- `common/` — shared packet/state structs.
- `runtime/` — starter interfaces for the injected client hook.
- `server/` — starter interfaces for the host-authoritative session server.
- `content/` — placeholder notes for the GoA test content pack.

## Recommended order
1. Do **not** start with world progression or cutscenes.
2. Lock a single supported KH2 PC build and one exact mod hash.
3. Build the `IGameBridge` pointer map first.
4. Get local camera retargeting working before any networking.
5. Prove 3 clients in one fixed room before touching transitions.
6. Only after stable single-room combat should you attempt room travel or story flags.

## Handoff boundary
The first thing that genuinely requires a live coding / reverse-engineering environment is:
- locating the actor structures for `PLAYER`, `FRIEND_1`, and `FRIEND_2`,
- locating the camera target / look-at control path,
- locating the input consumption path for friend slots,
- locating room/world/progress state and enemy lists.

Everything up to that point can still be specified and scaffolded from design docs.
