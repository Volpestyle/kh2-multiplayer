param(
    [Parameter(Mandatory=$true)][string]$Tag,
    [Parameter(Mandatory=$true)][string]$RepositoryRoot,
    [Parameter(Mandatory=$true)][string]$GameDirectory,
    [Parameter(Mandatory=$true)][string]$ProtectedSavesBaseline,
    [Parameter(Mandatory=$true)][string]$ForeignBaseline,
    [Parameter(Mandatory=$true)][string]$GameDirectoryBaseline,
    [Parameter(Mandatory=$true)][string]$GameAssetsBaseline,
    [Parameter(Mandatory=$true)][string]$ReceiptPath
)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath($RepositoryRoot)
$game = [IO.Path]::GetFullPath($GameDirectory)
$findings = @()
$saves = Get-Content -LiteralPath $ProtectedSavesBaseline -Raw | ConvertFrom-Json
foreach ($row in $saves) {
    if (!(Test-Path -LiteralPath $row.path -PathType Leaf) -or (Get-FileHash -LiteralPath $row.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256) { $findings += "Protected save changed: $($row.path)" }
}
$foreign = Get-Content -LiteralPath $ForeignBaseline -Raw | ConvertFrom-Json
foreach ($row in $foreign) {
    $file = Join-Path $repo $row.path
    if (!(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant() -ne $row.sha256) { $findings += "Foreign file changed: $($row.path)" }
}
$actualRoot = @(Get-ChildItem -LiteralPath $game -File | Sort-Object Name | ForEach-Object { [pscustomobject]@{name=$_.Name;bytes=$_.Length;lastWriteUtc=$_.LastWriteTimeUtc.ToString('o')} })
$beforeRoot = Get-Content -LiteralPath $GameDirectoryBaseline -Raw | ConvertFrom-Json
if (($actualRoot | ConvertTo-Json -Depth 5 -Compress) -ne ($beforeRoot | Sort-Object name | ConvertTo-Json -Depth 5 -Compress)) { $findings += 'Installed game root file list, size or mtime changed.' }
$actualAssets = @()
foreach ($target in @('Image','STEAM')) {
    $directory = Join-Path $game $target
    if (!(Test-Path -LiteralPath $directory -PathType Container)) { throw "Junction target missing: $directory" }
    $actualAssets += @(Get-ChildItem -LiteralPath $directory -File -Recurse | ForEach-Object { [pscustomobject]@{target=$target;path=$_.FullName.Substring($directory.Length+1);bytes=$_.Length;lastWriteUtc=$_.LastWriteTimeUtc.ToString('o')} })
}
$actualAssets = @($actualAssets | Sort-Object target,path)
$beforeAssets = Get-Content -LiteralPath $GameAssetsBaseline -Raw | ConvertFrom-Json
if (($actualAssets | ConvertTo-Json -Depth 5 -Compress) -ne ($beforeAssets | Sort-Object target,path | ConvertTo-Json -Depth 5 -Compress)) { $findings += 'Installed Image/STEAM file list, size or mtime changed.' }
$processes = @(Get-CimInstance Win32_Process -Filter "Name='KINGDOM HEARTS II FINAL MIX.exe' OR Name='kh2coop_runtime_scaffold.exe' OR Name='avatarctl.exe'" | Select-Object ProcessId,ParentProcessId,CreationDate,ExecutablePath)
$result = [pscustomobject]@{tag=$Tag;utc=[DateTime]::UtcNow.ToString('o');saveFiles=$saves.Count;foreignFiles=$foreign.Count;gameRootFiles=$actualRoot.Count;assetFiles=$actualAssets.Count;findings=$findings;processes=$processes}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ReceiptPath -Encoding UTF8
$result | ConvertTo-Json -Depth 8
if ($findings.Count) { throw 'Safety comparison has findings; inspect the retained receipt.' }
