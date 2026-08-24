# ADB Device Farm

**Self-hosted control center for tens or hundreds of Android devices from one
Windows PC** — live mirroring, automatic LAN discovery, persistent reconnect,
always-awake policy, batch management and a no-code automation studio. Native
C++ / Qt 6, built on the open-source [QtScrcpy](https://github.com/barry-ran/QtScrcpy)
+ [scrcpy](https://github.com/Genymobile/scrcpy) pipeline. No subscription, no
cloud, no Electron.

```
Android device → scrcpy-server (MediaCodec H.264) → ADB → FFmpeg decode → YUV → OpenGL tile
```

## What it does

- **Discovery on every start** — `adb devices`, adb mDNS, the known-device registry, an
  asynchronous probe of the LAN (default `192.168.100.1-254`, port 5555) and ARP
  neighbours. Devices appear as they answer; auto-connect and auto-mirror are on by default.
- **Persistent registry** (SQLite) — serial, IP, model, Android version, friendly name,
  farm number, group, favourite, notes, per-device quality/keep-awake preferences, last seen.
- **Reliable sessions** — bounded connect queue, unique `scid`/port per session, start
  watchdogs, automatic reconnect with exponential backoff (1 → 60 s), reboot tracking.
- **Keep awake** — `svc power stayon` + `stay_on_while_plugged_in` + max screen timeout,
  verified after applying, re-applied after reconnect/reboot, sleeping displays woken
  (`KEYCODE_WAKEUP`); lock screens are reported, never bypassed.
- **Devices page** — grid with Tiny/Compact/Normal/Large tiles, list view, search, filters
  (online, offline, mirroring, favourites, recently offline/connected, problems), sorting
  (numeric IP), click/Ctrl/Shift/rubber-band selection, host (MASTER) mode that broadcasts
  touch/keyboard/gestures/navigation to followers with Raw / Normalized / Aspect-aware
  coordinate mapping.
- **Batch actions** on any selection with progress, per-device results and *Retry failed*:
  screenshots (`YYYY-MM-DD_HH-MM-SS_DEVICE.png`), screen recording, text & clipboard
  (templates), APK install (also drag-and-drop), file upload/download/delete, launch /
  force-stop / clear data / permissions, keep-awake, reboot, bulk settings profiles
  (timeout, brightness, volume, animation scale, resolution/DPI, orientation, WiFi/BT).
- **Groups** — create/rename/delete/colour, drag devices between groups, per-group
  keep-awake and quality profile.
- **ADB Console** — one device / selection / group / all, per-device stdout/stderr, history,
  saved presets by category.
- **Automation Studio** — visual node editor (drag, connect, undo/redo, copy/paste, zoom),
  50+ node types (tap/swipe/text/keys, apps, waits, if/loop/while/switch, variables, adb,
  screenshot, image matching, OCR, UI-hierarchy selectors, files, logging), per-node
  retries/timeouts/on-failure, parallel device execution with concurrency, pause/stop/retry,
  structured logs and error screenshots, macro recorder, import/export packages, optional
  natural-language generation through a **local** model (Ollama) — never required.
- **Scheduler** — once / every N minutes / hourly / daily / weekly / weekdays / on app start /
  on device events (connected, reconnected, battery, temperature) with missed-run policies.
- **Activity Center, Performance page** (CPU, RAM, decoded/rendered/dropped fps,
  decode→display latency, ADB ops/s, connect latency, reconnects, UI lag) and a one-click
  diagnostics bundle; desktop notifications per category.

## Install

**Portable**: download/unzip `ADBDeviceFarm-portable-<version>.zip` (or build it with
`scripts\package-portable.ps1`) and run `ADB Device Farm.cmd`. Settings, database and logs
stay in `.\data`. **Installer**: `installer\adb-device-farm.iss` (Inno Setup 6).

Requirements: Windows 10/11 x64, OpenGL 2.0 (a software fallback ships), phones with USB
debugging (USB) and/or *ADB over network* on port 5555 (WiFi).

## Development setup

```powershell
git clone https://github.com/duarteenc/adb-device-farm.git
cd adb-device-farm
git submodule update --init --recursive
.\scripts\check-environment.ps1      # what is missing?
.\scripts\bootstrap.ps1              # installs Git/gh/CMake/Ninja/platform-tools via winget and the Qt MSVC kit from download.qt.io
cmd /c scripts\build.bat             # [build] BUILD_OK
cmd /c scripts\test.bat              # unit tests (CTest)
.\scripts\run-farm.ps1               # windeployqt once + QtScrcpy.exe --farm
```

Details: [`docs/BUILD_WINDOWS.md`](docs/BUILD_WINDOWS.md), [`CLAUDE.md`](CLAUDE.md) (map of the code).

## Connecting devices

- **USB**: plug in, accept the RSA prompt on the phone. *USB → WiFi ADB (selected)* switches
  a phone to `adb tcpip 5555` and connects to its WiFi IP.
- **WiFi**: enable *ADB over network* (or `adb tcpip 5555` once) — the farm finds the phone
  on the next LAN sweep. Change the subnet/port in *Settings › Device Discovery*.
- Command line: `QtScrcpy.exe --scan`, `QtScrcpy.exe --list-devices`.

See [`docs/DEVICE_DISCOVERY.md`](docs/DEVICE_DISCOVERY.md), [`docs/KEEP_AWAKE.md`](docs/KEEP_AWAKE.md),
[`docs/AUTOMATION.md`](docs/AUTOMATION.md), [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md),
[`docs/ARCHITECTURE_V3.md`](docs/ARCHITECTURE_V3.md), performance before/after in
[`docs/PERFORMANCE_BASELINE.md`](docs/PERFORMANCE_BASELINE.md) / [`docs/PERFORMANCE_FINAL.md`](docs/PERFORMANCE_FINAL.md).

## Boundaries

This is a device **management and testing** tool for devices you own. It does not and will
not implement lock-screen bypass, identity/fingerprint spoofing, CAPTCHA solving, account
automation or any anti-detection behaviour.

## License

Apache License 2.0 — see [`LICENSE`](LICENSE). Derived from QtScrcpy and scrcpy; provenance
and the (small) core divergence are documented in [`UPSTREAM.md`](UPSTREAM.md); third-party
components in [`docs/THIRD_PARTY.md`](docs/THIRD_PARTY.md).
