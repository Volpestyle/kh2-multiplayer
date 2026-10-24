# Install or remove the gated desk agent on a shared desktop PC (VUH-2339). Run over SSH as the console
# user after copying agent.ps1, Gate.cs, lib.ps1 and the PC's config to C:\desk\gate\incoming\:
#   powershell -NoProfile -File C:\desk\gate\incoming\install.ps1 -Action install
#   powershell -NoProfile -File C:\desk\gate\incoming\install.ps1 -Action uninstall
# install compiles Gate.cs as a check, exports the current desk-agent task to
# C:\desk\gate\desk-agent-before-<utc>.xml, holds the queue (gate\hold) until no job is running, stops only
# the C:\desk\agent.ps1 process, and registers desk-agent for the console user: AtLogOn, Interactive,
# Limited (no elevation), no time limit, IgnoreNew, restart on failure.
# uninstall unregisters desk-agent, stops its agent and removes the gate code; C:\desk\gate keeps the
# logs, receipts and exported definitions until someone deletes the folder.
param([string]$Action = '')
$ErrorActionPreference = 'Stop'
$desk = 'C:\desk'
$gate = "$desk\gate"
$incoming = "$gate\incoming"
$user = $env:USERNAME

function Stop-Agent {
  Stop-ScheduledTask -TaskName desk-agent -ErrorAction SilentlyContinue
  Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like '*C:\desk\agent.ps1*' } |
    ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "stopped agent pid $($_.ProcessId)" }
}

