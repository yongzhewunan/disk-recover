@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo  Disk Recover - Build and Package Script
echo ============================================
echo.

REM Get script directory (project root)
set "PROJECT_ROOT=%~dp0"
cd /d "%PROJECT_ROOT%"

REM Create release directory
if not exist "release" mkdir release

echo [1/3] Configuring CMake...
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=vcpkg\scripts\buildsystems\vcpkg.cmake
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed!
    pause
    exit /b 1
)

echo.
echo [2/3] Building Release version...
cmake --build build --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed!
    pause
    exit /b 1
)

echo.
echo [3/3] Copying files to release directory...

REM Copy GUI executable
if exist "build\src\ui\gui\Release\disk-recover.exe" (
    copy /Y "build\src\ui\gui\Release\disk-recover.exe" "release\disk-recover.exe"
    echo   - disk-recover.exe (GUI)
)

REM Copy CLI executable
if exist "build\src\ui\cli\Release\disk-recover-cli.exe" (
    copy /Y "build\src\ui\cli\Release\disk-recover-cli.exe" "release\disk-recover-cli.exe"
    echo   - disk-recover-cli.exe (CLI)
)

REM Copy README
if exist "README.md" (
    copy /Y "README.md" "release\README.md"
    echo   - README.md
)

echo.
echo Copying DLL dependencies...

REM SQLite3 DLL
if exist "vcpkg\packages\sqlite3_x64-windows\bin\sqlite3.dll" (
    copy /Y "vcpkg\packages\sqlite3_x64-windows\bin\sqlite3.dll" "release\sqlite3.dll"
    echo   - sqlite3.dll
) else if exist "build\vcpkg_installed\x64-windows\bin\sqlite3.dll" (
    copy /Y "build\vcpkg_installed\x64-windows\bin\sqlite3.dll" "release\sqlite3.dll"
    echo   - sqlite3.dll
) else if exist "vcpkg\installed\x64-windows\bin\sqlite3.dll" (
    copy /Y "vcpkg\installed\x64-windows\bin\sqlite3.dll" "release\sqlite3.dll"
    echo   - sqlite3.dll
)

REM FFmpeg DLLs
for %%D in (avcodec-62 avdevice-62 avfilter-11 avformat-62 avutil-60 swresample-6 swscale-9) do (
    if exist "vcpkg\packages\ffmpeg_x64-windows\bin\%%D.dll" (
        copy /Y "vcpkg\packages\ffmpeg_x64-windows\bin\%%D.dll" "release\%%D.dll"
        echo   - %%D.dll
    ) else if exist "build\vcpkg_installed\x64-windows\bin\%%D.dll" (
        copy /Y "build\vcpkg_installed\x64-windows\bin\%%D.dll" "release\%%D.dll"
        echo   - %%D.dll
    ) else if exist "vcpkg\installed\x64-windows\bin\%%D.dll" (
        copy /Y "vcpkg\installed\x64-windows\bin\%%D.dll" "release\%%D.dll"
        echo   - %%D.dll
    )
)

echo.
echo ============================================
echo  Build completed successfully!
echo  Output directory: %PROJECT_ROOT%release\
echo ============================================
echo.

REM List release contents
echo Release contents:
dir /B release
echo.

pause