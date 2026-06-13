# ADB Device Farm — v2.0 (build & run for AI agents)

Native **C++ / Qt 6** app, a fork of [QtScrcpy](https://github.com/barry-ran/QtScrcpy)
(Apache-2.0). It's a low-latency, multi-device Android **farm dashboard**. Branch: `v2.0`.

## Toolchain (already installed on this machine)

- **Qt 6.11.1** MSVC kit: `C:\Qt\6.11.1\msvc2022_64`
- **MSVC** from Visual Studio 2022 (`vcvars64.bat`)
- **CMake + Ninja** bundled with Qt: `C:\Qt\Tools\CMake_64\bin`, `C:\Qt\Tools\Ninja`
- adb: `C:\platform-tools\adb.exe` (the core also bundles its own under
  `QtScrcpy/QtScrcpyCore/src/third_party`)

If the Qt MSVC kit is missing, add it via `C:\Qt\MaintenanceTool.exe`
(component `qt.qt6.6111.win64_msvc2022_64` + `Qt Multimedia`). MinGW will NOT work
(FFmpeg links MSVC import libs; CMake uses `/WX`).

## Build

**Always kill any running instance first** — the running `.exe` locks the output and the
link step fails with `LNK1168`.

PowerShell (what the agents use):

```powershell
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force
& cmd /c "C:\adb-device-farm\scripts\build.bat"
```

`scripts\build.bat` runs `vcvars64.bat`, puts the bundled CMake/Ninja on PATH, then
`cmake --preset msvc-x64` + `cmake --build --preset msvc-x64`. On success it prints
`[build] BUILD_OK`. Binary: `output\x64\RelWithDebInfo\QtScrcpy.exe`.

> The two lines `'v2.0' is not recognized` / `vswhere.exe is not recognized` during the
> build are harmless (upstream's version-stamp script); ignore them.

## Run

```powershell
$exe = "C:\adb-device-farm\output\x64\RelWithDebInfo\QtScrcpy.exe"
Start-Process -FilePath $exe -ArgumentList "--farm" -WorkingDirectory (Split-Path $exe)
```

- **`--farm`** → the v2.0 device-farm dashboard (the product). Opens maximized.
- no flag → the original upstream QtScrcpy single-device UI.

**Qt runtime DLLs:** the build copies FFmpeg/adb/scrcpy-server next to the exe, but Qt DLLs
need `windeployqt` **once** after a clean `output/`:

```powershell
C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe output\x64\RelWithDebInfo\QtScrcpy.exe
```

To verify a change without the GUI, launch with `-PassThru`, `Start-Sleep 3`, then check the
process is still alive (a crash/`LNK`/missing-DLL exits immediately).

## Where things are

- `QtScrcpy/farm/` — the v2.0 dashboard (our code):
  - `farmwindow.*` — control panel, device grid, marquee selection, numbered selector,
    groups, WiFi connect, batched connect queue.
  - `devicetile.*` — one grid tile (YUV OpenGL render + overlay number/model/IP). Tiles are
    `WA_TransparentForMouseEvents` so the grid background drives rubber-band selection.
  - `focuspanel.*` — embedded "host mode" (double-click a tile); broadcasts input to the
    host + current selection.
- `QtScrcpy/QtScrcpyCore/` — the upstream core (FFmpeg decode → YUV → OpenGL, ADB, server).
  `qsc::` API in `include/QtScrcpyCore.h`: `IDeviceManage`, `IDevice`, `DeviceObserver`.
  We added `IDevice::replayLastFrame()` so a freshly-attached view paints immediately.
- `docs/BUILD_WINDOWS.md`, `docs/ARCHITECTURE_V2.md`, `UPSTREAM.md` — details & provenance.

## Conventions / gotchas

- CMake builds with **`/W3 /WX`** (warnings = errors). Cast `qsizetype`→`int`, avoid unused
  vars, etc.
- `main.cpp` sets `Qt::AA_ShareOpenGLContexts` — required so dozens of per-device
  `QOpenGLWidget`s don't exhaust GPU contexts.
- Each device connection needs a **unique `scid` and `localPort`** (see
  `FarmWindow::startConnect`) or simultaneous connections collide on port 27183.
- Commit messages end with the Co-Authored-By trailer; commit/push only when asked.

## Working with multiple Claude Code instances (Git Worktrees)

To work on multiple features simultaneously without conflicts, use **git worktrees**. Each worktree has its own independent `build/` and `output/` directories.

### Setup worktrees

```bash
# List current worktrees
git worktree list

# Create a worktree for a feature branch
git worktree add ../adb-device-farm-feature feature/my-feature

# Now you have:
# C:\adb-device-farm\          <- main worktree (main branch)
# C:\adb-device-farm-feature\  <- feature worktree (feature/my-feature branch)
```

### Using worktrees

**Claude instance 1** works in `C:\adb-device-farm` (main):
```powershell
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force
& cmd /c "C:\adb-device-farm\scripts\build.bat"
$exe = "C:\adb-device-farm\output\x64\RelWithDebInfo\QtScrcpy.exe"
Start-Process -FilePath $exe -ArgumentList "--farm" -WorkingDirectory (Split-Path $exe)
```

**Claude instance 2** works in `C:\adb-device-farm-feature` (feature branch):
```powershell
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force
& cmd /c "C:\adb-device-farm-feature\scripts\build.bat"
$exe = "C:\adb-device-farm-feature\output\x64\RelWithDebInfo\QtScrcpy.exe"
Start-Process -FilePath $exe -ArgumentList "--farm" -WorkingDirectory (Split-Path $exe)
```

### Benefits

- ✅ Both instances can compile simultaneously without conflicts
- ✅ Both executables can run at the same time
- ✅ Changes in one worktree don't affect the other
- ✅ Each has independent `build/` and `output/` directories

### Cleanup

```bash
# Remove a worktree when done
git worktree remove ../adb-device-farm-feature

# Prune stale worktree references
git worktree prune
```
