param([switch]$DryRunHandoff,[string]$ProtectedSavesBaseline)
$ErrorActionPreference='Stop'
$repo='C:/Users/volpe/repos/kh2-multiplayer'
$packet=Join-Path $repo 'build/rig/vuh1819-downed-spectate-20261007-01'
$jobOut=Join-Path $packet 'desktop-output'
$python='C:/Users/volpe/AppData/Local/Programs/Python/Python311/python.exe'
$game='C:/Program Files (x86)/Steam/steamapps/common/KINGDOM HEARTS -HD 1.5+2.5 ReMIX-'
function Test-SaveHandoff([string]$baseline) {
 & $python -B "$packet/run.py" --packet "$packet" --dry-run-handoff --protected-saves-baseline $baseline
 if($LASTEXITCODE -ne 0){throw 'desktop-to-arm protected-save handoff failed'}
}
if($DryRunHandoff){
 if(!$ProtectedSavesBaseline){throw 'dry run requires explicit protected-save baseline'}
 Test-SaveHandoff $ProtectedSavesBaseline
 return
}
$code=$null;$errorText=$null;$child=$null;$made=$false;$savedOptions=@{}
try {
 if($PID -ne 522932 -or (Get-Process -Id $PID).SessionId -ne 1){throw 'existing owned Session1 bridge required'}
 Set-Location -LiteralPath $repo
 & $python -B "$packet/run.py" --packet "$packet" --check
 if($LASTEXITCODE -ne 0){throw 'sealed offline pin/scenario check failed'}
 if(!(Test-Path -LiteralPath "$packet/adopt.json")){throw 'PENDING packet requires exact-seal lead adoption'}
 if(Test-Path -LiteralPath "$packet/attempt-spent.json"){throw 'single attempt already spent'}
 if(Test-Path -LiteralPath "$repo/build/rig/rig.lock"){throw 'live rig occupied'}
 if(Test-Path -LiteralPath "$repo/build/rig/ctest.lock"){throw 'offline test lane occupied'}
 New-Item -ItemType Directory -Path $jobOut -ErrorAction Stop | Out-Null;$made=$true
 # Existing closure helper: retain complete source/save/game baselines. The
 # continuously changing tool transcript cache-raw.log is excluded explicitly.
 $saves=@(Get-ChildItem -LiteralPath "$env:USERPROFILE/OneDrive/Documents/My Games/KINGDOM HEARTS HD 1.5+2.5 ReMIX" -File -Recurse | ForEach-Object { @{path=$_.FullName;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()} })
 ConvertTo-Json -InputObject $saves -Depth 5 | Set-Content "$jobOut/protected-saves.json" -Encoding UTF8
 Test-SaveHandoff "$jobOut/protected-saves.json" | Set-Content "$jobOut/save-handoff.jsonl" -Encoding UTF8
 $foreign=@(& git -c safe.directory=C:/Users/volpe/repos/kh2-multiplayer ls-files | Where-Object {$_ -ne 'cache-raw.log' -and (Test-Path -LiteralPath (Join-Path $repo $_) -PathType Leaf)} | ForEach-Object { @{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_) -Algorithm SHA256).Hash.ToLowerInvariant()} })
 if($LASTEXITCODE -ne 0){throw 'scoped git inventory failed'}
 ConvertTo-Json -InputObject $foreign -Depth 5 | Set-Content "$jobOut/foreign.json" -Encoding UTF8
 $roots=@(Get-ChildItem -LiteralPath $game -File | Sort-Object Name | ForEach-Object { [pscustomobject]@{name=$_.Name;bytes=$_.Length;lastWriteUtc=$_.LastWriteTimeUtc.ToString('o')} })
 ConvertTo-Json -InputObject $roots -Depth 5 | Set-Content "$jobOut/game-root.json" -Encoding UTF8
 $assets=@(foreach($target in @('Image','STEAM')) { $directory=Join-Path $game $target; Get-ChildItem -LiteralPath $directory -File -Recurse | ForEach-Object { [pscustomobject]@{target=$target;path=$_.FullName.Substring($directory.Length+1);bytes=$_.Length;lastWriteUtc=$_.LastWriteTimeUtc.ToString('o')} } } )
 $assets=@($assets | Sort-Object target,path)
 ConvertTo-Json -InputObject $assets -Depth 5 | Set-Content "$jobOut/assets.json" -Encoding UTF8
 $keys=@(Get-ChildItem Env: | Where-Object {$_.Name -like 'KH2COOP_*' -or $_.Name -eq 'KH2_GAME_DIR'} | ForEach-Object {$_.Name})
 foreach($key in $keys){$savedOptions[$key]=[Environment]::GetEnvironmentVariable($key,'Process');[Environment]::SetEnvironmentVariable($key,$null,'Process')}
 $start=New-Object System.Diagnostics.ProcessStartInfo
 $start.FileName=$python;$start.Arguments="-B -u `"$packet/run.py`" --packet `"$packet`" --execute --protected-saves-baseline `"$jobOut/protected-saves.json`""
 $start.WorkingDirectory=$repo;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true;$start.RedirectStandardInput=$true
 $child=New-Object System.Diagnostics.Process;$child.StartInfo=$start
 if(!$child.Start()){throw 'runner start failed'}
 @{pid=$child.Id;utc=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json | Set-Content "$packet/attempt-spent.json" -Encoding UTF8
 $ownedHandle=$child.Handle;$child.StandardInput.Close()
 $stdout=$child.StandardOutput.ReadToEndAsync();$stderr=$child.StandardError.ReadToEndAsync()
 @{pid=$child.Id;creationUtc=$child.StartTime.ToUniversalTime().ToString('o');bridge=$PID;arguments=$start.Arguments} | ConvertTo-Json | Set-Content "$jobOut/started.json" -Encoding UTF8
 if(!$child.WaitForExit(900000)){throw 'owned runner exceeded 900s; retain identity for exact owned cleanup; never kill every game'}
 $child.WaitForExit();$code=$child.ExitCode
 [IO.File]::WriteAllText("$jobOut/stdout.txt",$stdout.Result);[IO.File]::WriteAllText("$jobOut/stderr.txt",$stderr.Result)
 & "$repo/tools/rig/check-safety.ps1" -Tag 'downed-spectate-pc1' -RepositoryRoot $repo -GameDirectory $game -ProtectedSavesBaseline "$jobOut/protected-saves.json" -ForeignBaseline "$jobOut/foreign.json" -GameDirectoryBaseline "$jobOut/game-root.json" -GameAssetsBaseline "$jobOut/assets.json" -ReceiptPath "$jobOut/safety.json"
 if(Get-NetUDPEndpoint -LocalPort 27796 -ErrorAction SilentlyContinue){throw 'fixture relay port still occupied after closure'}
} catch {$errorText=$_.ToString()}
finally {
 foreach($key in $savedOptions.Keys){[Environment]::SetEnvironmentVariable($key,$savedOptions[$key],'Process')}
 if($made){ @{exitCode=$code;error=$errorText;finishedUtc=[DateTime]::UtcNow.ToString('o');childPid=if($child){$child.Id}else{$null}} | ConvertTo-Json | Set-Content "$jobOut/terminal.json" -Encoding UTF8 }
 else { Write-Output "desktop job refused before start: $errorText" }
}
