# restart-kh2.ps1
# Quickly kill and relaunch KH2 Final Mix for inject/script iteration.
# Bypasses the HD 1.5+2.5 launcher so you don't have to click through Steam.
#
# Usage:
#   .\scripts\restart-kh2.ps1            # kill + relaunch, wait for process
#   .\scripts\restart-kh2.ps1 -Kill      # kill only, don't relaunch
#   .\scripts\restart-kh2.ps1 -Steam     # relaunch via Steam (goes through launcher)
#   .\scripts\restart-kh2.ps1 -NoBuild   # skip the build step
#   .\scripts\restart-kh2.ps1 -CopyDll   # also copy inject DLL before launch

param(
    [switch]$Kill,       # Kill only, don't relaunch
    [switch]$Steam,      # Use steam:// protocol instead of direct exe launch
    [switch]$CopyDll,    # Copy kh2coop_inject.dll to game dir before launching
    [switch]$NoBuild     # Skip the CMake rebuild step
)

# --- Configuration -----------------------------------------------------------
$KH2_PROCESS   = "KINGDOM HEARTS II FINAL MIX"
$LAUNCHER_PROC = "KINGDOM HEARTS HD 1.5+2.5 Launcher"
$REMIXMAIN_PROC = "KINGDOM HEARTS HD 1.5+2.5 ReMIX"

$GAME_ROOT = "C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-"
$KH2_EXE   = Join-Path $GAME_ROOT "KINGDOM HEARTS II FINAL MIX.exe"

$STEAM_APPID = "2552430"  # KH HD 1.5+2.5 ReMIX

# Project paths (relative to this script's location)
$PROJECT_ROOT = Split-Path (Split-Path $PSScriptRoot -Parent) -Leaf
$PROJECT_DIR  = Split-Path $PSScriptRoot -Parent
$BUILD_DIR    = Join-Path $PROJECT_DIR "build"
$INJECT_DLL   = Join-Path $BUILD_DIR "inject\Release\kh2coop_inject.dll"
if (-not (Test-Path $INJECT_DLL)) {
    $INJECT_DLL = Join-Path $BUILD_DIR "inject\Debug\kh2coop_inject.dll"
}

# --- Helpers -----------------------------------------------------------------
function Write-Status($msg) { Write-Host "[restart-kh2] $msg" -ForegroundColor Cyan }
function Write-OK($msg)     { Write-Host "[restart-kh2] $msg" -ForegroundColor Green }
function Write-Warn($msg)   { Write-Host "[restart-kh2] $msg" -ForegroundColor Yellow }
function Write-Err($msg)    { Write-Host "[restart-kh2] $msg" -ForegroundColor Red }

# --- Step 1: Kill existing processes -----------------------------------------
Write-Status "Killing KH2 processes..."

$killed = $false
foreach ($name in @($KH2_PROCESS, $LAUNCHER_PROC, $REMIXMAIN_PROC)) {
    $procs = Get-Process -Name $name -ErrorAction SilentlyContinue
    if ($procs) {
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        Write-Status "  Killed: $name (PID $($procs.Id -join ', '))"
        $killed = $true
    }
}

if (-not $killed) {
    Write-Status "  No KH2 processes were running."
}

# Wait for processes to fully exit
$timeout = 10
$elapsed = 0
while ($elapsed -lt $timeout) {
    $remaining = Get-Process -Name $KH2_PROCESS -ErrorAction SilentlyContinue
    if (-not $remaining) { break }
    Start-Sleep -Milliseconds 500
    $elapsed += 0.5
}

if ($elapsed -ge $timeout) {
    Write-Err "KH2 process did not exit within ${timeout}s. You may need to kill it manually."
    exit 1
}

Write-OK "All KH2 processes stopped."

if ($Kill) {
    Write-OK "Done (kill-only mode)."
    exit 0
}

# --- Step 2: Optional rebuild ------------------------------------------------
if (-not $NoBuild -and (Test-Path (Join-Path $BUILD_DIR "build.ninja") ) -or (Test-Path (Join-Path $BUILD_DIR "*.sln"))) {
    Write-Status "Rebuilding inject DLL..."
    $buildResult = & cmake --build $BUILD_DIR --target kh2coop_inject --config Release 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-OK "Build succeeded."
    } else {
        Write-Warn "Build failed (exit code $LASTEXITCODE). Continuing with existing DLL..."
        $buildResult | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" -ForegroundColor DarkYellow }
    }
} elseif (-not $NoBuild) {
    Write-Warn "No build directory found at $BUILD_DIR -- skipping build. Use -NoBuild to silence."
}

# --- Step 3: Optional DLL copy -----------------------------------------------
if ($CopyDll) {
    if (Test-Path $INJECT_DLL) {
        $dest = Join-Path $GAME_ROOT "kh2coop_inject.dll"
        Copy-Item $INJECT_DLL $dest -Force
        Write-OK "Copied inject DLL -> $dest"
    } else {
        Write-Warn "Inject DLL not found at $INJECT_DLL -- skipping copy."
    }
}

# --- Step 4: Launch KH2 -----------------------------------------------------
if ($Steam) {
    Write-Status "Launching via Steam (will show launcher)..."
    Start-Process "steam://rungameid/$STEAM_APPID"
} else {
    if (-not (Test-Path $KH2_EXE)) {
        Write-Err "KH2 exe not found at: $KH2_EXE"
        Write-Err "Check the `$GAME_ROOT path in this script, or use -Steam to launch via Steam."
        exit 1
    }

    # Verify Steam is running (required for steam_api64.dll auth)
    $steamProc = Get-Process -Name "steam" -ErrorAction SilentlyContinue
    if (-not $steamProc) {
        Write-Warn "Steam doesn't appear to be running. KH2 may fail to launch."
        Write-Warn "Starting Steam..."
        Start-Process "steam://open/main"
        Start-Sleep -Seconds 3
    }

    Write-Status "Launching KH2 directly (bypassing launcher)..."
    Start-Process -FilePath $KH2_EXE -WorkingDirectory $GAME_ROOT
}

# --- Step 5: Wait for process to appear --------------------------------------
Write-Status "Waiting for KH2 process..."
$timeout = 30
$elapsed = 0
while ($elapsed -lt $timeout) {
    $proc = Get-Process -Name $KH2_PROCESS -ErrorAction SilentlyContinue
    if ($proc) {
        Write-OK "KH2 is running! (PID $($proc.Id))"
        Write-OK "Ready for attachment / injection."
        exit 0
    }
    Start-Sleep -Milliseconds 500
    $elapsed += 0.5

    # Progress dot every 2 seconds
    if ($elapsed % 2 -eq 0) {
        Write-Host "." -NoNewline -ForegroundColor DarkGray
    }
}

Write-Host ""
Write-Warn "KH2 process did not appear within ${timeout}s."
Write-Warn "If direct launch doesn't work, try: .\scripts\restart-kh2.ps1 -Steam"
exit 1
