$ErrorActionPreference = "Stop"

$repoRoot = Split-Path $PSScriptRoot -Parent
Set-Location $repoRoot

python "$repoRoot\tools\mcp_kh2ctl\server.py"
