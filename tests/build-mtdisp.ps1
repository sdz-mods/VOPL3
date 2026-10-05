# Build MTDISP.EXE - writes one SysEx to an MT-32's display through VOPL3's
# MPU-401 port, so the SysEx path can be tested without a game in the way.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    & wcl.exe -q -bt=dos -ms -l=dos MTDISP.C
    if ($LASTEXITCODE) { throw "wcl failed ($LASTEXITCODE)" }
    "built MTDISP.EXE : $((Get-Item MTDISP.EXE).Length) bytes"
}
finally { Pop-Location }
