# World Map and Gummi travel (VUH-2341)

The World Map and the Gummi Ship are travel, not rooms a friend can share. While
the host travels, friends hold in the room the host left, paused with "Waiting
for host...". When the host lands, they follow it into the same room through
the ordinary host-led load.

## What the game does

Observed live on 2026-10-17 from save slot FM-03 (BC Parlor, past HB 1) with two
local instances. Run: `build/scenarios/20261017-020254_net_worldmap_observe_1`.

- **Map entry:** the save point's Triangle menu → World Map → Yes. The host makes
  one native `RequestTransition` to world `0x0F` (door `0xFF04`). The load then
  commits `0F/00` door 4. On the map IN_FIELD reads 1, so an IN_FIELD check alone
  would treat the map as a field.
- **The first LoD trip needs a Gummi route.** In FM-03, Land of Dragons sits
  behind the uncleared Asteroid Sweep gate, which offers "Proceed". The ship
  cannot reach LoD around the gate.
- **Gummi menu and flight:** NOW stays `0F/00` door 4 throughout. IN_FIELD drops
  to 0, and no room load happens until the flight ends.
  - The flight is an on-rails shooter. Its pause menu has Continue, Retry,
    Rapid Fire (Semi-Auto / Full Auto), Gummi Menu and World Map.
  - Being shot down restarts the route.
  - Returning to the map is a native reload to `0F/00` door 13.
- **Landing:** World Menu → destination. Hollow Bastion → Merlin's House loaded
  `04/0D` door 99 (`0x63`), map 1, evt 21. TCM Murray's notes report the same
  door `0x63` for LoD's arrival room `08/00` (`VERIFIED_OFFSETS.md` row "World
  Map room and arrivals"; read only, no code taken).

## How co-op handles it

- **Host announces travel at once:** `warp` sees the host's own request for
  world `0x0F` and calls `enemysync::OnHostTravelRequest()` from the request hook.
  At that moment NOW still names the room being left, so the host publishes an
  event-hold acquire for that room's epoch.
  - This does not depend on the map running entity frames.
  - It is the existing `KH2COOP_EVENT_HOLD_PRODUCER` / `_CONTROL` path, which the
    player launcher ships on.
- **Travel counts as an active hold:** it lasts from that request until a load
  completes in another world (`warp::HostTravelling()`). The host releases after
  it lands, under the ordinary release rules.
- **The map is never a room:** a host on `0F` does not publish a
  `RoomTransition`, and a client ignores one if it arrives.
- **The client's side is unchanged:** it opens its own pause for the hold and
  neutralises input. The runtime's control channel wakes it on the host's
  landing room, so the pause closes and the client loads that room with the
  host's progress applied. Its input returns after convergence.
  - Client native exits were already blocked, so a held client cannot change
    location.
  - The overlay now reads "Waiting for host..." for events and travel alike.

## Evidence and gaps

- **Host to the map (passed):** the friend paused at once in BC Parlor. It
  ignored Down, Start and Cross, and stayed in `05/01`.
- **Gummi menu and flight (passed):** the friend stayed held.
- **Host landing (passed):** the host landed in HB `04/0D` door 99. The friend
  woke, applied progress, loaded the same room, door and programs, and walked
  once released.
- **Asteroid Sweep (open):** the route was not cleared in that run, so a landing
  in LoD after a Gummi flight is still to be shown.
- **Scenario runner:** stalled field frames read as a hang. On the map the
  frame counter can stop, so `net_worldmap_observe` turns the check off with
  the `hang_check` step.
