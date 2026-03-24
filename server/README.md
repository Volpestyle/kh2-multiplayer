# Server notes

The server should be able to run without KH2 attached.

## First concrete target
Simulate three fake players sending `InputFrame`s and verify the server can:
- maintain a session,
- enforce version hashes,
- emit periodic `ActorSnapshot`s,
- emit reliable `EventMessage`s.
