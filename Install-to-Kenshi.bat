@echo off
setlocal EnableDelayedExpansion
rem ==============================================================
rem  Warborn - copy the built mod into Kenshi\mods\Warborn
rem  Double-click after a successful Release x64 build.
rem  Other Kenshi folder?  Run:  Install-to-Kenshi.bat "D:\Games\Kenshi"
rem  (No ( ) blocks on purpose: "Program Files (x86)" breaks them.)
rem ==============================================================
set "KENSHI=C:\Program Files (x86)\Steam\steamapps\common\Kenshi"
if not "%~1"=="" set "KENSHI=%~1"
set "SRC=%~dp0"
set "DLL=!SRC!Warborn\Warborn.dll"
set "DEST=!KENSHI!\mods\Warborn"

if not exist "!KENSHI!\kenshi_x64.exe" goto :nokenshi
if not exist "!DLL!" goto :nodll

if not exist "!DEST!" mkdir "!DEST!"
if not exist "!DEST!" goto :denied

copy /Y "!DLL!" "!DEST!\" >nul
if errorlevel 1 goto :denied
copy /Y "!SRC!package\RE_Kenshi.json" "!DEST!\" >nul
if errorlevel 1 goto :denied
copy /Y "!SRC!package\Warborn.mod" "!DEST!\" >nul
if errorlevel 1 goto :denied
if exist "!DEST!\Warborn.ini" copy /Y "!DEST!\Warborn.ini" "!DEST!\Warborn.ini.bak" >nul
copy /Y "!SRC!Warborn.ini" "!DEST!\" >nul
if errorlevel 1 goto :denied
rem the older Direct Control based build lived here too - remove it
if exist "!DEST!\WASDCombatPlugin.dll" del /Q "!DEST!\WASDCombatPlugin.dll"
if exist "!DEST!\WASDCombatPlugin.ini" del /Q "!DEST!\WASDCombatPlugin.ini"

echo [OK] Warborn installed to:
echo      !DEST!
echo.
echo Next: start Kenshi with RE_Kenshi, open Mods, tick "Warborn".
goto :end

:nokenshi
echo [X] Kenshi not found in:
echo     !KENSHI!
echo     Run it like this:  Install-to-Kenshi.bat "C:\path\to\Kenshi"
goto :end

:nodll
echo [X] Built mod not found:
echo     !DLL!
echo     Build it first in Visual Studio: Release, x64, Build Solution.
goto :end

:denied
echo [X] Could not write to:
echo     !DEST!
echo     Right-click Install-to-Kenshi.bat and choose "Run as administrator".

:end
echo.
pause
