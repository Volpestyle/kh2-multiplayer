param([string]$Attempt = '', [string]$Lane = 'bot01-replay', [int]$Port = 27809, [switch]$Clear, [string]$MarkerUtc = '')
# bld (VUH-2339): redo a killed run's closing checks. A reclaim ends live.ps1 before its finally block, so the
# gate writes C:/desk/gate/needs-verify.json and refuses launches. Run this over SSH (it launches nothing):
#   powershell -NoProfile -File C:/kh2rig/ops/verify-after.ps1 -Attempt reclaim01-ac5280e-bld [-Clear -MarkerUtc <utc>]
# It repeats live.ps1's closing checks against the attempt's own baselines: full-install sha256 diff,
# check-safety.ps1 (saves, sources, game root, Image/STEAM), no new file in the save folder, staged products,
# relay port, and no owned game or build helper left. Each run adds verify-after-<utc>.json to the attempt's
# evidence. -Clear then deletes needs-verify.json only when every check passed, the marker is a reclaim (an
# `exit N` marker records a failed run for the lead to read), its utc equals -MarkerUtc (read it from the
# marker and the reclaim receipt) and it was written after this attempt's install baseline.
$ErrorActionPreference='Stop'
$env:PATH="C:/Program Files/Git/cmd;$env:PATH"
$root='C:/kh2rig/wt'
$game='C:/Program Files (x86)/Steam/steamapps/common/KINGDOM HEARTS -HD 1.5+2.5 ReMIX-'
$saveDir="$env:USERPROFILE/OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX"
$py="$env:LOCALAPPDATA/Programs/Python/Python311/python.exe"
$verify='C:/desk/gate/needs-verify.json'
if($Attempt -notmatch '^[A-Za-z0-9-]{4,64}$'){throw 'explicit -Attempt required'}
if(Test-Path -LiteralPath 'C:/desk/gate/lease.json'){throw 'desk lease held; inspect C:/desk/gate/reclaims first'}
$evidence=".local/evidence/$Lane/$Attempt"
$out="$root/$evidence"
foreach($f in 'install-before.json','protected-saves.json','source-before.json','game-root.json','assets.json'){
 if(!(Test-Path -LiteralPath "$out/$f")){throw "missing $out/$($f): the run stopped before its baselines"}
}
$stamp=[DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
Set-Location -LiteralPath $root
$failures=@()
$notes=@()
if(Test-Path -LiteralPath "$out/install-stop.json"){$failures+="live.ps1 stopped on the install comparison (install-stop.json); report to lead"}

try {
 $commit=(& git rev-parse HEAD)
 $built=Get-Content -Raw -LiteralPath "C:/kh2rig/products/$commit/products.json" | ConvertFrom-Json
 foreach($row in $built.products){
  if((Get-FileHash -LiteralPath (Join-Path $root $row.path) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256){$failures+="staged product differs: $($row.path)"}
 }
} catch { $failures+="products check: $($_.Exception.Message)" }
if(Get-NetUDPEndpoint -LocalPort $Port -ErrorAction SilentlyContinue){$failures+="relay port $Port occupied"}
$helpers=@(); $owned=@(); $kh2ctlExit=$null; $captureExit=$null; $diffExit=$null
try {
 $buildPrefix=[IO.Path]::GetFullPath("$root/build")+[IO.Path]::DirectorySeparatorChar
 $helpers=@(Get-CimInstance Win32_Process | Where-Object {$_.ExecutablePath -and $_.ExecutablePath.StartsWith($buildPrefix,[StringComparison]::OrdinalIgnoreCase)} | ForEach-Object { "$($_.Name) $($_.ProcessId)" })
 if($helpers.Count){$failures+="build helpers running: $($helpers -join ', ')"}
 $listing=& "$root/build/tools/kh2ctl/Release/kh2ctl.exe" instances
 $kh2ctlExit=$LASTEXITCODE
 $instances=$null; try { $instances=$listing | ConvertFrom-Json } catch {}
 $owned=@()
 if($kh2ctlExit -ne 0 -or !$instances -or $instances.ok -ne $true -or $null -eq $instances.PSObject.Properties['instances']){$failures+="kh2ctl instances failed (exit $kh2ctlExit)"}
 else { $owned=@($instances.instances | Where-Object owned | ForEach-Object { $_.processId }); if($owned.Count){$failures+="owned game running: $($owned -join ', ')"} }
} catch { $failures+="process check: $($_.Exception.Message)" }

$env:PYTHONUTF8='1'
try {
 & $py -B tools/rig/install_manifest.py capture --root $game --out "$evidence/install-after-verify-$stamp.json"
 $captureExit=$LASTEXITCODE
 $diffExit=$null
 if($captureExit -eq 0){
  & $py -B tools/rig/install_manifest.py diff --before "$evidence/install-before.json" --after "$evidence/install-after-verify-$stamp.json" --out "$evidence/install-diff-verify-$stamp.json"
  $diffExit=$LASTEXITCODE
 }
} catch { $failures+="install manifest: $($_.Exception.Message)" }
if($diffExit -ne 0){$failures+="install manifest: capture exit $captureExit, diff exit $diffExit"}

$saves=@(); $added=@()
try {
 $raw=Get-Content -Raw -LiteralPath "$out/protected-saves.json" | ConvertFrom-Json
 $saves=@($raw)
 if($saves.Count -lt 4){$failures+="protected save baseline has $($saves.Count) files"}
 $known=@{}; foreach($s in $saves){ $known[[IO.Path]::GetFullPath($s.path).ToLowerInvariant()]=$true }
 $added=@(Get-ChildItem -LiteralPath $saveDir -File -Recurse | Where-Object { !$known.ContainsKey($_.FullName.ToLowerInvariant()) } | ForEach-Object { $_.FullName })
 if($added.Count){$failures+="new files in the save folder: $($added -join ', ')"}
} catch { $failures+="save check: $($_.Exception.Message)" }

# check-safety.ps1 re-hashes the saves and sources and compares the game root and Image/STEAM listings.
# live.ps1 accepts an mtime-only listing change when the sha256 diff matched; the same rule applies here.
$receipt="$out/safety-verify-$stamp.json"
try { & "$root/tools/rig/check-safety.ps1" -Tag "$Lane-$Attempt-verify" -RepositoryRoot $root -GameDirectory $game -ProtectedSavesBaseline "$out/protected-saves.json" -ForeignBaseline "$out/source-before.json" -GameDirectoryBaseline "$out/game-root.json" -GameAssetsBaseline "$out/assets.json" -ReceiptPath $receipt | Out-Null }
catch {
 $safetyError=$_.Exception.Message
 try {
  $findings=@(); if(Test-Path -LiteralPath $receipt){ $findings=@((Get-Content -Raw -LiteralPath $receipt | ConvertFrom-Json).findings) }
  $mtimeOnly=$findings.Count -gt 0 -and @($findings | Where-Object { $_ -notmatch 'file list, size or mtime changed' }).Count -eq 0
  $strip={ param($rows) (@($rows) | Select-Object * -ExcludeProperty lastWriteUtc | ConvertTo-Json -Depth 5 -Compress) }
  $rootNow=@(Get-ChildItem -LiteralPath $game -File | Sort-Object Name | ForEach-Object { [pscustomobject]@{name=$_.Name;bytes=$_.Length} })
  $assetsNow=@(foreach($target in @('Image','STEAM')) { $dir=Join-Path $game $target; Get-ChildItem -LiteralPath $dir -File -Recurse |
   ForEach-Object { [pscustomobject]@{target=$target;path=$_.FullName.Substring($dir.Length+1);bytes=$_.Length} } }) | Sort-Object target,path
  $rootBefore=Get-Content -Raw -LiteralPath "$out/game-root.json" | ConvertFrom-Json
  $assetsBefore=Get-Content -Raw -LiteralPath "$out/assets.json" | ConvertFrom-Json
  $sameShape=((& $strip $rootNow) -eq (& $strip $rootBefore)) -and ((& $strip $assetsNow) -eq (& $strip $assetsBefore))
  if($mtimeOnly -and $sameShape -and $diffExit -eq 0){ $notes+='mtime-only listing change with identical sha256' }
  else { $failures+="check-safety: $safetyError $($findings -join '; ')" }
 } catch { $failures+="check-safety: $safetyError; mtime rule: $($_.Exception.Message)" }
}

$gate=$null; $ours=$false; $clearable=$false
try {
 if(Test-Path -LiteralPath $verify){ $gate=Get-Content -Raw -LiteralPath $verify | ConvertFrom-Json }
 $baselineUtc=[datetime]::Parse((Get-Content -Raw -LiteralPath "$out/install-before.json" | ConvertFrom-Json).capturedUtc,$null,'RoundtripKind').ToUniversalTime()
 if($gate){
  $ours=[datetime]::Parse([string]$gate.utc,$null,'RoundtripKind').ToUniversalTime() -gt $baselineUtc
  $clearable=$ours -and $MarkerUtc -and ([string]$gate.utc -eq $MarkerUtc) -and ([string]$gate.reason -notmatch '^exit ')
 }
} catch { $failures+="marker or baseline unreadable: $($_.Exception.Message)" }
$ok=!$failures.Count
$result=[ordered]@{ok=$ok;attempt=$Attempt;failures=$failures;notes=$notes;needsVerify=$gate;markerAfterBaseline=[bool]$ours;clearable=[bool]$clearable;
 installCaptureExit=$captureExit;installDiffExit=$diffExit;savesChecked=$saves.Count;savesAdded=$added;ownedGames=$owned;buildHelpers=$helpers;
 safetyReceipt=$receipt;utc=[DateTime]::UtcNow.ToString('o')}
$json=$result | ConvertTo-Json -Depth 5
$json | Set-Content -LiteralPath "$out/verify-after-$stamp.json" -Encoding UTF8
$json
if(!$ok){ exit 1 }
if($Clear){
 if(!$clearable){ throw "needs-verify.json not cleared: it must be a reclaim marker written after this attempt's baseline, with utc equal to -MarkerUtc" }
 Remove-Item -LiteralPath $verify
 [ordered]@{cleared=$verify;marker=$gate;by="verify-after-$stamp.json";utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath "$out/verify-cleared-$stamp.json" -Encoding UTF8
 "cleared $verify"
}
