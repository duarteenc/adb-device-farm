@echo off
:: Runs the farmcore unit tests (Qt Test via CTest). Build first with scripts\build.bat.
setlocal
cd /d "%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSINSTALL="
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
)
if exist "C:\Qt\Tools\CMake_64\bin\cmake.exe" set "PATH=C:\Qt\Tools\CMake_64\bin;%PATH%"
where cmake >nul 2>&1
if errorlevel 1 if defined VSINSTALL set "PATH=%VSINSTALL%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%"
if not exist build\CMakeCache.txt (
  echo [test] ERROR: not configured - run scripts\build.bat first.
  exit /b 1
)
echo [test] Running unit tests...
ctest --test-dir build --output-on-failure -C RelWithDebInfo %*
if errorlevel 1 (
  echo [test] TESTS_FAILED
  exit /b 1
)
echo [test] TESTS_OK
