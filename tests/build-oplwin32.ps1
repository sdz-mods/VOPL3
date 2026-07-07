# Build OPLWIN32.EXE (Win32 console, runs on Win9x) with Open Watcom.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h') + ';' + (Join-Path $ow 'h\nt')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    & wcl386.exe -q -bt=nt -l=nt OPLWIN32.C
    if ($LASTEXITCODE) { throw "build failed ($LASTEXITCODE)" }
    "built OPLWIN32.EXE : $((Get-Item OPLWIN32.EXE).Length) bytes"
}
finally { Pop-Location }
