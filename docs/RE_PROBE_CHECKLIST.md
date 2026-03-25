# Reverse-Engineering Probe Checklist

This is the shortest list of things the live game bridge must discover before the networking work can become real.

## 1) Actor roots

Find stable access to:
- [x] slot 0 actor (`PLAYER`) — DONE: entity struct discovered via camera actor pointer chain + vtable scan. Position, rotation, velocity, airborne flags all mapped. See `POSITION_PROBE_SESSION3.md`.
- [ ] slot 1 actor (`FRIEND_1`) — TODO: entity struct discovery for friend slots not yet implemented.
- [ ] slot 2 actor (`FRIEND_2`) — TODO: same as slot 1.

For each actor, identify at least:
- [x] position — DONE (slot 0): `entity::POS_X/Y/Z` at +0x30/0x34/0x38
- [x] rotation — DONE (slot 0): `entity::ROT_Y` at +0x4C, `COS/SIN_FACING` at +0x40/0x48
- [x] velocity — PARTIAL (slot 0): `entity::VEL_Y` at +0xA4 (airborne Y only)
- [ ] current motion / animation id — TODO
- [x] action state — PARTIAL: `entity::MOVE_STATE` at +0x100 (2=ground, 3=air)
- [x] HP — DONE (all slots): `slot::HP` at unit slot base + 0x80
- [ ] MP / gauge values — TODO: MP offset within unit slot not confirmed
- [ ] current target pointer — TODO

**Success check:** ~~log these values once per frame in a test room~~ DONE for slot 0 — live-verified in Twilight Town (World 2, Room 32). Teleport via dual-write confirmed with 0.00 drift.

---

## 2) Camera control path

- [x] current camera target pointer or actor reference — DONE: `camStruct+0x50` (ACTOR_PTR), points to followed actor object
- [x] follow distance / angle — DONE: distance at `camStruct+0x58` (~500)
- [ ] "forced cinematic" or "event camera" state flag — TODO: `CAMERA_TYPE` at +0x48 exists but behavior during cutscenes not mapped
- [x] safe way to temporarily override follow target — DONE: allocate fake actor object (0x700 bytes), copy original, write position to +0x640+0x30, point `camStruct+0x50` at it. See `CAMERA_RE_SESSION.md`.

**Success check:** ~~switch follow target between slot 0/1/2~~ DONE — camera retarget verified visually. Implemented in `WriteCameraTarget()` / `RestoreVanillaCamera()`.

---

## 3) Input path

- [ ] where vanilla input is read — TODO (global address at `0x0BF3120` known, consumption path not traced)
- [ ] per-actor command path for movement/attack/jump/guard — TODO
- [ ] safe injection point for slot 1 and slot 2 — TODO
- [ ] suppress AI control for player-owned actors — TODO

**Success check:** not yet achieved.

---

## 4) Room / progression state

- [x] world id — DONE: `0x717008`
- [x] room id — DONE: `0x717009`
- [x] spawn / program state — DONE: `MAP_PROGRAM`, `BATTLE_PROGRAM`, `EVENT_PROGRAM`
- [ ] transition start/end flags — TODO: room transitions detected by world/room change in `Tick()`, but explicit transition flags not mapped
- [x] cutscene or event state — DONE: `CUTSCENE_TIMER` at `0x0B64F18`
- [ ] hashable state for post-transition comparison — TODO

**Success check:** PARTIAL — `ReadRoomState()` returns world/room/program/cutscene. `Tick()` detects room changes and re-discovers entities.

---

## 5) Enemy list

- [ ] root enemy list / actor container — TODO
- [ ] per-enemy object id — TODO
- [ ] hp — TODO
- [ ] position / rotation — TODO
- [ ] motion or ai state — TODO
- [ ] alive / despawn / death transition — TODO

**Success check:** not yet achieved. All enemy offsets in `KH2Offsets.hpp` are still `0x0`.

---

## 6) Reward / event points

- [ ] kill confirmation — TODO
- [ ] reward popups / drops / progression — TODO
- [ ] reliable event surface for KO/revive/reward — TODO

**Success check:** not yet achieved.

---

## Logging format recommendation
Every probe log line should include:
- frame number
- timestamp
- world id / room id
- actor slot
- value set being logged

That makes multi-client comparison much easier later.
