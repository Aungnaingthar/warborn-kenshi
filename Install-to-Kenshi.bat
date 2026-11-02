@echo off
setlocal
rem ==============================================================
rem  Warborn - copy the built mod into Kenshi\mods\Warborn
rem  Double-click after a successful Release x64 build.
rem  Other Kenshi folder?  Drag this file onto a shortcut, or run:
rem    Install-to-Kenshi.bat "D:\Games\Kenshi"
rem ==============================================================
set "KENSHI=C:\Program Files (x86)\Steam\steamapps\common\Kenshi"
if not "%~1"=="" set "KENSHI=%~1"
set "SRC=%~dp0"
set "DLL=%SRC%WASDCombatPlugin\WASDCombatPlugin.dll"
set "DEST=%KENSHI%\mods\Warborn"

if not exist "%KENSHI%\kenshi_x64.exe" (
  echo [X] Kenshi not found in: %KENSHI%
  echo     Run it like this:  Install-to-Kenshi.bat "C:\path\to\Kenshi"
  goto :end
)
if not exist "%DLL%" (
  echo [X] Built mod not found: %DLL%
  echo     Build it first in Visual Studio: Release ^| x64, Build Solution.
  goto :end
)

if not exist "%DEST%" mkdir "%DEST%"
if errorlevel 1 goto :denied

copy /Y "%DLL%" "%DEST%\" >nul || goto :denied
copy /Y "%SRC%package\RE_Kenshi.json" "%DEST%\" >nul || goto :denied
copy /Y "%SRC%package\Warborn.mod" "%DEST%\" >nul || goto :denied
if exist "%DEST%\WASDCombatPlugin.ini" copy /Y "%DEST%\WASDCombatPlugin.ini" "%DEST%\WASDCombatPlugin.ini.bak" >nul
copy /Y "%SRC%WASDCombatPlugin.ini" "%DEST%\" >nul || goto :denied

echo [OK] Warborn installed to:
echo      %DEST%
echo.
echo Next: start Kenshi with RE_Kenshi, open Mods, tick "Warborn".
echo Turn OFF the original "Direct Control" mod if you have it.
goto :end

:denied
echo [X] Could not write to %DEST%
echo     Right-click Install-to-Kenshi.bat and choose "Run as administrator".

:end
echo.
pause
