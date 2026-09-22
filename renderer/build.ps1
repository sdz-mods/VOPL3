# Build the OPL3 renderer (Win32 GUI app, runs on Win98) with Open Watcom.
# Produces FOUR binaries with identical behaviour; the first two also have
# identical sound output:
#   voplsrv.exe  - Nuked OPL3 (nuked-opl3/, the reference emulator)
#   voplfast.exe - Nuked-OPL3-fast (nuked-opl3-fast/, bit-exact fork, ~2x
#                  less CPU - recommended for slower CPUs)
#   vopldb.exe   - DOSBox's DBOPL (dbopl/, C++; far less CPU, less exact).
#                  GPL v2+ as a whole, because DBOPL is - see dbopl/README.md
#   voplym.exe   - ymfm (ymfm/, C++, BSD 3-clause; MAME's OPL3 core -
#                  CPU cost between Nuked-fast and Nuked)
# INSTALL.BAT lets the user pick one; it is installed as VOPLSRV.EXE.
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

    & wcl386.exe -q -bt=nt -l=nt_win @opt -I"$root\nuked-opl3" voplsrv.c "$root\nuked-opl3\opl3.c" winmm.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 failed ($LASTEXITCODE)" }
    $exe = Get-Item voplsrv.exe
    "built voplsrv.exe : $($exe.Length) bytes (Nuked OPL3)"

    & wcl386.exe -q -bt=nt -l=nt_win @opt -dVOPL3_FAST -I"$root\nuked-opl3-fast" '-fe=voplfast.exe' voplsrv.c "$root\nuked-opl3-fast\opl3.c" winmm.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 (fast backend) failed ($LASTEXITCODE)" }
    $exe = Get-Item voplfast.exe
    "built voplfast.exe : $($exe.Length) bytes (Nuked-OPL3-fast)"

    # DBOPL is C++: wcl386 hands the .cpp files to wpp386 and links the C++
    # runtime. Built from this folder, so the only dbopl.h in reach is
    # dbopl/'s (Open Watcom searches the current directory first).
    & wcl386.exe -q -bt=nt -l=nt_win @opt -dVOPL3_DBOPL -I"$root\dbopl" '-fe=vopldb.exe' voplsrv.c "$root\dbopl\dbopl_glue.cpp" "$root\dbopl\dbopl.cpp" winmm.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 (DBOPL backend) failed ($LASTEXITCODE)" }
    $exe = Get-Item vopldb.exe
    "built vopldb.exe  : $($exe.Length) bytes (DOSBox DBOPL)"

    # ymfm is C++ too, but its headers pull in Watcom's <vector>, which needs
    # exception support (-xs) - an option wcl386 would also hand to the C
    # compiler for voplsrv.c, so the two C++ files are compiled on their own
    # first. ymfm_owcompat.h (included by ymfm.h) covers the C++11 it uses.
    & wpp386.exe -q -bt=nt @opt -xs -I"$root\ymfm" '-fo=ymfm_glue.obj' "$root\ymfm\ymfm_glue.cpp"
    if ($LASTEXITCODE -ne 0) { throw "wpp386 (ymfm glue) failed ($LASTEXITCODE)" }
    & wpp386.exe -q -bt=nt @opt -xs -I"$root\ymfm" '-fo=ymfm_opl.obj' "$root\ymfm\ymfm_opl.cpp"
    if ($LASTEXITCODE -ne 0) { throw "wpp386 (ymfm) failed ($LASTEXITCODE)" }
    & wcl386.exe -q -bt=nt -l=nt_win @opt -dVOPL3_YMFM -I"$root\ymfm" '-fe=voplym.exe' voplsrv.c ymfm_glue.obj ymfm_opl.obj winmm.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 (ymfm backend) failed ($LASTEXITCODE)" }
    Remove-Item ymfm_glue.obj, ymfm_opl.obj -ErrorAction SilentlyContinue
    $exe = Get-Item voplym.exe
    "built voplym.exe  : $($exe.Length) bytes (ymfm)"
}
finally { Pop-Location }
