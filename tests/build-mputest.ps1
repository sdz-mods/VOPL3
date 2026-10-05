# Build MPUTEST.EXE - drives VOPL3's MPU-401 intelligent mode from a DOS box.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    & wcl.exe -q -bt=dos -ms -l=dos MPUTEST.C
    if ($LASTEXITCODE) { throw "wcl failed ($LASTEXITCODE)" }
    "built MPUTEST.EXE : $((Get-Item MPUTEST.EXE).Length) bytes"
}
finally { Pop-Location }
