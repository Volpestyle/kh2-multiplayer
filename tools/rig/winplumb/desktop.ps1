param([string]$Worker,[string]$OperatorLogDir,[string]$RuntimeRoot,[string]$Packet)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'utf8.ps1')
foreach($path in @($Worker,$OperatorLogDir,$RuntimeRoot,$Packet)){if(-not $path -or -not [IO.Path]::IsPathRooted($path)){throw 'Explicit absolute Worker/OperatorLogDir/RuntimeRoot/Packet required; no PATH Python'}}
if(Test-Path -LiteralPath $OperatorLogDir){throw 'Fresh operator log directory required'}
$admission=Read-WinPlumbJson (Join-Path $RuntimeRoot 'runtime-admission.json')
if(-not $admission.ok -or -not $admission.importExited){throw 'Actual bundled311/Pillow admission required'}
$Python=Join-Path $RuntimeRoot 'python/python.exe'
if((Get-FileHash -LiteralPath $Python -Algorithm SHA256).Hash.ToLowerInvariant() -ne '5f7b89a612c9b8af1d6456cdfcd1dbe5ca630849e79aebced9bee9a6694952ec'){throw 'Bundled Python executable mismatch'}
foreach($entry in $admission.group.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $RuntimeRoot $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw 'Bundled runtime group changed'}
}
if((Get-FileHash -LiteralPath (Join-Path $Packet 'pins.json') -Algorithm SHA256).Hash.ToLowerInvariant() -ne $admission.packetPinsSha256){throw 'Runtime admission bound to another packet'}
foreach($entry in $admission.dependencyFiles.PSObject.Properties){
 if((Get-FileHash -LiteralPath (Join-Path $Packet $entry.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value){throw 'Pinned import dependencies changed'}
}
$priorUtf8=$env:PYTHONUTF8;$priorUnbuffered=$env:PYTHONUNBUFFERED;$priorLog=$env:PC2_OPERATOR_LOG_DIR
try{
 $env:PYTHONUTF8='1';$env:PYTHONUNBUFFERED='1';$env:PC2_OPERATOR_LOG_DIR=$OperatorLogDir
 # Embeddable Python's unchanged _pth omits the worker's directory. Add it
 # explicitly, then run the worker in THIS original interpreter process.
 $entry="import runpy,sys;from pathlib import Path;w=Path(sys.argv[1]).resolve();sys.path.insert(0,str(w.parent));sys.argv=[str(w)];runpy.run_path(str(w),run_name='__main__')"
 & $Python -u -B -c $entry $Worker
 if($LASTEXITCODE -ne 0){throw 'Friend worker failed; no retry; inspect outside-packet operator log and independently close owned processes'}
}finally{$env:PYTHONUTF8=$priorUtf8;$env:PYTHONUNBUFFERED=$priorUnbuffered;$env:PC2_OPERATOR_LOG_DIR=$priorLog}
