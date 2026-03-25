### General Guidelines
- Refer to `docs/` as the primary source of truth, and always keep up to date with code changes
- Utilize cheat engine MCP when necessary
- Always write detailed commit messages

### OpenKH Reference (`../openkh`)
The OpenKH repository is a sibling directory containing a KH2 modding toolkit.
See `docs/OPENKH_REFERENCE.md` for a full guide. Consult it when:

- **Looking up animation IDs** — `openkh/OpenKh.Kh2/MotionSet.cs` has the complete enum (IDLE=0, WALK=1, RUN=2, JUMP=3, etc.). Needed for animation sync.
- **Identifying entity types** — `openkh/OpenKh.Kh2/Objentry.cs` defines PLAYER=0, FRIEND=1, NPC=2, BOSS=3, ZAKO=4. Use during entity discovery RE.
- **Understanding party slot mapping** — `openkh/OpenKh.Kh2/SystemData/Memt.cs` defines which character occupies Friend1/Friend2 per world. Critical for friend entity RE.
- **Resolving character slot indices** — Sora=0, Donald=1, Goofy=2, Mickey=3, etc. Maps our SlotType to the game's internal indices.
- **Understanding attack/damage params** — `openkh/OpenKh.Kh2/Battle/Atkp.cs` for damage sync (M5).
- **Considering DLL injection** — `openkh/OpenKh.Research.Panacea/` has a working DLL injection + function hooking framework. If we need in-process hooks (camera stability, input injection), Panacea is the reference.
- **Looking up world IDs** — `openkh/OpenKh.Kh2/Constants.cs` maps all 19 world IDs to names.

Do NOT consult OpenKH for runtime memory offsets — those come from our Cheat Engine sessions and `KH2Offsets.hpp`. OpenKH's struct definitions are for on-disk file formats, not runtime memory layouts.
