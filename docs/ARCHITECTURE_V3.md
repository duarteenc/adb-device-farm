# Architecture v3 — ADB Device Farm control center

v3 keeps the proven native pipeline of v2 (QtScrcpy core: scrcpy-server →
ADB → FFmpeg → YUV → OpenGL) and replaces the single 2,800-line `FarmWindow`
with a service layer (`farmcore`, a static library without widgets) and a
multi-page UI on top of it. Everything that can block runs off the GUI thread.

```
┌────────────────────────────── QtScrcpy.exe ──────────────────────────────┐
│  ui/  (Qt Widgets)                                                        │
│   FarmMainWindow ── nav ── Dashboard · Devices · Groups · Automations ·    │
│                          Scheduler · Applications · Files · ADB Console · │
│                          Activity · Performance · Settings                │
│   DeviceGrid / DeviceTile / FocusPanel / WorkflowEditor / dialogs         │
├───────────────────────────── farmcore (static lib) ──────────────────────┤
│ core/       FarmSettings · FarmLog (rotating) · ActivityLog · TaskExecutor │
│             (bounded lanes) · BatchJob/JobManager · AppContext (startup)   │
│ adb/        AdbExecutor (own thread, bounded, timeouts, cancel, metrics)  │
│             parsers · shell quoting                                       │
│ storage/    Database (SQLite, WAL, migrations) · repositories             │
│ devices/    DeviceRegistry (state machine + persistence) · DeviceService   │
│             (mirror lifecycle, queue, reconnect backoff, profiles)        │
│             ConnectionIdAllocator (scid + port leases) · KeepAwakeManager │
│             DeviceHealthMonitor · DeviceCommands (batch operations)       │
│ discovery/  NetworkScanner (async TCP probe thread) · DiscoveryService    │
│             (adb devices, mDNS, known devices, ARP, subnet sweep)         │
│ automation/ Workflow model + validator · NodeCatalog · Expression ·       │
│             WorkflowEngine/AutomationRun · ImageMatcher (NCC) ·           │
│             OcrProvider (Windows OCR) · UiHierarchy · MacroRecorder ·     │
│             AiProvider (optional Ollama / OpenAI-compatible)              │
│ scheduler/  Scheduler (time + event triggers, missed-run policies)        │
│ performance/PerfMonitor (CPU/RAM/fps/latency/ADB/UI-lag snapshots)        │
│ mock/       MockDeviceProvider (--mock-devices N, clearly separated)      │
├──────────────────────────── QtScrcpyCore (upstream) ─────────────────────┤
│ IDeviceManage · IDevice · DeviceObserver · Server · Demuxer · Decoder ·   │
│ VideoBuffer (latest-frame-wins double buffer) · Controller               │
└───────────────────────────────────────────────────────────────────────────┘
```

## Threads

| Thread | Owner | Work |
| --- | --- | --- |
| GUI | Qt | widgets, QtScrcpy core objects (not thread-safe), registry, services' state machines |
| `adb-executor` | `AdbExecutor` | every external `adb.exe` (`QProcess`), FIFO with a concurrency cap (Settings › ADB) |
| `net-scanner` | `NetworkScanner` | bounded parallel TCP probes with per-socket timeouts |
| lane `automation` (pool) | `TaskExecutor` | one worker per running device run (`AutomationRun`), synchronous adb via `runSync` |
| lane `media` / `io` / `network` | `TaskExecutor` | screenshot encoding, file writes, ARP |
| per device: demuxer thread | QtScrcpyCore | H.264 receive + decode (unchanged) |

Nothing on the GUI thread waits on a device. Callbacks from the executor land
on the caller's thread through `QMetaObject::invokeMethod(context, …)`.

## Startup sequence (`AppContext`)

1. `FarmSettings` (INI in `%APPDATA%\ADBDeviceFarm`, or `.\data` in portable mode)
2. `FarmLog` (rotating file log, crash marker) and `Database` (SQLite + migrations)
3. `AdbExecutor::start()` (adb path resolution: Settings → beside the exe → env → PATH)
4. `DeviceRegistry::load()` (known devices appear immediately as *Offline*)
5. window shown — then `startServices()`:
6. `DeviceService` (mirror queue, reconnect), 7. `KeepAwakeManager`,
8. `DeviceHealthMonitor`, 9. `DeviceDiscoveryService` (quick refresh now, LAN
   sweep 300 ms later), 10. `Scheduler` (missed-run policies, appStart triggers),
11. `PerfMonitor`.

## Device state machine (`DeviceRegistry`)

`Unknown → Discovered → Connecting → AdbOnline → Mirroring`, with
`Unauthorized`, `Offline`, `Error`, `Reconnecting`, `Busy` (automation). Every
transition is logged, emitted (`stateChanged`) and — for online transitions —
persisted (`last_seen`). `DeviceService` owns `Connecting/Mirroring`;
`DeviceDiscoveryService` owns `Discovered/AdbOnline/Unauthorized/Offline`.

## Mirror session lifecycle (`DeviceService`)

```
startMirror(id) ─► queue (max N concurrent starts) ─► normalise wm size (async, 8 s)
   ─► ConnectionIdAllocator lease {scid, localPort} ─► IDeviceManage::connectDevice
   ─► watchdog (Settings › mirror start timeout) ─► deviceConnected ─► Mirroring
   │                                                    │
   └─ failure → Error + scheduleReconnect (1,2,5,10,30,60 s backoff) ◄─ deviceDisconnected
```

Operator *Stop* marks the device "operator stopped" (no auto reconnect until
*Mirror* again). *Reboot* keeps the wanted-flag and reconnects when adb sees
the device again.

## Rendering path and throttling

`Decoder::onNewFrame` (GUI thread) → `Device` observers → `DeviceTile::onFrame`
→ `QYUVOpenGLWidget::updateTextures` (now ONE `makeCurrent` per frame instead
of three) → `paintGL`. `DeviceGrid` assigns a render priority from the scroll
viewport: **Focused/Visible** upload every frame, **Offscreen** (one screen
  A tile allocates its OpenGL surface on its first frame inside the viewport; off-screen tiles keep
  the placeholder until they scroll in (hundreds of tiles never create hundreds of GL contexts).
away) upload at Settings › Performance › off-screen fps, **Hidden** upload
nothing. Decoding continues everywhere so input never stalls; a stale tile that
scrolls back into view gets `replayLastFrame()` immediately. The core's
`VideoBuffer` already drops frames that were not consumed (latest-frame-wins).

## Data

SQLite (`farm.db`, WAL): `devices`, `groups`, `saved_commands`, `text_templates`,
`workflows`, `schedules`, `job_runs`, `job_logs`, `activity`, `kv`,
`schema_version`. Migrations are numbered and applied inside transactions;
a newer schema than the build supports is refused, never truncated.

## Where to look

| Need | File |
| --- | --- |
| add a setting | `core/farmsettings.h` (+ `ui/pages/settingspage.cpp`) |
| add a batch device action | `devices/devicecommands.*` (returns a `BatchJob`) |
| add a workflow node | `automation/nodecatalog.cpp` (spec) + `automation/workflowengine.cpp` (`executeNode`) |
| add a page | `ui/pages/*` + `ui/farmmainwindow.cpp` (`kNav`) |
| change discovery | `discovery/devicediscoveryservice.cpp` |
| change reconnect policy | `devices/deviceservice.cpp` (`kBackoffSeconds`) |
