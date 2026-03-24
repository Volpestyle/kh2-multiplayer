# Runtime notes

Implement `IGameBridge` against the live KH2 process before attempting real netplay.

## First concrete target
Make a tiny diagnostic build that can print:
- room state
- slot 0/1/2 position
- camera target
- enemy count

Once that works, camera retargeting is the first live feature to attempt.
