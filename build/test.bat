@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\out"
set "VERIFY=%OUT%\verify"
set "OBJ=%ROOT%\build\obj"
set "VSROOT=C:\BuildTools"
if not exist "%VERIFY%" mkdir "%VERIFY%"
if not exist "%OBJ%" mkdir "%OBJ%"
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

echo [1/3] Compiling Dx12Test.exe / D3D11Test.exe ...
pushd "%OBJ%"
cl /nologo /MT /O2 /std:c++17 /EHsc /utf-8 /DNDEBUG "%ROOT%\test\Dx12Test.cpp" ^
   /Fo"%OBJ%\\" /Fe:"%VERIFY%\Dx12Test.exe" /link /SUBSYSTEM:CONSOLE /MACHINE:X64 d3d12.lib dxgi.lib user32.lib || exit /b 1
cl /nologo /MT /O2 /std:c++17 /EHsc /utf-8 /DNDEBUG "%ROOT%\test\D3D11Test.cpp" ^
   /Fo"%OBJ%\\" /Fe:"%VERIFY%\D3D11Test.exe" /link /SUBSYSTEM:CONSOLE /MACHINE:X64 d3d11.lib dxgi.lib user32.lib || exit /b 1
popd

echo [2/3] Preparing verify config (menu_open=1) ...
copy /y "%ROOT%\config\FSDTrainer.ini" "%VERIFY%\FSDTrainer.ini" >nul
powershell -NoProfile -Command "$p='%VERIFY%\FSDTrainer.ini'; $t=[IO.File]::ReadAllText($p,[Text.Encoding]::Unicode); [IO.File]::WriteAllText($p,$t.Replace('menu_open=0','menu_open=1'),(New-Object System.Text.UnicodeEncoding($false,$true)))"

echo [3/3] Done
echo   %VERIFY%\Dx12Test.exe    %VERIFY%\D3D11Test.exe
endlocal
