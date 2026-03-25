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

## Playtest gate (Track A)
Do not expand scope beyond GoA Arena 3P until the team can complete:
- 10 minutes of stable combat,
- at least one reconnect,
- at least five successful room transitions,
- one complete bug report bundle with logs from all peers.

---
---

## Track B — Refactor for Scale

### B1 — Stable identity types
**Pass when**
- `PeerId`, `CharacterId`, `ActorNetId`, `InstanceId` are used in all new code paths.
- `SlotType` is only referenced inside co-op instance logic, not as durable identity.
- All existing tests still pass with no regressions.

**Fail when**
- New code still hardcodes slot indices as player identity.

---

### B2 — Session/instance split
**Pass when**
- A session can exist without an active instance (lobby/party-only state).
- `ClientHello` is used for handshake instead of `SessionState`.
- Instance creation is an explicit operation, not implicit on connect.

**Fail when**
- Connecting to the server immediately creates a gameplay instance.

---

### B4 — Runtime mode
**Pass when**
- `--mode campaign_coop` and `--mode public_realm` are recognized at startup.
- Boot log shows the active mode.
- Mode-specific code paths are cleanly gated.

**Fail when**
- The runtime crashes or ignores the mode flag.

---
---

## Track C — Public Realm v1

### C1 — Realm login and character roster
**Pass when**
- A client can log in, create a character, select it, and reconnect without losing the character.

**Fail when**
- Characters are lost on disconnect.

---

### C3 — Public hub instance
**Pass when**
- Multiple clients can join a hub room and see each other moving for 5 minutes.
- No combat, no crashes, no fatal desyncs.

**Fail when**
- Remote players are invisible or crash the room.

---

### C5 — Party adventure instance
**Pass when**
- A formed party enters an adventure instance and completes one encounter together.
- Rewards persist back to character records.

**Fail when**
- Party members end up in different instances.
- Rewards duplicate or vanish.

---

## Track B regression gate
**CampaignCoop behavior must remain unchanged after scale refactors.**

**Pass when**
- All existing `FakeSimulation` tests pass with zero regressions after Track B changes.
- 3-client integration test still connects, assigns slots, exchanges snapshots.
- Runtime scaffold still attaches to KH2, discovers entities, retargets camera.
- No Track B type or protocol change alters the v1 wire format.

**Fail when**
- Any existing test breaks after a Track B commit.
- A CampaignCoop session behaves differently than it did before the refactor.

This is a **hard gate** -- Track B refactors must not break Track A.

---

## Scope expansion gate
Do not begin Track C implementation until:
- Track A playtest gate passes.
- Track B refactors (B1, B2, B4) are complete.
- Track B regression gate passes.
- `ARCHITECTURE_MODES.md` is reviewed and accepted.
