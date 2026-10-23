param([string]$Commit = '')
# PC1 desk job (VUH-2339): build one exact commit for bld in C:/kh2rig/wt. Queued by the PC1 desk owner
# between live runs; never prompts (no Mandatory parameters).
#   & 'C:/kh2rig/ops/build-job.ps1' -Commit <full sha>
$ErrorActionPreference='Stop'
$own='C:/kh2rig/ops'
if((Get-Process -Id $PID).SessionId -eq 0){throw 'desktop queue required'}
if($Commit -notmatch '^[0-9a-f]{40}$'){throw 'full commit sha required'}
$foreign=@(Get-Process | Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero -and ($_.ProcessName -match 'KINGDOM HEARTS|Marvel-Win64-Shipping|r5apex|FortniteClient|VALORANT-Win64|Overwatch|eldenring' -or ($_.Path -match '\\steamapps\\common\\|\\Epic Games\\|\\Riot Games\\|\\EA Games\\|\\XboxGames\\' -and $_.ProcessName -notmatch '^RiotClient|^Epic(GamesLauncher|WebHelper|OnlineServices)|^EOSOverlay')) })
if($foreign.Count){throw 'PC1 occupied; no build'}
(Get-Process -Id $PID).PriorityClass='BelowNormal'
Set-Location -LiteralPath 'C:/kh2rig/wt'
$env:PYTHONUTF8='1'
$ErrorActionPreference='Continue'
New-Item -ItemType Directory -Force "$own/logs" | Out-Null
& C:/Users/volpe/AppData/Local/Programs/Python/Python311/python.exe -B "$own/build.py" $Commit *> "$own/logs/build-$Commit.log"
$code=$LASTEXITCODE
"exit=$code" | Set-Content "$own/logs/build-$Commit.exit"
"VUH2339 bld build $Commit exit=$code; log=$own/logs/build-$Commit.log"
