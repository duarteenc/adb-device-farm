# Building on Windows (Qt 6 + MSVC)

This is a native C++/Qt app built with **MSVC x64 + Qt 6**. The notes below reflect the
toolchain actually installed on this machine.

## What's already installed ✅

| Component | Location |
| --- | --- |
| MSVC compiler | Visual Studio **Build Tools 2026** |
| Qt | **6.11.1** at `C:\Qt\6.11.1` |
| CMake (bundled with Qt) | `C:\Qt\Tools\CMake_64\bin\cmake.exe` |
| Ninja (bundled with Qt) | `C:\Qt\Tools\Ninja\ninja.exe` |
| FFmpeg (MSVC import libs) | `QtScrcpy\QtScrcpyCore\src\third_party\ffmpeg\lib\x64` |
| adb | `C:\platform-tools\adb.exe` (core also bundles its own) |

## The one missing piece ⚠️ — Qt's MSVC kit

Qt 6.11.1 is installed with the **MinGW** kit only. Qt libraries are ABI-specific to the
compiler, so MinGW Qt cannot link against MSVC. Add the MSVC kit:

1. Run **`C:\Qt\MaintenanceTool.exe`** → *Add or remove components*.
2. Expand **Qt → Qt 6.11.1** and check **"MSVC 2022 64-bit"**.
   *(Component id: `qt.qt6.6111.win64_msvc2022_64`.)*
3. Install. You'll get `C:\Qt\6.11.1\msvc2022_64`.

> Qt's `msvc2022_64` binaries are ABI-compatible with the VS Build Tools **2026** compiler
> (MSVC keeps a stable ABI across 2015–2022+), so this kit works with what you have.

The Windows build path of QtScrcpyCore links FFmpeg from `lib/x64` (MSVC `.lib` files) and
upstream's CI only tests MSVC on Windows — hence MSVC, not MinGW.

---

## Building

Open the **"x64 Native Tools Command Prompt for VS 2026"** from the Start menu (this puts
`cl.exe` on PATH). Then, at the repo root:

```bat
:: Use the CMake + Ninja bundled with Qt
set PATH=C:\Qt\Tools\CMake_64\bin;C:\Qt\Tools\Ninja;%PATH%

:: Configure + build via the provided preset (points at C:\Qt\6.11.1\msvc2022_64)
cmake --preset msvc-x64
cmake --build --preset msvc-x64
```

Or the explicit form, if you prefer:

```bat
cmake -S . -B build -G Ninja ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64

cmake --build build
```

The binary is written to `output\x64\RelWithDebInfo\QtScrcpy.exe`. The CMake post-build
step already copies the FFmpeg DLLs, `adb.exe`, and `scrcpy-server` next to it.

### Qt runtime DLLs

The `.exe` still needs the Qt DLLs. Deploy them once with `windeployqt`:

```bat
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe output\x64\RelWithDebInfo\QtScrcpy.exe
```

Then run `output\x64\RelWithDebInfo\QtScrcpy.exe`.

---

## First milestone

Build the **unmodified** upstream and confirm a single USB device mirrors with low
latency. Once that's green, we start the v2.0 farm extension (see
[`ARCHITECTURE_V2.md`](ARCHITECTURE_V2.md), phase 2).

## Troubleshooting

- **`Could not find a package configuration file provided by "Qt6"`** — the MSVC kit isn't
  installed yet, or `CMAKE_PREFIX_PATH` doesn't point at `C:\Qt\6.11.1\msvc2022_64`.
- **`cl.exe is not recognized`** — you're not in the *x64 Native Tools Command Prompt*.
- **`'cmake'/'ninja' not recognized`** — add the bundled paths to `PATH` (see above) or call
  them by full path.
- **App starts but no devices** — confirm `adb devices` lists your phone and USB debugging is
  authorized on the device.
