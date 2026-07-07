# Build voplsrv.exe (Win32 console, runs on Win98) with Open Watcom.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM = $ow
$env:INCLUDE = (Join-Path $ow 'h') + ';' + (Join-Path $ow 'h\nt')
$env:PATH = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    # -bt=nt Win32 target, -l=nt_win = GUI subsystem (WinMain, no console window
    # -> runs silently in the background); link winmm for waveOut. Nuked opl3.c too.
    & wcl386.exe -q -bt=nt -l=nt_win -I"$root\nuked-opl3" voplsrv.c "$root\nuked-opl3\opl3.c" winmm.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 failed ($LASTEXITCODE)" }
    $exe = Get-Item voplsrv.exe
    "built voplsrv.exe : $($exe.Length) bytes"
}
finally { Pop-Location }
