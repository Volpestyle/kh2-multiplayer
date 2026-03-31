# Developer Setup

Step-by-step guide to go from zero to a working kh2-multiplayer dev environment.

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| **MSVC Build Tools** | C++20 support | Build the mod (`cl.exe` — via VS Build Tools or full VS install) |
| **CMake** | 3.20+ | Build system |
| **Python** | 3.11+ | MCP bridges (Ghidra, Cheat Engine) |
| **JDK** | 21+ | Ghidra (Eclipse Temurin recommended) |
| **Git** | any recent | Clone repos |
| **Cheat Engine** | 7.5+ | Runtime memory analysis / dynamic RE |
| **KH2 Final Mix PC** | **Steam Global** | Target game build |

### Install via winget (Windows)

```bash
winget install EclipseAdoptium.Temurin.21.JDK
```

CMake and Visual Studio should be installed manually or via Visual Studio Installer.

## Target Game Build

All memory offsets and pointer chains are verified against **KH2 Final Mix PC (Steam Global)**.
Other builds (Epic, Steam JP, PCSX2) have different base addresses — see `KH2Offsets.hpp` and the KH2 Lua Library for cross-version tables, but Steam Global is the primary development target.

## Repository Layout

The project expects several sibling repositories alongside `kh2-multiplayer/`. From the parent directory (e.g. `C:\Users\<you>\`):

```
├── kh2-multiplayer/        # This repo — the mod itself
├── openkh/                 # OpenKH modding toolkit (file formats, struct defs)
├── kh2-lua-library/        # Community KH2 runtime address tables
├── kh2-tools/
│   ├── KHPCPatchManager/   # Binary patch manager for KH2
│   ├── LuaBackend/         # Lua scripting engine source (hooks KH2 frame loop)
│   ├── mods/               # Reference character mods (axel-mix, dual-wield-roxas, vanitas)
│   └── archives/           # Original download ZIPs
├── GhidraMCP/              # Ghidra MCP bridge for AI-assisted RE
├── ghidra_12.0.4_PUBLIC/   # Ghidra installation
└── cheatengine-mcp-bridge/ # Cheat Engine MCP bridge for AI-assisted RE
```

### Clone everything

```bash
# The mod itself
git clone https://github.com/<org>/kh2-multiplayer.git

# Sibling repos
git clone https://github.com/OpenKH/OpenKh.git openkh
git clone https://github.com/aliosgaming/KH2-Lua-Library.git kh2-lua-library
git clone https://github.com/AntonioDePau/KHPCPatchManager.git kh2-tools/KHPCPatchManager
git clone https://github.com/Sirius902/LuaBackend.git kh2-tools/LuaBackend
```

## Building the Mod

```bash
cd kh2-multiplayer
cmake -B build -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
```

ENet (UDP networking library) is fetched automatically via CMake FetchContent.

### Build targets

| Target | Description |
|--------|-------------|
| `kh2coop_server` | Host-authoritative relay server |
| `kh2coop_runtime_scaffold` | Runtime entry point (attaches to live KH2) |
| `kh2coop_fake_sim` | End-to-end test harness (no KH2 needed) |

### Quick test (no KH2 required)

```bash
./build/kh2coop_fake_sim
```

## Reverse Engineering Tools

### Cheat Engine + MCP Bridge

Used for **dynamic analysis** — live memory inspection, breakpoints, AOB scans while KH2 is running.

1. Install [Cheat Engine](https://cheatengine.org/) 7.5+
2. Clone the MCP bridge:
   ```bash
   git clone https://github.com/miscusi-peek/cheatengine-mcp-bridge.git
   ```
3. Install Python dependencies:
   ```bash
   pip install mcp pywin32
   ```
4. In Cheat Engine: `File > Execute Script` and load `cheatengine-mcp-bridge/ce_mcp_bridge.lua`
5. Attach Cheat Engine to `KINGDOM HEARTS II FINAL MIX.exe`

### Ghidra + GhidraMCP

Used for **static analysis** — decompilation, cross-references, call graphs of the KH2 binary.

1. Download [Ghidra 12.0.4](https://github.com/NationalSecurityAgency/ghidra/releases) and extract it
2. Clone the MCP bridge:
   ```bash
   git clone https://github.com/LaurieWired/GhidraMCP.git
   ```
3. Download the extension from [GhidraMCP releases](https://github.com/LaurieWired/GhidraMCP/releases) (v1.4)
   - Extract the outer ZIP, then install the inner `GhidraMCP-1-4.zip` via Ghidra's `File > Install Extensions`
   - Ghidra 12 will warn about version mismatch — click "Install Anyway"
4. Restart Ghidra, create a project, import `KINGDOM HEARTS II FINAL MIX.exe`, let auto-analysis complete
5. Enable the plugin: `File > Configure > Developer > check GhidraMCPPlugin`
6. The HTTP server starts on port 8080

### MCP Configuration (for AI-assisted development)

Add both MCP servers so Claude Code / OpenCode / Codex can use them.

**Claude Code** (`~/.claude.json` → `mcpServers`):
```json
"cheatengine": {
  "type": "stdio",
  "command": "python",
  "args": ["<path>/cheatengine-mcp-bridge/MCP_Server/mcp_cheatengine.py"],
  "env": {}
},
"ghidra": {
  "type": "stdio",
  "command": "python",
  "args": ["<path>/GhidraMCP/bridge_mcp_ghidra.py", "--ghidra-server", "http://127.0.0.1:8080/"],
  "env": {}
}
```

**OpenCode** (`~/.config/opencode/opencode.json` → `mcp`):
```json
"cheatengine": {
  "type": "local",
  "command": ["python", "<path>/cheatengine-mcp-bridge/MCP_Server/mcp_cheatengine.py"],
  "enabled": true
},
"ghidra": {
  "type": "local",
  "command": ["python", "<path>/GhidraMCP/bridge_mcp_ghidra.py", "--ghidra-server", "http://127.0.0.1:8080/"],
  "enabled": true
}
```

**Codex** (`~/.codex/config.toml`):
```toml
[mcp_servers.cheatengine]
command = 'python'
args = ["-u", "<path>/cheatengine-mcp-bridge/MCP_Server/mcp_cheatengine.py"]

[mcp_servers.ghidra]
command = 'python'
args = ["-u", "<path>/GhidraMCP/bridge_mcp_ghidra.py", "--ghidra-server", "http://127.0.0.1:8080/"]
```

Replace `<path>` with actual absolute paths.

## Runtime Configuration

Copy the example config:
```bash
cp kh2coop_runtime.ini.example kh2coop_runtime.ini
```

Key settings:
- `runtime_mode` — `campaign_coop` (default) or `public_realm`
- `client_role` — `0` (Player), `1` (Friend1), `2` (Friend2)
- `camera_override` — enable local camera retargeting
- `panic_hotkey` — F8 to restore vanilla camera if things break

## Reference Material

| Resource | Location | Use for |
|----------|----------|---------|
| OpenKH | `../openkh` | File formats, entity types, party slot mapping, animation IDs, world IDs |
| KH2 Lua Library | `../kh2-lua-library` | Runtime memory addresses across all PC builds + PCSX2 |
| LuaBackend source | `../kh2-tools/LuaBackend` | Frame hook mechanism, Lua API for memory read/write |
| KHPCPatchManager | `../kh2-tools/KHPCPatchManager` | Mod packaging and distribution |
| Character mods | `../kh2-tools/mods/` | Reference implementations for entity spawning, party slots, model swaps |
| Project docs | `docs/` | Architecture, RE sessions, backlog, pointer map |

See `AGENTS.md` for detailed guidance on when to consult each resource.
