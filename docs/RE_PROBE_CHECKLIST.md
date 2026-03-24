# Reverse-Engineering Probe Checklist

This is the shortest list of things the live game bridge must discover before the networking work can become real.

## 1) Actor roots
Find stable access to:
- slot 0 actor (`PLAYER`)
- slot 1 actor (`FRIEND_1`)
- slot 2 actor (`FRIEND_2`)

For each actor, identify at least:
- position
- rotation
- velocity if available
- current motion / animation id
- action state or equivalent
- HP / MP / gauge values
- current target pointer or target id if available

**Success check:** log these values once per frame in a test room and verify they track the correct visible character.

---

## 2) Camera control path
Find:
- current camera target pointer or actor reference
- follow distance / angle if there is a stable hook point
- any "forced cinematic" or "event camera" state flag
- a safe way to temporarily override follow target

**Success check:** switch follow target between slot 0/1/2 at runtime without crashing.

---

## 3) Input path
Find:
- where vanilla input is read
- whether there is a later per-actor command path for movement/attack/jump/guard
- a safe injection point for slot 1 and slot 2 direct control
- how to suppress unintended AI control if the actor is player-owned

**Success check:** in a local offline build, slot 1 or slot 2 can move and attack when bound to a test pad.

---

## 4) Room / progression state
Find:
- world id
- room id
- spawn / program state
- transition start/end flags
- cutscene or event state
- any hashable state that can be compared after transitions

**Success check:** logs clearly show room changes and transition boundaries.

---

## 5) Enemy list
Find:
- root enemy list / actor container
- per-enemy object id
- hp
- position / rotation
- motion or ai state
- alive / despawn / death transition

**Success check:** a fixed encounter can be enumerated every frame.

---

## 6) Reward / event points
Find:
- where kill confirmation is reflected
- where reward popups / drops / progression are triggered
- whether there is a safe reliable event surface for KO/revive/reward

**Success check:** one known event can be detected and logged exactly once.

---

## Logging format recommendation
Every probe log line should include:
- frame number
- timestamp
- world id / room id
- actor slot
- value set being logged

That makes multi-client comparison much easier later.
