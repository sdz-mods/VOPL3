# Assemble ADLIBTST.ASM (NASM syntax) into a DOS .COM in this folder.
# Needs NASM (https://www.nasm.us/): put nasm.exe at tools/nasm/nasm.exe under
# the repo root, or have nasm on PATH.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$nasm = Join-Path $root 'tools\nasm\nasm.exe'
if (-not (Test-Path $nasm)) {
    $onPath = Get-Command nasm.exe -ErrorAction SilentlyContinue
    if ($onPath) { $nasm = $onPath.Source }
    else { throw "NASM not found. Put nasm.exe at tools\nasm\nasm.exe or on PATH." }
}

$src = Join-Path $PSScriptRoot 'ADLIBTST.ASM'
$out = Join-Path $PSScriptRoot 'ADLIBTST.COM'
& $nasm -f bin $src -o $out
if ($LASTEXITCODE) { throw "NASM failed ($LASTEXITCODE)" }
Get-Item $out | Select-Object Name, Length
