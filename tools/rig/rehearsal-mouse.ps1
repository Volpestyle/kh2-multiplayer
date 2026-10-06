param(
    [Parameter(Mandatory=$true)][string]$RehearsalRoot,
    [Parameter(Mandatory=$true)][string]$GameViewRoot,
    [Parameter(Mandatory=$true)][string]$ReceiptDirectory,
    [Parameter(Mandatory=$true)][int]$GamePid,
    [Parameter(Mandatory=$true)][long]$CreationFileTime,
    [Parameter(Mandatory=$true)][long]$GameHwnd,
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][int]$SignedDx,
    [Parameter(Mandatory=$true)][int]$SignedDy,
    [int]$DurationMs = 0,
    [Parameter(Mandatory=$true)][string]$EvidenceTag,
    [switch]$ValidateOnly,
    [switch]$Execute
)
$ErrorActionPreference = 'Stop'

# Pure request validation must finish before any file/process/UI/input operation.
function Test-MouseRequest {
    param([int]$TargetPid,[long]$Created,[long]$Hwnd,[string]$Package,[int]$Dx,[int]$Dy,[int]$Duration,[string]$Tag,[string]$ScopeRoot)
    if ($TargetPid -le 0 -or $Created -le 0 -or $Hwnd -le 0) { throw 'Positive exact PID/creation/HWND required.' }
    if ($Dx -lt -32 -or $Dx -gt 32 -or $Dy -lt -32 -or $Dy -gt 32 -or
        ([Math]::Abs([long]$Dx) + [Math]::Abs([long]$Dy)) -gt 32 -or ($Dx -eq 0 -and $Dy -eq 0)) { throw 'Require nonzero relative motion, total absolute counts <=32.' }
    if ($Duration -ne 0) { throw 'One instantaneous move only; duration must be zero.' }
    if ($Tag -cnotmatch '^[a-z0-9][a-z0-9-]{0,63}$') { throw 'Invalid unique evidence tag.' }
    $lane = [IO.Path]::GetFullPath($ScopeRoot)
    $allowed = @("$lane\Host Fresh\KH2-Co-op", "$lane\Guest Fresh\KH2-Co-op")
    $canonical = [IO.Path]::GetFullPath($Package)
    if ($canonical -notin $allowed) { throw 'Only exact declared Host/Guest package roots allowed.' }
    return $canonical
}
$package = Test-MouseRequest $GamePid $CreationFileTime $GameHwnd $PackageRoot $SignedDx $SignedDy $DurationMs $EvidenceTag $RehearsalRoot
if ($ValidateOnly) {
    if ($Execute) { throw 'Validation and execution cannot be combined.' }
    [pscustomobject]@{validated=$true;liveChecksPerformed=$false;inputSent=$false;package=$package} | ConvertTo-Json
    return
}
if (!$Execute) { throw 'Root must explicitly supply -Execute after independent review.' }

