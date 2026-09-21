@ECHO OFF
ECHO.
ECHO   VOPL3 - OPL3 FM emulation for Windows 98/ME
ECHO   ===========================================
ECHO.
ECHO   This installer will:
ECHO     1. install VOPL3.VXD          (loads at boot, traps the AdLib/OPL
ECHO                                    and/or MPU-401 MIDI ports)
ECHO     2. install the OPL3 renderer  (C:\VOPL3\VOPLSRV.EXE, autostarts;
ECHO        you pick the standard or the CPU-friendly "fast" build)
ECHO     3. install the control panel  (C:\VOPL3\VOPLCFG.EXE - MIDI device,
ECHO        FM volume, status; you choose if it starts with Windows)
ECHO     4. patch SBEMUL.SYS so it frees the ports VOPL3 takes over (digital
ECHO        audio stays). Original backed up.
ECHO   First you choose what VOPL3 handles: FM, FM + MIDI, or MIDI only.
ECHO.
ECHO   (If VOPL3 is already installed, this will stop the running renderer
ECHO   first. If that ever fails, run UNINSTALL.BAT + reboot, then this.)
ECHO.
ECHO   Press Ctrl+C to abort, or
PAUSE

ECHO.
ECHO   What should VOPL3 handle?
ECHO     1 = FM only    - AdLib/OPL3 music. SBEMUL's MIDI is left as-is (plays
ECHO                      the Microsoft GS Wavetable synth). Recommended default.
ECHO     2 = FM + MIDI  - VOPL3 also takes over MIDI, so DOS-game General MIDI
ECHO                      can be routed to ANY installed synth/device via the
ECHO                      Windows MIDI Mapper (see README - needs a bit more
ECHO                      setup, and replaces SBEMUL's MIDI).
ECHO     3 = MIDI only  - MIDI as in 2, and FM is left to SBEMUL: games still
ECHO                      detect an AdLib, but AdLib music is SILENT (as it is
ECHO                      without VOPL3).
CHOICE /C:123 Choose what VOPL3 handles
IF ERRORLEVEL 3 GOTO modemidi
IF ERRORLEVEL 2 GOTO modefmmidi
SET VOPLMODE=fm
GOTO modedone
:modefmmidi
SET VOPLMODE=fmmidi
GOTO modedone
:modemidi
SET VOPLMODE=midionly
:modedone

ECHO.
ECHO Stopping any running VOPL3 renderer / control panel ...
IF EXIST VOPLSTOP.EXE VOPLSTOP.EXE

ECHO.
ECHO [1/5] Copying VOPL3.VXD to C:\WINDOWS\SYSTEM ...
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD ATTRIB -R C:\WINDOWS\SYSTEM\VOPL3.VXD >NUL
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD DEL C:\WINDOWS\SYSTEM\VOPL3.VXD >NUL
IF EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD GOTO inuse
COPY VOPL3.VXD C:\WINDOWS\SYSTEM\VOPL3.VXD
IF NOT EXIST C:\WINDOWS\SYSTEM\VOPL3.VXD GOTO copyerr

ECHO [2/5] Installing the OPL3 renderer to C:\VOPL3 ...
IF NOT EXIST C:\VOPL3\NUL MD C:\VOPL3
IF EXIST C:\VOPL3\VOPLSRV.EXE ATTRIB -R C:\VOPL3\VOPLSRV.EXE >NUL
IF EXIST C:\VOPL3\VOPLSRV.EXE DEL C:\VOPL3\VOPLSRV.EXE >NUL
IF EXIST C:\VOPL3\VOPLSRV.EXE GOTO inuse
REM MIDI only: no FM synthesis, so the choice of OPL3 emulator doesn't matter
IF "%VOPLMODE%"=="midionly" GOTO copystd
ECHO.
ECHO   Two renderer builds are included - identical sound output:
ECHO     1 = Nuked OPL3       (the reference emulator)
ECHO     2 = Nuked OPL3 fast  (same output, about half the CPU use;
ECHO                           recommended, especially on slower machines)
CHOICE /C:12 Which renderer do you want to install
IF ERRORLEVEL 2 GOTO copyfast
:copystd
COPY VOPLSRV.EXE C:\VOPL3\VOPLSRV.EXE
GOTO srvdone
:copyfast
COPY VOPLFAST.EXE C:\VOPL3\VOPLSRV.EXE
:srvdone
IF NOT EXIST C:\VOPL3\VOPLSRV.EXE GOTO copyerr
IF EXIST C:\VOPL3\VOPL3.INI GOTO inikeep
COPY VOPL3.INI C:\VOPL3\VOPL3.INI >NUL
GOTO inidone
:inikeep
REM upgrade: keep the user's settings, but deliver the new template (with
REM the latest settings documented) next to it for reference/merging
COPY VOPL3.INI C:\VOPL3\VOPL3NEW.INI >NUL
ECHO.
ECHO   Kept your existing C:\VOPL3\VOPL3.INI (your settings). The new
ECHO   version, with the latest settings documented, was saved as
ECHO   C:\VOPL3\VOPL3NEW.INI - copy new entries over if you want them.
:inidone

