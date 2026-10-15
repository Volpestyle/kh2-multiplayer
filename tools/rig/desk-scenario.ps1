# Run one scenario from the Session-1 desk bridge (C:\desk\agent.ps1).
# Agent shells are in Session 0, where KH2 gets no desktop/GPU and dies at boot,
# so a live run is queued as a bridge job that calls this script:
#   & 'C:/path/to/worktree/tools/rig/desk-scenario.ps1' -Scenario tools/scenario/scenarios/x.json -Tag x-01
# Never declare Mandatory parameters here: a bridge job that prompts freezes the bridge.
# Waits for the run (holding the bridge), stops it if a foreign game starts or the
# wall limit passes, and kills only this checkout's owned KH2 instances.
param([string]$Scenario = '', [string]$Tag = '', [int]$WallMinutes = 40)
$ErrorActionPreference = 'Continue'
$root = (Resolve-Path "$PSScriptRoot/../..").Path
if (-not $Scenario -or -not (Test-Path -LiteralPath "$root/$Scenario")) { "desk-scenario: no scenario '$Scenario' under $root"; return }
if (-not $Tag) { $Tag = Get-Date -Format 'yyyyMMdd-HHmmss' }
$ops = "$root/build/rig/operator-logs/desk-scenario"
New-Item -ItemType Directory -Force $ops | Out-Null
$kh2ctl = "$root/build/tools/kh2ctl/Release/kh2ctl.exe"
Set-Location -LiteralPath $root

function Get-OwnedIds {
  $ids = @()
  if (Test-Path "$root/build/rig/owned.txt") {
    foreach ($row in @(Get-Content "$root/build/rig/owned.txt")) {
      $f = $row -split ' '
      $p = Get-Process -Id ([int]$f[0]) -ErrorAction SilentlyContinue
      if ($p -and $p.StartTime.ToFileTimeUtc() -eq [long]$f[1]) { $ids += $p.Id }
    }
  }
  $ids
}
function Get-ForeignGames {
  $owned = Get-OwnedIds
  @(Get-Process | Where-Object {
      ($_.ProcessName -match 'Marvel-Win64-Shipping|KINGDOM HEARTS|r5apex|FortniteClient|VALORANT-Win64|Overwatch|eldenring' -or
       ($_.Path -match '\\steamapps\\common\\|\\Epic Games\\|\\Riot Games\\|\\EA Games\\|\\XboxGames\\' -and
        $_.ProcessName -notmatch '^RiotClient|^Epic(GamesLauncher|WebHelper|OnlineServices)|^EOSOverlay')) -and
      $_.Id -notin $owned })
}

if ((Get-ForeignGames).Count) { "PC1 occupied; no run: $((Get-ForeignGames | ForEach-Object ProcessName) -join ',')"; return }
if ((Get-PSDrive C).Free -lt 10GB) { 'PC1 below 10 GiB free; no run'; return }
foreach ($k in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'KH2COOP_*' } | ForEach-Object Name)) { Remove-Item "Env:$k" }
$env:PYTHONUTF8 = '1'
$run = Start-Process -FilePath 'C:/Users/volpe/AppData/Local/Programs/Python/Python311/python.exe' `
  -ArgumentList @('-B', 'tools/scenario/run.py', $Scenario) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput "$ops/$Tag.out" -RedirectStandardError "$ops/$Tag.err"
$null = $run.Handle
$deadline = (Get-Date).AddMinutes($WallMinutes)
$stopped = ''
while (!$run.HasExited) {
  if ((Get-ForeignGames).Count) { $stopped = 'PC1 reclaimed: ' + ((Get-ForeignGames | ForEach-Object ProcessName) -join ',') }
  elseif ((Get-Date) -gt $deadline) { $stopped = "wall limit $WallMinutes min" }
  if ($stopped) {
    Stop-Process -Id $run.Id -Force -ErrorAction SilentlyContinue
    foreach ($id in Get-OwnedIds) { & $kh2ctl kill --pid $id | Out-File -Append "$ops/$Tag.cleanup.log" }
    break
  }
  Start-Sleep -Seconds 1
}
$run.WaitForExit()
"exit=$($run.ExitCode) stopped=$stopped out=$ops/$Tag.out" | Tee-Object -FilePath "$ops/$Tag.exit"
