@echo off
setlocal
set "ROOT=%~dp0.."
set "SRC=%ROOT%\src"
set "OUT=%ROOT%\out"
set "OBJ=%ROOT%\build\obj"
set "VSROOT=C:\BuildTools"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

echo [1/3] Loading MSVC environment ...
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

echo [2/3] Compiling shaders ...
set "FXC="
for /f "delims=" %%i in ('where fxc 2^>nul') do if not defined FXC set "FXC=%%i"
if not defined FXC set "FXC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"
if not exist "%FXC%" (
  echo   fxc.exe not found, skip shader compile
) else (
  "%FXC%" /nologo /T vs_5_0 /E VSMain /O3 /Fo "%OBJ%\vs.cso" "%SRC%\shaders.hlsl" || exit /b 1
  "%FXC%" /nologo /T ps_5_0 /E PSMain /O3 /Fo "%OBJ%\ps.cso" "%SRC%\shaders.hlsl" || exit /b 1
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0gen_blob.ps1" -Vs "%OBJ%\vs.cso" -Ps "%OBJ%\ps.cso" -Out "%SRC%\shader_blob.h" || exit /b 1
)

echo [3/3] Compiling FSDTrainer.dll ...
pushd "%OBJ%"
cl /nologo /LD /MT /O2 /std:c++17 /EHsc /utf-8 /DNDEBUG /I"%SRC%" ^
   "%SRC%\dllmain.cpp" "%SRC%\log.cpp" "%SRC%\mem.cpp" "%SRC%\hook.cpp" "%SRC%\config.cpp" ^
   "%SRC%\scanner.cpp" "%SRC%\patches.cpp" "%SRC%\ui.cpp" "%SRC%\overlay.cpp" ^
   /Fo"%OBJ%\\" /Fe:"%OUT%\FSDTrainer.dll" /Fd"%OBJ%\FSDTrainer.pdb" ^
   /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /IMPLIB:"%OBJ%\FSDTrainer.lib" ^
   d3d12.lib dxgi.lib d3d11.lib user32.lib gdi32.lib ole32.lib shell32.lib advapi32.lib
set "RC=%ERRORLEVEL%"
popd
if not "%RC%"=="0" (
  echo   BUILD FAILED rc=%RC%
  exit /b %RC%
)
echo Done: %OUT%\FSDTrainer.dll
endlocal
