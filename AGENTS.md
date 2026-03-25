### General Guidelines
- Refer to `docs/` as the primary source of truth, and always keep up to date with code changes
- Utilize cheat engine MCP when necessary
- Always write detailed commit messages

### Testing Principle
**Tests follow stabilization, not implementation.** Write regression tests only after an interface stops changing (e.g., codec is locked, offsets are confirmed via Cheat Engine). Do not write tests while actively discovering memory layouts or prototyping — that's churn. For live KH2 memory code (`GameBridgePC`, `CameraController`, etc.), manual smoke tests against a running game process are the real validation; you cannot meaningfully mock `ReadProcessMemory` against KH2. Reserve unit/integration tests for stabilized boundaries like the codec, protocol, and networking layer.

### OpenKH Reference (`../openkh`)
The OpenKH repository (https://github.com/OpenKH/OpenKh) is a sibling directory containing a KH2 modding toolkit.
See `docs/OPENKH_REFERENCE.md` for a full guide. Consult it when:

- **Looking up animation IDs** — `openkh/OpenKh.Kh2/MotionSet.cs` has the complete enum (IDLE=0, WALK=1, RUN=2, JUMP=3, etc.). Needed for animation sync.
- **Identifying entity types** — `openkh/OpenKh.Kh2/Objentry.cs` defines PLAYER=0, FRIEND=1, NPC=2, BOSS=3, ZAKO=4. Use during entity discovery RE.
- **Understanding party slot mapping** — `openkh/OpenKh.Kh2/SystemData/Memt.cs` defines which character occupies Friend1/Friend2 per world. Critical for friend entity RE.
- **Resolving character slot indices** — Sora=0, Donald=1, Goofy=2, Mickey=3, etc. Maps our SlotType to the game's internal indices.
- **Understanding attack/damage params** — `openkh/OpenKh.Kh2/Battle/Atkp.cs` for damage sync (M5).
- **Considering DLL injection** — `openkh/OpenKh.Research.Panacea/` has a working DLL injection + function hooking framework. If we need in-process hooks (camera stability, input injection), Panacea is the reference.
- **Looking up world IDs** — `openkh/OpenKh.Kh2/Constants.cs` maps all 19 world IDs to names.

Do NOT consult OpenKH for runtime memory offsets — those come from our Cheat Engine sessions and `KH2Offsets.hpp`. OpenKH's struct definitions are for on-disk file formats, not runtime memory layouts.

### KH2 Lua Library Reference (`../kh2-lua-library`)
A community-maintained collection of KH2 runtime memory addresses for all PC versions + PCSX2 (https://github.com/aliosgaming/KH2-Lua-Library). Already used as a source for many `[KH2LIB]`-tagged offsets in `KH2Offsets.hpp`. Consult it when:

- **Looking up addresses for non-Steam-Global builds** — `KH2EpicGlobal.lua`, `KH2SteamJP.lua`, `KH2Emulator.lua` have complete address tables for multi-version support.
- **Finding unit slot internal offsets** — The GoA example uses MP at `Slot+0x180/0x184`, invincibility at `Slot+0x1AE`, drive gauge at `Slot+0x1B0..0x1B2`.
- **Save file structure** — Complete inventory map (`Save+0x3580..0x36CA`), world progress flags, ability slots, equipment offsets.
- **Detecting game state** — `LoadingIndicator` (`0x8EC540`), `CurrentOpenMenu` (`0x7435D0`), `Continue` (`0x29FB500`).
- **Game version auto-detection** — Signature bytes at `0x566A8E` (Epic), `0x56668E` (Steam Global), `0x56640E` (Steam JP).

Do NOT consult for entity transforms, position data, camera struct details, animation IDs, enemy lists, or friend entity discovery — those require original Cheat Engine RE and are documented in our own `KH2Offsets.hpp`.