# Exclusive append-only receipt. A crash after send-intent means outcome unknown, never retry.
$outDir = [IO.Path]::GetFullPath($ReceiptDirectory)
[IO.Directory]::CreateDirectory($outDir) | Out-Null
$outFile = Join-Path $outDir ($EvidenceTag + '.jsonl')
$receiptStream = [IO.File]::Open($outFile,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
function Write-MouseReceipt($value) {
    $bytes = [Text.Encoding]::UTF8.GetBytes(($value | ConvertTo-Json -Depth 5 -Compress) + "`n")
    $receiptStream.Write($bytes,0,$bytes.Length)
    $receiptStream.Flush($true)
}
$record = [ordered]@{phase='reserved';tag=$EvidenceTag;gamePid=$GamePid;creationFileTime=$CreationFileTime;hwnd=$GameHwnd;package=$package;signedDx=$SignedDx;signedDy=$SignedDy;durationMs=0;flags=1;attempted=$false;sendReturned=$false;sentCount=$null;success=$false;pixelsVerified=$false;startedUtc=[DateTime]::UtcNow.ToString('o')}
try {
    Write-MouseReceipt $record
    if ([IntPtr]::Size -ne 8 -or (Get-Process -Id $PID).SessionId -ne 1) { throw 'Requires x64 desktop session1.' }
    if (!(Test-Path -LiteralPath (Join-Path $package 'KH2COOP-PACKAGE') -PathType Leaf)) { throw 'Package marker absent.' }
    $ownershipFile = Join-Path $package 'build/rig/owned.txt'
    function Check-Ownership {
        $rows = @(Get-Content -LiteralPath $ownershipFile | Where-Object { $_.Trim() -match ('^' + $GamePid + ' [0-9]+$') })
        if ($rows.Count -ne 1 -or [long]($rows[0].Trim().Split(' ')[1]) -ne $CreationFileTime) { throw 'Exact package PID/creation ownership differs.' }
    }
    Check-Ownership
    $attested = @(Get-ChildItem -LiteralPath (Join-Path $package 'tools/launcher/.local/runs') -Filter 'launch-result.json' -File -Recurse | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw | ConvertFrom-Json } | Where-Object { $_.ready -and $_.receipt.ok -and $_.receipt.processId -eq $GamePid -and $_.receipt.hooksInstalled })
    if ($attested.Count -ne 1) { throw 'Exactly one ready owned launch receipt required.' }
    $nativeLog = Join-Path $package "build/rig/logs/kh2coop_inject_$GamePid.log"
    if ([IO.Path]::GetFullPath($attested[0].receipt.log) -ne $nativeLog) { throw 'Attested log is outside this exact owned launch.' }
    # Read only the bounded startup prefix while the native logger retains its writer.
    $nativeStream = [IO.File]::Open($nativeLog,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite)
    try {
        $nativeReader = [IO.StreamReader]::new($nativeStream,[Text.Encoding]::UTF8,$true,4096,$false)
        try {
            $nativeChars = [char[]]::new(65536)
            $nativeCount = $nativeReader.ReadBlock($nativeChars,0,$nativeChars.Length)
            $native = [string]::new($nativeChars,0,$nativeCount)
        } finally { $nativeReader.Dispose() }
    } finally { $nativeStream.Dispose() }
    $sandbox = Join-Path $package "build/rig/logs/save_sandbox_$GamePid"
    if (!$native.Contains("Save guard installed: writes under My Games\KINGDOM HEARTS HD 1.5+2.5 ReMIX go to $sandbox; deletes/moves/copies there are denied") -or
        !$native.Contains('Initialization complete') -or !$native.Contains('InputCollector hook installed')) { throw 'Saveguard/input/init readiness not retained.' }
    $proc = Get-Process -Id $GamePid
    $exe = $proc.MainModule.FileName
    $allowedExe = @('C:\Program Files (x86)\Steam\steamapps\common\KINGDOM HEARTS -HD 1.5+2.5 ReMIX-\KINGDOM HEARTS II FINAL MIX.exe',
        (Join-Path $GameViewRoot 'KINGDOM HEARTS II FINAL MIX.exe'))
    if ($exe -notin $allowedExe -or (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant() -ne '9002b2de6a1f91a790bd0673de125d1cf833f7942bfec827cdcf6ba64d5849ed') { throw 'Wrong supported EXE path/hash.' }
    $record.gameExe = $exe
    if (!('RehearsalRelativeMouse09' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class RehearsalRelativeMouse09 {
  [StructLayout(LayoutKind.Explicit, Size=40)] public struct INPUT {
    [FieldOffset(0)] public uint type;
    [FieldOffset(8)] public MOUSE mouse;
  }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSE { public int x,y; public uint data,flags,time; public UIntPtr extra; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint process);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int key);
  public static uint WindowPid(IntPtr hwnd) { uint pid; if(GetWindowThreadProcessId(hwnd,out pid)==0) return 0; return pid; }
  public static void CheckLayout() {
    if(IntPtr.Size!=8 || Marshal.SizeOf(typeof(INPUT))!=40 || Marshal.SizeOf(typeof(MOUSE))!=32 || Marshal.OffsetOf(typeof(MOUSE),"extra").ToInt32()!=24) throw new InvalidOperationException("Unexpected x64 INPUT layout");
  }
  public static uint Move(int dx,int dy,out int error) {
    CheckLayout();
    if(Math.Abs((long)dx)+Math.Abs((long)dy)>32 || (dx==0 && dy==0)) throw new ArgumentOutOfRangeException();
    var input=new INPUT { type=0, mouse=new MOUSE { x=dx,y=dy,flags=1 } };
    uint sent=SendInput(1,new[]{input},40); error=Marshal.GetLastWin32Error(); return sent;
  }
}
'@
    }
    [RehearsalRelativeMouse09]::CheckLayout()
    $clock = [Diagnostics.Stopwatch]::StartNew()
    function Check-Target {
        if ($clock.ElapsedMilliseconds -ge 1000) { throw 'One-second observation/send window expired.' }
        Check-Ownership
        $p = Get-Process -Id $GamePid
        if ($p.SessionId -ne 1 -or $p.StartTime.ToUniversalTime().ToFileTimeUtc() -ne $CreationFileTime -or
            $p.MainModule.FileName -ne $exe -or $p.MainWindowHandle.ToInt64() -ne $GameHwnd) { throw 'Owned process/window identity changed.' }
        $window = [IntPtr]$GameHwnd
        if ([RehearsalRelativeMouse09]::WindowPid($window) -ne $GamePid -or [RehearsalRelativeMouse09]::GetForegroundWindow() -ne $window) { throw 'Exact owned HWND must already be foreground; focus is never taken.' }
        # High bit only: held keys, modifiers and all five mouse buttons. No key releases.
        for ($keyCode=1; $keyCode -le 254; $keyCode++) {
            if (([RehearsalRelativeMouse09]::GetAsyncKeyState($keyCode) -band 0x8000) -ne 0) { throw "Physical key/button held: $keyCode" }
        }
        if ($clock.ElapsedMilliseconds -ge 1000 -or [RehearsalRelativeMouse09]::GetForegroundWindow() -ne $window) { throw 'Observation expired or foreground changed.' }
    }
    Check-Target
    $record.phase='send-intent'; Write-MouseReceipt $record
    Check-Target
    $record.attempted=$true
    $sendError=0
    $record.sentCount=[RehearsalRelativeMouse09]::Move($SignedDx,$SignedDy,[ref]$sendError)
    $record.sendReturned=$true; $record.sendWin32Error=$sendError
    if ($record.sentCount -ne 1) { throw 'SendInput did not report one event; no retry.' }
    Check-Target
    $record.observationMs=$clock.ElapsedMilliseconds
    $record.success=$true
} catch {
    $record.error=[string]$_
} finally {
    try { $record.phase='result'; $record.finishedUtc=[DateTime]::UtcNow.ToString('o'); Write-MouseReceipt $record }
    finally { $receiptStream.Dispose() }
}
$record | ConvertTo-Json -Depth 5
if (!$record.success) { throw 'Mouse stand-in refused or outcome unqualified; inspect receipt, never silently retry.' }
