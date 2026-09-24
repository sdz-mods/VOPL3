@ECHO OFF
ECHO.
ECHO   VOPL3 uninstaller
ECHO   ================
ECHO   Restores the original SBEMUL.SYS and removes VOPL3.
ECHO.
PAUSE

ECHO Restoring original SBEMUL.SYS ...
IF NOT EXIST C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS.orig GOTO nosbrestore
ATTRIB -R C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS
DEL C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS
COPY C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS.orig C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS >NUL
DEL C:\WINDOWS\SYSTEM32\DRIVERS\SBEMUL.SYS.orig
:nosbrestore

ECHO Removing registry entries ...
REM Run from the package folder, or from the copy installed in C:\VOPL3.
IF EXIST UNINSTALL.REG REGEDIT /S UNINSTALL.REG
IF NOT EXIST UNINSTALL.REG REGEDIT /S C:\VOPL3\UNINSTALL.REG

ECHO Removing the license texts ...
REM Not in use, so deleted now - and WININIT.INI below can't take their long
REM names anyway (it runs before Windows, with 8.3 names only).
FOR %%F IN (NUKED-OPL3 NUKED-OPL3-FAST DBOPL YMFM) DO IF EXIST C:\VOPL3\%%F-LICENSE.txt DEL C:\VOPL3\%%F-LICENSE.txt

ECHO Scheduling VOPL3 file removal (finished at next reboot) ...
REM  The VxD is loaded and the renderer is running, so they cannot be deleted
REM  now. WININIT.INI removes them at next boot, before Windows loads them.
ECHO [rename]>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\WINDOWS\SYSTEM\VOPL3.VXD>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLSRV.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLFAST.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLDB.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLYM.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLCFG.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPL3.INI>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPL3NEW.INI>>C:\WINDOWS\WININIT.INI

ECHO.
ECHO   Done. Reboot to complete removal.
ECHO   Afterwards C:\VOPL3 holds nothing but this uninstaller (a running
ECHO   batch file cannot delete itself) - remove the folder if you want
ECHO   it gone.
PAUSE
