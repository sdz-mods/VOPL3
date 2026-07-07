# Assemble the shippable VOPL3 installer into installer/dist/.
# Source lives in installer/ (this script, SBPATCH.C, the .BAT/.REG/README);
# dist/ contains ONLY what you copy onto the Win98 machine and run.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = "$ow\h;$ow\h\nt"
$env:PATH    = "$ow\binnt64;$ow\binnt;$env:PATH"

$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null

Push-Location $PSScriptRoot
try {
    # 1. build the patcher (Win32 console, runs on Win98)
    & wcl386.exe -q -bt=nt -l=nt SBPATCH.C
    if ($LASTEXITCODE) { throw "SBPATCH build failed ($LASTEXITCODE)" }
    Move-Item SBPATCH.EXE (Join-Path $dist 'SBPATCH.EXE') -Force
    Remove-Item SBPATCH.OBJ -ErrorAction SilentlyContinue

    # 2. stage the driver + renderer + license under their install names
    Copy-Item (Join-Path $root 'vxd\vopl3.vxd')        (Join-Path $dist 'VOPL3.VXD')   -Force
    Copy-Item (Join-Path $root 'renderer\voplsrv.exe') (Join-Path $dist 'VOPLSRV.EXE') -Force
    Copy-Item (Join-Path $root 'nuked-opl3\LICENSE')   (Join-Path $dist 'NUKED-OPL3-LICENSE.txt') -Force -ErrorAction SilentlyContinue

    # 3. copy the runtime text files, normalised to CRLF for DOS
    foreach ($t in 'INSTALL.BAT','UNINSTALL.BAT','INSTALL.REG','UNINSTALL.REG','README.TXT') {
        $src = Join-Path $PSScriptRoot $t
        $dst = Join-Path $dist $t
        $c = ([IO.File]::ReadAllText($src) -replace "`r`n","`n") -replace "`n","`r`n"
        [IO.File]::WriteAllText($dst, $c)
    }

    "=== shippable package: $dist ==="
    Get-ChildItem $dist | Select-Object Name,Length | Format-Table -Auto | Out-String
}
finally { Pop-Location }
