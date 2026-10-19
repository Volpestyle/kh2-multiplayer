param([Parameter(Mandatory=$true)][string]$Go,[Parameter(Mandatory=$true)][string]$Consent)
# Session-1 desk job on bld: verify the sealed packet, ADOPT, GO and consent; copy the payload to a
# fresh folder (one attempt per packet) and run lane.py with the payload's own CPython (-I).
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$env:PYTHONUTF8='1'
$pins=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'pins.json') -Raw | ConvertFrom-Json
foreach($row in $pins.files){
 $path=Join-Path $PSScriptRoot $row.path
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLower() -ne $row.sha256){throw "Pinned file mismatch: $($row.path)"}
}
$hash=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'pins.json') -Algorithm SHA256).Hash.ToLower()
$review=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'lead-review.json') -Raw | ConvertFrom-Json
if($review.status -ne 'ADOPT' -or $review.pinsSha256 -ne $hash){throw 'Lead ADOPT required'}
$g=Get-Content -LiteralPath $Go -Raw | ConvertFrom-Json
if($g.pinsSha256 -ne $hash -or $g.lane -ne $pins.packet){throw 'GO does not name this packet'}
if((Get-FileHash -LiteralPath $Consent -Algorithm SHA256).Hash.ToLower() -ne $g.consentSha256){throw 'Consent differs from GO'}
if($g.runId -notmatch '^[A-Za-z0-9-]{8,64}$'){throw 'Invalid RunId'}
$homeRoot=[Environment]::GetFolderPath('UserProfile')
$unpack=Join-Path $homeRoot ('KH2-PC2-'+$g.runId)
if(Test-Path -LiteralPath $unpack){throw 'One attempt only; unpack already exists'}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'payload') -Destination $unpack -Recurse
$logRoot=Join-Path $homeRoot ('lead-outbox/operator-logs/pc2-'+$g.runId)
New-Item -ItemType Directory -Path $logRoot | Out-Null
$evidence=Join-Path $homeRoot ('lead-outbox/pc2-'+$g.runId)
# A native child's stderr must never become a terminating NativeCommandError mid-run (lane.py also
# folds stderr into stdout); -X utf8 because -I ignores PYTHONUTF8.
$ErrorActionPreference='Continue'
& (Join-Path $unpack 'python/python.exe') -I -X utf8 -B (Join-Path $unpack 'lane.py') --consent $Consent --go $Go --evidence $evidence 2>&1 | Tee-Object -LiteralPath (Join-Path $logRoot 'operator.log')
if($LASTEXITCODE -ne 0){throw "Lane failed/refused: $LASTEXITCODE; see $logRoot\operator.log"}
