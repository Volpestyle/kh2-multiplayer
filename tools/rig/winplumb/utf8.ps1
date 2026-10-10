# Dot-source before parameter/JSON reads or child processes. UTF-8, no BOM.
function Initialize-WinPlumbUtf8 {
    $utf8=New-Object System.Text.UTF8Encoding($false,$true)
    $global:OutputEncoding=$utf8
    [Console]::OutputEncoding=$utf8
    [Console]::InputEncoding=$utf8
    $env:PYTHONUTF8='1';$env:PYTHONUNBUFFERED='1'
    if(-not ('WinPlumbConsole' -as [type])) {
        Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class WinPlumbConsole{[DllImport("kernel32.dll")]public static extern uint GetConsoleCP();}'
    }
    if([WinPlumbConsole]::GetConsoleCP() -ne 0) {
        & chcp.com 65001 | Out-Null
        if($LASTEXITCODE -ne 0){throw 'chcp 65001 failed'}
    }
}
function Read-WinPlumbText([string]$Path) {
    $bytes=[IO.File]::ReadAllBytes($Path)
    $text=(New-Object Text.UTF8Encoding($false,$true)).GetString($bytes)
    if($text.StartsWith([string][char]0xfeff,[StringComparison]::Ordinal)){$text=$text.Substring(1)}
    if($text.StartsWith([string][char]0xfeff,[StringComparison]::Ordinal) -or $text.StartsWith(([string][char]0xef+[char]0xbb+[char]0xbf),[StringComparison]::Ordinal) -or $text.StartsWith(([string][char]0x2039+[char]0xaf+[char]0xa8),[StringComparison]::Ordinal)) {throw 'Multiple or locale-decoded BOM refused'}
    return $text
}
function Read-WinPlumbJson([string]$Path) {return (Read-WinPlumbText $Path|ConvertFrom-Json)}
function Write-WinPlumbText([string]$Path,[string]$Value) {
    if($Value.StartsWith([string][char]0xfeff,[StringComparison]::Ordinal)){throw 'BOM-free output required'}
    $bytes=(New-Object Text.UTF8Encoding($false,$true)).GetBytes($Value)
    $file=[IO.File]::Open($Path,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::Read)
    try{$file.Write($bytes,0,$bytes.Length);$file.Flush($true)}finally{$file.Dispose()}
}
Initialize-WinPlumbUtf8
