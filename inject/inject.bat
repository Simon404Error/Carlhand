@echo off
setlocal
set "ROOT=%~dp0.."
set "DLL=%ROOT%\out\FSDTrainer.dll"
set "XPR=%ROOT%\inject\FSDTrainer.xpr"
set "XENOS=D:\SOMTOLS\DeepRockGalactic\in\Xenos64.exe"
set "XENOSDIR=D:\SOMTOLS\DeepRockGalactic\in"
set "GAME=E:\SteamLibrary\steamapps\common\Deep Rock Galactic"

if not exist "%DLL%" (
  echo [!] DLL not found: %DLL%   run build\build.bat first
  exit /b 1
)
if not exist "%XENOS%" (
  echo [!] injector not found: %XENOS%
  exit /b 1
)

echo [1/4] Build injection profile ...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Make-Xpr.ps1" -Dll "%DLL%" -Out "%XPR%" || exit /b 1

echo [2/4] Backup Xenos current profile ...
if exist "%XENOSDIR%\XenosCurrentProfile.xpr" (
  if not exist "%XENOSDIR%\XenosCurrentProfile.xpr.bak" copy /y "%XENOSDIR%\XenosCurrentProfile.xpr" "%XENOSDIR%\XenosCurrentProfile.xpr.bak" >nul
)
copy /y "%XPR%" "%XENOSDIR%\XenosCurrentProfile.xpr" >nul

echo [3/4] Deploy config to game folder ...
if not exist "%GAME%\FSDTrainer.ini" copy /y "%ROOT%\config\FSDTrainer.ini" "%GAME%\FSDTrainer.ini" >nul

echo [4/4] Launch Xenos64 (admin required)
echo      Select FSD-Win64-Shipping.exe in the process list, then click Inject.
echo      Game must run in DX12 mode (Steam launch option: -dx12).
start "" "%XENOS%" --load "%XPR%"
endlocal
