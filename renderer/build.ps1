# Build the OPL3 renderer (Win32 GUI app, runs on Win98) with Open Watcom.
# Produces FOUR binaries with identical behaviour; the first two also have
# identical sound output:
#   voplsrv.exe  - Nuked OPL3 (nuked-opl3/, the reference emulator)
#   voplfast.exe - Nuked-OPL3-fast (nuked-opl3-fast/, bit-exact fork, ~2x
#                  less CPU - recommended for slower CPUs)
#   vopldb.exe   - DOSBox's DBOPL (dbopl/, C++; far less CPU, less exact).
#                  GPL v2+ as a whole, because DBOPL is - see dbopl/README.md
#   voplym.exe   - ymfm (ymfm/, C++, BSD 3-clause; MAME's OPL3 core -
#                  about the CPU cost of Nuked-fast)
# INSTALL.BAT lets the user pick one; it is installed as VOPLSRV.EXE.
#
# The emulator cores are compiled by GCC (32-bit MinGW, e.g. MSYS2's
# mingw32: C:\msys64\mingw32\bin, or set VOPL3_GCC to its bin folder) - its
# optimiser makes them about twice as fast as Open Watcom's, with identical
# output. Only the cores: everything else is Watcom-built, and so is the
# link, so no GCC runtime ends up in the renderer (gccshim.c gives the cores
# the few runtime functions they call). -WatcomCores builds the cores with
# Watcom instead (slower; no GCC needed).
param([switch]$WatcomCores)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM = $ow
$env:INCLUDE = (Join-Path $ow 'h') + ';' + (Join-Path $ow 'h\nt')
$env:PATH = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

$gccbin = $null
if (-not $WatcomCores) {
    $gccbin = if ($env:VOPL3_GCC) { $env:VOPL3_GCC } else { 'C:\msys64\mingw32\bin' }
    if (-not (Test-Path (Join-Path $gccbin 'gcc.exe'))) {
        throw "32-bit MinGW GCC not found in $gccbin (set VOPL3_GCC to its bin folder, or build with -WatcomCores)"
    }
    $machine = & (Join-Path $gccbin 'gcc.exe') -dumpmachine
    if ($machine -notlike 'i686-*') { throw "$gccbin\gcc.exe targets $machine - a 32-bit (i686) MinGW is needed" }
}

# GCC for the cores: -march=i586 so no instruction newer than the Pentium
# is used (-march=i686 would add CMOV, which Pentium MMX and AMD K6 lack);
# no unwind tables or ident section, which the Watcom link doesn't need.
# C++: no exceptions, RTTI or thread-safe statics - none of which the cores
# use, and each would need GCC's runtime; NDEBUG drops assert (ymfm's).
$gflags  = @('-O2', '-march=i586', '-mtune=generic', '-fno-strict-aliasing',
             '-fno-asynchronous-unwind-tables', '-fno-ident')
$gxflags = $gflags + @('-fno-exceptions', '-fno-rtti', '-fno-threadsafe-statics', '-DNDEBUG')
function gcc_obj($tool, $flags, $src, $obj, $inc) {
    $p = $env:PATH; $env:PATH = "$gccbin;$p"
    & (Join-Path $gccbin $tool) @flags "-I$inc" -c $src -o $obj
    $rc = $LASTEXITCODE; $env:PATH = $p
    if ($rc -ne 0) { throw "$tool failed on $src ($rc)" }
}

