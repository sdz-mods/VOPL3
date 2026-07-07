# Build VOPLSTAT.EXE (Win32 console, runs on Win9x) with Open Watcom.
# Reads VOPL3's ring stats over \\.\VOPL3 and writes C:\VOPLSTAT.TXT.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h') + ';' + (Join-Path $ow 'h\nt')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    & wcl386.exe -q -bt=nt -l=nt VOPLSTAT.C
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
    "built VOPLSTAT.EXE : $((Get-Item VOPLSTAT.EXE).Length) bytes"
}
finally { Pop-Location }
