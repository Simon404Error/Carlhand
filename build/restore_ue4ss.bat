@echo off
rem ============================================================
rem  Restore Lua-capable UE4SS v3.0.1 into the DRG game folder.
rem  Reason: some third-party trainer packages replace UE4SS with a
rem  Lua-less loader ("UE4SSL"), which silently kills Carlhand's
rem  in-game Lua engine (menu still opens, all game features die).
rem  This script backs up the current loader and puts UE4SS back.
rem  Run it, then start the game and run Carlhand.exe as usual.
rem ============================================================
setlocal
set "GAME=E:\SteamLibrary\steamapps\common\Deep Rock Galactic\FSD\Binaries\Win64"
set "SRC=D:\Deepseek-Harness\install\UE4SS"
set "BK=D:\Deepseek-Harness\work\260918_143041\backup_ue4ssl"

if not exist "%SRC%\UE4SS.dll" ( echo [ERROR] UE4SS source not found: %SRC%\UE4SS.dll & pause & exit /b 1 )
if not exist "%GAME%\FSD-Win64-Shipping.exe" ( echo [ERROR] game folder not found: %GAME% & pause & exit /b 1 )

echo [1/5] closing game ...
taskkill /F /IM FSD-Win64-Shipping.exe >nul 2>&1
timeout /t 5 /nobreak >nul

echo [2/5] backing up current loader to %BK% ...
if not exist "%BK%" mkdir "%BK%"
if exist "%GAME%\ue4ss"          xcopy /E /I /Y "%GAME%\ue4ss" "%BK%\ue4ss" >nul
if exist "%GAME%\x3daudio1_7.dll" copy /Y "%GAME%\x3daudio1_7.dll" "%BK%\" >nul
if exist "%GAME%\dwmapi.dll"      copy /Y "%GAME%\dwmapi.dll"      "%BK%\dwmapi_current.dll" >nul

echo [3/5] removing UE4SSL files ...
if exist "%GAME%\ue4ss" rmdir /S /Q "%GAME%\ue4ss"
del /F /Q "%GAME%\x3daudio1_7.dll" >nul 2>&1

echo [4/5] installing UE4SS v3.0.1 ...
copy /Y "%SRC%\dwmapi.dll"          "%GAME%\dwmapi.dll" >nul
copy /Y "%SRC%\UE4SS.dll"           "%GAME%\UE4SS.dll" >nul
if not exist "%GAME%\UE4SS-settings.ini" copy /Y "%SRC%\UE4SS-settings.ini" "%GAME%\UE4SS-settings.ini" >nul
if not exist "%GAME%\Mods" xcopy /E /I /Y "%SRC%\Mods" "%GAME%\Mods" >nul

echo [5/5] ensure EngineVersionOverride = 4.27 ...
powershell -NoProfile -Command "$p='%GAME%\UE4SS-settings.ini'; $t=[IO.File]::ReadAllText($p); if($t -notmatch 'MajorVersion = 4'){ $t=$t -replace 'MajorVersion = \r?\n',\"MajorVersion = 4`r`n\" -replace 'MinorVersion = \r?\n',\"MinorVersion = 27`r`n\"; [IO.File]::WriteAllText($p,$t) ; Write-Host '  patched' } else { Write-Host '  ok' }"

echo.
echo Done. UE4SS v3.0.1 (Lua) restored.
echo   * the FSDTrainerMod executor mod is re-deployed automatically when you run Carlhand.exe
echo   * if you run that third-party package again it will overwrite this again - just run this script once more
echo.
pause
endlocal
