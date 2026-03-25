# Sim Loop Handoff

## What this is

You are implementing the **authoritative simulation loop** on the server. This is the core of the host-authoritative model: the server processes client inputs, updates world state, and broadcasts snapshots back to all clients.

## What already exists

The networking layer is complete and tested. You are building on top of it.

### Key files you'll work with
- `server/src/ServerMain.cpp` -- the server main loop. Currently calls `host.tick(16)` in a while loop with a TODO where the sim should go.
- `server/include/kh2coop/SessionHost.hpp` -- session manager. Has `broadcastActorSnapshots()`, `broadcastEnemySnapshots()`, `broadcastEvent()` ready to call.
- `server/include/kh2coop/PeerState.hpp` -- each peer has a `lastInput` field (InputFrame) that gets updated every time the server receives input.
- `common/include/kh2coop/Types.hpp` -- `ActorState`, `EnemyState`, `InputFrame`, `Vec3`, etc.
- `common/include/kh2coop/Protocol.hpp` -- `ActorSnapshot`, `EnemySnapshot`, `EventMessage`.
- `tests/FakeSimulation.cpp` -- existing end-to-end test. Extend this to verify your sim loop.

### What the server already does
- Accepts up to 3 ENet peers on port 7782
- Version-gates peers (build hash + mod hash)
- Assigns canonical slots (client declares desired slot in handshake)
- Receives `InputFrame` packets and stores them in `PeerState.lastInput`
- Can broadcast `ActorSnapshot`, `EnemySnapshot`, `EventMessage` to all verified peers
- Fires `SessionCallbacks.onInputReceived` when input arrives

### What the server does NOT do yet
- No simulation. Inputs are received but never applied to any state.
- No actor positions are tracked or updated.
- No snapshots are generated or broadcast on a cadence.
- No enemy simulation.

## What you need to build

### 1. SimulationState (new class)

Owns the authoritative world state:
- `ActorState actors[3]` -- one per slot (Player, Friend1, Friend2)
- `std::vector<EnemyState> enemies` -- enemy list (empty for now, can add later)
- `RoomState room` -- current room

### 2. SimulationState::applyInput(SlotType slot, const InputFrame& input)

For each tick, apply the latest input from each peer to their owned actor:
- Left stick -> position delta (simple: `position += stick * speed * dt`)
- Buttons -> action state transitions (attack, jump, guard, dodge)
- Keep it simple. This is a fake sim -- real game physics come from `IGameBridge` later.

### 3. SimulationState::tick(float dt)

Per-frame update:
- Apply gravity / basic physics if you want, or just do flat movement
- Update action state timers (e.g. attack lasts N frames then returns to idle)
- Increment snapshot ID

### 4. SimulationState::generateSnapshots() -> vector<ActorSnapshot>

Package each actor into an `ActorSnapshot` with the current snapshot ID.

### 5. Wire into ServerMain.cpp

The main loop currently looks like:
```cpp
while (g_running) {
    host.tick(16);
    // TODO: simulation goes here
}
```

Change it to:
```cpp
while (g_running) {
    host.tick(0);  // non-blocking poll

    // Collect latest inputs from each peer
    for (const auto& peer : host.peers()) {
        if (peer.status == PeerStatus::Verified) {
            sim.applyInput(peer.assignedSlot, peer.lastInput);
        }
    }

    sim.tick(dt);
    host.broadcastActorSnapshots(sim.generateSnapshots());

    // Sleep to maintain ~60Hz
    std::this_thread::sleep_for(std::chrono::milliseconds(16));
}
```

### 6. Extend FakeSimulation test

Add a test that:
1. Connects 3 clients
2. Client 0 sends InputFrames with leftStickX=1.0 (walking right) for 60 frames
3. Clients 1 and 2 send idle inputs
4. After 60 ticks, verify actor 0's position.x has increased
5. Verify actors 1 and 2 haven't moved
6. Verify all clients received ActorSnapshots with consistent positions

## Acceptance criteria

From `docs/ACCEPTANCE_TESTS.md` M4:
- Three peers stay in the same room for 60 seconds
- Mean positional error for remote actors stays under 1.0 meter
- Teleports/corrections are rare and visible in logs

Your test doesn't need a full 60-second run, but it should prove:
- Inputs cause position changes on the correct actor only
- Snapshots reach all clients
- Positions are consistent across clients (same snapshot = same data)

## Build and test

```
cmake --build build
./build/kh2coop_fake_sim
```

All existing 27 checks must still pass. Your new checks add to the count.

## Don't touch

- `common/` types and codec -- stable, don't change
- `NetworkClient` -- stable
- `SessionHost` internals -- use the public API only
- Anything in `runtime/` -- that's a separate workstream
