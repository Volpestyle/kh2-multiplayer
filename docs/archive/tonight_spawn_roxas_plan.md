# Tonight Plan: Spawn Extra Roxas (Local Prototype)

Scope: local single-room proof of concept only (no multiplayer sync tonight).

## Session Goal

Spawn one extra Roxas in a fixed test room and capture enough evidence to make it reproducible.

---

## 1) Lock test environment (15 min)

- [ ] Use one fixed KH2 build only
- [ ] Use one fixed mod setup only
- [ ] Pick one fixed test room (no transitions during tests)
- [ ] Start a timestamped log file with:
  - [ ] build hash
  - [ ] mod hash
  - [ ] room label/id

Exit criteria:
- Environment details are written down before any probing.

---

## 2) Baseline probe capture (20 min)

Capture baseline values every second for ~30 seconds:

- [ ] `world_id`
- [ ] `room_id`
- [ ] `map_program`
- [ ] `battle_program`
- [ ] `event_program`
- [ ] party slot references (0/1/2) if available
- [ ] current actor/enemy count if available

Exit criteria:
- You have a "known-good idle room" baseline log.

---

## 3) Confirm spawn path from an existing entity (45 min)

- [ ] Identify the *known* Roxas spawn in the current room
- [ ] Pick a "spawn sentinel" value `A` (something that flips when Roxas spawns)
  - [ ] Prefer simplest values you can observe reliably:
    - [ ] HP / alive flag (if you can damage Roxas)
    - [ ] OR position/rotation floats (always change after spawn)
  - [ ] In CE: find `A` by narrowing:
    - [ ] Load the room with Roxas already present (or wait until he appears)
    - [ ] Narrow by a value you can repeat (HP change, or x/y/z change due to a small movement/interaction)
- [ ] Confirm `A` is Roxas-specific (validation by causing a change that affects only Roxas)
  - [ ] If you damage Roxas, `A` should change accordingly
  - [ ] If you teleport/move Roxas, `A` should track that movement
- [ ] Set a breakpoint/watch on `A` and capture spawn parameters at the moment Roxas is created/activated
  - [ ] In CE: "Find out what writes to `A`" (or set breakpoint on write if available)
  - [ ] Reload the room and let Roxas spawn happen again
  - [ ] When the watch triggers, record:
    - [ ] the newly created actor/enemy pointer (spawn result pointer)
    - [ ] `object id` (spawn object/model id) if visible in the breakpoint/register context
    - [ ] `spawn group id` (the group/script/controller id used for the spawn) if visible
    - [ ] initial position/rotation for the spawned entity (read nearby floats around the result pointer)
- [ ] Repeat / reload to verify repeatability
  - [ ] Roxas spawn should trigger the breakpoint again
  - [ ] The captured sequence should be consistent:
    - [ ] same sentinel `A` behavior
    - [ ] same "result pointer pattern" (new actor pointer changes per reload, but should be consistently produced by the same code path)
    - [ ] same object id and spawn group id (or extremely stable equivalents)

Exit criteria:
- You can consistently observe one normal Roxas spawn end-to-end and produce:
  - [ ] a reproducible sentinel `A`
  - [ ] a reproducible spawn-result pointer source
  - [ ] stable `object id` / `spawn group id` (or the closest stable equivalents visible)

What to send me after each breakpoint hit (so we can deduce the actual spawn surface):
- `result_actor_ptr` (address/value you identified as the spawned Roxas entity)
- `object id` (if visible) and `spawn group id` (if visible)
- `pos/rot x,y,z` read near `result_actor_ptr` (even if offsets are rough/temporary)
- Any stack/callsite info CE shows (function name/module + the first few frames if available)

---

## 4) Attempt extra Roxas spawn (45 min)

- [ ] Trigger one controlled Roxas spawn in the same room
- [ ] Immediately capture:
  - [ ] spawned actor pointer
  - [ ] object/model id
  - [ ] HP/state flags
  - [ ] position/rotation
- [ ] Observe for at least 20-30 seconds

Success criteria:
- Roxas appears visually
- no immediate crash
- no instant despawn loop

---

## 5) Stabilize if partial success (30 min)

If he spawns but glitches/despawns:

- [ ] Test small spawn position offsets
- [ ] Test spawn timing (avoid transition/cutscene windows)
- [ ] Test forcing a safe initial state (idle-ish behavior)
- [ ] Change only one variable per attempt

Exit criteria:
- Either stable behavior improves, or failure mode is clearly characterized.

---

## 6) Writeback and tracker update (15 min)

- [ ] Update `docs/pointer_map_v1.md` with:
  - [ ] newly confirmed values
  - [ ] still-unknown values
  - [ ] exact pointers/offsets observed
  - [ ] reproducible steps and outcomes

Exit criteria:
- Another session can continue from your notes with no guesswork.

---

## Hard stop rules

- If a step fails 3 times with distinct inputs, log and move on.
- Do not add room transitions until single-room spawn is stable.
- Do not start multiplayer sync work tonight.

---

## End-of-night deliverables

- [ ] Baseline + spawn-attempt log file
- [ ] One reproducible "spawn Roxas" method OR one documented failure mode with captured evidence
- [ ] Updated `docs/pointer_map_v1.md`

