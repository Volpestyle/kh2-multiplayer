param([switch]$ValidateOnly, [string]$Attempt = '', [string]$Scenario = '.local/bot01-replay/net_twilight_thorn_bots.json',
      [string]$Entry = '.local/bot01-replay/run_bot01.py', [string]$Report = 'net_twilight_thorn_bots', [int]$Port = 27809,
      [string]$Lane = 'bot01-replay', [int]$WallMinutes = 30)
# bld (PC2) one live run (VUH-2339), PC1's bots05 live.ps1 retargeted: the checkout and its products sit at
# C:/kh2rig/wt as on PC1's build worktree; products are PC1's governed build copied unchanged
# (C:/kh2rig/products/<commit>/, staged by stage.ps1). Saves are account B's, hashed before and after;
# the full-install manifest runs before and after in a finally block (docs/THORN_BOTS.md). Queue it through
# bld's gated desk agent, which admits it only when the desk is free and reclaims it on physical input:
#   & 'C:/kh2rig/ops/live.ps1' -Attempt bot01r-ac5280e-bld01
$ErrorActionPreference='Stop'
$root='C:/kh2rig/wt'
$own='C:/kh2rig/ops'
$game='C:/Program Files (x86)/Steam/steamapps/common/KINGDOM HEARTS -HD 1.5+2.5 ReMIX-'
$py="$env:LOCALAPPDATA/Programs/Python/Python311/python.exe"
$env:PATH="C:/kh2rig/bin;C:/Program Files/Git/cmd;$env:PATH"  # ffmpeg for kh2ctl clip; git for the checkout checks
if($Attempt -notmatch '^[A-Za-z0-9-]{4,64}$'){throw 'explicit -Attempt required'}
if(!$ValidateOnly -and (Get-Process -Id $PID).SessionId -eq 0){throw 'desktop queue required'}
$evidence=".local/evidence/$Lane/$Attempt"
if(Test-Path -LiteralPath "$root/$evidence"){throw "$Attempt exists; preserve evidence and choose a fresh attempt"}
Set-Location -LiteralPath $root
$commit=(& git rev-parse HEAD)
if(@(& git status --porcelain --untracked-files=no).Count){throw 'tracked files modified'}
$built=Get-Content -Raw -LiteralPath "C:/kh2rig/products/$commit/products.json" | ConvertFrom-Json
if($built.commit -ne $commit){throw "products not built from $commit"}
function Check-Products {
 foreach($row in $built.products){
  if((Get-FileHash -LiteralPath (Join-Path $root $row.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256){throw "staged product differs from PC1's build: $($row.path)"}
 }
}
Check-Products
$sourcePaths=@($Scenario,$Entry,'tools/scenario/run.py','tools/scenario/run_thorn.py','tools/rig/route_pilot/thorn.py','tools/rig/route_pilot/pilot.py','tools/rig/install_manifest.py') +
 @(Get-ChildItem -LiteralPath "$root/tools/rig/route_pilot/routes" -Filter 'twilight_thorn_*.json' | ForEach-Object { "tools/rig/route_pilot/routes/$($_.Name)" })
foreach($path in $Scenario,$Entry){ if(!(Test-Path -LiteralPath (Join-Path $root $path) -PathType Leaf)){throw "missing $path"} }
$sources=@($sourcePaths | Where-Object { Test-Path -LiteralPath (Join-Path $root $_) } | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256).Hash.ToLowerInvariant()} }) +
 @(@{path='../ops/desk-scenario.ps1';sha256=(Get-FileHash -LiteralPath "$own/desk-scenario.ps1" -Algorithm SHA256).Hash.ToLowerInvariant()})
if(!(Test-Path -LiteralPath $game -PathType Container)){throw 'installed game missing'}
if($ValidateOnly){
 & $py -B $Entry $Scenario --validate
 if($LASTEXITCODE -ne 0){throw 'scenario validation failed'}
 @{ok=$true;commit=$commit;scope='checkout, products, sources and scenario validation; stopped before manifest and live side effects';products=$built.products.Count;sources=$sources} | ConvertTo-Json -Depth 4
 return
}
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw "relay port $Port busy; no launch"}
$out="$root/$evidence"
New-Item -ItemType Directory -Path $out | Out-Null
ConvertTo-Json -InputObject $sources -Depth 5 | Set-Content "$out/source-before.json" -Encoding UTF8
# run.py hashes the same folder; this list feeds the closure check. Hashing reads the files, never opens them for write.
$saves=@(Get-ChildItem -LiteralPath "$env:USERPROFILE/OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX" -File -Recurse |
 ForEach-Object { @{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} })
if($saves.Count -lt 4){throw 'protected save inventory incomplete'}
ConvertTo-Json -InputObject $saves -Depth 5 | Set-Content "$out/protected-saves.json" -Encoding UTF8
function Root-Files { @(Get-ChildItem -LiteralPath $game -File | Sort-Object Name | ForEach-Object { [pscustomobject]@{name=$_.Name;bytes=$_.Length;lastWriteUtc=$_.LastWriteTimeUtc.ToString('o')} }) }
function Asset-Files { @(foreach($target in @('Image','STEAM')) { $dir=Join-Path $game $target; Get-ChildItem -LiteralPath $dir -File -Recurse |
 ForEach-Object { [pscustomobject]@{target=$target;path=$_.FullName.Substring($dir.Length+1);bytes=$_.Length;lastWriteUtc=$_.LastWriteTimeUtc.ToString('o')} } }) | Sort-Object target,path }
