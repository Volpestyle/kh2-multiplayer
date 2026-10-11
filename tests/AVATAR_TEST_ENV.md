# Conditioned avatar relay fixture

`AvatarRelayTest.cpp::testEndToEnd` measures seeded 2% loss per hop through
`LinkConditioner` over real loopback ENet. ENet also has an adaptive unreliable
packet throttle driven by measured RTT. Local scheduling jitter can lower it
and introduce a second, unseeded loss model. On PC1 the unchanged main binary
reported 236 per mille; an observational transport adapter reproduced 260 per
mille with all four endpoints reaching throttle 20/32.

`AvatarConditionedTransport` fixes the throttle at full rate only for this
fixture, using ENet's peer throttle configuration with zero acceleration and
deceleration. It still uses the real ENet backend and the original seeded
conditioners. Existing loss, receive-count, interpolation, RTT and ownership
assertions are unchanged. Other fixtures and every production transport retain
their normal adaptive throttle. The adapter prints its minimum throttle and
number of unreliable sends below full rate to make a future fixture failure
distinguishable from the injected loss.

This identifies an environment-sensitive test assumption, not a specific OS
service or update responsible for PC1's changed scheduling. No firewall,
anti-cheat, power, timer or product setting is changed by the test fix.
