@echo off
setlocal

REM Use VS 2022 bundled CMake instead of broken system CMake
set CMAKE="C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

set VCPKG_ROOT=%~dp0vcpkg
set TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

echo === Disk Recover Build Script ===
echo CMake: %CMAKE%
echo vcpkg: %VCPKG_ROOT%

REM Step 1: Bootstrap vcpkg if needed
if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo Bootstrapping vcpkg...
    cd /d "%VCPKG_ROOT%"
    call bootstrap-vcpkg.bat -disableMetrics
    cd /d "%~dp0"
)

REM Step 2: Configure
echo Configuring project...
%CMAKE% -B build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE=%TOOLCHAIN%
if %ERRORLEVEL% NEQ 0 (
    echo Configuration failed!
    exit /b 1
)

REM Step 3: Build
echo Building project...
%CMAKE% --build build --config Debug
if %ERRORLEVEL% NEQ 0 (
    echo Build failed!
    exit /b 1
)

REM Step 4: Test
echo Running tests...
cd build
ctest --output-on-failure -C Debug
cd /d "%~dp0"

echo === Build complete ===
