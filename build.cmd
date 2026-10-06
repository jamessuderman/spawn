@echo off
REM build.cmd - configure + build Spawn (Standalone) using MSVC + CMake presets.
REM Usage:  build.cmd [debug|release]   (defaults to debug)
setlocal

set CFG=%1
if "%CFG%"=="" set CFG=debug

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul
if errorlevel 1 (
    echo Failed to set up MSVC environment.
    exit /b 1
)

echo === Configure ===
cmake --preset=%CFG%-x64
if errorlevel 1 exit /b 1

echo === Build ===
cmake --build --preset=%CFG%
endlocal