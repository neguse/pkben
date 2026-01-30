@echo off
setlocal enabledelayedexpansion

:: Find Visual Studio using vswhere
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_PATH=%%i"
)

if not defined VS_PATH (
    echo Error: Visual Studio not found
    exit /b 1
)

echo Found Visual Studio at: %VS_PATH%

:: Setup environment
call "%VS_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:: Create build directory
if not exist build mkdir build

:: Build
echo Building...
cd build
cmake -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release ..
if errorlevel 1 (
    echo CMake configuration failed
    exit /b 1
)

nmake
if errorlevel 1 (
    echo Build failed
    exit /b 1
)

echo.
echo Build successful! Run: build\pkben.exe
