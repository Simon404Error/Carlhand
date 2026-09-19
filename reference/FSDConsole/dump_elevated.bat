@echo off
chcp 65001 >nul
setlocal
set "DIR=%~dp0"
echo ============================================
echo  以管理员身份转储 FSDConsole.exe 内存（含解密字符串）
echo  输出目录: %DIR%
echo ============================================
powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -Verb RunAs -Wait -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File','\"%DIR%dump_fsdconsole.ps1\"'"
echo.
echo 完成（若上面没有报错）。转储文件已写入:
echo   %DIR%fsdconsole_full.dmp
pause
