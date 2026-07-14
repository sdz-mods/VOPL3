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
REGEDIT /S UNINSTALL.REG

ECHO Scheduling VOPL3 file removal (finished at next reboot) ...
REM  The VxD is loaded and the renderer is running, so they cannot be deleted
REM  now. WININIT.INI removes them at next boot, before Windows loads them.
ECHO [rename]>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\WINDOWS\SYSTEM\VOPL3.VXD>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLSRV.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPLCFG.EXE>>C:\WINDOWS\WININIT.INI
ECHO NUL=C:\VOPL3\VOPL3.INI>>C:\WINDOWS\WININIT.INI

ECHO.
ECHO   Done. Reboot to complete removal.
PAUSE
