@ECHO OFF
ECHO.
ECHO   VOPL3 - OPL3 FM emulation for Windows 98/ME
ECHO   ===========================================
ECHO.
ECHO   This installer will:
ECHO     1. install VOPL3.VXD          (loads at boot, traps the AdLib/OPL
ECHO                                    and/or MPU-401 MIDI ports)
ECHO     2. install the OPL3 renderer  (C:\VOPL3\VOPLSRV.EXE, autostarts;
ECHO        you pick one of three OPL3 emulator builds)
ECHO     3. install the control panel  (C:\VOPL3\VOPLCFG.EXE - MIDI device,
ECHO        FM volume, status; you choose if it starts with Windows)
ECHO     4. set up SBEMUL so it leaves the chosen ports alone (digital
ECHO        audio stays): FM through SBEMUL's own SoftFM registry setting,
ECHO        MIDI by patching SBEMUL.SYS. Original backed up.
ECHO   First you choose who handles the FM ports and the MIDI ports.
ECHO.
ECHO   (If VOPL3 is already installed, this will stop the running renderer
ECHO   first. If that ever fails, run UNINSTALL.BAT + reboot, then this.)
ECHO.
ECHO   Press Ctrl+C to abort, or
PAUSE

:askfm
ECHO.
ECHO   FM - the AdLib/OPL3 ports 388-38B:
ECHO     1 = VOPL3      - SBEMUL is steered away from these ports, and VOPL3
ECHO                      traps them and synthesizes the music. Recommended.
ECHO     2 = Left free  - SBEMUL is steered away from these ports, and VOPL3
ECHO                      does NOT trap them either: nothing claims them, so
ECHO                      they are free for anything else that uses them.
ECHO     3 = SBEMUL     - left as stock: games detect an AdLib, but AdLib
ECHO                      music is silent.
CHOICE /C:123 FM ports
IF ERRORLEVEL 3 GOTO fmsbemul
IF ERRORLEVEL 2 GOTO fmfree
SET VOPLFM=vopl
GOTO askmidi
:fmfree
SET VOPLFM=free
GOTO askmidi
:fmsbemul
SET VOPLFM=sbemul

:askmidi
ECHO.
ECHO   MIDI - the MPU-401 ports 330/331:
ECHO     1 = VOPL3      - SBEMUL is steered away from these ports, and VOPL3
ECHO                      traps them and sends DOS-game MIDI to the Windows
ECHO                      MIDI Mapper: by default the same Microsoft GS
ECHO                      Wavetable synth SBEMUL uses, or ANY other installed
ECHO                      synth/device you pick in the control panel.
ECHO                      Recommended.
ECHO     2 = SBEMUL     - left as stock: MIDI plays on the Microsoft GS
ECHO                      Wavetable synth.
CHOICE /C:12 MIDI ports
IF ERRORLEVEL 2 GOTO midisbemul
SET VOPLMIDI=vopl
GOTO askdone
:midisbemul
SET VOPLMIDI=sbemul
:askdone
IF NOT "%VOPLFM%%VOPLMIDI%"=="sbemulsbemul" GOTO asksok
ECHO.
ECHO   With both left to SBEMUL there is nothing for VOPL3 to do - please
ECHO   choose again.
GOTO askfm
:asksok

REM the port groups SBEMUL must leave alone (SBPATCH arguments). SBPATCH
REM still gets "fm": on SBEMUL builds other than 4.10.2222/2223, where
REM SoftFM may not be read, it moves the FM table as a fallback; on
REM 4.10.2222/2223 it gives back an FM table an earlier install moved.
SET VOPLSB=
IF NOT "%VOPLFM%"=="sbemul" SET VOPLSB=fm
IF "%VOPLMIDI%"=="vopl" SET VOPLSB=%VOPLSB% midi

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
REM VOPL3 not playing FM: no synthesis, so the choice of OPL3 emulator doesn't matter
IF NOT "%VOPLFM%"=="vopl" GOTO copystd
ECHO.
ECHO   Three renderer builds are included:
ECHO     1 = Nuked OPL3       (the reference emulator - the most accurate)
ECHO     2 = Nuked OPL3 fast  (same output as 1, about half the CPU use;
ECHO                           recommended, especially on slower machines)
ECHO     3 = DOSBox DBOPL     (DOSBox's emulator: a small fraction of the
ECHO                           CPU use of 1 or 2, but less accurate)
CHOICE /C:123 Which renderer do you want to install
IF ERRORLEVEL 3 GOTO copydb
IF ERRORLEVEL 2 GOTO copyfast
:copystd
COPY VOPLSRV.EXE C:\VOPL3\VOPLSRV.EXE
GOTO srvdone
:copyfast
COPY VOPLFAST.EXE C:\VOPL3\VOPLSRV.EXE
GOTO srvdone
:copydb
COPY VOPLDB.EXE C:\VOPL3\VOPLSRV.EXE
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