$roots=Root-Files; $assets=@(Asset-Files)
ConvertTo-Json -InputObject $roots -Depth 5 | Set-Content "$out/game-root.json" -Encoding UTF8
ConvertTo-Json -InputObject $assets -Depth 5 | Set-Content "$out/assets.json" -Encoding UTF8

$env:PYTHONUTF8='1'
& $py -B tools/rig/install_manifest.py capture --root $game --out "$evidence/install-before.json"
if($LASTEXITCODE -ne 0){throw 'STOP: install baseline refused'}
$startedUtc=[DateTime]::UtcNow
$installExit=$null
try {
 & "$own/desk-scenario.ps1" -Root $root -Python $py -Scenario $Scenario -Entry $Entry -Tag "$Lane-$Attempt" -WallMinutes $WallMinutes | Tee-Object -FilePath "$out/desktop.txt"
} finally {
 $buildPrefix=[IO.Path]::GetFullPath("$root/build")+[IO.Path]::DirectorySeparatorChar
 $helperDeadline=[DateTime]::UtcNow.AddSeconds(20)
 do {
  $remaining=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase)})
  if(!$remaining.Count){break}
  Start-Sleep -Milliseconds 500
 } while([DateTime]::UtcNow -lt $helperDeadline)
 & $py -B tools/rig/install_manifest.py capture --root $game --out "$evidence/install-after.json"
 if($LASTEXITCODE -ne 0){ '{"installDiff":"post-capture refused"}' | Set-Content "$out/install-stop.json"; throw 'STOP: install post-capture refused; report to lead' }
 & $py -B tools/rig/install_manifest.py diff --before "$evidence/install-before.json" --after "$evidence/install-after.json" --out "$evidence/install-diff.json"
 $installExit=$LASTEXITCODE
 if($installExit -ne 0){ "{`"installDiff`":$installExit}" | Set-Content "$out/install-stop.json"; throw "STOP: install comparison failed ($installExit); no further runs; report to lead" }
}
if($remaining.Count){throw 'owned build helper remains after bounded closure wait'}

$safetyFailure=$null
$notes=@()
try { & "$root/tools/rig/check-safety.ps1" -Tag "$Lane-$Attempt" -RepositoryRoot $root -GameDirectory $game -ProtectedSavesBaseline "$out/protected-saves.json" -ForeignBaseline "$out/source-before.json" -GameDirectoryBaseline "$out/game-root.json" -GameAssetsBaseline "$out/assets.json" -ReceiptPath "$out/safety-closed.json" } catch { $safetyFailure=$_.Exception.Message }
if($safetyFailure -and (Test-Path -LiteralPath "$out/safety-closed.json")){
 # The post-boss FMV can rewrite zmovie's mtime with identical bytes (PC1, VUH-2617). When the only findings
 # are root/asset mtimes, the file lists and sizes are unchanged and the full-install sha256 diff matched,
 # record a note instead of a failure. Any content, list or size change still fails.
 $receipt=Get-Content -Raw -LiteralPath "$out/safety-closed.json" | ConvertFrom-Json
 $findings=@($receipt.findings)
 $mtimeOnly=$findings.Count -gt 0 -and @($findings | Where-Object { $_ -notmatch 'file list, size or mtime changed' }).Count -eq 0
 $strip={ param($rows) ($rows | Select-Object * -ExcludeProperty lastWriteUtc | ConvertTo-Json -Depth 5 -Compress) }
 $sameShape=((& $strip (Root-Files)) -eq (& $strip $roots)) -and ((& $strip @(Asset-Files)) -eq (& $strip $assets))
 if($mtimeOnly -and $sameShape -and $installExit -eq 0){
  $after=@(Asset-Files); $afterRoot=@(Root-Files)
  $changed=@(for($i=0;$i -lt $assets.Count;$i++){ if($assets[$i].lastWriteUtc -ne $after[$i].lastWriteUtc){ "$($assets[$i].target)/$($assets[$i].path)" } }) +
   @(for($i=0;$i -lt $roots.Count;$i++){ if($roots[$i].lastWriteUtc -ne $afterRoot[$i].lastWriteUtc){ "<root>/$($roots[$i].name)" } })
  $notes+="mtime-only change with identical sha256: $($changed -join ', ')"
  $safetyFailure=$null
 }
}
Check-Products
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){throw 'owned relay port remains occupied'}
$closed=& "$root/build/tools/kh2ctl/Release/kh2ctl.exe" instances
$closed | Set-Content "$out/instances-closed.json" -Encoding UTF8
if(@(($closed | ConvertFrom-Json).instances | Where-Object owned).Count){throw 'owned game remains after closure'}

$reportDir=Get-ChildItem -LiteralPath "$root/build/scenarios" -Directory | Where-Object Name -like "*_${Report}_*" | Sort-Object Name -Descending | Select-Object -First 1
$status='no fresh report'
if($reportDir -and $reportDir.CreationTimeUtc -ge $startedUtc){ $status=(Get-Content -Raw -LiteralPath (Join-Path $reportDir.FullName 'report.json') | ConvertFrom-Json).status }
@{status=$status;report=$reportDir.FullName;commit=$commit;machine=$env:COMPUTERNAME;installDiffExit=$installExit;savesUnchanged=(@($saves | Where-Object {(Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $_.sha256}).Count -eq 0);safetyPassed=(!$safetyFailure);safetyFailure=$safetyFailure;notes=$notes;relayPortFree=$true;topology='single-PC loopback';finishedUtc=[DateTime]::UtcNow.ToString('o')} |
 ConvertTo-Json | Set-Content "$out/terminal.json" -Encoding UTF8
"$Lane $Attempt ${status}: $($reportDir.FullName)"
if($safetyFailure){throw "safety findings: $safetyFailure"}
