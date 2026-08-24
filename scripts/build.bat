@echo off
:: ADB Device Farm build script — MSVC x64 + Qt 6 + Ninja.
:: Run from anywhere; it cd's to the repo root. Auto-detects the toolchain:
::   * Visual Studio (any edition/version with the C++ x64 tools) via vswhere
::   * Qt MSVC kit: %QT_ROOT%, then C:\Qt\6.11.1\msvc2022_64, then any C:\Qt\6.*\msvc*_64
::   * Ninja / CMake: Qt Tools, then the Visual Studio bundled copies, then PATH
:: Prints [build] BUILD_OK on success. Binary: output\x64\RelWithDebInfo\QtScrcpy.exe
setlocal EnableDelayedExpansion
cd /d "%~dp0.."

:: ---- Visual Studio ----
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if not defined VSINSTALL (
  for %%p in ("%ProgramFiles%\Microsoft Visual Studio\18\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\Community" "%ProgramFiles%\Microsoft Visual Studio\2022\Professional" "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools" "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools") do (
    if not defined VSINSTALL if exist "%%~p\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=%%~p"
  )
)
if not defined VSINSTALL (
  echo [build] ERROR: no Visual Studio with C++ x64 tools found. Install "Desktop development with C++".
  exit /b 1
)
echo [build] Visual Studio: %VSINSTALL%
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 (
  echo [build] ERROR: could not initialize MSVC environment.
  exit /b 1
)

:: ---- Qt ----
if defined QT_ROOT if exist "%QT_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" set "QTDIR=%QT_ROOT%"
if not defined QTDIR if exist "C:\Qt\6.11.1\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" set "QTDIR=C:\Qt\6.11.1\msvc2022_64"
if not defined QTDIR (
  for /d %%v in ("C:\Qt\6.*") do (
    for /d %%k in ("%%~v\msvc*_64") do (
      if not defined QTDIR if exist "%%~k\lib\cmake\Qt6\Qt6Config.cmake" set "QTDIR=%%~k"
    )
  )
)
if not defined QTDIR (
  echo [build] ERROR: Qt 6 MSVC kit not found. Run scripts\bootstrap.ps1 or set QT_ROOT.
  exit /b 1
)
set "QTDIR=%QTDIR:\=/%"
echo [build] Qt: %QTDIR%

:: ---- Ninja / CMake ----
set "NINJA="
if exist "C:\Qt\Tools\Ninja\ninja.exe" set "NINJA=C:\Qt\Tools\Ninja\ninja.exe"
if not defined NINJA if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" set "NINJA=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not defined NINJA for %%i in (ninja.exe) do if not "%%~$PATH:i"=="" set "NINJA=%%~$PATH:i"
if not defined NINJA (
  echo [build] ERROR: ninja.exe not found. Install it with: winget install Ninja-build.Ninja
  exit /b 1
)
set "NINJA=%NINJA:\=/%"
if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" set "PATH=C:\Qt\Tools\CMake_64\bin;%PATH%"
where cmake >nul 2>&1
if errorlevel 1 (
  if exist "%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
)
where cmake >nul 2>&1
if errorlevel 1 (
  echo [build] ERROR: cmake.exe not found. Install it with: winget install Kitware.CMake
  exit /b 1
)
echo [build] Ninja: %NINJA%
for /f "tokens=*" %%i in ('cmake --version') do (echo [build] %%i & goto :cmake_ver_done)
:cmake_ver_done

:: ---- Configure + build ----
set "PRESET=msvc-x64"
if /i "%~1"=="debug" set "PRESET=msvc-x64-debug"
echo [build] Configuring (%PRESET%)...
cmake --preset %PRESET% -DCMAKE_PREFIX_PATH="%QTDIR%" -DCMAKE_MAKE_PROGRAM="%NINJA%"
if errorlevel 1 (
  echo [build] ERROR: cmake configure failed.
  exit /b 1
)

echo [build] Building...
cmake --build --preset %PRESET%
if errorlevel 1 (
  echo [build] ERROR: cmake build failed.
  exit /b 1
)

echo [build] BUILD_OK