ECHO [4/5] Registering VOPL3 (boot VxD + renderer autostart + choices) ...
REM INSTALL.REG sets FM = VOPL3, MIDI = SBEMUL, and SBEMUL's SoftFM=1 (it
REM leaves all its FM ports alone, 388-38B and base+8/+9); the others
REM override that. FMSBEMUL.REG removes SoftFM again (stock SBEMUL FM).
REM Written BEFORE patching, so a failed patch finished by hand later (see
REM sberr) still finds the choices in place.
REGEDIT /S INSTALL.REG
IF "%VOPLFM%"=="sbemul" REGEDIT /S FMSBEMUL.REG
IF "%VOPLFM%"=="free" REGEDIT /S FMFREE.REG
IF "%VOPLMIDI%"=="vopl" REGEDIT /S MIDION.REG

ECHO [5/5] Setting up SBEMUL.SYS to leave the chosen ports alone ...
SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS %VOPLSB%
IF ERRORLEVEL 1 GOTO sberr

ECHO.
ECHO   ===================  INSTALL COMPLETE  ===================
ECHO   Please REBOOT now.
ECHO   After reboot, in your DOS game set Sound FX = Sound Blaster, and Music:
IF "%VOPLFM%"=="vopl" ECHO     AdLib (or OPL3)                - played by VOPL3
IF "%VOPLFM%"=="free" ECHO     AdLib (or OPL3)                - ports 388-38B are left free
IF "%VOPLMIDI%"=="vopl" ECHO     General MIDI (MPU-401, 330)    - played by VOPL3
IF "%VOPLMIDI%"=="sbemul" ECHO     General MIDI (MPU-401, 330)    - played by SBEMUL
ECHO   The renderer C:\VOPL3\VOPLSRV.EXE starts automatically at login.
IF NOT "%VOPLMIDI%"=="vopl" GOTO end
ECHO   Pick the MIDI device in the VOPL3 control panel
ECHO   (C:\VOPL3\VOPLCFG.EXE) - or in the Multimedia control panel or
ECHO   C:\VOPL3\VOPL3.INI (run MIDILIST.EXE to list device indices).
GOTO end

:sberr
ECHO.
ECHO   NOTE: the VxD + renderer are installed, but SBEMUL.SYS could not be
ECHO   patched (see the messages above - it may be in use). SoundBlaster
ECHO   digital audio will not work in DOS games until it is patched. If it
ECHO   was in use, finish from MS-DOS mode (Start - Shut Down - Restart in
ECHO   MS-DOS mode), change to this drive, and run:
ECHO       SBPATCH.EXE C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS %VOPLSB%
GOTO end

:inuse
ECHO.
ECHO   A previous VOPL3 file is still loaded / running and cannot be replaced.
ECHO   Please run UNINSTALL.BAT, REBOOT, then run INSTALL.BAT again.
GOTO end

:copyerr
ECHO   ERROR: could not copy files. Run this from the folder that contains
ECHO   INSTALL.BAT, VOPL3.VXD, VOPLSRV.EXE, VOPLFAST.EXE, VOPLDB.EXE, VOPLCFG.EXE,
ECHO   SBPATCH.EXE, VOPLSTOP.EXE, INSTALL.REG, VOPLCFG.REG, MIDION.REG,
ECHO   FMSBEMUL.REG and FMFREE.REG.

:end
SET VOPLFM=
SET VOPLMIDI=
SET VOPLSB=
ECHO.
PAUSE
