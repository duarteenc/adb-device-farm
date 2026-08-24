# Building on Windows (Qt 6 + MSVC)

Native C++17 / Qt 6 application built with **MSVC x64 + Ninja**. Nothing is
hard-coded to one machine: the scripts detect the toolchain.

## 1. Check what you have

```powershell
.\scripts\check-environment.ps1
```

```
Git                    PASS   git version 2.54.0.windows.1 @ C:\Program Files\Git\cmd\git.exe
GitHub CLI             WARN   not found - winget install GitHub.cli (optional, for PRs)
MSVC                   PASS   toolset 14.51.36231 @ C:\Program Files\Microsoft Visual Studio\18\Community
Qt                     PASS   Qt 6.11.1 @ C:\Qt\6.11.1\msvc2022_64
Qt MSVC kit            PASS   Core Widgets Network Multimedia OpenGL OpenGLWidgets Sql Concurrent Test
CMake                  PASS   cmake version 4.3.1 @ ...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
Ninja                  PASS   ninja 1.13.2 @ ...\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe
ADB                    PASS   Android Debug Bridge version 1.0.41 ...
FFmpeg project libs    PASS   ...\QtScrcpyCore\src\third_party\ffmpeg (lib/x64 + bin/x64 + include)
lunasvg submodule      PASS   initialized
OpenGL                 PASS   Intel(R) HD Graphics 4000 ...
```

## 2. Install what is missing

```powershell
.\scripts\bootstrap.ps1
```

- Git, GitHub CLI, CMake, Ninja, Android platform-tools → **winget** (official packages)
- Qt 6.11.1 MSVC 2022 64-bit (qtbase, qtsvg, qttools, qtdeclarative, qtmultimedia,
  qtshadertools, qtimageformats) → `scripts\install-qt.py` downloads the archives from
  **download.qt.io** (the Qt Online Installer's own repository), verifies the published
  SHA-256 sums and extracts to `C:\Qt\6.11.1\msvc2022_64`. Needs Python 3 + `pip install py7zr`.
  You can instead use the Qt Online Installer / Maintenance Tool (component
  `qt.qt6.6111.win64_msvc2022_64` + Qt Multimedia).
- Visual Studio (Community or Build Tools) with *Desktop development with C++* must be
  installed manually (the script prints the winget command). Any VS 2022/2026 works — the
  Qt msvc2022 kit is ABI-compatible.

**MinGW does not work**: FFmpeg ships MSVC import libraries and the project builds with `/WX`.

## 3. Build

```powershell
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force   # a running exe locks the link step
cmd /c scripts\build.bat            # RelWithDebInfo
cmd /c scripts\build.bat debug      # Debug preset
```

`build.bat` runs `vcvars64.bat` (found through `vswhere`), picks Qt from `%QT_ROOT%`
→ `C:\Qt\6.11.1\msvc2022_64` → any `C:\Qt\6.*\msvc*_64`, Ninja/CMake from Qt Tools → the
Visual Studio bundled copies → `PATH`, then `cmake --preset msvc-x64` + `cmake --build`.
Success prints `[build] BUILD_OK`. Output:

```
output\x64\RelWithDebInfo\QtScrcpy.exe      (+ FFmpeg DLLs, adb.exe, scrcpy-server copied by CMake)
output\x64\RelWithDebInfo\tests\tst_*.exe   (unit tests, Qt runtime deployed automatically)
```

Qt runtime DLLs for the main exe are deployed once with `windeployqt`
(`scripts\run-farm.ps1` does it when `Qt6Core.dll` is missing):

```powershell
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe --no-translations output\x64\RelWithDebInfo\QtScrcpy.exe
```

## 4. Run / test / package

```powershell
.\scripts\run-farm.ps1                       # QtScrcpy.exe --farm, verifies the process stays alive
cmd /c scripts\test.bat                      # ctest — 11 test binaries
.\scripts\measure-perf.ps1 -Devices 10 -Load # CPU/RAM sampling (see docs\PERFORMANCE_*.md)
.\scripts\package-portable.ps1               # dist\ADBDeviceFarm-portable-<ver>.zip
```

## Troubleshooting

- **`Could not find a package configuration file provided by "Qt6"`** — Qt MSVC kit missing
  or `QT_ROOT` wrong; re-run `check-environment.ps1`.
- **`cl.exe is not recognized`** — Visual Studio C++ workload missing; `build.bat` prints the
  installation it found (or did not).
- **`LNK1168: cannot open ...QtScrcpy.exe`** — the app is running; kill it first.
- **App exits immediately** — Qt DLLs not deployed (see step 3) or a plugin missing
  (`platforms\qwindows.dll`, `sqldrivers\qsqlite.dll`).
- **Windows Firewall prompt for adb.exe** — allow on private networks; the farm only needs
  localhost + LAN.
