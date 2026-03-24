# Acceptance Tests

## Global preflight
- Same KH2 build hash on all clients.
- Same content/mod hash on all clients.
- Same protocol version on all clients.
- Runtime attach succeeds on all clients.
- Host and peers agree on session id.

---

## M0 — Runtime attach
**Pass when**
- Runtime boots 10 times without crashing.
- World/room logging appears.
- Feature flags can disable all invasive hooks.

**Fail when**
- Attach success depends on timing luck.
- Logging only works in some rooms.

---

## M1 — Pointer map
**Pass when**
- Slot 0/1/2 positions change in logs as expected.
- Camera target can be read.
- Enemy count is correct in a controlled room.

**Fail when**
- Read values drift wildly or alias the wrong actor.

---

## M2 — Camera retarget
**Pass when**
- Three local clients can each follow a different owned slot.
- A forced camera/cutscene temporarily overrides local follow.
- The panic hotkey restores vanilla camera behavior.

**Fail when**
- Camera constantly snaps back.
- Retarget breaks event cameras.

---

## M3 — Local friend-slot control
**Pass when**
- Slot 1 and slot 2 can move, attack, and dodge offline.
- Wrong-slot control never occurs during 5 minutes of play.
- Input overlay matches observed behavior.

**Fail when**
- Input bleeds into slot 0.
- Camera ownership and input ownership disagree.

---

## M4 — 3-client movement sync
**Pass when**
- Three peers stay in the same room for 60 seconds.
- Mean positional error for remote actors stays under 1.0 meter.
- Teleports/corrections are rare and visible in logs.

**Fail when**
- A peer drifts into a different room state.
- Remote actors jitter uncontrollably.

---

## M5 — Shared combat
**Pass when**
- The same enemy pack spawns on all three clients.
- Enemy HP is visually and numerically consistent.
- Kills and rewards happen once, not once per client.
- A downed player state is reflected to all peers.

**Fail when**
- Enemies are alive on one client and dead on another.
- Rewards duplicate or disappear.

---

## M6 — Resync and reconnect
**Pass when**
- A disconnected peer can rejoin the lobby without rebooting the host.
- Host can force a session resync.
- The rejoined peer lands in the correct room and sees the correct enemy set.

**Fail when**
- Reconnect requires recreating the session every time.

---

## M7 — Controlled transitions
**Pass when**
- 20 repeated transitions between the chosen test rooms complete without room divergence.
- All peers report matching world/room/program hashes after each load.

**Fail when**
- Any peer remains in the old room.
- Actor ownership is lost after transition.

---

## Playtest gate
Do not expand scope beyond GoA Arena 3P until the team can complete:
- 10 minutes of stable combat,
- at least one reconnect,
- at least five successful room transitions,
- one complete bug report bundle with logs from all peers.
