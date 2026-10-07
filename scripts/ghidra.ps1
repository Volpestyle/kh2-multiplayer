# ghidra.ps1
# Headless Ghidra for agents: build the analyzed KH2 project once, then ask
# for cross-references or decompiles by RVA without the Ghidra GUI.
#
# Usage:
#   .\scripts\ghidra.ps1 -Setup                       # import + full analysis (~5 min)
#   .\scripts\ghidra.ps1 -Xrefs 0x7435D0 [-Window 0x40] [-MaxDecomp 3]
#   .\scripts\ghidra.ps1 -Decompile 0x3C86A0,0x3BFD30
#   .\scripts\ghidra.ps1 -Strings "player attack","ATTACK@YS"   # strings + who uses them
#   .\scripts\ghidra.ps1 -Symbols ATTACK                        # RTTI classes, vftable slots
#   .\scripts\ghidra.ps1 -Dump 0x5C3420,32                      # qwords at an RVA
#
# RVAs are relative to the image base (0x140000000), matching
# docs/pointer_map_v1.md and KH2Offsets.hpp. The project lives in
# build/ghidra/kh2_full (gitignored) and is opened read-only for queries.
# Read-only queries still take kh2_full.lock: a second concurrent query fails
# with LockException. To query in parallel, copy kh2_full.gpr + kh2_full.rep
# to your scratch dir and run analyzeHeadless against the copy (2026-10-06).
#
# Ghidra: $env:GHIDRA_HOME, else the newest ~\ghidra_*_PUBLIC.
# Game:   $env:KH2_GAME_DIR, else the default Steam install.

param(
    [switch]$Setup,
    [string[]]$Xrefs,
    [string[]]$Decompile,
    [string[]]$Strings,
    [string[]]$Symbols,
    [string[]]$Dump,
    [string]$Window = "0x40",
    [int]$MaxDecomp = 3
)

$ErrorActionPreference = "Stop"
$RepoRoot   = Split-Path $PSScriptRoot -Parent
$ProjectDir = Join-Path $RepoRoot "build\ghidra"
$ScriptDir  = Join-Path $RepoRoot "tools\ghidra"
$Project    = "kh2_full"

function Find-Ghidra {
    if ($env:GHIDRA_HOME -and (Test-Path $env:GHIDRA_HOME)) { return $env:GHIDRA_HOME }
    $found = Get-ChildItem -Path $HOME -Directory -Filter "ghidra_*_PUBLIC" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $found) { throw "Ghidra not found: set GHIDRA_HOME" }
    return $found.FullName
}

$Headless = Join-Path (Find-Ghidra) "support\analyzeHeadless.bat"

# analyzeHeadless prints INFO noise and Java warnings on stderr; keep only the
# script's own output and Ghidra ERROR lines. PowerShell 5.1 turns native
# stderr into error records, so don't let them stop the script.
function Invoke-Headless([string[]]$HeadlessArgs) {
    $ErrorActionPreference = "Continue"
    $raw = @(& $Headless @HeadlessArgs 2>&1 | ForEach-Object { "$_" })
    $kept = @($raw | ForEach-Object {
        if ($_ -match "^INFO\s+\S+\.java> (.*?)\s*\(GhidraScript\)\s*$") { $Matches[1] }
        elseif ($_ -match "^ERROR ") { $_ }
    })
    if ($kept.Count -eq 0) {
        # A script compile error or bad project path prints nothing tagged:
        # show the raw tail rather than silently returning nothing.
        Write-Output "ghidra.ps1: no script output; last lines of analyzeHeadless:"
        $raw | Select-Object -Last 25
        exit 1
    }
    $kept
}

if ($Setup) {
    $gameDir = if ($env:KH2_GAME_DIR) { $env:KH2_GAME_DIR } else {
        "C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-" }
    New-Item -ItemType Directory -Force $ProjectDir | Out-Null
    # analyzeHeadless.bat mis-parses paths with parentheses, so import a copy.
    $exeCopy = Join-Path $ProjectDir "kh2fm.exe"
    Copy-Item (Join-Path $gameDir "KINGDOM HEARTS II FINAL MIX.exe") $exeCopy -Force
    Write-Host "Importing and analyzing (several minutes)..."
    & $Headless $ProjectDir $Project -import $exeCopy -overwrite -analysisTimeoutPerFile 6600 |
        Out-File (Join-Path $ProjectDir "analyze.log")
    Select-String -Path (Join-Path $ProjectDir "analyze.log") -Pattern "Analysis succeeded|Import succeeded|ERROR" |
        ForEach-Object { $_.Line }
    exit 0
}

if (-not (Test-Path (Join-Path $ProjectDir "$Project.gpr"))) {
    throw "No analyzed project at $ProjectDir - run: .\scripts\ghidra.ps1 -Setup"
}

$common = @($ProjectDir, $Project, "-process", "-noanalysis", "-readOnly", "-scriptPath", $ScriptDir)

if ($Xrefs) {
    $targets = $Xrefs | ForEach-Object { $_ -split "," } | Where-Object { $_ }
    Invoke-Headless ($common + @("-postScript", "XrefDecomp.java", $Window, "$MaxDecomp") + $targets)
} elseif ($Decompile) {
    $targets = $Decompile | ForEach-Object { $_ -split "," } | Where-Object { $_ }
    Invoke-Headless ($common + @("-postScript", "Decompile.java") + $targets)
} elseif ($Strings) {
    Invoke-Headless ($common + @("-postScript", "FindStrings.java") + $Strings)
} elseif ($Symbols) {
    Invoke-Headless ($common + @("-postScript", "FindSymbols.java") + $Symbols)
} elseif ($Dump) {
    $pairs = $Dump | ForEach-Object { $_ -split "," } | Where-Object { $_ }
    Invoke-Headless ($common + @("-postScript", "DumpData.java") + $pairs)
} else {
    Get-Help $MyInvocation.MyCommand.Path -Detailed
}
