# Build the OPL3 renderer (Win32 GUI app, runs on Win98) with Open Watcom.
# Produces TWO binaries with identical behaviour and sound output:
#   voplsrv.exe  - Nuked OPL3 (nuked-opl3/, the reference emulator)
#   voplfast.exe - Nuked-OPL3-fast (nuked-opl3-fast/, bit-exact fork, ~2x
#                  less CPU - recommended for slower CPUs)
# INSTALL.BAT lets the user pick one; either is installed as VOPLSRV.EXE.
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
    # -otexan -6r -fp6: full optimization. Watcom's default is NO optimization,
    # which makes the synthesis ~2.3x slower - enough to peg a P3-class CPU and
    # starve the whole system at the renderer's realtime priority.
    $opt = @('-otexan', '-6r', '-fp6')

    & wcl386.exe -q -bt=nt -l=nt_win @opt -I"$root\nuked-opl3" voplsrv.c "$root\nuked-opl3\opl3.c" winmm.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 failed ($LASTEXITCODE)" }
    $exe = Get-Item voplsrv.exe
    "built voplsrv.exe : $($exe.Length) bytes (Nuked OPL3)"

    & wcl386.exe -q -bt=nt -l=nt_win @opt -I"$root\nuked-opl3-fast" '-fe=voplfast.exe' voplsrv.c "$root\nuked-opl3-fast\opl3.c" winmm.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 (fast backend) failed ($LASTEXITCODE)" }
    $exe = Get-Item voplfast.exe
    "built voplfast.exe : $($exe.Length) bytes (Nuked-OPL3-fast)"
}
finally { Pop-Location }
