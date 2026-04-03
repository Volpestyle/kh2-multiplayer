# Development Workflow

How to build, test, and iterate on the inject DLL against a running KH2 instance.

## Prerequisites

- KH2 HD 1.5+2.5 ReMIX (Steam Global)
- Visual Studio 2019+ build tools (for CMake/MSVC)
- Cheat Engine 7.x (for DLL injection and runtime analysis)
- `steam_appid.txt` containing `2552430` in the KH2 game directory (bypasses Steam launcher)

## The development loop

```
1. Edit code (inject/src/EntityHook.cpp)
2. Kill KH2          →  .\scripts\restart-kh2.ps1 -Kill
3. Build             →  cmake --build build --target kh2coop_inject --config Release
4. Relaunch KH2      →  .\scripts\restart-kh2.ps1 -NoBuild
5. Load a save (get into gameplay with Donald in party)
6. Attach CE to KH2
7. Inject DLL via CE Lua console (see below)
8. Press F5 to toggle solo mode
9. Test, observe logs, repeat from step 1
```

Steps 2-4 can be combined:
```powershell
.\scripts\restart-kh2.ps1           # kill + rebuild + relaunch (all-in-one)
```

## DLL injection (CE Lua console)

```lua
openProcess("KINGDOM HEARTS II FINAL MIX.exe")
local loadLibA = getAddress("kernel32.LoadLibraryA")
local mem = allocateMemory(512)
writeString(mem, "C:\\Users\\volpe\\kh2-multiplayer\\build\\inject\\staging\\kh2coop_inject.dll", false)
createRemoteThread(loadLibA, mem)
```

Update the DLL path if your build output is elsewhere.

## Why you must kill KH2 before rebuilding

The inject DLL is loaded into the KH2 process. The linker can't overwrite a DLL that's in use. You must kill KH2 before rebuilding. The restart script handles this automatically.

## Log file

The inject DLL writes to `kh2coop_inject.log` in the KH2 game directory:
```
C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\kh2coop_inject.log
```

Check this after injection for:
- Hook installation success/failure
- Friend entity detection
- Animation override events
- Movement injection diagnostics
- Periodic status (every 300 frames)

## Quick reference: restart script flags

| Command | What it does |
|---------|-------------|
| `.\scripts\restart-kh2.ps1` | Kill + rebuild inject DLL + relaunch |
| `.\scripts\restart-kh2.ps1 -NoBuild` | Kill + relaunch only (DLL already built) |
| `.\scripts\restart-kh2.ps1 -Kill` | Kill only (no relaunch) |
| `.\scripts\restart-kh2.ps1 -CopyDll` | Also copy DLL to game directory |

## Building other targets

```powershell
cmake --build build --target kh2ctl --config Release           # CLI tool
cmake --build build --target kh2coop_server --config Release   # multiplayer server
cmake --build build --target kh2coop_fake_sim --config Release # E2E test (no KH2 needed)
cmake --build build --config Release                           # everything
```

## Testing without KH2

The E2E test (`kh2coop_fake_sim`) exercises the codec, networking, and server with 3 simulated clients. No running KH2 instance needed:

```powershell
.\build\tests\Release\kh2coop_fake_sim.exe
```

## Using kh2ctl

The CLI tool for automated KH2 control. Requires the runtime or inject DLL to be attached.

```powershell
.\build\tools\kh2ctl\Release\kh2ctl.exe status          # check if attached
.\build\tools\kh2ctl\Release\kh2ctl.exe room             # current world/room
.\build\tools\kh2ctl\Release\kh2ctl.exe player-pos       # Sora's position
.\build\tools\kh2ctl\Release\kh2ctl.exe boot-load-save 1 # restart KH2 + load save 1
```

See `docs/KH2_CONTROL_CLI.md` for the full command reference.

## RE workflow (Cheat Engine + Ghidra)

When reverse engineering game internals:

1. **Find the address** — CE scans, data breakpoints, pointer chains
2. **Understand the code** — Ghidra decompile at the address, trace xrefs
3. **Verify live** — CE breakpoints to confirm behavior matches decompilation
4. **Document** — Add offset to `KH2Offsets.hpp`, update `pointer_map_v1.md`
5. **Hook it** — Add to `EntityHook.cpp` if needed

See `docs/LESSONS_LEARNED.md` for hard-won RE insights.
