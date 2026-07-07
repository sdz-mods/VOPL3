# Build oplrender.exe (Nuked OPL3 render harness) and run its selftest.
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc) {
    $gcc = (Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter gcc.exe -ErrorAction SilentlyContinue | Select-Object -First 1).FullName
}
if (-not $gcc) { Write-Error 'gcc not found (winget install BrechtSanders.WinLibs.POSIX.UCRT)'; exit 1 }
& $gcc -O2 -o "$PSScriptRoot\oplrender.exe" "$PSScriptRoot\oplrender.c" "$root\nuked-opl3\opl3.c" "-I$root\nuked-opl3"
if ($LASTEXITCODE -ne 0) { exit 1 }
& "$PSScriptRoot\oplrender.exe" selftest
exit $LASTEXITCODE