ECHO [3/5] Installing the VOPL3 control panel ...
IF EXIST C:\VOPL3\VOPLCFG.EXE ATTRIB -R C:\VOPL3\VOPLCFG.EXE >NUL
IF EXIST C:\VOPL3\VOPLCFG.EXE DEL C:\VOPL3\VOPLCFG.EXE >NUL
IF EXIST C:\VOPL3\VOPLCFG.EXE GOTO inuse
COPY VOPLCFG.EXE C:\VOPL3\VOPLCFG.EXE
IF NOT EXIST C:\VOPL3\VOPLCFG.EXE GOTO copyerr
ECHO.
ECHO   The control panel (C:\VOPL3\VOPLCFG.EXE) lets you pick the MIDI output
ECHO   device, adjust the FM volume and watch VOPL3's status - changes apply
ECHO   live, no reboot needed.
ECHO     1 = start it with Windows, minimized to the system tray (recommended)
ECHO     2 = do not autostart (run C:\VOPL3\VOPLCFG.EXE yourself when needed)
CHOICE /C:12 Start the control panel with Windows
IF ERRORLEVEL 2 GOTO nocfgrun
REGEDIT /S VOPLCFG.REG
:nocfgrun

ECHO [4/5] Registering VOPL3 (boot VxD + renderer autostart + mode) ...
REM INSTALL.REG sets the FM only mode; the others override it. Written
REM BEFORE patching, so a failed patch finished by hand later (see sberr)
REM still finds the chosen mode in place.
REGEDIT /S INSTALL.REG
IF "%VOPLMODE%"=="fmmidi" REGEDIT /S MIDION.REG
IF "%VOPLMODE%"=="midionly" REGEDIT /S MIDIONLY.REG

ECHO [5/5] Patching SBEMUL.SYS for this mode ...
SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS %VOPLMODE%
IF ERRORLEVEL 1 GOTO sberr

ECHO.
ECHO   ===================  INSTALL COMPLETE  ===================
ECHO   Please REBOOT now.
ECHO   After reboot, in your DOS game set:
IF "%VOPLMODE%"=="midionly" GOTO donemidi
ECHO       Music     = AdLib (or OPL3)
ECHO       Sound FX  = Sound Blaster
ECHO   You get OPL3 music (VOPL3) AND digital effects (SBEMUL) together.
GOTO donecommon
:donemidi
ECHO       Music     = General MIDI (MPU-401, port 330)
ECHO       Sound FX  = Sound Blaster
ECHO   You get MIDI music (VOPL3) AND digital effects (SBEMUL) together.
:donecommon
ECHO   The renderer C:\VOPL3\VOPLSRV.EXE starts automatically at login.
IF "%VOPLMODE%"=="fm" GOTO end
ECHO   Pick the MIDI device in the VOPL3 control panel
ECHO   (C:\VOPL3\VOPLCFG.EXE) - or in the Multimedia control panel or
ECHO   C:\VOPL3\VOPL3.INI (run MIDILIST.EXE to list device indices).
GOTO end

:sberr
ECHO.
ECHO   NOTE: the VxD + renderer are installed, but SBEMUL.SYS could not be
ECHO   patched (it may be in use). SoundBlaster digital audio will not work
ECHO   in DOS games until it is patched. To finish: restart in MS-DOS mode
ECHO   (Start - Shut Down - Restart in MS-DOS mode), change to this drive,
ECHO   and run:   SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS %VOPLMODE%
GOTO end

:inuse
ECHO.
ECHO   A previous VOPL3 file is still loaded / running and cannot be replaced.
ECHO   Please run UNINSTALL.BAT, REBOOT, then run INSTALL.BAT again.
GOTO end

:copyerr
ECHO   ERROR: could not copy files. Run this from the folder that contains
ECHO   INSTALL.BAT, VOPL3.VXD, VOPLSRV.EXE, VOPLFAST.EXE, VOPLCFG.EXE,
ECHO   SBPATCH.EXE, VOPLSTOP.EXE, INSTALL.REG, VOPLCFG.REG, MIDION.REG and
ECHO   MIDIONLY.REG.

:end
SET VOPLMODE=
ECHO.
PAUSE
