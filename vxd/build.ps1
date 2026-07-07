# Build VOPL3.VXD with Open Watcom (bundled in tools/ow) + fixlink.
# Produces vxd/vopl3.vxd and stages it into drop/VOPL3 for the ISO.
#   -Serial : compile in COM1 debug tracing (-DVOPL3_SERIAL). Off by default.
# NOTE: do NOT use ErrorActionPreference=Stop here — wcc386 writes warnings to
# stderr which PS 5.1 would turn into a terminating error and abort the link.
param([switch]$Serial)
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
if (-not (Test-Path (Join-Path $ow 'binnt64\wcc386.exe'))) { throw "Open Watcom not found in $ow" }

# --- Watcom environment ---
$env:WATCOM = $ow
$env:INCLUDE = (Join-Path $ow 'h') + ';' + (Join-Path $ow 'h\win')
$env:PATH = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    # fixlink: fixes Watcom's LE/VXD header flags so 9x will load it
    $fixlinkSrc = Join-Path $root 'ref\vmdisp9x\fixlink\fixlink.c'
    if (-not (Test-Path (Join-Path $PSScriptRoot 'fixlink.exe'))) {
        & wcl386.exe -q $fixlinkSrc '-fe=fixlink.exe'
        if ($LASTEXITCODE) { throw "fixlink build failed ($LASTEXITCODE)" }
    }

    # compile: 32-bit flat VxD code, Pentium, no runtime
    # -zc puts string literals in the code segment so DGROUP/_DATA (VXD_DDB +
    # struct S) starts at offset 0 — needed for correct runtime data mapping.
    $ccargs = @('-q','-wcd=303','-s','-zls','-zc','-mf','-DVXD32','-fpi87','-ei','-oeatxhn','-6s','-fp6')
    if ($Serial) { $ccargs += '-DVOPL3_SERIAL'; "serial debug: ON" } else { "serial debug: OFF" }
    & wcc386.exe @ccargs vopl3.c
    if ($LASTEXITCODE) { throw "wcc386 failed ($LASTEXITCODE)" }

    # link into a raw VxD (--% stops PS parsing so wlink sees @vopl3.lnk)
    wlink.exe --% @vopl3.lnk
    if ($LASTEXITCODE) { throw "wlink failed ($LASTEXITCODE)" }

    # post-process the LE header
    & .\fixlink.exe -vxd32 vopl3.vxd
    if ($LASTEXITCODE) { throw "fixlink -vxd32 failed ($LASTEXITCODE)" }

    # Two wlink quirks have to be patched out of the LE image or Win9x loads the
    # VxD but it silently does nothing. fixlink -vxd32 fixes neither.
    $bytes = [IO.File]::ReadAllBytes("$PSScriptRoot\vopl3.vxd")
    $leoff = [BitConverter]::ToInt32($bytes, 0x3C)
    $changed = $false

    # (1) ModuleFlags: wlink emits 0x38000 ("no internal fixups") even though the
    #     image HAS a fixup section, so the loader skips relocations and every
    #     absolute reference (incl. the DDB's control-proc pointer) is left at its
    #     unrelocated link address. Clear bit 0x10000 (-> 0x28000) so fixups apply.
    $mf = [BitConverter]::ToUInt32($bytes, $leoff + 0x10)
    if ($mf -band 0x10000) {
        $bytes[$leoff + 0x12] = $bytes[$leoff + 0x12] -band 0xFE   # bit 0x10000 = byte[LE+0x12] bit0
        $changed = $true
        "patched ModuleFlags 0x$('{0:X}' -f $mf) -> 0x$('{0:X}' -f ([BitConverter]::ToUInt32($bytes,$leoff+0x10)))"
    }

    # (2) Entry table: wlink exports VXD_DDB.1 as a 286 CALL GATE (bundle type
    #     0x02) instead of a 32-bit entry (type 0x03). VMM resolves the DDB
    #     through this ordinal, and a call-gate entry yields a bogus DDB address,
    #     so the control proc is never called -> the VxD loads but never installs.
    #     Walk the resident-name table (len,name,ord... terminated by a 0 length),
    #     then convert the first entry bundle from call-gate to 32-bit form.
    $p = $leoff + [BitConverter]::ToUInt32($bytes, $leoff + 0x50)   # Entry/Resident-name table
    while ($bytes[$p] -ne 0) { $p += 1 + $bytes[$p] + 2 }           # skip name entries
    $p++                                                            # skip the 0-length terminator
    # $p now points at the first entry bundle: [cnt][type][obj:2][flags]...
    if ($bytes[$p + 1] -eq 0x02) {
        $bytes[$p + 1] = 0x03      # 286 call gate -> 32-bit entry
        $bytes[$p + 4] = 0x01      # entry flags: exported 32-bit offset (was 0x03)
        $changed = $true
        "patched DDB export bundle: call-gate (0x02) -> 32-bit (0x03)"
    }

    if ($changed) { [IO.File]::WriteAllBytes("$PSScriptRoot\vopl3.vxd", $bytes) }

    $vxd = Get-Item vopl3.vxd
    "built vopl3.vxd : $($vxd.Length) bytes"
}
finally { Pop-Location }
