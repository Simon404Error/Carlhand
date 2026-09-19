@echo off
setlocal
set "ROOT=%~dp0.."
echo [1/2] Rebuild FSDTrainer.dll ...
call "%~dp0build.bat" || exit /b 1
echo [2/2] Repack FSDTrainer.exe ...
call "%~dp0loader.bat" || exit /b 1
echo.
echo Repacked: %ROOT%\out\FSDTrainer.exe   (inject it again; the game must be restarted)
endlocal
