# Upstream provenance

This project is a **fork of [QtScrcpy](https://github.com/barry-ran/QtScrcpy)**
by barry-ran, which is itself based on [Genymobile/scrcpy](https://github.com/Genymobile/scrcpy).
Both are licensed under the **Apache License 2.0** (see `LICENSE`). We retain that
license and attribution.

## Vendored commit (base of this fork)

The full QtScrcpy source tree was vendored into this repository (flattened — the
`QtScrcpyCore` submodule was materialized in place) at:

| Component         | Repository                                          | Commit                                     |
| ----------------- | --------------------------------------------------- | ------------------------------------------ |
| QtScrcpy (app)    | `github.com/barry-ran/QtScrcpy`                     | `3e8892649d1a36982197f4fd3664bc1321bb13b2` |
| QtScrcpyCore (lib)| `github.com/barry-ran/QtScrcpyCore`                 | `cef8558255b2f90461dad21c062e240aeba18f23` |

Vendored on 2026-06-12.

## Local divergence from upstream (kept minimal on purpose)

The farm is built **around** the core, not inside it. The core files touched:

| File | Change | Why |
| --- | --- | --- |
| `QtScrcpy/QtScrcpyCore/include/QtScrcpyCore.h`, `src/device/device.*`, `src/device/decoder/*` | `IDevice::replayLastFrame(observer)` + `VideoBuffer::peekRenderedYUV` (v2.0) | a view attached after the last frame (host panel, tile scrolled back into the viewport) paints immediately instead of staying black |
| `QtScrcpy/render/qyuvopenglwidget.*` | `updateTextures()` does ONE `makeCurrent()/doneCurrent()` per frame instead of one per YUV plane; `setOnPainted()` callback after `paintGL()` | with dozens of tiles the per-plane context switches dominated the GUI thread; the callback feeds decode→display latency and rendered-fps metrics to the Performance page |
| `QtScrcpy/QtScrcpyCore/src/device/server/server.*` | reverse-tunnel start reads the device-info header asynchronously (`readyRead` + 3 s timer, `finishReverseStart()`); `readInfo(…, wait)` keeps the blocking loop only for the forward fallback | upstream blocked the GUI thread with `waitForReadyRead(300)` for up to 3 s per connecting device — measured 1–1.5 s event-loop stalls while 78 phones came up |
| `QtScrcpy/CMakeLists.txt` | `GuiPrivate` requested explicitly (Qt 6.11 compat, v2.0); `Sql`, `Concurrent` components; C++17; `add_subdirectory(farm)`; links `farmcore` | build wiring only |
| `QtScrcpy/main.cpp` | `--farm` boots `farm::AppContext` + `FarmMainWindow`; `--list-devices`, `--scan`, `--run-workflow`, `--mock-devices`, `--help` CLI modes; `Qt::AA_ShareOpenGLContexts` (v2.0) | the upstream single-device UI (no arguments) is untouched and still works |

Everything else (discovery, registry, reconnect, keep-awake, automation,
scheduler, UI pages) lives under `QtScrcpy/farm/` and only uses the public
`qsc::` API (`IDeviceManage`, `IDevice`, `DeviceObserver`, `DeviceParams`).

## Pulling upstream changes later

```bash
git remote add upstream https://github.com/barry-ran/QtScrcpy.git
git fetch upstream
git diff 3e8892649d1a36982197f4fd3664bc1321bb13b2 upstream/master -- QtScrcpy/
```

Re-apply the two small core patches above if the affected files changed. The
original upstream READMEs are kept as `README.upstream.md` and
`docs/README_zh.upstream.md` for reference.
