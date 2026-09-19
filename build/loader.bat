@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\out"
set "OBJ=%ROOT%\build\obj"
set "VSROOT=C:\BuildTools"
set "UE4DIR=D:\Deepseek-Harness\install\UE4SS"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

echo [1/4] Build FSDTrainer.dll ...
call "%~dp0build.bat" || exit /b 1

call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

echo [2/4] Generate resource script ...
set "RC_FILE=%OBJ%\loader.rc"
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$dll=('%OUT%\FSDTrainer.dll').Replace('\','\\'); $ini=('%ROOT%\config\FSDTrainer.ini').Replace('\','\\'); $o='%RC_FILE%';" ^
  "$u=('%UE4DIR%\UE4SS.dll').Replace('\','\\'); $us=('%ROOT%\config\UE4SS-settings.ini').Replace('\','\\');" ^
  "$t='101 RCDATA \"'+$dll+'\"'+\"`r`n\"+'102 RCDATA \"'+$ini+'\"'+\"`r`n\"+'103 RCDATA \"'+$u+'\"'+\"`r`n\"+'104 RCDATA \"'+$us+'\"'+\"`r`n\";" ^
  "[System.IO.File]::WriteAllText($o,$t,(New-Object System.Text.ASCIIEncoding))" || exit /b 1

echo [3/4] Compile resources ...
set "RC="
for /f "delims=" %%i in ('where rc 2^>nul') do if not defined RC set "RC=%%i"
if not defined RC set "RC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe"
"%RC%" /nologo /fo "%OBJ%\loader.res" "%RC_FILE%" || exit /b 1

echo [4/4] Link FSDTrainer.exe ...
pushd "%OBJ%"
cl /nologo /MT /O2 /std:c++17 /EHsc /utf-8 /DNDEBUG /DUNICODE /D_UNICODE ^
   "%ROOT%\loader\FSDTrainerLoader.cpp" ^
   /Fo"%OBJ%\\" /Fe:"%OUT%\FSDTrainer.exe" /Fd"%OBJ%\FSDTrainer.pdb" ^
   /link /SUBSYSTEM:WINDOWS /ENTRY:wmainCRTStartup /MACHINE:X64 "%OBJ%\loader.res" ^
   psapi.lib shell32.lib user32.lib advapi32.lib
set "RC2=%ERRORLEVEL%"
popd
if not "%RC2%"=="0" ( echo LINK FAILED rc=%RC2% & exit /b %RC2% )
echo Done: %OUT%\FSDTrainer.exe
endlocal
