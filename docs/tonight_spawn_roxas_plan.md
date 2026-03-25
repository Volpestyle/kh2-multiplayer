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

- [ ] Identify a known spawn path used by the current room
- [ ] For one known spawn, capture:
  - [ ] object id
  - [ ] spawn group id
  - [ ] resulting actor pointer
  - [ ] initial position/rotation
- [ ] Repeat/reload and verify the same path is observable

Exit criteria:
- You can consistently observe one normal spawn end-to-end.

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

