# Build SBTEST.EXE - 16-bit real-mode DOS SoundBlaster DSP probe (Open Watcom).
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH
Push-Location $PSScriptRoot
try {
    & wcl.exe -q -bt=dos -ms -l=dos SBTEST.C
    if ($LASTEXITCODE) { throw "wcl failed ($LASTEXITCODE)" }
    "built SBTEST.EXE : $((Get-Item SBTEST.EXE).Length) bytes"
}
finally { Pop-Location }
