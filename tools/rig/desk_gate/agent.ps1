# Gated desktop agent (VUH-2339): the desk-agent scheduled task runs this inside the console user's
# session on a PC that is someone's everyday desktop. It keeps the C:\desk protocol (stage a job, move
# it into jobs\, wait for done\<job>.out; the .ps1 stays in jobs\ while it runs), with two changes:
# - Admission: a job starts only when the gate admits it (README.md). A refused job is finished at once
#   with "REFUSED gate: <reasons>" in its .out; nothing waits in the queue for the person to leave.
# - Reclaim: the job runs as a child process in a job object while this loop keeps watching. A person
#   at the desk after admission (or a stop file, a session change or a lost observer) ends the job
#   object, closes the rig's owned games through kh2ctl, verifies every exit, then releases the lease
#   and writes a timed receipt to gate\reclaims\. If the agent itself dies, the job object ends the tree.
# A job's first line may be "#gate: desk" (no game launch: idle, unlocked, no fullscreen app) instead of
# the default launch level. James authorized this rig on 2026-10-23 (VUH-2339).
$ErrorActionPreference = 'Continue'
$root = 'C:\desk'
$gate = "$root\gate"
New-Item -ItemType Directory -Force "$root\jobs", "$root\done", "$root\out", "$gate\reclaims", "$gate\availability" | Out-Null
$config = Get-Content -Raw -LiteralPath "$gate\config.json" | ConvertFrom-Json
Add-Type -Path "$root\Gate.cs"
[Kh2Desk.Gate]::Start()
$me = [Diagnostics.Process]::GetCurrentProcess()
$session = $me.SessionId
$idleMs = [long]$config.idleMinutes * 60000
$budgetMs = 2000
$leasePath = "$gate\lease.json"
$stopPath = "$gate\stop"
$verifyPath = "$gate\needs-verify.json"
$holdPath = "$gate\hold"   # install.ps1 sets it: queued jobs wait (not refused) until it is gone
$gameDir = [IO.Path]::GetFullPath($config.gameDirectory).TrimEnd('\') + '\'
$kh2ctlTimeoutMs = 3000
# PC1 runs on pad input while James works there: idleMinutes 0 and reclaimOnInput false keep that
# practice, and a foreign game or fullscreen app still refuses admission and reclaims a running job.
$reclaimOnInput = $config.reclaimOnInput -ne $false
# On bld every process under a rig root's build\ is the rig's. On PC1 other agents run tests from their
# own worktrees, so only the job object and owned.txt say what is ours (helperSweep false).
$helperSweep = $config.helperSweep -ne $false
# Seconds a foreign game or fullscreen app must persist before a running job is reclaimed. PC1 uses 10, so
# desk-scenario.ps1's own 5 s reclaim stops the run first and its wrapper still runs the closing checks.
$foreignReclaimSeconds = if ($config.foreignReclaimSeconds) { [int]$config.foreignReclaimSeconds } else { 3 }
# Launch refuses below this many free GB on C:. A Thorn scenario's paired 30 s clip block holds ~14 GB of raw BMPs
# until ffmpeg encodes them; on 2026-10-25 a launch admitted at 14 GB filled C: mid-fight (zr111). PC1 uses 20.
$minFreeGB = if ($config.minFreeGB) { [double]$config.minFreeGB } else { 10 }

function Log([string]$Text) { "$((Get-Date).ToUniversalTime().ToString('o')) $Text" | Out-File -Append -Encoding utf8 "$root\agent.log" }
function Write-Json($Path, $Value) {
  $tmp = "$Path.tmp"
  $Value | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $tmp -Encoding UTF8
  Move-Item -LiteralPath $tmp -Destination $Path -Force
}
function Expand([string]$Path) { [Environment]::ExpandEnvironmentVariables($Path) }

# Checkouts whose build\ holds rig products; entries may use wildcards (PC1's .local\wt-*).
function Get-RigRoots {
  @($config.rigRoots | ForEach-Object {
      $parent = [IO.Path]::GetDirectoryName($_)
      if ($_ -notmatch '[*?]') { $_ } elseif ([IO.Directory]::Exists($parent)) { [IO.Directory]::GetDirectories($parent, [IO.Path]::GetFileName($_)) } })
}

# Rows of each rig root's build\rig\owned.txt that are still running (pid and creation time match).
function Get-Owned {
  $rows = @()
  foreach ($rig in Get-RigRoots) {
    $file = Join-Path $rig 'build\rig\owned.txt'
    if (-not [IO.File]::Exists($file)) { continue }
    $lines = $null
    for ($i = 0; $i -lt 20 -and $null -eq $lines; $i++) {
      try { $lines = @(Get-Content -LiteralPath $file -ErrorAction Stop) } catch { Start-Sleep -Milliseconds 25 }
    }
    if ($null -eq $lines) { $rows += [pscustomobject]@{ rig = $rig; pid = 0; created = 0; unreadable = $true }; continue }
    foreach ($line in $lines) {
      $f = $line -split ' '
      if ($f.Count -ge 2 -and [Kh2Desk.Gate]::Alive([int]$f[0], [long]$f[1])) { $rows += [pscustomobject]@{ rig = $rig; pid = [int]$f[0]; created = [long]$f[1]; unreadable = $false } }
    }
  }
  $rows
}

# Time from which idle is measured: the latest physical or unattributed input, injected input that was
# not the rig's own, the observer's start, and the last moment the context could have hidden a person
# (another session, the secure desktop, a lost hook, a sleep or stall).
function Get-IdleBase { [Math]::Max([Math]::Max([Kh2Desk.Gate]::LastHuman, [Kh2Desk.Gate]::HooksAt), [Math]::Max($script:contextResetAt, $script:foreignInjectedAt)) }

# A game or fullscreen app that is neither the rig's (owned.txt) nor this job's (its job object).
function Get-ForeignActivity($Job) {
  $owned = @(Get-Owned)
  $foreign = @([Kh2Desk.Gate]::Windowed() | Where-Object {
      $_.Pid -notin $owned.pid -and -not ($Job -and [Kh2Desk.Gate]::InJob($Job.jobObject, $_.Pid)) -and
      ($_.Name -match $config.foreignGameNames -or ($_.Path -match $config.foreignGamePaths -and $_.Name -notmatch $config.foreignGameExempt)) })
  if ($foreign.Count) { return 'foreign-game' }
  $front = [Kh2Desk.Gate]::ForegroundPid()
  if ([Kh2Desk.Gate]::NotificationState() -in 2, 3, 4, 7 -and $front -notin $owned.pid -and -not ($Job -and [Kh2Desk.Gate]::InJob($Job.jobObject, $front))) { return 'fullscreen' }
  $null
}

function Get-Refusals([string]$Level) {
  try {
    $ErrorActionPreference = 'Stop'
    $why = @()
    $now = [Kh2Desk.Gate]::Now()
    if (Test-Path -LiteralPath $stopPath) { $why += 'stopped' }
    if (Test-Path -LiteralPath $leasePath) { $why += 'lease-held' }
    if (-not [Kh2Desk.Gate]::HooksOk) { $why += 'observer' }
    if ($session -eq 0 -or [Kh2Desk.Gate]::ActiveConsoleSession() -ne $session) { $why += 'session' }
    if ($config.requireUnlocked -and [Kh2Desk.Gate]::InputDesktop() -ne 'Default') { $why += 'locked' }
    if ($now - (Get-IdleBase) -lt $idleMs) { $why += 'recent-input' }
    if ([Kh2Desk.Gate]::NotificationState() -in 2, 3, 4, 7) { $why += 'fullscreen' }  # busy, D3D fullscreen, presentation, immersive app
    $owned = @(Get-Owned)
    if (@($owned | Where-Object unreadable).Count) { $why += 'owned-unreadable' }
    if ((Get-ForeignActivity $null) -eq 'foreign-game') { $why += 'foreign-game' }
    if ($Level -eq 'launch') {
      if ([Kh2Desk.Gate]::Under($gameDir) -or $owned.Count) { $why += 'kh2-running' }
      $steam = Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam\ActiveProcess' -ErrorAction SilentlyContinue
      $steamUp = $steam -and $steam.pid -and @(Get-Process -Id $steam.pid -ErrorAction SilentlyContinue | Where-Object ProcessName -eq 'steam').Count
      if (-not $steamUp -or ([int64]$steam.ActiveUser -band 0xFFFFFFFF) -ne [int64]$config.steamAccountId) { $why += 'steam-account' }
      foreach ($lock in $config.laneLocks) { if (Test-Path -LiteralPath (Expand $lock)) { $why += 'lane-lock'; break } }
      if (Test-Path -LiteralPath $verifyPath) { $why += 'needs-verify' }
      if ((Get-PSDrive C).Free -lt $minFreeGB * 1GB) { $why += 'disk' }
    }
    $why
  } catch {
    Log "gate-error: $_"
    'gate-error'
  }
}

function Invoke-Kh2ctlKill([string]$Rig) {
  $exe = Join-Path $Rig 'build\tools\kh2ctl\Release\kh2ctl.exe'
  try {
    $p = Start-Process -FilePath $exe -ArgumentList 'kill', '--all' -WorkingDirectory $Rig -WindowStyle Hidden -PassThru -ErrorAction Stop
    if (-not $p.WaitForExit($kh2ctlTimeoutMs)) { Log "kh2ctl kill --all ($Rig) still running after $kh2ctlTimeoutMs ms" }
  } catch { Log "kh2ctl kill --all ($Rig) failed: $_" }
}

# Ends the job object (or, for an orphaned lease, the recorded tree), closes the rig's owned games and
# stray rig helpers, verifies every exit and only then releases the lease. Returns the receipt; an
# uncertain cleanup keeps the lease so nothing else starts until a person has looked.
function Invoke-Reclaim($Job, [string]$Reason, [long]$TriggerTick) {
  $t = @{ trigger = $TriggerTick; detected = [Kh2Desk.Gate]::Now() }
  $stuck = @()
  $treeEnded = $true
  if ($Job -and $Job.jobObject -ne [IntPtr]::Zero) {
    $treeEnded = [Kh2Desk.Gate]::EndJob($Job.jobObject, 1500)
  } elseif ($Job -and $Job.pid -gt 4) {
    try { foreach ($p in [Kh2Desk.Gate]::Tree([Kh2Desk.Gate]::Snapshot(), $Job.pid, $Job.created)) { if (-not [Kh2Desk.Gate]::Terminate($p.Pid, $p.Created)) { $stuck += $p.Pid } } }
    catch { $stuck += 'snapshot' }
  }
  if (-not $treeEnded) { $stuck += 'job-object' }
  $t.inputStopped = [Kh2Desk.Gate]::Now()
  $games = @(Get-Owned)
  foreach ($rig in @($games | Select-Object -ExpandProperty rig -Unique)) { Invoke-Kh2ctlKill $rig }
  $prefixes = @(Get-RigRoots | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $_ 'build')).TrimEnd('\') + '\' })
  try { $helpers = @($(if ($helperSweep) { [Kh2Desk.Gate]::Snapshot() }) | Where-Object { $path = $_.Path; $path -and $_.Pid -ne $me.Id -and @($prefixes | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count }) }
  catch { $helpers = @(); $stuck += 'snapshot' }
  foreach ($p in $helpers) { if (-not [Kh2Desk.Gate]::Terminate($p.Pid, $p.Created)) { $stuck += $p.Pid } }
  $deadline = [Kh2Desk.Gate]::Now() + 5000
  do {
    $left = @(Get-Owned)
    if (-not $left.Count) { break }
    Start-Sleep -Milliseconds 20
  } while ([Kh2Desk.Gate]::Now() -lt $deadline)
  $t.gamesExited = [Kh2Desk.Gate]::Now()
  $stuck += @($left | ForEach-Object { if ($_.unreadable) { 'owned-unreadable' } else { $_.pid } })
  $released = $false
  if (-not $stuck.Count -and (Test-Path -LiteralPath $leasePath) -and (Get-Content -Raw -LiteralPath $leasePath) -eq $Job.leaseText) {
    Remove-Item -LiteralPath $leasePath; $released = $true
  }
  $t.leaseReleased = [Kh2Desk.Gate]::Now()
  $ms = [ordered]@{ detect = $t.detected - $t.trigger; inputStopped = $t.inputStopped - $t.trigger
                    gamesExited = $t.gamesExited - $t.trigger; leaseReleased = $t.leaseReleased - $t.trigger }
  $receipt = [ordered]@{
    job = $Job.name; level = $Job.level; reason = $Reason; utc = (Get-Date).ToUniversalTime().ToString('o')
    ok = $released; withinBudget = ($released -and $ms.leaseReleased -le $budgetMs); budgetMs = $budgetMs; ms = $ms
    stuck = $stuck; games = @($games | ForEach-Object pid); helpers = $helpers.Count
  }
  Write-Json "$gate\reclaims\$((Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ'))-$($Job.name).json" $receipt
  # A killed live run never reaches its closing manifest and safety checks: launch stays refused until
  # someone has compared the install and saves and deleted this marker.
  if ($Job.level -ne 'desk') { Write-Json $verifyPath ([ordered]@{ job = $Job.name; reason = $Reason; utc = $receipt.utc }) }
  Log "reclaim $Reason job=$($Job.name) ok=$released ms=$($ms | ConvertTo-Json -Compress) stuck=$($stuck -join ',')"
  $receipt
}

# git refuses a checkout whose folder, or whose worktree admin folder, another account owns ("detected
# dubious ownership"). Folders created over elevated SSH are owned by BUILTIN\Administrators, which passes
# only with an elevated token, and this agent is not elevated. Names each such folder of a rig root the
# job mentions, with the fix.
function Get-OwnershipWarnings([string]$Text) {
  $me = [Security.Principal.WindowsIdentity]::GetCurrent().User
  $norm = $Text.Replace('/', '\')
  $roots = @(Get-RigRoots | ForEach-Object { $_.TrimEnd('\') })
  foreach ($rig in $roots) {
    # A mention counts for the most specific root only: ...\kh2-multiplayer\.local\wt-x is not the main checkout.
    $deeper = @($roots | Where-Object { $_.Length -gt $rig.Length -and $_.StartsWith($rig + '\', [StringComparison]::OrdinalIgnoreCase) })
    $mentioned = $false
    for ($at = $norm.IndexOf($rig, [StringComparison]::OrdinalIgnoreCase); $at -ge 0 -and -not $mentioned; $at = $norm.IndexOf($rig, $at + 1, [StringComparison]::OrdinalIgnoreCase)) {
      $end = $at + $rig.Length
      $mentioned = ($end -ge $norm.Length -or [string]$norm[$end] -notmatch '[\w.-]') -and
        -not @($deeper | Where-Object { [string]::Compare($norm, $at, $_, 0, $_.Length, [StringComparison]::OrdinalIgnoreCase) -eq 0 }).Count
    }
    if (-not $mentioned) { continue }
    $dirs = @($rig)
    $dotgit = Join-Path $rig '.git'
    if ([IO.Directory]::Exists($dotgit)) { $dirs += $dotgit }
    elseif ([IO.File]::Exists($dotgit) -and (Get-Content -LiteralPath $dotgit -TotalCount 1) -match '^gitdir:\s*(.+)$') {
      $dirs += [IO.Path]::GetFullPath([IO.Path]::Combine($rig, $Matches[1].Trim().Replace('/', '\')))   # a relative gitdir is relative to the checkout
    }
    foreach ($d in $dirs) {
      try { $owner = (Get-Acl -LiteralPath $d).GetOwner([Security.Principal.SecurityIdentifier]) } catch { continue }
      if ($owner -ne $me) {
        $name = try { $owner.Translate([Security.Principal.NTAccount]).Value } catch { $owner.Value }
        "WARNING gate: git will refuse $d (owned by $name, not $env:USERNAME). Fix: icacls `"$d`" /setowner $env:USERNAME /T"
      }
    }
  }
}

function Finish-Job($Job, [string]$Prefix) {
  $text = ''
  foreach ($f in $Job.stdout, $Job.stderr) { if ($f -and (Test-Path -LiteralPath $f)) { $text += Get-Content -Raw -LiteralPath $f; Remove-Item -LiteralPath $f } }
  if ($text -match "detected dubious ownership in repository at '([^']+)'") {
    $Prefix = "GIT OWNERSHIP gate: git refused $($Matches[1]): another account owns it. Fix: icacls `"$($Matches[1])`" /setowner $env:USERNAME /T (and the same for its .git\worktrees\<name> folder)`r`n" + $Prefix
  }
  if ($Job.warnings) { $Prefix = (@($Job.warnings) -join "`r`n") + "`r`n" + $Prefix }
  Set-Content -LiteralPath "$root\done\$($Job.base).out" -Value ($Prefix + $text) -Encoding UTF8
  if (Test-Path -LiteralPath $Job.file) { Move-Item -LiteralPath $Job.file -Destination "$root\done\$($Job.name)" -Force }
  [Kh2Desk.Gate]::CloseJob($Job.jobObject)
}

$contextResetAt = [Kh2Desk.Gate]::Now()
[long]$foreignInjectedAt = 0
[long]$ownInjectionUntil = 0   # injected input up to this tick came from the rig's own desk-level job
$lastPass = [Kh2Desk.Gate]::Now()
Log "agent up session $session pid $($me.Id) (gated, $($config.machine))"

# A lease left by an earlier agent names a job that ran without a watchdog. Its job object died with that
# agent; reclaim whatever else it left, and finish its job file so it isn't admitted again.
if (Test-Path -LiteralPath $leasePath) {
  $text = Get-Content -Raw -LiteralPath $leasePath
  $old = $null
  try { $old = $text | ConvertFrom-Json } catch { }
  if (-not $old -or -not $old.job -or [int]$old.childPid -le 4) {
    Log "lease $leasePath can't be attributed to a job; kept for a person to inspect"
  } else {
    $orphan = [pscustomobject]@{ name = $old.job; base = [IO.Path]::GetFileNameWithoutExtension($old.job); file = "$root\jobs\$($old.job)"; level = $old.level
                                 pid = [int]$old.childPid; created = [long]$old.childCreated; leaseText = $text; jobObject = [IntPtr]::Zero; stdout = $null; stderr = $null }
    $r = Invoke-Reclaim $orphan 'agent-restart' ([Kh2Desk.Gate]::Now())
    Finish-Job $orphan "RECLAIMED gate: agent-restart ok=$($r.ok)`r`n"
  }
}

$job = $null
$nextState = 0; $nextMinute = 0
while ($true) {
  $human = [Kh2Desk.Gate]::Poll()
  $now = [Kh2Desk.Gate]::Now()
  # A stalled or sleeping PC, another session, the secure desktop or a lost hook can each hide a person.
  if ($now - $lastPass -gt 5000 -or -not [Kh2Desk.Gate]::HooksOk -or [Kh2Desk.Gate]::ActiveConsoleSession() -ne $session -or
      ($config.requireUnlocked -and [Kh2Desk.Gate]::InputDesktop() -ne 'Default')) { $contextResetAt = $now }
  $lastPass = $now
  $injected = [Kh2Desk.Gate]::LastInjected
  if (-not $job -and $injected -gt $ownInjectionUntil) { $foreignInjectedAt = [Math]::Max($foreignInjectedAt, $injected) }
  if ($job) {
    $reason = $null
    if ($reclaimOnInput -and $human -gt $job.startTick) { $reason = 'physical-input' }
    elseif ($reclaimOnInput -and $job.level -ne 'desk' -and $injected -gt $job.startTick) { $reason = 'injected-input' }  # launch jobs send no OS input
    elseif (Test-Path -LiteralPath $stopPath) { $reason = 'stopped' }
    elseif (-not [Kh2Desk.Gate]::HooksOk) { $reason = 'observer' }
    elseif ([Kh2Desk.Gate]::ActiveConsoleSession() -ne $session) { $reason = 'session' }
    elseif ($now -ge $job.nextCheck) {
      $job.nextCheck = $now + 1000
      try {
        # A desk job never passed the launch checks, so it must not start a game.
        if ($job.level -eq 'desk' -and (@(Get-Owned).Count -or [Kh2Desk.Gate]::Under($gameDir))) {
          $reason = 'desk-job-launched'; $job.level = 'launch'
        } else {
          # Someone starting a game or a fullscreen app takes the PC back, once it has persisted for
          # $foreignReclaimSeconds, so a window briefly in front while the job launches doesn't count.
          $activity = Get-ForeignActivity $job
          $job.foreignSeconds = if ($activity) { $job.foreignSeconds + 1 } else { 0 }
          if ($job.foreignSeconds -ge $foreignReclaimSeconds) { $reason = $activity }
        }
      } catch {
        Log "gate-error during $($job.name): $_"
        $reason = 'gate-error'   # a check that can't see the desk can't keep watching it
      }
    }
    if ($reason) {
      $trigger = if ($reason -eq 'physical-input') { $human } elseif ($reason -eq 'injected-input') { $injected } else { $now }
      $r = Invoke-Reclaim $job $reason $trigger
      Finish-Job $job "RECLAIMED gate: $reason ok=$($r.ok) withinBudget=$($r.withinBudget) ms=$($r.ms | ConvertTo-Json -Compress)`r`n"
      $job = $null
    } elseif ($job.process.HasExited) {
      $job.process.WaitForExit()
      # The job's own processes may outlive its root: end the job object, then make sure no owned game or
      # rig helper is left before giving the desk back.
      $ended = [Kh2Desk.Gate]::EndJob($job.jobObject, 1500)
      $prefixes = @(Get-RigRoots | ForEach-Object { [IO.Path]::GetFullPath((Join-Path $_ 'build')).TrimEnd('\') + '\' })
      try { $strays = @($(if ($helperSweep) { [Kh2Desk.Gate]::Snapshot() }) | Where-Object { $path = $_.Path; $path -and @($prefixes | Where-Object { $path.StartsWith($_, [StringComparison]::OrdinalIgnoreCase) }).Count }) }
      catch { $strays = @('snapshot') }
      if (-not $ended -or @(Get-Owned).Count -or $strays.Count) { $r = Invoke-Reclaim $job 'leftover-after-exit' $now; $prefix = "LEFTOVER after exit; reclaim ok=$($r.ok)`r`n" }
      else {
        if ((Get-Content -Raw -LiteralPath $leasePath) -eq $job.leaseText) { Remove-Item -LiteralPath $leasePath }
        $prefix = ''
      }
      if ($job.level -eq 'desk') { $ownInjectionUntil = [Kh2Desk.Gate]::Now() + 2000 }
      # A live wrapper that throws (install mismatch, safety finding, owned game left) says "no further
      # runs": hold launches until someone has looked.
      elseif ($job.process.ExitCode -ne 0) {
        Write-Json $verifyPath ([ordered]@{ job = $job.name; reason = "exit $($job.process.ExitCode)"; utc = (Get-Date).ToUniversalTime().ToString('o') })
        $prefix = "EXIT $($job.process.ExitCode): launches held until $verifyPath is cleared`r`n" + $prefix
      }
      Finish-Job $job $prefix
      $job = $null
    } elseif ($job.level -eq 'desk') {
      $ownInjectionUntil = $now + 2000
    }
  } else {
    $next = if (-not [IO.File]::Exists($holdPath)) { Get-ChildItem "$root\jobs\*.ps1" -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1 }
    if ($next) {
      $startTick = [Kh2Desk.Gate]::Now()   # input from here on reclaims; the checks below read the same instant's idle
      $level = if ((Get-Content -LiteralPath $next.FullName -TotalCount 1) -match '^\s*#\s*gate:\s*desk\b') { 'desk' } else { 'launch' }
      $why = @(Get-Refusals $level)
      if ([Kh2Desk.Gate]::LastHuman -gt $startTick) { $why += 'recent-input' }
      $lease = [ordered]@{ job = $next.Name; level = $level; agentPid = $me.Id; startUtc = (Get-Date).ToUniversalTime().ToString('o'); childPid = $null; childCreated = $null }
      if (-not $why.Count) {
        try {
          $f = [IO.File]::Open($leasePath, 'CreateNew', 'Write')
          $bytes = [Text.Encoding]::UTF8.GetBytes(($lease | ConvertTo-Json -Compress)); $f.Write($bytes, 0, $bytes.Length); $f.Close()
        } catch { $why += 'lease-held' }
      }
      if ($why.Count) {
        Set-Content -LiteralPath "$root\done\$($next.BaseName).out" -Value "REFUSED gate: $(@($why | Select-Object -Unique) -join ',')" -Encoding UTF8
        Move-Item -LiteralPath $next.FullName -Destination "$root\done\$($next.Name)" -Force
        Log "refused $($next.Name): $(@($why | Select-Object -Unique) -join ',')"
      } else {
        # Jobs are usually one line calling a wrapper (build-job.ps1, live.ps1) that names the checkout, so
        # the scripts a job calls are read too.
        $warnings = @(try {
            $scan = Get-Content -Raw -LiteralPath $next.FullName
            foreach ($m in [regex]::Matches($scan, "[A-Za-z]:[\\/][^'`"\r\n]+?\.ps1")) { if ([IO.File]::Exists($m.Value)) { $scan += "`n" + [IO.File]::ReadAllText($m.Value) } }
            Get-OwnershipWarnings $scan
          } catch { "WARNING gate: ownership check failed: $_" })
        foreach ($w in $warnings) { Log "$($next.Name): $w" }
        $quoted = $next.FullName -replace "'", "''"
        $stdout = "$root\out\$($next.BaseName).stdout"; $stderr = "$root\out\$($next.BaseName).stderr"
        $jobObject = [Kh2Desk.Gate]::NewJob()
        try {
          if ($jobObject -eq [IntPtr]::Zero) { throw 'job object unavailable' }
          $p = Start-Process -FilePath powershell.exe -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
            -ArgumentList @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-Command', "& { . 'C:\desk\lib.ps1'; & '$quoted' }") -ErrorAction Stop
          $null = $p.Handle
          if (-not [Kh2Desk.Gate]::Assign($jobObject, $p.Id)) { $p.Kill(); throw 'could not assign the job to its job object' }
          $lease.childPid = $p.Id; $lease.childCreated = $p.StartTime.ToFileTimeUtc()
          Set-Content -LiteralPath $leasePath -Value ($lease | ConvertTo-Json -Compress) -Encoding UTF8 -NoNewline
          $job = [pscustomobject]@{ name = $next.Name; base = $next.BaseName; file = $next.FullName; level = $level; process = $p; pid = $p.Id
                                    created = $lease.childCreated; jobObject = $jobObject; startTick = $startTick; nextCheck = 0; foreignSeconds = 0; stdout = $stdout; stderr = $stderr; warnings = $warnings
                                    leaseText = (Get-Content -Raw -LiteralPath $leasePath) }
          Log "admitted $($next.Name) level=$level child=$($p.Id)"
        } catch {
          [Kh2Desk.Gate]::CloseJob($jobObject)
          Remove-Item -LiteralPath $leasePath -ErrorAction SilentlyContinue
          Set-Content -LiteralPath "$root\done\$($next.BaseName).out" -Value "ERROR starting job: $_" -Encoding UTF8
          Move-Item -LiteralPath $next.FullName -Destination "$root\done\$($next.Name)" -Force
          Log "start failed $($next.Name): $_"
        }
      }
    }
  }
  if ($now -ge $nextState) {
    $nextState = $now + 2000
    $idle = [long](($now - (Get-IdleBase)) / 1000)
    $state = [ordered]@{ utc = (Get-Date).ToUniversalTime().ToString('o'); agentPid = $me.Id; session = $session; hooks = [Kh2Desk.Gate]::HooksOk
                         inputDesktop = [Kh2Desk.Gate]::InputDesktop(); notification = [Kh2Desk.Gate]::NotificationState()
                         physicalIdleSeconds = $idle; injectedIdleSeconds = $(if ($injected) { [long](($now - $injected) / 1000) } else { $null })
                         job = $(if ($job) { $job.name } else { $null }); hold = [IO.File]::Exists($holdPath) }
    if ($now -ge $nextMinute) {
      $nextMinute = $now + 60000
      $desk = @(if ($job) { 'job-running' } else { Get-Refusals 'desk' })
      $launch = @(if ($job) { 'job-running' } else { Get-Refusals 'launch' })
      $script:lastEligibility = [ordered]@{ desk = $desk; launch = $launch }
      [ordered]@{ utc = $state.utc; idleMin = [long][Math]::Floor($idle / 60); desk = !$desk.Count; launch = !$launch.Count; reasons = $launch } |
        ConvertTo-Json -Compress | Out-File -Append -Encoding utf8 "$gate\availability\$((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd')).jsonl"
    }
    $state.refusals = $script:lastEligibility
    Write-Json "$gate\state.json" $state
  }
  Start-Sleep -Milliseconds 100
}