Push-Location $PSScriptRoot
try {
    # -bt=nt Win32 target, -l=nt_win = GUI subsystem (WinMain, no console window
    # -> runs silently in the background); link winmm for waveOut.
    # -otexan -6r -fp6: full optimization. Watcom's default is NO optimization,
    # which makes the synthesis ~2.3x slower - enough to peg a P3-class CPU and
    # starve the whole system at the renderer's realtime priority.
    $opt = @('-otexan', '-6r', '-fp6')
    $libs = @('winmm.lib', 'advapi32.lib')
    $core = if ($gccbin) { 'GCC' } else { 'Watcom' }

    if ($gccbin) {
        & wcc386.exe -q -bt=nt @opt gccshim.c
        if ($LASTEXITCODE -ne 0) { throw "wcc386 (gccshim) failed ($LASTEXITCODE)" }
    }
    function link_renderer($exe, $defs, $inc, $objs, $name) {
        if ($gccbin) { $defs += '-dVOPL3_GCC'; $objs += 'gccshim.obj' }
        & wcl386.exe -q -bt=nt -l=nt_win @opt @defs -I"$inc" "-fe=$exe" voplsrv.c @objs @libs
        if ($LASTEXITCODE -ne 0) { throw "wcl386 ($name) failed ($LASTEXITCODE)" }
        "built $exe : $((Get-Item $exe).Length) bytes ($name, core by $core)"
    }

    # the two Nuked builds: opl3.c
    foreach ($b in @(@{ exe='voplsrv.exe';  dir='nuked-opl3';      def=@();              name='Nuked OPL3' },
                     @{ exe='voplfast.exe'; dir='nuked-opl3-fast'; def=@('-dVOPL3_FAST'); name='Nuked-OPL3-fast' })) {
        $inc = Join-Path $root $b.dir
        if ($gccbin) { gcc_obj 'gcc.exe' $gflags (Join-Path $inc 'opl3.c') 'core.obj' $inc }
        else         { & wcc386.exe -q -bt=nt @opt -I"$inc" '-fo=core.obj' (Join-Path $inc 'opl3.c')
                       if ($LASTEXITCODE -ne 0) { throw "wcc386 ($($b.name)) failed" } }
        link_renderer $b.exe $b.def $inc @('core.obj') $b.name
    }

    # DBOPL (C++): dbopl.cpp + its glue. Built from this folder, so the only
    # dbopl.h in reach is dbopl/'s (Open Watcom searches the current
    # directory first).
    $inc = Join-Path $root 'dbopl'
    if ($gccbin) {
        gcc_obj 'g++.exe' $gxflags "$inc\dbopl.cpp"      'core.obj' $inc
        gcc_obj 'g++.exe' $gxflags "$inc\dbopl_glue.cpp" 'glue.obj' $inc
    } else {
        & wpp386.exe -q -bt=nt @opt -I"$inc" '-fo=core.obj' "$inc\dbopl.cpp"
        if ($LASTEXITCODE -ne 0) { throw "wpp386 (DBOPL) failed" }
        & wpp386.exe -q -bt=nt @opt -I"$inc" '-fo=glue.obj' "$inc\dbopl_glue.cpp"
        if ($LASTEXITCODE -ne 0) { throw "wpp386 (DBOPL glue) failed" }
    }
    link_renderer 'vopldb.exe' @('-dVOPL3_DBOPL') $inc @('core.obj', 'glue.obj') 'DOSBox DBOPL'

    # ymfm (C++): its glue includes ymfm_opl.cpp and is compiled as one unit
    # (see ymfm/ymfm_glue.cpp). With Watcom it needs -xs (exceptions): its
    # <vector>, pulled in by ymfm.h, won't compile without; ymfm_owcompat.h
    # (included by ymfm.h) covers the C++11 it uses. GCC: C++14, and one
    # false-positive warning silenced - in ymfm's timer code, which never
    # runs here (the VxD emulates the OPL timers; ymfm is given none).
    $inc = Join-Path $root 'ymfm'
    if ($gccbin) {
        gcc_obj 'g++.exe' ($gxflags + @('-std=c++14', '-Wno-stringop-overflow')) "$inc\ymfm_glue.cpp" 'core.obj' $inc
    } else {
        & wpp386.exe -q -bt=nt @opt -xs -I"$inc" '-fo=core.obj' "$inc\ymfm_glue.cpp"
        if ($LASTEXITCODE -ne 0) { throw "wpp386 (ymfm) failed" }
    }
    link_renderer 'voplym.exe' @('-dVOPL3_YMFM') $inc @('core.obj') 'ymfm'

    Remove-Item core.obj, glue.obj, gccshim.obj, voplsrv.obj -ErrorAction SilentlyContinue
}
finally { Pop-Location }
