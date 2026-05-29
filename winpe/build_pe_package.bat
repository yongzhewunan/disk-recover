@echo off
echo Building WinPE deployment package...

set OUTPUT=winpe_package
if exist %OUTPUT% rmdir /s /q %OUTPUT%
mkdir %OUTPUT%

REM Copy GUI executable
copy build\src\ui\gui\Release\disk-recover.exe %OUTPUT%\

REM Copy CLI executable
copy build\src\ui\cli\Release\disk-recover-cli.exe %OUTPUT%\

REM Copy FFmpeg DLLs
copy build\vcpkg_installed\x64-windows\bin\avcodec-*.dll %OUTPUT%\ >nul 2>&1
copy build\vcpkg_installed\x64-windows\bin\avformat-*.dll %OUTPUT%\ >nul 2>&1
copy build\vcpkg_installed\x64-windows\bin\avutil-*.dll %OUTPUT%\ >nul 2>&1
copy build\vcpkg_installed\x64-windows\bin\swscale-*.dll %OUTPUT%\ >nul 2>&1
copy build\vcpkg_installed\x64-windows\bin\swresample-*.dll %OUTPUT%\ >nul 2>&1

REM Copy SQLite DLL
copy build\vcpkg_installed\x64-windows\bin\sqlite3.dll %OUTPUT%\ >nul 2>&1

echo.
echo Package created in %OUTPUT%
echo Contents:
dir %OUTPUT%

echo.
echo Done. Copy the winpe_package folder to your WinPE environment.