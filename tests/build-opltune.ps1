# Build OPLTUNE.EXE - 16-bit real-mode DOS OPL3 test player (Open Watcom).
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    & wcl.exe -q -bt=dos -ms -l=dos OPLTUNE.C
    if ($LASTEXITCODE) { throw "wcl failed ($LASTEXITCODE)" }
    "built OPLTUNE.EXE : $((Get-Item OPLTUNE.EXE).Length) bytes"
}
finally { Pop-Location }
