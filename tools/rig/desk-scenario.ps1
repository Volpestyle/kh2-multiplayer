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

# kh2ctl replaces owned.txt by rename, so a read can fail for a moment while
# the old file is delete-pending. Retry, and if it still can't be read, report
# "unknown" ($script:ownedReadOk = $false) rather than an empty owned list.
function Get-OwnedIds {
  $ids = @()
  $script:ownedReadOk = $true
  $file = "$root/build/rig/owned.txt"
  if (-not (Test-Path $file)) { return $ids }
  $rows = $null
  for ($i = 0; $i -lt 20 -and $null -eq $rows; $i++) {
    try { $rows = @(Get-Content -LiteralPath $file -ErrorAction Stop) } catch { Start-Sleep -Milliseconds 25 }
  }
  if ($null -eq $rows) { $script:ownedReadOk = $false; return $ids }
  foreach ($row in $rows) {
    $f = $row -split ' '
    if ($f.Count -lt 2) { continue }
    $p = Get-Process -Id ([int]$f[0]) -ErrorAction SilentlyContinue
    if (-not $p) { continue }
    # While WER holds a crashing owned game its StartTime can be unreadable.
    # A listed pid we can't check stays owned; never reclaim it mid-dump.
    $start = $null
    try { $start = $p.StartTime.ToFileTimeUtc() } catch { }
    if ($null -eq $start -or $start -eq [long]$f[1]) { $ids += $p.Id }
  }
  $ids
}
function Get-ForeignGames {
  $owned = Get-OwnedIds
  if (-not $script:ownedReadOk) { return @() } # unreadable is not evidence of a foreign game
  @(Get-Process | Where-Object {
      ($_.ProcessName -match 'Marvel-Win64-Shipping|KINGDOM HEARTS|r5apex|FortniteClient|VALORANT-Win64|Overwatch|eldenring' -or
       ($_.Path -match '\\steamapps\\common\\|\\Epic Games\\|\\Riot Games\\|\\EA Games\\|\\XboxGames\\' -and
        $_.ProcessName -notmatch '^RiotClient|^Epic(GamesLauncher|WebHelper|OnlineServices)|^EOSOverlay')) -and
      $_.Id -notin $owned -and
      # A game someone is playing has a window. A crashing owned game's WER
      # snapshot clone (a new, unowned KINGDOM HEARTS pid) does not.
      $_.MainWindowHandle -ne [IntPtr]::Zero })
}

if ((Get-ForeignGames).Count) { "PC1 occupied; no run: $((Get-ForeignGames | ForEach-Object ProcessName) -join ',')"; return }
if ((Get-PSDrive C).Free -lt 10GB) { 'PC1 below 10 GiB free; no run'; return }
# A relay left over from a stopped run holds its UDP port, and the next run then
# fails with "Failed to create ENet host". Refuse with the holder's name instead.
$relayPorts = @()
try {
  $steps = (Get-Content -Raw -LiteralPath "$root/$Scenario" | ConvertFrom-Json).steps
  $relayPorts = @($steps | Where-Object { $_.do -eq 'relay' } | ForEach-Object { if ($null -ne $_.port) { [int]$_.port } else { 7782 } })
} catch { }
foreach ($port in $relayPorts) {
  $holder = @(Get-NetUDPEndpoint -LocalPort $port -ErrorAction SilentlyContinue)
  if ($holder.Count) {
    $p = Get-CimInstance Win32_Process -Filter "ProcessId=$($holder[0].OwningProcess)" -ErrorAction SilentlyContinue
    "relay port $port busy (pid $($holder[0].OwningProcess) $($p.ExecutablePath)); no run"; return
  }
}
# Stops the runner's whole tree (relay, runtimes), anything still running from
# this checkout's build\ whose parent already exited, and this checkout's games.
function Stop-RunTree([int]$runnerId) {
  # Walk the tree ourselves (leaves first) so each kill or failure is logged
  # with the process name; taskkill /T only reported bare pids.
  $all = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
  $tree = [System.Collections.Generic.List[int]]::new(); $tree.Add($runnerId)
  for ($i = 0; $i -lt $tree.Count; $i++) {
    foreach ($c in $all) { if ($c.ParentProcessId -eq $tree[$i] -and $c.ProcessId -ne $tree[$i] -and -not $tree.Contains([int]$c.ProcessId)) { $tree.Add([int]$c.ProcessId) } }
  }
  for ($i = $tree.Count - 1; $i -ge 0; $i--) {
    $id = $tree[$i]; $p = $all | Where-Object { $_.ProcessId -eq $id } | Select-Object -First 1
    try { Stop-Process -Id $id -Force -ErrorAction Stop; "stop $id $($p.Name)" | Out-File -Append "$ops/$Tag.cleanup.log" }
    catch { "could not stop $id $($p.Name) $($p.ExecutablePath): $($_.Exception.Message)" | Out-File -Append "$ops/$Tag.cleanup.log" }
  }
  $build = [IO.Path]::GetFullPath("$root/build") + [IO.Path]::DirectorySeparatorChar
  foreach ($p in @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
      $_.ExecutablePath -and $_.ExecutablePath.StartsWith($build, [StringComparison]::OrdinalIgnoreCase) })) {
    "stop $($p.ProcessId) $($p.ExecutablePath)" | Out-File -Append "$ops/$Tag.cleanup.log"
    Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
  }
  foreach ($id in Get-OwnedIds) { & $kh2ctl kill --pid $id | Out-File -Append "$ops/$Tag.cleanup.log" }
}
foreach ($k in @(Get-ChildItem Env: | Where-Object { $_.Name -like 'KH2COOP_*' } | ForEach-Object Name)) { Remove-Item "Env:$k" }
$env:PYTHONUTF8 = '1'
$run = Start-Process -FilePath 'C:/Users/volpe/AppData/Local/Programs/Python/Python311/python.exe' `
  -ArgumentList @('-B', 'tools/scenario/run.py', $Scenario) -WorkingDirectory $root -WindowStyle Hidden -PassThru `
  -RedirectStandardOutput "$ops/$Tag.out" -RedirectStandardError "$ops/$Tag.err"
$null = $run.Handle
$deadline = (Get-Date).AddMinutes($WallMinutes)
$stopped = ''
# A game the run is launching is briefly unowned until kh2ctl records it, so
# only a foreign game seen for 5 consecutive seconds stops the run.
$foreignSeconds = 0
while (!$run.HasExited) {
  $foreign = @(Get-ForeignGames)
  # A crash dump in progress (WerFault) means one of the run's games is
  # crashing, not that someone took the PC; let the dump finish first.
  if (@(Get-Process WerFault, WerFaultSecure -ErrorAction SilentlyContinue).Count) { $foreign = @() }
  $foreignSeconds = if ($foreign.Count) { $foreignSeconds + 1 } else { 0 }
  if ($foreignSeconds -ge 5) { $stopped = 'PC1 reclaimed: ' + (($foreign | ForEach-Object ProcessName) -join ',') }
  elseif ((Get-Date) -gt $deadline) { $stopped = "wall limit $WallMinutes min" }
  if ($stopped) {
    Stop-RunTree $run.Id
    break
  }
  Start-Sleep -Seconds 1
}
$run.WaitForExit()
"exit=$($run.ExitCode) stopped=$stopped out=$ops/$Tag.out" | Tee-Object -FilePath "$ops/$Tag.exit"
