[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^/[A-Za-z0-9_./-]+$')][string]$RemoteBase,
    [Parameter(Mandatory=$true)][string]$MacRemoteScript,
    [Parameter(Mandatory=$true)][string]$ReceiptRoot,
    [Parameter(Mandatory=$true)]
    [ValidateSet('start','status','stop','fetch')]
    [string]$Action,
    [Parameter(Mandatory=$true)]
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$')]
    [string]$RunId
)
$ErrorActionPreference = 'Stop'
# RemoteBase is the existing installed producer directory, never created/reconfigured here.
if ($Action -ne 'fetch') {
    & $MacRemoteScript "/usr/bin/python3 $remoteBase/relay-control.py $Action $RunId"
    exit $LASTEXITCODE
}
# Keep every fetch as a distinct snapshot; never overwrite another run or receipt.
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$destination = Join-Path $ReceiptRoot "fetched-$RunId-$stamp"
New-Item -ItemType Directory -Path $destination | Out-Null
& 'C:/Program Files/Git/usr/bin/scp.exe' -o BatchMode=yes -F C:/Users/volpe/.ssh/config -r "mac:$remoteBase/runs/$RunId" $destination
if ($LASTEXITCODE -ne 0) { throw "Receipt fetch failed; partial snapshot retained at $destination" }
$receipt = Join-Path $destination $RunId
$complete = Test-Path -LiteralPath (Join-Path $receipt 'exit.json')
[pscustomobject]@{path=$receipt; complete=$complete; note='A running snapshot is not an exit receipt.'} | ConvertTo-Json
