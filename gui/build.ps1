# Build VOPLCFG.EXE - the VOPL3 control-panel / system-tray app (Open Watcom).
# GUI subsystem (WinMain), links the tray (shell32), trackbar/combo common
# controls (comctl32), MIDI device enumeration (winmm) and the registry
# auto-start (advapi32). Shares vopl3ipc.h with the renderer.
$ErrorActionPreference = 'Continue'
$root = Split-Path $PSScriptRoot -Parent
$ow   = Join-Path $root 'tools\ow'
$env:WATCOM  = $ow
$env:INCLUDE = (Join-Path $ow 'h') + ';' + (Join-Path $ow 'h\nt')
$env:PATH    = (Join-Path $ow 'binnt64') + ';' + (Join-Path $ow 'binnt') + ';' + $env:PATH

Push-Location $PSScriptRoot
try {
    & wcl386.exe -q -bt=nt -l=nt_win -I"$root\renderer" voplcfg.c `
        shell32.lib comctl32.lib winmm.lib advapi32.lib
    if ($LASTEXITCODE -ne 0) { throw "wcl386 failed ($LASTEXITCODE)" }
    Remove-Item voplcfg.obj -ErrorAction SilentlyContinue

    # attach the app icon (resource.rc -> voplcfg.ico) to the built exe
    & wrc.exe -q -bt=nt resource.rc voplcfg.exe
    if ($LASTEXITCODE -ne 0) { throw "wrc failed ($LASTEXITCODE)" }

    $exe = Get-Item voplcfg.exe
    "built voplcfg.exe : $($exe.Length) bytes"
}
finally { Pop-Location }
