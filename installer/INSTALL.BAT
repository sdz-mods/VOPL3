@ECHO OFF
ECHO.
ECHO   VOPL3 - OPL3 FM emulation for Windows 98/ME
ECHO   ===========================================
ECHO.
ECHO   This installer will:
ECHO     1. install VOPL3.VXD          (loads at boot, traps AdLib/OPL ports)
ECHO     2. install the OPL3 renderer  (C:\VOPL3\VOPLSRV.EXE, autostarts)
ECHO     3. patch SBEMUL.SYS so it stops claiming FM port 388 but keeps its
ECHO        digital audio + MIDI. The original is backed up to SBEMUL.SYS.orig
ECHO.
ECHO   Press Ctrl+C to abort, or
PAUSE

ECHO.
ECHO [1/4] Copying VOPL3.VXD to C:\WINDOWS\SYSTEM ...
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD ATTRIB -R C:\WINDOWS\SYSTEM\VOPL3.VXD >NUL
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD DEL C:\WINDOWS\SYSTEM\VOPL3.VXD >NUL
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD GOTO inuse
COPY VOPL3.VXD C:\WINDOWS\SYSTEM\VOPL3.VXD
IF NOT EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD GOTO copyerr

ECHO [2/4] Copying renderer to C:\VOPL3 ...
IF NOT EXIST C:\VOPL3\NUL MD C:\VOPL3
IF EXIST C:\VOPL3\VOPLSRV.EXE ATTRIB -R C:\VOPL3\VOPLSRV.EXE >NUL
IF EXIST C:\VOPL3\VOPLSRV.EXE DEL C:\VOPL3\VOPLSRV.EXE >NUL
IF EXIST C:\VOPL3\VOPLSRV.EXE GOTO inuse
COPY VOPLSRV.EXE C:\VOPL3\VOPLSRV.EXE
IF NOT EXIST C:\VOPL3\VOPLSRV.EXE GOTO copyerr

ECHO [3/4] Registering VOPL3 (boot VxD + renderer autostart) ...
REGEDIT /S INSTALL.REG

ECHO [4/4] Patching SBEMUL.SYS ...
SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS
IF ERRORLEVEL 1 GOTO sberr

ECHO.
ECHO   ===================  INSTALL COMPLETE  ===================
ECHO   Please REBOOT now.
ECHO   After reboot, in your DOS game set:
ECHO       Music     = AdLib (or OPL3)
ECHO       Sound FX  = Sound Blaster
ECHO   You get OPL3 music (VOPL3) AND digital effects (SBEMUL) together.
ECHO   The renderer C:\VOPL3\VOPLSRV.EXE starts automatically at login.
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
ECHO   INSTALL.BAT, VOPL3.VXD, VOPLSRV.EXE, SBPATCH.EXE and INSTALL.REG.

:end
ECHO.
PAUSE