if ($Action -eq 'install') {
  $explorer = @(Get-Process explorer -IncludeUserName -ErrorAction SilentlyContinue)
  if (-not $explorer.Count -or @($explorer | Where-Object { $_.UserName -ne "$env:COMPUTERNAME\$user" }).Count) { throw "console desktop is not $user's" }
  foreach ($f in 'agent.ps1', 'Gate.cs', 'lib.ps1', 'config.json') { if (-not (Test-Path "$incoming\$f")) { throw "missing $incoming\$f" } }
  Add-Type -Path "$incoming\Gate.cs"   # compile check in this shell; the agent compiles its own copy
  $console = [Kh2Desk.Gate]::ActiveConsoleSession()
  if ($console -ne $explorer[0].SessionId) { throw "active console session $console is not Explorer's $($explorer[0].SessionId)" }
  # One install at a time: a second one could stop the agent the first just started, with a job in it.
  $lock = try { [IO.File]::Open("$gate\install.lock", 'CreateNew', 'Write') } catch { throw "another install is running (or one died: delete $gate\install.lock)" }
  $ownHold = $false
  try {
    $existing = Get-ScheduledTask -TaskName desk-agent -ErrorAction SilentlyContinue
    if ($existing) {
      $export = "$gate\desk-agent-before-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')).xml"
      Export-ScheduledTask -TaskName desk-agent | Set-Content -LiteralPath $export -Encoding Unicode
      "exported $export"
    }
    # Stopping the agent ends its job object, so let the running job finish first. The hold keeps queued
    # jobs waiting (not refused); a state.json written after the hold, with no job, means none can start.
    # An agent from before the hold (2026-10-23) ignores it: replace one only while its queue is empty.
    # Someone else's hold (a lane pausing the queue) stays after the install.
    $held = (Get-Date).ToUniversalTime()
    if (Test-Path "$gate\hold") {
      "leaving the existing gate\hold (written $((Get-Item "$gate\hold").LastWriteTimeUtc.ToString('o'))) in place: queued jobs wait until whoever set it removes it"
    } else {
      try { New-Item -ItemType File "$gate\hold" | Out-Null; $ownHold = $true } catch { if (-not (Test-Path "$gate\hold")) { throw } }
    }
    for ($i = 0; ; $i++) {
      $alive = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" | Where-Object { $_.CommandLine -like '*C:\desk\agent.ps1*' }).Count
      $state = try { Get-Content -Raw "$gate\state.json" | ConvertFrom-Json } catch { $null }
      $fresh = $state -and [datetime]::Parse($state.utc, $null, 'RoundtripKind').ToUniversalTime() -gt $held
      if (-not $alive) { 'no agent running'; break }   # nothing can admit; a lease left behind is reclaimed by the new agent
      if ($fresh -and $state.hold -and -not $state.job -and -not (Test-Path "$gate\lease.json")) { 'queue held, no job running'; break }
      if ($state -and $null -eq $state.PSObject.Properties['hold'] -and -not (Test-Path "$gate\lease.json") -and -not @(Get-ChildItem "$desk\jobs\*.ps1" -ErrorAction SilentlyContinue).Count) { 'agent predates the hold; queue empty'; break }
      if ($i -ge 1800) { throw 'a job still holds the desk after 60 min; inspect gate\lease.json before reinstalling' }
      if ($i % 30 -eq 0) { "waiting for the running job ($(if ($state) { $state.job }))" }
      Start-Sleep -Seconds 2
    }
    Stop-Agent
    # The ungated agent wrote its log as UTF-16; each install starts a fresh UTF-8 log.
    if (Test-Path "$desk\agent.log") { Move-Item "$desk\agent.log" "$gate\agent-before-$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')).log" }
    foreach ($f in 'agent.ps1', 'Gate.cs', 'lib.ps1') { Copy-Item "$incoming\$f" "$desk\$f" -Force }
    Copy-Item "$incoming\config.json" "$gate\config.json" -Force
    $a = New-ScheduledTaskAction -Execute powershell.exe -Argument "-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File $desk\agent.ps1"
    $t = New-ScheduledTaskTrigger -AtLogOn -User $user
    $p = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Limited
    $s = New-ScheduledTaskSettingsSet -ExecutionTimeLimit ([TimeSpan]::Zero) -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
      -MultipleInstances IgnoreNew -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    Register-ScheduledTask -TaskName desk-agent -Action $a -Trigger $t -Principal $p -Settings $s -Description 'Gated KH2 rig desk agent (VUH-2339); remove with C:\desk\gate\incoming\install.ps1 -Action uninstall' -Force | Out-Null
    Start-ScheduledTask -TaskName desk-agent
    Start-Sleep -Seconds 5
    "task: $((Get-ScheduledTask -TaskName desk-agent).State)"
    Get-Content "$desk\agent.log" -Tail 2
  } finally {
    # Released on success or failure: a failure before Stop-Agent leaves the old agent running its queue,
    # and after it there is no agent left to admit anything. Only a hold this install created is removed.
    if ($ownHold) { Remove-Item "$gate\hold" -ErrorAction SilentlyContinue }
    $lock.Close(); Remove-Item "$gate\install.lock" -ErrorAction SilentlyContinue
  }
} elseif ($Action -eq 'uninstall') {
  # A running job must be reclaimed by its watchdog before the watchdog goes: ask through the stop file.
  if (Test-Path "$gate\lease.json") {
    New-Item -ItemType File -Force "$gate\stop" | Out-Null
    for ($i = 0; $i -lt 30 -and (Test-Path "$gate\lease.json"); $i++) { Start-Sleep -Seconds 1 }
    if (Test-Path "$gate\lease.json") { throw 'the lease is still held after a stop request; inspect gate\reclaims\ before uninstalling' }
  }
  Stop-Agent
  Remove-Item "$gate\stop" -ErrorAction SilentlyContinue
  Unregister-ScheduledTask -TaskName desk-agent -Confirm:$false -ErrorAction SilentlyContinue
  foreach ($f in 'agent.ps1', 'Gate.cs') { Remove-Item "$desk\$f" -ErrorAction SilentlyContinue }
  'desk-agent removed; C:\desk\gate keeps logs, receipts and exported task definitions'
} else {
  throw 'usage: install.ps1 -Action install|uninstall'
}
