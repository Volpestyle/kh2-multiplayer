param(
    [Parameter(Mandatory=$true)][string]$RehearsalRoot,
    [Parameter(Mandatory=$true)][string]$GameViewRoot,
    [Parameter(Mandatory=$true)][string]$ReceiptDirectory,
    [Parameter(Mandatory=$true)][int]$GamePid,
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][ValidateSet('ENTER','DOWN','UP','LEFT','RIGHT','ESCAPE','W','A','S','D','SPACE','F','R','M','Q','E','SHIFT')][string]$Key,
    [ValidateRange(80,1500)][int]$HoldMs = 180,
    [Parameter(Mandatory=$true)][ValidatePattern('^[a-z0-9-]+$')][string]$EvidenceTag
)
$ErrorActionPreference = 'Stop'
if ((Get-Process -Id $PID).SessionId -ne 1) { throw 'Keyboard stand-in requires the existing desktop bridge in session 1.' }
$package = [IO.Path]::GetFullPath($PackageRoot)
$proof = [IO.Path]::GetFullPath($RehearsalRoot)
if (!$package.StartsWith([IO.Path]::GetFullPath($proof) + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Wrong rehearsal package root.' }
if (!(Test-Path -LiteralPath (Join-Path $package 'KH2COOP-PACKAGE') -PathType Leaf)) { throw 'Package marker missing.' }
$owned = @(Get-Content -LiteralPath (Join-Path $package 'build/rig/owned.txt') | Where-Object { $_.Trim() -match ('^' + $GamePid + ' [0-9]+$') })
if ($owned.Count -ne 1) { throw 'PID is not retained in this package ownership file.' }
$ownedCreated = [long]($owned[0].Trim().Split(' ')[1])
$retainedProcess = Get-Process -Id $GamePid
if ($retainedProcess.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $ownedCreated) { throw 'Owned PID creation time differs; no input sent.' }
$receipts = @(Get-ChildItem -LiteralPath (Join-Path $package 'tools/launcher/.local/runs') -Filter 'launch-result.json' -File -Recurse | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json } | Where-Object { $_.ready -and $_.receipt.processId -eq $GamePid -and $_.receipt.hooksInstalled })
if ($receipts.Count -ne 1) { throw 'Exactly one attested owned launch receipt is required.' }
$game = Get-CimInstance Win32_Process -Filter "ProcessId=$GamePid"
$allowed = @('C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\KINGDOM HEARTS II FINAL MIX.exe', (Join-Path $GameViewRoot 'KINGDOM HEARTS II FINAL MIX.exe')) | ForEach-Object { [IO.Path]::GetFullPath($_) }
if (!$game -or $game.SessionId -ne 1 -or $game.ExecutablePath -notin $allowed) { throw 'Wrong game identity or desktop session.' }
if ((Get-FileHash -LiteralPath $game.ExecutablePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne '9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed') { throw 'Unexpected game EXE.' }
$outDir = [IO.Path]::GetFullPath($ReceiptDirectory)
[IO.Directory]::CreateDirectory($outDir) | Out-Null
$outFile = Join-Path $outDir ($EvidenceTag + '.json')
if (Test-Path -LiteralPath $outFile) { throw 'Evidence tag already used; never retry a key silently.' }
if (!('RehearsalScanKey' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RehearsalScanKey {
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public UNION data; }
  [StructLayout(LayoutKind.Explicit)] public struct UNION {
    [FieldOffset(0)] public MOUSE mouse;
    [FieldOffset(0)] public KEYBOARD keyboard;
  }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSE { public int x,y; public uint data,flags,time; public UIntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBOARD { public ushort vk,scan; public uint flags,time; public UIntPtr extra; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code,uint kind);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
  public static uint ForegroundPid() { uint p; GetWindowThreadProcessId(GetForegroundWindow(), out p); return p; }
  public static bool Send(ushort vk,ushort scan,bool extended,bool up) {
    var input=new INPUT(); input.type=1;
    input.data.keyboard=new KEYBOARD { vk=vk,scan=scan,flags=(extended?1u:0u)|(up?2u:0u) };
    return SendInput(1,new[]{input},Marshal.SizeOf(typeof(INPUT)))==1;
  }
}
'@
}
$map = @{ENTER=13;DOWN=40;UP=38;LEFT=37;RIGHT=39;ESCAPE=27;W=87;A=65;S=83;D=68;SPACE=32;F=70;R=82;M=77;Q=81;E=69;SHIFT=160}
$vk = [uint16]$map[$Key]; $scan = [uint16][RehearsalScanKey]::MapVirtualKeyW($vk,0)
if ($scan -eq 0) { throw 'No physical scan code for key.' }
$extended = $Key -in @('DOWN','UP','LEFT','RIGHT')
$record = [ordered]@{tag=$EvidenceTag;role='physical keyboard stand-in, no memory or mod control';key=$Key;vk=$vk;scan=$scan;extended=$extended;holdMs=$HoldMs;gamePid=$GamePid;gameExe=$game.ExecutablePath;gameCreatedUtc=$game.CreationDate.ToUniversalTime().ToString('o');ownedCreationFileTime=$ownedCreated;startedUtc=[DateTime]::UtcNow.ToString('o');sentDown=$false;sentUp=$false;success=$false}
$down = $false
try {
    $proc = Get-Process -Id $GamePid
    if ($proc.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $ownedCreated) { throw 'Owned PID changed before input.' }
    if ($proc.MainWindowHandle -eq [IntPtr]::Zero) { throw 'No game window.' }
    [RehearsalScanKey]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
    Start-Sleep -Milliseconds 100
    if ([RehearsalScanKey]::ForegroundPid() -ne $GamePid) { throw 'Game is not foreground; no input sent.' }
    foreach ($modifier in @(16,17,18,91,92)) { if (([RehearsalScanKey]::GetAsyncKeyState($modifier) -band 0x8000) -ne 0) { throw 'Physical modifier is held; no input sent.' } }
    if (![RehearsalScanKey]::Send($vk,$scan,$extended,$false)) { throw 'Key down failed.' }
    $down=$true; $record.sentDown=$true
    $until=[DateTime]::UtcNow.AddMilliseconds($HoldMs)
    do { Start-Sleep -Milliseconds 20; if ([RehearsalScanKey]::ForegroundPid() -ne $GamePid) { throw 'Focus changed during key; release immediately.' } } while ([DateTime]::UtcNow -lt $until)
    $record.success=$true
} catch { $record.error=[string]$_; throw } finally {
    if ($down) { $record.sentUp=[RehearsalScanKey]::Send($vk,$scan,$extended,$true); if (!$record.sentUp) { $record.success=$false; $record.releaseError='Key release failed.' } }
    $record.finishedUtc=[DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText($outFile,($record | ConvertTo-Json -Depth 4),(New-Object System.Text.UTF8Encoding($false)))
    $record | ConvertTo-Json -Depth 4
}
if (!$record.success) { throw 'Keyboard stand-in did not complete.' }
