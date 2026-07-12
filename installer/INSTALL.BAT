@ECHO OFF
ECHO.
ECHO   VOPL3 - OPL3 FM emulation for Windows 98/ME
ECHO   ===========================================
ECHO.
ECHO   This installer will:
ECHO     1. install VOPL3.VXD          (loads at boot, traps AdLib/OPL ports)
ECHO     2. install the OPL3 renderer  (C:\VOPL3\VOPLSRV.EXE, autostarts;
ECHO        you pick the standard or the CPU-friendly "fast" build)
ECHO     3. patch SBEMUL.SYS so it stops claiming FM port 388 (digital audio
ECHO        stays). You also choose FM-only or FM+MIDI. Original backed up.
ECHO.
ECHO   (If VOPL3 is already installed, this will stop the running renderer
ECHO   first. If that ever fails, run UNINSTALL.BAT + reboot, then this.)
ECHO.
ECHO   Press Ctrl+C to abort, or
PAUSE

ECHO.
ECHO Stopping any running VOPL3 renderer ...
IF EXIST VOPLSTOP.EXE VOPLSTOP.EXE

ECHO.
ECHO [1/4] Copying VOPL3.VXD to C:\WINDOWS\SYSTEM ...
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD ATTRIB -R C:\WINDOWS\SYSTEM\VOPL3.VXD >NUL
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD DEL C:\WINDOWS\SYSTEM\VOPL3.VXD >NUL
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD GOTO inuse
COPY VOPL3.VXD C:\WINDOWS\SYSTEM\VOPL3.VXD
IF NOT EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD GOTO copyerr

ECHO [2/4] Installing the OPL3 renderer to C:\VOPL3 ...
IF NOT EXIST C:\VOPL3\NUL MD C:\VOPL3
IF EXIST C:\VOPL3\VOPLSRV.EXE ATTRIB -R C:\VOPL3\VOPLSRV.EXE >NUL
IF EXIST C:\VOPL3\VOPLSRV.EXE DEL C:\VOPL3\VOPLSRV.EXE >NUL
IF EXIST C:\VOPL3\VOPLSRV.EXE GOTO inuse
ECHO.
ECHO   Two renderer builds are included - identical sound output:
ECHO     1 = Nuked OPL3       (the reference emulator)
ECHO     2 = Nuked OPL3 fast  (same output, about half the CPU use;
ECHO                           recommended, especially on slower machines)
CHOICE /C:12 Which renderer do you want to install
IF ERRORLEVEL 2 GOTO copyfast
COPY VOPLSRV.EXE C:\VOPL3\VOPLSRV.EXE
GOTO srvdone
:copyfast
COPY VOPLFAST.EXE C:\VOPL3\VOPLSRV.EXE
:srvdone
IF NOT EXIST C:\VOPL3\VOPLSRV.EXE GOTO copyerr
IF NOT EXIST C:\VOPL3\VOPL3.INI COPY VOPL3.INI C:\VOPL3\VOPL3.INI >NUL

ECHO [3/4] Registering VOPL3 (boot VxD + renderer autostart) ...
REGEDIT /S INSTALL.REG

ECHO [4/4] DOS-game MIDI (MPU-401) + patching SBEMUL.SYS ...
ECHO.
ECHO   MIDI handling:
ECHO     1 = FM only    - leave SBEMUL's MIDI as-is (plays the Microsoft GS
ECHO                      Wavetable synth, as before). Recommended default.
ECHO     2 = FM + MIDI  - VOPL3 also takes over MIDI, so DOS-game General MIDI
ECHO                      can be routed to ANY installed synth/device via the
ECHO                      Windows MIDI Mapper (see README - needs a bit more
ECHO                      setup, and replaces SBEMUL's MIDI).
CHOICE /C:12 Choose MIDI handling
IF ERRORLEVEL 2 GOTO midiyes

SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS
IF ERRORLEVEL 1 GOTO sberr
GOTO complete

:midiyes
SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS midi
IF ERRORLEVEL 1 GOTO sberr
REGEDIT /S MIDION.REG

:complete
ECHO.
ECHO   ===================  INSTALL COMPLETE  ===================
ECHO   Please REBOOT now.
ECHO   After reboot, in your DOS game set:
ECHO       Music     = AdLib (or OPL3)
ECHO       Sound FX  = Sound Blaster
ECHO   You get OPL3 music (VOPL3) AND digital effects (SBEMUL) together.
ECHO   The renderer C:\VOPL3\VOPLSRV.EXE starts automatically at login.
ECHO   If you chose FM+MIDI: set the MIDI device in the Multimedia control
ECHO   panel (or C:\VOPL3\VOPL3.INI) - run MIDILIST.EXE to list devices.
GOTO end

:sberr
ECHO.
ECHO   NOTE: the VxD + renderer are installed, but SBEMUL.SYS could not be
ECHO   patched (it may be in use). SoundBlaster digital audio will not work
ECHO   in DOS games until it is patched. To finish: restart in MS-DOS mode
ECHO   (Start - Shut Down - Restart in MS-DOS mode), change to this drive,
ECHO   and run:   SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS
GOTO end

:inuse
ECHO.
ECHO   A previous VOPL3 file is still loaded / running and cannot be replaced.
ECHO   Please run UNINSTALL.BAT, REBOOT, then run INSTALL.BAT again.
GOTO end

:copyerr
ECHO   ERROR: could not copy files. Run this from the folder that contains
ECHO   INSTALL.BAT, VOPL3.VXD, VOPLSRV.EXE, VOPLFAST.EXE, SBPATCH.EXE,
ECHO   VOPLSTOP.EXE, INSTALL.REG and MIDION.REG.

:end
ECHO.
PAUSE
