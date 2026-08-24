# ADB Device Farm — v3 (build, run, test, extend — for AI agents)

Native **C++17 / Qt 6** Windows app, a fork of [QtScrcpy](https://github.com/barry-ran/QtScrcpy)
(Apache-2.0). It is a self-hosted Android **device farm control center**: LAN
discovery, persistent registry + reconnect, keep-awake, batch management, and a
no-code automation studio. Branch: `feat/ultimate-device-farm` (base `main`).

## Toolchain — detected, never assumed

```powershell
.\scripts\check-environment.ps1     # PASS/FAIL per component (Git, gh, MSVC, Qt, CMake, Ninja, ADB, FFmpeg, OpenGL)
.\scripts\bootstrap.ps1             # installs what can be automated (winget + official Qt archives via scripts\install-qt.py)
```

`scripts\build.bat` auto-detects: Visual Studio (any edition, via `vswhere`),
the Qt MSVC kit (`%QT_ROOT%` → `C:\Qt\6.11.1\msvc2022_64` → any `C:\Qt\6.*\msvc*_64`),
Ninja/CMake (Qt Tools → VS bundled → PATH). MinGW will NOT work (FFmpeg MSVC libs, `/WX`).
On this machine: VS 18 Community (MSVC 14.51), Qt 6.11.1 at `C:\Qt\6.11.1\msvc2022_64`
(installed with `scripts\install-qt.py`), VS-bundled CMake/Ninja, adb from the
Android SDK (`C:\Program Files (x86)\Android\android-sdk\platform-tools`) — the
app itself uses the `adb.exe` copied next to the exe.

## Build / run / test

```powershell
Get-Process QtScrcpy -ErrorAction SilentlyContinue | Stop-Process -Force   # the running exe locks the link step (LNK1168)
cmd /c scripts\build.bat            # cmake --preset msvc-x64 + build; prints [build] BUILD_OK
cmd /c scripts\test.bat             # ctest (11 Qt Test binaries under output\x64\RelWithDebInfo\tests)
.\scripts\run-farm.ps1              # windeployqt once, then QtScrcpy.exe --farm (checks it stays alive)
.\scripts\measure-perf.ps1 -Devices 10 -Load -Seconds 30   # CPU/RAM sampling with screen-motion load
.\scripts\package-portable.ps1      # dist\ADBDeviceFarm-portable-<ver>\ + .zip
```

Binary: `output\x64\RelWithDebInfo\QtScrcpy.exe`. CLI: `--farm` (control center),
`--list-devices`, `--scan`, `--run-workflow "Name" [--targets …]`,
`--farm --mock-devices 100` (simulated devices, no ADB), `--data-dir PATH`, `--help`.
No arguments = the original upstream single-device UI.

Data lives in `%APPDATA%\ADBDeviceFarm` (`settings.ini`, `farm.db`, `logs\`,
`automation-runs\`), or `.\data` when a file named `portable` sits next to the exe.

## Where things are (`QtScrcpy/farm/`)

- `core/` — `FarmSettings` (typed INI settings + `changed(key)`), `FarmLog` (rotating log + crash marker),
  `ActivityLog` (event feed), `TaskExecutor` (bounded lanes: adb/connect/network/automation/media/io),
  `BatchJob`+`JobManager` (bounded batch ops, retry-failed), `AppContext` (startup order §87).
- `adb/` — `AdbExecutor` (own thread, FIFO with concurrency cap, timeouts, cancel, metrics; `run` async /
  `runSync` for workers), `adbparsers` (devices -l, mdns, dumpsys, pm, ls, arp…), `adbquote` (shell quoting).
- `storage/` — `Database` (SQLite WAL, numbered migrations in `kMigrations`), `repositories` (devices, groups,
  saved commands, templates, workflows, schedules, runs/logs, activity, kv).
- `devices/` — `DeviceRegistry` (source of truth: records, states, groups, numbering, sorting/search, debounced
  persistence), `DeviceService` (mirror queue, scid/port leases, watchdog, reconnect backoff 1-2-5-10-30-60 s,
  quality profiles), `ConnectionIdAllocator`, `KeepAwakeManager`, `DeviceHealthMonitor`, `DeviceCommands`
  (screenshot/record/apk/files/text/settings as `BatchJob`s).
- `discovery/` — `NetworkScanner` (async TCP probe thread), `DeviceDiscoveryService` (adb devices, mDNS, known
  devices, ARP, subnet sweep, bounded `adb connect`, adaptive rescan).
- `automation/` — `workflowmodel` (JSON + validator), `nodecatalog` (node specs), `expression` (`${var}`,
  conditions), `workflowengine` (`AutomationRun` per-device workers), `imagematcher` (NCC), `ocrprovider`
  (Windows OCR via C++/WinRT), `uihierarchy` (uiautomator selectors), `macrorecorder`, `aiprovider` (optional).
- `scheduler/` — `Scheduler` (time + event triggers, missed-run policies, no run stacking).
- `performance/` — `PerfMonitor` (CPU/RAM/decoded-rendered-dropped fps/latency/ADB/UI lag).
- `mock/` — `MockDeviceProvider` (`--mock-devices N`, synthetic frames, separate from production).
- `ui/` — `FarmMainWindow` (nav + pages), `DeviceGrid`/`DeviceTile` (manual flow layout, selection, viewport
  render priorities), `FocusPanel` (master/follower + coordinate modes + macro hooks), `pages/*`,
  `widgets/*` (batch job dialog, text/templates, device inspector), `automation/workfloweditor` (node editor).
- `QtScrcpy/QtScrcpyCore/` — upstream core (unchanged except `replayLastFrame`); `QtScrcpy/render/` — YUV GL
  widget (one `makeCurrent` per frame + `setOnPainted`). Divergence list: `UPSTREAM.md`.
- Docs: `docs/ARCHITECTURE_V3.md`, `DEVICE_DISCOVERY.md`, `KEEP_AWAKE.md`, `AUTOMATION.md`,
  `PERFORMANCE_BASELINE.md`, `PERFORMANCE_FINAL.md`, `TROUBLESHOOTING.md`, `THIRD_PARTY.md`.

## How the important flows work

- **Discovery**: `DeviceDiscoveryService::start()` → `adb devices -l` every 4 s + full subnet probe (default
  `192.168.100.0/24`, hosts .1–.254) every 45 s (adaptive). Hosts answering on 5555 → `Discovered` →
  bounded `adb connect` → `AdbOnline` → `DeviceService` auto-mirrors (Settings › Discovery).
- **Reconnect**: core `deviceDisconnected` or `adb devices` drop → `DeviceService::scheduleReconnect` with
  backoff; operator *Stop* disables it for that device; keep-awake re-applied on every online transition.
- **Mirror start**: queue (max concurrent starts) → optional `wm size/density` normalisation → unique
  `{scid, localPort}` lease → `IDeviceManage::connectDevice` → watchdog → `Mirroring`.
- **Rendering**: `DeviceTile::onFrame` uploads only Visible/Focused tiles every frame; Offscreen at
  `perf/offscreenFps`; Hidden never; `replayLastFrame` when scrolled back. Decoding never stops.
- **Automation**: `WorkflowEngine::start(workflow, targets, concurrency)` → `AutomationRun` → one worker per
  device on the `automation` lane (`AdbExecutor::runSync`), retries/timeouts per node, error screenshot,
  logs to DB + `automation-runs/<run>/logs.json`. Nodes are dispatched in `executeNode()`.
- **Migrations**: add `kMigration00N[]` + entry in `kMigrations` (`storage/database.cpp`); never drop data;
  a newer schema than the build is refused.

## Conventions / gotchas

- `/W3 /WX` everywhere: cast `qsizetype`→`int`, no unused vars, `[[nodiscard]]` results must be used.
- `farmcore` is compiled as C++20 (C++/WinRT coroutines); consumers only need C++17.
- Never touch `qsc::IDeviceManage`/`IDevice` off the GUI thread; use `AdbExecutor` for blocking work.
- Worker threads read device records through `DeviceRegistry::snapshot(id)` (mutex-protected copy), never
  `get()`; settings reads are serialised inside `FarmSettings`; each thread gets its own SQLite connection
  (`Database::connection()`, closed with the thread).
- `AdbExecutor::run(cmd, context, cb)`: pass the receiver as `context` — the callback is dropped if it dies;
  `nullptr` means "call inline on the executor thread" and the lambda must not capture `this`.
- An `AutomationRun` is deleted through `deleteWhenIdle()` only after its last worker reported back;
  `WorkflowEngine::shutdown()` runs before the executors stop.
- Every device operation needs a timeout + cancellation (`CancellationToken`) — see `AdbCommand::timeoutMs`.
- Bulk destructive actions ask for confirmation with the device count (`DevicesPage::confirmBulk`).
- Tests live in `tests/` (Qt Test, `farm_add_test` in `tests/CMakeLists.txt`); `tst_workflow` runs the
  engine without ADB using logic-only nodes.
- Commit messages: small logical `type(scope): summary` commits ending with the Co-Authored-By trailer.

## Branches

`main` is canonical (v2.0 farm). This work is on `feat/ultimate-device-farm`. Use worktrees for parallel
agents (`git worktree add ../adb-device-farm-<name> -b fix/<name> main`); each has its own `build/` and
`output/`. Kill `QtScrcpy.exe` before building.
