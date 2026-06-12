@echo off
:: v2.0 build script — MSVC x64 + Qt 6.11.1 + Ninja
:: Run from anywhere; it cd's to the repo root.
setlocal
cd /d "%~dp0.."

echo [build] Setting up MSVC x64 environment...
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (
  echo [build] ERROR: could not initialize MSVC environment.
  exit /b 1
)

:: Use the CMake + Ninja bundled with Qt
set "PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%"

echo [build] Configuring...
cmake --preset msvc-x64
if errorlevel 1 (
  echo [build] ERROR: cmake configure failed.
  exit /b 1
)

echo [build] Building...
cmake --build --preset msvc-x64
if errorlevel 1 (
  echo [build] ERROR: cmake build failed.
  exit /b 1
)

echo [build] BUILD_OK
