# Reverse-Engineering Probe Checklist

This is the shortest list of things the live game bridge must discover before the networking work can become real.

## 1) Actor roots

Find stable access to:
- [x] slot 0 actor (`PLAYER`) — DONE: entity struct discovered via camera actor pointer chain + vtable scan. Position, rotation, velocity, airborne flags all mapped. See `archive/POSITION_PROBE_SESSION3.md`.
- [x] slot 1 actor (`FRIEND_1`) — DONE: discovered via Slot1+0x220/+0x228 friend entity pointers. Position, rotation, velocity mapped. See `../INPUT_RE_SESSION.md`.
- [x] slot 2 actor (`FRIEND_2`) — DONE: same mechanism as slot 1 (Slot1+0x228). See `../INPUT_RE_SESSION.md`.

For each actor, identify at least:
- [x] position — DONE (slot 0): `entity::POS_X/Y/Z` at +0x30/0x34/0x38
- [x] rotation — DONE (slot 0): `entity::ROT_Y` at +0x4C, `COS/SIN_FACING` at +0x40/0x48
- [x] velocity — PARTIAL (slot 0): `entity::VEL_Y` at +0xA4 (airborne Y only)
- [x] current motion / animation id — DONE: `actor+0x180` (DWORD), maps to OpenKH MotionSet enum (IDLE=0, WALK=1, RUN=2, etc.)
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
- [x] safe way to temporarily override follow target — DONE: allocate fake actor object (0x700 bytes), copy original, write position to +0x640+0x30, point `camStruct+0x50` at it. See `../CAMERA_RE_SESSION.md`.

**Success check:** ~~switch follow target between slot 0/1/2~~ DONE — camera retarget verified visually. Implemented in `WriteCameraTarget()` / `RestoreVanillaCamera()`.

---

## 3) Input path

- [x] where vanilla input is read — DONE: full pipeline traced from raw collection (`exe+0x105810`) through button mapping (`exe+0x39C720`) to processed state (`exe+0xBF31A0`). See `../INPUT_RE_SESSION.md`.
- [x] per-actor command path for movement/attack/jump/guard — DONE: friend AI dispatch via vtable+0x10, pre-physics via vtable+0x28, entity update chain fully mapped.
- [x] safe injection point for slot 1 and slot 2 — DONE: `PerEntityUpdate` hook at `exe+0x3BFD30` with friend entity identification via Slot1+0x220/+0x228.
- [x] suppress AI control for player-owned actors — DONE: vtable+0x10 (AI dispatch) and vtable+0x28 (pre-physics tether) both suppressed for player-owned friend entities.

**Success check:** DONE — F5 solo mode drives Friend1 locally with correct movement, facing, and no residual AI tether.

---

## 4) Room / progression state

- [x] world id — DONE: `0x717008`
- [x] room id — DONE: `0x717009`
- [x] spawn / program state — DONE: `MAP_PROGRAM`, `BATTLE_PROGRAM`, `EVENT_PROGRAM`
- [ ] transition start/end flags — TODO: room transitions detected by world/room change in `Tick()`, but explicit transition flags not mapped. **NOW commit** (staging `0x717018` → `axaAppMain+0x3A00`) documented in `../pointer_map_v1.md` (`NOW commit path`).
- [x] cutscene or event state — DONE: `CUTSCENE_TIMER` at `0x0B64F18`
- [ ] hashable state for post-transition comparison — TODO

**Success check:** PARTIAL — `ReadRoomState()` returns world/room/program/cutscene. `Tick()` detects room changes and re-discovers entities.

---

## 5) Enemy list

- [x] root enemy list / actor container — DONE: active entity list root at `exe+0x2A171C8`, next handle at `actor+0xA90`. See `IMPLEMENTATION_BACKLOG.md` M1 status.
- [ ] per-enemy object id — PARTIAL: `actor+0x918` exposes objentry record for stable `objectId` reads, but filtering logic still needs refinement.
- [ ] hp — TODO: enemy HP offset within entity struct not confirmed.
- [x] position / rotation — DONE: per-entity position at `entity+0x30/0x34/0x38`, rotation at `entity+0x4C`.
- [ ] motion or ai state — TODO
- [ ] alive / despawn / death transition — TODO

**Success check:** PARTIAL — entity list traversal works, position/rotation readable per entity. Enemy HP/spawn-group fields and death transitions still TODO.

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
